#include "mico.h"
#include "mico_system.h"
#include "wifi_manager.h"

static const char *status = "connecting";

const char *wifi_manager_status(void) { return status; }

static void wifi_thread(mico_thread_arg_t arg)
{
    IPStatusTypedef ip;
    network_InitTypeDef_st ap;
    uint8_t mac[6] = {0};
    unsigned int elapsed;
    (void)arg;
    for (elapsed = 0; elapsed < 30; elapsed++) {
        memset(&ip, 0, sizeof(ip));
        if (micoWlanGetIPStatus(&ip, Station) == kNoErr && ip.ip[0] && strcmp(ip.ip, "0.0.0.0")) {
            status = "connected";
            mico_rtos_delete_thread(NULL);
            return;
        }
        mico_thread_sleep(1);
    }
    memset(&ap, 0, sizeof(ap));
    memset(&ip, 0, sizeof(ip));
    micoWlanGetIPStatus(&ip, Station);
    mico_wlan_get_mac_address(mac);
    snprintf(ap.wifi_ssid, sizeof(ap.wifi_ssid), "OpenM1-%02X%02X", mac[4], mac[5]);
    ap.wifi_mode = Soft_AP;
    ap.dhcpMode = DHCP_Server;
    strncpy(ap.local_ip_addr, "192.168.4.1", sizeof(ap.local_ip_addr) - 1);
    strncpy(ap.net_mask, "255.255.255.0", sizeof(ap.net_mask) - 1);
    strncpy(ap.gateway_ip_addr, "192.168.4.1", sizeof(ap.gateway_ip_addr) - 1);
    status = micoWlanStart(&ap) == kNoErr ? "softap" : "offline";
    mico_rtos_delete_thread(NULL);
}

void wifi_manager_start(void)
{
    mico_rtos_create_thread(NULL, MICO_APPLICATION_PRIORITY, "wifi", wifi_thread, 0x800, 0);
}
