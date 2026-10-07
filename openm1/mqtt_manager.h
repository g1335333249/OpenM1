#pragma once
#include "mico.h"
#include <stddef.h>

#define MQTT_WORKER_STACK 6144
OSStatus mqtt_manager_init(void);
void mqtt_manager_maybe_start(int user_requested);
int mqtt_manager_ready_for_cpu(void);
int mqtt_manager_configure(const char *body,size_t length);
int mqtt_manager_start(void);
int mqtt_manager_stop(void);
int mqtt_manager_set_discovery(int enabled);
void mqtt_manager_status_json(char *out,size_t capacity);
void homeassistant_status_json(char *out,size_t capacity);
