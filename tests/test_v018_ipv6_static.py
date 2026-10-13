from pathlib import Path
c=Path('openm1/ipv6_diagnostic.c').read_text()
h=Path('openm1/recovery_http.c').read_text()
page=Path('openm1/recovery_page.html').read_text()
assert 'ipv6_interface_query\\":\\"unavailable' in c
assert 'openm1_sockaddr_in6_size_check' in c
assert 'api->lwip_bind && api->lwip_getsockname' in c
assert 'close(fd);' in c and 'if (fd<0) goto done;' in c
assert 'if (recovery_ota_busy()) return -2;' in c
assert 'if (recovery_ota_busy()) return;' in c
assert 'so_error_checked' in c and '"null"' in c
assert 'OPENM1_DIAG_LAN_IPV6_HEX' in c
assert 'confirm_ipv6_deep' in h and '/api/ipv6/deep/status' in h
assert 'WIFI_AP_AUTO_CLOSE_SUPPORTED 0' in Path('openm1/wifi_station_logic.h').read_text()
assert 'micoWlanSuspendSoftAP' not in Path('openm1/wifi_manager.c').read_text().replace('micoWlanSuspendSoftAP()', '')
assert 'IPv4 / IPv6 TCP 对照' in page
assert '0.6.21' in Path('openm1/main.c').read_text()
print('V018_IPV6_STATIC_PASS')
