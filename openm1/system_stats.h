#pragma once
#include "mico.h"
#include <stddef.h>
#include <stdint.h>

#define SYSTEM_STATS_CPU_STACK 1024u
#define SYSTEM_STATS_CPU_PRIORITY 8u
#define SYSTEM_STATS_CPU_SAMPLE_MS 100u
#define SYSTEM_STATS_CPU_INTERVAL_MS 5000u

OSStatus system_stats_init(void);
void system_stats_json(char *out,size_t capacity);
unsigned system_stats_cpu_estimate(uint32_t current,uint32_t baseline);
