#!/usr/bin/env python3
"""Bound the APP heap after thread stack reservations; .su is per-function only."""
import argparse
import re
import subprocess
from pathlib import Path

p=argparse.ArgumentParser()
p.add_argument('--elf',type=Path,required=True)
p.add_argument('--nm',required=True)
p.add_argument('--su-dir',type=Path,required=True)
p.add_argument('--output',type=Path,required=True)
a=p.parse_args()
symbols={}
for line in subprocess.check_output([a.nm,'-n',str(a.elf)],text=True).splitlines():
    parts=line.split()
    if len(parts)>=3 and parts[-1] in ('link_bss_end','_ram_end_'):
        symbols[parts[-1]]=int(parts[0],16)
assert len(symbols)==2, 'missing APP heap linker bounds'
heap=symbols['_ram_end_']-symbols['link_bss_end']
log_symbols={'records','scratch','log_mutex','wrap_count','dropped_count','head','count','logger_available'}
log_sizes={}
for line in subprocess.check_output([a.nm,'-S',str(a.elf)],text=True).splitlines():
    parts=line.split()
    if len(parts)==4 and parts[2].lower()=='b' and parts[3] in log_symbols:
        log_sizes[parts[3]]=int(parts[1],16)
assert set(log_sizes)==log_symbols, f'missing logger BSS symbols: {log_symbols-set(log_sizes)}'
log_bss=sum(log_sizes.values())
assert log_bss<=2560, f'OpenM1 logger BSS exceeds 2560 bytes: {log_bss}'
critical=6144+2048+4096+5120+2048 # HTTP, display, UART, Wi-Fi, housekeeping
ota=5120
optional=3072+1024+6144 # health, CPU, MQTT
frames={}
needed=('housekeeping_worker','wifi_control_worker','recovery_http_server_thread','m1_uart_worker',
        'network_display_worker','health_worker','mqtt_worker')
for path in a.su_dir.glob('*.su'):
    for line in path.read_text().splitlines():
        parts=line.split('\t')
        if len(parts)>=3:
            name=parts[0].rsplit(':',1)[-1]
            if name in needed:
                frames[name]=(int(parts[1]),parts[2],path.name)
missing=set(needed)-set(frames)
assert not missing, f'missing stack usage frames: {sorted(missing)}'
reserve=heap-critical
report=[f'APP_HEAP_REGION_BYTES={heap}',f'CRITICAL_THREAD_STACK_BUDGET={critical}',
        f'OPENM1_LOG_BSS_BYTES={log_bss}',
        f'ESTIMATED_HEAP_REGION_BEFORE_LOG_BSS={heap+log_bss}',
        f'ESTIMATED_STABLE_RESERVE_BEFORE_LOG_BSS={reserve+log_bss}',
        f'STABLE_THEORETICAL_RESERVE_AFTER_LOG_BSS={reserve}',
        f'PERMANENT_CRITICAL_STACK_BUDGET={critical}',f'TEMPORARY_OTA_STACK={ota}',
        f'OPTIONAL_THREAD_STACK_BUDGET={optional}',f'OPTIONAL_STACK_BUDGET={optional}',
        f'STABLE_THEORETICAL_HEAP_RESERVE={reserve}',
        f'OTA_THEORETICAL_HEAP_RESERVE={reserve-ota}',
        'APP_THREAD_RELEASED_AFTER_BOOT=YES',
        'STACK_USAGE_SCOPE=individual function frames only; external SDK call chains excluded']
for name in needed:
    size,kind,source=frames[name]
    report.append(f'{name}: {size} bytes ({kind}; {source})')
a.output.write_text('\n'.join(report)+'\n')
print('\n'.join(report))
if reserve<14000 or reserve-ota<10000:
    raise SystemExit('theoretical heap reserve below safety threshold')
