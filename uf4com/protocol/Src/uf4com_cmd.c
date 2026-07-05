/**
  ******************************************************************************
  * @file    uf4com_cmd.c
  * @author  UF4
  * @date    26-6-13 下午1:41
  * @brief   UF4COM command dispatcher
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
        // ACK_REQ is the contract: without it, WRITE can be fire-and-forget and
        // READ is effectively a silent request. PC side should set ACK_REQ.
        return 0u;
    }

    if(frame->dst == UF4_BROADCAST_ID || frame->src == UF4_BROADCAST_ID)
    {
        // Never ACK broadcast. One command may hit many slaves, and simultaneous
        // ACKs are exactly what kills a half-duplex bus.
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
        // READ request uses TV format too. VALUE bytes are ignored but kept so
        // READ/WRITE/STREAM all share the same item packing.
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
            // Fail the whole WRITE on first protected item. Partial writes before
            // this point remain applied; keep payload small if atomicity matters.
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
        // Empty STREAM_START means "stream every registered ID". This is handy
        // for bring-up, but production masters usually request a shorter list.
        g_stream_count = (uint8_t)UF4_GetAllIDs(g_stream_ids, UF4_MAX_STREAM_IDS);
    }
    else
    {
        for(uint8_t i = 0u; i < frame->len; i += UF4_TV_SIZE)
        {
            if(UF4_FindID(frame->data[i]) == NULL)
            {
                // Do not leave a half-configured stream running after bad input.
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
            // The ID table may change at runtime on some targets. Skip stale IDs
            // rather than killing the stream task.
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
        // If the requester did not ask for ACK, there is nowhere reliable to put
        // the error. Silently dropping mirrors CRC-error behavior.
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
