/**
  ******************************************************************************
  * @file    uf4com_cmd.c
  * @author  UF4
  * @date    26-6-13 下午1:41
  * @brief   UF4COM 命令调度器
  ******************************************************************************
  */
#include "uf4com_cmd.h"
#include "uf4com_id_table.h"

#define UF4_TV_SIZE             3u
#define UF4_MAX_STREAM_IDS      64u

uint8_t g_stream_enable;
uint8_t g_stream_ids[UF4_MAX_STREAM_IDS];
uint8_t g_stream_count;
static uint8_t s_stream_dst = UF4_DEFAULT_MASTER_ID;

static void UF4_HandleRead(uf4_frame_t *frame);
static void UF4_HandleWrite(uf4_frame_t *frame);
static void UF4_HandleStreamStart(uf4_frame_t *frame);
static void UF4_HandleStreamStop(uf4_frame_t *frame);
static void UF4_SendError(const uf4_frame_t *frame, uint8_t rsp_cmd, uf4_error_t error);
static uint8_t UF4_ShouldReply(const uf4_frame_t *frame);

void UF4_Dispatch(uf4_frame_t *frame)
{
    if(frame == NULL)
    {
        return;
    }

    switch(frame->cmd)
    {
        case UF4_CMD_READ_REQ:
            UF4_HandleRead(frame);
            break;

        case UF4_CMD_WRITE_REQ:
            UF4_HandleWrite(frame);
            break;

        case UF4_CMD_STREAM_START_REQ:
            UF4_HandleStreamStart(frame);
            break;

        case UF4_CMD_STREAM_STOP_REQ:
            UF4_HandleStreamStop(frame);
            break;

        default:
            UF4_SendError(frame, frame->cmd, UF4_ERR_INVALID_CMD);
            break;
    }
}

static uint8_t UF4_ShouldReply(const uf4_frame_t *frame)
{
    if(frame == NULL)
    {
        return 0u;
    }

    if((frame->flags & UF4_FLAG_ACK_REQ) == 0u)
    {
        // ACK_REQ 是请求响应契约：不带它时，WRITE 可以是只发不等；
        // READ 则等价于静默请求。上位机正常读写时应该置位 ACK_REQ。
        return 0u;
    }

    if(frame->dst == UF4_BROADCAST_ID || frame->src == UF4_BROADCAST_ID)
    {
        // 广播绝不 ACK。一个命令可能命中多个从站，同时 ACK 正是半双工总线
        // 最容易撞包的地方。
        return 0u;
    }

    return 1u;
}

static void UF4_HandleRead(uf4_frame_t *frame)
{
    uint8_t rsp[UF4_MAX_DATA_LEN];
    uint8_t rsp_len = 0u;

    if((frame->len % UF4_TV_SIZE) != 0u)
    {
        UF4_SendError(frame, UF4_CMD_READ_RSP, UF4_ERR_INVALID_LEN);
        return;
    }

    for(uint8_t i = 0u; i < frame->len; i += UF4_TV_SIZE)
    {
        // READ 请求也使用 TV 格式。VALUE 字节会被忽略，但保留下来能让
        // READ / WRITE / STREAM 共用同一种数据项打包方式。
        const uint8_t id = frame->data[i];
        uf4_id_t *item = UF4_FindID(id);

        if(item == NULL)
        {
            UF4_SendError(frame, UF4_CMD_READ_RSP, UF4_ERR_INVALID_ID);
            return;
        }

        const uint16_t value = *(item->ptr);
        rsp[rsp_len++] = id;
        rsp[rsp_len++] = (uint8_t)(value >> 8);
        rsp[rsp_len++] = (uint8_t)(value & 0xFFu);
    }

    if(UF4_ShouldReply(frame) != 0u)
    {
        (void)UF4_SendFrameTo(frame->src,
                              frame->seq,
                              UF4_FLAG_ACK_FRAME,
                              UF4_CMD_READ_RSP,
                              rsp,
                              rsp_len);
    }
}

static void UF4_HandleWrite(uf4_frame_t *frame)
{
    if((frame->len % UF4_TV_SIZE) != 0u)
    {
        UF4_SendError(frame, UF4_CMD_WRITE_RSP, UF4_ERR_INVALID_LEN);
        return;
    }

    for(uint8_t i = 0u; i < frame->len; i += UF4_TV_SIZE)
    {
        const uint8_t id = frame->data[i];
        const uint16_t value = ((uint16_t)frame->data[i + 1u] << 8) | frame->data[i + 2u];
        uf4_id_t *item = UF4_FindID(id);

        if(item == NULL)
        {
            UF4_SendError(frame, UF4_CMD_WRITE_RSP, UF4_ERR_INVALID_ID);
            return;
        }

        if(item->rw == 0u)
        {
        // 遇到第一个写保护项就停止整个 WRITE。注意：此前已经写入的项
        // 不会回滚；如果业务要求原子性，就把载荷控制小一点。
            UF4_SendError(frame, UF4_CMD_WRITE_RSP, UF4_ERR_WRITE_PROTECT);
            return;
        }

        *(item->ptr) = value;
    }

    UF4_NotifyWriteApplied();

    if(UF4_ShouldReply(frame) != 0u)
    {
        (void)UF4_SendFrameTo(frame->src,
                              frame->seq,
                              UF4_FLAG_ACK_FRAME,
                              UF4_CMD_WRITE_RSP,
                              frame->data,
                              frame->len);
    }
}

static void UF4_HandleStreamStart(uf4_frame_t *frame)
{
    const uint8_t req_count = frame->len / UF4_TV_SIZE;

    if((frame->len % UF4_TV_SIZE) != 0u || req_count > UF4_MAX_STREAM_IDS)
    {
        UF4_SendError(frame, UF4_CMD_STREAM_START_RSP, UF4_ERR_INVALID_LEN);
        return;
    }

    g_stream_enable = 1u;
    g_stream_count = 0u;
    s_stream_dst = frame->src;

    if(frame->len == 0u)
    {
        // 空 STREAM_START 表示“流式上传全部已注册 ID”。调机时方便，
        // 量产主站通常应该请求更短的 ID 列表。
        g_stream_count = (uint8_t)UF4_GetAllIDs(g_stream_ids, UF4_MAX_STREAM_IDS);
    }
    else
    {
        for(uint8_t i = 0u; i < frame->len; i += UF4_TV_SIZE)
        {
            if(UF4_FindID(frame->data[i]) == NULL)
            {
                // 输入有坏 ID 时，不要留下半配置状态的流模式。
                g_stream_enable = 0u;
                g_stream_count = 0u;
                UF4_SendError(frame, UF4_CMD_STREAM_START_RSP, UF4_ERR_INVALID_ID);
                return;
            }

            g_stream_ids[g_stream_count++] = frame->data[i];
        }
    }

    if(UF4_ShouldReply(frame) != 0u)
    {
        (void)UF4_SendFrameTo(frame->src,
                              frame->seq,
                              UF4_FLAG_ACK_FRAME,
                              UF4_CMD_STREAM_START_RSP,
                              NULL,
                              0u);
    }
}

static void UF4_HandleStreamStop(uf4_frame_t *frame)
{
    g_stream_enable = 0u;
    g_stream_count = 0u;

    if(UF4_ShouldReply(frame) != 0u)
    {
        (void)UF4_SendFrameTo(frame->src,
                              frame->seq,
                              UF4_FLAG_ACK_FRAME,
                              UF4_CMD_STREAM_STOP_RSP,
                              NULL,
                              0u);
    }
}

void UF4_StreamTask(void)
{
    uint8_t data[UF4_MAX_DATA_LEN];
    uint8_t len = 0u;

    if(g_stream_enable == 0u)
    {
        return;
    }

    for(uint8_t i = 0u; i < g_stream_count; i++)
    {
        const uint8_t id = g_stream_ids[i];
        uf4_id_t *item = UF4_FindID(id);

        if(item == NULL || (uint16_t)(len + UF4_TV_SIZE) > UF4_MAX_DATA_LEN)
        {
            // 有些目标上 ID 表可能运行时变化。遇到过期 ID 时跳过，
            // 不要把整个流任务打死。
            continue;
        }

        const uint16_t value = *(item->ptr);
        data[len++] = id;
        data[len++] = (uint8_t)(value >> 8);
        data[len++] = (uint8_t)(value & 0xFFu);
    }

    (void)UF4_SendFrameTo(s_stream_dst,
                          0u,
                          0u,
                          UF4_CMD_STREAM_DATA,
                          data,
                          len);
}

static void UF4_SendError(const uf4_frame_t *frame, uint8_t rsp_cmd, uf4_error_t error)
{
    uint8_t data[UF4_TV_SIZE];

    if(UF4_ShouldReply(frame) == 0u)
    {
        // 请求方没要求 ACK 时，错误也没有可靠回传位置。这里静默丢弃，
        // 行为和 CRC 错误类似。
        return;
    }

    data[0] = 0xFFu;
    data[1] = (uint8_t)((uint16_t)error >> 8);
    data[2] = (uint8_t)((uint16_t)error & 0xFFu);

    (void)UF4_SendFrameTo(frame->src,
                          frame->seq,
                          UF4_FLAG_ACK_FRAME | UF4_FLAG_ERROR,
                          rsp_cmd,
                          data,
                          sizeof(data));
}
