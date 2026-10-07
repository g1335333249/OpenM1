#pragma once
#include <stdint.h>
#define NETWORK_HEALTH_RETRY_MS 30000u
int network_health_retry_due(uint32_t now, uint32_t failed_at, int has_failed);
uint32_t network_health_retry_remaining(uint32_t now, uint32_t failed_at, int has_failed);
