#!/usr/bin/env python3
import argparse
import hashlib
import json
import struct
from pathlib import Path
from ota_common import APP_OFFSET, SDK_COMMIT
from verify_ota import verify

p = argparse.ArgumentParser()
p.add_argument('--ota', type=Path, required=True)
p.add_argument('--reference', type=Path, required=True)
p.add_argument('--output', type=Path, required=True)
p.add_argument('--version', default='0.0.1')
p.add_argument('--toolchain', required=True)
a = p.parse_args()
checks, details = verify(a.ota, a.reference)
verified_toolchain = '5.4.1' in a.toolchain
safe = bool(checks) and all(checks.values()) and verified_toolchain
data = {
    'name': 'OpenM1', 'version': a.version, 'board': 'MK3080B',
    'platform': 'EMW3080BE', 'soc': 'MX1290', 'app_offset': hex(APP_OFFSET),
    'size': details.get('size'), 'app_payload_size': details.get('payload_size'),
    'crc16': details.get('crc16'), 'md5': details.get('md5'),
    'sha256': details.get('sha256'), 'kernel_sha256': details.get('kernel_sha256'),
    'toolchain': a.toolchain, 'toolchain_verified': verified_toolchain,
    'sdk_commit': SDK_COMMIT, 'reference_kernel_match': checks.get('kernel match', False),
    'safe_to_flash': safe, 'checks': checks,
}
a.output.parent.mkdir(parents=True, exist_ok=True)
a.output.write_text(json.dumps(data, indent=2) + '\n')
print(a.output)
raise SystemExit(0 if safe else 1)
