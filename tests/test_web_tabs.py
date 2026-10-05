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
assert 'const SENSOR_REFRESH_INTERVAL_MS=2000;' in source
assert 'overviewSensorTimer=setInterval(pollOverviewSensors,SENSOR_REFRESH_INTERVAL_MS)' in source
assert 'clearInterval(overviewSensorTimer)' in source
assert 'if(force)sensorPollQueued=true' in source
assert "if(id==='overview')startOverviewSensorPolling()" in source
assert 'function startOtaPolling(){if(otaTimer)return;stopOverviewSensorPolling()' in source
overview_scheduler = source.split("if(activeTab==='overview'){if(now-lastOverviewTick", 1)[1].split("else if(activeTab==='network'", 1)[0]
assert 'refreshSensors()' not in overview_scheduler, 'overview sensors must use only the dedicated 2 s timer'
assert '暂时无法读取传感器数据，已保留上次显示值' in source
assert "if(activeTab==='diagnostics')" in source
assert "'/api/uart/sensor-request'" in source
assert '500,1000,2000' in source
assert 'mqttFormInitialized' in source and 'haFormInitialized' in source
assert "if($('mqtt-password').value)c.password=" in source
for route in ('/api/wifi/scan', '/api/mqtt/status', '/api/homeassistant/status',
              '/api/ota/upload', '/api/ota/url', '/api/uart/init', '/api/uart/raw',
              '/api/display/status', '/api/display/brightness',
              '/api/display/network-test', '/api/network/health'):
    assert route in source, route
for mode in ('blink', 'online', 'no_internet', 'auto'):
    assert f'data-network-test="{mode}"' in source, mode
for label in ('实时环境数据', '温度', '湿度', 'PM2.5', '甲醛', '亮度事件帧'):
    assert label in source, label
assert "$('display-brightness').addEventListener('input',showBrightnessLabel)" in source
assert "$('display-brightness').addEventListener('change',async()=>" in source
assert '重新同步显示状态' in source
assert '本版不会自动轮询' not in source
print('WEB_TABS_PASS: 7 tabs, hash, independent OTA polling, retained API and UTF-8')
