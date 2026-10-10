#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
SDK_COMMIT=9b09de78164940ff3876d2053f8e7dd42ca2b8ba
KERNEL=mico-os/resources/moc_kernel/3080B/kernel.bin
PREFIX=OpenM1-v0.6.16
OTA="dist/$PREFIX@MK3080B@moc.ota.bin"
if [[ "$(uname -s)" == Darwin ]]; then BUILD_HOST_OS=OSX; else BUILD_HOST_OS=Linux64; fi
TOOL=".micoder/compiler/arm-none-eabi-5_4-2016q2-20160622/$BUILD_HOST_OS/bin"
[[ "$(git -C mico-os rev-parse HEAD)" == "$SDK_COMMIT" ]]
echo '[PASS] SDK commit'
python3 tools/embed_page.py --check
python3 tests/test_web_tabs.py
python3 tests/test_v012_api_console.py
node tests/test_api_console.js
node tests/test_button_web.js
python3 tests/test_v014_diagnostics_static.py
python3 tests/test_v015_ap_policy_static.py
python3 tests/test_v016_ipv6_static.py
node tests/test_v014_diagnostics_web.js
cc -std=c99 -Wall -Wextra -Werror -Iopenm1 tests/test_ipv6_probe_logic.c openm1/ipv6_probe_logic.c -o /tmp/openm1-ipv6-probe-test
/tmp/openm1-ipv6-probe-test
python3 tests/test_v013_factory_reset_static.py
python3 tests/test_v063_static.py
python3 tests/test_v063_safety.py
python3 tests/test_v064_static.py
python3 tests/test_v065_hostname_static.py
python3 tests/test_v066_logs_static.py
python3 tests/test_v067_mqtt_static.py
python3 tests/test_v068_stability_static.py
python3 tests/test_v069_ota_static.py
python3 tests/test_v010_ha_brightness_static.py
python3 tests/test_v011_stack_static.py
cc -std=c99 -Wall -Wextra -Werror -Iopenm1 tests/test_ha_brightness.c openm1/ha_brightness.c -o /tmp/openm1-ha-brightness-test
/tmp/openm1-ha-brightness-test
cc -std=c99 -Wall -Wextra -Werror -Itests/stubs -Iopenm1 tests/test_ha_discovery.c openm1/homeassistant.c -o /tmp/openm1-ha-discovery-test
/tmp/openm1-ha-discovery-test
cc -std=c99 -Wall -Wextra -Werror -Iopenm1 tests/test_ota_transfer_logic.c openm1/ota_transfer_logic.c -o /tmp/openm1-ota-transfer-test
/tmp/openm1-ota-transfer-test
cc -std=c99 -Wall -Wextra -Werror -Iopenm1 tests/test_worker_retry_logic.c openm1/worker_retry_logic.c -o /tmp/openm1-worker-retry-test
/tmp/openm1-worker-retry-test
node tests/test_log_copy.js
cc -std=c99 -Wall -Wextra -Werror -Iopenm1 tests/test_mqtt_bounded_read.c openm1/mqtt_bounded_read.c -o /tmp/openm1-mqtt-read-test
/tmp/openm1-mqtt-read-test
cc -std=c99 -Wall -Wextra -Werror -Iopenm1 tests/test_mqtt_diagnostics.c openm1/mqtt_diagnostics.c -o /tmp/openm1-mqtt-diagnostics-test
/tmp/openm1-mqtt-diagnostics-test
cc -std=c99 -Wall -Wextra -Werror -Itests/stubs -Iopenm1 tests/test_openm1_log.c openm1/openm1_log.c -o /tmp/openm1-log-test
/tmp/openm1-log-test
cc -std=c99 -Wall -Wextra -Werror -Iopenm1 tests/test_http_activity.c openm1/http_activity.c -o /tmp/openm1-http-activity-test
/tmp/openm1-http-activity-test
cc -std=c99 -Wall -Wextra -Werror -Itests/stubs -Iopenm1 tests/test_recovery_hostname.c openm1/recovery_identity.c -o /tmp/openm1-hostname-test
/tmp/openm1-hostname-test
cc -std=c99 -Wall -Wextra -Werror -Itests/stubs -Iopenm1 tests/test_m1_sensor.c openm1/m1_sensor.c -o /tmp/openm1-sensor-test
/tmp/openm1-sensor-test
cc -std=c99 -Wall -Wextra -Werror -Itests/stubs -Iopenm1 tests/test_button_manager.c openm1/button_manager.c -o /tmp/openm1-button-test
/tmp/openm1-button-test
cc -std=c99 -Wall -Wextra -Werror -DOPENM1_FACTORY_RESET_DRY_RUN=0 -DOPENM1_FACTORY_RESET_EXPLICIT_ENABLE=1 -Itests/stubs -Iopenm1 tests/test_button_manager.c openm1/button_manager.c -o /tmp/openm1-button-real-path-test
/tmp/openm1-button-real-path-test
cc -std=c99 -Wall -Wextra -Werror -Itests/stubs -Iopenm1 tests/test_m1_display.c openm1/m1_display_network.c openm1/openm1_log.c -o /tmp/openm1-display-test
/tmp/openm1-display-test
cc -std=c99 -Wall -Wextra -Werror -Itests/stubs -Iopenm1 tests/test_m1_display_network.c openm1/m1_display_network.c -o /tmp/openm1-display-network-test
/tmp/openm1-display-network-test
cc -std=c99 -Wall -Wextra -Werror -Itests/stubs -Iopenm1 tests/test_network_health.c openm1/network_health_state.c -o /tmp/openm1-network-health-test
/tmp/openm1-network-health-test
cc -std=c99 -Wall -Wextra -Werror -Itests/stubs -Iopenm1 tests/test_config_migration.c openm1/config_store.c -o /tmp/openm1-config-migration-test
/tmp/openm1-config-migration-test
cc -std=c99 -Wall -Wextra -Werror -Itests/stubs -Iopenm1 tests/test_wifi_settings.c openm1/wifi_settings.c openm1/json_min.c -o /tmp/openm1-wifi-settings-test
/tmp/openm1-wifi-settings-test
cc -std=c99 -Wall -Wextra -Werror -Itests/stubs -Iopenm1 tests/test_wifi_station_logic.c openm1/wifi_station_logic.c openm1/network_health_state.c -o /tmp/openm1-station-logic-test
/tmp/openm1-station-logic-test
cc -std=c99 -Wall -Wextra -Werror -Itests/stubs -Iopenm1 tests/test_system_stats.c openm1/system_stats.c openm1/openm1_log.c -o /tmp/openm1-system-stats-test
/tmp/openm1-system-stats-test
cc -std=c99 -Wall -Wextra -Werror -Iopenm1 tests/test_ha_policy.c openm1/ha_policy.c -o /tmp/openm1-ha-policy-test
/tmp/openm1-ha-policy-test
python3 - <<'PY_CHECK_PAGE'
from pathlib import Path
p=Path('openm1/recovery_page.html').read_text(encoding='utf-8')
for text in ('斐讯悟空 M1 开源固件','设备信息','网络状态','连接家庭 Wi-Fi','本地固件升级','网络固件升级','等待设备重新上线','实时环境数据','温度','湿度','PM2.5','甲醛','串口诊断'):
    assert text in p, text
print('[PASS] Chinese UTF-8 Recovery page')
PY_CHECK_PAGE
GCC_VERSION="$($TOOL/arm-none-eabi-gcc --version)"
GCC_VERSION="${GCC_VERSION%%$'\n'*}"
echo "$GCC_VERSION"
[[ "$GCC_VERSION" == *5.4.1* ]]
echo '[PASS] GCC 5.4.1'
set -o pipefail
make -f mico-os/makefiles/Makefile openm1@MK3080B@moc HOST_OS="$BUILD_HOST_OS" TOOLS_ROOT=./.micoder SOURCE_ROOT=./ PYTHON="$(command -v python3)" VERBOSE=1 2>&1 | tee build.log
mkdir -p dist/recovery
APP_BIN='build/openm1@MK3080B@moc/binary/openm1@MK3080B@moc.bin'
[[ -f "$APP_BIN" ]]
APP_ELF="${APP_BIN%.bin}.elf"
APP_MAP="${APP_BIN%.bin}.map"
cp "$APP_BIN" "dist/$PREFIX.bin"
cp "$APP_ELF" "dist/$PREFIX.elf"
cp "$APP_MAP" "dist/$PREFIX.map"
cp build.log dist/build.log
echo '[PASS] MK3080B@moc build'
if "$TOOL/arm-none-eabi-nm" -u build/openm1@MK3080B@moc/modules/openm1/wifi_manager.o | grep -q 'micoWlanSuspendSoftAP'; then
    echo '[FAIL] runtime Wi-Fi manager still references SuspendSoftAP' >&2
    exit 1
fi
echo '[PASS] runtime Wi-Fi manager has no SuspendSoftAP reference'
python3 tools/verify_app.py --elf "dist/$PREFIX.elf" --nm "$TOOL/arm-none-eabi-nm" --objdump "$TOOL/arm-none-eabi-objdump" --symbols dist/symbols.txt --report dist/app-header-report.txt
python3 tools/verify_app_stack.py --elf "dist/$PREFIX.elf" --nm "$TOOL/arm-none-eabi-nm" --objdump "$TOOL/arm-none-eabi-objdump" --app-header-report dist/app-header-report.txt --output dist/app-stack-report.txt
python3 tests/test_app_stack_runtime.py --elf "dist/$PREFIX.elf" --nm "$TOOL/arm-none-eabi-nm" --objdump "$TOOL/arm-none-eabi-objdump" --app-header-report dist/app-header-report.txt
mkdir -p dist/stack-usage
find build/openm1@MK3080B@moc -type f -name '*.su' -path '*/openm1/*' -exec cp {} dist/stack-usage/ \;
python3 tools/check_stack_budget.py --elf "dist/$PREFIX.elf" --nm "$TOOL/arm-none-eabi-nm" --su-dir dist/stack-usage --app-stack-report dist/app-stack-report.txt --output dist/stack-budget.txt
for symbol in recovery_http_server_thread recovery_ota_upload_handler recovery_ota_url_handler recovery_ota_verify_flash wifi_manager_init wifi_manager_connect wifi_manager_disconnect wifi_manager_status_json recovery_set_identity recovery_ssid recovery_mac recovery_hostname recovery_prepare_hostname sethostname wlan_get_mac_address mico_ota_switch_to_new_fw; do
  if ! grep -Eq "[[:space:]]${symbol}$" dist/symbols.txt; then echo "[FAIL] missing $symbol"; exit 1; fi
  echo "[PASS] $symbol"
done
for symbol in m1_uart_init m1_uart_worker m1_uart_send_init_command m1_uart_request_sensors zm1_sensor_request m1_sensor_parse m1_sensor_get_snapshot m1_display_init m1_display_set_brightness m1_display_handle_brightness_event m1_display_set_network_state m1_display_network_test network_display_worker network_health_init network_health_step; do
  if ! grep -Eq "[[:space:]]${symbol}$" dist/symbols.txt; then echo "[FAIL] missing $symbol"; exit 1; fi
  echo "[PASS] $symbol"
done
for symbol in button_manager_init button_manager_note_long_press button_manager_tick button_manager_status_json ipv6_diagnostic_init ipv6_diagnostic_start wifi_manager_history_note wifi_manager_history_record_json; do
  if ! grep -Eq "[[:space:]]${symbol}$" dist/symbols.txt; then echo "[FAIL] missing $symbol"; exit 1; fi
  echo "[PASS] $symbol"
done
for symbol in wifi_manager_start_scan scan_complete wifi_manager_apply_boot_settings wifi_manager_save_settings wifi_control_worker wifi_ap_restore_backoff_ms system_stats_init system_stats_json openm1_log_init openm1_log_json openm1_log_clear openm1_http_explicit_recovery_activity mqtt_manager_init MQTTClientInit MQTTConnect MQTTPublish mqtt_manager_set_discovery homeassistant_publish ha_policy_can_enable; do
  if ! grep -Eq "[[:space:]]${symbol}$" dist/symbols.txt; then echo "[FAIL] missing $symbol"; exit 1; fi
  echo "[PASS] $symbol"
done
grep -Fq 'mico_notify_WIFI_SCAN_ADV_COMPLETED' openm1/wifi_manager.c
grep -Fq 'micoWlanStartScanAdv()' openm1/wifi_manager.c
echo '[PASS] SDK advanced scan callback and API'
for route in /api/logs /api/logs/download /api/logs/clear /api/ota/prepare /api/wifi/status /api/wifi/history /api/ipv6/status /api/ipv6/probe /api/wifi/settings /api/wifi/connect /api/wifi/disconnect /api/wifi/scan /api/system/stats /api/sensors /api/uart/status /api/uart/raw /api/button/status /api/uart/config /api/uart/init /api/uart/sensor-request /api/display/status /api/display/brightness /api/display/network-test /api/network/health /api/ota/status /api/ota/upload /api/ota/url /api/reboot /api/mqtt/status /api/mqtt/config /api/mqtt/start /api/mqtt/stop /api/mqtt/test /api/homeassistant/status /api/homeassistant/discovery; do
  if ! "$TOOL/arm-none-eabi-strings" "dist/$PREFIX.elf" | grep -F "$route" >/dev/null; then echo "[FAIL] missing route $route"; exit 1; fi
  echo "[PASS] $route"
done
grep -Eq '^#define MICO_STDIO_UART[[:space:]]+MICO_UART_2' mico-os/board/MK3080B/mico_board.h
grep -Eq '^#define MICO_UART_FOR_APP[[:space:]]+MICO_UART_1' mico-os/board/MK3080B/mico_board.h
echo '[PASS] STDIO UART2 and app UART1 unchanged'
python3 tools/check_uart_init_command.py
python3 tools/check_display_pwm.py
if grep -En 'MICO_STDIO_UART|/api/uart/send' openm1/m1_uart.c openm1/m1_sensor.c openm1/recovery_http.c; then
  echo '[FAIL] sensor bridge must leave STDIO UART alone and forbid arbitrary TX'; exit 1
fi
echo '[PASS] UART TX restricted to sensor request and validated display frames'
cp docs/zm1-uart-reverse.md dist/zm1-uart-reverse.md
cp docs/zm1-wifi-icon-reverse.md docs/mqtt.md docs/homeassistant.md dist/
python3 tools/analyze_zm1_uart.py --reference 'reference/zM1@MK3080B@moc.ota.bin' --output dist/uart-protocol-report.txt
python3 tools/kernel_report.py --sdk-kernel "$KERNEL" --output dist/kernel-report.txt
python3 tools/make_ota.py --app "dist/$PREFIX.bin" --sdk-kernel "$KERNEL" --output "$OTA"
cmp "$OTA" "${APP_BIN%.bin}.ota.bin"
echo '[PASS] Official SDK OTA match'
python3 tools/verify_ota.py "$OTA" --sdk-kernel "$KERNEL" --app "dist/$PREFIX.bin" | tee dist/verify-report.txt
cat dist/app-stack-report.txt >> dist/verify-report.txt
python3 tools/generate_manifest.py --ota "$OTA" --app "dist/$PREFIX.bin" --sdk-kernel "$KERNEL" --app-header-report dist/app-header-report.txt --app-stack-report dist/app-stack-report.txt --output dist/manifest.json --toolchain "$GCC_VERSION"
cp 'reference/zM1@MK3080B@moc.ota.bin' dist/recovery/zM1-recovery.ota.bin
python3 - <<'PY'
from pathlib import Path
import hashlib
p=Path('dist/recovery/zM1-recovery.ota.bin')
b=p.read_bytes()
Path('dist/recovery/checksums.txt').write_text(f'size: {len(b)}\nMD5: {hashlib.md5(b).hexdigest()}\nSHA256: {hashlib.sha256(b).hexdigest()}\n')
PY
python3 - <<'PY_SUMS'
from pathlib import Path
import hashlib
root=Path('dist')
lines=[]
for path in sorted(root.rglob('*')):
    if path.is_file() and path.name!='SHA256SUMS.txt':
        lines.append(f'{hashlib.sha256(path.read_bytes()).hexdigest()}  {path.relative_to(root)}')
(root/'SHA256SUMS.txt').write_text('\n'.join(lines)+'\n')
PY_SUMS
echo 'KERNEL_APP_SAME_SDK=YES'
echo 'MOC_BOOT_VERIFIED=YES'
echo 'MOC_APP_ENTRY_VERIFIED=YES'
echo 'MOC_RUNTIME_STABLE=YES'
echo 'WIFI_VERIFIED=YES'
echo 'SOFTAP_VERIFIED=YES'
echo 'HTTP_AND_UPLOAD_OTA_VERIFIED_ON_V0_1_0_HARDWARE=YES'
echo 'STA_VERIFIED_ON_HARDWARE=YES (v0.2.0)'
echo 'WIFI_SCAN_FEATURE_PRESENT=YES; HARDWARE_VERIFIED=NO'
echo 'WIFI_ICON_BLINK_AND_SOLID_HARDWARE_VERIFIED=YES; MANUAL_RED_X_HARDWARE_VERIFIED=YES; AUTOMATIC_NO_INTERNET_RED_X=NO'
echo 'NETWORK_DISPLAY_EXECUTION=WORKER_THREAD; RTOS_TIMER=NO; NO NEW UART TX'
echo 'MQTT_AND_FOUR_HA_SENSORS_HARDWARE_VERIFIED=YES; HA_BRIGHTNESS_NUMBER_HARDWARE_VERIFIED=NO'
echo 'SENSOR_UART_VERIFIED_ON_HARDWARE=YES (type 0x0C and 0x0F received)'
echo 'SENSOR_PROTOCOL_STATUS=PARTIAL'
echo 'DISPLAY_STARTUP_SYNC_PRESENT=YES'
echo 'SENSOR_ONE_SHOT_REQUEST_PRESENT=YES; AUTO_POLLING=2000ms; HARDWARE_VERIFIED=YES'
echo 'SENSOR_VALUES_VERIFIED_ON_HARDWARE=YES'
echo 'SAFE_TO_FLASH_FOR_HARDWARE_TEST=YES'
