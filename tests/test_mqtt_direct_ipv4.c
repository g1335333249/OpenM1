#include "mqtt_direct_ipv4.h"
#include "mico_socket.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    struct sockaddr_in address;
    socklen_t length=sizeof(address);
    mqtt_failure_stage_t stage=MQTT_STAGE_NONE;
    int listener,fd,peer,i;
    listener=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);
    assert(listener>=0);
    memset(&address,0,sizeof(address));
    address.sin_family=AF_INET;
    address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
    address.sin_port=0;
    assert(bind(listener,(struct sockaddr *)&address,sizeof(address))==0);
    assert(getsockname(listener,(struct sockaddr *)&address,&length)==0);
    assert(listen(listener,1)==0);
    assert(mqtt_direct_ipv4_open("127.0.0.1",ntohs(address.sin_port),&fd,&stage)==0);
    assert(fd>=0);
    peer=accept(listener,NULL,NULL);
    assert(peer>=0);
    close(peer);close(fd);close(listener);
    /* Repeated refused connections must leave no live MQTT descriptor. */
    for (i=0;i<8;i++) {
        fd=123;
        assert(mqtt_direct_ipv4_open("127.0.0.1",ntohs(address.sin_port),&fd,&stage)<0);
        assert(fd==-1 && stage==MQTT_STAGE_TCP_CONNECT);
    }
    assert(mqtt_direct_ipv4_open("2001:db8::1",1883,&fd,&stage)<0);
    assert(fd==-1 && stage==MQTT_STAGE_DIRECT_IPV6);
    assert(mqtt_direct_ipv4_open("",1883,&fd,&stage)<0);
    assert(fd==-1 && stage==MQTT_STAGE_DNS);
    puts("MQTT_DIRECT_IPV4_PASS");
    return 0;
}
