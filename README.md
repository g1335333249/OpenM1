# OpenM1 v0.6.2

**开发版 / 实验版。** OpenM1 是斐讯悟空 M1 的开源固件项目。当前固件替换 EMW3080B/MK3080B 上的 MiCO/MOC 用户 APP，不刷写 ATSAMD20G17A。SDK 固定为 [MXCHIP/mico-os `9b09de78164940ff3876d2053f8e7dd42ca2b8ba`](https://github.com/MXCHIP/mico-os/tree/9b09de78164940ff3876d2053f8e7dd42ca2b8ba)，使用同 SDK 的 `3080B002.023` Kernel 和 ARM GCC 5.4.1。

## 实机验证状态

用户已在真实 M1 上验证 Recovery SoftAP、动态 SSID、`192.168.4.1`、中文 Web 页面、AP+STA 家庭 Wi-Fi 连接、网页 OTA、Bootloader 应用升级及升级后自动恢复。UART1 的 `0x01` 传感器请求和四项解析数值已经与实体屏幕核对；`0x0C` 时间帧和 `0x0F` 亮度事件也已收到。OTA_TEMP 分区起始 `0x00110000`、长度 `0xB5000`（741376 字节）。v0.5.2 实机已验证 PWM5 控制的 Wi-Fi 图标闪烁和常亮、PWM4 控制的手动红 X 测试。**真实 WAN 断开后自动点亮红 X 的完整场景仍待验证。** v0.6.0 新增的配置保存、开机自动连接、AP 自动关闭/恢复和系统统计也待实机验证。URL OTA、扫描、MQTT 和 Home Assistant 尚未实机核对。

v0.5.2 实机长期运行时曾发生真实家庭 Wi-Fi Station 掉线：路由器显示设备离线，原 Station IP 无法访问。精确根因尚未确认。v0.6.1 增加了常驻 Station supervisor、链路与 IP 防抖、运行期自动重连、递增退避、WLAN 状态缓存和 WLAN 控制串行化。

v0.6.1 第一次刷入运行正常；断电冷启动后，实体 Wi-Fi 图标不亮，保存的家庭 Wi-Fi 未连接，Recovery AP 也消失。串口显示初始 SoftAP `StartNetwork` 返回 0，HTTP 和 OTA 已启动，因此故障出现在后续启动管理阶段。首次 Station 连接前无条件调用 `micoWlanSuspendStation()` 是**高可信疑似回归点**，尚未确认是 Kernel 底层缺陷。v0.6.2 首次连接跳过该调用；普通链路掉线后的首轮重连也先直接 `StartNetwork(Station)`，只有启动失败、30 秒连接超时或用户手动断开才调用 Station suspend。开机自动连接等待 2 秒，让 Recovery 与显示先稳定启动。

Recovery AP 策略现在每秒读取一次 `micoWlanGetIPStatus(Soft_AP)`，以实际 `192.168.4.1` 状态判断热点是否存在；非策略关闭而实际消失时按 2、5、10、20、30 秒退避恢复。初始 SoftAP 最多尝试 3 次，失败后仍继续启动 HTTP、配置、显示、UART、Station supervisor 等子系统。**v0.6.2 等待真实 M1 冷启动和长期运行验证。**

恢复热点名称由设备 Wi-Fi MAC 的最后 3 字节生成，格式为 **`OpenM1-XXXXXX`**，例如 MAC `34:EA:34:12:AB:CD` 对应 `OpenM1-12ABCD`。若 MAC 读取结果无效，则使用 `OpenM1-RECOVERY` 并输出故障日志。热点开放、无密码，地址固定为 `192.168.4.1/24`，DHCP Server 开启，HTTP 监听 TCP 80。**每次重启先启动恢复热点、HTTP 和 OTA；只有用户保存凭据并启用开机自动连接后，才会尝试连接家庭 Wi-Fi。** Wi-Fi 与 MQTT 密码保存在本机 MiCO 参数 Flash，未加密，不是保险库；GET API 与日志不返回密码。不要在不可信网络暴露此无认证管理界面。

## 页面与接口

访问 [http://192.168.4.1](http://192.168.4.1)。页面为单份内嵌 UTF-8 中文 HTML/CSS/JavaScript，无外部 CDN。七个 Tab 依次为首页、网络、MQTT、Home Assistant、固件升级、诊断、系统；URL hash 可直接打开指定 Tab，例如 `/#diagnostics`。页面只轮询当前 Tab 需要的状态，OTA 开始后则跨 Tab 持续轮询。OTA 重启时每 2 秒探测 `/api/health`，重新上线后刷新版本信息。

| 路由 | 功能 |
| --- | --- |
| `GET /` | 中文管理页 |
| `GET /api/health` | Recovery 存活检查 |
| `GET /api/info` | 设备版本、RF、IP、运行时间、空闲堆 |
| `GET /api/ota/status` | OTA 状态、进度和错误 |
| `POST /api/ota/upload` | 原始 `.ota.bin` 文件流上传 |
| `POST /api/ota/url` | 从设备可访问的 HTTP URL 下载 |
| `POST /api/reboot` | 受控重启 |
| `GET /api/wifi/status` | 真实 AP 观察状态、自愈计数、Station 首次启动与清理状态、SSID、IP、RSSI 和稳定性计数；不含密码 |
| `GET /api/wifi/settings` | 已保存 SSID、是否有凭据及两个开关；不含密码 |
| `POST /api/wifi/settings` | 保存/清除开机 Wi-Fi 设置；只在用户提交时写参数 Flash |
| `GET /api/system/stats` | 估算 CPU 负载、MiCO 堆信息和裸 Flash 分区元数据 |
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
| `POST /api/uart/init` | 无请求体；兼容旧路由，重新同步当前保存的显示亮度与开关状态 |
| `POST /api/uart/sensor-request` | 无请求体；仅发送一次参考 zM1 固件中确认的固定 12 字节传感器请求帧 |
| `GET /api/display/status`、`POST /api/display/brightness` | 查看亮度及提交 `{"brightness":0..4}`；0 关闭屏幕，恢复时保留原档位 |
| `GET /api/network/health` | 独立于 MQTT 的 Station/IP/公共 TCP 探测与目标屏幕状态 |
| `POST /api/display/network-test` | 仅允许 `blink`、`online`、`no_internet`、`auto` 四种固定模式；测试 5 秒后自动恢复 |

SSID 限制为 31 字节加字符串结束符，密码限制为 63 字节加结束符，对应 SDK 的 `wifi_ssid[32]` 和 `wifi_key[64]`。页面刷新后密码输入框为空；密码不会出现在日志、`/api/info` 或 `/api/wifi/status` 中。STA 由 4096 字节栈的常驻 supervisor 线程管理，连接最多等待 30 秒，每秒采样链路与 IP；连续 2 次有效才认定连接成功，连接后连续 3 次异常才认定掉线。掉线后按 2、5、10、20、30 秒（上限 30 秒）退避重连。手动断开会暂停本次启动期间的自动重连，重启后仍遵循保存的开机自动连接配置。HTTP、MQTT、网络健康和 AP 策略只读 supervisor 的缓存状态；运行期的 Station 与 SoftAP 状态变更通过 WLAN 控制互斥锁串行化。断开仅使用 `micoWlanSuspendStation()`，不会调用会停止两种接口的 `micoWlanSuspend()`。

### AP+STA 源码依据与待测风险

固定 SDK 的 `include/mico_wlan.h` 在 `micoWlanStart()` 注释中说明建立 Station+SoftAP 共存时调用两次。`platform/MCU/MX1290/moc/moc_api.c` 将 `StartNetwork()` 转发至 MOC 的 `micoWlanStart`。原有 SoftAP 启动和手动 STA 调用顺序保持不变；用户已在真实 M1 上验证 AP+STA 可工作。新开机策略先启动 Recovery，再读取配置并复用现有 STA 连接路径。仅在自动连接已启用、已保存 SSID 与当前 STA 一致、Station 有有效 IP、没有 OTA 且持续稳定约 3 秒时，才通过 `micoWlanSuspendSoftAP()` 关闭 AP；STA 失联或策略关闭后，后台检查会用相同 SSID/IP/DHCP 配置重新启动 Recovery AP。此策略尚待 v0.6.0 实机验证。

普通 `ScanResult` 只有 SSID/RSSI；本版用固定 SDK 的 `micoWlanStartScanAdv()` 和 `mico_notify_WIFI_SCAN_ADV_COMPLETED`，取得 `ScanResult_adv` 的信道及安全类型。扫描回调只保存最多 20 个 AP，同 SSID 留最强记录，按 RSSI 排序；15 秒未回调则标记失败。不调用完整 `mico_system_init()`，也不主动停止恢复热点。**扫描期间热点和 HTTP 是否持续可用必须实机核对；失败时仍可手工输入 SSID。**

### 保存网络与系统资源

`POST /api/wifi/settings` 可提交 `{"ssid":"MyWiFi","password":"12345678","auto_connect":true,"disable_ap_after_connect":false}`。密码字段省略且 SSID 不变时保留原密码；改变 SSID 必须显式提交密码，开放网络用 `"password":""`。仅提交 `{"auto_connect":false}` 会同时清除自动关闭 AP 标记；`{"clear_saved_wifi":true}` 清除全部保存的 Wi-Fi 设置，但不断开当前 Station。未启用自动连接却要求关闭 AP 时返回 HTTP 409。`GET /api/wifi/settings` 只返回保存的 SSID、`credential_saved`、`password_nonempty` 和两个开关。

系统 Tab 每 3 秒读取 `/api/system/stats`。CPU 为优先级 8 的普通线程每 5 秒进行 100 ms 低优先级计数采样，是估算值而非 Kernel 精确计数，首份样本前显示 `--`。内存来自 `MicoGetMemoryInfo()`；Flash 仅展示 `MicoFlashGetInfo()` 的分区名称、起始地址和容量，不读取参数内容。设备没有文件系统，不显示虚构的磁盘使用率。

## 传感器桥接与串口诊断

业务串口使用 SDK 的 `MICO_UART_FOR_APP`（UART1，TX GPIO9、RX GPIO10）；UART2 继续用于调试输出。参考 zM1 APP 的反汇编显示 UART1 使用 **115200 8N1**，20 字节帧以 `#` 开头、`!` 结尾，类型 `0x01` 含传感器字段。完整证据和地址见 [zM1 UART 逆向记录](docs/zm1-uart-reverse.md)。OpenM1 在 UART worker 中使用 2048 字节 RX ring、1024 字节原始数据历史和 4096 字节线程栈；初始化失败不会停止 Recovery。进入首页立即读取一次 `/api/sensors`，之后每 2 秒读取；离开首页或开始 OTA 时停止该轮询。读取失败时保留上次显示值并提示异常。诊断 Tab 提供原始数据和运行时波特率切换。

**协议的校验规则仍为 PARTIAL，但四项数值已实机核对。** 参考 APP 对类型 `0x01` 仅检查固定长度、首尾和字段，未发现明确的 checksum/CRC 比对；OpenM1 增加取值范围筛查。所有数值在首次完整类型 `0x01` 帧出现前为 `null`，页面显示 `--`。原 zM1 启动时发送的 `23 02 64 01 00 00 00 00 00 00 00 21` 已确认是**亮度 4、屏幕开启**帧。v0.6.0 在 UART 初始化后等待 400 ms，发送当前保存的屏幕状态；诊断路由 `POST /api/uart/init` 保留兼容，但语义改为重新同步当前显示状态。

新的实机 RX 已确认 type `0x0C` 日期时间帧和 type `0x0F` 帧。[定点逆向报告](docs/zm1-uart-type0f.md)表明 `0x0F` 进入亮度处理路径，`0x18` 可触发确认及重置路径；OpenM1 不发送 `0x18` 响应。`/api/uart/status` 将总帧、传感器帧、时间帧、亮度事件帧、`0x18`、未知帧、无效帧分别计数；M1 时间只显示，不写设备 RTC。

v0.6.0 在 UART 在线、波特率稳定且没有 OTA 时，每 **2000 ms** 自动发送一次已实机验证的固定传感器请求 **`23 01 00 00 00 00 00 00 00 00 00 21`**。手工请求仍在诊断 Tab，所有发送由 UART worker 串行完成；没有任意 HEX 发送接口。首页每 **2000 ms** 请求一次 `/api/sensors`，离开首页后停止该定时器。

屏幕亮度为 0–4 档：1–4 分别发送亮度字节 `19`、`32`、`4B`、`64`，开关字节为 `01`；0 档发送保存的最后非零档位并将开关字节置 `00`。实体 `0x0F` 亮度事件使用同一档位回复，500 ms 内的同状态回报不再次发送，避免回环。配置在 MiCO 参数区升级为 v3，支持 v1/v2 迁移，保留 MQTT、HA、亮度和密码。亮度协议和 Android 客户端 0–4 的范围见 [定点逆向记录](docs/zm1-uart-type0f.md)。

网络健康检查与 MQTT 独立：Station 有有效 IP 后，后台每 10 秒对 `223.5.5.5:53` 和 `1.1.1.1:53` 做最长 1800 ms 的 TCP connect 探测；两个连续成功判为在线，两个连续失败判为无 Internet。该状态表示**公共探测端点的 TCP 可达性**，不保证任意网站可访问。参考 zM1 的定点反汇编确认实体 Wi-Fi 图标使用 PWM5、红 X 使用 PWM4，两路均为 50 kHz、20% 占空比；未连接时每 150 ms 启停 PWM5，在线时 PWM5 常开，已连接但无 Internet 时两路常开。OpenM1 沿用现有网络健康判断，**不发送新的图标 UART 命令，也不直接操作 GPIO13/14**。证据见 [逆向记录](docs/zm1-wifi-icon-reverse.md)。

v0.5.1 在 150 ms RTOS timer callback 中调用互斥锁与 PWM HAL，HardFault 发生在进入闪烁后；这是**高可信疑因，并非精确故障指令已确认**。v0.5.2 删除此定时器，改由 2048 字节栈的普通显示线程独占 PWM5/PWM4 Start/Stop，HAL 调用时不持锁。v0.5.2 实机已验证未连家用 Wi-Fi 时图标闪烁、连接成功后常亮，以及诊断按钮手动点亮红 X。真实 WAN 断开后由网络健康状态自动控制红 X 的完整场景尚待实机验证。

## MQTT 与 Home Assistant

MQTT 使用 SDK 自带库和独立线程，支持普通 TCP、LWT、离线重连与 retained 传感器状态。Broker 配置保存在 MiCO 参数分区的应用 user data，带 magic、版本和 CRC32；MQTT 密码以明文存在本机 Flash，**不是加密保险库**，GET API 和日志不会返回密码。密码框留空会保留已保存密码，勾选“清除已保存的密码”才清除。设置细节见 [MQTT 文档](docs/mqtt.md)。

Home Assistant 自动发现只通过 MQTT 实现。后端仅在 Broker 已配置、MQTT 已启用、Broker 已连接时允许开启；否则返回 HTTP 409。四个传感器共享一个设备标识，关闭时删除 retained Discovery 配置。字段与主题见 [Home Assistant 文档](docs/homeassistant.md)。MQTT/Discovery 尚未实机验证，不能把编译成功视为 Broker 或 HA 连接成功。

## OTA 格式与恢复

完整 MOC OTA 由 Kernel、填充到 `0x75000`、8 字节 APP 头、APP payload、末尾 16 字节 raw MD5 组成。上传或 URL 下载时使用 2048 字节静态缓冲流式写入 `MICO_PARTITION_OTA_TEMP`，然后从 Flash 回读长度、两份 APP CRC、payload CRC 和整个 OTA（不含尾部 MD5）的 MD5；另外计算 boot table 所需 CRC16。验证成功才调用 `mico_ota_switch_to_new_fw(total_size - 16, boot_crc16)`，发送 HTTP 成功响应，等待两秒后 `MicoSystemReboot()`。任一校验失败不写升级标志、不重启。最大 OTA 文件限制为分区实际长度与 `0xB5000` 两者较小值。

`reference/zM1@MK3080B@moc.ota.bin` 保持不变，构建产物仍附带此手工恢复参考文件。CI 不连接真实设备。若新固件无法启动或 Recovery 不可用，可能需要拆机和物理刷写；**静态 `safe_to_flash` 不代表 v0.6.2 的长期 Station 稳定性已通过实机验证。**

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
python3 tools/verify_ota.py dist/OpenM1-v0.6.2@MK3080B@moc.ota.bin \
  --sdk-kernel mico-os/resources/moc_kernel/3080B/kernel.bin \
  --app dist/OpenM1-v0.6.2.bin
```

GitHub Actions 在推送 `main` 或手动触发时构建 Artifact `OpenM1-v0.6.2`，包含 OTA、BIN、ELF、MAP、manifest、SHA256SUMS、符号、校验报告、UART 与 Wi-Fi 图标逆向报告及构建日志。首次使用 v0.6.2 时应先核对 Artifact 与 manifest，再进行可恢复的实机测试。
