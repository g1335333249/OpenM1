#!/usr/bin/env python3
"""Python 3 equivalent of MXCHIP gen_moc_bin_output_file.py."""
import argparse
import hashlib
from pathlib import Path
from ota_common import APP_OFFSET, OTA_MAX_SIZE, app_record, require_sdk_kernel


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--app", type=Path, required=True)
    p.add_argument("--sdk-kernel", type=Path, required=True)
    p.add_argument("--output", type=Path, required=True)
    a = p.parse_args()
    kernel = require_sdk_kernel(a.sdk_kernel)
    body = kernel.ljust(APP_OFFSET, b"\xff") + app_record(a.app.read_bytes())
    ota = body + hashlib.md5(body).digest()
    if len(ota) > OTA_MAX_SIZE:
        p.error(f"OTA size {len(ota)} exceeds 0x{OTA_MAX_SIZE:x}")
    a.output.parent.mkdir(parents=True, exist_ok=True)
    a.output.write_bytes(ota)
    print(f"OTA: {a.output} ({len(ota)} bytes)")


if __name__ == "__main__":
    main()
