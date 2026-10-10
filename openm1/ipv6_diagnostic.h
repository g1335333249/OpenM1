#pragma once
#include "mico.h"
#include <stddef.h>

/* Manual, one-shot experiment. No IPv6 operation runs during boot. */
#define IPV6_DIAGNOSTIC_WORKER_STACK 3072u
/* Experimental Kernel data path. Only explicit POST /probe/tcp may enter it. */
#define OPENM1_IPV6_TCP_EXPERIMENT_ENABLED 1
OSStatus ipv6_diagnostic_init(void);
int ipv6_diagnostic_start(void);
int ipv6_diagnostic_start_tcp(void);
void ipv6_diagnostic_status_json(char *out,size_t capacity);
