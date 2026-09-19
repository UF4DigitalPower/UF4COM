# UF4COM V5

UF4COM 是 UF4 通信协议的 C11 实现，用于一条点对点 UART、USB CDC 或 TCP 字节流链路上的帧编码、CRC16、流式解析、命令调度和请求响应。

V5 删除帧中的 SRC/DST 设备地址，不兼容 V4 的线上格式和带地址 API；数据项 ID 仍保留，并由 ID 表标记 RO/RW 权限。另增加 SAVE 命令、STM32G474 数据 ID、解析器复位/空闲查询。协议层不直接依赖 HAL，底层发送和存储由应用注入。

## 关键文件

- `uf4com/protocol/Inc/uf4com.h`：公共协议 API、帧格式、错误码和 V5 配置。
- `uf4com/protocol/Src/uf4com.c`：帧构建、发送回调、ACK 请求表、超时重发、全局输入入口。
- `uf4com/protocol/Src/uf4com_parser.c`：V5 流式解析器。
- `uf4com/protocol/Src/uf4com_cmd.c`：READ/WRITE/STREAM 命令调度和错误响应。
- `uf4com/register/Inc/uf4com_id_table.h`：ID 表注册和查找接口。
- `main.c`：PC 侧最小自测入口。

## 设计约定

- CRC16 参数：Poly `0x1021`，Init `0xFFFF`，XorOut `0x0000`。
- CRC 覆盖范围为 `SEQ FLAGS CMD LEN DATA`，不包含 `SOF` 和 `CRC16`。
- 帧内 CRC 字节序为小端：`CRC_L CRC_H`。
- 每个协议实例只处理一条点对点链路，没有设备地址、广播或总线仲裁；多端口路由由应用层负责。
- TV 数据项固定为 3 字节：`ID VALUE_H VALUE_L`。
- ID 表包含 `id`、`uint16_t *ptr` 和 `rw`。`UF4_ID_PERMISSION_RO` 禁止 WRITE，`UF4_ID_PERMISSION_RW` 允许 WRITE；整个 WRITE 包先检查 ID 与权限，再统一写入。参数范围、来源鉴权和输出安全仍由应用负责。
- 接收端仅在请求带 `ACK_REQ` 时返回 `ACK_FRAME`。

## 1. 帧格式

| 字段    | 长度(Byte) | 示例       | 说明        |
|-------|---------:|----------|-----------|
| SOF   |        2 | AA 55    | 固定帧头      |
| SEQ   |        1 | 01       | 帧序号       |
| FLAGS |        1 | 01       | 控制标志      |
| CMD   |        1 | 01       | 命令码       |
| LEN   |        1 | 09       | DATA 总字节数 |
| DATA  |        N | 0A 00 00 | 数据区       |
| CRC16 |        2 | XX XX    | CRC 校验    |

最大 DATA 长度为 255 字节，最大整帧长度为 263 字节。V5 不提供旧版 `AA 55 SRC DST ...` 帧的解析或兼容入口；测试中的有效 V4 帧会被拒收。

## 2. FLAGS 定义

| Bit | Mask | 名称        | 说明        |
|-----|------|-----------|-----------|
| 0   | 0x01 | ACK_REQ   | 请求 ACK/响应 |
| 1   | 0x02 | ACK_FRAME | ACK/响应帧   |
| 2   | 0x04 | ERROR     | 错误响应      |
| 3~7 | -    | Reserved  | 保留        |

示例：

| FLAGS | 含义          |
|-------|-------------|
| 0x00  | 普通数据帧       |
| 0x01  | 请求 ACK      |
| 0x02  | ACK 响应      |
| 0x06  | ACK 响应 + 错误 |

## 3. ACK 请求表、超时和重发

发送方调用 `UF4_SendFrame()`，且 `FLAGS` 包含 `UF4_FLAG_ACK_REQ` 时，协议层会登记请求表：

- 匹配条件：`响应 SEQ == 请求 SEQ`、`响应 CMD == 请求 CMD 对应的 RSP CMD`。同一链路上不能同时发起相同 SEQ/响应 CMD 的未完成请求。
- 默认超时：`50 ms`。
- 默认重发：`3` 次。
- 请求表容量：`8` 个未完成请求。

应用层需要周期调用：

```c
UF4_ProcessMs(now_ms);
```

`UF4_Process()` 按每次调用递增 1 ms；正式固件应使用真实毫秒 tick。

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

## 4. CMD 定义

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
| 0x12 | SAVE_REQ | Master -> Slave |
| 0x92 | SAVE_RSP | Slave -> Master |
| 0x15 | STREAM_DATA | Slave -> Master |

## 5. DATA 格式

采用固定长度 TV 格式。

| 字段 | 长度(Byte) | 说明 |
|---|---:|---|
| ID | 1 | 数据 ID |
| VALUE | 2 | `VALUE_H VALUE_L`，大端 |

READ/WRITE/STREAM 请求中，`LEN` 必须是 3 的整数倍。无 DATA 响应时 `LEN = 0`。

## 6. 错误响应

错误帧格式：

| 字段 | 值 |
|---|---|
| FLAGS | `UF4_FLAG_ACK_FRAME | UF4_FLAG_ERROR` |
| CMD | 对应响应 CMD |
| LEN | 03 |
| DATA | `FF ERR_H ERR_L` |

CRC 错误直接丢弃；接收端无法可靠获取 `SEQ/CMD`，因此不返回错误帧。

## 7. 错误码定义

| 错误码 | 名称 | 说明 |
|---|---|---|
| 0x0000 | OK | 成功 |
| 0x0001 | INVALID_ID | ID 不存在 |
| 0x0002 | INVALID_CMD | 命令错误 |
| 0x0003 | CRC_ERROR | CRC 错误 |
| 0x0004 | INVALID_LEN | 长度错误 |
| 0x0005 | WRITE_PROTECT | WRITE 包含 RO ID |
| 0x0006 | OUT_OF_RANGE | 参数超范围 |
| 0x0007 | DEVICE_BUSY | 设备忙 |
| 0x0008 | INTERNAL_ERROR | 内部错误 |
| 0x0009 | TIMEOUT | ACK 超时 |
| 0x000A | NO_PENDING_SLOT | 请求表无空位 |

## 8. C 代码接入方式

初始化协议端点：

```c
static const uf4_config_t config = {
    .ack_timeout_ms = 50,
    .ack_retries = 3,
};

UF4_IDTableBind(g_id_table, table_count);
UF4_InitEx(&config, MyTxBytes, user_ctx);
```

发送读请求：

```c
uint8_t data[] = {
    UF4_ID_INPUT_VOLTAGE, 0x00, 0x00,
    UF4_ID_INPUT_CURRENT, 0x00, 0x00,
};

UF4_SendFrame(seq, UF4_FLAG_ACK_REQ, UF4_CMD_READ_REQ,
              data, sizeof(data));
```

接收入口：

```c
UF4_InputByte(byte);
```

周期任务：

```c
UF4_ProcessMs(system_ms);
```

## 9. 数据 ID 定义

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

上表的 R/RW 对应示例 ID 表的权限配置。STM32G474 中新增的 `0x13`、`0x28`、`0x2C`~`0x56` 常量定义见 `uf4com.h`；参数管理器注册时逐项指定权限，应用仅绑定本机实际支持的 ID。

## V5 SAVE 与移植

`SAVE_REQ (0x12)` 的 `LEN` 必须为 0。先注册 `UF4_SetSaveRequestCallback(StartSave, user)`；回调只启动存储事务，不在接收/中断上下文阻塞写 Flash。应用在前台确认提交或失败后调用 `UF4_CompleteSave(UF4_OK)` 或相应错误码，协议层才发送 `SAVE_RSP (0x92)`。同 SEQ 的超时重发不会重复启动保存；没有注册回调时返回 `INTERNAL_ERROR`。无 `ACK_REQ` 的 SAVE 不回包。`UF4_InitEx` 会取消未完成事务，重复初始化后应用应重新确认自身存储状态。

STM32G474 的专属实现仍留在原工程，移植时重点核对：

- 双 UART (USART1 PC、USART2 F429) 的 Receive-to-Idle DMA 缓冲和各自 RX 队列；一条链路的半帧不能串入另一条。切换输入端口时用 `UF4_ResetParser()`，可通过 `UF4_IsInputIdle()` 判断半帧；复位解析器不会清除 ACK 表。
- TX 回调必须复制 `UF4_SendFrame()` 的栈上帧到持久队列后才交给异步 HAL 发送；多端口场景须在应用层将回复路由到请求来源端口，并隔离各端口的协议状态。队列溢出与 HAL 错误应计数处理。
- 前台调用 `UF4_ProcessMs(HAL_GetTick())` 做超时与流任务；流上传在应用侧限速（G474 为 50 ms），不应随前台循环无限发送。参数管理器负责 SAVE 完成及写后生效，链路超时、来源鉴权、输出使能撤销与数值范围检查属于应用安全策略，不在通用协议层。
- STM32 参数管理器注册 ID 时保留 `rw` 字段，并逐项核对 RO/RW、来源及输出安全策略。V5 帧已去掉设备地址，现有固件协议仍需单独移植，不能只替换头文件。
