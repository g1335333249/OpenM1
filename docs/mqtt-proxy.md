# OpenM1 v0.6.19 MQTT SOCKS5 代理

## 连接方式

关闭代理时，M1 通过家庭 Wi-Fi 的 IPv4 网络直接连接 MQTT Broker。开启代理时，M1 仍只建立 IPv4 TCP Socket，先连接 SOCKS5 服务器，按 RFC 1928/1929 完成方法协商、可选用户名密码认证和 TCP CONNECT，再在隧道里运行原有 MQTT。Broker 可以是 IPv4、域名或普通/压缩 IPv6 字面量。域名以 SOCKS5 `ATYP=03` 交给代理解析；IPv6 字面量仅在 APP 中编码为 16 字节 `ATYP=04`，M1 不建立原生 IPv6 Socket。带 `%` 作用域的地址和链路本地 IPv6 地址不被接受。

先在 MQTT 服务卡片填写 Broker 地址、端口、MQTT 账号、Client ID、Topic 等并保存。代理卡片的开关默认关闭；启用后填写云服务器 IPv4 地址或可解析出 A 记录的域名、端口（默认 1080）和独立的代理账号。点击“保存代理配置”后才生效。每台 M1 应使用独立的 MQTT Client ID 和代理账号。关闭代理不会清除已保存的代理参数。**代理失败不会自动改为直连。**直连模式不能连接纯 IPv6 Broker，会显示 `direct_ipv6_unsupported`。原有 HA Discovery、LWT、亮度命令和 MAC 派生主题结构沿用 MQTT 层实现。

“测试代理连接”只建立 SOCKS5 CONNECT 并关闭测试 Socket，不发送 MQTT 账号或消息，不修改当前 MQTT 会话。测试需要 Station 联网、代理已启用、Broker 已配置且 OTA/低内存安全模式未运行。`GET /api/mqtt/proxy/status` 和 `GET /api/mqtt/status` 报告模式、代理认证、目标连接和错误阶段，密码只显示是否已设置。代理 TCP、方法、认证和目标 CONNECT 阶段均有限时；固定 SDK 的 IPv4 `gethostbyname` 没有公开可取消的调用级超时，域名解析仍可能等待 Kernel 自身超时。需要严格的整条路径时限时，代理地址应使用 IPv4 字面量。

## Debian IPv6 Broker

在 Debian 上先确认 Broker 监听正确的 IPv6 地址和 TCP 端口，并从云服务器测试该端口可达。不要把 Debian 的动态公网 IPv6 地址提交到固件仓库；地址变化时在 OpenM1 页面更新 Broker 地址。Broker 监听地址和防火墙只允许必要来源。M1 配置 Broker 为 IPv6 字面量时，代理服务器负责跨 IPv4/IPv6 建连。普通 SOCKS5 CONNECT 不会转发 UDP，也不改变设备自己的 IPv6 地址或路由。

## GOST 服务端示例

GOST v3 的 SOCKS5 处理器支持 RFC 1929 认证；其 service-level bypass 可作目标白名单。以下是结构示例，部署时将占位名称和口令替换为私密配置文件中的实际值，先用所用 GOST 版本验证规则与 IPv6 目标匹配，再开放公网端口：

```yaml
services:
- name: openm1-mqtt
  addr: ":1080"
  bypass: mqtt-targets
  handler:
    type: socks5
    authers:
    - m1-auth
    metadata:
      notls: true
  listener:
    type: tcp
authers:
- name: m1-auth
  auths:
  - username: device-1
    password: CHANGE_ME_1
  - username: device-2
    password: CHANGE_ME_2
bypasses:
- name: mqtt-targets
  whitelist: true
  matchers:
  - mqtt.example.org:1883
```

这个示例按 Broker 域名限制目标；使用 IPv6 字面量时，应按当前 GOST 版本确认 IPv6 匹配器写法，并在云服务器的**出站防火墙**另行限制目标 IPv6 地址和 TCP 1883。入口防火墙仅允许预期来源连接 1080；关闭不需要的 UDP ASSOCIATE/BIND；设置并发连接上限，避免成为开放代理。不要把真实代理密码、Broker 密码或设备公网地址写入仓库。

## 安全限制

**普通 SOCKS5 用户名密码认证没有传输加密，现有 MQTT 也是明文 TCP。**公网链路上的代理凭据与 MQTT 数据可能被窃听或修改，不适合作为长期公网生产安全方案。固定 MiCO SDK 声明了 TLS 1.0～1.2 客户端接口，但本版本未测量 MX1290 MOC Kernel 的 TLS 功能、证书校验、堆和栈成本，因此不提供 SOCKS5 over TLS；不能把 GOST 的私有加密协商当作此客户端支持。后续需单独评估端到端加密或可信隧道。

## 故障与验收

- `proxy_dns`：代理名称未解析出 IPv4；先改用 IPv4 字面量核对网络。
- `proxy_tcp_connect` / `proxy_timeout`：云服务器监听、防火墙、Station 路由或超时。
- `proxy_method` / `proxy_protocol_error`：服务端未按标准 SOCKS5 回复，或认证方法不匹配。
- `proxy_auth`：检查每台设备独立账号；不要在日志中打印密码。
- `proxy_target_connect`：检查 Broker 地址、IPv6 路由、监听端口和云端目标白名单。

真实 M1 验收顺序：先在关闭代理时确认原有 IPv4 MQTT、HA、传感器、亮度与 OTA；再在隔离的测试 Broker 上验证 IPv4、域名和 IPv6 目标、认证失败、代理断开自动重连、多台 M1 并发。分别观察 `/api/mqtt/status`、`/api/mqtt/proxy/status`、内存最低值和栈溢出计数。随后测试 Recovery AP 密码设置、重启后 WPA2 广播与客户端认证、AP+STA 稳定运行和 AP 自愈。CI 只验证编译与模拟协议交互，不能证明这些真实设备场景。

协议与服务端配置来源：[RFC 1928](https://www.rfc-editor.org/rfc/rfc1928)、[RFC 1929](https://www.rfc-editor.org/rfc/rfc1929)、[GOST SOCKS5](https://gost.run/en/tutorials/protocols/socks/)、[GOST 认证](https://v3.gost.run/en/concepts/auth/)、[GOST Bypass](https://gost.run/en/concepts/bypass/)。
