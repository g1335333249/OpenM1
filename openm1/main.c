#include "mico.h"
#include "mico_wlan.h"

int main(void)
{
    network_InitTypeDef_st ap = {0};
    OSStatus result;
    unsigned long counter = 0;

    printf("================================\r\n"
           "OpenM1 BootProbe\r\n"
           "Version: 0.0.2\r\n"
           "Board: MK3080B\r\n"
           "Kernel target: 3080B002.023\r\n"
           "================================\r\n");
    printf("BOOT: main entered\r\n");
    mico_thread_msleep(1000);
    printf("BOOT: calling micoWlanPowerOn\r\n");
    result = micoWlanPowerOn();
    printf("BOOT: micoWlanPowerOn result = %ld\r\n", (long)result);
    mico_thread_msleep(500);

    printf("BOOT: preparing SoftAP\r\n");
    ap.wifi_mode = Soft_AP;
    memcpy(ap.wifi_ssid, "OpenM1-Recovery", sizeof("OpenM1-Recovery"));
    memcpy(ap.local_ip_addr, "192.168.4.1", sizeof("192.168.4.1"));
    memcpy(ap.net_mask, "255.255.255.0", sizeof("255.255.255.0"));
    memcpy(ap.gateway_ip_addr, "192.168.4.1", sizeof("192.168.4.1"));
    ap.dhcpMode = DHCP_Server;
    printf("BOOT: calling StartNetwork\r\n");
    result = StartNetwork(&ap);
    printf("BOOT: StartNetwork result = %ld\r\n", (long)result);
    printf("BOOT: SoftAP requested\r\n");

    for (;;) {
        mico_thread_msleep(2000);
        printf("OPENM1 ALIVE %lu\r\n", ++counter);
    }
}
