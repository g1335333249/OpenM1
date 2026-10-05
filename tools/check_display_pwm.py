#!/usr/bin/env python3
"""Guard the reference-backed PWM channels and fixed diagnostic API."""
from pathlib import Path

display = Path('openm1/m1_display.c').read_text(encoding='utf-8')
header = Path('openm1/m1_display.h').read_text(encoding='utf-8')
http = Path('openm1/recovery_http.c').read_text(encoding='utf-8')
board = Path('mico-os/board/MK3080B/mico_board.c').read_text(encoding='utf-8')
for literal in ('#define M1_WIFI_ICON_PWM MICO_PWM_5',
                '#define M1_RED_X_PWM MICO_PWM_4',
                'MicoPwmInitialize(M1_WIFI_ICON_PWM',
                'MicoPwmInitialize(M1_RED_X_PWM',
                'MicoPwmStart(M1_WIFI_ICON_PWM)',
                'MicoPwmStop(M1_WIFI_ICON_PWM)',
                'MicoPwmStart(M1_RED_X_PWM)',
                'MicoPwmStop(M1_RED_X_PWM)',
                'mico_rtos_init_timer(&network_blink_timer,M1_WIFI_BLINK_INTERVAL_MS'):
    assert literal in display, literal
for literal in ('M1_DISPLAY_PWM_FREQUENCY_HZ 50000u',
                'M1_DISPLAY_PWM_DUTY_PERCENT 20.0f',
                'M1_WIFI_BLINK_INTERVAL_MS 150u'):
    assert literal in header, literal
assert '[MICO_PWM_4] = {.pin = MICO_GPIO_13,' in board
assert '[MICO_PWM_5] = {.pin = MICO_GPIO_14,' in board
assert 'MicoUartSend' not in display
for forbidden in ('MicoGpioInitialize', 'MicoGpioOutputHigh', 'MicoGpioOutputLow',
                  'MicoSysLed(', 'MicoRfLed('):
    assert forbidden not in display, forbidden
assert '"/api/display/network-test"' in http
assert 'json_min_parse(body,length,field,1)' in http
assert 'strcmp(field[0].key,"mode")' in http
for mode in ('blink', 'online', 'no_internet', 'auto'):
    assert f'strcmp(field[0].value,"{mode}")' in http, mode
assert '/api/display/pwm' not in http and '/api/uart/send' not in http
print('PASS: PWM5/PWM4, 50 kHz/20%, 150 ms blink, fixed test modes; no GPIO or icon UART TX')
