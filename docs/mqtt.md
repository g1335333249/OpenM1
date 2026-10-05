# OpenM1 v0.5.0 MQTT

本版使用固定 MiCO SDK `libraries/protocols/mqtt/` 的 `MQTTClientInit`、`MQTTConnect`、`MQTTPublish`、`MQTTYield` 与 MiCO TCP 传输。仅支持明文 TCP；默认端口 1883、发布周期 5 秒。MQTT 线程与 Recovery HTTP/OTA 分离，Broker 不可用时按 5、10、20、30 秒退避，最长 30 秒。开机不自动连接家庭 Wi-Fi；已保存且启用的 MQTT 配置会先等待手工建立 STA。

配置通过 MiCO 官方 `mico_system_context_init(sizeof(openm1_config_t))` 的应用 user data 段存储，仍由 `mico_system_context_update()` 更新双参数分区和 bootTable。结构有 magic、版本、大小、CRC32；只在用户保存时写入。保存时拒绝正在进行的 OTA。由于原 SDK `MICOReadConfiguration` 的分区 CRC 覆盖完整参数分区，与应用 user data 长度无关，v0.3.1 的 `size=0` 升级为本版非零长度不会改变已有 CRC 判断。首次进入本版时，若应用配置头不匹配，在 RAM 中使用默认值；不会仅为此自动擦写参数分区。

密码只在本机参数 Flash 保存，**不是加密保险库**。`GET /api/mqtt/status` 只提供 `password_set`，不返回明文；日志也不输出密码。`POST /api/mqtt/config` 省略 `password` 会保留旧密码，`clear_password:true` 才会清除。管理页面无认证，请只在可信环境使用。MQTT 本身尚未实机验证。

主题（`XXXXXX` 为 Wi-Fi MAC 后三字节大写）：

| 用途 | 主题 | retained |
| --- | --- | --- |
| 传感器状态 | `openm1/XXXXXX/state` | 是 |
| 在线状态 / LWT | `openm1/XXXXXX/availability` | 是，`online` / `offline` |

状态 JSON 保留 `temperature`、`humidity`、`PM25`、`formaldehyde` 字段；无效或离线超过 30 秒的值为 `null`，另含 `uptime`、`rssi`。正常停止前发布 retained `offline` 并断开。所有 MQTT 调用由独立 6144 字节栈线程执行；SDK 初始化时动态分配两个 512 字节缓冲区，大于 512 字节的 Discovery 消息可能临时 `realloc`。接收回调在 SDK 扩容前拒绝 remaining length 大于 1024 字节的 Broker 报文。自动发现启用期间不能修改主题前缀或发现前缀，以免遗留旧的 retained 配置。
