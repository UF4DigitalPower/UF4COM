/**
  ******************************************************************************
  * @file    uf4com_parser.c
  * @author  UF4
  * @date    26-6-13 下午1:41
  * @brief   UF4COM V4 stream parser
  ******************************************************************************
  */
#include "uf4com_parser.h"
#include "uf4com_crc.h"

static uint16_t UF4_FrameCRC(const uf4_frame_t *frame)
{
    uint8_t crc_data[6u + UF4_MAX_DATA_LEN];

    // V4 deliberately includes SRC/DST in CRC. A flipped address bit must not
    // turn into a valid command for the wrong slave.
    crc_data[0] = frame->src;
    crc_data[1] = frame->dst;
    crc_data[2] = frame->seq;
    crc_data[3] = frame->flags;
    crc_data[4] = frame->cmd;
    crc_data[5] = frame->len;

    for(uint16_t i = 0u; i < frame->len; i++)
    {
        crc_data[6u + i] = frame->data[i];
    }

    return UF4_CRC16(crc_data, (uint16_t)(6u + frame->len));
}

void UF4_ParserInit(uf4_parser_t *parser)
{
    if(parser == NULL)
    {
        return;
    }

    parser->state = WAIT_SOF1;
    parser->data_index = 0u;
    parser->crc_rx = 0u;
    parser->frame.src = 0u;
    parser->frame.dst = 0u;
    parser->frame.seq = 0u;
    parser->frame.flags = 0u;
    parser->frame.cmd = 0u;
    parser->frame.len = 0u;
}

uint8_t UF4_ParserInput(uf4_parser_t *parser, uint8_t byte, uf4_frame_t *out_frame)
{
    if(parser == NULL || out_frame == NULL)
    {
        return 0u;
    }

    switch(parser->state)
    {
        case WAIT_SOF1:
            // Any byte stream noise is ignored until AA 55 appears again.
            parser->state = (byte == UF4_SOF1) ? WAIT_SOF2 : WAIT_SOF1;
            break;

        case WAIT_SOF2:
            // If the second byte is wrong, restart from SOF1. This keeps the
            // parser simple and bounded; no history buffer needed.
            parser->state = (byte == UF4_SOF2) ? WAIT_SRC : WAIT_SOF1;
            break;

        case WAIT_SRC:
            parser->frame.src = byte;
            parser->state = WAIT_DST;
            break;

        case WAIT_DST:
            parser->frame.dst = byte;
            parser->state = WAIT_SEQ;
            break;

        case WAIT_SEQ:
            parser->frame.seq = byte;
            parser->state = WAIT_FLAGS;
            break;

        case WAIT_FLAGS:
            parser->frame.flags = byte;
            parser->state = WAIT_CMD;
            break;

        case WAIT_CMD:
            parser->frame.cmd = byte;
            parser->state = WAIT_LEN;
            break;

        case WAIT_LEN:
            parser->frame.len = byte;
            parser->data_index = 0u;
            parser->state = (byte == 0u) ? WAIT_CRC_L : WAIT_DATA;
            break;

        case WAIT_DATA:
            if(parser->data_index >= UF4_MAX_DATA_LEN)
            {
                // LEN is one byte, so this is mostly defensive against future
                // format changes or a corrupted parser state.
                UF4_ParserInit(parser);
                return 0u;
            }

            parser->frame.data[parser->data_index++] = byte;
            if(parser->data_index >= parser->frame.len)
            {
                parser->state = WAIT_CRC_L;
            }
            break;

        case WAIT_CRC_L:
            parser->crc_rx = byte;
            parser->state = WAIT_CRC_H;
            break;

        case WAIT_CRC_H:
        {
            parser->crc_rx |= (uint16_t)byte << 8;

            const uint16_t crc_calc = UF4_FrameCRC(&parser->frame);
            const uint8_t valid = (crc_calc == parser->crc_rx);

            if(valid != 0u)
            {
                *out_frame = parser->frame;
            }

            // Always reset after CRC, valid or not. A bad frame must not leak
            // partial state into the next frame.
            UF4_ParserInit(parser);
            return valid;
        }

        default:
            UF4_ParserInit(parser);
            break;
    }

    return 0u;
}
