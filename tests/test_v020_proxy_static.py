from pathlib import Path

root = Path(__file__).resolve().parents[1]
source = lambda name: (root / name).read_text(encoding="utf-8")
config = source("openm1/config_store.h")
manager = source("openm1/mqtt_manager.c")
http = source("openm1/recovery_http.c")
page = source("openm1/recovery_page.html")
wifi = source("openm1/wifi_manager.c")
main = source("openm1/main.c")

assert "#define OPENM1_CONFIG_VERSION 4u" in config
assert "proxy_enabled" in config and "proxy_password[64]" in config
assert "recovery_ap_password" not in config
assert "proxy_port=1080" in source("openm1/config_store.c")
assert "config_v3_valid" in source("openm1/config_store.c")
assert "if (config->proxy_enabled)" in manager
assert "mqtt_proxy_open(config" in manager
assert "host=gethostbyname(config->host)" in manager
assert "socket(AF_INET,SOCK_STREAM,IPPROTO_TCP)" in manager
assert "MQTT_STAGE_DIRECT_IPV6" in manager
assert "mqtt_proxy_open" in source("openm1/mqtt_proxy.c")
assert "socks5_connect_request" in source("openm1/socks5_protocol.c")
for route in ("/api/mqtt/proxy/status", "/api/mqtt/proxy/config", "/api/mqtt/proxy/test"):
    assert route in http and route in page
assert "#if WIFI_AP_AUTO_CLOSE_SUPPORTED" in wifi
assert "WIFI_AP_AUTO_CLOSE_SUPPORTED 0" in source("openm1/wifi_station_logic.h")
assert "ap_security" not in main and "ap_security" not in wifi
assert "OPENM1_FACTORY_RESET_DRY_RUN 1" in source("openm1/button_manager.h")
assert "0.6.20" in main and "0.6.20" in source("tools/generate_manifest.py")
print("V020_PROXY_STABILITY_STATIC_PASS")
