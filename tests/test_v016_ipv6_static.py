#!/usr/bin/env python3
"""Keep the v0.6.16 manual API and IPv4 MQTT behavior in v0.6.17."""
from pathlib import Path
ipv6=Path('openm1/ipv6_diagnostic.c').read_text()
http=Path('openm1/recovery_http.c').read_text()
page=Path('openm1/recovery_page.html').read_text()
mqtt=Path('openm1/mqtt_manager.c').read_text()
for address in ('2001:db8::1','2001:0db8:0000:0000:0000:0000:0000:0001','::1','192.0.2.1'):
    assert address in ipv6
assert 'ipv6_diagnostic_start_tcp()' in http and '"/api/ipv6/probe/tcp"' in http
assert 'confirm_ipv6_tcp' in http and 'confirm_ipv6_tcp' in page
assert 'ipv6_diagnostic_start()' not in Path('openm1/main.c').read_text()
assert 'IPV6_TCP_CONNECT_TIMEOUT_MS 3000u' in Path('openm1/ipv6_probe_logic.h').read_text()
assert 'if (recovery_ota_busy()) return -2;' in ipv6
assert 'IPV6_DIAGNOSTIC_WORKER_STACK+9216u+8192u' in ipv6
assert 'gethostbyname' in mqtt and 'AF_INET' in mqtt and 'AF_INET6' not in mqtt
assert 'WIFI_AP_AUTO_CLOSE_SUPPORTED 0' in Path('openm1/wifi_station_logic.h').read_text()
assert 'OPENM1_CONFIG_VERSION 3' in Path('openm1/config_store.h').read_text()
print('V016_IPV6_STATIC_PASS: manual TCP, IPv4 MQTT and AP policy preserved')
