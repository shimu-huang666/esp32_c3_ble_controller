# ESP32-C3 最小 BLE 主控

使用 ESP-IDF v6.1，从官方空白模板建立。C3 广播自己的通信服务，接受 S3 连接；收到合法 PING 后调度 PONG 通知，断连后恢复广播。

## 编译与烧录

用 VS Code 的 ESP-IDF 插件打开本目录，选择已安装的 v6.1，在 ESP-IDF 终端编译。项目已固定目标为 esp32c3。

编译：

```powershell
idf.py build
```

烧录并查看串口日志，将 COM8 替换为 C3 的实际串口：

```powershell
idf.py -p COM8 flash monitor
```

配套 S3 固件也需烧录并上电。C3 的广播名为 `BLE-C3-Controller`。连接并开启通知后，日志持续出现 `RX: PING` 和 `TX: PONG queued`；queued 表示已交给协议栈排队，实际接收以 S3 的 PONG 日志为准。退出串口监视器使用 Ctrl+]。

## 通信方式

BLE 是 Bluetooth Low Energy，低功耗蓝牙。C3 担任外围设备，负责广播；S3 担任中央设备，负责扫描和连接。GATT 是 Generic Attribute Profile，通用属性规范，通过“服务”和“特征”组织可读写的数据。

- 服务 UUID（通用唯一标识符）放入广播包，设备名放入扫描响应，避免超过传统广播包的 31 字节长度限制。
- 单个特征支持读取、写入和通知。读取返回 PONG；写入只接受四字节 PING；非法长度或内容返回 ATT（Attribute Protocol，属性协议）错误。
- S3 必须先开启通知订阅。C3 使用官方 ble_gatts_chr_updated 调度回复，由 NimBLE 跟踪订阅并构造通知。
- PING 和 PONG 均为四字节 ASCII 文本，不附加字符串结束符或换行。当前单连接、无配对认证，服务 UUID 仅用于匹配协议，不代表设备身份认证。

服务 UUID：

```text
d58f7e60-4c93-4f7b-9ba7-664e114dab7c
```

特征 UUID：

```text
d58f7e61-4c93-4f7b-9ba7-664e114dab7c
```

请求文本：

```text
PING
```

请求十六进制字节：

```text
50 49 4E 47
```

回复文本：

```text
PONG
```

回复十六进制字节：

```text
50 4F 4E 47
```

## 核心代码与配置

- main/main.c：初始化 NVS 和 NimBLE，启动官方主机任务。NVS 是 Non-Volatile Storage，片上持久存储，官方例程用它保存射频校准等数据。
- main/ble_controller.c：注册服务、校验写入、调度回复、处理广播与断连恢复。使用 mbuf（协议栈的链式数据缓冲区）复制接口，不能假定接收数据在内存中连续，也不能假定它带字符串结束符。
- main/ble_protocol.h：服务 UUID、特征 UUID 与 PING/PONG 定义。两个独立仓库各保存一份，修改协议时同步两份。
- sdkconfig.defaults：开启 NimBLE，限制为一个连接；按 ESP32-C3 Supermini 常见的 4 MB Flash 配置。
- 串口日志同时输出到默认 UART0 和原生 USB Serial/JTAG，便于从 Supermini 的 USB 接口查看。
- .clang-format：Allman 风格，左大括号独占一行，四空格缩进；.vscode/settings.json 让 C/C++ 扩展按此文件格式化。

修改前这里只是空白 app_main。现在保留官方广播与服务注册流程，把演示数据处理改成“检查长度 → 复制数据 → 比较 PING → 调度 PONG 通知”。在校验失败时返回错误，避免把非法数据当成有效命令。

## 验证

编译验证和板端验证是两件事。编译成功只能说明代码、配置、链接与固件空间检查通过，不能证明真实无线链路可用。

板端快速验收：先给两板烧录并上电，关闭占用 C3 串口的监视器，再运行下列脚本。它使用 ESP-IDF 环境已有的 pyserial，默认在 30 秒内要求至少三次 PING 接收与 PONG 调度。完整的无线回复需同时在 S3 端验收。

```powershell
python tests/verify_ble.py --port COM8
```

随后重启 S3，确认 C3 断连后恢复广播，重新连接后继续响应；也检查 C3 重启后的恢复行为。

首次交付的验证结果见 [docs/verification.md](docs/verification.md)。本阶段不接入 INMP441、SG90 或电源控制。

## 官方来源

- [空白项目模板](https://github.com/espressif/esp-idf/tree/v6.1/tools/templates/sample_project)
- [bleprph 外围设备示例](https://github.com/espressif/esp-idf/tree/v6.1/examples/bluetooth/nimble/bleprph)
- [官方 GATT Server 入门示例](https://github.com/espressif/esp-idf/tree/v6.1/examples/bluetooth/ble_get_started/nimble/NimBLE_GATT_Server)

本机 SDK 标签为 v6.1，对应配置头文件版本 6.1.0。代码复用对应版本的官方示例，并保留 Apache-2.0 许可与 NOTICE；未新增第三方蓝牙库。
