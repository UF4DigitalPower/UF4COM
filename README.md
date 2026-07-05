# UF4COM V4

UF4COM 是 UF4 通信协议的 C11 实现，目标是在 UART、USB CDC、RS485、TCP 透传等字节流链路上复用同一套帧编码、CRC16、流式解析、命令调度和主从站请求响应逻辑。

V4 在 V3 基础上增加了设备 ID、主从站寻址、ACK 请求表、超时重发、SEQ/CMD 匹配和简单仲裁 slot 计算。协议层仍不直接依赖 HAL，底层发送由应用注入。

## 关键文件

- `uf4com/protocol/Inc/uf4com.h`：公共协议 API、帧格式、错误码和 V4 配置。
- `uf4com/protocol/Src/uf4com.c`：帧构建、发送回调、ACK 请求表、超时重发、全局输入入口。
- `uf4com/protocol/Src/uf4com_parser.c`：V4 流式解析器。
- `uf4com/protocol/Src/uf4com_cmd.c`：READ/WRITE/STREAM 命令调度和错误响应。
- `uf4com/register/Inc/uf4com_id_table.h`：ID 表注册和查找接口。
- `main.c`：PC 侧最小自测入口。

## 设计约定

- CRC16 参数：Poly `0x1021`，Init `0xFFFF`，XorOut `0x0000`。
- CRC 覆盖范围为 `SRC DST SEQ FLAGS CMD LEN DATA`，不包含 `SOF` 和 `CRC16`。
- 帧内 CRC 字节序为小端：`CRC_L CRC_H`。
- `SRC` 是发送方设备 ID，`DST` 是接收方设备 ID。
- `0x00` 默认是主站 ID，`0xFF` 是广播 ID。
- 广播请求不会产生 ACK/响应，避免一对多同时回包撞线。
- TV 数据项固定为 3 字节：`ID VALUE_H VALUE_L`。
- `rw == 0` 表示只读，`rw != 0` 表示可写。
- `ACK_REQ` 只对非广播请求生效；接收端仅在请求带 `ACK_REQ` 时返回 `ACK_FRAME`。

## 1. 帧格式

| 字段 | 长度(Byte) | 示例 | 说明 |
|---|---:|---|---|
| SOF | 2 | AA 55 | 固定帧头 |
| SRC | 1 | 00 | 发送方设备 ID |
| DST | 1 | 11 | 接收方设备 ID |
| SEQ | 1 | 01 | 帧序号 |
| FLAGS | 1 | 01 | 控制标志 |
| CMD | 1 | 01 | 命令码 |
| LEN | 1 | 09 | DATA 总字节数 |
| DATA | N | 0A 00 00 | 数据区 |
| CRC16 | 2 | XX XX | CRC 校验 |

最大 DATA 长度为 255 字节，最大整帧长度为 265 字节。

## 2. 主从站设备 ID

推荐拓扑：

```text
Master ID 0x00  <---- RS485/UART/USB CDC/TCP ---->  Slave ID 0x01
                                                    Slave ID 0x02
                                                    Slave ID 0x03
```

规则：

- 主站主动发起 READ/WRITE/STREAM_START/STREAM_STOP。
- 从站只处理 `DST == 本机 ID` 或 `DST == 0xFF` 的帧。
- 从站响应时 `SRC = 本机 ID`，`DST = 请求帧 SRC`。
- 主站请求多个从站时应逐个轮询，或使用广播请求让所有从站进入同一状态。
- 广播请求不返回 ACK，避免多个从站同时响应。

## 3. 仲裁 slot

V4 提供 `UF4_GetArbitrationDelayMs(device_id)`，按设备 ID 计算发送 slot：

```text
delay_ms = device_id * arbitration_slot_ms
```

设备 ID 越小，delay 越小，优先级越高。默认 `arbitration_slot_ms = 2`。

注意：这是软件层的 slot 计算，不等于完整物理总线仲裁。RS485 一对多场景仍建议主站轮询；如果多个从站必须主动上传，应用层应按该 delay 延迟发送，并结合驱动层的总线空闲检测。

## 4. FLAGS 定义

| Bit | Mask | 名称 | 说明 |
|---|---|---|---|
| 0 | 0x01 | ACK_REQ | 请求 ACK/响应 |
| 1 | 0x02 | ACK_FRAME | ACK/响应帧 |
| 2 | 0x04 | ERROR | 错误响应 |
| 3 | 0x08 | BROADCAST | 广播帧提示 |
| 4~7 | - | Reserved | 保留 |

示例：

| FLAGS | 含义 |
|---|---|
| 0x00 | 普通数据帧 |
| 0x01 | 请求 ACK |
| 0x02 | ACK 响应 |
| 0x06 | ACK 响应 + 错误 |
| 0x09 | 广播请求 |

## 5. ACK 请求表、超时和重发

发送方调用 `UF4_SendFrameTo()`，且 `FLAGS` 包含 `UF4_FLAG_ACK_REQ` 时，协议层会登记请求表：

- 匹配条件：`响应 SRC == 请求 DST`、`响应 SEQ == 请求 SEQ`、`响应 CMD == 请求 CMD 对应的 RSP CMD`。
- 默认超时：`50 ms`。
- 默认重发：`3` 次。
- 请求表容量：`8` 个未完成请求。
- 广播请求不会进入请求表。

应用层需要周期调用：

```c
UF4_ProcessMs(now_ms);
```

如果仍使用旧接口 `UF4_Process()`，内部会按每次调用递增 1 ms 处理，但正式固件建议使用真实毫秒 tick。

可注册 ACK 事件：

```c
UF4_SetAckEventCallback(MyAckEvent, user_ctx);
```

主站需要读取 `READ_RSP` 或 `STREAM_DATA` 时，注册完整帧回调：

```c
UF4_SetFrameRxCallback(MyFrameRx, user_ctx);
```

`ACK_FRAME` 会先完成请求表匹配，再交给帧回调，因此应用层可以同时获得 ACK 状态和响应 payload。

状态包括：

| 状态 | 说明 |
|---|---|
| `UF4_ACK_DONE` | 收到匹配 ACK/响应 |
| `UF4_ACK_TIMEOUT` | 重发次数耗尽 |
| `UF4_ACK_DROPPED` | 请求表已满或无法登记 |

## 6. CMD 定义

| CMD(hex) | 名称 | 方向 |
|---|---|---|
| 0x01 | READ_REQ | Master -> Slave |
| 0x81 | READ_RSP | Slave -> Master |
| 0x02 | WRITE_REQ | Master -> Slave |
| 0x82 | WRITE_RSP | Slave -> Master |
| 0x10 | STREAM_START_REQ | Master -> Slave |
| 0x90 | STREAM_START_RSP | Slave -> Master |
| 0x11 | STREAM_STOP_REQ | Master -> Slave |
| 0x91 | STREAM_STOP_RSP | Slave -> Master |
| 0x15 | STREAM_DATA | Slave -> Master |

## 7. DATA 格式

采用固定长度 TV 格式。

| 字段 | 长度(Byte) | 说明 |
|---|---:|---|
| ID | 1 | 数据 ID |
| VALUE | 2 | `VALUE_H VALUE_L`，大端 |

READ/WRITE/STREAM 请求中，`LEN` 必须是 3 的整数倍。无 DATA 响应时 `LEN = 0`。

## 8. 错误响应

错误帧格式：

| 字段 | 值 |
|---|---|
| FLAGS | `UF4_FLAG_ACK_FRAME | UF4_FLAG_ERROR` |
| CMD | 对应响应 CMD |
| LEN | 03 |
| DATA | `FF ERR_H ERR_L` |

CRC 错误通常只能丢弃该帧；如果帧无法通过 CRC 校验，接收端无法可靠获取 `SRC/DST/SEQ/CMD`，因此不保证返回 `CRC_ERROR` 帧。

## 9. 错误码定义

| 错误码 | 名称 | 说明 |
|---|---|---|
| 0x0000 | OK | 成功 |
| 0x0001 | INVALID_ID | ID 不存在 |
| 0x0002 | INVALID_CMD | 命令错误 |
| 0x0003 | CRC_ERROR | CRC 错误 |
| 0x0004 | INVALID_LEN | 长度错误 |
| 0x0005 | WRITE_PROTECT | 不允许写入 |
| 0x0006 | OUT_OF_RANGE | 参数超范围 |
| 0x0007 | DEVICE_BUSY | 设备忙 |
| 0x0008 | INTERNAL_ERROR | 内部错误 |
| 0x0009 | TIMEOUT | ACK 超时 |
| 0x000A | NO_PENDING_SLOT | 请求表无空位 |
| 0x000B | NOT_FOR_THIS_DEVICE | 目标设备不匹配 |
| 0x000C | ARBITRATION | 仲裁失败 |

## 10. C 代码接入方式

初始化从站：

```c
static const uf4_config_t config = {
    .local_id = 0x11,
    .role = UF4_ROLE_SLAVE,
    .ack_timeout_ms = 50,
    .ack_retries = 3,
    .arbitration_slot_ms = 2,
};

UF4_IDTableBind(g_id_table, table_count);
UF4_InitEx(&config, MyTxBytes, user_ctx);
```

主站发送读请求：

```c
uint8_t data[] = {
    UF4_ID_INPUT_VOLTAGE, 0x00, 0x00,
    UF4_ID_INPUT_CURRENT, 0x00, 0x00,
};

UF4_SendFrameTo(0x11,
                seq,
                UF4_FLAG_ACK_REQ,
                UF4_CMD_READ_REQ,
                data,
                sizeof(data));
```

接收入口：

```c
UF4_InputByte(byte);
```

周期任务：

```c
UF4_ProcessMs(system_ms);
```

## 11. 数据 ID 定义

| ID(hex) | 名称 | RW | 单位 | 说明 |
|---|---|---|---|---|
| 0x0A | INPUT_VOLTAGE | R | mV | 输入电压 |
| 0x0B | INPUT_CURRENT | R | mA | 输入电流 |
| 0x0C | OUTPUT_VOLTAGE | R | mV | 输出电压 |
| 0x0D | OUTPUT_CURRENT | R | mA | 输出电流 |
| 0x0E | CORE_TEMPERATURE | R | 0.01C | 核心温度 |
| 0x0F | TEMP1_TEMPERATURE | R | 0.01C | 第一温度 |
| 0x10 | TEMP2_TEMPERATURE | R | 0.01C | 第二温度 |
| 0x11 | SET_VOLTAGE_LIMIT | RW | mV | 电压设定值 |
| 0x12 | SET_CURRENT_LIMIT | RW | mA | 电流设定值 |
| 0x14 | CC_CV_MODE | R | - | CC/CV 模式 |
| 0x15 | POWER_STATE | RW | - | 电源状态 |
| 0x16 | FAULT_STATE | R | - | 故障状态 |
| 0x17 | STATE_MACHINE_FLAG_BITS | R | - | 状态机标志 |
| 0x18 | STATE_MACHINE_STATE | R | - | 状态机状态 |
| 0x19 | INPUT_VOLTAGE_RAW | R | ADC | 原始输入电压 |
| 0x1A | INPUT_CURRENT_RAW | R | ADC | 原始输入电流 |
| 0x1B | OUTPUT_VOLTAGE_RAW | R | ADC | 原始输出电压 |
| 0x1C | OUTPUT_CURRENT_RAW | R | ADC | 原始输出电流 |
| 0x1D | OTP_VALUE | R | 0.01C | 当前 OTP 值 |
| 0x1E | OTP_SET_VALUE | RW | 0.01C | OTP 设定值 |
| 0x1F | OVP_VALUE | R | mV | 当前 OVP 值 |
| 0x20 | OVP_SET_VALUE | RW | mV | OVP 设定值 |
| 0x21 | OCP_VALUE | R | mA | 当前 OCP 值 |
| 0x22 | OCP_SET_VALUE | RW | mA | OCP 设定值 |
| 0x23 | DUTY_CMD | RW | permille | 占空比命令 |
| 0x24 | PWM_A_COMPARE | R | Tick | PWM_A 比较值 |
| 0x25 | PWM_D_COMPARE | R | Tick | PWM_D 比较值 |
| 0x26 | FAN_SPEED | R | RPM | 风扇转速 |
| 0x27 | FAN_SET_VALUE | RW | % | 风扇设定值 |
| 0x29 | LOOP_CURRENT_FEEDBACK | R | ADC | 电流反馈 |
| 0x2A | LOOP_CURRENT_REFERENCE | R | ADC | 电流参考 |
| 0x2B | VOLTAGE_LOOP_CURRENT_REFERENCE | R | ADC | 电压环参考 |
