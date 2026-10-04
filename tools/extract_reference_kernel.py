#!/usr/bin/env python3
import argparse
from pathlib import Path
from ota_common import APP_OFFSET, compare_kernel

p = argparse.ArgumentParser()
p.add_argument("--reference", type=Path, required=True)
p.add_argument("--sdk-kernel", type=Path, required=True)
p.add_argument("--output", type=Path, required=True)
p.add_argument("--report", type=Path, default=Path("build/kernel_compare.txt"))
a = p.parse_args()
if not a.reference.is_file():
    p.error("REFERENCE OTA REQUIRED FOR FIRST-MIGRATION SAFETY CHECK")
data = a.reference.read_bytes()
if len(data) < APP_OFFSET:
    p.error("reference OTA is truncated")
a.output.parent.mkdir(parents=True, exist_ok=True)
a.output.write_bytes(data[:APP_OFFSET])
compare_kernel(a.reference, a.sdk_kernel, a.report)
