#!/usr/bin/env python3
from pathlib import Path

root = Path(__file__).resolve().parents[1]
display = (root / 'openm1/m1_display.c').read_text()
header = (root / 'openm1/m1_display.h').read_text()
main = (root / 'openm1/main.c').read_text()
manifest = (root / 'tools/generate_manifest.py').read_text()

step = display[display.index('static void network_display_step(void)'):display.index('static void network_display_worker(')]
set_pwm = display[display.index('static int network_pwm_set('):display.index('static void network_display_step(')]
assert 'if (on && !screen_on) return -1;' in set_pwm
assert 'screen_on=state.screen_on;' in step
assert 'if (!screen_on)' in step
off = step[step.index('if (!screen_on)'):step.index('if (target!=previous)')]
assert 'network_pwm_set(M1_WIFI_ICON_PWM,false,false)' in off
assert 'network_pwm_set(M1_RED_X_PWM,false,false)' in off
assert 'state.network_applied_target=(m1_net_display_state_t)255;' in off
assert 'state.network_test_active=false' in step
assert 'MicoPwmStart' not in step and 'MicoPwmStop' not in step

worker = display[display.index('static void network_display_worker('):display.index('int m1_display_build_brightness_frame(')]
assert 'for (;;) network_display_step();' in worker
for name, next_name in (('m1_display_set_brightness', 'm1_display_screen_on'),
                        ('m1_display_handle_brightness_event', 'm1_display_set_network_state')):
    body = display[display.index(f'{name}('):display.index(f'{next_name}(')]
    assert 'MicoPwmStart' not in body and 'MicoPwmStop' not in body
assert '#define M1_WIFI_ICON_PWM MICO_PWM_5' in display
assert '#define M1_RED_X_PWM MICO_PWM_4' in display
assert '#define M1_NETWORK_DISPLAY_WORKER_STACK 2048u' in header
assert 'OpenM1 v0.6.21' in main and "'version':'0.6.21'" in manifest
print('V021_DISPLAY_POWER_STATIC_PASS')
