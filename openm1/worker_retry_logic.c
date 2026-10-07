#include "worker_retry_logic.h"
uint32_t network_health_retry_remaining(uint32_t now, uint32_t failed_at, int has_failed)
{
    uint32_t elapsed=(uint32_t)(now-failed_at);
    return has_failed && elapsed<NETWORK_HEALTH_RETRY_MS?NETWORK_HEALTH_RETRY_MS-elapsed:0u;
}
int network_health_retry_due(uint32_t now, uint32_t failed_at, int has_failed)
{
    return !network_health_retry_remaining(now,failed_at,has_failed);
}
