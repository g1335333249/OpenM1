#!/usr/bin/env python3
import argparse
import hashlib
import json
from pathlib import Path
from ota_common import SDK_COMMIT, require_sdk_kernel
from verify_ota import verify

p = argparse.ArgumentParser()
p.add_argument('--ota', type=Path, required=True)
p.add_argument('--app', type=Path, required=True)
p.add_argument('--sdk-kernel', type=Path, required=True)
p.add_argument('--app-header-report', type=Path, required=True)
p.add_argument('--output', type=Path, required=True)
p.add_argument('--toolchain', required=True)
a = p.parse_args()
checks, details = verify(a.ota, a.sdk_kernel, a.app)
header = a.app_header_report.read_text()
header_valid = all(line.startswith('PASS ') for line in header.splitlines() if line.startswith(('PASS ', 'FAIL ')))
symbols_valid = all(f'PASS {symbol}' in header for symbol in ('user_handler','moc_app_main','moc_adapter','main'))
sdk_valid = Path('mico-os').is_dir() and __import__('subprocess').check_output(['git','-C','mico-os','rev-parse','HEAD'], text=True).strip() == SDK_COMMIT
kernel = require_sdk_kernel(a.sdk_kernel)
valid = all(checks.values()) and header_valid and symbols_valid and sdk_valid and '5.4.1' in a.toolchain
data = {'name':'OpenM1-BootProbe','version':'0.0.3','board':'MK3080B','sdk_commit':SDK_COMMIT,
        'kernel_version':'3080B002.023','sdk_kernel_version':'3080B002.023',
        'kernel_source':'mico-os/resources/moc_kernel/3080B/kernel.bin',
        'kernel_sha256':hashlib.sha256(kernel).hexdigest(),'interface_version':3,
        'moc_app_header_valid':header_valid,'startup_symbols_valid':symbols_valid,
        'ota_format_valid':all(checks.values()),'kernel_app_same_sdk':sdk_valid,
        'safe_to_flash':valid,
        'boot_verified_on_hardware':True,
        'main_verified_on_hardware':True,
        'runtime_verified_on_hardware':True,
        'wifi_init_fix':'MicoInit before micoWlanPowerOn',
        'wifi_verified_on_hardware':False,
        'softap_verified_on_hardware':False,
        'hardware_verified':False,'toolchain':a.toolchain,
        'app_size':details['app_size'],'app_payload_size':details['payload_size'],
        'ota_size':details['size'],'app_crc16':details['crc16'],
        'ota_md5':details['md5'],'ota_sha256':details['sha256']}
a.output.write_text(json.dumps(data, indent=2)+'\n')
raise SystemExit(0 if valid else 1)
