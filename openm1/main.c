#include "mico.h"
#include "mico_wlan.h"

int main(void)
{
    network_InitTypeDef_st wifi_config;
    char rf_version[64] = {0};
    OSStatus result;
    unsigned long counter = 0;

    printf("================================\r\n"
           "OpenM1 BootProbe\r\n"
           "Version: 0.0.3\r\n"
           "Board: MK3080B\r\n"
           "Kernel target: 3080B002.023\r\n"
           "================================\r\n");
    printf("BOOT: main entered\r\n");
    printf("BOOT: calling MicoInit\r\n");
    result = MicoInit();
    printf("BOOT: MicoInit result = %d\r\n", result);
    mico_thread_msleep(500);

    printf("BOOT: reading RF version\r\n");
    MicoGetRfVer(rf_version, sizeof(rf_version));
    rf_version[sizeof(rf_version) - 1] = '\0';
    printf("BOOT: RF version = %s\r\n", rf_version[0] ? rf_version : "(unavailable)");

    printf("BOOT: calling micoWlanPowerOn\r\n");
    result = micoWlanPowerOn();
    printf("BOOT: micoWlanPowerOn result = %d\r\n", result);
    mico_thread_msleep(500);

    printf("BOOT: preparing SoftAP\r\n");
    memset(&wifi_config, 0, sizeof(wifi_config));
    wifi_config.wifi_mode = Soft_AP;
    memcpy(wifi_config.wifi_ssid, "OpenM1-Recovery", sizeof("OpenM1-Recovery"));
    memcpy(wifi_config.local_ip_addr, "192.168.4.1", sizeof("192.168.4.1"));
    memcpy(wifi_config.net_mask, "255.255.255.0", sizeof("255.255.255.0"));
    memcpy(wifi_config.gateway_ip_addr, "192.168.4.1", sizeof("192.168.4.1"));
    memcpy(wifi_config.dnsServer_ip_addr, "192.168.4.1", sizeof("192.168.4.1"));
    wifi_config.dhcpMode = DHCP_Server;
    printf("BOOT: SoftAP SSID = OpenM1-Recovery\r\n");
    printf("BOOT: SoftAP IP = 192.168.4.1\r\n");
    printf("BOOT: SoftAP DHCP = Server\r\n");
    printf("BOOT: calling StartNetwork\r\n");
    result = StartNetwork(&wifi_config);
    printf("BOOT: StartNetwork result = %d\r\n", result);
    printf("BOOT: SoftAP requested\r\n");

    for (;;) {
        mico_thread_msleep(2000);
        printf("OPENM1 ALIVE %lu\r\n", ++counter);
    }
}
