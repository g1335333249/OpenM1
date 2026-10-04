#!/usr/bin/env python3
"""Strict structural and first-migration kernel verification."""
import argparse
import hashlib
import struct
from pathlib import Path
from ota_common import APP_OFFSET, OTA_MAX_SIZE, REFERENCE_SHA256, crc16


def verify(path, reference):
    data = Path(path).read_bytes()
    ref = Path(reference).read_bytes()
    checks = {}
    checks["reference identity"] = hashlib.sha256(ref).hexdigest() == REFERENCE_SHA256
    checks["app offset"] = len(data) > APP_OFFSET + 8 + 16
    if not checks["app offset"]:
        return checks, {}
    size, crc1, crc2 = struct.unpack_from("<IHH", data, APP_OFFSET)
    payload = data[APP_OFFSET + 8:-16]
    checks["payload size"] = len(payload) == size
    checks["crc duplicated"] = crc1 == crc2
    checks["crc16"] = crc16(payload) == crc1
    checks["md5"] = hashlib.md5(data[:-16]).digest() == data[-16:]
    checks["kernel match"] = len(ref) >= APP_OFFSET and data[:APP_OFFSET] == ref[:APP_OFFSET]
    checks["OTA size"] = len(data) <= OTA_MAX_SIZE
    return checks, {"size": len(data), "payload_size": size, "crc16": f"{crc1:04x}", "md5": data[-16:].hex(), "sha256": hashlib.sha256(data).hexdigest(), "kernel_sha256": hashlib.sha256(data[:APP_OFFSET]).hexdigest()}


def main():
    p = argparse.ArgumentParser()
    p.add_argument("ota", type=Path)
    p.add_argument("--reference", type=Path, required=True)
    a = p.parse_args()
    if not a.reference.is_file():
        p.error("REFERENCE OTA REQUIRED FOR FIRST-MIGRATION SAFETY CHECK")
    checks, details = verify(a.ota, a.reference)
    print("OTA Verification\n----------------")
    for name, ok in checks.items():
        print(f"{name}: {'PASS' if ok else 'FAIL'}")
    for name, value in details.items():
        print(f"{name}: {value}")
    safe = bool(checks) and all(checks.values())
    print(f"SAFE TO FLASH: {'YES' if safe else 'NO'}")
    raise SystemExit(0 if safe else 1)


if __name__ == "__main__":
    main()
