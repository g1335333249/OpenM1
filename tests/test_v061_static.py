from pathlib import Path

wifi = Path('openm1/wifi_manager.c').read_text()
settings = Path('openm1/wifi_settings.c').read_text()
stats = Path('openm1/system_stats.c').read_text()
main = Path('openm1/main.c').read_text()
http = Path('openm1/recovery_http.c').read_text()
page = Path('openm1/recovery_page.html').read_text(encoding='utf-8')
assert main.index('recovery_http_start()') < main.index('config_store_init()') < main.index('wifi_manager_apply_boot_settings()')
assert 'micoWlanSuspendSoftAP()' in wifi and 'recovery_ap_start()' in wifi
assert 'WIFI_AP_CLOSE_GRACE_MS 3000u' in wifi and 'mico_thread_msleep(1000)' in wifi
assert 'station_matches_saved(config.wifi_ssid)' in wifi
assert 'recovery_ota_busy()' in wifi
assert '"password_nonempty"' in settings or '\\"password_nonempty\\"' in settings
assert 'config->wifi_password[0]' in settings and '\\"password\\":' not in settings.split('void wifi_settings_json',1)[1]
assert 'MicoGetMemoryInfo()' in stats and 'MicoFlashGetInfo' in stats
assert 'SYSTEM_STATS_CPU_SAMPLE_MS' in stats and 'SYSTEM_STATS_CPU_INTERVAL_MS-SYSTEM_STATS_CPU_SAMPLE_MS' in stats
assert '/api/system/stats' in http and '/api/wifi/settings' in http
assert 'refreshSystemStats()' in page and 'wifiSettingsInitialized' in page
assert '0.6.1' in main and '0.6.1' in http
assert 'wifi_station_supervisor_worker' in wifi
assert 'wifi_sta_worker' not in wifi and 'mico_rtos_delete_thread(NULL)' not in wifi
assert 'WIFI_STATION_SUPERVISOR_STACK' in wifi
assert 'WIFI_STATION_MONITOR_INTERVAL_MS' in wifi
assert 'WIFI_STATION_LOSS_THRESHOLD' in Path('openm1/wifi_station_logic.h').read_text()
assert 'WIFI_STATION_READY_THRESHOLD' in Path('openm1/wifi_station_logic.h').read_text()
assert 'wlan_control_mutex' in wifi and 'station_suspend()' in wifi and 'station_start(&local_config)' in wifi
assert 'micoWlanPowerOff' not in wifi and 'micoWlanPowerOn' not in wifi
for path in Path('openm1').glob('*.c'):
    if path.name == 'wifi_manager.c':
        continue
    code = path.read_text()
    assert 'micoWlanGetLinkStatus' not in code, path
    assert 'micoWlanGetIPStatus' not in code, path
    assert 'micoWlanSuspendStation' not in code, path
    assert 'StartNetwork(Station)' not in code, path
for accessor in ('wifi_manager_station_ready', 'wifi_manager_station_link',
                 'wifi_manager_station_rssi', 'wifi_manager_station_ip',
                 'wifi_manager_status_json'):
    body = wifi.split(' '+accessor+'(', 1)[1].split('\n}', 1)[0]
    assert 'micoWlanGetLinkStatus' not in body and 'micoWlanGetIPStatus' not in body
assert 'reconnect_attempts' in wifi and 'manual_disconnect_latched' in wifi
assert '连接稳定性' in page and '累计掉线' in page
assert '0.6.1' in Path('tools/generate_manifest.py').read_text()
print('V061_STATIC_PASS: boot order, AP policy, cached Station HAL, supervisor and diagnostics')
