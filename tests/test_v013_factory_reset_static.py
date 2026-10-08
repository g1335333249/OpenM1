from pathlib import Path

button=Path('openm1/button_manager.c').read_text()
header=Path('openm1/button_manager.h').read_text()
sensor=Path('openm1/m1_sensor.c').read_text()
config=Path('openm1/config_store.c').read_text()
http=Path('openm1/recovery_http.c').read_text()
page=Path('openm1/recovery_page.html').read_text()
main=Path('openm1/main.c').read_text()
budget=Path('tools/check_stack_budget.py').read_text()

assert '#define OPENM1_FACTORY_RESET_DRY_RUN 1' in header
assert '!defined(OPENM1_FACTORY_RESET_EXPLICIT_ENABLE)' in header
assert '#if OPENM1_FACTORY_RESET_DRY_RUN' in button
assert 'dry run confirmed; no configuration changed' in button
assert 'button_manager_tick();' in main.split('static void housekeeping_worker',1)[1]
assert main.index('button_manager_init()')<main.index('m1_uart_init()')
assert 'button_manager_note_long_press();' in sensor
assert 'for (j=2;j<19 && frame[j]==0;j++)' in sensor
assert sensor.index('case 0x04:')<sensor.index('case 0x0f:')
assert 'button_manager_note_short_press();' in sensor
assert 'if (brightness_callback) brightness_callback(frame[2]);' in sensor
assert 'BUTTON_MIN_EVENT_GAP_MS 4000u' in header
assert 'BUTTON_CONFIRMATION_WINDOW_MS 15000u' in header
assert '(uint32_t)(now-first_long_ms)' in button
assert 'if (recovery_ota_busy())' in button
assert 'result=config_store_factory_reset();' in button
assert button.index('if (result!=kNoErr)')<button.index('MicoSystemReboot();')
assert 'config_store_defaults(saved);' in config
assert 'else *saved=config_cache;' in config
assert 'MicoFlashErase' not in config
assert '"/api/button/status"' in http
assert 'button_manager_status_json(json,sizeof(json))' in http
assert 'POST /api/button' not in http
assert '/api/button/status' in page and '实体按钮诊断' in page
assert 'button_manager_tick' in budget and 'config_store_factory_reset' in budget
assert 'OPENM1_CONFIG_VERSION 3u' in Path('openm1/config_store.h').read_text()
print('V013_FACTORY_RESET_STATIC_PASS: dry-run, UART validation, OTA gate, safe context, read-only API')
