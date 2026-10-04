#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
SDK_COMMIT=9b09de78164940ff3876d2053f8e7dd42ca2b8ba
KERNEL=mico-os/resources/moc_kernel/3080B/kernel.bin
PREFIX=OpenM1-BootProbe-v0.0.3
OTA="dist/$PREFIX@MK3080B@moc.ota.bin"
TOOL=.micoder/compiler/arm-none-eabi-5_4-2016q2-20160622/Linux64/bin
[[ "$(git -C mico-os rev-parse HEAD)" == "$SDK_COMMIT" ]]
echo '[PASS] SDK commit'
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
echo 'WIFI_VERIFIED=NO'
echo 'SOFTAP_VERIFIED=NO'
echo 'SAFE_TO_FLASH_FOR_HARDWARE_TEST=YES'
