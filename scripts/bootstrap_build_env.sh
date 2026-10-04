#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
if [[ ! -d mico-os/.git ]]; then
  echo 'mico-os checkout required at pinned commit' >&2
  exit 1
fi
if [[ "$(git -C mico-os rev-parse HEAD)" != 9b09de78164940ff3876d2053f8e7dd42ca2b8ba ]]; then
  echo 'Incorrect MiCO SDK commit' >&2
  exit 1
fi
mkdir -p .micoder/cmd/Linux64 .micoder/compiler/arm-none-eabi-5_4-2016q2-20160622/Linux64
for name in dash cat echo rm cp mv make mkdir; do
  ln -sf "$(type -P "$name")" ".micoder/cmd/Linux64/$name"
done
cat > .micoder/cmd/Linux64/bin2c <<'EOF'
#!/usr/bin/env python3
import sys
if len(sys.argv) < 3:
    raise SystemExit('bin2c requires input and output')
data = open(sys.argv[1], 'rb').read()
with open(sys.argv[2], 'w') as out:
    out.write('const unsigned char bin2c_data[] = {\n')
    for i in range(0, len(data), 16):
        out.write(','.join('0x%02x' % c for c in data[i:i+16]) + ',\n')
    out.write('};\n')
EOF
chmod +x .micoder/cmd/Linux64/bin2c
tool_dir=.micoder/compiler/arm-none-eabi-5_4-2016q2-20160622/Linux64
if [[ ! -x "$tool_dir/bin/arm-none-eabi-gcc" ]]; then
  archive=.micoder/gcc-arm-none-eabi-5_4-2016q2-20160622-linux.tar.bz2
  curl -fL --retry 3 -o "$archive" 'https://launchpad.net/gcc-arm-embedded/5.0/5-2016-q2-update/+download/gcc-arm-none-eabi-5_4-2016q2-20160622-linux.tar.bz2'
  mkdir -p .micoder/unpacked
  tar -xf "$archive" -C .micoder/unpacked
  extracted="$(find .micoder/unpacked -type f -name arm-none-eabi-gcc -print -quit)"
  [[ -n "$extracted" ]] || { echo 'GCC archive has no compiler' >&2; exit 1; }
  cp -a "$(dirname "$extracted")/.."/. "$tool_dir"/
fi
"$tool_dir/bin/arm-none-eabi-gcc" --version | head -1
"$tool_dir/bin/arm-none-eabi-gcc" --version | head -1 | grep -q '5.4.1' || { echo 'Exact GCC 5.4.1 required' >&2; exit 1; }
python3 -m lib2to3 -w -n mico-os/makefiles/scripts >/dev/null 2>&1
python3 - <<'PY'
from pathlib import Path
p = Path('mico-os/MiCO/system/qc_test/qc_test.mk')
s = p.read_text()
s = s.replace('internal/qc_test_cli.c \\\n                   internal/qc_test.blenrg.c', 'internal/qc_test_cli.c')
if 'internal/qc_test.blenrg.c' in s:
    raise SystemExit('QC source exclusion failed')
p.write_text(s)
gen = Path('mico-os/makefiles/scripts/gen_moc_bin_output_file.py')
s = gen.read_text().replace("md5.hexdigest().decode('hex')", "md5.digest()")
s = s.replace("'\\xFF'*(MOC_APP_OFFSET-len(kernel))", "b'\\xFF'*(MOC_APP_OFFSET-len(kernel))")
gen.write_text(s)
common = Path('mico-os/makefiles/scripts/gen_common_bin_output_file.py')
s = common.read_text().replace("'\\xFF'*gap_szie", "b'\\xFF'*gap_szie")
common.write_text(s)
PY
