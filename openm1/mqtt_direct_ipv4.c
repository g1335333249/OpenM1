#include "mqtt_direct_ipv4.h"
#include "mico_socket.h"
#include <string.h>

int mqtt_direct_ipv4_open(const char *name,uint16_t port,int *fd,
                          mqtt_failure_stage_t *failure_stage)
{
    struct hostent *host;
    struct sockaddr_in address;
    if (!fd || !failure_stage) return -1;
    *fd=-1;
    if (!name || strchr(name,':')) {
        *failure_stage=MQTT_STAGE_DIRECT_IPV6;return -1;
    }
    host=gethostbyname(name);
    if (!host || !host->h_addr_list || !host->h_addr_list[0]) {
        *failure_stage=MQTT_STAGE_DNS;return -1;
    }
    memset(&address,0,sizeof(address));
    address.sin_family=AF_INET;
    address.sin_port=htons(port);
    memcpy(&address.sin_addr,host->h_addr_list[0],sizeof(address.sin_addr));
    *fd=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);
    if (*fd<0) { *failure_stage=MQTT_STAGE_TCP_SOCKET;return -1; }
    if (connect(*fd,(struct sockaddr *)&address,sizeof(address))!=0) {
        *failure_stage=MQTT_STAGE_TCP_CONNECT;
        close(*fd);*fd=-1;return -1;
    }
    return 0;
}
