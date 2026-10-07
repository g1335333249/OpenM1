#pragma once

typedef enum {
    MQTT_STAGE_NONE,
    MQTT_STAGE_DNS,
    MQTT_STAGE_TCP_SOCKET,
    MQTT_STAGE_TCP_CONNECT,
    MQTT_STAGE_MQTT_CONNECT,
    MQTT_STAGE_AVAILABILITY_PUBLISH,
    MQTT_STAGE_HA_DISCOVERY,
    MQTT_STAGE_STATE_PUBLISH,
    MQTT_STAGE_YIELD,
    MQTT_STAGE_PEER_CLOSED,
    MQTT_STAGE_SOCKET_READ,
    MQTT_STAGE_SOCKET_WRITE,
    MQTT_STAGE_CONFIG_CHANGED,
    MQTT_STAGE_WIFI_LOST
} mqtt_failure_stage_t;

const char *mqtt_failure_stage_name(mqtt_failure_stage_t stage);
mqtt_failure_stage_t mqtt_yield_failure_stage(int read_result,int write_result);
int mqtt_failure_is_publish(mqtt_failure_stage_t stage);
int mqtt_failure_is_yield(mqtt_failure_stage_t stage);
