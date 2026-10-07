#pragma once
#include <stdint.h>

#define OPENM1_MQTT_INBOUND_MAX 1024u
#define OPENM1_MQTT_READ_PEER_CLOSED (-2)
#define OPENM1_MQTT_READ_SOCKET_ERROR (-3)
#define OPENM1_MQTT_READ_PROTOCOL_ERROR (-4)

typedef struct {
    uint32_t read_timeout_count;
    uint32_t peer_close_count;
    unsigned phase,remaining,multiplier;
    int last_result,last_socket_error;
} openm1_mqtt_read_state_t;

typedef struct {
    uint32_t (*now_ms)(void *context);
    int (*wait_readable)(void *context,int socket,uint32_t timeout_ms);
    int (*recv_bytes)(void *context,int socket,unsigned char *buffer,int length);
    int (*socket_error)(void *context,int socket);
    void (*pause_ms)(void *context,uint32_t milliseconds);
} openm1_mqtt_read_ops_t;

void openm1_mqtt_read_reset(openm1_mqtt_read_state_t *state);
int openm1_mqtt_read_bounded(openm1_mqtt_read_state_t *state,
                            const openm1_mqtt_read_ops_t *ops,void *context,
                            int socket,unsigned char *buffer,int length,int timeout_ms);
