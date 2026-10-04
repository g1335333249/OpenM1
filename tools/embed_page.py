#!/usr/bin/env python3
"""Embed the UTF-8 Recovery page as a C string; no runtime filesystem needed."""
import argparse
import json
from pathlib import Path

p = argparse.ArgumentParser()
p.add_argument('--check', action='store_true')
a = p.parse_args()
html = Path('openm1/recovery_page.html').read_text(encoding='utf-8')
lines = html.splitlines(keepends=True)
source = '#include "recovery.h"\nconst char recovery_page[] =\n'
source += ''.join(json.dumps(line, ensure_ascii=False) + '\n' for line in lines)
source += ';\nconst size_t recovery_page_length = sizeof(recovery_page)-1;\n'
path = Path('openm1/recovery_page.c')
if a.check:
    if path.read_text(encoding='utf-8') != source:
        raise SystemExit('embedded Recovery page is stale; run python3 tools/embed_page.py')
else:
    path.write_text(source, encoding='utf-8')
