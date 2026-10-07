#include "mqtt_bounded_read.h"
#include <string.h>

void openm1_mqtt_read_reset(openm1_mqtt_read_state_t *state)
{
    if (!state) return;
    memset(state,0,sizeof(*state));
    state->multiplier=1u;
}

int openm1_mqtt_read_bounded(openm1_mqtt_read_state_t *state,
                            const openm1_mqtt_read_ops_t *ops,void *context,
                            int socket,unsigned char *buffer,int length,int timeout_ms)
{
    uint32_t start,elapsed,remaining;
    int got=0,ready,n;
    if (!state || !ops || !ops->now_ms || !ops->wait_readable ||
        !ops->recv_bytes || !ops->socket_error || !ops->pause_ms ||
        !buffer || socket<0 || length<=0) return -1;
    if ((unsigned)length>OPENM1_MQTT_INBOUND_MAX) {
        state->last_result=OPENM1_MQTT_READ_PROTOCOL_ERROR;
        return state->last_result;
    }
    start=ops->now_ms(context);
    for (;;) {
        elapsed=ops->now_ms(context)-start;
        if (timeout_ms<=0 || elapsed>=(uint32_t)timeout_ms) {
            state->read_timeout_count++;
            state->last_result=got;
            return got; /* No bytes is an ordinary MQTT idle read. */
        }
        remaining=(uint32_t)timeout_ms-elapsed;
        ready=ops->wait_readable(context,socket,remaining);
        if (ready==0) {
            state->read_timeout_count++;
            state->last_result=got;
            return got;
        }
        if (ready<0) {
            state->last_socket_error=ops->socket_error(context,socket);
            state->last_result=OPENM1_MQTT_READ_SOCKET_ERROR;
            return state->last_result;
        }
        n=ops->recv_bytes(context,socket,buffer+got,length-got);
        if (n==0) {
            state->peer_close_count++;
            state->last_result=OPENM1_MQTT_READ_PEER_CLOSED;
            return state->last_result;
        }
        if (n<0) {
            state->last_socket_error=ops->socket_error(context,socket);
            if (state->last_socket_error==0) {
                /* A readiness race is not a proven socket failure. Sleep
                 * briefly before selecting again so it cannot spin. */
                ops->pause_ms(context,1u);
                continue;
            }
            state->last_result=OPENM1_MQTT_READ_SOCKET_ERROR;
            return state->last_result;
        }
        if (n>length-got) {
            state->last_result=OPENM1_MQTT_READ_PROTOCOL_ERROR;
            return state->last_result;
        }
        got+=n;
        if (got==length) break;
    }
    if (state->phase==0 && length==1) {
        state->phase=1;state->remaining=0;state->multiplier=1;
    } else if (state->phase==1 && length==1) {
        unsigned digit=buffer[0]&127u;
        if (state->multiplier>128u*128u*128u ||
            digit>(OPENM1_MQTT_INBOUND_MAX-state->remaining)/state->multiplier) {
            state->last_result=OPENM1_MQTT_READ_PROTOCOL_ERROR;
            return state->last_result;
        }
        state->remaining+=digit*state->multiplier;
        if (buffer[0]&128u) {
            if (state->multiplier>=128u*128u*128u) {
                state->last_result=OPENM1_MQTT_READ_PROTOCOL_ERROR;
                return state->last_result;
            }
            state->multiplier*=128u;
        } else state->phase=state->remaining?2u:0u;
    } else if (state->phase==2) state->phase=0;
    state->last_result=got;
    state->last_socket_error=0;
    return got;
}
