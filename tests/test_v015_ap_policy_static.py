#!/usr/bin/env python3
"""Guard the MK3080B .023 Recovery AP safety policy and legacy migration."""
from pathlib import Path
import re

wifi = Path('openm1/wifi_manager.c').read_text()
logic = Path('openm1/wifi_station_logic.h').read_text()
settings = Path('openm1/wifi_settings.c').read_text()
page = Path('openm1/recovery_page.html').read_text()
manifest = Path('tools/generate_manifest.py').read_text()

assert re.search(r'^#define WIFI_AP_AUTO_CLOSE_SUPPORTED 0$', logic, re.M)
close = wifi.index('micoWlanSuspendSoftAP();')
guard = wifi.rfind('#if WIFI_AP_AUTO_CLOSE_SUPPORTED', 0, close)
end = wifi.find('#else', close)
assert guard != -1 and end > close
assert 'policy.disable_ap_after_connect=0;' in wifi
assert 'policy.saved_ap_close_requested=config.ap_disable_after_sta_connected;' in wifi
boot = wifi.split('OSStatus wifi_manager_apply_boot_settings(void)', 1)[1].split('void wifi_manager_settings_json', 1)[0]
assert 'config_store_save' not in boot
assert 'WIFI_HISTORY_AP_CLOSE_SUPPRESSED' in boot
assert 'WIFI_HISTORY_AP_CLOSE_ATTEMPT' in wifi
assert 'WIFI_HISTORY_AP_CLOSE_FAILED' in wifi
save = wifi.split('int wifi_manager_save_settings(', 1)[1].split('int wifi_manager_connect(', 1)[0]
assert save.index('config.ap_disable_after_sta_connected=0;') < save.index('config_store_save(&config)')
assert 'if (!strcmp(ap_off->value,"true")) return -5;' in settings
assert 'legacy_saved_ap_close_requested' in settings and 'ap_auto_close_supported' in settings
assert 'legacy_saved_ap_close_requested' in wifi and 'ap_auto_close_supported' in wifi
assert 'wifi-ap-off' not in page and 'syncApOffControl' not in page
assert '固定 Kernel 3080B002.023 上暂不支持安全地自动关闭 Recovery AP' in page
assert "'ap_disable_after_sta_supported':False" in manifest
assert 'recovery_ap_start();' in wifi
assert 'WIFI_NATIVE_RETRY_INTERVAL_MS' in wifi
assert 'M1_WIFI_ICON_PWM' in Path('openm1/m1_display.c').read_text()
print('V015_AP_POLICY_STATIC_PASS: close compiled out, legacy setting ignored, AP self-heal preserved')
