#pragma once
#include <stdint.h>

#define WIFI_STATION_MONITOR_INTERVAL_MS 1000u
#define WIFI_STATION_LOSS_THRESHOLD 3u
#define WIFI_STATION_READY_THRESHOLD 2u
#define WIFI_STATION_SUPERVISOR_STACK 4096u

typedef enum {
    WIFI_DESIRED_NONE,
    WIFI_DESIRED_MANUAL,
    WIFI_DESIRED_AUTO
} wifi_desired_source_t;
typedef enum { STATION_IDLE,STATION_CONNECTING,STATION_CONNECTED,STATION_BACKOFF } station_phase_t;

typedef struct {
    uint8_t good_samples;
    uint8_t bad_samples;
} wifi_station_samples_t;

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
uint32_t wifi_station_backoff_ms(unsigned failure_index);
station_phase_t wifi_station_phase_after_loss(int want_connected);
void wifi_station_reset_backoff(unsigned *failure_index);
void wifi_station_desire(wifi_station_desired_t *desired,const char *ssid,
                         const char *password,wifi_desired_source_t source);
void wifi_station_manual_disconnect(wifi_station_desired_t *desired);
void wifi_station_drop_auto_desired(wifi_station_desired_t *desired);
