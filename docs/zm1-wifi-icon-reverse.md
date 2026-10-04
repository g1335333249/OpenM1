# zM1 Wi-Fi 图标控制路径调查（v0.4.0）

状态：**UNKNOWN；本版不发送任何新的 UART 控制帧，也不操作未知 GPIO。** 本文件的地址是参考 OTA APP payload 链接地址，APP payload 从 OTA `0x75008` 开始，载入基址 `0x08088008`。参考文件 SHA256：`20c5e6ae1692e3e047063b637b137635ab9e57711dba7c27ef0eb6cf1390887e`。

## UART 发送路径

用 ARM GCC 5.4.1 `arm-none-eabi-objdump -D -b binary -m arm -M force-thumb --adjust-vma=0x08088008` 反汇编参考 APP，找到对 UART 发送 ABI veneer `0x08091900` 的直接 `bl`：

| 调用地址 | 来源 / 长度 | 已知调用路径 |
| --- | --- | --- |
| `0x0808c1fe` | 12 字节固定 `23 02 64 01 00 00 00 00 00 00 00 21`，常量位于 `0x080a06d4` | UART 线程 `0x0808c1c8` 初始化 UART 后发送；这是 v0.3.1 已复现的 **zM1 init command**，没有 Wi-Fi 图标语义证据。 |
| `0x0808c2ee` | 长度从队列消息 `[r1+0x28]` 读取，内容由生产者动态构造 | UART 线程从 `0x1003051c` 队列取消息（`0x0808c28c`），再调用发送 veneer；队列生产者 `0x0808bcbc` 在 `0x0808bd28` 入队。 |
| `0x08096a62` | 通用 UART HAL 桥接调用；非应用层固定图标帧 | SDK 类适配包装，向同一 MOC UART API 转发。 |

队列生产者 `0x0808bcbc` 的已定位上层：

- `0x0808bde4` 复制 `0x080a06c8` 的 12 字节 `23 01 00 00 00 00 00 00 00 00 00 21`，由主循环 `0x08088616` 调用；它是在循环中的帧，不能推定为图标命令。
- `0x0808be64` 复制 `0x080a06e0` 的 12 字节 `23 16 00 10 14 00 03 10 f1 ff ff 21`，由接收帧处理路径 `0x0808c0bc` 调用。
- `0x0808be08` 以 `0x080a06d4` 的 12 字节模板构造动态帧，修改第 2、3 字节；直接调用点 `0x0808d2f2`、`0x0808d33e` 位于数据处理逻辑，尚无 Wi-Fi 连接/断开事件调用证据。
- `0x0808bd90` 从 `0x080a06bc` 的 12 字节模板构造帧；调用点 `0x0808d52e` 上方在计算日期/时间（年份加 1900），并非已证实的网络状态通知。

已识别四个相邻 12 字节模板地址：`0x080a06bc`、`0x080a06c8`、`0x080a06d4`、`0x080a06e0`。这些仅证明 zM1 向 ATSAMD20 发送过相应格式；**没有证据证明哪条是 Wi-Fi icon ON/OFF**，不能把 init 帧冒充图标控制。

## GPIO 与结论

SDK `platform/MCU/MX1290/moc/moc_api.c` 还提供 `MicoGpioOutputHigh/Low`，所以 UART 并非唯一候选机制。当前参考 APP 为无符号原始二进制，尚未建立可信的 Wi-Fi 事件回调到 GPIO 操作的调用边。Wi-Fi 状态回调、DHCP 完成、STA 连接/断开的上层调用关系未能静态确定。

因此图标控制机制目前只能标记 **UNKNOWN**，`wifi_icon_protocol_verified=false`、`wifi_icon_feature_present=false`、`wifi_icon_verified_on_hardware=false`。v0.4.0 的 MQTT 状态与实体屏幕图标不绑定；图标只应在将来找到可复核的 Wi-Fi 状态事件 → 明确 UART/GPIO 控制动作证据后实现。
