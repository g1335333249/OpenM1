#include "wifi_station_logic.h"
#include <string.h>

int wifi_station_note_good(wifi_station_samples_t *samples)
{
    if (!samples) return 0;
    samples->bad_samples=0;
    if (samples->good_samples<WIFI_STATION_READY_THRESHOLD) samples->good_samples++;
    return samples->good_samples>=WIFI_STATION_READY_THRESHOLD;
}
int wifi_station_note_bad(wifi_station_samples_t *samples)
{
    if (!samples) return 0;
    samples->good_samples=0;
    if (samples->bad_samples<WIFI_STATION_LOSS_THRESHOLD) samples->bad_samples++;
    return samples->bad_samples>=WIFI_STATION_LOSS_THRESHOLD;
}
void wifi_station_samples_reset(wifi_station_samples_t *samples)
{
    if (samples) memset(samples,0,sizeof(*samples));
}
uint32_t wifi_station_backoff_ms(unsigned failure_index)
{
    static const uint32_t values[]={2000u,5000u,10000u,20000u,30000u};
    if (failure_index>=sizeof(values)/sizeof(values[0]))
        failure_index=sizeof(values)/sizeof(values[0])-1;
    return values[failure_index];
}
station_phase_t wifi_station_phase_after_loss(int want_connected)
{
    return want_connected?STATION_BACKOFF:STATION_IDLE;
}
void wifi_station_reset_backoff(unsigned *failure_index)
{
    if (failure_index) *failure_index=0;
}
void wifi_station_desire(wifi_station_desired_t *desired,const char *ssid,
                         const char *password,wifi_desired_source_t source)
{
    if (!desired || !ssid || !password) return;
    memset(desired,0,sizeof(*desired));
    strncpy(desired->ssid,ssid,sizeof(desired->ssid)-1);
    strncpy(desired->password,password,sizeof(desired->password)-1);
    desired->source=source;
    desired->want_connected=1;
}
void wifi_station_manual_disconnect(wifi_station_desired_t *desired)
{
    if (!desired) return;
    memset(desired,0,sizeof(*desired));
    desired->manual_disconnect_latched=1;
}
void wifi_station_drop_auto_desired(wifi_station_desired_t *desired)
{
    if (!desired || desired->source!=WIFI_DESIRED_AUTO) return;
    memset(desired,0,sizeof(*desired));
}
int wifi_station_cleanup_for_next_attempt(wifi_station_attempt_event_t event)
{
    return event==WIFI_STATION_START_ERROR || event==WIFI_STATION_CONNECT_TIMEOUT;
}
int wifi_station_should_cleanup(int station_started_once,int cleanup_required)
{
    return station_started_once && cleanup_required;
}
int wifi_recovery_ap_needs_restore(int close_eligible,int policy_closed,int observed_on)
{
    return !observed_on && (!close_eligible || !policy_closed);
}
