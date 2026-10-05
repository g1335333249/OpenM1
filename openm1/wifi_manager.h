#pragma once
#include "mico.h"
#include "wifi_station_logic.h"
#include <stddef.h>

#define WIFI_STA_CONNECT_TIMEOUT_MS 30000
#define WIFI_STA_CONNECT_SUPPORTED 1
#define WIFI_SCAN_SUPPORTED 1
#define WIFI_SCAN_MAX_AP 20

OSStatus wifi_manager_init(void);
OSStatus wifi_manager_apply_boot_settings(void);
void wifi_manager_settings_json(char *out,size_t capacity);
int wifi_manager_save_settings(const char *body,size_t length);
int wifi_manager_connect(const char *ssid, const char *password);
int wifi_manager_disconnect(void);
void wifi_manager_status_json(char *out, size_t out_size);
int wifi_manager_start_scan(void);
const char *wifi_manager_scan_json(void);
int wifi_manager_station_ready(void);
int wifi_manager_station_link(void);
int wifi_manager_station_rssi(void);
void wifi_manager_station_ip(char out[16]);
