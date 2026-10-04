#!/usr/bin/env python3
"""Guard the two fixed, evidence-backed UART TX payloads."""
import re
from pathlib import Path

source = Path('openm1/m1_uart.c').read_text(encoding='utf-8')
http = Path('openm1/recovery_http.c').read_text(encoding='utf-8')
match = re.search(r'static const uint8_t zm1_uart_init_command\[12\]\s*=\s*\{([^}]+)\}', source)
if not match:
    raise SystemExit('FAIL: missing 12-byte zM1 UART init command')
values = bytes(int(value, 16) for value in re.findall(r'0x([0-9a-fA-F]{2})', match.group(1)))
expected = bytes.fromhex('23 02 64 01 00 00 00 00 00 00 00 21')
if values != expected or len(values) != 12:
    raise SystemExit('FAIL: UART init command differs from reference zM1')
sensor = re.search(r'static const uint8_t zm1_sensor_request\[12\]\s*=\s*\{([^}]+)\}', source)
if not sensor:
    raise SystemExit('FAIL: missing 12-byte zM1 sensor request')
sensor_values = bytes(int(value, 16) for value in re.findall(r'0x([0-9a-fA-F]{2})', sensor.group(1)))
if sensor_values != bytes.fromhex('23 01 00 00 00 00 00 00 00 00 00 21') or len(sensor_values) != 12:
    raise SystemExit('FAIL: sensor request differs from reference zM1')
if source.count('MicoUartSend(') != 2 or 'sizeof(zm1_uart_init_command)' not in source or 'sizeof(zm1_sensor_request)' not in source:
    raise SystemExit('FAIL: UART TX must use only the two fixed commands')
if '"/api/uart/init"' not in http or '"/api/uart/sensor-request"' not in http or '"/api/uart/send"' in http:
    raise SystemExit('FAIL: UART HTTP routes are not restricted to fixed commands')
if '23 16 00 10 14 00 03 10 F1 FF FF 21' in source:
    raise SystemExit('FAIL: type 0x18 response must remain disabled')
print('PASS zM1 init command: 12 exact bytes')
print('PASS zM1 sensor request: 12 exact bytes')
print('PASS two fixed MicoUartSend calls; no arbitrary UART TX endpoint')
