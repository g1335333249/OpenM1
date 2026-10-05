#include "mico.h"
#include "mico_wlan.h"
#include "recovery.h"
#include "wifi_manager.h"
#include "m1_uart.h"
#include "config_store.h"
#include "mqtt_manager.h"
#include "m1_display.h"
#include "network_health.h"
#include "system_stats.h"

int main(void)
{
    network_InitTypeDef_st wifi_config;
    mico_Context_t *context;
    char rf_version[64] = {0};
    uint8_t mac[6] = {0};
    char recovery_ssid[32] = RECOVERY_FALLBACK_SSID;
    char mac_text[18] = {0};
    OSStatus result;
    unsigned long counter = 0;
    micoMemInfo_t *memory;

    printf("================================\r\n"
           "OpenM1\r\n"
           "Version: 0.6.0\r\n"
           "Board: MK3080B\r\n"
           "Kernel: 3080B002.023\r\n"
           "================================\r\n");
    printf("RECOVERY: main entered\r\n");
    printf("RECOVERY: system context init\r\n");
    context = mico_system_context_init(sizeof(openm1_config_t));
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

    /* The MOC wrapper returns void. Reject clearly invalid/uninitialized MACs. */
    mico_wlan_get_mac_address(mac);
    if (memcmp(mac, "\0\0\0\0\0\0", sizeof(mac)) != 0 &&
        memcmp(mac, "\xff\xff\xff\xff\xff\xff", sizeof(mac)) != 0 &&
        (mac[0] & 1u) == 0) {
        snprintf(recovery_ssid, sizeof(recovery_ssid), "OpenM1-%02X%02X%02X",
                 mac[3], mac[4], mac[5]);
        snprintf(mac_text, sizeof(mac_text), "%02X:%02X:%02X:%02X:%02X:%02X",
                 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        printf("RECOVERY: MAC = %s\r\n", mac_text);
    } else {
        printf("RECOVERY: MAC read failed, using fallback SSID\r\n");
    }
    recovery_set_identity(recovery_ssid, mac_text);
    printf("RECOVERY: SSID = %s\r\n", recovery_ssid);

    printf("RECOVERY: starting SoftAP\r\n");
    memset(&wifi_config, 0, sizeof(wifi_config));
    wifi_config.wifi_mode = Soft_AP;
    snprintf(wifi_config.wifi_ssid, sizeof(wifi_config.wifi_ssid), "%s", recovery_ssid);
    memcpy(wifi_config.local_ip_addr, RECOVERY_IP, sizeof(RECOVERY_IP));
    memcpy(wifi_config.net_mask, "255.255.255.0", sizeof("255.255.255.0"));
    memcpy(wifi_config.gateway_ip_addr, RECOVERY_IP, sizeof(RECOVERY_IP));
    memcpy(wifi_config.dnsServer_ip_addr, RECOVERY_IP, sizeof(RECOVERY_IP));
    wifi_config.dhcpMode = DHCP_Server;
    printf("RECOVERY: SoftAP SSID = %s\r\n", recovery_ssid);
    printf("RECOVERY: calling StartNetwork\r\n");
    result = StartNetwork(&wifi_config);
    printf("RECOVERY: StartNetwork result = %d\r\n", result);
    if (result == kNoErr) {
        printf("RECOVERY: SoftAP ready\r\n");
        printf("RECOVERY: WiFi ready\r\n");
        printf("RECOVERY: IP = %s\r\n", RECOVERY_IP);
        recovery_ota_partition_log();
        result=recovery_http_start();
        if (result != kNoErr)
            printf("RECOVERY: HTTP server failed to start\r\n");
        else if (wifi_manager_init() != kNoErr)
            printf("WIFI: manager initialization failed\r\n");
        if (result==kNoErr) {
            while (!recovery_http_ready()) mico_thread_msleep(100);
            if (config_store_init()!=kNoErr)
                printf("CONFIG: persistence unavailable; Recovery remains active\r\n");
            if (m1_display_init()!=kNoErr)
                printf("DISPLAY: initialization failed; Recovery remains active\r\n");
            if (m1_uart_init() != kNoErr)
                printf("SENSOR: UART diagnostic initialization failed; Recovery remains active\r\n");
            if (network_health_init()!=kNoErr)
                printf("NETWORK: health monitor unavailable; Recovery remains active\r\n");
            if (wifi_manager_apply_boot_settings()!=kNoErr)
                printf("WIFI: boot policy unavailable; Recovery remains active\r\n");
            if (system_stats_init()!=kNoErr)
                printf("STATS: sampler unavailable; Recovery remains active\r\n");
            if (mqtt_manager_init()!=kNoErr)
                printf("MQTT: manager initialization failed; Recovery remains active\r\n");
        }
    }
    for (;;) {
        mico_thread_msleep(10000);
        memory = MicoGetMemoryInfo();
        printf("RECOVERY: alive %lu, free heap = %d\r\n", ++counter,
               memory ? memory->free_memory : -1);
    }
}
