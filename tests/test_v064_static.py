from pathlib import Path
import re
main=Path('openm1/main.c').read_text()
wifi=Path('openm1/wifi_manager.c').read_text()
logic=Path('openm1/wifi_station_logic.h').read_text()
page=Path('openm1/recovery_page.html').read_text()
budget=Path('tools/check_stack_budget.py').read_text()
manifest=Path('tools/generate_manifest.py').read_text()
assert '#define WIFI_BOOT_AUTO_CONNECT_GRACE_MS 60000u' in logic
assert '#define WIFI_AP_PROBE_INTERVAL_MS 5000u' in logic
assert '#define WIFI_AP_MISSING_THRESHOLD 3u' in logic
assert 'wifi_ap_restore_confident(' in wifi
assert 'ap_probe_failures=wifi_ap_missing_after_probe(ap_probe_failures,0)' in wifi
assert 'wifi_ap_restore_confident(now,*ap_missing_since,*ap_probe_failures,*ap_down_confirmed)' in wifi
assert '(now>=WIFI_AP_BOOT_SELF_HEAL_HOLD_MS || *ap_down_confirmed)' in wifi
assert 'next_ap_probe_ms=mico_rtos_get_time()+WIFI_AP_PROBE_INTERVAL_MS' in wifi
assert 'ap_down_confirm_at=now+1000u' in wifi
assert '(events&WIFI_EVENT_AP_UP)' in wifi
assert 'wifi_manager_set_initial_ap_state(ap_result==kNoErr)' in main
assert 'boot_auto_connect_remaining_ms' in wifi and 'boot_auto_connect_waiting' in wifi
assert 'WIFI_RECOVERY_WEB_ACTIVITY_HOLD_MS 15000u' in logic
assert 'wifi_manager_note_recovery_activity();' in Path('openm1/recovery_http.c').read_text()
assert '(int32_t)(now-WIFI_BOOT_AUTO_CONNECT_GRACE_MS)>=0' in wifi
assert 'desired.want_connected && !disconnect && !scan_is_active() && !recovery_ota_busy()' in wifi
callback=wifi.split('static void wifi_status_notice(',1)[1].split('\n}',1)[0]
for event in ('NOTIFY_STATION_UP','NOTIFY_STATION_DOWN','NOTIFY_AP_UP','NOTIFY_AP_DOWN'):
    assert event in callback
for forbidden in ('StartNetwork(', 'micoWlan', 'mico_rtos_lock_mutex(', 'printf(', 'malloc('):
    assert forbidden not in callback
assert 'last_wifi_connect_fail_error=err' in wifi
for event in ('station_up','station_down','ap_up','ap_down'):
    assert f'"{event}"' in wifi
assert 'last_wifi_event_code' in wifi
assert 'network_health_available()' in wifi and 'display_station_fallback(1)' in wifi
assert '#define OPENM1_HOUSEKEEPING_STACK 3072u' in main
assert 'static void housekeeping_worker(' in main
assert 'network_health_init()' in main.split('static void housekeeping_worker(',1)[1].split('int main(void)',1)[0]
assert 'StartNetwork(' not in main.split('static void housekeeping_worker(',1)[1].split('int main(void)',1)[0]
assert 'mico_rtos_create_thread(&housekeeping_thread' in main
assert re.search(r'openm1_log_info\("BOOT","main release.*?\n\s*return 0;\n}',main,re.S)
assert 'static network_InitTypeDef_st wifi_config;' in main
assert 'boot-rescue-window' in page and '开机救援窗口：剩余' in page
assert 'PERMANENT_CRITICAL_STACK_BUDGET' in budget and 'TEMPORARY_OTA_STACK' in budget
assert 'housekeeping_worker' in budget
for key in ('boot_recovery_only_window_ms','ap_probe_interval_ms','ap_missing_threshold','main_thread_released_after_boot','housekeeping_stack_bytes'):
    assert key in manifest
assert 'config.wifi_retry_interval=WIFI_NATIVE_RETRY_INTERVAL_MS;' in wifi
print('V064_STATIC_PASS: 60s rescue, debounced AP recovery, events, released app thread, heap budget')
