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
                'static void network_display_worker(',
                'mico_rtos_create_thread(&network_display_thread,MICO_APPLICATION_PRIORITY',
                'mico_thread_msleep(M1_WIFI_BLINK_INTERVAL_MS)',
                'MicoPwmStart(pwm)', 'MicoPwmStop(pwm)'):
    assert literal in display, literal
for literal in ('M1_DISPLAY_PWM_FREQUENCY_HZ 50000u',
                'M1_DISPLAY_PWM_DUTY_PERCENT 20.0f',
                'M1_WIFI_BLINK_INTERVAL_MS 150u',
                'M1_NETWORK_DISPLAY_WORKER_STACK 2048u'):
    assert literal in header, literal
for forbidden in ('network_blink_timer_handler', 'mico_rtos_init_timer',
                  'mico_rtos_start_timer', 'mico_rtos_stop_timer', 'mico_timer_t',
                  'network_apply_mutex', 'apply_network_output',
                  'm1_display_network_test_tick'):
    assert forbidden not in display and forbidden not in header, forbidden
worker=display.split('static void network_display_worker(',1)[1].split('\n}',1)[0]
assert 'network_pwm_set(' in worker
hal=display.split('static int network_pwm_set(',1)[1].split('\n}',1)[0]
assert hal.index('mico_rtos_unlock_mutex(&display_mutex)') < hal.index('MicoPwmStart(pwm)')
assert hal.index('MicoPwmStop(pwm)') < hal.rindex('mico_rtos_lock_mutex(&display_mutex)')
assert display.count('MicoPwmStart(')==1 and display.count('MicoPwmStop(')==1
assert 'm1_display_network_test_tick' not in Path('openm1/network_health.c').read_text()
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
print('PASS: sole PWM worker, no RTOS timer or mutex-held HAL, PWM5/PWM4 50 kHz/20%, 150 ms blink')
