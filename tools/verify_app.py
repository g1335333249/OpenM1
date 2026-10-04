#!/usr/bin/env python3
"""Inspect linked MOC user header and startup symbols with GNU Arm binutils."""
import argparse
import re
import struct
import subprocess
from pathlib import Path

p = argparse.ArgumentParser()
p.add_argument('--elf', type=Path, required=True)
p.add_argument('--nm', required=True)
p.add_argument('--objdump', required=True)
p.add_argument('--symbols', type=Path, required=True)
p.add_argument('--report', type=Path, required=True)
a = p.parse_args()
raw_nm = subprocess.check_output([a.nm, '-S', '-n', str(a.elf)], text=True)
raw_sections = subprocess.check_output([a.objdump, '-h', str(a.elf)], text=True)
raw_header = subprocess.check_output([a.objdump, '-s', '-j', '.vectors', str(a.elf)], text=True)
raw_disassembly = subprocess.check_output([a.objdump, '-d', str(a.elf)], text=True)
symbols = {}
for line in raw_nm.splitlines():
    m = re.match(r'^([0-9a-fA-F]+)\s+([0-9a-fA-F]+)\s+\w\s+(\S+)$', line)
    if m:
        symbols[m.group(3)] = (int(m.group(1), 16), int(m.group(2), 16))
required = ('user_handler', 'moc_app_main', 'moc_adapter', 'main')
checks = {name: name in symbols and bool(re.search(r'<'+name+r'>:', raw_disassembly)) if name != 'user_handler' else name in symbols for name in required}
section = re.search(r'^\s*\d+\s+\.vectors\s+[0-9a-fA-F]+\s+([0-9a-fA-F]+)', raw_sections, re.M)
if not section or not checks['user_handler']:
    raise SystemExit('missing .vectors or user_handler')
base = int(section.group(1), 16)
bytes_by_address = {}
for line in raw_header.splitlines():
    m = re.match(r'^\s*([0-9a-fA-F]{7,8})\s+((?:[0-9a-fA-F]{8}\s*){1,4})', line)
    if m:
        at = int(m.group(1), 16)
        chunk = bytes.fromhex(''.join(m.group(2).split()))
        bytes_by_address.update((at+i, b) for i,b in enumerate(chunk))
address, size = symbols['user_handler']
header = bytes(bytes_by_address.get(address+i, 0) for i in range(size))
if size < 48 or not all(address+i in bytes_by_address for i in range(size)):
    raise SystemExit('user_handler bytes incomplete')
magic, stack, interface = struct.unpack_from('<III', header, 8)
entry = struct.unpack_from('<I', header, 44)[0]
checks.update({'magic 0xC89346': magic == 0x00C89346,
               'interface version 3': interface == 3,
               'app_stack_size 4096': stack == 4096,
               'stdio_break_in 1': header[-4] == 1,
               'user_app_in moc_app_main': entry & ~1 == symbols.get('moc_app_main', (None,))[0],
               'header at APP start': address == base})
a.symbols.write_text(raw_nm)
a.report.write_text('\n'.join(f"{'PASS' if ok else 'FAIL'} {name}" for name,ok in checks.items()) + '\n' +
                    '\n'.join(f'{name} address: 0x{symbols[name][0]:08x}' for name in required if name in symbols) + '\n' +
                    f'magic_num: 0x{magic:08X}\ninterface_version: {interface}\napp_stack_size: {stack}\nstdio_break_in: {header[-4]}\n')
for name, ok in checks.items():
    print(f"[{'PASS' if ok else 'FAIL'}] {name}")
print(f"MOC_APP_HEADER_VALID={'YES' if all(checks[k] for k in checks if k not in required) else 'NO'}")
print(f"STARTUP_SYMBOLS_VALID={'YES' if all(checks[k] for k in required) else 'NO'}")
raise SystemExit(0 if all(checks.values()) else 1)
