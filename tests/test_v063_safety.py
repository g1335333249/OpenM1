from pathlib import Path
main=Path('openm1/main.c').read_text()
wifi=Path('openm1/wifi_manager.c').read_text()
logic=Path('openm1/wifi_station_logic.h').read_text()
stats=Path('openm1/system_stats.c').read_text()
mqtt=Path('openm1/mqtt_manager.c').read_text()
ota=Path('openm1/recovery_ota.c').read_text()
http=Path('openm1/recovery_http.c').read_text()
script=Path('scripts/build_recovery.sh').read_text()
manifest=Path('tools/generate_manifest.py').read_text()
assert main.index('system_stats_init()') < main.index('recovery_http_start()') < main.index('config_store_init()') < main.index('wifi_manager_init()') < main.index('m1_display_init()') < main.index('m1_uart_init()') < main.index('wifi_manager_apply_boot_settings()') < main.index('mqtt_manager_init()')
assert main.index('wifi_manager_control_running()') < main.index('network_health_init()')
assert main.index('wifi_manager_station_ready()') < main.index('network_health_init()')
assert main.index('network_health_init()') > main.index('for (;;)')
assert 'mico_thread_msleep(250)' in main and 'mico_thread_msleep(500)' in main
for name in ('after HTTP','after display','after UART','before Wi-Fi control','after Wi-Fi control','after network health','before MQTT','after MQTT'):
    assert f'boot_heap_log("{name}")' in main
assert 'control worker unavailable; entering Recovery safe mode' in main
assert 'system_stats_low_memory_safe_mode()' in main
assert 'mico_rtos_get_time()>=30000u' in main
assert main.index('mqtt_manager_maybe_start(0)') > main.index('mico_rtos_get_time()>=30000u')
assert 'system_stats_maybe_start_cpu()' in main
assert 'OPENM1_MIN_HEAP_RESERVE 8192u' in Path('openm1/system_stats.h').read_text()
assert 'system_stats_register_stack_diagnostic()' in main
assert 'mico_notify_Stack_Overflow_ERROR' in stats
callback=stats.split('static void stack_overflow_notice(',1)[1].split('\n}',1)[0]
for forbidden in ('printf(', 'malloc(', 'mico_rtos_lock_mutex(', 'StartNetwork(', 'micoWlan'):
    assert forbidden not in callback
init=stats.split('OSStatus system_stats_init(void)',1)[1].split('void system_stats_maybe_start_cpu',1)[0]
assert 'mico_rtos_create_thread' not in init
assert 'boot_min_free_bytes' in stats and 'runtime_min_free_bytes' in stats
assert 'low_memory_safe_mode' in stats and 'boot_stable' in stats and 'stack_overflow_count' in stats
mqtt_init=mqtt.split('OSStatus mqtt_manager_init(void)',1)[1].split('void mqtt_manager_maybe_start',1)[0]
assert 'mico_rtos_create_thread' not in mqtt_init
assert 'config.mqtt_enabled' in mqtt_init
assert 'worker_created || worker_creation_pending' in mqtt
assert 'system_stats_begin_optional_thread(MQTT_WORKER_STACK)' in mqtt
assert 'system_stats_end_thread_creation()' in mqtt
assert 'thread_creation_mutex' in stats and 'mico_rtos_lock_mutex(&thread_creation_mutex)' in stats
assert 'system_stats_begin_optional_thread(SYSTEM_STATS_CPU_STACK)' in stats
assert 'system_stats_begin_optional_thread(NETWORK_HEALTH_WORKER_STACK)' in main
assert 'deferred_low_memory' in mqtt
assert 'MQTT_WORKER_STACK 6144' in Path('openm1/mqtt_manager.h').read_text()
assert 'system_stats_begin_ota_thread(RECOVERY_OTA_STACK)' in ota and 'return -2;' in ota
assert 'system_stats_end_thread_creation()' in ota
assert 'Insufficient free memory for safe OTA' in http and 'ota_result==-2?503:409' in http
assert 'WIFI_AP_BOOT_FAILSAFE_MS 120000u' in logic
assert 'WIFI_AP_STABLE_BEFORE_CLOSE_MS 30000u' in logic
assert 'WIFI_AP_CLOSE_RETRY_MS 30000u' in logic
assert 'wifi_ap_close_timing_ready' in wifi
assert 'station_snapshot(&final_link,&final_ip,&final_good,&final_error)' in wifi
assert wifi.index('station_snapshot(&final_link') < wifi.index('micoWlanSuspendSoftAP()')
assert 'rearm_after_restore' in wifi and 'one-time Station arm after Recovery AP restore' in wifi
assert 'mico_thread_msleep(500);' in wifi and 'err=station_start(&desired);' in wifi
for notice in ('mico_notify_WIFI_CONNECT_FAILED','mico_notify_WIFI_Fatal_ERROR','mico_notify_WIFI_STATUS_CHANGED'):
    assert notice in wifi
for name in ('wifi_connect_failed_notice','wifi_fatal_notice','wifi_status_notice'):
    callback=wifi.split(f'static void {name}(',1)[1].split('}',1)[0]
    for forbidden in ('StartNetwork(', 'micoWlan', 'printf(', 'malloc(', 'mico_rtos_lock_mutex('):
        assert forbidden not in callback,(name,forbidden)
assert 'wifi_connect_fail_count' in wifi and 'wifi_fatal_error_count' in wifi and 'last_wifi_event' in wifi
assert '$(NAME)_CFLAGS += -fstack-usage' in Path('openm1/openm1.mk').read_text()
assert 'tools/check_stack_budget.py' in script and 'dist/stack-usage' in script
for key in ('ota_runtime_heap_preflight','low_memory_safe_mode','mqtt_lazy_worker','cpu_stats_lazy_worker'):
    assert f"'{key}':True" in manifest
print('V063_SAFETY_PASS: staged boot, lazy optional workers, OTA preflight, AP failsafe, notifications and stack budget')
