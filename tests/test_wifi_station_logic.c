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
    assert(WIFI_STATION_MONITOR_INTERVAL_MS==1000);
    assert(!wifi_station_note_good(&samples));
    assert(wifi_station_note_good(&samples)); /* only two stable IP samples are ready */
    assert(!wifi_station_note_bad(&samples)); /* first bad sample stays connected */
    assert(!wifi_station_note_bad(&samples)); /* second bad sample stays connected */
    assert(wifi_station_note_bad(&samples));  /* third bad sample means LOST */
    wifi_station_samples_reset(&samples);
    assert(!samples.good_samples && !samples.bad_samples);
    for (i=0;i<sizeof(expected)/sizeof(expected[0]);i++)
        assert(wifi_station_backoff_ms(i)==expected[i]);
    assert(!wifi_station_should_cleanup(0,0)); /* cold boot: StartNetwork directly */
    assert(!wifi_station_should_cleanup(0,1)); /* never suspend before first start */
    assert(!wifi_station_cleanup_for_next_attempt(WIFI_STATION_FIRST_START));
    assert(!wifi_station_cleanup_for_next_attempt(WIFI_STATION_LINK_LOST));
    assert(wifi_station_cleanup_for_next_attempt(WIFI_STATION_START_ERROR));
    assert(wifi_station_cleanup_for_next_attempt(WIFI_STATION_CONNECT_TIMEOUT));
    assert(!wifi_station_should_cleanup(1,
        wifi_station_cleanup_for_next_attempt(WIFI_STATION_LINK_LOST)));
    assert(wifi_station_should_cleanup(1,
        wifi_station_cleanup_for_next_attempt(WIFI_STATION_START_ERROR)));
    assert(wifi_station_should_cleanup(1,
        wifi_station_cleanup_for_next_attempt(WIFI_STATION_CONNECT_TIMEOUT)));
    assert(!wifi_recovery_ap_needs_restore(0,0,1)); /* AP required and observed */
    assert(wifi_recovery_ap_needs_restore(0,0,0)); /* AP missing */
    assert(!wifi_recovery_ap_needs_restore(1,1,0)); /* intentional close */
    assert(wifi_recovery_ap_needs_restore(0,1,0)); /* Station lost */
    assert(wifi_recovery_ap_needs_restore(1,0,0)); /* missing before planned close */
    assert(wifi_station_backoff_ms(100)==30000);
    assert(wifi_station_phase_after_loss(1)==STATION_BACKOFF);
    assert(wifi_station_phase_after_loss(0)==STATION_IDLE);
    i=4; wifi_station_reset_backoff(&i); assert(i==0);
    wifi_station_desire(&desired,"AutoSSID","auto-secret",WIFI_DESIRED_AUTO);
    assert(desired.want_connected && desired.source==WIFI_DESIRED_AUTO);
    wifi_station_drop_auto_desired(&desired);
    assert(!desired.want_connected && desired.source==WIFI_DESIRED_NONE && !desired.password[0]);
    wifi_station_desire(&desired,"ManualSSID","manual-secret",WIFI_DESIRED_MANUAL);
    wifi_station_drop_auto_desired(&desired);
    assert(desired.want_connected && desired.source==WIFI_DESIRED_MANUAL);
    wifi_station_manual_disconnect(&desired);
    assert(!desired.want_connected && desired.manual_disconnect_latched && !desired.password[0]);
    memset(&health,0,sizeof(health));
    health.state=NETWORK_NO_INTERNET;
    health.consecutive_failures=2;
    network_health_step(&health,0,-1);
    assert(health.state==NETWORK_NO_WIFI); /* display mapping is blink, red X off */
    return 0;
}
