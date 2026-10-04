#!/usr/bin/env python3
import argparse
import hashlib
import json
from pathlib import Path
from ota_common import SDK_COMMIT, require_sdk_kernel
from verify_ota import verify

p = argparse.ArgumentParser()
p.add_argument('--ota', type=Path, required=True)
p.add_argument('--app', type=Path, required=True)
p.add_argument('--sdk-kernel', type=Path, required=True)
p.add_argument('--app-header-report', type=Path, required=True)
p.add_argument('--output', type=Path, required=True)
p.add_argument('--toolchain', required=True)
a = p.parse_args()
checks, details = verify(a.ota, a.sdk_kernel, a.app)
header = a.app_header_report.read_text()
header_valid = all(line.startswith('PASS ') for line in header.splitlines() if line.startswith(('PASS ', 'FAIL ')))
symbols_valid = all(f'PASS {symbol}' in header for symbol in ('user_handler','moc_app_main','moc_adapter','main'))
sdk_valid = Path('mico-os').is_dir() and __import__('subprocess').check_output(['git','-C','mico-os','rev-parse','HEAD'], text=True).strip() == SDK_COMMIT
kernel = require_sdk_kernel(a.sdk_kernel)
valid = all(checks.values()) and header_valid and symbols_valid and sdk_valid and '5.4.1' in a.toolchain
data = {'name':'OpenM1','firmware':'OpenM1','version':'0.4.1','board':'MK3080B','sdk_commit':SDK_COMMIT,
        'kernel':'3080B002.023','kernel_version':'3080B002.023','sdk_kernel_version':'3080B002.023',
        'kernel_source':'mico-os/resources/moc_kernel/3080B/kernel.bin',
        'kernel_sha256':hashlib.sha256(kernel).hexdigest(),'interface_version':3,
        'moc_app_header_valid':header_valid,'startup_symbols_valid':symbols_valid,
        'ota_format_valid':all(checks.values()),'kernel_app_same_sdk':sdk_valid,
        'safe_to_flash':valid,
        'boot_verified_on_hardware':True,
        'main_verified_on_hardware':True,
        'runtime_verified_on_hardware':True,
        'wifi_init_fix':'MicoInit before micoWlanPowerOn',
        'wifi_verified_on_hardware':True,
        'softap_verified_on_hardware':True,
        'http_verified_on_hardware':True,
        'ota_upload_verified_on_hardware':True,
        'ota_flash_write_verified_on_hardware':True,
        'ota_validation_verified_on_hardware':True,
        'ota_boot_table_verified_on_hardware':True,
        'ota_reboot_verified_on_hardware':True,
        'ota_bootloader_apply_verified_on_hardware':True,
        'ota_url_verified_on_hardware':False,
        'sta_feature_present':True,'sta_connect_supported':True,
        'sta_verified_on_hardware':True,
        'wifi_scan_feature_present':True,
        'wifi_scan_verified_on_hardware':False,
        'wifi_icon_feature_present':False,
        'wifi_icon_protocol_verified':False,
        'wifi_icon_verified_on_hardware':False,
        'mqtt_feature_present':True,
        'mqtt_verified_on_hardware':False,
        'homeassistant_discovery_present':True,
        'homeassistant_discovery_verified_on_hardware':False,
        'mqtt_config_storage':'MiCO parameter partition app user data',
        'mqtt_worker_stack':6144,
        'sensor_uart_present':True,'sensor_uart_verified_on_hardware':True,
        'sensor_protocol_status':'partial','temperature_feature':True,
        'humidity_feature':True,'pm25_feature':True,'formaldehyde_feature':True,
        'sensor_values_verified_on_hardware':False,
        'sensor_uart':'MICO_UART_1','sensor_uart_tx_gpio':'GPIO9','sensor_uart_rx_gpio':'GPIO10',
        'sensor_uart_default_baud':115200,'sensor_uart_passive_rx_only':False,
        'sensor_uart_tx_init_command_present':True,
        'sensor_uart_init_command_source':'reference zM1 firmware static disassembly',
        'sensor_uart_init_command':'23 02 64 01 00 00 00 00 00 00 00 21',
        'web_ui_tabs':True,'web_ui_tabs_verified_on_hardware':False,
        'sensor_request_command_present':True,
        'sensor_request_command':'23 01 00 00 00 00 00 00 00 00 00 21',
        'sensor_request_source':'reference zM1 static disassembly',
        'sensor_request_verified_on_hardware':False,
        'sensor_auto_polling':False,
        'uart_init_command_verified_on_hardware':True,
        'type0c_verified_on_hardware':True,
        'type0f_semantics':'brightness',
        'type18_response_enabled':False,
        'sensor_uart_response_verified_on_hardware':True,
        'sensor_uart_rx_ring_size':2048,'sensor_uart_worker_stack':4096,
        'recovery_ip':'192.168.4.1','recovery_ssid_format':'OpenM1-XXXXXX',
        'ota_partition_start':'0x00110000','ota_partition_size':741376,
        'hardware_verified':False,'toolchain':a.toolchain,
        'app_size':details['app_size'],'app_payload_size':details['payload_size'],
        'ota_size':details['size'],'app_crc16':details['crc16'],
        'ota_md5':details['md5'],'ota_sha256':details['sha256']}
a.output.write_text(json.dumps(data, indent=2)+'\n')
raise SystemExit(0 if valid else 1)
