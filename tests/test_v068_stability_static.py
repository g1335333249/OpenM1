from pathlib import Path

main=Path('openm1/main.c').read_text()
stats=Path('openm1/system_stats.c').read_text()
stats_h=Path('openm1/system_stats.h').read_text()
mqtt=Path('openm1/mqtt_manager.c').read_text()
health=Path('openm1/network_health.c').read_text()
health_state=Path('openm1/network_health_state.c').read_text()
page=Path('openm1/recovery_page.html').read_text()
budget=Path('tools/check_stack_budget.py').read_text()

assert '#define OPENM1_HOUSEKEEPING_STACK 3072u' in main
assert 'mico_thread_msleep(1000)' in main and '(now-last_heartbeat_ms)>=10000u' in main
assert '!system_stats_stack_overflow_count()' not in main
assert 'system_stats_stack_fault_quiet_remaining_ms()' in main
assert 'mqtt_manager_maybe_start(0)' in main
assert 'MQTT_WORKER_CREATE_RETRY_MS 30000u' in mqtt
assert 'worker_retry_not_before_ms' in mqtt
assert 'cpu_retry_not_before_ms' in stats
assert '#define OPENM1_STACK_FAULT_QUIET_MS 30000u' in stats_h
assert 'system_stats_stack_fault_cpu_ready()' in main
assert 'last_seen_stack_overflow_count=count' in stats
assert 'OPENM1_STACK_FAULT_QUIET_MS-elapsed' in stats
callback=stats.split('static void stack_overflow_notice(',1)[1].split('\n}',1)[0]
for unsafe in ('printf(', 'snprintf(', 'strlen(', 'strncpy(', 'malloc(', 'free(',
               'openm1_log', 'mico_rtos_lock_mutex(', 'StartNetwork(', 'micoWlan'):
    assert unsafe not in callback,unsafe
assert 'last_stack_overflow_task[i]' in callback
assert 'stack_overflow_count++' in callback
for field in ('stack_overflow_recent','stack_fault_quiet_ms','stack_fault_quiet_remaining_ms',
              'last_stack_overflow_task'):
    assert field in stats
for field in ('worker_created','worker_creation_pending','station_ready','start_block_reason',
              'stack_fault_quiet_remaining_ms','low_memory_safe_mode','ota_busy'):
    assert field in mqtt
for state in ('waiting_worker','waiting_network','deferred_stack_fault','deferred_low_memory',
              'connecting','connected','reconnecting'):
    assert state in mqtt+Path('openm1/mqtt_diagnostics.c').read_text(),state
assert 'health.state=wifi_manager_station_ready()?NETWORK_CHECKING:NETWORK_NO_WIFI' in health
assert 'snapshot.state=wifi_manager_station_ready()?NETWORK_CHECKING:NETWORK_NO_WIFI' in health
assert 'if (state==NETWORK_NO_WIFI) return M1_NET_DISPLAY_DISCONNECTED' in health_state
assert 'return M1_NET_DISPLAY_ONLINE; /* CHECKING means Station has link and IP. */' in health_state
assert 'id="log-copy"' in page
assert "fetch('/api/logs/download')" in page
assert 'navigator.clipboard.writeText(logText)' in page
assert "document.createElement('textarea')" in page
assert "document.execCommand('copy')" in page
assert '复制失败，请使用下载日志' in page
assert '6144+2048+4096+5120+3072' in budget
assert 'SO_RCVTIMEO' not in mqtt and 'SO_SNDTIMEO' not in mqtt
assert 'network->mqttread=openm1_mqtt_read' in mqtt
for forbidden in ('StartNetwork(', 'micoWlan', 'wifi_manager_connect('):
    assert forbidden not in mqtt
assert 'assert(!c.wifi_auto_connect' in Path('tests/test_wifi_settings.c').read_text()
assert '#define OPENM1_MQTT_INBOUND_MAX 1024u' in Path('openm1/mqtt_bounded_read.h').read_text()
print('V068_STATIC_PASS: stack cooldown, MQTT worker diagnostics, CHECKING display, full log copy')
