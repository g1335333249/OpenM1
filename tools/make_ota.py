#!/usr/bin/env python3
"""Python 3 equivalent of MXCHIP gen_moc_bin_output_file.py."""
import argparse
import hashlib
from pathlib import Path
from ota_common import APP_OFFSET, OTA_MAX_SIZE, app_record, compare_kernel


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--app", type=Path, required=True, help="compiler .bin, including first 8 header bytes")
    p.add_argument("--reference", type=Path, required=True)
    p.add_argument("--sdk-kernel", type=Path, required=True)
    p.add_argument("--output", type=Path, required=True)
    p.add_argument("--usr-output", type=Path)
    p.add_argument("--kernel-report", type=Path, default=Path("build/kernel_compare.txt"))
    a = p.parse_args()
    if not a.reference.is_file():
        p.error("REFERENCE OTA REQUIRED FOR FIRST-MIGRATION SAFETY CHECK")
    ref = a.reference.read_bytes()
    if len(ref) < APP_OFFSET:
        p.error("reference OTA kernel region is truncated")
    compare_kernel(a.reference, a.sdk_kernel, a.kernel_report)
    record = app_record(a.app.read_bytes())
    body = ref[:APP_OFFSET] + record
    ota = body + hashlib.md5(body).digest()
    if len(ota) > OTA_MAX_SIZE:
        p.error(f"OTA size {len(ota)} exceeds 0x{OTA_MAX_SIZE:x}")
    a.output.parent.mkdir(parents=True, exist_ok=True)
    a.output.write_bytes(ota)
    if a.usr_output:
        a.usr_output.parent.mkdir(parents=True, exist_ok=True)
        a.usr_output.write_bytes(record)
    print(f"OTA: {a.output} ({len(ota)} bytes)")


if __name__ == "__main__":
    main()
