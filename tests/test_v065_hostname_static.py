from pathlib import Path
main=Path('openm1/main.c').read_text()
identity=Path('openm1/recovery_identity.c').read_text()
http=Path('openm1/recovery_http.c').read_text()
wifi=Path('openm1/wifi_manager.c').read_text()
page=Path('openm1/recovery_page.html').read_text()
logic=Path('openm1/wifi_station_logic.h').read_text()
assert 'static char device_hostname[32] = "OpenM1";' in identity
assert '"OpenM1-%02X%02X%02X"' in identity
assert 'mac[3],mac[4],mac[5]' in identity
assert 'extern char *sethostname(char *name);' in main
assert main.count('sethostname(recovery_hostname())')==1
assert main.index('recovery_set_identity(recovery_ssid, mac_text)') < main.index('recovery_prepare_hostname(mac,mac_valid)') < main.index('sethostname(recovery_hostname())') < main.index('StartNetwork(&wifi_config)') < main.index('wifi_manager_apply_boot_settings()')
assert 'mac_valid=memcmp(mac,' in main and '(mac[0] & 1u) == 0' in main
assert '"OpenM1-%02X%02X%02X"' in main
assert 'char recovery_ssid[32] = RECOVERY_FALLBACK_SSID;' in main
assert 'DEVICE: DHCP hostname setup unavailable' in main
assert '"hostname"' in http.replace('\\"','"')
assert '"dhcp_hostname"' in wifi.replace('\\"','"')
assert "['设备名称',info.hostname||'OpenM1']" in page
assert '设备名称通过 DHCP 上报给家庭路由器' in page
assert '#define WIFI_BOOT_AUTO_CONNECT_GRACE_MS 60000u' in logic
assert '#define WIFI_AP_MISSING_THRESHOLD 3u' in logic
assert '#define WIFI_NATIVE_RETRY_INTERVAL_MS 5000u' in logic
assert '3080B002.023' in main
for file in ('openm1/m1_display.c','openm1/m1_uart.c','openm1/m1_sensor.c','openm1/recovery_ota.c','openm1/mqtt_manager.c','openm1/homeassistant.c'):
    assert 'sethostname(' not in Path(file).read_text()
print('V065_HOSTNAME_STATIC_PASS')
