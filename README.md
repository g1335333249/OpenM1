# OpenM1 BootProbe v0.0.2

Experimental MOC application for the Phicomm Wukong M1. This build checks the boot chain through `user_handler`, `moc_app_main()`, `moc_adapter()`, `main()`, Wi-Fi power on, and a single open SoftAP. Hardware behavior of v0.0.2 has not yet been verified.

The test OTA combines `mico-os/resources/moc_kernel/3080B/kernel.bin` (3080B002.023) with an APP compiled from the same pinned SDK commit `9b09de78164940ff3876d2053f8e7dd42ca2b8ba`. It never copies the reference zM1 kernel into the test OTA. The original zM1 OTA is included unchanged in the artifact under `recovery/` with checksums.

`main()` logs each stage, calls `micoWlanPowerOn()`, then `StartNetwork()` with an open `OpenM1-Recovery` SoftAP, `192.168.4.1/24`, DHCP server. It prints `OPENM1 ALIVE <counter>` every two seconds. The return values of both Wi-Fi calls are logged. The SDK maps `micoWlanPowerOn` to `wifi_power_up()` and `micoWlanStart` to `StartNetwork()` in `include/mico_wlan.h`; `platform/MCU/MX1290/moc/moc_api.c` forwards both to kernel API pointers. `MiCO/moc_main.c` initializes the adapter and starts `main()` independently of `mico_system_init()`.

## Build

```sh
git clone https://github.com/MXCHIP/mico-os.git mico-os
git -C mico-os checkout 9b09de78164940ff3876d2053f8e7dd42ca2b8ba
# Ubuntu 22.04: install make perl curl python3 python-is-python3 dash libncurses5 lib32gcc-s1 libc6-i386
bash scripts/bootstrap_build_env.sh
bash scripts/build_bootprobe.sh
```

GitHub Actions uses the same pinned SDK and GNU Arm Embedded 5.4.1 toolchain. It checks the linked startup symbols and `user_handler`, verifies the SDK kernel identity, APP CRC16, OTA MD5 and size, and uploads `OpenM1-BootProbe-v0.0.2`. `safe_to_flash` means these static checks pass for a controlled hardware test; `hardware_verified` remains false until the device test succeeds. There is no automated device flashing. A hardwired recovery path is recommended for this experiment.
