#include "mico.h"
#include "wifi_manager.h"
#include "device_info.h"

void device_info_json(char *out, size_t capacity)
{
    IPStatusTypedef ip;
    LinkStatusTypeDef link;
    memset(&ip, 0, sizeof(ip));
    memset(&link, 0, sizeof(link));
    micoWlanGetIPStatus(&ip, Station);
    micoWlanGetLinkStatus(&link);
    if (!ip.ip[0] || !strcmp(ip.ip, "0.0.0.0")) micoWlanGetIPStatus(&ip, Soft_AP);
    snprintf(out, capacity, "{\"name\":\"OpenM1\",\"version\":\"0.0.1\",\"board\":\"MK3080B\",\"mac\":\"%.15s\",\"ip\":\"%.15s\",\"ssid\":\"%.31s\",\"wifi_status\":\"%s\",\"uptime\":%lu}",
             ip.mac, ip.ip, (char *)link.ssid, wifi_manager_status(), (unsigned long)(mico_rtos_get_time() / 1000));
}
