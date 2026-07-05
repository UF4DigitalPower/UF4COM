/**
  ******************************************************************************
  * @file    uf4com.c
  * @author  UF4
  * @date    26-6-13 下午1:39
  * @brief   UF4COM V4 frame build, ACK tracking and RX entry
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
    config->local_id = UF4_DEFAULT_DEVICE_ID;
    config->role = UF4_ROLE_MASTER;
    config->ack_timeout_ms = UF4_DEFAULT_ACK_TIMEOUT_MS;
    config->ack_retries = UF4_DEFAULT_ACK_RETRIES;
    config->arbitration_slot_ms = UF4_DEFAULT_ARBITRATION_SLOT_MS;
}

static uint8_t UF4_ResponseCmdFor(uint8_t cmd)
{
    // Request table stores the response command we expect to see on the wire.
    // That keeps ACK matching cheap: SRC + SEQ + CMD is enough for V4.
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

        default:
            return cmd;
    }
}

static uint8_t UF4_IsRequestCmd(uint8_t cmd)
{
    // Only these commands are handled inside uf4com_cmd.c. Response frames and
    // STREAM_DATA belong to the application callback after optional ACK matching.
    switch(cmd)
    {
        case UF4_CMD_READ_REQ:
        case UF4_CMD_WRITE_REQ:
        case UF4_CMD_STREAM_START_REQ:
        case UF4_CMD_STREAM_STOP_REQ:
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

static void UF4_NotifyAck(uint8_t dst, uint8_t seq, uint8_t cmd, uf4_ack_status_t status)
{
    if(s_ack_event != NULL)
    {
        s_ack_event(dst, seq, cmd, status, s_ack_event_user);
    }
}

static uint8_t UF4_AddRequest(uint8_t dst,
                              uint8_t seq,
                              uint8_t expected_cmd,
                              const uint8_t *frame,
                              uint16_t frame_len)
{
    // Broadcast is fire-and-forget. If every slave ACKed the same broadcast,
    // half-duplex buses would collide immediately.
    if(dst == UF4_BROADCAST_ID || frame == NULL || frame_len > UF4_MAX_FRAME_LEN)
    {
        return 1u;
    }

    for(uint8_t i = 0u; i < UF4_MAX_PENDING_REQUESTS; i++)
    {
        if(s_requests[i].active == 0u)
        {
            // Store the encoded frame, not just user payload. Retransmit must be
            // byte-identical to the first send, including SEQ and CRC.
            s_requests[i].seq = seq;
            s_requests[i].dst = dst;
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

    UF4_NotifyAck(dst, seq, expected_cmd, UF4_ACK_DROPPED);
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
        // DST in the request becomes SRC in the response. SEQ must echo. CMD is
        // the expected response command, e.g. READ_REQ waits for READ_RSP.
        if(s_requests[i].active != 0u &&
           s_requests[i].dst == frame->src &&
           s_requests[i].seq == frame->seq &&
           s_requests[i].cmd == frame->cmd)
        {
            s_requests[i].active = 0u;
            UF4_NotifyAck(frame->src, frame->seq, frame->cmd, UF4_ACK_DONE);
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

    if(s_config.arbitration_slot_ms == 0u)
    {
        s_config.arbitration_slot_ms = UF4_DEFAULT_ARBITRATION_SLOT_MS;
    }

    // Re-init is allowed. It intentionally clears pending requests because the
    // local address, role or timeout policy may have changed.
    s_tx = tx;
    s_tx_user = user;
    s_now_ms = 0u;

    UF4_ClearRequests();
    UF4_ParserInit(&s_parser);
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
    // Compatibility path for old code. Real firmware should call UF4_ProcessMs()
    // with a system tick; this fallback assumes the caller runs it every 1 ms.
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
            UF4_NotifyAck(s_requests[i].dst,
                          s_requests[i].seq,
                          s_requests[i].cmd,
                          UF4_ACK_TIMEOUT);
            s_requests[i].active = 0u;
            continue;
        }

        if(s_tx != NULL)
        {
            // retries_left counts retransmissions, not the initial send. So a
            // value of 3 means: first send + up to 3 resends.
            s_tx(s_requests[i].frame, s_requests[i].frame_len, s_tx_user);
        }

        s_requests[i].retries_left--;
        s_requests[i].last_tx_ms = now_ms;
    }

    UF4_StreamTask();
}

uint8_t UF4_GetLocalID(void)
{
    return s_config.local_id;
}

void UF4_SetLocalID(uint8_t local_id)
{
    s_config.local_id = local_id;
}

uint32_t UF4_GetArbitrationDelayMs(uint8_t device_id)
{
    if(device_id == UF4_BROADCAST_ID)
    {
        return 0u;
    }

    // Lower ID gets an earlier slot. This is only the protocol-side slot helper;
    // the RS485 driver still needs bus-idle detection before enabling TX.
    return (uint32_t)device_id * (uint32_t)s_config.arbitration_slot_ms;
}

uint8_t UF4_IsFrameForLocalDevice(const uf4_frame_t *frame)
{
    if(frame == NULL)
    {
        return 0u;
    }

    if(frame->dst == s_config.local_id || frame->dst == UF4_BROADCAST_ID)
    {
        return 1u;
    }

    return 0u;
}

uint16_t UF4_BuildFrameTo(uint8_t src,
                          uint8_t dst,
                          uint8_t seq,
                          uint8_t flags,
                          uint8_t cmd,
                          const uint8_t *data,
                          uint8_t len,
                          uint8_t *out,
                          uint16_t out_size)
{
    const uint16_t frame_len = (uint16_t)(UF4_V4_HEADER_LEN + len + UF4_CRC_LEN);

    if(out == NULL || out_size < frame_len || (data == NULL && len > 0u))
    {
        return 0u;
    }

    if(dst == UF4_BROADCAST_ID)
    {
        // Keep the explicit flag as a cheap hint for sniffers/loggers. Dispatch
        // still trusts DST == 0xFF as the real broadcast decision.
        flags |= UF4_FLAG_BROADCAST;
    }

    out[0] = UF4_SOF1;
    out[1] = UF4_SOF2;
    out[2] = src;
    out[3] = dst;
    out[4] = seq;
    out[5] = flags;
    out[6] = cmd;
    out[7] = len;

    for(uint8_t i = 0u; i < len; i++)
    {
        out[8u + i] = data[i];
    }

    const uint16_t crc = UF4_CRC16(&out[2], (uint16_t)(6u + len));
    out[8u + len] = (uint8_t)(crc & 0xFFu);
    out[9u + len] = (uint8_t)(crc >> 8);

    return frame_len;
}

uint16_t UF4_BuildFrame(uint8_t seq,
                        uint8_t flags,
                        uint8_t cmd,
                        const uint8_t *data,
                        uint8_t len,
                        uint8_t *out,
                        uint16_t out_size)
{
    return UF4_BuildFrameTo(s_config.local_id,
                            UF4_DEFAULT_MASTER_ID,
                            seq,
                            flags,
                            cmd,
                            data,
                            len,
                            out,
                            out_size);
}

uint8_t UF4_SendFrameTo(uint8_t dst,
                        uint8_t seq,
                        uint8_t flags,
                        uint8_t cmd,
                        const uint8_t *data,
                        uint8_t len)
{
    uint8_t buffer[UF4_MAX_FRAME_LEN];
    const uint16_t frame_len = UF4_BuildFrameTo(s_config.local_id,
                                                dst,
                                                seq,
                                                flags,
                                                cmd,
                                                data,
                                                len,
                                                buffer,
                                                sizeof(buffer));

    if(frame_len == 0u || s_tx == NULL)
    {
        return 0u;
    }

    s_tx(buffer, frame_len, s_tx_user);

    if((flags & UF4_FLAG_ACK_REQ) != 0u && (flags & UF4_FLAG_ACK_FRAME) == 0u)
    {
        // Tracking happens after the first send. If the request table is full,
        // the caller gets 0 and the ACK event reports UF4_ACK_DROPPED.
        return UF4_AddRequest(dst, seq, UF4_ResponseCmdFor(cmd), buffer, frame_len);
    }

    return 1u;
}

uint8_t UF4_SendFrame(uint8_t seq,
                      uint8_t flags,
                      uint8_t cmd,
                      const uint8_t *data,
                      uint8_t len)
{
    return UF4_SendFrameTo(UF4_DEFAULT_MASTER_ID, seq, flags, cmd, data, len);
}

uint8_t UF4_InputByte(uint8_t byte)
{
    uf4_frame_t frame;

    if(UF4_ParserInput(&s_parser, byte, &frame) == 0u)
    {
        return 0u;
    }

    if(UF4_IsFrameForLocalDevice(&frame) == 0u)
    {
        // Important on shared buses: a valid CRC frame may still be for another
        // slave. Drop it silently so we do not create bus chatter.
        return 0u;
    }

    if((frame.flags & UF4_FLAG_ACK_FRAME) != 0u)
    {
        // ACK matching is protocol housekeeping. The frame is still delivered so
        // READ_RSP/WRITE_RSP payload stays visible to the application.
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
