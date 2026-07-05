/**
  ******************************************************************************
  * @file    uf4com.h
  * @author  UF4
  * @date    26-6-13 下午1:39
  * @brief   UF4COM V4 协议公共接口
  ******************************************************************************
  */
#ifndef UF4COM_H
#define UF4COM_H

#include <stddef.h>
#include <stdint.h>

#define UF4_SOF1                         0xAAu
#define UF4_SOF2                         0x55u

#define UF4_BROADCAST_ID                 0xFFu
#define UF4_DEFAULT_MASTER_ID            0x00u
#define UF4_DEFAULT_DEVICE_ID            UF4_DEFAULT_MASTER_ID

#define UF4_MAX_DATA_LEN                 255u
#define UF4_V4_HEADER_LEN                8u
#define UF4_CRC_LEN                      2u
#define UF4_MAX_FRAME_LEN                (UF4_V4_HEADER_LEN + UF4_MAX_DATA_LEN + UF4_CRC_LEN)

#define UF4_DEFAULT_ACK_TIMEOUT_MS       50u
#define UF4_DEFAULT_ACK_RETRIES          3u
#define UF4_DEFAULT_ARBITRATION_SLOT_MS  2u
#define UF4_MAX_PENDING_REQUESTS         8u

#define UF4_CMD_READ_REQ                 0x01u
#define UF4_CMD_READ_RSP                 0x81u

#define UF4_CMD_WRITE_REQ                0x02u
#define UF4_CMD_WRITE_RSP                0x82u

#define UF4_CMD_STREAM_START_REQ         0x10u
#define UF4_CMD_STREAM_START_RSP         0x90u

#define UF4_CMD_STREAM_STOP_REQ          0x11u
#define UF4_CMD_STREAM_STOP_RSP          0x91u

#define UF4_CMD_STREAM_DATA              0x15u

// 控制标志
#define UF4_FLAG_ACK_REQ                 0x01u
#define UF4_FLAG_ACK_FRAME               0x02u
#define UF4_FLAG_ERROR                   0x04u
#define UF4_FLAG_BROADCAST               0x08u

// 数据 ID
#define UF4_ID_INPUT_VOLTAGE                    0x0Au
#define UF4_ID_INPUT_CURRENT                    0x0Bu
#define UF4_ID_OUTPUT_VOLTAGE                   0x0Cu
#define UF4_ID_OUTPUT_CURRENT                   0x0Du
#define UF4_ID_CORE_TEMPERATURE                 0x0Eu
#define UF4_ID_TEMP1_TEMPERATURE                0x0Fu
#define UF4_ID_TEMP2_TEMPERATURE                0x10u
#define UF4_ID_SET_VOLTAGE_LIMIT                0x11u
#define UF4_ID_SET_CURRENT_LIMIT                0x12u
#define UF4_ID_CC_CV_MODE                       0x14u
#define UF4_ID_POWER_STATE                      0x15u
#define UF4_ID_FAULT_STATE                      0x16u
#define UF4_ID_STATE_MACHINE_FLAG_BITS          0x17u
#define UF4_ID_STATE_MACHINE_STATE              0x18u
#define UF4_ID_INPUT_VOLTAGE_RAW                0x19u
#define UF4_ID_INPUT_CURRENT_RAW                0x1Au
#define UF4_ID_OUTPUT_VOLTAGE_RAW               0x1Bu
#define UF4_ID_OUTPUT_CURRENT_RAW               0x1Cu
#define UF4_ID_OTP_VALUE                        0x1Du
#define UF4_ID_OTP_SET_VALUE                    0x1Eu
#define UF4_ID_OVP_VALUE                        0x1Fu
#define UF4_ID_OVP_SET_VALUE                    0x20u
#define UF4_ID_OCP_VALUE                        0x21u
#define UF4_ID_OCP_SET_VALUE                    0x22u
#define UF4_ID_DUTY_CMD                         0x23u
#define UF4_ID_PWM_A_COMPARE                    0x24u
#define UF4_ID_PWM_D_COMPARE                    0x25u
#define UF4_ID_FAN_SPEED                        0x26u
#define UF4_ID_FAN_SET_VALUE                    0x27u
#define UF4_ID_LOOP_CURRENT_FEEDBACK            0x29u
#define UF4_ID_LOOP_CURRENT_REFERENCE           0x2Au
#define UF4_ID_VOLTAGE_LOOP_CURRENT_REFERENCE   0x2Bu

/**
 * @brief ERROR ACK 帧携带的协议错误码。
 *
 * 线上格式固定为 DATA = FF ERR_H ERR_L。错误码数值不要随便改，
 * 上位机和老工具通常比较的是数值，不是枚举名。
 */
typedef enum
{
    UF4_OK = 0x0000,
    UF4_ERR_INVALID_ID = 0x0001,
    UF4_ERR_INVALID_CMD = 0x0002,
    UF4_ERR_CRC = 0x0003,
    UF4_ERR_INVALID_LEN = 0x0004,
    UF4_ERR_WRITE_PROTECT = 0x0005,
    UF4_ERR_OUT_OF_RANGE = 0x0006,
    UF4_ERR_DEVICE_BUSY = 0x0007,
    UF4_ERR_INTERNAL = 0x0008,
    UF4_ERR_TIMEOUT = 0x0009,
    UF4_ERR_NO_PENDING_SLOT = 0x000A,
    UF4_ERR_NOT_FOR_THIS_DEVICE = 0x000B,
    UF4_ERR_ARBITRATION = 0x000C
} uf4_error_t;

/**
 * @brief 本机站点角色。
 *
 * V4 不把半双工总线策略藏在协议层里。角色放在配置里，方便应用层
 * 按主站/从站做调度、轮询和总线方向控制。
 */
typedef enum
{
    UF4_ROLE_MASTER = 0u,
    UF4_ROLE_SLAVE = 1u
} uf4_role_t;

/**
 * @brief ACK_REQ 请求表的处理结果。
 */
typedef enum
{
    UF4_ACK_PENDING = 0u,
    UF4_ACK_DONE = 1u,
    UF4_ACK_TIMEOUT = 2u,
    UF4_ACK_DROPPED = 3u
} uf4_ack_status_t;

/**
 * @brief 已解析的 UF4COM V4 帧。
 *
 * SOF 后的线上顺序是 SRC、DST、SEQ、FLAGS、CMD、LEN、DATA、CRC16。
 * CRC 覆盖 SRC 到 DATA，地址位翻转会先被 CRC 拦住，不会误投递给别的从站。
 */
typedef struct
{
    uint8_t src;
    uint8_t dst;
    uint8_t seq;
    uint8_t flags;
    uint8_t cmd;
    uint8_t len;
    uint8_t data[UF4_MAX_DATA_LEN];
} uf4_frame_t;

typedef struct
{
    uint8_t id;
    uint16_t value;
} tv_t;

/**
 * @brief 字节流解析器状态。
 */
typedef enum
{
    WAIT_SOF1,
    WAIT_SOF2,
    WAIT_SRC,
    WAIT_DST,
    WAIT_SEQ,
    WAIT_FLAGS,
    WAIT_CMD,
    WAIT_LEN,
    WAIT_DATA,
    WAIT_CRC_L,
    WAIT_CRC_H
} uf4_state_t;

/**
 * @brief 字节流解析器上下文。
 *
 * 一个物理字节流对应一个解析器。默认 API 内部只持有一个静态解析器，
 * 因为这个库通常只接一个 UART 或 USB CDC 端点。
 */
typedef struct
{
    uf4_state_t state;
    uf4_frame_t frame;
    uint16_t data_index;
    uint16_t crc_rx;
} uf4_parser_t;

/**
 * @brief 一个等待 ACK 的请求表项。
 *
 * 这里保存整帧编码后的字节，而不是只保存 DATA。超时重发必须和第一次发送
 * 字节一致，不能让应用层重新取一份可能已经变化的数据。
 */
typedef struct
{
    uint8_t seq;
    uint8_t dst;
    uint8_t cmd;
    uint8_t retries_left;
    uint16_t frame_len;
    uint32_t last_tx_ms;
    uint32_t timeout_ms;
    uint8_t frame[UF4_MAX_FRAME_LEN];
    uint8_t active;
} uf4_request_entry_t;

/**
 * @brief 协议运行时配置。
 */
typedef struct
{
    uint8_t local_id;
    uf4_role_t role;
    uint32_t ack_timeout_ms;
    uint8_t ack_retries;
    uint16_t arbitration_slot_ms;
} uf4_config_t;

typedef void (*uf4_tx_fn_t)(const uint8_t *data, uint16_t len, void *user);
typedef void (*uf4_write_apply_fn_t)(void *user);
typedef void (*uf4_ack_event_fn_t)(uint8_t dst,
                                   uint8_t seq,
                                   uint8_t cmd,
                                   uf4_ack_status_t status,
                                   void *user);
typedef void (*uf4_frame_rx_fn_t)(const uf4_frame_t *frame, void *user);

/**
 * @brief 使用默认主站 ID 0x00 初始化。
 */
void UF4_Init(uf4_tx_fn_t tx, void *user);

/**
 * @brief 使用显式本机 ID 和定时参数初始化 V4 协议状态。
 */
void UF4_InitEx(const uf4_config_t *config, uf4_tx_fn_t tx, void *user);
void UF4_SetWriteApplyCallback(uf4_write_apply_fn_t cb, void *user);

/**
 * @brief 注册 ACK_REQ 完成、超时或请求表丢弃事件回调。
 */
void UF4_SetAckEventCallback(uf4_ack_event_fn_t cb, void *user);

/**
 * @brief 注册完整响应帧或数据帧回调。
 *
 * 请求帧由协议层内部调度。ACK 帧会先做请求表匹配，再送到这里，
 * 这样应用层仍然能拿到 READ_RSP / STREAM_DATA 的载荷。
 */
void UF4_SetFrameRxCallback(uf4_frame_rx_fn_t cb, void *user);
void UF4_Process(void);

/**
 * @brief 使用单调递增的毫秒 tick 处理超时和重发。
 */
void UF4_ProcessMs(uint32_t now_ms);

uint8_t UF4_GetLocalID(void);
void UF4_SetLocalID(uint8_t local_id);

/**
 * @brief 计算软件仲裁延时。设备 ID 越小，时隙越靠前。
 */
uint32_t UF4_GetArbitrationDelayMs(uint8_t device_id);
uint8_t UF4_IsFrameForLocalDevice(const uf4_frame_t *frame);

/**
 * @brief 使用显式源设备 ID 和目标设备 ID 构建 V4 帧。
 */
uint16_t UF4_BuildFrameTo(uint8_t src,
                          uint8_t dst,
                          uint8_t seq,
                          uint8_t flags,
                          uint8_t cmd,
                          const uint8_t *data,
                          uint8_t len,
                          uint8_t *out,
                          uint16_t out_size);

/**
 * @brief 兼容旧接口的构帧函数。SRC 使用本机 ID，DST 使用默认主站 ID。
 */
uint16_t UF4_BuildFrame(uint8_t seq,
                        uint8_t flags,
                        uint8_t cmd,
                        const uint8_t *data,
                        uint8_t len,
                        uint8_t *out,
                        uint16_t out_size);

/**
 * @brief 向指定设备发送帧；如果置位 ACK_REQ，则登记请求表。
 */
uint8_t UF4_SendFrameTo(uint8_t dst,
                        uint8_t seq,
                        uint8_t flags,
                        uint8_t cmd,
                        const uint8_t *data,
                        uint8_t len);

/**
 * @brief 兼容旧接口的发送函数。默认发给主站 ID。
 */
uint8_t UF4_SendFrame(uint8_t seq,
                      uint8_t flags,
                      uint8_t cmd,
                      const uint8_t *data,
                      uint8_t len);

/**
 * @brief 向全局解析器输入 1 个接收字节。
 */
uint8_t UF4_InputByte(uint8_t byte);

/**
 * @brief 向全局解析器输入一段缓冲区，返回被本机接受的完整帧数量。
 */
uint8_t UF4_InputBuffer(const uint8_t *data, uint16_t len);

#endif // UF4COM_H
