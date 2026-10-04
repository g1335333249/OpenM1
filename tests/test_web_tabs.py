#!/usr/bin/env python3
"""Check the embedded page's navigation and preserved API surface."""
from html.parser import HTMLParser
from pathlib import Path


class Page(HTMLParser):
    def __init__(self):
        super().__init__()
        self.tabs = []
        self.panels = []
        self.ids = []
        self.external = []

    def handle_starttag(self, tag, attrs):
        a = dict(attrs)
        if 'id' in a:
            self.ids.append(a['id'])
        if 'data-tab' in a:
            self.tabs.append(a['data-tab'])
        if 'data-panel' in a:
            self.panels.append(a['data-panel'])
        if tag in ('script', 'link', 'iframe', 'img') and ('src' in a or 'href' in a):
            self.external.append((tag, a))


source = Path('openm1/recovery_page.html').read_text(encoding='utf-8')
page = Page()
page.feed(source)
expected = ['overview', 'network', 'mqtt', 'homeassistant', 'update', 'diagnostics', 'system']
assert page.tabs == expected and page.panels == expected
assert len(page.ids) == len(set(page.ids)), 'duplicate HTML id'
assert not page.external, 'external page resource'
assert "activeTab='overview'" in source
assert "activateTab(location.hash.slice(1))" in source
assert "panel.hidden=panel.dataset.panel!==id" in source
assert 'history.replaceState' in source
assert 'startOtaPolling()' in source and 'stopOtaPolling()' in source
assert "if(activeTab==='diagnostics')" in source
assert "'/api/uart/sensor-request'" in source
assert '500,1000,2000' in source
assert 'mqttFormInitialized' in source and 'haFormInitialized' in source
assert "if($('mqtt-password').value)c.password=" in source
for route in ('/api/wifi/scan', '/api/mqtt/status', '/api/homeassistant/status',
              '/api/ota/upload', '/api/ota/url', '/api/uart/init', '/api/uart/raw'):
    assert route in source, route
for label in ('实时环境数据', '温度', '湿度', 'PM2.5', '甲醛', '亮度事件帧'):
    assert label in source, label
print('WEB_TABS_PASS: 7 tabs, hash, independent OTA polling, retained API and UTF-8')
