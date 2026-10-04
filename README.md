# OpenM1

**OpenM1 v0.0.x = DEVELOPMENT / EXPERIMENTAL. Do not flash test builds to a real M1 yet.**

OpenM1 is an experimental replacement for the zM1 user application on the Phicomm Wukong M1 air monitor. It targets the MXCHIP EMW3080BE Wi-Fi module, MiCO board `MK3080B`, and MX1290 Cortex-M4F. The ATSAMD20G17A main MCU and its original firmware are untouched. This project uses the MiCO/MOC application ABI, not a standalone RTL8710 build.

## Current stage

Version 0.0.1 builds an application that prints a startup banner, uses the MiCO system monitor to service the watchdog independently of logging, tries the stored MiCO Wi-Fi credentials, starts `OpenM1-XXXX` SoftAP after 30 seconds without an IP address, serves a small status page and `GET /api/info`, accepts `POST /api/ota`, and records UART1 RX bytes as hex. The SoftAP is **open, without a password**, at `192.168.4.1`. UART1 is provisionally configured for 115200 8N1; its physical connection and baud rate have not been measured on an M1.

The OTA endpoint accepts JSON such as `{"url":"http://server/OpenM1-v0.0.2@MK3080B@moc.ota.bin"}`. It accepts plain HTTP and requires `Content-Length`. It writes only to the MiCO `MICO_PARTITION_OTA_TEMP` partition, reads the image back, checks APP length, duplicated CRC, APP CRC16, and trailing binary MD5, then calls the MiCO upgrade marker API and reboots. It does not send OTA commands to any device from CI. The first physical migration and subsequent upgrade have **not** been tested on hardware; successful static checks cannot establish runtime compatibility with the reference MOC kernel.

Air quality parsing, screen control, MQTT, Home Assistant, and a full UI are future work.

## Reference firmware and kernel

`reference/zM1@MK3080B@moc.ota.bin` is the supplied working zM1 image and is never modified by the build. Its full SHA256 is `20c5e6ae1692e3e047063b637b137635ab9e57711dba7c27ef0eb6cf1390887e`. The first `0x75000` bytes are copied verbatim into the first-migration image. The reference kernel SHA256 is `f22ad344286bb97755ab7ba2fb18e8768140c5383875098dfb0449d2184abc74`. The pinned SDK kernel differs beginning at `0x8`; see `kernel_compare.txt` in each artifact. This difference is a material ABI risk. Source compatibility and a successful link do not prove that the reference kernel will run the new APP.

If the reference file is missing, first-migration verification fails with `REFERENCE OTA REQUIRED FOR FIRST-MIGRATION SAFETY CHECK`. Never substitute a generated or unrelated image.

## Build

The SDK is fixed at [MXCHIP/mico-os commit `9b09de78164940ff3876d2053f8e7dd42ca2b8ba`](https://github.com/MXCHIP/mico-os/tree/9b09de78164940ff3876d2053f8e7dd42ca2b8ba). A Linux build uses the exact GNU Arm Embedded 5.4.1 toolchain:

```sh
git clone https://github.com/MXCHIP/mico-os.git mico-os
git -C mico-os checkout 9b09de78164940ff3876d2053f8e7dd42ca2b8ba
sudo apt-get install make perl curl python3 python-is-python3 dash libncurses5
bash scripts/bootstrap_build_env.sh
make -f mico-os/makefiles/Makefile openm1@MK3080B@moc HOST_OS=Linux64 TOOLS_ROOT=./.micoder SOURCE_ROOT=./
```

The bootstrap downloads the pinned toolchain, creates MiCoder command links, converts legacy Python 2 scripts for Python 3, and excludes an unrelated BlueNRG factory-test source with absent vendor headers. It does not change the committed SDK revision.

The GitHub Actions workflow runs on pushes to `main`, `v*` tags, or manually through **Actions → Build OpenM1 Firmware → Run workflow**. After a successful run, open the run summary and download `OpenM1-firmware-<commit SHA>` from **Artifacts**. The artifact contains the APP BIN and ELF, our first-migration OTA, the SDK-generated OTA and user BIN, manifest, checksums, kernel comparison, and build log. Tag runs also create a Release after all checks pass.

## OTA format and verification

At `0x75000` are a little-endian 32-bit APP payload length, two identical little-endian CRC16 values, and the APP payload. The CRC16 matches `gen_moc_bin_output_file.py` and covers compiler APP binary bytes from offset 8. The last 16 OTA bytes are the raw `MD5(ota[:-16])` digest. The total file must fit within `0xB5000` bytes. For the first migration, the entire kernel region must match the reference OTA exactly.

```sh
python3 tools/verify_ota.py 'dist/OpenM1-v0.0.1@MK3080B@moc.ota.bin' --reference 'reference/zM1@MK3080B@moc.ota.bin'
```

`SAFE TO FLASH: YES` from this script means only that these **static file checks** passed. It is not a claim of tested boot, Wi-Fi, UART, OTA recovery, or compatibility between the reference kernel and an APP linked against the SDK's different kernel.

## Migration risk and recovery

The zM1 → OpenM1 path would use zM1's existing HTTP OTA setting and the generated `*.ota.bin`. **Do not trigger that OTA on a real device at this stage.** There is no proven remote rollback if OpenM1 fails to boot or connect; recovery may require opening the M1 and using the module's serial or debug interface. A hardware recovery procedure has not yet been validated. Do not place device MACs, Wi-Fi credentials, or MQTT secrets in this repository.
