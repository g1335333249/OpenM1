#include "wifi_station_logic.h"
#include "network_health.h"
#include <assert.h>
#include <string.h>

int main(void)
{
    wifi_station_samples_t samples={0,0};
    wifi_station_desired_t desired;
    network_health_snapshot_t health;
    unsigned i;
    const uint32_t expected[]={2000,5000,10000,20000,30000,30000};
    assert(WIFI_CONTROL_INTERVAL_MS==1000);
    assert(WIFI_CONTROL_WORKER_STACK==5120);
    assert(WIFI_NATIVE_RETRY_INTERVAL_MS==5000);
    assert(WIFI_NATIVE_RECONNECT_GRACE_MS==60000);
    assert(WIFI_BOOT_AUTO_CONNECT_GRACE_MS==60000);
    assert(WIFI_AP_PROBE_INTERVAL_MS==5000);
    assert(WIFI_AP_MISSING_THRESHOLD==3);
    assert(!wifi_station_note_good(&samples));
    assert(wifi_station_note_good(&samples));
    assert(!wifi_station_note_bad(&samples));
    assert(!wifi_station_note_bad(&samples));
    assert(wifi_station_note_bad(&samples));
    wifi_station_samples_reset(&samples);
    assert(!samples.good_samples && !samples.bad_samples);
    for (i=0;i<sizeof(expected)/sizeof(expected[0]);i++)
        assert(wifi_ap_restore_backoff_ms(i)==expected[i]);
    assert(!wifi_station_rearm_due(20000,1000,0));
    assert(!wifi_station_rearm_due(60000,1000,0));
    assert(wifi_station_rearm_due(61000,1000,0));
    assert(!wifi_station_rearm_due(62000,1000,30000));
    assert(wifi_station_rearm_due(90000,1000,30000));
    assert(!wifi_ap_close_eligible(1,1,1,1,0)); /* legacy true is ignored on .023 */
    assert(!wifi_ap_close_eligible(0,1,1,1,0));
    assert(!wifi_ap_close_eligible(1,0,1,1,0));
    assert(!wifi_ap_close_eligible(1,1,0,1,0));
    assert(!wifi_ap_close_eligible(1,1,1,0,0));
    assert(!wifi_ap_close_eligible(1,1,1,1,1));
    /* One hour of healthy STA samples with a legacy saved close flag must
     * neither qualify AP close nor manufacture a Station loss. */
    wifi_station_samples_reset(&samples);
    for (i=0;i<3600;i++) {
        assert(wifi_station_note_good(&samples) == (i!=0));
        assert(!wifi_ap_close_eligible(1,1,1,1,0));
        assert(!samples.bad_samples);
    }
    assert(!wifi_ap_close_timing_ready(119999,60000,0));
    assert(!wifi_ap_close_timing_ready(120000,100000,0));
    assert(wifi_ap_close_timing_ready(130000,100000,0));
    assert(!wifi_ap_close_timing_ready(130000,100000,150000));
    assert(wifi_ap_close_timing_ready(150000,100000,150000));
    assert(!wifi_recovery_ap_needs_restore(0,0,1));
    assert(wifi_recovery_ap_needs_restore(0,0,0));
    assert(!wifi_recovery_ap_needs_restore(1,1,0));
    assert(wifi_recovery_ap_needs_restore(0,1,0));
    assert(wifi_ap_missing_after_probe(0,0)==1);
    assert(wifi_ap_missing_after_probe(1,0)==2);
    assert(wifi_ap_missing_after_probe(2,0)==3);
    assert(wifi_ap_missing_after_probe(3,0)==3);
    assert(wifi_ap_missing_after_probe(3,1)==0);
    assert(!wifi_ap_restore_confident(15000,5000,1,0));
    assert(!wifi_ap_restore_confident(15000,5000,2,0));
    assert(!wifi_ap_restore_confident(19999,5000,3,0));
    assert(wifi_ap_restore_confident(20000,5000,3,0));
    assert(wifi_ap_restore_confident(6000,5000,1,1));
    wifi_station_desire(&desired,"AutoSSID","auto-secret",WIFI_DESIRED_AUTO);
    assert(desired.want_connected && desired.source==WIFI_DESIRED_AUTO);
    wifi_station_drop_auto_desired(&desired);
    assert(!desired.want_connected && !desired.password[0]);
    wifi_station_desire(&desired,"ManualSSID","manual-secret",WIFI_DESIRED_MANUAL);
    wifi_station_drop_auto_desired(&desired);
    assert(desired.want_connected && desired.source==WIFI_DESIRED_MANUAL);
    wifi_station_manual_disconnect(&desired);
    assert(!desired.want_connected && desired.manual_disconnect_latched && !desired.password[0]);
    memset(&health,0,sizeof(health));
    health.state=NETWORK_NO_INTERNET;
    health.consecutive_failures=2;
    network_health_step(&health,0,-1);
    assert(health.state==NETWORK_NO_WIFI);
    return 0;
}
