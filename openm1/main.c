#include "mico.h"
#include "mico_wlan.h"
#include "recovery.h"
#include "wifi_manager.h"

int main(void)
{
    network_InitTypeDef_st wifi_config;
    mico_Context_t *context;
    char rf_version[64] = {0};
    OSStatus result;
    unsigned long counter = 0;
    micoMemInfo_t *memory;

    printf("================================\r\n"
           "OpenM1\r\n"
           "Version: 0.2.0\r\n"
           "Board: MK3080B\r\n"
           "Kernel: 3080B002.023\r\n"
           "================================\r\n");
    printf("RECOVERY: main entered\r\n");
    printf("RECOVERY: system context init\r\n");
    context = mico_system_context_init(0);
    if (context == NULL) {
        printf("RECOVERY: system context failed\r\n");
        return -1;
    }
    printf("RECOVERY: system context initialized\r\n");

    /* Preserve the v0.0.3 hardware verified Wi-Fi sequence and delays. */
    printf("RECOVERY: MicoInit\r\n");
    result = MicoInit();
    printf("RECOVERY: MicoInit result = %d\r\n", result);
    mico_thread_msleep(500);
    MicoGetRfVer(rf_version, sizeof(rf_version));
    rf_version[sizeof(rf_version) - 1] = '\0';
    recovery_set_rf(rf_version);
    printf("RECOVERY: RF = %s\r\n", recovery_rf());
    printf("RECOVERY: calling micoWlanPowerOn\r\n");
    result = micoWlanPowerOn();
    printf("RECOVERY: micoWlanPowerOn result = %d\r\n", result);
    mico_thread_msleep(500);

    printf("RECOVERY: starting SoftAP\r\n");
    memset(&wifi_config, 0, sizeof(wifi_config));
    wifi_config.wifi_mode = Soft_AP;
    memcpy(wifi_config.wifi_ssid, RECOVERY_SSID, sizeof(RECOVERY_SSID));
    memcpy(wifi_config.local_ip_addr, RECOVERY_IP, sizeof(RECOVERY_IP));
    memcpy(wifi_config.net_mask, "255.255.255.0", sizeof("255.255.255.0"));
    memcpy(wifi_config.gateway_ip_addr, RECOVERY_IP, sizeof(RECOVERY_IP));
    memcpy(wifi_config.dnsServer_ip_addr, RECOVERY_IP, sizeof(RECOVERY_IP));
    wifi_config.dhcpMode = DHCP_Server;
    printf("RECOVERY: SoftAP SSID = %s\r\n", RECOVERY_SSID);
    printf("RECOVERY: calling StartNetwork\r\n");
    result = StartNetwork(&wifi_config);
    printf("RECOVERY: StartNetwork result = %d\r\n", result);
    if (result == kNoErr) {
        printf("RECOVERY: SoftAP ready\r\n");
        printf("RECOVERY: WiFi ready\r\n");
        printf("RECOVERY: IP = %s\r\n", RECOVERY_IP);
        recovery_ota_partition_log();
        if (recovery_http_start() != kNoErr)
            printf("RECOVERY: HTTP server failed to start\r\n");
        else if (wifi_manager_init() != kNoErr)
            printf("WIFI: manager initialization failed\r\n");
    }
    for (;;) {
        mico_thread_msleep(10000);
        memory = MicoGetMemoryInfo();
        printf("RECOVERY: alive %lu, free heap = %d\r\n", ++counter,
               memory ? memory->free_memory : -1);
    }
}
