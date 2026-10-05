NAME := App_openm1
$(NAME)_SOURCES := main.c recovery_http.c recovery_ota.c recovery_page.c recovery_identity.c wifi_manager.c m1_uart.c m1_sensor.c m1_display.c network_health.c network_health_state.c config_store.c json_min.c mqtt_manager.c homeassistant.c ha_policy.c
$(NAME)_COMPONENTS += protocols.mqtt
GLOBAL_INCLUDES += .
