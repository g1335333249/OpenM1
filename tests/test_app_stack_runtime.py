#!/usr/bin/env python3
"""The ELF runtime value must be checked independently of the MOC header."""
import argparse
import subprocess
import struct
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from verify_app_stack import EXPECTED_STACK, SDK_WEAK_DEFAULT, check, inspect_header, inspect_runtime

p = argparse.ArgumentParser()
p.add_argument("--elf", type=Path, required=True)
p.add_argument("--nm", required=True)
p.add_argument("--objdump", required=True)
p.add_argument("--app-header-report", type=Path, required=True)
a = p.parse_args()
header = inspect_header(a.app_header_report)
runtime, kind, offset = inspect_runtime(a.elf, a.nm, a.objdump)
assert header == EXPECTED_STACK and runtime == EXPECTED_STACK and kind == "D"
assert check(header, runtime, kind)

# Keep the verified header untouched while restoring the former 1500-byte
# .data initializer. This exact historical mismatch must fail CI.
with tempfile.TemporaryDirectory() as temp:
    bad_elf = Path(temp) / "app-stack-1500.elf"
    damaged = bytearray(a.elf.read_bytes())
    struct.pack_into("<I", damaged, offset, SDK_WEAK_DEFAULT)
    bad_elf.write_bytes(damaged)
    bad_runtime, bad_kind, _ = inspect_runtime(bad_elf, a.nm, a.objdump)
    assert bad_runtime == SDK_WEAK_DEFAULT and bad_kind == "D"
    assert not check(header, bad_runtime, bad_kind)
    rejected = subprocess.run([
        sys.executable, str(Path(__file__).resolve().parents[1] / "tools" / "verify_app_stack.py"),
        "--elf", str(bad_elf), "--nm", a.nm, "--objdump", a.objdump,
        "--app-header-report", str(a.app_header_report),
        "--output", str(Path(temp) / "rejected-report.txt")],
        capture_output=True, text=True, check=False)
    assert rejected.returncode != 0
    assert "expected 4096, got 1500" in rejected.stdout + rejected.stderr

assert not check(EXPECTED_STACK, SDK_WEAK_DEFAULT, "V")
print("APP_STACK_RUNTIME_TEST_PASS: header 4096/runtime 1500 is rejected")
