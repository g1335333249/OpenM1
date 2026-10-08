from pathlib import Path
ota=Path('openm1/recovery_ota.c').read_text()
http=Path('openm1/recovery_http.c').read_text()
main=Path('openm1/main.c').read_text()
wifi=Path('openm1/wifi_manager.c').read_text()
health=Path('openm1/network_health.c').read_text()
page=Path('openm1/recovery_page.html').read_text()
logic=Path('openm1/ota_transfer_logic.h').read_text()
assert '#define OTA_RECV_TIMEOUT_MS 5000u' in logic
assert '#define OTA_UPLOAD_IDLE_TIMEOUT_MS 60000u' in logic
assert '#define OTA_PREPARED_TIMEOUT_MS 120000u' in logic
assert 'ota_recv_step(count,socket_error' in ota
for field in ('recv_timeout_count','recv_retry_count','last_recv_error','last_progress_ms',
              'idle_ms','erase_duration_ms','flash_write_fail_count','peer_closed'):
    assert field in ota,field
for stage in ('Flash erase failed','Flash write failed','APP CRC mismatch','OTA MD5 mismatch',
              'Boot table update failed','Upload peer closed','Upload stalled'):
    assert stage in ota,stage
assert 'if (MicoFlashWrite(' in ota and 'fail("Flash write failed")' in ota
assert 'status.prepared=1' in ota and 'ota_prepared_expired' in ota
assert 'recovery_ota_prepare()' in http and '/api/ota/prepare' in http
assert "requestPost('/api/ota/prepare')" in page
assert page.index("requestPost('/api/ota/prepare')")<page.index("requestPost('/api/ota/upload'")
assert 'initial_len>sizeof(job.initial)' in ota
assert 'static char request[2049]' in http and 'while (used<2048' in http
assert 'return; /* OTA worker owns fd' in http
assert 'if (job.fd>=0) close(job.fd)' in ota
assert 'job.fd=-1;' in ota
assert 'mico_ota_switch_to_new_fw' in ota and 'recovery_ota_verify_flash' in ota
assert 'if (recovery_ota_busy()) { mico_thread_msleep(WIFI_CONTROL_INTERVAL_MS); continue; }' in wifi
assert 'if (recovery_ota_busy()) return -3;' in wifi
assert 'if (recovery_ota_busy()) return -2;' in wifi
assert 'if (!connected || recovery_ota_busy())' in health
assert 'network_health_attempted' not in main
assert 'network_health_retry_remaining(' in main and 'network_health_failed_at=mico_rtos_get_time()' in main
assert 'NETWORK_HEALTH_RETRY_MS 30000u' in Path('openm1/worker_retry_logic.h').read_text()
assert 'worker_retry_remaining_ms' in health and 'start_block_reason' in health
assert 'system_stats_stack_fault_quiet_remaining_ms()' in main
assert '0.6.13' in main and '0.6.13' in http and '0.6.13' in Path('tools/generate_manifest.py').read_text()
print('V069_OTA_STATIC_PASS')
