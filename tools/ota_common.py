"""MXCHIP MOC OTA layout shared by packaging and verification."""
import hashlib
import struct
from pathlib import Path

APP_OFFSET = 0x75000
OTA_MAX_SIZE = 0xB5000
SDK_COMMIT = "9b09de78164940ff3876d2053f8e7dd42ca2b8ba"
REFERENCE_SHA256 = "20c5e6ae1692e3e047063b637b137635ab9e57711dba7c27ef0eb6cf1390887e"


def require_reference(path):
    ref = Path(path).read_bytes()
    if hashlib.sha256(ref).hexdigest() != REFERENCE_SHA256:
        raise ValueError("reference OTA identity mismatch; expected supplied zM1 image")
    return ref


def crc16(data):
    wcrc = 0
    for value in data:
        c = value
        for _ in range(8):
            treat = c & 0x80
            c <<= 1
            bcrc = (wcrc >> 8) & 0x80
            wcrc = (wcrc << 1) & 0xffff
            if treat != bcrc:
                wcrc ^= 0x1021
    return wcrc


def app_record(compiler_bin):
    if len(compiler_bin) <= 8:
        raise ValueError("compiler APP binary is too short")
    payload = compiler_bin[8:]
    crc = crc16(payload)
    return struct.pack("<IHH", len(payload), crc, crc) + payload


def compare_kernel(reference, sdk, report):
    ref = require_reference(reference)[:APP_OFFSET]
    sdk_bytes = Path(sdk).read_bytes()
    padded = sdk_bytes.ljust(APP_OFFSET, b"\xff")
    first = next((i for i, (a, b) in enumerate(zip(ref, padded)) if a != b), None)
    if first is None and len(ref) != len(padded):
        first = min(len(ref), len(padded))
    lines = [
        f"reference kernel SHA256: {hashlib.sha256(ref).hexdigest()}",
        f"SDK kernel SHA256: {hashlib.sha256(sdk_bytes).hexdigest()}",
        f"reference kernel size: {len(ref)}",
        f"SDK kernel size: {len(sdk_bytes)}",
        f"identical after 0xFF padding: {first is None}",
        f"first difference address: {hex(first) if first is not None else 'none'}",
    ]
    Path(report).parent.mkdir(parents=True, exist_ok=True)
    Path(report).write_text("\n".join(lines) + "\n")
    return first is None
