#!/usr/bin/env python3
"""Check evidence-backed UART TX constraints for v0.5.0."""
import re
from pathlib import Path

uart = Path('openm1/m1_uart.c').read_text(encoding='utf-8')
display = Path('openm1/m1_display.c').read_text(encoding='utf-8')
http = Path('openm1/recovery_http.c').read_text(encoding='utf-8')
sensor = re.search(r'static const uint8_t zm1_sensor_request\[12\]\s*=\s*\{([^}]+)\}', uart)
if not sensor:
    raise SystemExit('FAIL: sensor request missing')
values = bytes(int(v, 16) for v in re.findall(r'0x([0-9a-fA-F]{2})', sensor.group(1)))
if values != bytes.fromhex('23 01 00 00 00 00 00 00 00 00 00 21'):
    raise SystemExit('FAIL: sensor request differs from hardware-verified frame')
if '#define M1_SENSOR_POLL_INTERVAL_MS 2000u' not in Path('openm1/m1_uart.h').read_text():
    raise SystemExit('FAIL: sensor polling must be 2000 ms')
if uart.count('MicoUartSend(') != 2 or 'valid_display_frame(frame)' not in uart:
    raise SystemExit('FAIL: UART TX must be limited to sensor request and validated display frames')
for needle in ('level<1 || level>4', 'level*25u', 'out[3]=on?1u:0u',
               'state.last_nonzero_brightness', 'DISPLAY_ECHO_SUPPRESS_MS 500u'):
    if needle not in display:
        raise SystemExit(f'FAIL: display brightness safety check missing: {needle}')
for route in ('/api/uart/init', '/api/uart/sensor-request', '/api/display/brightness'):
    if f'"{route}"' not in http:
        raise SystemExit(f'FAIL: route missing: {route}')
if '/api/uart/send' in http or '23 16 00 10 14 00 03 10 F1 FF FF 21' in uart:
    raise SystemExit('FAIL: arbitrary UART TX or type 0x18 response detected')
if 'return m1_display_sync();' not in uart:
    raise SystemExit('FAIL: deprecated init route must resync saved display state')
print('PASS: 2 s sensor polling, brightness 0..4, preserved level on screen off, guarded UART TX')
