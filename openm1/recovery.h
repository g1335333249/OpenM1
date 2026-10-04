#pragma once
#include "mico.h"
#include <stdint.h>

#define RECOVERY_FALLBACK_SSID "OpenM1-RECOVERY"
#define RECOVERY_IP "192.168.4.1"
#define RECOVERY_HTTP_STACK 6144
#define RECOVERY_OTA_STACK 5120
#define RECOVERY_OTA_BUFFER 2048
#define RECOVERY_MAX_OTA_SIZE 0xB5000u

void recovery_set_rf(const char *version);
const char *recovery_rf(void);
void recovery_set_identity(const char *ssid, const char *mac);
const char *recovery_ssid(void);
const char *recovery_mac(void);
OSStatus recovery_http_start(void);
int recovery_http_ready(void);
void recovery_ota_partition_log(void);
void recovery_ota_status_json(char *out, size_t size);
int recovery_ota_begin(int fd, int is_url, uint32_t length, const uint8_t *initial, size_t initial_len);
int recovery_ota_busy(void);
void recovery_send_json(int fd, int code, const char *json);
void recovery_send_text(int fd, int code, const char *type, const char *body, size_t length);
int recovery_send_all(int fd, const void *buf, size_t len);
extern const char recovery_page[];
extern const size_t recovery_page_length;
