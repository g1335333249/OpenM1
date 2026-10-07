#!/usr/bin/env python3
"""Check the MOC header and the initialized ELF runtime app stack variable."""
import argparse
import re
import struct
import subprocess
from pathlib import Path

EXPECTED_STACK = 4096
SDK_WEAK_DEFAULT = 1500


def inspect_runtime(elf: Path, nm: str, objdump: str):
    symbols = subprocess.check_output([nm, "-S", str(elf)], text=True)
    matches = re.findall(r"^([0-9a-fA-F]+)\s+([0-9a-fA-F]+)\s+([A-Za-z])\s+app_stack_size$",
                         symbols, re.M)
    if len(matches) != 1:
        raise ValueError(f"expected one app_stack_size symbol, found {len(matches)}")
    address, size, kind = matches[0]
    address, size = int(address, 16), int(size, 16)
    if size != 4:
        raise ValueError(f"app_stack_size symbol size is {size}, expected 4")
    sections = subprocess.check_output([objdump, "-h", str(elf)], text=True)
    data = re.search(r"^\s*\d+\s+\.data\s+([0-9a-fA-F]+)\s+([0-9a-fA-F]+)\s+"
                     r"[0-9a-fA-F]+\s+([0-9a-fA-F]+)", sections, re.M)
    if not data:
        raise ValueError("missing initialized .data section")
    section_size, vma, file_offset = (int(x, 16) for x in data.groups())
    if not vma <= address <= vma + section_size - size:
        raise ValueError("app_stack_size is outside initialized .data")
    offset = file_offset + address - vma
    raw = elf.read_bytes()
    if offset + size > len(raw):
        raise ValueError("app_stack_size file bytes are incomplete")
    return struct.unpack_from("<I", raw, offset)[0], kind, offset


def inspect_header(report: Path):
    content = report.read_text()
    if "PASS app_stack_size 4096" not in content.splitlines():
        raise ValueError("MOC header stack check did not pass")
    match = re.search(r"^app_stack_size:\s*(\d+)\s*$", content, re.M)
    if not match:
        raise ValueError("MOC header stack value missing")
    return int(match.group(1))


def check(header: int, runtime: int, symbol_kind: str):
    return header == EXPECTED_STACK and runtime == EXPECTED_STACK and symbol_kind == "D"


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--elf", type=Path, required=True)
    p.add_argument("--nm", required=True)
    p.add_argument("--objdump", required=True)
    p.add_argument("--app-header-report", type=Path, required=True)
    p.add_argument("--output", type=Path, required=True)
    a = p.parse_args()
    header = inspect_header(a.app_header_report)
    runtime, kind, _ = inspect_runtime(a.elf, a.nm, a.objdump)
    results = [
        (f"MOC header app stack = {EXPECTED_STACK}", header == EXPECTED_STACK),
        (f"ELF runtime app_thread stack = {EXPECTED_STACK}", runtime == EXPECTED_STACK),
        ("runtime app_stack_size is an application strong .data symbol", kind == "D"),
        ("Header/runtime app stack match", header == runtime == EXPECTED_STACK),
    ]
    lines = [f"[{'PASS' if ok else 'FAIL'}] {label}" for label, ok in results]
    lines += [f"MOC_HEADER_APP_STACK={header}", f"ELF_RUNTIME_APP_STACK={runtime}",
              f"APP_THREAD_STACK_MATCH={'YES' if check(header, runtime, kind) else 'NO'}"]
    a.output.write_text("\n".join(lines) + "\n")
    print("\n".join(lines))
    if not check(header, runtime, kind):
        raise SystemExit(f"[FAIL] ELF runtime app_stack_size expected {EXPECTED_STACK}, got {runtime} (symbol {kind})")


if __name__ == "__main__":
    main()
