/**
  ******************************************************************************
  * @file    uf4com.c
  * @author  UF4
  * @date    26-6-13 下午1:39
  * @brief   UF4COM V5 构帧、ACK 跟踪和接收入口
  ******************************************************************************
  */
#include "uf4com.h"
#include "uf4com_cmd.h"
#include "uf4com_crc.h"
#include "uf4com_parser.h"

static uf4_tx_fn_t s_tx;
static void *s_tx_user;
static uf4_write_apply_fn_t s_write_apply;
static void *s_write_apply_user;
static uf4_ack_event_fn_t s_ack_event;
static void *s_ack_event_user;
static uf4_frame_rx_fn_t s_frame_rx;
static void *s_frame_rx_user;
static uf4_parser_t s_parser;
static uf4_config_t s_config;
static uf4_request_entry_t s_requests[UF4_MAX_PENDING_REQUESTS];
static uint32_t s_now_ms;

static void UF4_LoadDefaultConfig(uf4_config_t *config)
{
    config->ack_timeout_ms = UF4_DEFAULT_ACK_TIMEOUT_MS;
    config->ack_retries = UF4_DEFAULT_ACK_RETRIES;
}

static uint8_t UF4_ResponseCmdFor(uint8_t cmd)
{
    // 请求表里存的是期望收到的响应命令。这样 ACK 匹配只需要看
    // SEQ + CMD，逻辑简单，查表也快。每个实例只对应一条点对点链路。
    switch(cmd)
    {
        case UF4_CMD_READ_REQ:
            return UF4_CMD_READ_RSP;

        case UF4_CMD_WRITE_REQ:
            return UF4_CMD_WRITE_RSP;

        case UF4_CMD_STREAM_START_REQ:
            return UF4_CMD_STREAM_START_RSP;

        case UF4_CMD_STREAM_STOP_REQ:
            return UF4_CMD_STREAM_STOP_RSP;

        case UF4_CMD_SAVE_REQ:
            return UF4_CMD_SAVE_RSP;

        default:
            return cmd;
    }
}

static uint8_t UF4_IsRequestCmd(uint8_t cmd)
{
    // 只有这些请求命令交给 uf4com_cmd.c 处理。响应帧和 STREAM_DATA
    // 在可选 ACK 匹配之后交给应用层回调。
    switch(cmd)
    {
        case UF4_CMD_READ_REQ:
        case UF4_CMD_WRITE_REQ:
        case UF4_CMD_STREAM_START_REQ:
        case UF4_CMD_STREAM_STOP_REQ:
        case UF4_CMD_SAVE_REQ:
            return 1u;

        default:
            return 0u;
    }
}

static void UF4_ClearRequests(void)
{
    for(uint8_t i = 0u; i < UF4_MAX_PENDING_REQUESTS; i++)
    {
        s_requests[i].active = 0u;
    }
}

static void UF4_NotifyAck(uint8_t seq, uint8_t cmd, uf4_ack_status_t status)
{
    if(s_ack_event != NULL)
    {
        s_ack_event(seq, cmd, status, s_ack_event_user);
    }
}

static uint8_t UF4_AddRequest(uint8_t seq,
                              uint8_t expected_cmd,
                              const uint8_t *frame,
                              uint16_t frame_len)
{
    if(frame == NULL || frame_len > UF4_MAX_FRAME_LEN)
    {
        return 0u;
    }

    for(uint8_t i = 0u; i < UF4_MAX_PENDING_REQUESTS; i++)
    {
        if(s_requests[i].active != 0u && s_requests[i].seq == seq &&
           s_requests[i].cmd == expected_cmd)
        {
            UF4_NotifyAck(seq, expected_cmd, UF4_ACK_DROPPED);
            return 0u;
        }
    }

    for(uint8_t i = 0u; i < UF4_MAX_PENDING_REQUESTS; i++)
    {
        if(s_requests[i].active == 0u)
        {
            // 保存编码后的整帧，而不是只保存用户 payload。重发必须和第一次发送
            // 字节一致，包括 SEQ 和 CRC。
            s_requests[i].seq = seq;
            s_requests[i].cmd = expected_cmd;
            s_requests[i].retries_left = s_config.ack_retries;
            s_requests[i].frame_len = frame_len;
            s_requests[i].last_tx_ms = s_now_ms;
            s_requests[i].timeout_ms = s_config.ack_timeout_ms;
            s_requests[i].active = 1u;

            for(uint16_t j = 0u; j < frame_len; j++)
            {
                s_requests[i].frame[j] = frame[j];
            }

            return 1u;
        }
    }

    UF4_NotifyAck(seq, expected_cmd, UF4_ACK_DROPPED);
    return 0u;
}

static uint8_t UF4_MatchAck(const uf4_frame_t *frame)
{
    if(frame == NULL || (frame->flags & UF4_FLAG_ACK_FRAME) == 0u)
    {
        return 0u;
    }

    for(uint8_t i = 0u; i < UF4_MAX_PENDING_REQUESTS; i++)
    {
        // SEQ 必须回显，CMD 是期望响应命令。
        if(s_requests[i].active != 0u &&
           s_requests[i].seq == frame->seq &&
           s_requests[i].cmd == frame->cmd)
        {
            s_requests[i].active = 0u;
            UF4_NotifyAck(frame->seq, frame->cmd, UF4_ACK_DONE);
            return 1u;
        }
    }

    return 0u;
}

void UF4_Init(uf4_tx_fn_t tx, void *user)
{
    UF4_InitEx(NULL, tx, user);
}

void UF4_InitEx(const uf4_config_t *config, uf4_tx_fn_t tx, void *user)
{
    UF4_LoadDefaultConfig(&s_config);
    if(config != NULL)
    {
        s_config = *config;
    }

    if(s_config.ack_timeout_ms == 0u)
    {
        s_config.ack_timeout_ms = UF4_DEFAULT_ACK_TIMEOUT_MS;
    }

    // 重复初始化会取消旧请求和命令状态。
    s_tx = tx;
    s_tx_user = user;
    s_now_ms = 0u;

    UF4_ClearRequests();
    UF4_ParserInit(&s_parser);
    UF4_ResetCommandState();
}

void UF4_SetWriteApplyCallback(uf4_write_apply_fn_t cb, void *user)
{
    s_write_apply = cb;
    s_write_apply_user = user;
}

void UF4_SetAckEventCallback(uf4_ack_event_fn_t cb, void *user)
{
    s_ack_event = cb;
    s_ack_event_user = user;
}

void UF4_SetFrameRxCallback(uf4_frame_rx_fn_t cb, void *user)
{
    s_frame_rx = cb;
    s_frame_rx_user = user;
}

void UF4_NotifyWriteApplied(void)
{
    if(s_write_apply != NULL)
    {
        s_write_apply(s_write_apply_user);
    }
}

void UF4_Process(void)
{
    // 兼容旧代码的路径。正式固件建议传入真实系统 tick 调 UF4_ProcessMs()；
    // 这里假设调用方每 1 ms 调一次。
    s_now_ms++;
    UF4_ProcessMs(s_now_ms);
}

void UF4_ProcessMs(uint32_t now_ms)
{
    s_now_ms = now_ms;

    for(uint8_t i = 0u; i < UF4_MAX_PENDING_REQUESTS; i++)
    {
        if(s_requests[i].active == 0u)
        {
            continue;
        }

        if((uint32_t)(now_ms - s_requests[i].last_tx_ms) < s_requests[i].timeout_ms)
        {
            continue;
        }

        if(s_requests[i].retries_left == 0u)
        {
            UF4_NotifyAck(s_requests[i].seq,
                          s_requests[i].cmd,
                          UF4_ACK_TIMEOUT);
            s_requests[i].active = 0u;
            continue;
        }

        if(s_tx != NULL)
        {
            // retries_left 统计的是重发次数，不包含第一次发送。所以值为 3 表示：
            // 第一次发送 + 最多 3 次重发。
            s_tx(s_requests[i].frame, s_requests[i].frame_len, s_tx_user);
        }

        s_requests[i].retries_left--;
        s_requests[i].last_tx_ms = now_ms;
    }

    UF4_StreamTask();
}

uint16_t UF4_BuildFrame(uint8_t seq,
                        uint8_t flags,
                        uint8_t cmd,
                        const uint8_t *data,
                        uint8_t len,
                        uint8_t *out,
                        uint16_t out_size)
{
    const uint16_t frame_len = (uint16_t)(UF4_V5_HEADER_LEN + len + UF4_CRC_LEN);

    if(out == NULL || out_size < frame_len || (data == NULL && len > 0u))
    {
        return 0u;
    }

    out[0] = UF4_SOF1;
    out[1] = UF4_SOF2;
    out[2] = seq;
    out[3] = flags;
    out[4] = cmd;
    out[5] = len;

    for(uint8_t i = 0u; i < len; i++)
    {
        out[6u + i] = data[i];
    }

    const uint16_t crc = UF4_CRC16(&out[2], (uint16_t)(4u + len));
    out[6u + len] = (uint8_t)(crc & 0xFFu);
    out[7u + len] = (uint8_t)(crc >> 8);

    return frame_len;
}

uint8_t UF4_SendFrame(uint8_t seq,
                      uint8_t flags,
                      uint8_t cmd,
                      const uint8_t *data,
                      uint8_t len)
{
    uint8_t buffer[UF4_MAX_FRAME_LEN];
    const uint16_t frame_len = UF4_BuildFrame(seq, flags, cmd, data, len,
                                              buffer, sizeof(buffer));

    if(frame_len == 0u || s_tx == NULL)
    {
        return 0u;
    }

    if((flags & UF4_FLAG_ACK_REQ) != 0u && (flags & UF4_FLAG_ACK_FRAME) == 0u)
    {
        if(UF4_AddRequest(seq, UF4_ResponseCmdFor(cmd), buffer, frame_len) == 0u)
        {
            return 0u;
        }
    }

    s_tx(buffer, frame_len, s_tx_user);
    return 1u;
}

uint8_t UF4_InputByte(uint8_t byte)
{
    uf4_frame_t frame;

    if(UF4_ParserInput(&s_parser, byte, &frame) == 0u)
    {
        return 0u;
    }

    if((frame.flags & UF4_FLAG_ACK_FRAME) != 0u)
    {
        // ACK 匹配是协议层内部事务。帧仍然会继续送给应用层，
        // 这样 READ_RSP / WRITE_RSP 的载荷不会被吞掉。
        (void)UF4_MatchAck(&frame);

        if(s_frame_rx != NULL)
        {
            s_frame_rx(&frame, s_frame_rx_user);
        }

        return 1u;
    }

    if(UF4_IsRequestCmd(frame.cmd) != 0u)
    {
        UF4_Dispatch(&frame);
    }
    else if(s_frame_rx != NULL)
    {
        s_frame_rx(&frame, s_frame_rx_user);
    }

    return 1u;
}

void UF4_ResetParser(void)
{
    UF4_ParserInit(&s_parser);
}

uint8_t UF4_IsInputIdle(void)
{
    return s_parser.state == WAIT_SOF1 ? 1u : 0u;
}

uint8_t UF4_InputBuffer(const uint8_t *data, uint16_t len)
{
    uint8_t frames = 0u;

    if(data == NULL)
    {
        return 0u;
    }

    for(uint16_t i = 0u; i < len; i++)
    {
        frames += UF4_InputByte(data[i]);
    }

    return frames;
}
