#!/usr/bin/env python3
"""Protect manual, independent IPv6 stages and all stable IPv4/Wi-Fi paths."""
from pathlib import Path
import re
ipv6=Path('openm1/ipv6_diagnostic.c').read_text()
logic=Path('openm1/ipv6_probe_logic.c').read_text()
http=Path('openm1/recovery_http.c').read_text()
page=Path('openm1/recovery_page.html').read_text()
wifi=Path('openm1/wifi_manager.c').read_text()
mqtt=Path('openm1/mqtt_manager.c').read_text()
assert 'IPV6_PRIMARY_DNS "www.ustc.edu.cn"' in ipv6
assert 'IPV6_FALLBACK_DNS "ipv6.mirrors.ustc.edu.cn"' in ipv6
assert 'ipv6.google.com' not in ipv6
for address in ('2001:db8::1','2001:0db8:0000:0000:0000:0000:0000:0001','::1','192.0.2.1'):
    assert address in ipv6
assert ipv6.index('local.compressed.result') < ipv6.index('IPV6_PRIMARY_DNS,"443"')
assert '(void)parse_result;' in logic and '(void)socket_result;' in logic
assert 'freeaddrinfo(addresses)' in ipv6 and 'close(fd)' in ipv6
assert 'ipv6_addrinfo_valid(' in ipv6 and 'ai_addrlen<sizeof(struct sockaddr_in6)' in ipv6
assert 'ioctl(fd,FIONBIO,&nonblocking)' in ipv6
assert 'select(fd+1,NULL,&writable,&exception,&timeout)' in ipv6
assert 'getsockopt(fd,SOL_SOCKET,SO_ERROR' in ipv6
assert 'IPV6_TCP_CONNECT_TIMEOUT_MS 3000u' in Path('openm1/ipv6_probe_logic.h').read_text()
assert 'IPV6_PROBE_RETRY_GAP_MS 30000u' in ipv6
assert 'if (recovery_ota_busy()) return -2;' in ipv6
assert 'IPV6_DIAGNOSTIC_WORKER_STACK+9216u+8192u' in ipv6
assert 'ipv6_diagnostic_start_tcp()' in http and '"/api/ipv6/probe/tcp"' in http
assert 'confirm(' in page and "'/api/ipv6/probe/tcp'" in page
assert 'confirm_ipv6_tcp' in page and 'confirm_ipv6_tcp' in http
assert 'ipv6_diagnostic_start()' not in Path('openm1/main.c').read_text()
assert not re.search(r'micoWlan|StartNetwork|SuspendStation|SuspendSoftAP', ipv6)
assert 'gethostbyname' in mqtt and 'AF_INET' in mqtt and 'AF_INET6' not in mqtt
assert 'WIFI_AP_AUTO_CLOSE_SUPPORTED 0' in Path('openm1/wifi_station_logic.h').read_text()
assert '#if WIFI_AP_AUTO_CLOSE_SUPPORTED' in wifi
assert 'OPENM1_CONFIG_VERSION 3' in Path('openm1/config_store.h').read_text()
print('V016_IPV6_STATIC_PASS: manual stages, bounded TCP, IPv4 MQTT, AP safety preserved')
