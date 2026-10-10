#!/usr/bin/env python3
"""Guard the v0.6.14 diagnostics and the stable WLAN/OTA behavior."""
from pathlib import Path
import re

root = Path('openm1')
wifi = (root / 'wifi_manager.c').read_text()
health = (root / 'network_health.c').read_text()
mapping = (root / 'network_health_state.c').read_text()
display = (root / 'm1_display.c').read_text()
ipv6 = (root / 'ipv6_diagnostic.c').read_text()
http = (root / 'recovery_http.c').read_text()
page = (root / 'recovery_page.html').read_text()
mqtt = (root / 'mqtt_manager.c').read_text()

assert '#define WIFI_HISTORY_CAPACITY 32u' in wifi
assert 'static wifi_history_record_t wifi_history[WIFI_HISTORY_CAPACITY]' in wifi
assert 'wifi_manager_history_note(WIFI_HISTORY_STATION_BAD)' in wifi
assert 'wifi_manager_history_note(WIFI_HISTORY_STATION_LOST)' in wifi
assert 'wifi_manager_history_note(WIFI_HISTORY_STATION_CONNECTED)' in wifi
assert 'wifi_manager_history_note(WIFI_HISTORY_AP_RESTORED)' in wifi
assert 'wifi_manager_history_note(WIFI_HISTORY_AP_MISSING_CONFIRMED)' in wifi
assert 'wifi_manager_history_note(WIFI_HISTORY_AP_RESTORE_FAILED)' in wifi
assert 'wifi_manager_history_note(WIFI_HISTORY_DISPLAY_APPLIED)' in display
assert 'wifi_manager_history_note(WIFI_HISTORY_NETWORK_HEALTH)' in health
assert 'status.last_link_query_result=err;' in wifi
assert 'status.last_ip_query_result=ip_err;' in wifi
assert 'status.last_query_link_connected=err==kNoErr?link->is_connected:-1;' in wifi
assert '"/api/wifi/history"' in http
assert 'send_wifi_history(fd)' in http
assert "'/api/wifi/history'" in page
assert 'wifi-history-copy' in page and 'wifi-history-export' in page
assert 'if (state==NETWORK_NO_INTERNET) return M1_NET_DISPLAY_NO_INTERNET;' in mapping
assert 'if (state==NETWORK_NO_WIFI) return M1_NET_DISPLAY_DISCONNECTED;' in mapping
assert 'return M1_NET_DISPLAY_ONLINE; /* CHECKING means Station has link and IP. */' in mapping
assert 'M1_WIFI_ICON_PWM' in display and 'M1_RED_X_PWM' in display
assert "s.state==='no_wifi'?'闪烁':'常亮'" in page

assert 'AF_INET6' in ipv6 and 'inet_pton(family,input,address)' in ipv6
assert 'IPV6_PRIMARY_DNS' in ipv6
assert 'api->lwip_socket' in ipv6 and 'api->lwip_sendto' in ipv6
assert 'if (recovery_ota_busy()) return -2;' in ipv6
assert 'if (!wifi_manager_station_ready()) return -3;' in ipv6
assert 'IPV6_DIAGNOSTIC_WORKER_STACK+9216u+8192u' in ipv6
assert 'ipv6_diagnostic_start()' in http
assert 'ipv6_diagnostic_init()' in (root / 'main.c').read_text()
assert 'ipv6_diagnostic_start()' not in (root / 'main.c').read_text()
assert not re.search(r'micoWlan|StartNetwork|SuspendStation|SuspendSoftAP', ipv6)
assert 'gethostbyname' in mqtt and 'AF_INET' in mqtt
assert 'AF_INET6' not in mqtt

assert '#define WIFI_BOOT_AUTO_CONNECT_GRACE_MS 60000u' in (root / 'wifi_station_logic.h').read_text()
assert 'WIFI_NATIVE_RETRY_INTERVAL_MS' in wifi and 'wifi_retry_interval=WIFI_NATIVE_RETRY_INTERVAL_MS' in wifi
assert 'OPENM1_FACTORY_RESET_DRY_RUN 1' in (root / 'button_manager.h').read_text()
assert 'OPENM1_CONFIG_VERSION 3' in (root / 'config_store.h').read_text()
print('V014_DIAGNOSTICS_STATIC_PASS: 32-entry transitions, manual IPv6, IPv4 MQTT, WLAN/OTA guard')
