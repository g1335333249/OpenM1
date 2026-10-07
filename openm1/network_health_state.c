#include "network_health.h"

m1_net_display_state_t network_health_display_target(network_health_state_t state)
{
    if (state==NETWORK_NO_INTERNET) return M1_NET_DISPLAY_NO_INTERNET;
    if (state==NETWORK_NO_WIFI) return M1_NET_DISPLAY_DISCONNECTED;
    return M1_NET_DISPLAY_ONLINE; /* CHECKING means Station has link and IP. */
}

void network_health_step(network_health_snapshot_t *s,int has_wifi,int probe_result)
{
    if (!s) return;
    if (!has_wifi) {
        s->state=NETWORK_NO_WIFI;
        s->consecutive_successes=0;
        s->consecutive_failures=0;
        return;
    }
    if (s->state==NETWORK_NO_WIFI) {
        s->state=NETWORK_CHECKING;
        s->consecutive_successes=0;
        s->consecutive_failures=0;
    }
    if (probe_result<0) return;
    if (probe_result) {
        s->consecutive_failures=0;
        if (s->consecutive_successes<2) s->consecutive_successes++;
        if (s->consecutive_successes>=2) s->state=NETWORK_ONLINE;
    } else {
        s->consecutive_successes=0;
        if (s->consecutive_failures<2) s->consecutive_failures++;
        if (s->consecutive_failures>=2) s->state=NETWORK_NO_INTERNET;
    }
}
