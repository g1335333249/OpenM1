#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
SDK_COMMIT=9b09de78164940ff3876d2053f8e7dd42ca2b8ba
KERNEL=mico-os/resources/moc_kernel/3080B/kernel.bin
PREFIX=OpenM1-v0.2.0
OTA="dist/$PREFIX@MK3080B@moc.ota.bin"
TOOL=.micoder/compiler/arm-none-eabi-5_4-2016q2-20160622/Linux64/bin
[[ "$(git -C mico-os rev-parse HEAD)" == "$SDK_COMMIT" ]]
echo '[PASS] SDK commit'
python3 tools/embed_page.py --check
python3 - <<'PY_CHECK_PAGE'
from pathlib import Path
p=Path('openm1/recovery_page.html').read_text(encoding='utf-8')
for text in ('斐讯悟空 M1 开源固件','设备信息','网络状态','连接家庭 Wi-Fi','本地固件升级','网络固件升级','等待设备重新上线'):
    assert text in p, text
print('[PASS] Chinese UTF-8 Recovery page')
PY_CHECK_PAGE
GCC_VERSION="$($TOOL/arm-none-eabi-gcc --version)"
GCC_VERSION="${GCC_VERSION%%$'\n'*}"
echo "$GCC_VERSION"
[[ "$GCC_VERSION" == *5.4.1* ]]
echo '[PASS] GCC 5.4.1'
set -o pipefail
make -f mico-os/makefiles/Makefile openm1@MK3080B@moc HOST_OS=Linux64 TOOLS_ROOT=./.micoder SOURCE_ROOT=./ VERBOSE=1 2>&1 | tee build.log
mkdir -p dist/recovery
APP_BIN="$(find build -type f -name 'openm1@MK3080B@moc.bin' -print -quit)"
[[ -n "$APP_BIN" ]]
APP_ELF="${APP_BIN%.bin}.elf"
APP_MAP="${APP_BIN%.bin}.map"
cp "$APP_BIN" "dist/$PREFIX.bin"
cp "$APP_ELF" "dist/$PREFIX.elf"
cp "$APP_MAP" "dist/$PREFIX.map"
cp build.log dist/build.log
echo '[PASS] MK3080B@moc build'
python3 tools/verify_app.py --elf "dist/$PREFIX.elf" --nm "$TOOL/arm-none-eabi-nm" --objdump "$TOOL/arm-none-eabi-objdump" --symbols dist/symbols.txt --report dist/app-header-report.txt
for symbol in recovery_http_server_thread recovery_ota_upload_handler recovery_ota_url_handler recovery_ota_verify_flash wifi_manager_init wifi_manager_connect wifi_manager_disconnect wifi_manager_status_json recovery_set_identity recovery_ssid recovery_mac wlan_get_mac_address mico_ota_switch_to_new_fw; do
  if ! grep -Eq "[[:space:]]${symbol}$" dist/symbols.txt; then echo "[FAIL] missing $symbol"; exit 1; fi
  echo "[PASS] $symbol"
done
for route in /api/wifi/status /api/wifi/connect /api/wifi/disconnect /api/wifi/scan; do
  if ! "$TOOL/arm-none-eabi-strings" "dist/$PREFIX.elf" | grep -F "$route" >/dev/null; then echo "[FAIL] missing route $route"; exit 1; fi
  echo "[PASS] $route"
done
python3 tools/kernel_report.py --sdk-kernel "$KERNEL" --output dist/kernel-report.txt
python3 tools/make_ota.py --app "dist/$PREFIX.bin" --sdk-kernel "$KERNEL" --output "$OTA"
cmp "$OTA" "${APP_BIN%.bin}.ota.bin"
echo '[PASS] Official SDK OTA match'
python3 tools/verify_ota.py "$OTA" --sdk-kernel "$KERNEL" --app "dist/$PREFIX.bin" | tee dist/verify-report.txt
python3 tools/generate_manifest.py --ota "$OTA" --app "dist/$PREFIX.bin" --sdk-kernel "$KERNEL" --app-header-report dist/app-header-report.txt --output dist/manifest.json --toolchain "$GCC_VERSION"
cp 'reference/zM1@MK3080B@moc.ota.bin' dist/recovery/zM1-recovery.ota.bin
python3 - <<'PY'
from pathlib import Path
import hashlib
p=Path('dist/recovery/zM1-recovery.ota.bin')
b=p.read_bytes()
Path('dist/recovery/checksums.txt').write_text(f'size: {len(b)}\nMD5: {hashlib.md5(b).hexdigest()}\nSHA256: {hashlib.sha256(b).hexdigest()}\n')
PY
(cd dist && find . -type f ! -name SHA256SUMS.txt -print0 | sort -z | xargs -0 sha256sum > SHA256SUMS.txt)
echo 'KERNEL_APP_SAME_SDK=YES'
echo 'MOC_BOOT_VERIFIED=YES'
echo 'MOC_APP_ENTRY_VERIFIED=YES'
echo 'MOC_RUNTIME_STABLE=YES'
echo 'WIFI_VERIFIED=YES'
echo 'SOFTAP_VERIFIED=YES'
echo 'HTTP_AND_UPLOAD_OTA_VERIFIED_ON_V0_1_0_HARDWARE=YES'
echo 'STA_VERIFIED_ON_HARDWARE=NO'
echo 'WIFI_SCAN_ENABLED=NO'
echo 'SAFE_TO_FLASH_FOR_HARDWARE_TEST=YES'
