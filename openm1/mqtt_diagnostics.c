#include "mqtt_diagnostics.h"
#include "mqtt_bounded_read.h"

const char *mqtt_failure_stage_name(mqtt_failure_stage_t stage)
{
    switch (stage) {
    case MQTT_STAGE_DNS: return "dns";
    case MQTT_STAGE_TCP_SOCKET: return "tcp_socket";
    case MQTT_STAGE_TCP_CONNECT: return "tcp_connect";
    case MQTT_STAGE_MQTT_CONNECT: return "mqtt_connect";
    case MQTT_STAGE_SUBSCRIBE: return "subscribe";
    case MQTT_STAGE_AVAILABILITY_PUBLISH: return "availability_publish";
    case MQTT_STAGE_HA_DISCOVERY: return "ha_discovery";
    case MQTT_STAGE_STATE_PUBLISH: return "state_publish";
    case MQTT_STAGE_YIELD: return "yield";
    case MQTT_STAGE_PEER_CLOSED: return "peer_closed";
    case MQTT_STAGE_SOCKET_READ: return "socket_read";
    case MQTT_STAGE_SOCKET_WRITE: return "socket_write";
    case MQTT_STAGE_CONFIG_CHANGED: return "config_changed";
    case MQTT_STAGE_WIFI_LOST: return "wifi_lost";
    default: return "none";
    }
}
mqtt_failure_stage_t mqtt_yield_failure_stage(int read_result,int write_result)
{
    if (read_result==OPENM1_MQTT_READ_PEER_CLOSED) return MQTT_STAGE_PEER_CLOSED;
    if (read_result==OPENM1_MQTT_READ_SOCKET_ERROR ||
        read_result==OPENM1_MQTT_READ_PROTOCOL_ERROR) return MQTT_STAGE_SOCKET_READ;
    if (write_result<0) return MQTT_STAGE_SOCKET_WRITE;
    return MQTT_STAGE_YIELD;
}
int mqtt_failure_is_publish(mqtt_failure_stage_t stage)
{
    return stage==MQTT_STAGE_AVAILABILITY_PUBLISH || stage==MQTT_STAGE_HA_DISCOVERY ||
           stage==MQTT_STAGE_STATE_PUBLISH;
}
int mqtt_failure_is_yield(mqtt_failure_stage_t stage)
{
    return stage==MQTT_STAGE_YIELD || stage==MQTT_STAGE_PEER_CLOSED ||
           stage==MQTT_STAGE_SOCKET_READ || stage==MQTT_STAGE_SOCKET_WRITE;
}
mqtt_start_block_t mqtt_start_block_decide(int enabled,int configured,int wifi_control,
                                           int station_ready,int ota_busy,int low_memory,
                                           unsigned stack_quiet_remaining_ms)
{
    if (!enabled) return MQTT_START_DISABLED;
    if (!configured) return MQTT_START_NOT_CONFIGURED;
    if (ota_busy) return MQTT_START_OTA_BUSY;
    if (low_memory) return MQTT_START_LOW_MEMORY;
    if (!wifi_control || !station_ready) return MQTT_START_WAITING_NETWORK;
    if (stack_quiet_remaining_ms) return MQTT_START_STACK_FAULT_COOLDOWN;
    return MQTT_START_NONE;
}
const char *mqtt_start_block_name(mqtt_start_block_t reason)
{
    switch (reason) {
    case MQTT_START_DISABLED: return "disabled";
    case MQTT_START_NOT_CONFIGURED: return "not_configured";
    case MQTT_START_WAITING_NETWORK: return "waiting_network";
    case MQTT_START_STACK_FAULT_COOLDOWN: return "stack_fault_cooldown";
    case MQTT_START_LOW_MEMORY: return "low_memory";
    case MQTT_START_OTA_BUSY: return "ota_busy";
    default: return "none";
    }
}
const char *mqtt_start_state_name(mqtt_start_block_t reason)
{
    if (reason==MQTT_START_DISABLED) return "disabled";
    if (reason==MQTT_START_STACK_FAULT_COOLDOWN) return "deferred_stack_fault";
    if (reason==MQTT_START_LOW_MEMORY) return "deferred_low_memory";
    return "waiting_worker";
}
