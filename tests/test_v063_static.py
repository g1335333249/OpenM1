from pathlib import Path
wifi=Path('openm1/wifi_manager.c').read_text()
logic=Path('openm1/wifi_station_logic.h').read_text()
main=Path('openm1/main.c').read_text()
http=Path('openm1/recovery_http.c').read_text()
page=Path('openm1/recovery_page.html').read_text()
manifest=Path('tools/generate_manifest.py').read_text()
assert '0.6.14' in main and '0.6.14' in http and '0.6.14' in manifest
assert 'wifi_control_worker(mico_thread_arg_t arg)' in wifi
assert wifi.count('mico_rtos_create_thread(')==1
for old in ('ap_policy_worker','wifi_station_supervisor_worker','wlan_control_mutex','station_supervisor_thread','ap_policy_thread','openm1_config_t config;\n    OSStatus err;\n    int eligible'):
    assert old not in wifi, old
assert '#define WIFI_CONTROL_WORKER_STACK 5120u' in logic
worker=wifi.split('static void wifi_control_worker(mico_thread_arg_t arg)',1)[1].split('OSStatus wifi_manager_init',1)[0]
assert 'openm1_config_t' not in worker
assert 'mico_thread_msleep(WIFI_CONTROL_INTERVAL_MS)' in worker
assert 'micoWlanStartScanAdv()' in worker
assert 'wifi_station_rearm_due' in worker and 'WIFI_STA_NATIVE_RECONNECT_WAIT' in worker
assert 'config.wifi_retry_interval=WIFI_NATIVE_RETRY_INTERVAL_MS;' in wifi
assert 'WIFI_NATIVE_RETRY_INTERVAL_MS 5000u' in logic
assert 'WIFI_NATIVE_RECONNECT_GRACE_MS 60000u' in logic
assert 'WIFI_CONTROLLED_REARM_MIN_INTERVAL_MS 60000u' in logic
assert 'station_suspend();' in worker and 'station_start(&desired);' in worker
assert worker.index('if (disconnect)') < worker.index('station_suspend();')
assert 'if (!armed && !boot_waiting' in worker and 'station_start(&desired)' in worker
assert worker.index('ap_control_step(') < worker.index('if (desired.want_connected')
assert 'micoWlanGetLinkStatus' in wifi and 'micoWlanGetIPStatus(&ap,Soft_AP)' in wifi
assert 'micoWlanSuspendSoftAP' in wifi and 'StartNetwork(&config)' in wifi
for name in ('wifi_manager_connect','wifi_manager_disconnect','wifi_manager_start_scan','wifi_manager_status_json'):
    body=wifi.split(' '+name+'(',1)[1].split('\n}',1)[0]
    for hal in ('StartNetwork(', 'micoWlanSuspendStation(', 'micoWlanSuspendSoftAP(', 'micoWlanGetLinkStatus(', 'micoWlanGetIPStatus(', 'micoWlanStartScanAdv('):
        assert hal not in body,(name,hal)
for path in Path('openm1').glob('*.c'):
    if path.name in ('main.c','wifi_manager.c'): continue
    code=path.read_text()
    for hal in ('micoWlanGetLinkStatus(', 'micoWlanGetIPStatus(', 'micoWlanSuspendStation(', 'micoWlanSuspendSoftAP(', 'micoWlanStartScanAdv('):
        assert hal not in code,(path,hal)
assert main.index('system_stats_init()') < main.index('recovery_http_start()') < main.index('config_store_init()') < main.index('wifi_manager_init()') < main.index('m1_display_init()') < main.index('wifi_manager_apply_boot_settings()') < main.index('mqtt_manager_init()')
assert 'mico_thread_msleep(500);' in main
assert 'native_reconnect_successes' in wifi and 'wifi_control_loop_count' in wifi
assert 'MiCO 原生重连' in page and '受控重置 Station' in page
assert 'openm1_log_info("WIFI","first Station arm")' in wifi
assert 'wifi_ap_close_eligible' in wifi and 'wifi_ap_close_eligible' in Path('openm1/wifi_station_logic.c').read_text()
assert 'openm1_config_t' not in worker
import re
assert not re.search(r'\b(?:char|uint8_t|uint16_t|uint32_t)\s+\w+\[(?:[1-9]\d{3,})\]',worker)
assert wifi.split('OSStatus wifi_manager_init(void)',1)[1].split('OSStatus wifi_manager_apply_boot_settings',1)[0].count('mico_rtos_create_thread')==0
assert 'micoWlanStartScanAdv()' not in wifi.split('int wifi_manager_start_scan(void)',1)[1].split('int wifi_manager_scan_snapshot',1)[0]
print('V063_STATIC_PASS: single WLAN worker, 5s native retry, 60s fallback, boot ordering and cached HTTP')
