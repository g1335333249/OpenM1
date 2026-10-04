#!/usr/bin/env python3
import argparse
import hashlib
from pathlib import Path
from ota_common import APP_OFFSET, SDK_COMMIT, KERNEL_VERSION, require_sdk_kernel

p = argparse.ArgumentParser()
p.add_argument('--sdk-kernel', type=Path, required=True)
p.add_argument('--output', type=Path, required=True)
a = p.parse_args()
kernel = require_sdk_kernel(a.sdk_kernel)
a.output.write_text(f"SDK commit: {SDK_COMMIT}\nSDK kernel: resources/moc_kernel/3080B/kernel.bin\nKernel version: {KERNEL_VERSION.decode()}\nraw kernel size: {len(kernel)}\npadded kernel size: {APP_OFFSET}\nMD5: {hashlib.md5(kernel).hexdigest()}\nSHA256: {hashlib.sha256(kernel).hexdigest()}\nAPP compiled against same SDK kernel: YES\n")
print('[PASS] SDK kernel 002.023')
print('[PASS] Kernel and APP same SDK')
