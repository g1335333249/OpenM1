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
critical=4096+6144+2048+4096+5120+5120 # app, HTTP, display, UART, Wi-Fi, OTA rescue
optional=3072+1024+6144 # health, CPU, MQTT
frames={}
needed=('wifi_control_worker','recovery_http_server_thread','m1_uart_worker',
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
        f'OPTIONAL_THREAD_STACK_BUDGET={optional}',f'CRITICAL_THEORETICAL_HEAP_RESERVE={reserve}',
        'CRITICAL_INCLUDES_OTA_WORKER=YES',
        'STACK_USAGE_SCOPE=individual function frames only; external SDK call chains excluded']
for name in needed:
    size,kind,source=frames[name]
    report.append(f'{name}: {size} bytes ({kind}; {source})')
a.output.write_text('\n'.join(report)+'\n')
print('\n'.join(report))
if reserve<10000:
    raise SystemExit('critical theoretical heap reserve below 10000 bytes')
