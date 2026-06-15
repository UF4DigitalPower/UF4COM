# AI.MD

## 项目定位

UF4COM 是 UF4 通信协议 V3 的 C11 实现，目标是在 UART、USB CDC、RS485、TCP 透传等链路上复用同一套帧编码、CRC16、流式解析和命令调度逻辑。

## 当前维护状态

- 日期：2026-06-13
- 维护者：Codex
- 当前重点：把协议文档中的帧格式落成可编译、可自测、可移植的基础库。

## 本次完善内容

- 补齐公共协议 API：帧常量、命令码、错误码、发送回调、帧构建、输入解析入口。
- 补齐 README 数据 ID 对应的 `UF4_ID_*` 宏，避免应用层继续手写裸 ID。
- 增加 `UF4_SetWriteApplyCallback()`，应用层可在 `WRITE_REQ` 成功写入影子寄存器后同步业务变量。
- 实现流式解析器：支持按字节输入、SOF 同步、DATA 收集、CRC16 校验，校验通过后产出完整帧。
- 整理 ID 表：从头文件中的全局数组改成 `UF4_IDTableBind()` 注册，避免多重定义和未声明变量。
- 完善命令调度：支持 READ、WRITE、STREAM_START、STREAM_STOP、STREAM_DATA，并对非法长度、非法 ID、写保护、非法命令返回错误帧。
- 增加发送封装：`UF4_BuildFrame()` 负责生成完整帧，`UF4_SendFrame()` 通过应用层回调发出。
- 增加 UART 接收占位：`UF4_UART_RxByte()` 将字节交给协议解析器，便于后续接 STM32 HAL UART/CDC 回调。
- 将 `main.c` 改为最小自测程序，验证 READ 请求编码、解析和响应。
- 调整 CMake：启用 C11 必需项，并为当前 GNU/MinGW 工具链使用 `-g0`，避开当前汇编器不支持 DWARF 调试信息选项的问题。
- 修订 `README.md`：补充 VALUE 字节序、LEN 约束、错误帧 DATA 格式、CRC 错误处理说明和当前 C 代码接入方式。

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

## 验证记录

- 2026-06-13：执行 CMake 构建并运行 `UF4COM.exe`，自测输出 `UF4COM self-test passed.`。
- 2026-06-13：UF4DigitalPower 已通过 CMake 直接引用本项目协议核心，旧 TVLCOM TLV/Modbus CRC 固件协议已移除，完整数据 ID 在固件 `Core/Src/tvlcom.c` 中绑定。
