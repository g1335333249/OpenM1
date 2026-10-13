NAME := App_openm1
$(NAME)_SOURCES := main.c time_sync.c time_sync_logic.c openm1_log.c http_activity.c recovery_http.c recovery_ota.c ota_transfer_logic.c worker_retry_logic.c recovery_page.c recovery_identity.c wifi_manager.c wifi_station_logic.c wifi_settings.c ipv6_diagnostic.c ipv6_probe_logic.c dns_aaaa_logic.c system_stats.c m1_uart.c m1_sensor.c button_manager.c m1_display.c m1_display_network.c network_health.c network_health_state.c config_store.c json_min.c mqtt_manager.c mqtt_bounded_read.c mqtt_diagnostics.c mqtt_proxy.c socks5_protocol.c homeassistant.c ha_ota.c ha_ota_logic.c ha_brightness.c ha_policy.c
$(NAME)_COMPONENTS += protocols.mqtt
$(NAME)_CFLAGS += -fstack-usage
ifneq ($(OPENM1_DIAG_IPV4_TARGET),)
$(NAME)_CFLAGS += -DOPENM1_DIAG_IPV4_TARGET=\"$(OPENM1_DIAG_IPV4_TARGET)\"
endif
ifneq ($(OPENM1_DIAG_IPV4_PORT),)
$(NAME)_CFLAGS += -DOPENM1_DIAG_IPV4_PORT=$(OPENM1_DIAG_IPV4_PORT)
endif
ifneq ($(OPENM1_DIAG_LAN_IPV6_HEX),)
$(NAME)_CFLAGS += -DOPENM1_DIAG_LAN_IPV6_HEX=\"$(OPENM1_DIAG_LAN_IPV6_HEX)\"
endif
ifneq ($(OPENM1_DIAG_LAN_IPV6_PORT),)
$(NAME)_CFLAGS += -DOPENM1_DIAG_LAN_IPV6_PORT=$(OPENM1_DIAG_LAN_IPV6_PORT)
endif
GLOBAL_INCLUDES += .
