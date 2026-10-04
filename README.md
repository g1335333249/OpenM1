# OpenM1 v0.4.0

**开发版 / 实验版。** OpenM1 是斐讯悟空 M1 的开源固件项目。当前固件替换 EMW3080B/MK3080B 上的 MiCO/MOC 用户 APP，不刷写 ATSAMD20G17A。SDK 固定为 [MXCHIP/mico-os `9b09de78164940ff3876d2053f8e7dd42ca2b8ba`](https://github.com/MXCHIP/mico-os/tree/9b09de78164940ff3876d2053f8e7dd42ca2b8ba)，使用同 SDK 的 `3080B002.023` Kernel 和 ARM GCC 5.4.1。

## 实机验证状态

用户已在真实 M1 上验证 v0.2.0 的 Recovery SoftAP、动态 SSID、`192.168.4.1`、中文 Web 页面、AP+STA 家庭 Wi-Fi 连接、网页 OTA、Bootloader 应用升级及升级后自动恢复。v0.3.0 实机验证 UART1 初始化成功、115200 8N1 在线，但持续观察 `rx_bytes=0`。OTA_TEMP 分区起始 `0x00110000`、长度 `0xB5000`（741376 字节）。**v0.4.0 的扫描、MQTT、Home Assistant 以及 v0.3.1 启动帧响应和传感器数值均尚未实机验证；URL OTA 也尚未实机验证。**

恢复热点名称由设备 Wi-Fi MAC 的最后 3 字节生成，格式为 **`OpenM1-XXXXXX`**，例如 MAC `34:EA:34:12:AB:CD` 对应 `OpenM1-12ABCD`。若 MAC 读取结果无效，则使用 `OpenM1-RECOVERY` 并输出故障日志。热点开放、无密码，地址固定为 `192.168.4.1/24`，DHCP Server 开启，HTTP 监听 TCP 80。任何家庭 Wi-Fi 凭据只保存在运行时 RAM；**每次重启都会先启动恢复热点和 OTA 服务，不自动连接家庭 Wi-Fi。** 不要在不可信网络暴露此无认证管理界面。

## 页面与接口

访问 [http://192.168.4.1](http://192.168.4.1)。页面为内嵌 UTF-8 中文 HTML/CSS/JavaScript，无外部 CDN。实时环境数据、设备信息、网络状态、手动家庭 Wi-Fi 连接、固件升级、串口诊断及重启均在首页。OTA 成功后页面将把连接中断视为正常重启过程，每 2 秒探测 `/api/health`，重新上线后刷新版本信息。

| 路由 | 功能 |
| --- | --- |
| `GET /` | 中文管理页 |
| `GET /api/health` | Recovery 存活检查 |
| `GET /api/info` | 设备版本、RF、IP、运行时间、空闲堆 |
| `GET /api/ota/status` | OTA 状态、进度和错误 |
| `POST /api/ota/upload` | 原始 `.ota.bin` 文件流上传 |
| `POST /api/ota/url` | 从设备可访问的 HTTP URL 下载 |
| `POST /api/reboot` | 受控重启 |
| `GET /api/wifi/status` | 恢复热点、STA 状态、SSID、IP、RSSI；不含密码 |
| `POST /api/wifi/connect` | JSON `{"ssid":"...","password":"..."}`，仅手动调用 |
| `POST /api/wifi/disconnect` | 只断开 Station，保留恢复热点 |
| `POST /api/wifi/scan` | 启动一次异步扫描；OTA 期间拒绝 |
| `GET /api/wifi/scan` | 扫描状态和最多 20 个最强 SSID，含 RSSI、安全类型和信道 |
| `GET /api/mqtt/status` | MQTT 状态和非秘密配置；仅返回 `password_set` |
| `POST /api/mqtt/config` | 保存 Broker、认证、主题和发布周期配置 |
| `POST /api/mqtt/start`、`/api/mqtt/stop`、`/api/mqtt/test` | 启动、停止和尝试连接 Broker |
| `GET /api/homeassistant/status` | 自动发现配置和运行状态 |
| `POST /api/homeassistant/discovery` | `{"enabled":true/false}`；启用需要 MQTT 三重条件 |
| `GET /api/sensors` | 温度、湿度、PM2.5、甲醛；未知值为 `null`、`valid:false` |
| `GET /api/uart/status` | UART1 波特率、收包和解析统计 |
| `GET /api/uart/raw` | 最近 128 字节 HEX（内存保留最近 1024 字节） |
| `POST /api/uart/config` | JSON `{"baud":115200}`，支持 9600、19200、38400、57600、115200 |
| `POST /api/uart/init` | 无请求体；仅重发一次参考 zM1 固件中确认的固定 12 字节初始化帧 |

SSID 限制为 31 字节加字符串结束符，密码限制为 63 字节加结束符，对应 SDK 的 `wifi_ssid[32]` 和 `wifi_key[64]`。页面刷新后密码输入框为空；密码不会出现在日志、`/api/info` 或 `/api/wifi/status` 中。STA 连接由 4096 字节栈的独立线程执行，最多等待 30 秒。只有 `micoWlanGetLinkStatus().is_connected == 1` 且 `micoWlanGetIPStatus(..., Station)` 返回有效 IP 才认定连接成功。断开调用 `micoWlanSuspendStation()`，不会调用会停止两种接口的 `micoWlanSuspend()`。

### AP+STA 源码依据与待测风险

固定 SDK 的 `include/mico_wlan.h` 在 `micoWlanStart()` 注释中说明建立 Station+SoftAP 共存时调用两次。`platform/MCU/MX1290/moc/moc_api.c` 将 `StartNetwork()` 转发至 MOC 的 `micoWlanStart`。原有 SoftAP 启动和手动 STA 调用顺序保持不变；用户已在真实 M1 上验证 AP+STA 可工作。重启后仍不自动连接家庭 Wi-Fi，以优先保证 Recovery。

普通 `ScanResult` 只有 SSID/RSSI；本版用固定 SDK 的 `micoWlanStartScanAdv()` 和 `mico_notify_WIFI_SCAN_ADV_COMPLETED`，取得 `ScanResult_adv` 的信道及安全类型。扫描回调只保存最多 20 个 AP，同 SSID 留最强记录，按 RSSI 排序；15 秒未回调则标记失败。不调用完整 `mico_system_init()`，也不主动停止恢复热点。**扫描期间热点和 HTTP 是否持续可用必须实机核对；失败时仍可手工输入 SSID。**

## 传感器桥接与串口诊断

业务串口使用 SDK 的 `MICO_UART_FOR_APP`（UART1，TX GPIO9、RX GPIO10）；UART2 继续用于调试输出。参考 zM1 APP 的反汇编显示 UART1 使用 **115200 8N1**，20 字节帧以 `#` 开头、`!` 结尾，类型 `0x01` 含传感器字段。完整证据和地址见 [zM1 UART 逆向记录](docs/zm1-uart-reverse.md)。OpenM1 在 UART worker 中使用 2048 字节 RX ring、1024 字节原始数据历史和 4096 字节线程栈；初始化失败不会停止 Recovery。网页每秒读取 `/api/sensors`，并提供折叠的串口诊断区和运行时波特率切换。

**协议状态为 PARTIAL。** 参考 APP 对类型 `0x01` 仅检查固定长度、首尾和字段，未发现明确的 checksum/CRC 比对；OpenM1 增加取值范围筛查，但不会把结构有效帧称为“校验和通过”。所有数值在首次完整类型 `0x01` 帧出现前为 `null`，页面显示 `--`。v0.3.0 实机持续观察到 UART1 `rx_bytes=0`；zM1 参考 APP 在 UART 初始化后发送过一次固定 12 字节帧。因此 v0.3.1 在 UART1 初始化成功后等待 400 ms，发送 **`23 02 64 01 00 00 00 00 00 00 00 21`** 一次；网页切换波特率并重新初始化后也发送一次。`POST /api/uart/init` 可手工重发同一帧。其业务含义尚未确认，**没有任意 HEX 发送接口，也不周期 polling**。

实机测试时先观察 `SENSOR: init command result`、`tx_frames` 和 `rx_bytes`，再把网页温度、湿度、PM2.5、甲醛与 M1 正面屏幕逐项对比。若仍收不到任何数据，请提供 `/api/uart/status`、`/api/uart/raw`、测试持续时间和串口日志；不要自行向 ATSAMD20 发送其他命令。屏幕 Wi-Fi 图标仍未找到可信控制协议，见 [逆向记录](docs/zm1-wifi-icon-reverse.md)，本版不发送新的 UART/GPIO 控制动作。

## MQTT 与 Home Assistant

MQTT 使用 SDK 自带库和独立线程，支持普通 TCP、LWT、离线重连与 retained 传感器状态。Broker 配置保存在 MiCO 参数分区的应用 user data，带 magic、版本和 CRC32；MQTT 密码以明文存在本机 Flash，**不是加密保险库**，GET API 和日志不会返回密码。密码框留空会保留已保存密码，勾选“清除已保存的密码”才清除。设置细节见 [MQTT 文档](docs/mqtt.md)。

Home Assistant 自动发现只通过 MQTT 实现。后端仅在 Broker 已配置、MQTT 已启用、Broker 已连接时允许开启；否则返回 HTTP 409。四个传感器共享一个设备标识，关闭时删除 retained Discovery 配置。字段与主题见 [Home Assistant 文档](docs/homeassistant.md)。MQTT/Discovery 尚未实机验证，不能把编译成功视为 Broker 或 HA 连接成功。

## OTA 格式与恢复

完整 MOC OTA 由 Kernel、填充到 `0x75000`、8 字节 APP 头、APP payload、末尾 16 字节 raw MD5 组成。上传或 URL 下载时使用 2048 字节静态缓冲流式写入 `MICO_PARTITION_OTA_TEMP`，然后从 Flash 回读长度、两份 APP CRC、payload CRC 和整个 OTA（不含尾部 MD5）的 MD5；另外计算 boot table 所需 CRC16。验证成功才调用 `mico_ota_switch_to_new_fw(total_size - 16, boot_crc16)`，发送 HTTP 成功响应，等待两秒后 `MicoSystemReboot()`。任一校验失败不写升级标志、不重启。最大 OTA 文件限制为分区实际长度与 `0xB5000` 两者较小值。

`reference/zM1@MK3080B@moc.ota.bin` 保持不变，构建产物仍附带此手工恢复参考文件。CI 不连接真实设备。若新固件无法启动或 Recovery 不可用，可能需要拆机和物理刷写；**静态 `safe_to_flash` 不代表 v0.4.0 的扫描、MQTT、Home Assistant 或 UART 传感器响应已通过实机验证。**

## 构建与验证

在 Ubuntu 22.04 安装 Python 3、make、perl 和必要 32 位库后：

```sh
git clone https://github.com/MXCHIP/mico-os.git mico-os
git -C mico-os checkout 9b09de78164940ff3876d2053f8e7dd42ca2b8ba
bash scripts/bootstrap_build_env.sh
bash scripts/build_recovery.sh
```

修改页面源文件 `openm1/recovery_page.html` 后，运行 `python3 tools/embed_page.py` 更新嵌入固件的 `openm1/recovery_page.c`。CI 会检查两者一致及中文 UTF-8 文本。构建还检查 MOC APP 头、HTTP/OTA/Wi-Fi 符号、路由、官方 OTA 与自制 OTA 一致、APP CRC、MD5 和 SDK Kernel 身份。

手工验证：

```sh
python3 tools/verify_ota.py dist/OpenM1-v0.4.0@MK3080B@moc.ota.bin \
  --sdk-kernel mico-os/resources/moc_kernel/3080B/kernel.bin \
  --app dist/OpenM1-v0.4.0.bin
```

GitHub Actions 在推送 `main`、`feature/v0.4-network-mqtt-ha` 或手动触发时构建 Artifact `OpenM1-v0.4.0`，包含 OTA、BIN、ELF、MAP、manifest、SHA256SUMS、符号、校验报告、UART 与 Wi-Fi 图标逆向报告及构建日志。首次使用 v0.4.0 时应先核对 Artifact 与 manifest，再进行可恢复的实机测试。
