#pragma once
#include "mico.h"
#include <stddef.h>

#define WIFI_STA_WORKER_STACK 4096
#define WIFI_STA_CONNECT_TIMEOUT_MS 30000
#define WIFI_STA_CONNECT_SUPPORTED 1
#define WIFI_SCAN_SUPPORTED 0

OSStatus wifi_manager_init(void);
int wifi_manager_connect(const char *ssid, const char *password);
int wifi_manager_disconnect(void);
void wifi_manager_status_json(char *out, size_t out_size);
const char *wifi_manager_scan_json(void);
