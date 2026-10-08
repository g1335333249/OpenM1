from pathlib import Path
import re
log=Path('openm1/openm1_log.c').read_text()
http=Path('openm1/recovery_http.c').read_text()
wifi=Path('openm1/wifi_manager.c').read_text()
page=Path('openm1/recovery_page.html').read_text()
assert '#define OPENM1_LOG_RECORD_COUNT 24u' in Path('openm1/openm1_log.h').read_text()
assert 'malloc(' not in log and 'MicoFlash' not in log and 'StartNetwork' not in log
assert 'static char scratch[OPENM1_LOG_MESSAGE_MAX]' in log and 'mico_rtos_lock_mutex(&log_mutex)' in log
assert 'OPENM1_LOG_PAGE_RECORDS 8u' in Path('openm1/openm1_log.h').read_text()
assert 'openm1_http_explicit_recovery_activity(method,path)' in http
activity=Path('openm1/http_activity.c').read_text()
for path in ('/api/logs/download','/api/wifi/connect','/api/wifi/disconnect','/api/wifi/settings','/api/reboot','/api/ota/upload','/api/ota/url'):
    assert path in activity
for path in ('/api/logs?','/api/wifi/status','/api/system/stats','/api/sensors','/api/ota/status'):
    assert path not in activity
assert 'if (openm1_http_explicit_recovery_activity(method,path)) wifi_manager_note_recovery_activity();' in http
assert 'OPENM1_LOG_MESSAGE_MAX 64u' in Path('openm1/openm1_log.h').read_text()
assert 'char line[192]' in http and 'send_log_download' in http and '/api/logs/clear' in http
assert 'setInterval(()=>{if(activeTab===\'system\')refreshLogs()},2000)' in page
assert "childElementCount>200" in page and 'ROM/MOC' in page and '/api/uart/raw' in page
assert '#define WIFI_BOOT_AUTO_CONNECT_GRACE_MS 60000u' in Path('openm1/wifi_station_logic.h').read_text()
assert 'wifi_retry_interval=WIFI_NATIVE_RETRY_INTERVAL_MS' in wifi
for callback in ('wifi_status_notice','wifi_connect_failed_notice','wifi_fatal_notice'):
    if callback in wifi:
        body=wifi.split(callback,1)[1].split('\n}',1)[0]
        assert 'openm1_log_' not in body
print('[PASS] v0.6.6 RAM log and rescue activity static checks')
