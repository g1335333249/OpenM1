from pathlib import Path

main=Path('openm1/main.c').read_text()
sdk_config=Path('mico-os/MiCO/core/mico_config.c').read_text()
sdk_main=Path('mico-os/MiCO/moc_main.c').read_text()
mqtt=Path('openm1/mqtt_manager.c').read_text()
build=Path('scripts/build_recovery.sh').read_text()

assert 'uint32_t app_stack_size = 4096u;' in main
assert 'app_thread stack configured = %lu' in main
assert 'MICO_WEAK app_stack_size = 1500' in sdk_config
assert 'extern uint32_t app_stack_size;' in sdk_main
assert 'app_stack_size, 0 );' in sdk_main
assert 'mico_rtos_delete_thread( NULL )' in sdk_main
assert main.rstrip().endswith('return 0;\n}')
assert 'status.last_subscribe_result=MQTT_SUCCESS;' in mqtt
assert build.index('tools/verify_app.py')<build.index('tools/verify_app_stack.py')
assert build.index('tools/verify_app_stack.py')<build.index('tools/generate_manifest.py')
assert '--app-stack-report dist/app-stack-report.txt' in build
assert 'APP_THREAD_STACK_MATCH' in Path('tools/verify_app_stack.py').read_text()
print('V011_STACK_STATIC_PASS')
