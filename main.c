#include <stdio.h>
#include <string.h>

#include "uf4com.h"
#include "uf4com_id_table.h"

static uint16_t g_input_voltage = 13088u;
static uint16_t g_input_current = 320u;
static uint16_t g_output_voltage = 4660u;
static uint16_t g_set_voltage = 0u;
static uint16_t g_set_current = 0u;
static uint16_t g_fan_set = 0u;

static uint8_t g_tx_buffer[UF4_MAX_FRAME_LEN];
static uint16_t g_tx_len;
static uint16_t g_tx_count;
static uint16_t g_rx_count;
static uint8_t g_rx_cmd;
static uf4_ack_status_t g_ack_status = UF4_ACK_PENDING;

static uf4_id_t g_id_table[] =
{
    {UF4_ID_INPUT_VOLTAGE, &g_input_voltage, 0u},
    {UF4_ID_INPUT_CURRENT, &g_input_current, 0u},
    {UF4_ID_OUTPUT_VOLTAGE, &g_output_voltage, 0u},
    {UF4_ID_SET_VOLTAGE_LIMIT, &g_set_voltage, 1u},
    {UF4_ID_SET_CURRENT_LIMIT, &g_set_current, 1u},
    {UF4_ID_FAN_SET_VALUE, &g_fan_set, 1u},
};

static void CaptureTx(const uint8_t *data, uint16_t len, void *user)
{
    (void)user;

    if(len <= sizeof(g_tx_buffer))
    {
        memcpy(g_tx_buffer, data, len);
        g_tx_len = len;
        g_tx_count++;
    }
}

static void CaptureAck(uint8_t dst,
                       uint8_t seq,
                       uint8_t cmd,
                       uf4_ack_status_t status,
                       void *user)
{
    (void)dst;
    (void)seq;
    (void)cmd;
    (void)user;

    g_ack_status = status;
}

static void CaptureRx(const uf4_frame_t *frame, void *user)
{
    (void)user;

    if(frame != NULL)
    {
        g_rx_count++;
        g_rx_cmd = frame->cmd;
    }
}

static int Expect(int condition, const char *message)
{
    if(condition)
    {
        return 0;
    }

    printf("FAIL: %s\n", message);
    return 1;
}

static void ResetCapture(void)
{
    g_tx_len = 0u;
    g_tx_count = 0u;
    g_rx_count = 0u;
    g_rx_cmd = 0u;
    g_ack_status = UF4_ACK_PENDING;
    memset(g_tx_buffer, 0, sizeof(g_tx_buffer));
}

int main(void)
{
    uint8_t frame[UF4_MAX_FRAME_LEN];
    uint8_t read_data[] = {
        UF4_ID_INPUT_VOLTAGE, 0x00u, 0x00u,
        UF4_ID_INPUT_CURRENT, 0x00u, 0x00u,
        UF4_ID_OUTPUT_VOLTAGE, 0x00u, 0x00u,
    };
    int failures = 0;

    UF4_IDTableBind(g_id_table, sizeof(g_id_table) / sizeof(g_id_table[0]));

    // ---------- Slave RX path ----------
    ResetCapture();
    const uf4_config_t slave_config = {
        .local_id = 0x11u,
        .role = UF4_ROLE_SLAVE,
        .ack_timeout_ms = 50u,
        .ack_retries = 2u,
        .arbitration_slot_ms = 2u,
    };
    UF4_InitEx(&slave_config, CaptureTx, NULL);

    const uint16_t req_len = UF4_BuildFrameTo(0x00u,
                                              0x11u,
                                              0x01u,
                                              UF4_FLAG_ACK_REQ,
                                              UF4_CMD_READ_REQ,
                                              read_data,
                                              sizeof(read_data),
                                              frame,
                                              sizeof(frame));

    failures += Expect(req_len == 19u, "V4 read request length should include src/dst");
    failures += Expect(UF4_InputBuffer(frame, req_len) == 1u, "slave should accept addressed request");
    failures += Expect(g_tx_len == 19u, "read response length should be 19 bytes");
    failures += Expect(g_tx_buffer[2] == 0x11u, "response src should be slave id");
    failures += Expect(g_tx_buffer[3] == 0x00u, "response dst should be master id");
    failures += Expect(g_tx_buffer[4] == 0x01u, "response seq should echo request seq");
    failures += Expect(g_tx_buffer[5] == UF4_FLAG_ACK_FRAME, "response should be ACK frame");
    failures += Expect(g_tx_buffer[6] == UF4_CMD_READ_RSP, "response command should be READ_RSP");
    failures += Expect(g_tx_buffer[8] == 0x0Au && g_tx_buffer[9] == 0x33u && g_tx_buffer[10] == 0x20u,
                       "input voltage should encode as 0x3320");

    ResetCapture();
    (void)UF4_BuildFrameTo(0x00u,
                           0x12u,
                           0x02u,
                           UF4_FLAG_ACK_REQ,
                           UF4_CMD_READ_REQ,
                           read_data,
                           sizeof(read_data),
                           frame,
                           sizeof(frame));
    failures += Expect(UF4_InputBuffer(frame, req_len) == 0u, "slave should drop other device frames");
    failures += Expect(g_tx_count == 0u, "wrong-dst frame should not respond");

    // ---------- Master ACK table and retry path ----------
    ResetCapture();
    const uf4_config_t master_config = {
        .local_id = 0x00u,
        .role = UF4_ROLE_MASTER,
        .ack_timeout_ms = 50u,
        .ack_retries = 2u,
        .arbitration_slot_ms = 2u,
    };
    UF4_InitEx(&master_config, CaptureTx, NULL);
    UF4_SetAckEventCallback(CaptureAck, NULL);
    UF4_SetFrameRxCallback(CaptureRx, NULL);

    failures += Expect(UF4_SendFrameTo(0x11u,
                                       0x33u,
                                       UF4_FLAG_ACK_REQ,
                                       UF4_CMD_READ_REQ,
                                       read_data,
                                       sizeof(read_data)) == 1u,
                       "master should send tracked ACK request");
    failures += Expect(g_tx_count == 1u, "initial request should transmit once");

    UF4_ProcessMs(50u);
    failures += Expect(g_tx_count == 2u, "timeout should retransmit request");

    const uint16_t ack_len = UF4_BuildFrameTo(0x11u,
                                              0x00u,
                                              0x33u,
                                              UF4_FLAG_ACK_FRAME,
                                              UF4_CMD_READ_RSP,
                                              read_data,
                                              sizeof(read_data),
                                              frame,
                                              sizeof(frame));
    failures += Expect(UF4_InputBuffer(frame, ack_len) == 1u, "master should accept matching ACK response");
    failures += Expect(g_ack_status == UF4_ACK_DONE, "ACK table should mark request done");
    failures += Expect(g_rx_count == 1u && g_rx_cmd == UF4_CMD_READ_RSP,
                       "matched ACK response should be delivered to app callback");

    UF4_ProcessMs(100u);
    failures += Expect(g_tx_count == 2u, "matched request should not retransmit again");
    failures += Expect(UF4_GetArbitrationDelayMs(0x03u) < UF4_GetArbitrationDelayMs(0x04u),
                       "lower device id should have earlier arbitration slot");

    if(failures == 0)
    {
        printf("UF4COM V4 self-test passed.\n");
        return 0;
    }

    printf("UF4COM V4 self-test failed: %d\n", failures);
    return 1;
}
