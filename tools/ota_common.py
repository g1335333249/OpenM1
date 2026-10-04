"""MXCHIP MOC OTA layout shared by packaging and verification."""
import hashlib
import struct
from pathlib import Path

APP_OFFSET = 0x75000
OTA_MAX_SIZE = 0xB5000
SDK_COMMIT = "9b09de78164940ff3876d2053f8e7dd42ca2b8ba"
REFERENCE_SHA256 = "20c5e6ae1692e3e047063b637b137635ab9e57711dba7c27ef0eb6cf1390887e"
KERNEL_VERSION = b"3080B002.023"


def require_reference(path):
    ref = Path(path).read_bytes()
    if hashlib.sha256(ref).hexdigest() != REFERENCE_SHA256:
        raise ValueError("reference OTA identity mismatch")
    return ref


def require_sdk_kernel(path):
    kernel = Path(path).read_bytes()
    if not kernel or len(kernel) > APP_OFFSET or KERNEL_VERSION not in kernel:
        raise ValueError("SDK kernel is absent, oversized, or not 3080B002.023")
    return kernel


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
