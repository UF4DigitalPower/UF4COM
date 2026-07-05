/**
  ******************************************************************************
  * @file    uf4com.h
  * @author  UF4
  * @date    26-6-13 下午1:39
  * @brief   UF4COM V4 protocol public API
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

// FLAGS
#define UF4_FLAG_ACK_REQ                 0x01u
#define UF4_FLAG_ACK_FRAME               0x02u
#define UF4_FLAG_ERROR                   0x04u
#define UF4_FLAG_BROADCAST               0x08u

// DATA ID
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
 * @brief Protocol error codes carried by ERROR ACK frames.
 *
 * Wire format uses DATA = FF ERR_H ERR_L. Keep values stable because the PC
 * side and old tooling will usually compare the numeric code, not the enum name.
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
 * @brief Local station role.
 *
 * V4 does not hide half-duplex bus policy inside the protocol layer. The role is
 * kept in config so application code can make master/slave decisions around it.
 */
typedef enum
{
    UF4_ROLE_MASTER = 0u,
    UF4_ROLE_SLAVE = 1u
} uf4_role_t;

/**
 * @brief Result of a tracked ACK_REQ transaction.
 */
typedef enum
{
    UF4_ACK_PENDING = 0u,
    UF4_ACK_DONE = 1u,
    UF4_ACK_TIMEOUT = 2u,
    UF4_ACK_DROPPED = 3u
} uf4_ack_status_t;

/**
 * @brief Decoded UF4COM V4 frame.
 *
 * Wire order after SOF is SRC, DST, SEQ, FLAGS, CMD, LEN, DATA, CRC16. CRC
 * covers SRC through DATA so an address-bit error is caught before dispatch.
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
 * @brief Parser states for byte-by-byte stream input.
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
 * @brief Stream parser context.
 *
 * Keep one parser per physical byte stream. The default API owns a single static
 * parser because this library is normally used with one UART/USB CDC endpoint.
 */
typedef struct
{
    uf4_state_t state;
    uf4_frame_t frame;
    uint16_t data_index;
    uint16_t crc_rx;
} uf4_parser_t;

/**
 * @brief One pending ACK_REQ transaction.
 *
 * The whole encoded frame is kept so timeout handling can retransmit without
 * asking the application to rebuild payload data that may already have changed.
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
 * @brief Runtime protocol configuration.
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
 * @brief Initialize with default master ID 0x00.
 */
void UF4_Init(uf4_tx_fn_t tx, void *user);

/**
 * @brief Initialize V4 protocol state with explicit local ID and timing config.
 */
void UF4_InitEx(const uf4_config_t *config, uf4_tx_fn_t tx, void *user);
void UF4_SetWriteApplyCallback(uf4_write_apply_fn_t cb, void *user);

/**
 * @brief Register callback for ACK_REQ completion, timeout or request-table drop.
 */
void UF4_SetAckEventCallback(uf4_ack_event_fn_t cb, void *user);

/**
 * @brief Register callback for complete incoming response/data frames.
 *
 * Request frames are dispatched internally. ACK frames are first matched against
 * the request table, then delivered here so the application can consume payload.
 */
void UF4_SetFrameRxCallback(uf4_frame_rx_fn_t cb, void *user);
void UF4_Process(void);

/**
 * @brief Run timeout/retransmit processing with a monotonic millisecond tick.
 */
void UF4_ProcessMs(uint32_t now_ms);

uint8_t UF4_GetLocalID(void);
void UF4_SetLocalID(uint8_t local_id);

/**
 * @brief Calculate software arbitration delay. Lower device ID wins earlier slot.
 */
uint32_t UF4_GetArbitrationDelayMs(uint8_t device_id);
uint8_t UF4_IsFrameForLocalDevice(const uf4_frame_t *frame);

/**
 * @brief Build a V4 frame with explicit source and destination device IDs.
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
 * @brief Compatibility builder. Uses local ID as SRC and default master ID as DST.
 */
uint16_t UF4_BuildFrame(uint8_t seq,
                        uint8_t flags,
                        uint8_t cmd,
                        const uint8_t *data,
                        uint8_t len,
                        uint8_t *out,
                        uint16_t out_size);

/**
 * @brief Send a frame to a specific device and track it if ACK_REQ is set.
 */
uint8_t UF4_SendFrameTo(uint8_t dst,
                        uint8_t seq,
                        uint8_t flags,
                        uint8_t cmd,
                        const uint8_t *data,
                        uint8_t len);

/**
 * @brief Compatibility sender. Sends to the default master ID.
 */
uint8_t UF4_SendFrame(uint8_t seq,
                      uint8_t flags,
                      uint8_t cmd,
                      const uint8_t *data,
                      uint8_t len);

/**
 * @brief Feed one received byte into the global parser.
 */
uint8_t UF4_InputByte(uint8_t byte);

/**
 * @brief Feed a buffer into the global parser. Returns accepted complete frames.
 */
uint8_t UF4_InputBuffer(const uint8_t *data, uint16_t len);

#endif // UF4COM_H
