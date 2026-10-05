#pragma once
#include "config_store.h"
#include <stddef.h>

/* 0 success, -1 invalid fields, -2 AP-off policy conflict. */
int wifi_settings_apply_json(const char *body,size_t length,openm1_config_t *next);
int wifi_ap_policy_can_close(const openm1_config_t *config,int station_ready,
                             int saved_ssid_matches,int ota_busy);
void wifi_settings_json(const openm1_config_t *config,char *out,size_t capacity);
