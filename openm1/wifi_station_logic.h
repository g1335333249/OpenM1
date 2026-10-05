#pragma once
#include <stdint.h>

#define WIFI_CONTROL_INTERVAL_MS 1000u
#define WIFI_CONTROL_WORKER_STACK 5120u
#define WIFI_NATIVE_RETRY_INTERVAL_MS 5000u
#define WIFI_NATIVE_RECONNECT_GRACE_MS 60000u
#define WIFI_CONTROLLED_REARM_MIN_INTERVAL_MS 60000u
#define WIFI_STATION_LOSS_THRESHOLD 3u
#define WIFI_STATION_READY_THRESHOLD 2u
#define WIFI_BOOT_AUTO_CONNECT_GRACE_MS 2000u

typedef enum { WIFI_DESIRED_NONE, WIFI_DESIRED_MANUAL, WIFI_DESIRED_AUTO } wifi_desired_source_t;
typedef enum {
    WIFI_STA_IDLE, WIFI_STA_ARMED_CONNECTING, WIFI_STA_CONNECTED,
    WIFI_STA_NATIVE_RECONNECT_WAIT, WIFI_STA_REARM_WAIT
} wifi_station_phase_t;
typedef struct { uint8_t good_samples,bad_samples; } wifi_station_samples_t;
typedef struct {
    char ssid[32];
    char password[64];
    wifi_desired_source_t source;
    uint8_t want_connected;
    uint8_t manual_disconnect_latched;
} wifi_station_desired_t;

int wifi_station_note_good(wifi_station_samples_t *samples);
int wifi_station_note_bad(wifi_station_samples_t *samples);
void wifi_station_samples_reset(wifi_station_samples_t *samples);
uint32_t wifi_ap_restore_backoff_ms(unsigned failure_index);
int wifi_station_rearm_due(uint32_t now,uint32_t wait_since,uint32_t last_rearm);
int wifi_ap_close_eligible(int auto_connect,int disable_after_connect,
                           int station_ready,int ssid_match,int ota_busy);
void wifi_station_desire(wifi_station_desired_t *desired,const char *ssid,
                         const char *password,wifi_desired_source_t source);
void wifi_station_manual_disconnect(wifi_station_desired_t *desired);
void wifi_station_drop_auto_desired(wifi_station_desired_t *desired);
int wifi_recovery_ap_needs_restore(int close_eligible,int policy_closed,int observed_on);
