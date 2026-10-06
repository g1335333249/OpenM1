#include "recovery.h"
#include <stdio.h>

/* Written once during startup, before HTTP and STA threads are created. */
static char device_ssid[32] = RECOVERY_FALLBACK_SSID;
static char device_mac[18] = "unknown";
/* MiCO may retain this pointer for later DHCP requests. */
static char device_hostname[32] = "OpenM1";

void recovery_set_identity(const char *ssid, const char *mac)
{
    if (ssid && ssid[0])
        snprintf(device_ssid, sizeof(device_ssid), "%s", ssid);
    if (mac && mac[0])
        snprintf(device_mac, sizeof(device_mac), "%s", mac);
}

const char *recovery_ssid(void) { return device_ssid; }
const char *recovery_mac(void) { return device_mac; }
void recovery_prepare_hostname(const uint8_t mac[6], int mac_valid)
{
    if (mac_valid && mac)
        snprintf(device_hostname,sizeof(device_hostname),"OpenM1-%02X%02X%02X",
                 mac[3],mac[4],mac[5]);
    else
        snprintf(device_hostname,sizeof(device_hostname),"OpenM1");
}
char *recovery_hostname(void) { return device_hostname; }
