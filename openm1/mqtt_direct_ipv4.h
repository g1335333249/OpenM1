#pragma once
#include "mqtt_diagnostics.h"
#include <stdint.h>

/* v0.6.18 blocking IPv4 connection path. Caller owns fd on success. */
int mqtt_direct_ipv4_open(const char *host,uint16_t port,int *fd,
                          mqtt_failure_stage_t *failure_stage);
