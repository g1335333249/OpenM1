#include "network_health.h"
#include <assert.h>
#include <string.h>

int main(void)
{
    network_health_snapshot_t s;
    memset(&s,0,sizeof(s));
    network_health_step(&s,0,-1);
    assert(s.state==NETWORK_NO_WIFI);
    network_health_step(&s,1,-1);
    assert(s.state==NETWORK_CHECKING);
    network_health_step(&s,1,1);
    assert(s.state==NETWORK_CHECKING);
    network_health_step(&s,1,1);
    assert(s.state==NETWORK_ONLINE);
    network_health_step(&s,1,0);
    assert(s.state==NETWORK_ONLINE); /* one failed probe does not change icon target */
    network_health_step(&s,1,0);
    assert(s.state==NETWORK_NO_INTERNET);
    network_health_step(&s,1,1);
    assert(s.state==NETWORK_NO_INTERNET);
    network_health_step(&s,1,1);
    assert(s.state==NETWORK_ONLINE);
    network_health_step(&s,0,-1);
    assert(s.state==NETWORK_NO_WIFI && s.consecutive_successes==0 && s.consecutive_failures==0);
    return 0;
}
