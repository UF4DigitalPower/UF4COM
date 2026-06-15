## 项目定位

UF4COM 是 UF4 通信协议 V3 的 C11 实现，目标是在 UART、USB CDC、RS485、TCP 透传等链路上复用同一套帧编码、CRC16、流式解析和命令调度逻辑。

## 关键文件

- `README.md`：协议 V3 文档，描述帧格式、CRC、命令、数据 ID 和错误码。
- `uf4com/protocol/Inc/uf4com.h`：公共协议 API。
- `uf4com/protocol/Src/uf4com.c`：帧构建、发送回调、全局输入入口。
- `uf4com/protocol/Src/uf4com_parser.c`：流式解析器。
- `uf4com/protocol/Src/uf4com_cmd.c`：命令调度和错误响应。
- `uf4com/register/Inc/uf4com_id_table.h`：ID 表注册和查找接口。
- `main.c`：PC 侧最小自测入口。

## 设计约定

- CRC16 参数保持 README 定义：Poly `0x1021`，Init `0xFFFF`，XorOut `0x0000`。
- CRC 覆盖范围为 `SEQ FLAGS CMD LEN DATA`，不包含 `SOF` 和 `CRC16`。
- 帧内 CRC 字节序为小端：`CRC_L CRC_H`。
- TV 数据项固定为 3 字节：`ID VALUE_H VALUE_L`。
- 温度类 ID 使用 `0.01℃` 单位，即摄氏度乘以 100，保留两位小数并适配 `uint16_t` VALUE。
- 错误帧 DATA 固定为 `FF ERR_H ERR_L`。
- `rw == 0` 表示只读，`rw != 0` 表示可写。
- 协议层不直接依赖 HAL，底层发送由 `UF4_Init(tx, user)` 注入。

## 后续建议

- 为 STM32 工程增加适配文件，例如 `uf4com_hal_uart.c`、`uf4com_usb_cdc.c`，只负责收发字节，不改协议核心。
- 给 WRITE、错误帧、STREAM_START/STOP 再补几组自测，覆盖异常路径。
- 如果需要在多个设备产品间复用 ID 定义，建议把具体 ID 表放到应用层，由产品工程注册。
- 可以增加 `UF4_SetOnFrameHook()` 或事件回调，方便调试器记录原始请求和响应。