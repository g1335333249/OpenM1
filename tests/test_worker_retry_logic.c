#include "worker_retry_logic.h"
#include <assert.h>
#include <stdio.h>
int main(void)
{
    uint32_t failed_at=1000;
    int worker_created=0,attempts=0,has_failed=0;
    /* Simulated RTOS create failure, bounded delay, then successful retry. */
    if (network_health_retry_due(1000,failed_at,has_failed)) {
        attempts++;
        has_failed=1;failed_at=1000;
    }
    assert(attempts==1 && !worker_created);
    if (network_health_retry_due(30999,failed_at,has_failed)) attempts++;
    assert(attempts==1);
    if (network_health_retry_due(31000,failed_at,has_failed)) {
        attempts++;worker_created=1;has_failed=0;
    }
    assert(attempts==2 && worker_created);
    assert(network_health_retry_due(1000,failed_at,0));
    assert(!network_health_retry_due(2000,failed_at,1));
    assert(network_health_retry_remaining(2000,failed_at,1)==29000u);
    assert(!network_health_retry_due(30999,failed_at,1));
    assert(network_health_retry_due(31000,failed_at,1));
    assert(!network_health_retry_due(999u,UINT32_MAX-1000u,1));
    puts("WORKER_RETRY_LOGIC_PASS");
}
