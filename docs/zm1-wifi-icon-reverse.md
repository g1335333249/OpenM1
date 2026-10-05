# zM1 Wi-Fi 图标与红 X PWM 控制定点逆向

状态：**静态逆向已确认；OpenM1 v0.5.1 实机验证待完成。** 参考文件 `reference/zM1@MK3080B@moc.ota.bin` 为 600292 字节，SHA256 `20c5e6ae1692e3e047063b637b137635ab9e57711dba7c27ef0eb6cf1390887e`，Kernel `3080B002.024`。APP payload 从 OTA `0x75008` 起，反汇编载入基址 `0x08088008`。使用固定 GCC 5.4.1：

```sh
arm-none-eabi-objdump -D -b binary -m arm -M force-thumb \
  --adjust-vma=0x08088008 --start-address=0x0808c408 \
  --stop-address=0x0808c498 zm1-app.bin
```

## PWM 初始化 `0x0808c478`

`0x0808c47a–0x0808c484` 将浮点 `20.0`、频率 `50000`、通道参数 `r0=4` 传入 `0x080919d8`；`0x0808c488–0x0808c496` 以同样参数和 `r0=3` 再调用一次。SDK `board/MK3080B/mico_board.h` 的枚举使数字 4、3 分别对应 **`MICO_PWM_5`**、**`MICO_PWM_4`**；`mico_board.c` 将它们映射到 GPIO14 和 GPIO13。SDK `moc_api.c` 的 `MicoPwmInitialize` 通过 PWM API 表调用 Kernel，`0x080919d8` 也是此表的初始化 veneer。因此两路载波均为 **50,000 Hz / 20% duty**，不是低频软件 GPIO 翻转。

## 状态函数 `0x0808c408`

同一 PWM API 表的 `0x080919e8` 读取偏移 `+4`，对应 `MicoPwmStart`；`0x080919f8` 读取偏移 `+8`，对应 `MicoPwmStop`。按 SDK 枚举复核状态函数：

| zM1 状态 | 关键地址 | PWM5：Wi-Fi 图标 | PWM4：红 X |
| --- | --- | --- | --- |
| `-1` | `0x0808c414–0x0808c432` | 读取 RAM `0x100305b2` 的相位，交替 Start/Stop，随后异或 1 | Stop |
| `0` | `0x0808c434–0x0808c446` | Stop | Stop |
| `1` | `0x0808c448–0x0808c458` | Start | Stop |
| `2` | `0x0808c45c–0x0808c46c` | Start | Start |

参考 zM1 的闪烁定时器以 **150 ms** 调用状态 `-1`，所以载波约 150 ms 开、150 ms 关，完整可见周期约 300 ms。OpenM1 以 `mico_rtos_init_timer(..., 150, ...)` 复刻，并通过同 SDK 的 `MicoPwmStart` / `MicoPwmStop` 操作符号通道；不直接控制 GPIO13/14。

## OpenM1 v0.5.1 映射

- `NETWORK_NO_WIFI` 和 `NETWORK_CHECKING` → `M1_NET_DISPLAY_DISCONNECTED`：PWM5 150 ms 闪烁，PWM4 Stop。
- `NETWORK_ONLINE` → `M1_NET_DISPLAY_ONLINE`：PWM5 Start，PWM4 Stop。
- `NETWORK_NO_INTERNET` → `M1_NET_DISPLAY_NO_INTERNET`：PWM5 Start，PWM4 Start。

业务判断仍来自 OpenM1 的 Station IP 与两个公共 TCP 探测点，不恢复原厂云连接。Recovery SoftAP 不算 Station 在线。参考 zM1 的 UART TX 生产者分别用于传感器请求、亮度、时间和 type `0x18` 确认；本版**没有新增任何 Wi-Fi UART 命令**。`0x0F` 仍只处理亮度。

协议证据是静态反汇编和固定 SDK 映射；**OpenM1 的 PWM5 图标闪烁/常亮和 PWM4 红 X 还没有真实 M1 验证**。Manifest 的 `wifi_icon_protocol_reverse_verified`、`red_x_protocol_reverse_verified` 为 true，两个 `*_hardware_verified` 保持 false，直到实机四场景验证通过。
