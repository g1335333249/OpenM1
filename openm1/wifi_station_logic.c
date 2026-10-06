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
uint32_t wifi_ap_restore_backoff_ms(unsigned failure_index)
{
    static const uint32_t values[]={2000u,5000u,10000u,20000u,30000u};
    if (failure_index>=sizeof(values)/sizeof(values[0])) failure_index=4;
    return values[failure_index];
}
int wifi_station_rearm_due(uint32_t now,uint32_t wait_since,uint32_t last_rearm)
{
    return (uint32_t)(now-wait_since)>=WIFI_NATIVE_RECONNECT_GRACE_MS &&
           (!last_rearm || (uint32_t)(now-last_rearm)>=WIFI_CONTROLLED_REARM_MIN_INTERVAL_MS);
}
int wifi_ap_close_eligible(int auto_connect,int disable_after_connect,
                           int station_ready,int ssid_match,int ota_busy)
{
    return auto_connect && disable_after_connect && station_ready && ssid_match && !ota_busy;
}
int wifi_ap_close_timing_ready(uint32_t now,uint32_t ready_since,uint32_t next_close_ms)
{
    return ready_since && now>=WIFI_AP_BOOT_FAILSAFE_MS &&
           (uint32_t)(now-ready_since)>=WIFI_AP_STABLE_BEFORE_CLOSE_MS &&
           (!next_close_ms || (int32_t)(now-next_close_ms)>=0);
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
int wifi_recovery_ap_needs_restore(int close_eligible,int policy_closed,int observed_on)
{
    return !observed_on && (!close_eligible || !policy_closed);
}
