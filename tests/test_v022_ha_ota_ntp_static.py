from pathlib import Path

config = Path('openm1/config_store.c').read_text()
header = Path('openm1/config_store.h').read_text()
ota = Path('openm1/recovery_ota.c').read_text()
mqtt = Path('openm1/mqtt_manager.c').read_text()
ha = Path('openm1/homeassistant.c').read_text()
ntp = Path('openm1/time_sync.c').read_text()
http = Path('openm1/recovery_http.c').read_text()
page = Path('openm1/recovery_page.html').read_text()

assert '#define OPENM1_CONFIG_VERSION 5u' in header
assert 'OPENM1_CONFIG_V4_SIZE 936u' in header
assert 'config_v4_valid(saved)' in config and 'defer_migration=1' in config
assert 'config->ntp_enabled=1' in config and 'config->ntp_interval_minutes=360' in config
assert 'config->ha_ota_enabled=0' not in config  # zero initialized by defaults
assert 'if (!job.background && job.fd>=0) close(job.fd)' in ota
assert 'if (ok && job.background) ok=verify_expected_sha256' in ota
assert ota.index('verify_expected_sha256(total,job.expected_sha256)') < ota.index('mico_ota_switch_to_new_fw(')
assert 'if (!flash_read(at,io_buffer,count) || SHA256Input' in ota
callback = mqtt.split('static void firmware_install_callback(', 1)[1].split('static void brightness_message_callback(', 1)[0]
assert 'data->message->retained' in callback and 'ha_ota_install_payload_valid(' in callback
logic = Path('openm1/ha_ota_logic.c').read_text()
assert 'length==7 && !memcmp(payload,"INSTALL",7)' in logic
for forbidden in ('recovery_ota_begin', 'MicoFlash', 'gethostbyname', 'MQTTPublish', 'MicoSystemReboot'):
    assert forbidden not in callback
assert 'MQTTSubscribe(&client,firmware_install_topic' in mqtt
assert 'recovery_ota_begin_background_url(current.ha_ota_url,current.ha_ota_sha256)' in mqtt
assert 'wifi_manager_station_ready_uptime_ms()>=30000u' in mqtt
assert '"%s/update/%s/firmware/config"' in ha
assert '"device_class":"firmware"' not in ha or 'device_class' in ha
assert 'if (remove || !config->ha_ota_enabled' in ha
assert 'MicoRtcSetTime((time_t)utc)' in ntp and 'MicoRtcGetTime(&readback)' in ntp
assert 'select(fd+1,&readfds,NULL,NULL,&timeout)' in ntp
assert 'from.sin_addr.s_addr!=target.sin_addr.s_addr' in ntp
assert 'recovery_ota_busy()' in ntp and 'wifi_manager_station_ready()' in ntp
for route in ('/api/time/status', '/api/time/config', '/api/time/sync',
              '/api/homeassistant/ota/config'):
    assert route in http and route in page
assert 'ntp.aliyun.com' in page and 'ntp1.aliyun.com' in page
assert 'ntp.tencent.com' in page and 'ntp1.tencent.com' in page
assert 'http_lan_only' in Path('openm1/ha_ota.c').read_text()
print('V022_HA_OTA_NTP_STATIC_PASS')
