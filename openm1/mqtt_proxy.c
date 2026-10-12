#include "mqtt_proxy.h"
#include "socks5_protocol.h"
#include "recovery.h"
#include "mico_socket.h"
#include <errno.h>
#include <string.h>

#define PROXY_PHASE_TIMEOUT_MS 3000u
const char *mqtt_proxy_stage_name(mqtt_proxy_stage_t stage)
{
    switch (stage) {
    case PROXY_STAGE_DNS:return "proxy_dns";
    case PROXY_STAGE_TCP:return "proxy_tcp_connect";
    case PROXY_STAGE_METHOD:return "proxy_method";
    case PROXY_STAGE_AUTH:return "proxy_auth";
    case PROXY_STAGE_TARGET:return "proxy_target_connect";
    case PROXY_STAGE_TIMEOUT:return "proxy_timeout";
    case PROXY_STAGE_PROTOCOL:return "proxy_protocol_error";
    case PROXY_STAGE_DIRECT_IPV6:return "direct_ipv6_unsupported";
    default:return "none";
    }
}
static int wait_socket(int fd,int writing,uint32_t deadline)
{
    fd_set rd,wr,ex;
    struct timeval timeout;
    uint32_t remaining;
    int result;
    for (;;) {
        if (recovery_ota_busy()) return -3;
        remaining=deadline-mico_rtos_get_time();
        if ((int32_t)remaining<=0) return 0;
        if (remaining>250u) remaining=250u;
        timeout.tv_sec=0;timeout.tv_usec=remaining*1000u;
        FD_ZERO(&rd);FD_ZERO(&wr);FD_ZERO(&ex);
        if (writing) FD_SET(fd,&wr); else FD_SET(fd,&rd);
        FD_SET(fd,&ex);
        result=select(fd+1,writing?NULL:&rd,writing?&wr:NULL,&ex,&timeout);
        if (result<0 && errno==EINTR) continue;
        if (result<=0) {if (result<0) return -1;continue;}
        if (FD_ISSET(fd,&ex)) return -1;
        if (!FD_ISSET(fd,writing?&wr:&rd)) return -1;
        return 1;
    }
}
static int send_exact(int fd,const uint8_t *buffer,size_t length)
{
    uint32_t deadline=mico_rtos_get_time()+PROXY_PHASE_TIMEOUT_MS;
    size_t sent=0;
    while (sent<length) {
        int result=wait_socket(fd,1,deadline),n;
        if (result<=0) return result;
        n=send(fd,buffer+sent,length-sent,0);
        if (n>0) sent+=(size_t)n;
        else if (n==0) return -1;
        else if (errno!=EAGAIN && errno!=EWOULDBLOCK && errno!=EINTR) return -1;
    }
    return 1;
}
static int recv_exact(int fd,uint8_t *buffer,size_t length)
{
    uint32_t deadline=mico_rtos_get_time()+PROXY_PHASE_TIMEOUT_MS;
    size_t got=0;
    while (got<length) {
        int result=wait_socket(fd,0,deadline),n;
        if (result<=0) return result;
        n=recv(fd,buffer+got,length-got,0);
        if (n>0) got+=(size_t)n;
        else if (n==0) return -1;
        else if (errno!=EAGAIN && errno!=EWOULDBLOCK && errno!=EINTR) return -1;
    }
    return 1;
}
static int connect_ipv4(const char *host,uint16_t port,int *fd,mqtt_proxy_stage_t *stage,int *error)
{
    struct hostent *resolved;
    struct sockaddr_in address;
    uint32_t deadline;
    unsigned long mode=1;
    int rc,so_error=0;
    socklen_t so_length=sizeof(so_error);
    *stage=PROXY_STAGE_DNS;
    /* Fixed SDK exposes gethostbyname for A records, with no cancellation API. */
    resolved=gethostbyname(host);
    if (!resolved || resolved->h_addrtype!=AF_INET || resolved->h_length!=4 ||
        !resolved->h_addr_list || !resolved->h_addr_list[0]) return -1;
    memset(&address,0,sizeof(address));
    address.sin_family=AF_INET;address.sin_port=htons(port);
    memcpy(&address.sin_addr,resolved->h_addr_list[0],4);
    *stage=PROXY_STAGE_TCP;
    *fd=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);
    if (*fd<0) {*error=errno;return -1;}
    if (ioctl(*fd,FIONBIO,&mode)<0) {*error=errno;return -1;}
    errno=0;
    rc=connect(*fd,(struct sockaddr *)&address,sizeof(address));
    if (rc==0) return 0;
    *error=errno;
    if (errno==ECONNREFUSED || errno==ENETUNREACH || errno==EHOSTUNREACH) return -1;
    deadline=mico_rtos_get_time()+PROXY_PHASE_TIMEOUT_MS;
    rc=wait_socket(*fd,1,deadline);
    if (rc<=0) {*stage=rc==0?PROXY_STAGE_TIMEOUT:PROXY_STAGE_TCP;if (rc==0) *error=0;return -1;}
    if (getsockopt(*fd,SOL_SOCKET,SO_ERROR,&so_error,&so_length)<0 || so_length!=sizeof(so_error)) {
        *error=errno;return -1;
    }
    *error=so_error;
    return so_error==0?0:-1;
}
int mqtt_proxy_open(const openm1_config_t *config,int *fd,mqtt_proxy_stage_t *stage,
                    int *error,int *authenticated,int *target_connected)
{
    uint8_t buffer[270],reply[4],method[3];
    size_t length,address_length;
    int rc;
    unsigned long blocking=0;
    *fd=-1;*error=0;*authenticated=0;*target_connected=0;
    *stage=PROXY_STAGE_NONE;
    if (recovery_ota_busy()) return -1;
    if (!config->proxy_enabled) {
        if (strchr(config->host,':')) {*stage=PROXY_STAGE_DIRECT_IPV6;return -1;}
        return connect_ipv4(config->host,config->port,fd,stage,error)==0 &&
            ioctl(*fd,FIONBIO,&blocking)==0?0:(*fd>=0?(close(*fd),*fd=-1,-1):-1);
    }
    if (!config->proxy_host[0] || !config->proxy_port ||
        (config->proxy_username[0] && !config->proxy_password[0])) {
        *stage=PROXY_STAGE_PROTOCOL;return -1;
    }
    if (socks5_connect_request(config->host,config->port,buffer,sizeof(buffer),&length)) {
        *stage=PROXY_STAGE_PROTOCOL;return -1;
    }
    if (connect_ipv4(config->proxy_host,config->proxy_port,fd,stage,error)) goto fail;
    *stage=PROXY_STAGE_METHOD;
    method[0]=5;method[1]=1;method[2]=config->proxy_username[0]?2:0;
    rc=send_exact(*fd,method,3);if (rc<=0) goto io_fail;
    rc=recv_exact(*fd,reply,2);if (rc<=0) goto io_fail;
    if (reply[0]!=5 || reply[1]!=method[2]) {*error=reply[1];*stage=PROXY_STAGE_PROTOCOL;goto fail;}
    if (method[2]==2) {
        size_t user_length=strlen(config->proxy_username),password_length=strlen(config->proxy_password);
        *stage=PROXY_STAGE_AUTH;
        if (!user_length || !password_length || user_length>255 || password_length>255) goto fail;
        buffer[0]=1;buffer[1]=(uint8_t)user_length;
        memcpy(buffer+2,config->proxy_username,user_length);
        buffer[2+user_length]=(uint8_t)password_length;
        memcpy(buffer+3+user_length,config->proxy_password,password_length);
        rc=send_exact(*fd,buffer,3+user_length+password_length);if (rc<=0) goto io_fail;
        memset(buffer,0,3+user_length+password_length);
        rc=recv_exact(*fd,reply,2);if (rc<=0) goto io_fail;
        if (reply[0]!=1 || reply[1]!=0) {*error=reply[1];goto fail;}
        *authenticated=1;
    }
    *stage=PROXY_STAGE_TARGET;
    if (socks5_connect_request(config->host,config->port,buffer,sizeof(buffer),&length)) goto fail;
    rc=send_exact(*fd,buffer,length);if (rc<=0) goto io_fail;
    rc=recv_exact(*fd,reply,4);if (rc<=0) goto io_fail;
    rc=socks5_reply_header(reply,&address_length);
    if (rc<0) {*stage=PROXY_STAGE_PROTOCOL;goto fail;}
    if (reply[3]==3) {
        rc=recv_exact(*fd,buffer,1);if (rc<=0) goto io_fail;
        address_length=buffer[0];
        if (!address_length) {*stage=PROXY_STAGE_PROTOCOL;goto fail;}
    }
    rc=recv_exact(*fd,buffer,address_length+2);if (rc<=0) goto io_fail;
    if (reply[1]!=0) {*error=reply[1];goto fail;}
    *target_connected=1;*stage=PROXY_STAGE_NONE;
    if (ioctl(*fd,FIONBIO,&blocking)<0) {*error=errno;*stage=PROXY_STAGE_TCP;goto fail;}
    return 0;
io_fail:
    if (rc==0) *stage=PROXY_STAGE_TIMEOUT;
    *error=rc==0?0:errno;
fail:
    if (*fd>=0) {close(*fd);*fd=-1;}
    return -1;
}
