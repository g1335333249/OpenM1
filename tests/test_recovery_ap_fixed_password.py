#!/usr/bin/env python3
"""Source-level guardrails for the isolated Recovery AP password hardware test."""
import re
from pathlib import Path

root = Path(__file__).resolve().parents[1]
header = (root / 'openm1/recovery.h').read_text()
main = (root / 'openm1/main.c').read_text()
wifi = (root / 'openm1/wifi_manager.c').read_text()
sdk_header = (root / 'mico-os/include/mico_wlan.h').read_text()
sdk_wrapper = (root / 'mico-os/platform/MCU/MX1290/moc/moc_api.c').read_text()
workflow = (root / '.github/workflows/build.yml').read_text()
build = (root / 'scripts/build_recovery.sh').read_text()

password = re.search(r'^#define RECOVERY_AP_DEFAULT_PASSWORD "([^"]+)"$', header, re.M)
assert password and password.group(1) == 'QQ123456qq'
assert 8 <= len(password.group(1)) <= 63
assert 'char wifi_key[64]' in sdk_header
assert 'return lib_api_p->micoWlanStart(inNetworkInitPara)' in sdk_wrapper

boot = main[main.index('printf("RECOVERY: starting SoftAP'):main.index('recovery_ota_partition_log();')]
restore = wifi[wifi.index('static OSStatus recovery_ap_start(void)'):wifi.index('static int recovery_ap_probe(void)')]
for source, target in ((boot, 'wifi_config'), (restore, 'config')):
    key = re.search(
        rf'snprintf\(\s*{target}\.wifi_key\s*,\s*sizeof\(\s*{target}\.wifi_key\s*\)\s*,\s*"%s"\s*,\s*RECOVERY_AP_DEFAULT_PASSWORD\s*\);',
        source,
    )
    assert key
    assert key.start() < source.index(f'StartNetwork(&{target})')
    assert f'{target}.wifi_mode = Soft_AP' in source or f'{target}.wifi_mode=Soft_AP' in source
    assert f'{target}.dhcpMode = DHCP_Server' in source or f'{target}.dhcpMode=DHCP_Server' in source
    assert 'config_store_' not in source

assert main.count('StartNetwork(&wifi_config)') == 1
assert wifi.count('StartNetwork(&config)') == 2  # Station and AP restore only.
assert main.index('StartNetwork(&wifi_config)') < main.index('config_store_init()')
assert 'feature/recovery-ap-default-password' in workflow.split('branches:')[1].split(']')[0]
assert 'workflow_dispatch:' in workflow
assert 'OpenM1-v0.6.22' in build
assert 'OpenM1-v0.6.22' in workflow
assert 'ap_password' not in (root / 'openm1/config_store.h').read_text()
assert 'ap_password' not in (root / 'openm1/recovery_page.html').read_text()
print('[PASS] fixed Recovery AP password, both paths, boot order, isolated test artifact')
