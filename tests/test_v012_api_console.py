#!/usr/bin/env python3
"""Keep the browser catalog in sync with the real single-client HTTP router."""
import re
from pathlib import Path

page = Path('openm1/recovery_page.html').read_text()
http = Path('openm1/recovery_http.c').read_text()
catalog = page.split('const apiEndpoints=[', 1)[1].split('];', 1)[0]
entries = re.findall(r"\{method:'(GET|POST)',path:'([^']+)',category:'([^']+)',name:'([^']+)'", catalog)
assert len(entries) == 35 and len(set((m, p) for m, p, _, _ in entries)) == 35
get = http.split('if (!strcmp(method,"GET"))', 1)[1].split('if (strcmp(method,"POST"))', 1)[0]
post = http.split('if (strcmp(method,"POST"))', 1)[1].split('recovery_send_json(fd,404', 1)[0]
routes = lambda part: set(re.findall(r'(?:strcmp|strncmp)\(path,"(/api/[^\"]+)', part))
for method, section, expected in [('GET', get, 16), ('POST', post, 19)]:
    actual = {p.split('?', 1)[0] for m, p, _, _ in entries if m == method}
    assert actual == routes(section), (method, actual ^ routes(section))
    assert len(actual) == expected
assert ('GET', '/api/wifi/scan', '网络', '扫描结果') in entries
assert ('POST', '/api/wifi/scan', '网络', '发起 Wi-Fi 扫描') in entries
for path in ['/api/reboot', '/api/wifi/disconnect', '/api/wifi/settings',
             '/api/mqtt/config', '/api/mqtt/stop', '/api/logs/clear',
             '/api/uart/config', '/api/ota/prepare']:
    assert re.search(r"\{method:'POST',path:'" + re.escape(path) + r"'.*danger:", catalog)
assert "special:'upload'" in catalog and "special:'url'" in catalog
assert "activateTab('update')" in page
assert 'JSON.stringify(JSON.parse(raw),null,2)' in page
assert "await response.text()" in page and "response.ok?'成功':'HTTP 错误'" in page
assert "$('api-response').textContent=apiFormattedResponse" in page
assert "document.execCommand('copy')" in page
assert "const tabIds=['overview','network','mqtt','homeassistant','update','diagnostics','api','system']" in page
assert "else if(activeTab==='api')" not in page
assert 'localStorage' not in catalog
assert 'OpenM1 v0.6.12' in Path('openm1/homeassistant.c').read_text()
assert 'homeassistant_entity_count\':5' in Path('tools/generate_manifest.py').read_text()
print('API_CONSOLE_STATIC_PASS: 16 GET + 19 POST match firmware router')
