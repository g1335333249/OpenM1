NAME := App_openm1
$(NAME)_SOURCES := main.c openm1_log.c http_activity.c recovery_http.c recovery_ota.c ota_transfer_logic.c worker_retry_logic.c recovery_page.c recovery_identity.c wifi_manager.c wifi_station_logic.c wifi_settings.c system_stats.c m1_uart.c m1_sensor.c m1_display.c m1_display_network.c network_health.c network_health_state.c config_store.c json_min.c mqtt_manager.c mqtt_bounded_read.c mqtt_diagnostics.c homeassistant.c ha_policy.c
$(NAME)_COMPONENTS += protocols.mqtt
$(NAME)_CFLAGS += -fstack-usage
GLOBAL_INCLUDES += .
