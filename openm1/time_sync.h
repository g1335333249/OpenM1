#pragma once
#include "mico.h"
#include <stddef.h>
#include <stdint.h>

#define OPENM1_NTP_WORKER_STACK 2560u
#define OPENM1_NTP_RECEIVE_TIMEOUT_MS 3000u

int time_sync_server_valid(const char *server);
int time_sync_parse_reply(const uint8_t *packet,size_t length,const uint8_t token[8],uint32_t *unix_seconds);
OSStatus time_sync_init(void);
void time_sync_tick(void);
int time_sync_request(void);
int time_sync_configure(const char *body,size_t length);
void time_sync_status_json(char *out,size_t capacity);
