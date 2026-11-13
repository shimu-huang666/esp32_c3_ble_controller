# 首次交付验证记录

日期：2026-11-12。目标：ESP32-C3 Supermini，采用常见的 4 MB Flash 配置。

## 已完成

- 核对本机 ESP-IDF：工具显示 v6.1，生成配置头文件显示 6.1.0；使用用户指定的已安装 SDK，没有升级或修改 SDK。
- 先编译官方空白模板项目，再增加 BLE 功能。空白基线保留在 main，功能位于独立分支。
- 使用 RISC-V GCC 工具链执行下列命令，退出码为 0；生成应用镜像，1 MB 应用分区检查通过。

```powershell
idf.py reconfigure build
```

- 使用 Espressif clang-format 21.1.3 对 main 下全部 C 文件和头文件执行格式化与严格检查，通过。
- 两个仓库的 ble_protocol.h 内容完全一致。
- 验收脚本的 Python 语法检查、帮助入口检查通过；非法 count、timeout 参数在打开串口前被拒绝。
- 对照本机 SDK 做了独立只读审查，覆盖广播长度、通知构造、写入校验、角色配置和断连路径，未发现重要问题。
- 执行 Git 差异空白检查，通过；构建目录、生成配置和日志由 .gitignore 排除。

SDK 来源：[Espressif 官方提交](https://github.com/espressif/esp-idf/tree/fff9895c82d744c7237be8847347bdd1b07c6643)。

## 编译提示

配置过程中 SDK 自身产生 esp_wifi / wpa_supplicant 私有包含目录的五项 CMake 警告，以及部分 Kconfig 格式、弃用提示。项目编译与链接成功；没有为消除这些 SDK 提示修改其源文件。

## 尚未验证

未烧录板卡，未运行真实串口收发验收，未验证实际无线距离、USB 日志输出、断电与重连表现。C3 端脚本只统计请求接收和通知调度；完整无线回复必须以 S3 收到 PONG 为准。

板端验收步骤见 README：两板烧录并上电，检查两端收发，再分别重启 S3 和 C3，确认恢复通信。
