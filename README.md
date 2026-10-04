# OpenM1 Recovery v0.1.0

**DEVELOPMENT / EXPERIMENTAL.** OpenM1 Recovery is an open firmware application for the Phicomm Wukong M1 air monitor. It replaces the MiCO/MOC user application on the MXCHIP EMW3080BE (MX1290, Cortex-M4F), while leaving the ATSAMD20G17A factory firmware untouched. It uses the pinned [MXCHIP mico-os commit `9b09de78164940ff3876d2053f8e7dd42ca2b8ba`](https://github.com/MXCHIP/mico-os/tree/9b09de78164940ff3876d2053f8e7dd42ca2b8ba) and its `MK3080B@moc` kernel `3080B002.023`.

## Status and hardware evidence

BootProbe v0.0.3 was tested on a real M1: ROM, bootloader, MOC kernel, `main()`, `MicoInit()`, RF read (`3080B-3.6a`), Wi-Fi power, and the open `OpenM1-Recovery` SoftAP all worked; a phone could discover and join the AP. The `WIFI deinitialized` kernel line was present, but SoftAP worked. Recovery v0.1.0 keeps that exact Wi-Fi startup sequence. **Its HTTP server, upload OTA, and URL OTA have not yet been tested on hardware.** The manifest records these separately. Wait for a controlled hardware test before relying on this firmware as the only recovery method.

| Hardware check | Status |
| --- | --- |
| Bootloader / MOC kernel / APP entry / `main()` | Verified with v0.0.3 |
| Wi-Fi / SoftAP | Verified with v0.0.3 |
| Recovery HTTP / OTA upload / OTA URL | Not yet verified |

## Recovery operation

OpenM1 starts a SoftAP before any future application logic. It is **open, with no password**, named `OpenM1-Recovery`, at `192.168.4.1/24`, with DHCP server. Join it and visit [http://192.168.4.1](http://192.168.4.1). The page displays device status and offers raw OTA file upload or an HTTP URL. There is no authentication. Do not expose the interface to an untrusted network.

The browser posts the selected `*.ota.bin` directly as `application/octet-stream` to `/api/ota/upload`, with `Content-Length`; there is no multipart form. It polls `/api/ota/status` for progress. The alternative `/api/ota/url` accepts `{"url":"http://host:port/path.ota.bin"}`. HTTPS, chunked responses and redirects are not supported in v0.1.0. An HTTP URL must be reachable **from the device**; SoftAP has no Internet uplink. A computer joined to the AP can run `python3 -m http.server 8000` and serve `http://192.168.4.2:8000/firmware.ota.bin` if that is its assigned IP.

APIs: `GET /`, `GET /api/health` (`{"status":"ok","recovery":true}`), `GET /api/info`, `GET /api/ota/status`, `POST /api/ota/upload`, `POST /api/ota/url`, and `POST /api/reboot`. Connections close after each response. HTTP uses a dedicated socket thread with a 6144-byte stack; the OTA worker has a 5120-byte stack and a static 2048-byte transfer/verify buffer. The application does not explicitly allocate a full firmware image or issue any application-level `malloc`; MiCO context and thread APIs may allocate internally. Only one OTA may run at a time.

## OTA format and safeguards

A full MOC `.ota.bin` contains the MOC kernel at `0x00000`, padded to APP offset `0x75000`, then an 8-byte APP record (`uint32` payload length, two identical `uint16` APP CRC values), the APP payload, and a final 16-byte raw MD5 of all preceding bytes. The OTA partition begins around `0x110000` and is approximately `0xB5000` bytes; the running firmware queries its actual size and also caps transfers at `0xB5000`.

Recovery streams the upload or download into `MICO_PARTITION_OTA_TEMP`. It then **re-reads Flash** to validate the record length, duplicate APP CRCs, recalculated APP payload CRC, and embedded MD5. It separately calculates the MiCO boot-table CRC16 over the complete OTA **excluding** the final 16-byte MD5. Only after all checks pass does it call `mico_ota_switch_to_new_fw(total_size - 16, boot_crc16)` and check that the system context update succeeds. It sends the HTTP success response, waits two seconds, and calls `MicoSystemReboot()`. On any failure it leaves the current firmware running and reports a failure state. These checks validate the file format and transfer integrity; they do not prove that an arbitrary APP will boot or that its hardware behavior is correct.

The provided `reference/zM1@MK3080B@moc.ota.bin` is unchanged and copied into the artifact as a manual recovery reference. The v0.1.0 OTA uses the pinned SDK `3080B002.023` kernel and an APP built against that same SDK; it does not reuse the original zM1 `002.024` kernel. If the new Recovery HTTP/OTA path fails in hardware, restoring firmware may require physical access or another already working OTA path. CI never sends OTA commands to a device.

## Build and verification

On Ubuntu 22.04 with Python 3, make, perl and required 32-bit libraries:

```sh
git clone https://github.com/MXCHIP/mico-os.git mico-os
git -C mico-os checkout 9b09de78164940ff3876d2053f8e7dd42ca2b8ba
bash scripts/bootstrap_build_env.sh
bash scripts/build_recovery.sh
```

The bootstrap downloads the exact ARM GCC 5.4.1 toolchain and prepares MiCoder compatibility wrappers. The build runs `openm1@MK3080B@moc`, checks the MOC APP header and required Recovery symbols, compares our OTA with the official SDK output, verifies APP CRC and MD5, and writes `dist/manifest.json`, `verify-report.txt`, `symbols.txt`, `app-header-report.txt`, `kernel-report.txt`, `SHA256SUMS.txt`, and `build.log`. To verify an existing build manually, run:

```sh
python3 tools/verify_ota.py dist/OpenM1-Recovery-v0.1.0@MK3080B@moc.ota.bin \
  --sdk-kernel mico-os/resources/moc_kernel/3080B/kernel.bin \
  --app dist/OpenM1-Recovery-v0.1.0.bin
```

GitHub Actions runs the same clean build on pushes to `main` and manual dispatch. Open the latest **Build OpenM1 Recovery** run, download artifact `OpenM1-Recovery-v0.1.0`, and inspect the manifest and verification reports before considering a controlled hardware test. `safe_to_flash` indicates static build and OTA-format checks only; it does **not** claim that v0.1.0 HTTP or OTA have passed hardware testing.
