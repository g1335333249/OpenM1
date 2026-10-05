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
assert '0.6.0' in main and '0.6.0' in http
print('V060_STATIC_PASS: boot order, AP policy, stats and settings API')
