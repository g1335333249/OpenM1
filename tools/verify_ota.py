#!/usr/bin/env python3
"""Verify SDK-kernel MOC OTA without any reference-kernel fallback."""
import argparse
import hashlib
import struct
from pathlib import Path
from ota_common import APP_OFFSET, OTA_MAX_SIZE, crc16, require_sdk_kernel


def verify(path, sdk_kernel, app):
    data = Path(path).read_bytes()
    kernel = require_sdk_kernel(sdk_kernel)
    app_bytes = Path(app).read_bytes()
    checks = {
        "SDK kernel 002.023": True,
        "SDK kernel match": data[:APP_OFFSET] == kernel.ljust(APP_OFFSET, b"\xff"),
        "OTA size": APP_OFFSET + 8 + 16 < len(data) <= OTA_MAX_SIZE,
    }
    if not checks["OTA size"]:
        return checks, {}
    size, crc1, crc2 = struct.unpack_from("<IHH", data, APP_OFFSET)
    payload = data[APP_OFFSET + 8:-16]
    checks.update({
        "APP payload match": payload == app_bytes[8:],
        "payload size": len(payload) == size == len(app_bytes) - 8,
        "CRC duplicated": crc1 == crc2,
        "APP CRC16": crc16(payload) == crc1,
        "OTA MD5": hashlib.md5(data[:-16]).digest() == data[-16:],
    })
    return checks, {"size": len(data), "app_size": len(app_bytes), "payload_size": size,
                    "crc16": f"{crc1:04x}", "md5": data[-16:].hex(),
                    "sha256": hashlib.sha256(data).hexdigest(),
                    "kernel_sha256": hashlib.sha256(kernel).hexdigest()}


def main():
    p = argparse.ArgumentParser()
    p.add_argument("ota", type=Path)
    p.add_argument("--sdk-kernel", type=Path, required=True)
    p.add_argument("--app", type=Path, required=True)
    a = p.parse_args()
    checks, details = verify(a.ota, a.sdk_kernel, a.app)
    for name, ok in checks.items():
        print(f"[{'PASS' if ok else 'FAIL'}] {name}")
    for name, value in details.items():
        print(f"{name}: {value}")
    valid = bool(checks) and all(checks.values())
    print(f"OTA_FORMAT_VALID={'YES' if valid else 'NO'}")
    raise SystemExit(0 if valid else 1)


if __name__ == "__main__":
    main()
