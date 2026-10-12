#pragma once
#include "config_store.h"

typedef enum {
    PROXY_STAGE_NONE,PROXY_STAGE_DNS,PROXY_STAGE_TCP,PROXY_STAGE_METHOD,
    PROXY_STAGE_AUTH,PROXY_STAGE_TARGET,PROXY_STAGE_TIMEOUT,PROXY_STAGE_PROTOCOL,
    PROXY_STAGE_DIRECT_IPV6
} mqtt_proxy_stage_t;
const char *mqtt_proxy_stage_name(mqtt_proxy_stage_t stage);
/* Caller owns fd on success. Every failure closes its temporary socket. */
int mqtt_proxy_open(const openm1_config_t *config,int *fd,mqtt_proxy_stage_t *stage,
                    int *error,int *authenticated,int *target_connected);
