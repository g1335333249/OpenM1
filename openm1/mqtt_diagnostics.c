#include "mqtt_diagnostics.h"
#include "mqtt_bounded_read.h"

const char *mqtt_failure_stage_name(mqtt_failure_stage_t stage)
{
    switch (stage) {
    case MQTT_STAGE_DNS: return "dns";
    case MQTT_STAGE_TCP_SOCKET: return "tcp_socket";
    case MQTT_STAGE_TCP_CONNECT: return "tcp_connect";
    case MQTT_STAGE_MQTT_CONNECT: return "mqtt_connect";
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
