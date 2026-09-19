/**
  ******************************************************************************
  * @file    uf4com_parser.c
  * @author  UF4
  * @date    26-6-13 下午1:41
  * @brief   UF4COM V5 字节流解析器
  ******************************************************************************
  */
#include "uf4com_parser.h"
#include "uf4com_crc.h"

static uint16_t UF4_FrameCRC(const uf4_frame_t *frame)
{
    uint8_t crc_data[4u + UF4_MAX_DATA_LEN];

    crc_data[0] = frame->seq;
    crc_data[1] = frame->flags;
    crc_data[2] = frame->cmd;
    crc_data[3] = frame->len;

    for(uint16_t i = 0u; i < frame->len; i++)
    {
        crc_data[4u + i] = frame->data[i];
    }

    return UF4_CRC16(crc_data, (uint16_t)(4u + frame->len));
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
            // 字节流里的噪声一律忽略，直到再次看到 AA 55。
            parser->state = (byte == UF4_SOF1) ? WAIT_SOF2 : WAIT_SOF1;
            break;

        case WAIT_SOF2:
            // AA AA 55 中的第二个 AA 也可以是下一帧帧头。
            parser->state = (byte == UF4_SOF2) ? WAIT_SEQ :
                            ((byte == UF4_SOF1) ? WAIT_SOF2 : WAIT_SOF1);
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
                // LEN 只有 1 字节，这里主要是防御未来格式变化或 parser 状态损坏。
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

            // CRC 检查后无论成功失败都复位。坏帧不能把半截状态带进下一帧。
            UF4_ParserInit(parser);
            return valid;
        }

        default:
            UF4_ParserInit(parser);
            break;
    }

    return 0u;
}
