# OpenM1 BootProbe v0.0.3

Experimental MOC application for the Phicomm Wukong M1. This build probes the minimum Wi-Fi initialization path through `MicoInit()`, `micoWlanPowerOn()`, and `StartNetwork()`. It uses the SDK kernel `3080B002.023` and an APP compiled from the same fixed [MiCO commit](https://github.com/MXCHIP/mico-os/tree/9b09de78164940ff3876d2053f8e7dd42ca2b8ba) (`9b09de78164940ff3876d2053f8e7dd42ca2b8ba`). It does not use the zM1 `002.024` kernel. The original zM1 OTA remains unchanged in `reference/` and is copied into the build artifact under `recovery/`.

## Hardware findings

BootProbe v0.0.2 was tested on a real M1. Its serial log confirmed ROM and bootloader startup, kernel `3080B002.023`, `moc_app_main`, entry into `main()`, and 48 heartbeat iterations without an observed HardFault or watchdog reset. `micoWlanPowerOn()` returned 0. `StartNetwork()` returned 0 but the kernel printed `WIFI is not running`; Wi-Fi and SoftAP were **not** verified.

| Status | Value |
| --- | --- |
| `MOC_BOOT_VERIFIED` | `true` |
| `MOC_APP_ENTRY_VERIFIED` | `true` |
| `MOC_RUNTIME_STABLE` | `true` |
| `KERNEL_APP_SAME_SDK` | `true` |
| `WIFI_VERIFIED` | `false` |
| `SOFTAP_VERIFIED` | `false` |

MiCO's `system_network_daemen_start()` calls `MicoInit()` before querying or using Wi-Fi. In `include/mico.h`, `MicoInit` maps to `mxchipInit`; the MX1290 MOC API adapter forwards that call to `_kernel_api.os_apis->mxchipInit`. BootProbe v0.0.2 omitted this call. Version 0.0.3 adds it before powering on Wi-Fi. The MOC `mxchipInit()` wrapper returns `kNoErr` after invoking the kernel function, so its printed result confirms wrapper execution but does not expose an internal kernel return code.

## Probe behavior

`main()` prints the startup banner and logs each call: `MicoInit()`, RF version read, `micoWlanPowerOn()`, and `StartNetwork()`. It requests an **open** SoftAP named `OpenM1-Recovery` with IP, gateway, and DNS `192.168.4.1`, mask `255.255.255.0`, and DHCP server. It prints `OPENM1 ALIVE <counter>` every two seconds. MiCO/MOC kernel logs remain enabled, including any `WIFI is not running` message.

This version has no HTTP server, OTA server, MQTT, station mode, automatic provisioning, ATSAMD20 UART, sensor parsing, or display control. It does not call `mico_system_context_init()`, `mico_system_init()`, or `mico_system_wlan_start_autoconf()` and does not read zM1 Wi-Fi configuration.

## Build and checks

```sh
git clone https://github.com/MXCHIP/mico-os.git mico-os
git -C mico-os checkout 9b09de78164940ff3876d2053f8e7dd42ca2b8ba
# Ubuntu 22.04: install make perl curl python3 python3-lib2to3 python-is-python3 dash libncurses5 lib32gcc-s1 libc6-i386
bash scripts/bootstrap_build_env.sh
bash scripts/build_bootprobe.sh
```

GitHub Actions runs the same build and uploads `OpenM1-BootProbe-v0.0.3`. The artifact contains OTA, ELF, MAP, BIN, manifest, verification reports, SHA256SUMS, build log, and the unchanged zM1 recovery OTA. CI never sends firmware to a device.

`safe_to_flash` in the manifest means the static build, APP header, kernel identity, and OTA checks passed for a **controlled hardware test**. Version 0.0.3 Wi-Fi and SoftAP behavior remains unverified until a real M1 test. An inoperable SoftAP may require physical recovery; the included zM1 image alone is not an automatic rollback.
