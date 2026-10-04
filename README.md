# OpenM1 v0.2.0

**开发版 / 实验版。** OpenM1 是斐讯悟空 M1 的开源固件项目。当前固件替换 EMW3080B/MK3080B 上的 MiCO/MOC 用户 APP，不刷写 ATSAMD20G17A。SDK 固定为 [MXCHIP/mico-os `9b09de78164940ff3876d2053f8e7dd42ca2b8ba`](https://github.com/MXCHIP/mico-os/tree/9b09de78164940ff3876d2053f8e7dd42ca2b8ba)，使用同 SDK 的 `3080B002.023` Kernel 和 ARM GCC 5.4.1。

## 实机验证状态

用户已在真实 M1 上验证 v0.1.0：启动、SoftAP、DHCP、HTTP 页面、`/api/health`、`/api/info`、OTA 上传、Flash 写入、APP CRC、MD5、boot CRC16、boot table 更新、自动重启及 Bootloader 应用新 OTA 均成功。OTA_TEMP 分区起始 `0x00110000`、长度 `0xB5000`（741376 字节）；稳定时空闲堆约 99088 字节。v0.1.0 的 OTA 测试文件 515452 字节，boot CRC16 为 `e0ad`。**v0.2.0 的手动 STA 连接和新中文页面尚未实机验证；URL OTA 也尚未实机验证。** Manifest 分别记录已验证的 Recovery 基础设施和待验证的新功能。

恢复热点名称由设备 Wi-Fi MAC 的最后 3 字节生成，格式为 **`OpenM1-XXXXXX`**，例如 MAC `34:EA:34:12:AB:CD` 对应 `OpenM1-12ABCD`。若 MAC 读取结果无效，则使用 `OpenM1-RECOVERY` 并输出故障日志。热点开放、无密码，地址固定为 `192.168.4.1/24`，DHCP Server 开启，HTTP 监听 TCP 80。任何家庭 Wi-Fi 凭据只保存在运行时 RAM；**每次重启都会先启动恢复热点和 OTA 服务，不自动连接家庭 Wi-Fi。** 不要在不可信网络暴露此无认证管理界面。

## 页面与接口

访问 [http://192.168.4.1](http://192.168.4.1)。页面为内嵌 UTF-8 中文 HTML/CSS/JavaScript，无外部 CDN。设备信息、网络状态、手动家庭 Wi-Fi 连接、固件升级及重启均在首页。OTA 成功后页面将把连接中断视为正常重启过程，每 2 秒探测 `/api/health`，重新上线后刷新版本信息。

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
| `GET /api/wifi/scan` | 本版返回 `supported:false`；手工输入 SSID 可用 |

SSID 限制为 31 字节加字符串结束符，密码限制为 63 字节加结束符，对应 SDK 的 `wifi_ssid[32]` 和 `wifi_key[64]`。页面刷新后密码输入框为空；密码不会出现在日志、`/api/info` 或 `/api/wifi/status` 中。STA 连接由 4096 字节栈的独立线程执行，最多等待 30 秒。只有 `micoWlanGetLinkStatus().is_connected == 1` 且 `micoWlanGetIPStatus(..., Station)` 返回有效 IP 才认定连接成功。断开调用 `micoWlanSuspendStation()`，不会调用会停止两种接口的 `micoWlanSuspend()`。

### AP+STA 源码依据与待测风险

固定 SDK 的 `include/mico_wlan.h` 在 `micoWlanStart()` 注释中明确说明：建立 Station+SoftAP 共存时调用该函数两次，Station 调用立即返回并在后台连接。`platform/MCU/MX1290/moc/moc_api.c` 将 `StartNetwork()` 转发至 MOC 的 `micoWlanStart`，`moc_adapter.c` 将它与 `micoWlanSuspendStation` 分别转发到 Kernel。SDK 的 Wi-Fi 固件文件也标记为 `uapsta`。本版因此保留原有 SoftAP 启动调用，只有网页显式请求后才再次以 `wifi_mode=Station`、`dhcpMode=DHCP_Client` 调用 `StartNetwork()`。源码支持不等于真实 M1 上已稳定共存：**第一次 STA 测试须观察恢复热点和 HTTP 是否持续可用**。若 STA 实验异常，重启后不读取配置，仍应回到 Recovery。

扫描 API 为异步 `micoWlanStartScan()`，其结果依赖 `mico_notify_WIFI_SCAN_COMPLETED` 回调；当前 MOC Kernel 扫描期间是否保持恢复热点尚未实机确认，因此 v0.2.0 不启动扫描，也不调用完整 `mico_system_init()`。页面保留手工输入。

## OTA 格式与恢复

完整 MOC OTA 由 Kernel、填充到 `0x75000`、8 字节 APP 头、APP payload、末尾 16 字节 raw MD5 组成。上传或 URL 下载时使用 2048 字节静态缓冲流式写入 `MICO_PARTITION_OTA_TEMP`，然后从 Flash 回读长度、两份 APP CRC、payload CRC 和整个 OTA（不含尾部 MD5）的 MD5；另外计算 boot table 所需 CRC16。验证成功才调用 `mico_ota_switch_to_new_fw(total_size - 16, boot_crc16)`，发送 HTTP 成功响应，等待两秒后 `MicoSystemReboot()`。任一校验失败不写升级标志、不重启。最大 OTA 文件限制为分区实际长度与 `0xB5000` 两者较小值。

`reference/zM1@MK3080B@moc.ota.bin` 保持不变，构建产物仍附带此手工恢复参考文件。CI 不连接真实设备。若新固件无法启动或 Recovery 不可用，可能需要拆机和物理刷写；**静态 `safe_to_flash` 不代表 v0.2.0 的 STA/页面已通过实机验证。** 当前屏幕 Wi-Fi 图标不亮暂不处理。TODO：ATSAMD20 UART 协议逆向；不发送猜测的 UART 指令。

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
python3 tools/verify_ota.py dist/OpenM1-v0.2.0@MK3080B@moc.ota.bin \
  --sdk-kernel mico-os/resources/moc_kernel/3080B/kernel.bin \
  --app dist/OpenM1-v0.2.0.bin
```

GitHub Actions 在推送 `main` 或手动触发时构建 Artifact `OpenM1-v0.2.0`，包含 OTA、BIN、ELF、MAP、manifest、SHA256SUMS、符号、校验报告和构建日志。首次使用 v0.2.0 时应先核对 Artifact 与 manifest，再进行可恢复的实机测试。
