#include "recovery.h"
#include <stdio.h>

/* Written once during startup, before HTTP and STA threads are created. */
static char device_ssid[32] = RECOVERY_FALLBACK_SSID;
static char device_mac[18] = "unknown";

void recovery_set_identity(const char *ssid, const char *mac)
{
    if (ssid && ssid[0])
        snprintf(device_ssid, sizeof(device_ssid), "%s", ssid);
    if (mac && mac[0])
        snprintf(device_mac, sizeof(device_mac), "%s", mac);
}

const char *recovery_ssid(void) { return device_ssid; }
const char *recovery_mac(void) { return device_mac; }
