#pragma once
#include "mico.h"
#include <stddef.h>
#include <stdint.h>

#define NETWORK_HEALTH_PROBE_INTERVAL_MS 10000u
#define NETWORK_HEALTH_PROBE_TIMEOUT_MS 1800u
#define NETWORK_HEALTH_WORKER_STACK 3072

typedef enum { NETWORK_NO_WIFI, NETWORK_CHECKING, NETWORK_ONLINE, NETWORK_NO_INTERNET } network_health_state_t;
typedef struct {
    network_health_state_t state;
    uint8_t consecutive_successes,consecutive_failures;
    uint32_t last_probe_ms;
} network_health_snapshot_t;

void network_health_step(network_health_snapshot_t *snapshot, int has_wifi, int probe_result);
OSStatus network_health_init(void);
void network_health_notify_link_down(void);
void network_health_notify_link_ready(void);
void network_health_status_json(char *out,size_t capacity);
