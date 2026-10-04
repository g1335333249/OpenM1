#!/usr/bin/env python3
"""Check reproducible static UART evidence in the unmodified zM1 OTA."""
import argparse
import hashlib
import struct
from pathlib import Path

p = argparse.ArgumentParser()
p.add_argument('--reference', type=Path, required=True)
p.add_argument('--output', type=Path, required=True)
a = p.parse_args()
ota = a.reference.read_bytes()
payload_size = struct.unpack_from('<I', ota, 0x75000)[0]
app = ota[0x75008:-16]
checks = {
    'APP payload length': len(app) == payload_size,
    'user_uart.c string': app.find(b'user_uart.c') == 0x186e9,
    '20-byte UART GET format': app.count(b'%02X', 0x1872e, 0x1880a) >= 20,
    'zM1 sensor JSON fields': b'"temperature"' in app and b'"humidity"' in app and b'"PM25"' in app and b'"formaldehyde"' in app,
    '12-byte startup TX literal': app[0x186cc:0x186d8] == bytes.fromhex('23 02 64 01 00 00 00 00 00 00 00 21'),
    '115200 immediate in UART thread': app[0x0808c1c8-0x08088008:0x0808c1cc-0x08088008] == bytes.fromhex('4f f4 e1 33'),
}
lines = ['zM1 UART protocol evidence', '==========================',
         f'reference SHA256: {hashlib.sha256(ota).hexdigest()}',
         f'APP payload size: {payload_size}',
         'UART: MICO_UART_1; TX GPIO9; RX GPIO10',
         'baud: 115200 (reference disassembly 0x0808c1c8)',
         'data: 8N1; flow control: disabled',
         'frame: 20 bytes; start 0x23 (#); end 0x21 (!); type at [1]',
         'type 0x01: HCHO [2:4] BE /1000, T [5]+floor([6]/10)/10, H [7]+floor([8]/10)/10, PM25 [9:11] BE',
         'checksum: not established by static analysis',
         'startup TX observed in zM1: 23 02 64 01 00 00 00 00 00 00 00 21; OpenM1 v0.3.1+ sends once after UART init',
         'status: PARTIAL', 'hardware sensor validation: NO', '']
lines += [f'{"PASS" if ok else "FAIL"} {name}' for name, ok in checks.items()]
a.output.write_text('\n'.join(lines) + '\n')
print('\n'.join(lines))
raise SystemExit(0 if all(checks.values()) else 1)
