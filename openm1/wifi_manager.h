#pragma once
#include "mico.h"
#include "wifi_station_logic.h"
#include <stddef.h>
#include <stdint.h>

#define WIFI_STA_CONNECT_TIMEOUT_MS 30000
#define WIFI_STA_CONNECT_SUPPORTED 1
#define WIFI_SCAN_SUPPORTED 1
#define WIFI_SCAN_MAX_AP 20

typedef enum {
    WIFI_HISTORY_STATION_BAD, WIFI_HISTORY_STATION_SAMPLE_RECOVERED,
    WIFI_HISTORY_STATION_LOST, WIFI_HISTORY_STATION_CONNECTED,
    WIFI_HISTORY_STATION_REARM, WIFI_HISTORY_AP_DOWN, WIFI_HISTORY_AP_UP,
    WIFI_HISTORY_AP_RESTORED, WIFI_HISTORY_AP_CLOSED,
    WIFI_HISTORY_NETWORK_HEALTH, WIFI_HISTORY_DISPLAY_TARGET,
    WIFI_HISTORY_DISPLAY_APPLIED, WIFI_HISTORY_WLAN_FATAL,
    WIFI_HISTORY_CONNECT_FAILED, WIFI_HISTORY_MANUAL_DISCONNECT,
    WIFI_HISTORY_FIRST_ARM, WIFI_HISTORY_EXPLICIT_SWITCH,
    WIFI_HISTORY_STATION_UP_EVENT, WIFI_HISTORY_STATION_DOWN_EVENT,
    WIFI_HISTORY_AP_MISSING_CONFIRMED, WIFI_HISTORY_AP_RESTORE_FAILED,
    WIFI_HISTORY_AP_CLOSE_SUPPRESSED, WIFI_HISTORY_AP_CLOSE_ATTEMPT,
    WIFI_HISTORY_AP_CLOSE_FAILED
} wifi_history_reason_t;

OSStatus wifi_manager_init(void);
void wifi_manager_set_initial_ap_state(int started);
void wifi_manager_note_recovery_activity(void);
OSStatus wifi_manager_apply_boot_settings(void);
void wifi_manager_settings_json(char *out,size_t capacity);
int wifi_manager_save_settings(const char *body,size_t length);
int wifi_manager_connect(const char *ssid, const char *password);
int wifi_manager_disconnect(void);
void wifi_manager_status_json(char *out, size_t out_size);
int wifi_manager_start_scan(void);
int wifi_manager_scan_snapshot(char state[12], unsigned *count);
int wifi_manager_scan_record_json(unsigned index,char *out,size_t capacity);
void wifi_manager_history_note(wifi_history_reason_t reason);
void wifi_manager_history_snapshot(uint32_t *oldest,uint32_t *newest,unsigned *count);
int wifi_manager_history_record_json(uint32_t sequence,char *out,size_t capacity);
int wifi_manager_station_ready(void);
int wifi_manager_station_link(void);
int wifi_manager_station_rssi(void);
void wifi_manager_station_ip(char out[16]);
void wifi_manager_station_dns(char out[16]);
int wifi_manager_control_running(void);
