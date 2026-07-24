#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#endif

#include "uf4com.h"
#include "uf4com_id_table.h"

static uint16_t g_input_voltage = 13088u;
static uint16_t g_input_current = 320u;
static uint16_t g_output_voltage = 4660u;
static uint16_t g_output_current = 120u;
static uint16_t g_core_temperature = 3650u;
static uint16_t g_temp1_temperature = 3520u;
static uint16_t g_temp2_temperature = 3480u;
static uint16_t g_set_voltage = 12000u;
static uint16_t g_set_current = 1000u;
static uint16_t g_cc_cv_mode = 0u;
static uint16_t g_power_state = 1u;
static uint16_t g_fault_state = 0u;
static uint16_t g_state_machine_flags = 0u;
static uint16_t g_state_machine_state = 2u;
static uint16_t g_input_voltage_raw = 2048u;
static uint16_t g_input_current_raw = 128u;
static uint16_t g_output_voltage_raw = 1900u;
static uint16_t g_output_current_raw = 96u;
static uint16_t g_otp_value = 8000u;
static uint16_t g_otp_set_value = 9000u;
static uint16_t g_ovp_value = 13000u;
static uint16_t g_ovp_set_value = 14000u;
static uint16_t g_ocp_value = 1200u;
static uint16_t g_ocp_set_value = 1500u;
static uint16_t g_duty_cmd = 500u;
static uint16_t g_pwm_a_compare = 1000u;
static uint16_t g_pwm_d_compare = 1000u;
static uint16_t g_fan_speed = 2400u;
static uint16_t g_fan_set = 60u;
static uint16_t g_loop_current_feedback = 100u;
static uint16_t g_loop_current_reference = 120u;
static uint16_t g_voltage_loop_current_reference = 110u;

static uint8_t g_tx_buffer[UF4_MAX_FRAME_LEN];
static uint16_t g_tx_len;
static uint16_t g_tx_count;
static uint16_t g_rx_count;
static uint8_t g_rx_cmd;
static uf4_ack_status_t g_ack_status = UF4_ACK_PENDING;

#ifdef _WIN32
static HANDLE g_serial = INVALID_HANDLE_VALUE;
#endif

static uf4_id_t g_id_table[] =
{
    {UF4_ID_INPUT_VOLTAGE, &g_input_voltage, UF4_ID_PERMISSION_RO},
    {UF4_ID_INPUT_CURRENT, &g_input_current, UF4_ID_PERMISSION_RO},
    {UF4_ID_OUTPUT_VOLTAGE, &g_output_voltage, UF4_ID_PERMISSION_RO},
    {UF4_ID_OUTPUT_CURRENT, &g_output_current, UF4_ID_PERMISSION_RO},
    {UF4_ID_CORE_TEMPERATURE, &g_core_temperature, UF4_ID_PERMISSION_RO},
    {UF4_ID_TEMP1_TEMPERATURE, &g_temp1_temperature, UF4_ID_PERMISSION_RO},
    {UF4_ID_TEMP2_TEMPERATURE, &g_temp2_temperature, UF4_ID_PERMISSION_RO},
    {UF4_ID_SET_VOLTAGE_LIMIT, &g_set_voltage, UF4_ID_PERMISSION_RW},
    {UF4_ID_SET_CURRENT_LIMIT, &g_set_current, UF4_ID_PERMISSION_RW},
    {UF4_ID_CC_CV_MODE, &g_cc_cv_mode, UF4_ID_PERMISSION_RO},
    {UF4_ID_POWER_STATE, &g_power_state, UF4_ID_PERMISSION_RW},
    {UF4_ID_FAULT_STATE, &g_fault_state, UF4_ID_PERMISSION_RO},
    {UF4_ID_STATE_MACHINE_FLAG_BITS, &g_state_machine_flags, UF4_ID_PERMISSION_RO},
    {UF4_ID_STATE_MACHINE_STATE, &g_state_machine_state, UF4_ID_PERMISSION_RO},
    {UF4_ID_INPUT_VOLTAGE_RAW, &g_input_voltage_raw, UF4_ID_PERMISSION_RO},
    {UF4_ID_INPUT_CURRENT_RAW, &g_input_current_raw, UF4_ID_PERMISSION_RO},
    {UF4_ID_OUTPUT_VOLTAGE_RAW, &g_output_voltage_raw, UF4_ID_PERMISSION_RO},
    {UF4_ID_OUTPUT_CURRENT_RAW, &g_output_current_raw, UF4_ID_PERMISSION_RO},
    {UF4_ID_OTP_VALUE, &g_otp_value, UF4_ID_PERMISSION_RO},
    {UF4_ID_OTP_SET_VALUE, &g_otp_set_value, UF4_ID_PERMISSION_RW},
    {UF4_ID_OVP_VALUE, &g_ovp_value, UF4_ID_PERMISSION_RO},
    {UF4_ID_OVP_SET_VALUE, &g_ovp_set_value, UF4_ID_PERMISSION_RW},
    {UF4_ID_OCP_VALUE, &g_ocp_value, UF4_ID_PERMISSION_RO},
    {UF4_ID_OCP_SET_VALUE, &g_ocp_set_value, UF4_ID_PERMISSION_RW},
    {UF4_ID_DUTY_CMD, &g_duty_cmd, UF4_ID_PERMISSION_RW},
    {UF4_ID_PWM_A_COMPARE, &g_pwm_a_compare, UF4_ID_PERMISSION_RO},
    {UF4_ID_PWM_D_COMPARE, &g_pwm_d_compare, UF4_ID_PERMISSION_RO},
    {UF4_ID_FAN_SPEED, &g_fan_speed, UF4_ID_PERMISSION_RO},
    {UF4_ID_FAN_SET_VALUE, &g_fan_set, UF4_ID_PERMISSION_RW},
    {UF4_ID_LOOP_CURRENT_FEEDBACK, &g_loop_current_feedback, UF4_ID_PERMISSION_RO},
    {UF4_ID_LOOP_CURRENT_REFERENCE, &g_loop_current_reference, UF4_ID_PERMISSION_RO},
    {UF4_ID_VOLTAGE_LOOP_CURRENT_REFERENCE, &g_voltage_loop_current_reference, UF4_ID_PERMISSION_RO},
};

static void PrintHex(const char *prefix, const uint8_t *data, uint16_t len)
{
    printf("%s", prefix);
    for(uint16_t i = 0u; i < len; i++)
    {
        printf("%02X%s", data[i], (i + 1u == len) ? "" : " ");
    }
    printf("\n");
}

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

static void UpdateDemoValues(uint32_t now_ms)
{
    static uint32_t s_last_ms;

    if((uint32_t)(now_ms - s_last_ms) < 100u)
    {
        return;
    }

    s_last_ms = now_ms;
    g_input_voltage = (uint16_t)(13000u + ((now_ms / 100u) % 300u));
    g_input_current = (uint16_t)(300u + ((now_ms / 200u) % 80u));
    g_output_voltage = (g_set_voltage == 0u) ? 4660u : g_set_voltage;
    g_output_current = (g_set_current == 0u) ? 120u : g_set_current;
    g_core_temperature = (uint16_t)(3600u + ((now_ms / 500u) % 60u));
    g_fan_speed = (uint16_t)(g_fan_set * 40u);
    g_loop_current_feedback = g_output_current;
    g_loop_current_reference = g_set_current;
    g_voltage_loop_current_reference = g_set_voltage / 100u;
}

static int RunSelfTest(void)
{
    uint8_t frame[UF4_MAX_FRAME_LEN];
    uint8_t read_data[] = {
        UF4_ID_INPUT_VOLTAGE, 0x00u, 0x00u,
        UF4_ID_INPUT_CURRENT, 0x00u, 0x00u,
        UF4_ID_OUTPUT_VOLTAGE, 0x00u, 0x00u,
    };
    int failures = 0;

    UF4_IDTableBind(g_id_table, sizeof(g_id_table) / sizeof(g_id_table[0]));

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

#ifdef _WIN32
static int OpenSerial(const char *port_name, uint32_t baudrate)
{
    char path[64];
    DCB dcb;
    COMMTIMEOUTS timeouts;

    if(strncmp(port_name, "\\\\.\\", 4u) == 0)
    {
        snprintf(path, sizeof(path), "%s", port_name);
    }
    else
    {
        snprintf(path, sizeof(path), "\\\\.\\%s", port_name);
    }

    g_serial = CreateFileA(path,
                           GENERIC_READ | GENERIC_WRITE,
                           0u,
                           NULL,
                           OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL,
                           NULL);
    if(g_serial == INVALID_HANDLE_VALUE)
    {
        fprintf(stderr, "cannot open %s, GetLastError=%lu\n", port_name, GetLastError());
        return 0;
    }

    memset(&dcb, 0, sizeof(dcb));
    dcb.DCBlength = sizeof(dcb);
    if(GetCommState(g_serial, &dcb) == 0)
    {
        fprintf(stderr, "GetCommState failed, GetLastError=%lu\n", GetLastError());
        CloseHandle(g_serial);
        g_serial = INVALID_HANDLE_VALUE;
        return 0;
    }

    dcb.BaudRate = baudrate;
    dcb.ByteSize = 8u;
    dcb.Parity = NOPARITY;
    dcb.StopBits = ONESTOPBIT;
    dcb.fBinary = TRUE;
    dcb.fDtrControl = DTR_CONTROL_ENABLE;
    dcb.fRtsControl = RTS_CONTROL_ENABLE;

    if(SetCommState(g_serial, &dcb) == 0)
    {
        fprintf(stderr, "SetCommState failed, GetLastError=%lu\n", GetLastError());
        CloseHandle(g_serial);
        g_serial = INVALID_HANDLE_VALUE;
        return 0;
    }

    memset(&timeouts, 0, sizeof(timeouts));
    timeouts.ReadIntervalTimeout = 20u;
    timeouts.ReadTotalTimeoutConstant = 20u;
    timeouts.WriteTotalTimeoutConstant = 1000u;
    if(SetCommTimeouts(g_serial, &timeouts) == 0)
    {
        fprintf(stderr, "SetCommTimeouts failed, GetLastError=%lu\n", GetLastError());
        CloseHandle(g_serial);
        g_serial = INVALID_HANDLE_VALUE;
        return 0;
    }

    PurgeComm(g_serial, PURGE_RXCLEAR | PURGE_TXCLEAR);
    return 1;
}

static void SerialTx(const uint8_t *data, uint16_t len, void *user)
{
    DWORD written = 0u;
    (void)user;

    if(g_serial == INVALID_HANDLE_VALUE || data == NULL || len == 0u)
    {
        return;
    }

    if(WriteFile(g_serial, data, len, &written, NULL) != 0 && written == len)
    {
        PrintHex("TX ", data, len);
    }
    else
    {
        fprintf(stderr, "serial write failed, GetLastError=%lu\n", GetLastError());
    }
}

static void OnWriteApplied(void *user)
{
    (void)user;
    printf("WRITE applied: set_voltage=%u set_current=%u power=%u fan_set=%u duty=%u\n",
           g_set_voltage,
           g_set_current,
           g_power_state,
           g_fan_set,
           g_duty_cmd);
}

static int RunSerialSlave(const char *port_name, uint32_t baudrate, uint8_t local_id)
{
    uint8_t rx[256];
    DWORD read_len;

    if(OpenSerial(port_name, baudrate) == 0)
    {
        return 1;
    }

    UF4_IDTableBind(g_id_table, sizeof(g_id_table) / sizeof(g_id_table[0]));

    const uf4_config_t config = {
        .local_id = local_id,
        .role = UF4_ROLE_SLAVE,
        .ack_timeout_ms = UF4_DEFAULT_ACK_TIMEOUT_MS,
        .ack_retries = UF4_DEFAULT_ACK_RETRIES,
        .arbitration_slot_ms = UF4_DEFAULT_ARBITRATION_SLOT_MS,
    };
    UF4_InitEx(&config, SerialTx, NULL);
    UF4_SetWriteApplyCallback(OnWriteApplied, NULL);

    printf("UF4COM C slave ready: port=%s baud=%lu id=0x%02X\n",
           port_name,
           (unsigned long)baudrate,
           local_id);
    printf("Open Python GUI on the paired port, default master COM8 -> slave COM9.\n");

    for(;;)
    {
        read_len = 0u;
        if(ReadFile(g_serial, rx, sizeof(rx), &read_len, NULL) == 0)
        {
            fprintf(stderr, "serial read failed, GetLastError=%lu\n", GetLastError());
            break;
        }

        if(read_len > 0u)
        {
            PrintHex("RX ", rx, (uint16_t)read_len);
            (void)UF4_InputBuffer(rx, (uint16_t)read_len);
        }

        UpdateDemoValues(GetTickCount());
        UF4_ProcessMs(GetTickCount());
        Sleep(1u);
    }

    CloseHandle(g_serial);
    g_serial = INVALID_HANDLE_VALUE;
    return 1;
}
#else
static int RunSerialSlave(const char *port_name, uint32_t baudrate, uint8_t local_id)
{
    (void)port_name;
    (void)baudrate;
    (void)local_id;
    fprintf(stderr, "serial slave demo currently uses Win32 serial APIs.\n");
    return 1;
}
#endif

int main(int argc, char **argv)
{
    const char *port_name;
    uint32_t baudrate = 115200u;
    uint8_t local_id = 0x11u;

    if(argc <= 1 || strcmp(argv[1], "--self-test") == 0)
    {
        return RunSelfTest();
    }

    if(strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0)
    {
        printf("Usage:\n");
        printf("  UF4COM_C_SLAVE_PC.exe --self-test\n");
        printf("  UF4COM_C_SLAVE_PC.exe COM9 [baudrate] [local_id]\n");
        return 0;
    }

    port_name = argv[1];
    if(argc >= 3)
    {
        baudrate = (uint32_t)strtoul(argv[2], NULL, 0);
    }
    if(argc >= 4)
    {
        local_id = (uint8_t)strtoul(argv[3], NULL, 0);
    }

    return RunSerialSlave(port_name, baudrate, local_id);
}
