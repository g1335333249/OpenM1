# OpenM1 v0.6.12 Home Assistant MQTT Discovery

Home Assistant 集成只使用 MQTT Discovery，不需要 HA Token、URL 或 REST API。后端启用门槛由 `ha_policy_can_enable` 执行：**Broker 已配置 + MQTT 已启用 + 当前已连接**。任一条件不满足，`POST /api/homeassistant/discovery` 返回 HTTP 409 和 `请先配置并启动 MQTT 服务。`。掉线时配置 `enabled` 可保持 true，运行状态 `active` 变 false，重连后自动重发 retained Discovery。

默认 discovery prefix 是 `homeassistant`。对于 MAC 后缀 `XXXXXX`，五个 retained 配置主题为：

- `homeassistant/sensor/openm1_XXXXXX/temperature/config`
- `homeassistant/sensor/openm1_XXXXXX/humidity/config`
- `homeassistant/sensor/openm1_XXXXXX/pm25/config`
- `homeassistant/sensor/openm1_XXXXXX/formaldehyde/config`
- `homeassistant/number/openm1_XXXXXX/brightness/config`

四个 Sensor payload 均含 `state_topic=openm1/XXXXXX/state`、`availability_topic=openm1/XXXXXX/availability`、`payload_available=online`、`payload_not_available=offline`、`state_class=measurement`，以及同一设备标识 `openm1_XXXXXX`、Phicomm / OpenM1、M1、OpenM1 v0.6.12。若有 STA IP，配置网址指向 STA 地址，否则是 `192.168.4.1`。各字段：

| 对象 | name | value_template | unit | device_class |
| --- | --- | --- | --- | --- |
| temperature | 温度 | `{{ value_json.temperature }}` | °C | temperature |
| humidity | 湿度 | `{{ value_json.humidity }}` | % | humidity |
| pm25 | PM2.5 | `{{ value_json.PM25 }}` | µg/m³ | pm25 |
| formaldehyde | 甲醛 | `{{ value_json.formaldehyde }}` | mg/m³ | 省略 |

每个 `unique_id` 为 `openm1_XXXXXX_<对象>`。亮度 Number 使用 `command_topic=openm1/XXXXXX/brightness/set`、`state_topic=openm1/XXXXXX/state`、`value_template={{ value_json.brightness }}`、`min=0`、`max=4`、`step=1`、`mode=slider`，共用设备与 availability 信息。设备每次 MQTT 重连后重新订阅命令主题；只接受单字节 `0`–`4`，成功执行后立即发布 retained 状态。网页或 UART 改动在正常状态发布周期内同步。

关闭自动发现时向上述五个主题逐一发布 **retained 零字节 payload**，由 Broker 删除配置。根据 [Home Assistant MQTT Discovery 文档](https://www.home-assistant.io/integrations/mqtt/) 和 [传感器 device class 列表](https://www.home-assistant.io/integrations/sensor/)，`pm25` 是有效的 device class，甲醛没有等价的专用类别，因此省略类别。原有四个传感器已在 v0.6.9 实机验证；亮度 Number 与下行控制仍需真实 M1 和 Home Assistant 验证。
