from pathlib import Path

root=Path('openm1')
main=(root/'main.c').read_text()
wifi=(root/'wifi_manager.c').read_text()
http=(root/'recovery_http.c').read_text()
mqtt=(root/'mqtt_manager.c').read_text()
proxy=(root/'mqtt_proxy.c').read_text()
direct=(root/'mqtt_direct_ipv4.c').read_text()
page=(root/'recovery_page.html').read_text()
config=(root/'config_store.c').read_text()
make=(root/'openm1.mk').read_text()

for name in ('ipv6_diagnostic','ipv6_probe_logic','dns_aaaa_logic'):
    assert not (root/(name+'.c')).exists()
    assert name not in make
for path in ('/api/ipv6/status','/api/ipv6/deep/status','/api/ipv6/probe',
             '/api/ipv6/probe/tcp','/api/ipv6/probe/deep'):
    assert path not in http and path not in page
assert 'AF_INET6' not in mqtt and 'AF_INET6' not in proxy
assert 'getaddrinfo' not in proxy and 'inet_pton' not in proxy
assert main.index('StartNetwork(&wifi_config)') < main.index('recovery_http_start()') < main.index('config_store_init()')
assert 'config_store_boot_ap_password(wifi_config.wifi_key)' in main
assert 'wifi_manager_set_boot_ap_password(wifi_config.wifi_key)' in main
assert 'openm1_config_t startup_config' not in main
assert 'memcpy(config.wifi_key,boot_ap_password' in wifi
assert '#if WIFI_AP_AUTO_CLOSE_SUPPORTED' in wifi
assert 'config_v3_valid' in config and 'sizeof(openm1_config_v3_t)' in config
assert 'config->proxy_port=1080' in config
assert 'mqtt_proxy_open(config,&network->my_socket' in mqtt
assert 'if (!config->proxy_enabled)' in mqtt
direct_branch=mqtt.split('if (!config->proxy_enabled) {',1)[1].split('if (mqtt_proxy_open(',1)[0]
assert 'mqtt_direct_ipv4_open(config->host,config->port' in direct_branch
assert 'gethostbyname(name)' in direct and 'connect(*fd' in direct
assert 'ioctl(' not in direct and 'select(' not in direct and 'getsockopt(' not in direct
assert 'a->proxy_enabled!=b->proxy_enabled' in mqtt
assert 'if (!config->proxy_enabled)' in proxy
assert 'PROXY_STAGE_PROTOCOL;return -1;' in proxy
assert 'PROXY_STAGE_DIRECT_IPV6' in proxy
assert 'recovery_ota_busy()' in proxy
assert 'if (*fd>=0) {close(*fd);*fd=-1;}' in proxy
for route in ('/api/mqtt/proxy/status','/api/mqtt/proxy/config',
              '/api/mqtt/proxy/test','/api/wifi/ap/config'):
    assert route in http and route in page
assert 'config.recovery_ap_password[0]?"true":"false"' in http
assert 'config.proxy_password[0]?"true":"false"' in mqtt
assert 'confirm_clear' in http and 'confirm_password' in http
assert 'proxy-fields' in page and 'toggleProxyFields()' in page
print('V019_STATIC_PASS: native IPv6 diagnostics removed; proxy, AP and migration guards')
