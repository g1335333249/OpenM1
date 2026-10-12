#include "ipv6_diagnostic.h"
#include "ipv6_probe_logic.h"
#include "dns_aaaa_logic.h"
#include "wifi_manager.h"
#include "recovery.h"
#include "openm1_log.h"
#include "mico_socket.h"
#include "moc_api.h"
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stddef.h>

#if AF_INET6 != 10 || AF_INET != 2
#error "IPv6 diagnostic requires the fixed MiCO socket ABI"
#endif
typedef char openm1_sockaddr_in6_size_check[(sizeof(struct sockaddr_in6)==28)?1:-1];
typedef char openm1_sockaddr_in6_addr_offset_check[
    (offsetof(struct sockaddr_in6,sin6_addr)==8)?1:-1];

#define IPV6_PRIMARY_DNS "www.ustc.edu.cn"
#define IPV6_FALLBACK_DNS "ipv6.mirrors.ustc.edu.cn"
/* Official AliDNS public IPv4 resolver. Only tried when Station DNS fails. */
#ifndef IPV6_FALLBACK_RESOLVER
#define IPV6_FALLBACK_RESOLVER "223.5.5.5"
#endif
#define IPV6_PROBE_RETRY_GAP_MS 30000u
#define IPV6_DNS_SERVER_TIMEOUT_MS 3000u
#define IPV6_STAGE_COUNT 7
/* Build-time controlled targets. No untrusted HTTP input becomes a scanner. */
#ifndef OPENM1_DIAG_IPV4_TARGET
#define OPENM1_DIAG_IPV4_TARGET "223.5.5.5"
#endif
#ifndef OPENM1_DIAG_IPV4_PORT
#define OPENM1_DIAG_IPV4_PORT 53
#endif
/* 32 hexadecimal digits, no colons. Empty means use the validated AAAA. */
#ifndef OPENM1_DIAG_LAN_IPV6_HEX
#define OPENM1_DIAG_LAN_IPV6_HEX ""
#endif
#ifndef OPENM1_DIAG_LAN_IPV6_PORT
#define OPENM1_DIAG_LAN_IPV6_PORT 443
#endif
extern const mico_api_t *lib_api_p;

typedef enum { ST_API,ST_SOCKET,ST_PARSE6,ST_PARSE4,ST_NUMERIC,ST_DNS,ST_TCP } stage_id_t;
typedef struct { uint32_t started_ms,finished_ms; int attempted,done,result,error; } stage_info_t;
typedef struct { int result,error; char bytes[40]; } parse_case_t;
typedef struct {
    int attempted,socket_fd,socket_errno,nonblocking_result,nonblocking_errno;
    int connect_result,connect_errno,select_result,select_errno,writable,exception;
    int so_error_checked,getsockopt_result,getsockopt_errno,so_error;
    int getsockname_result,getsockname_errno,local_family,local_port;
    unsigned local_length,address_length;uint32_t socket_elapsed_ms,elapsed_ms;
    uint8_t local_address[16];char outcome[28];
} tcp_detail_t;
typedef struct {
    int attempted,socket_fd,socket_errno,bind_result,bind_errno;
    int getsockname_result,getsockname_errno,returned_family,returned_port;
    unsigned request_length,returned_length;uint32_t elapsed_ms;
    uint8_t returned_address[16];
} bind_detail_t;
typedef struct {
    int running,done,tcp_requested,deep_requested,api_table_present,socket_result,socket_error;
    int dns_result,dns_error,dns_rcode,dns_ipv6_result,dns_primary_result,dns_fallback_result;
    int numeric_result,numeric_error,numeric_ipv6;
    int tcp_attempted,tcp_connected,tcp_result,tcp_error,tcp_so_error;
    uint32_t tcp_elapsed_ms,dns_elapsed_ms,started_ms,finished_ms;
    parse_case_t compressed,full,loopback,ipv4;
    stage_info_t steps[IPV6_STAGE_COUNT];
    uint8_t dns_bytes[16];
    char verdict[20],dns_name[36],dns_address[40],numeric_address[40];
    char dns_server[16],dns_outcome[20],dns_skip_reason[24];
    char tcp_skip_reason[24],tcp_outcome[28],stage[24];
    tcp_detail_t ipv4_detail,ipv6_detail;
    bind_detail_t bind_detail;
} ipv6_probe_status_t;
static ipv6_probe_status_t result;
static mico_mutex_t result_mutex;
static mico_thread_t probe_thread;
static int probe_ready;

static void init_result(ipv6_probe_status_t *p)
{
    unsigned i;
    memset(p,0,sizeof(*p));
    p->socket_result=p->compressed.result=p->full.result=p->loopback.result=
        p->ipv4.result=p->numeric_result=p->dns_result=p->dns_primary_result=
        p->dns_fallback_result=p->tcp_result=IPV6_PROBE_NOT_ATTEMPTED;
    for (i=0;i<IPV6_STAGE_COUNT;i++) p->steps[i].result=IPV6_PROBE_NOT_ATTEMPTED;
    strcpy(p->verdict,"not_tested");strcpy(p->dns_name,IPV6_PRIMARY_DNS);
    strcpy(p->dns_outcome,"not_tested");strcpy(p->tcp_outcome,"not_tested");
    strcpy(p->dns_skip_reason,"not_started");strcpy(p->tcp_skip_reason,"not_requested");
    strcpy(p->stage,"not_started");
    p->ipv4_detail.socket_fd=p->ipv6_detail.socket_fd=p->bind_detail.socket_fd=-1;
    p->ipv4_detail.nonblocking_result=p->ipv6_detail.nonblocking_result=-32768;
    p->ipv4_detail.connect_result=p->ipv6_detail.connect_result=-32768;
    p->ipv4_detail.select_result=p->ipv6_detail.select_result=-32768;
    p->ipv4_detail.getsockopt_result=p->ipv6_detail.getsockopt_result=-32768;
    p->ipv4_detail.getsockname_result=p->ipv6_detail.getsockname_result=-32768;
    p->bind_detail.bind_result=p->bind_detail.getsockname_result=-32768;
    strcpy(p->ipv4_detail.outcome,"not_tested");
    strcpy(p->ipv6_detail.outcome,"not_tested");
}
static void publish(const ipv6_probe_status_t *p)
{
    mico_rtos_lock_mutex(&result_mutex);result=*p;mico_rtos_unlock_mutex(&result_mutex);
}
static void begin(ipv6_probe_status_t *p,stage_id_t id,const char *name)
{
    p->steps[id].started_ms=mico_rtos_get_time();p->steps[id].attempted=1;
    snprintf(p->stage,sizeof(p->stage),"%s",name);publish(p);
}
static void end(ipv6_probe_status_t *p,stage_id_t id,int rc,int error)
{
    p->steps[id].result=rc;p->steps[id].error=error;p->steps[id].done=1;
    p->steps[id].finished_ms=mico_rtos_get_time();publish(p);
}
static void skip(ipv6_probe_status_t *p,stage_id_t id,const char *reason)
{
    p->steps[id].done=1;p->steps[id].finished_ms=mico_rtos_get_time();
    snprintf(p->stage,sizeof(p->stage),"%s",reason);publish(p);
}
OSStatus ipv6_diagnostic_init(void)
{
    OSStatus err=mico_rtos_init_mutex(&result_mutex);
    if (err!=kNoErr) return err;
    init_result(&result);probe_ready=1;return kNoErr;
}
static void format_address(const uint8_t *bytes,unsigned count,char *out,size_t capacity)
{
    size_t used=0;unsigned i;
    if (!out || !capacity) return;
    out[0]=0;
    for (i=0;i<count;i++) {
        int n=snprintf(out+used,capacity-used,"%02X%s",bytes[i],
            count==16 && (i&1) && i!=15?":":"");
        if (n<0 || (size_t)n>=capacity-used) break;
        used+=(size_t)n;
    }
}
static void parse_one(int family,const char *input,parse_case_t *item,unsigned byte_count)
{
    uint8_t address[16];
    memset(address,0xA5,sizeof(address));errno=0;
    item->result=inet_pton(family,input,address);item->error=errno;
    format_address(address,byte_count,item->bytes,sizeof(item->bytes));
}
static int parse_ipv4(const char *text,uint8_t bytes[4])
{
    unsigned value=0,part=0,digits=0;const char *p;
    if (!text || !*text) return 0;
    for (p=text;;p++) {
        if (*p>='0' && *p<='9') {
            if (++digits>3) return 0;
            value=value*10u+(unsigned)(*p-'0');if (value>255u) return 0;
        } else if (*p=='.' || !*p) {
            if (!digits || part>=4) return 0;
            bytes[part++]=(uint8_t)value;value=0;digits=0;
            if (!*p) return part==4;
        } else return 0;
    }
}
/* No Kernel getaddrinfo() is used: this API has blocked indefinitely on real
 * MK3080B hardware. A bounded UDP exchange uses the cached Station DNS IP. */
static int dns_exchange(const char *server,const char *name,uint16_t id,
                        uint8_t address[16],int *socket_error,int *rcode,
                        dns_aaaa_result_t *parse_status)
{
    uint8_t query[96],reply[DNS_AAAA_PACKET_MAX],server_ip[4];
    struct sockaddr_in target,source;
    unsigned long nonblocking=1;
    uint32_t started=mico_rtos_get_time();
    socklen_t source_len;
    fd_set readable;
    struct timeval wait;
    int fd,n,selected,query_len,answer=-1;
    *socket_error=0;*rcode=0;*parse_status=DNS_AAAA_MALFORMED;
    if (!parse_ipv4(server,server_ip)) return -4;
    query_len=dns_aaaa_make_query(name,id,query,sizeof(query));
    if (query_len<0) return -4;
    fd=socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);
    if (fd<0) { *socket_error=errno;return -3; }
    if (ioctl(fd,FIONBIO,&nonblocking)<0) { *socket_error=errno;answer=-3;goto done; }
    memset(&target,0,sizeof(target));target.sin_len=sizeof(target);
    target.sin_family=AF_INET;target.sin_port=htons(53);
    memcpy(&target.sin_addr.s_addr,server_ip,4);
    errno=0;
    n=sendto(fd,query,(size_t)query_len,0,(struct sockaddr *)&target,sizeof(target));
    if (n!=query_len) { *socket_error=errno;answer=-3;goto done; }
    for (;;) {
        if (recovery_ota_busy()) { answer=-5;break; }
        uint32_t remaining=dns_aaaa_remaining_ms(started,mico_rtos_get_time(),
                                                   IPV6_DNS_SERVER_TIMEOUT_MS);
        if (!remaining) { answer=-1;break; }
        wait.tv_sec=remaining/1000u;
        wait.tv_usec=(remaining%1000u)*1000u;
        FD_ZERO(&readable);FD_SET(fd,&readable);
        errno=0;selected=select(fd+1,&readable,NULL,NULL,&wait);
        if (selected==0) { answer=-1;break; }
        if (selected<0) { if (errno==EINTR) continue;*socket_error=errno;answer=-3;break; }
        source_len=sizeof(source);memset(&source,0,sizeof(source));errno=0;
        n=recvfrom(fd,reply,sizeof(reply),0,(struct sockaddr *)&source,&source_len);
        if (n<0) {
            if (errno==EINTR || errno==EAGAIN || errno==EWOULDBLOCK) continue;
            *socket_error=errno;answer=-3;break;
        }
        if (source_len<sizeof(source) || source.sin_family!=AF_INET ||
            source.sin_port!=htons(53) ||
            memcmp(&source.sin_addr.s_addr,server_ip,4)) continue;
        *parse_status=dns_aaaa_parse(reply,(size_t)n,id,name,address,rcode);
        if (*parse_status==DNS_AAAA_WRONG_REPLY) continue;
        answer=*parse_status==DNS_AAAA_OK?0:-2;break;
    }
done:
    close(fd);return answer;
}
static void tcp_connect_detail(tcp_detail_t *d,int family,
                               struct sockaddr *target,socklen_t target_len)
{
    unsigned long nonblocking=1;struct timeval timeout;
    fd_set writable,exception;uint32_t started=mico_rtos_get_time(),socket_started;
    int fd,error=0; socklen_t error_size=sizeof(error);
    struct sockaddr_in6 local6;struct sockaddr_in local4;socklen_t local_len;
    d->attempted=1;d->address_length=target_len;
    socket_started=mico_rtos_get_time();errno=0;
    fd=socket(family,SOCK_STREAM,IPPROTO_TCP);
    d->socket_fd=fd;d->socket_errno=fd<0?errno:0;
    d->socket_elapsed_ms=(uint32_t)(mico_rtos_get_time()-socket_started);
    if (fd<0) { strcpy(d->outcome,"socket_error");goto finished; }
    errno=0;d->nonblocking_result=ioctl(fd,FIONBIO,&nonblocking);
    d->nonblocking_errno=d->nonblocking_result<0?errno:0;
    if (d->nonblocking_result<0) { strcpy(d->outcome,"nonblocking_failed");goto closed; }
    errno=0;d->connect_result=connect(fd,target,target_len);
    d->connect_errno=d->connect_result<0?errno:0;
    if (d->connect_result<0 && d->connect_errno!=ENETUNREACH &&
        d->connect_errno!=EHOSTUNREACH && d->connect_errno!=ECONNREFUSED) {
        FD_ZERO(&writable);FD_ZERO(&exception);
        FD_SET(fd,&writable);FD_SET(fd,&exception);
        timeout.tv_sec=IPV6_TCP_CONNECT_TIMEOUT_MS/1000u;
        timeout.tv_usec=(IPV6_TCP_CONNECT_TIMEOUT_MS%1000u)*1000u;
        errno=0;d->select_result=select(fd+1,NULL,&writable,&exception,&timeout);
        d->select_errno=d->select_result<0?errno:0;
        if (d->select_result>0) {
            d->writable=FD_ISSET(fd,&writable)?1:0;
            d->exception=FD_ISSET(fd,&exception)?1:0;
            errno=0;d->getsockopt_result=getsockopt(fd,SOL_SOCKET,SO_ERROR,&error,&error_size);
            d->getsockopt_errno=d->getsockopt_result<0?errno:0;
            if (d->getsockopt_result==0 && error_size==sizeof(error)) {
                d->so_error_checked=1;d->so_error=error;
            }
        }
    }
    snprintf(d->outcome,sizeof(d->outcome),"%s",
        tcp_probe_outcome(d->connect_result,d->connect_errno,d->select_result,
                          d->writable,d->exception,d->getsockopt_result,
                          d->so_error_checked,d->so_error));
    /* A successful active connect can reveal the selected source address. */
    if (!strcmp(d->outcome,"connected")) {
        if (family==AF_INET6) {
            memset(&local6,0,sizeof(local6));local_len=sizeof(local6);errno=0;
            d->getsockname_result=getsockname(fd,(struct sockaddr *)&local6,&local_len);
            d->getsockname_errno=d->getsockname_result<0?errno:0;
            d->local_length=local_len;
            if (!d->getsockname_result && local_len>=sizeof(local6)) {
                d->local_family=local6.sin6_family;d->local_port=ntohs(local6.sin6_port);
                memcpy(d->local_address,local6.sin6_addr.s6_addr,16);
            }
        } else {
            memset(&local4,0,sizeof(local4));local_len=sizeof(local4);errno=0;
            d->getsockname_result=getsockname(fd,(struct sockaddr *)&local4,&local_len);
            d->getsockname_errno=d->getsockname_result<0?errno:0;
            d->local_length=local_len;
            if (!d->getsockname_result && local_len>=sizeof(local4)) {
                d->local_family=local4.sin_family;d->local_port=ntohs(local4.sin_port);
                memcpy(d->local_address,&local4.sin_addr.s_addr,4);
            }
        }
    }
closed:
    close(fd);
finished:
    d->elapsed_ms=(uint32_t)(mico_rtos_get_time()-started);
}
static void tcp_connect_probe(ipv6_probe_status_t *p,const uint8_t address[16])
{
    struct sockaddr_in6 target;tcp_detail_t *d=&p->ipv6_detail;
    memset(&target,0,sizeof(target));target.sin6_len=sizeof(target);
    target.sin6_family=AF_INET6;target.sin6_port=htons(443);
    memcpy(target.sin6_addr.s6_addr,address,16);
    p->tcp_attempted=1;
    tcp_connect_detail(d,AF_INET6,(struct sockaddr *)&target,sizeof(target));
    p->tcp_result=d->connect_result;p->tcp_error=d->connect_errno;
    p->tcp_so_error=d->so_error;p->tcp_elapsed_ms=d->elapsed_ms;
    p->tcp_connected=!strcmp(d->outcome,"connected");
    snprintf(p->tcp_outcome,sizeof(p->tcp_outcome),"%s",d->outcome);
}
static void bind_probe(bind_detail_t *d)
{
    struct sockaddr_in6 wildcard,returned; socklen_t length;
    uint32_t started=mico_rtos_get_time();int fd;
    memset(&wildcard,0,sizeof(wildcard));wildcard.sin6_len=sizeof(wildcard);
    wildcard.sin6_family=AF_INET6;
    d->attempted=1;d->request_length=sizeof(wildcard);
    errno=0;fd=socket(AF_INET6,SOCK_STREAM,IPPROTO_TCP);
    d->socket_fd=fd;d->socket_errno=fd<0?errno:0;
    if (fd<0) goto done;
    errno=0;d->bind_result=bind(fd,(struct sockaddr *)&wildcard,sizeof(wildcard));
    d->bind_errno=d->bind_result<0?errno:0;
    if (!d->bind_result) {
        memset(&returned,0,sizeof(returned));length=sizeof(returned);errno=0;
        d->getsockname_result=getsockname(fd,(struct sockaddr *)&returned,&length);
        d->getsockname_errno=d->getsockname_result<0?errno:0;
        d->returned_length=length;
        if (!d->getsockname_result && length>=sizeof(returned)) {
            d->returned_family=returned.sin6_family;
            d->returned_port=ntohs(returned.sin6_port);
            memcpy(d->returned_address,returned.sin6_addr.s6_addr,16);
        }
    }
    close(fd);
done:
    d->elapsed_ms=(uint32_t)(mico_rtos_get_time()-started);
}
static int configured_lan_ipv6(uint8_t out[16])
{
    const char *s=OPENM1_DIAG_LAN_IPV6_HEX;unsigned i;
    if (strlen(s)!=32u) return 0;
    for (i=0;i<16;i++) {
        unsigned j,value=0;
        for (j=0;j<2;j++) {
            char c=s[i*2+j];unsigned digit;
            if (c>='0' && c<='9') digit=(unsigned)(c-'0');
            else if (c>='a' && c<='f') digit=(unsigned)(c-'a'+10);
            else if (c>='A' && c<='F') digit=(unsigned)(c-'A'+10);
            else return 0;
            value=value*16u+digit;
        }
        out[i]=(uint8_t)value;
    }
    return 1;
}
static void deep_probe(ipv6_probe_status_t *p,const lwip_api_t *api)
{
    uint8_t ipv4[4],ipv6[16];struct sockaddr_in target4;
    struct sockaddr_in6 target6;
    if (recovery_ota_busy()) return;
    if (api && api->lwip_socket && api->lwip_close &&
        api->lwip_bind && api->lwip_getsockname) {
        snprintf(p->stage,sizeof(p->stage),"wildcard_bind");publish(p);
        bind_probe(&p->bind_detail);publish(p);
    }
    if (recovery_ota_busy()) return;
    if (!api || !api->lwip_socket || !api->lwip_close || !api->lwip_ioctl ||
        !api->lwip_connect || !api->lwip_select || !api->lwip_getsockopt ||
        !api->lwip_getsockname) return;
    if (parse_ipv4(OPENM1_DIAG_IPV4_TARGET,ipv4)) {
        memset(&target4,0,sizeof(target4));target4.sin_len=sizeof(target4);
        target4.sin_family=AF_INET;target4.sin_port=htons(OPENM1_DIAG_IPV4_PORT);
        memcpy(&target4.sin_addr.s_addr,ipv4,4);
        snprintf(p->stage,sizeof(p->stage),"ipv4_control");publish(p);
        tcp_connect_detail(&p->ipv4_detail,AF_INET,
                           (struct sockaddr *)&target4,sizeof(target4));publish(p);
    }
    if (recovery_ota_busy()) return;
    if (configured_lan_ipv6(ipv6)) {
        memset(&target6,0,sizeof(target6));target6.sin6_len=sizeof(target6);
        target6.sin6_family=AF_INET6;
        target6.sin6_port=htons(OPENM1_DIAG_LAN_IPV6_PORT);
        memcpy(target6.sin6_addr.s6_addr,ipv6,16);
        snprintf(p->stage,sizeof(p->stage),"ipv6_lan_control");publish(p);
        tcp_connect_detail(&p->ipv6_detail,AF_INET6,
                           (struct sockaddr *)&target6,sizeof(target6));publish(p);
        p->tcp_attempted=1;p->tcp_result=p->ipv6_detail.connect_result;
        p->tcp_error=p->ipv6_detail.connect_errno;
        p->tcp_so_error=p->ipv6_detail.so_error;
        p->tcp_elapsed_ms=p->ipv6_detail.elapsed_ms;
        p->tcp_connected=!strcmp(p->ipv6_detail.outcome,"connected");
        snprintf(p->tcp_outcome,sizeof(p->tcp_outcome),"%s",p->ipv6_detail.outcome);
    }
}
static void run_dns(ipv6_probe_status_t *p)
{
    char station_dns[16];uint8_t server_bytes[4];int rc,error=0,rcode=0;
    uint32_t started;dns_aaaa_result_t parsed;
    wifi_manager_station_dns(station_dns);
    if (!parse_ipv4(station_dns,server_bytes) || !server_bytes[0] ||
        server_bytes[0]>=224)
        snprintf(station_dns,sizeof(station_dns),"%s",IPV6_FALLBACK_RESOLVER);
    snprintf(p->dns_server,sizeof(p->dns_server),"%s",station_dns);
    begin(p,ST_DNS,"aaaa_primary");started=p->steps[ST_DNS].started_ms;
    p->dns_skip_reason[0]=0;
    rc=dns_exchange(station_dns,IPV6_PRIMARY_DNS,(uint16_t)(started^0xA617u),
                    p->dns_bytes,&error,&rcode,&parsed);
    p->dns_primary_result=rc;p->dns_result=rc;p->dns_error=error;p->dns_rcode=rcode;
    strcpy(p->dns_outcome,rc==-1?"timeout":rc==-3?"socket_error":rc==-5?"ota_busy":
           rc==-4?"bad_server":dns_aaaa_result_name(parsed));
    p->dns_elapsed_ms=(uint32_t)(mico_rtos_get_time()-started);publish(p);
    if (rc && !recovery_ota_busy()) {
        /* One alternate name and resolver, never an unbounded retry loop. */
        snprintf(p->stage,sizeof(p->stage),"aaaa_fallback");publish(p);
        p->dns_fallback_result=dns_exchange(IPV6_FALLBACK_RESOLVER,
            IPV6_FALLBACK_DNS,(uint16_t)(mico_rtos_get_time()^0xB617u),
            p->dns_bytes,&error,&rcode,&parsed);
        p->dns_result=p->dns_fallback_result;p->dns_error=error;p->dns_rcode=rcode;
        snprintf(p->dns_server,sizeof(p->dns_server),"%s",IPV6_FALLBACK_RESOLVER);
        snprintf(p->dns_name,sizeof(p->dns_name),"%s",IPV6_FALLBACK_DNS);
        strcpy(p->dns_outcome,p->dns_result==-1?"timeout":p->dns_result==-3?
            "socket_error":p->dns_result==-5?"ota_busy":p->dns_result==-4?
            "bad_server":dns_aaaa_result_name(parsed));
    }
    p->dns_elapsed_ms=(uint32_t)(mico_rtos_get_time()-started);
    if (recovery_ota_busy()) strcpy(p->dns_skip_reason,"ota_busy");
    if (p->dns_result==0) {
        static const uint8_t unspecified[16]={0};
        if (memcmp(p->dns_bytes,unspecified,16)) {
            p->dns_ipv6_result=1;
            format_address(p->dns_bytes,16,p->dns_address,sizeof(p->dns_address));
        } else { p->dns_result=-2;strcpy(p->dns_outcome,"invalid_address"); }
    }
    end(p,ST_DNS,p->dns_result,p->dns_error);
}
static void ipv6_probe_worker(mico_thread_arg_t arg)
{
    ipv6_probe_status_t local;
    const lwip_api_t *api=lib_api_p?lib_api_p->lwip_apis:NULL;
    int fd;
    (void)arg;
    mico_rtos_lock_mutex(&result_mutex);local=result;mico_rtos_unlock_mutex(&result_mutex);
    begin(&local,ST_API,"api_table");
    local.api_table_present=api && api->lwip_socket && api->lwip_close &&
        api->inet_pton && api->lwip_ioctl && api->lwip_connect &&
        api->lwip_select && api->lwip_getsockopt && api->lwip_sendto && api->lwip_recvfrom;
    end(&local,ST_API,local.api_table_present?0:-1,0);
    if (recovery_ota_busy()) {
        strcpy(local.verdict,"canceled");strcpy(local.dns_skip_reason,"ota_busy");
        strcpy(local.tcp_skip_reason,"ota_busy");goto finished;
    }
    if (api && api->lwip_socket && api->lwip_close) {
        begin(&local,ST_SOCKET,"socket");errno=0;
        fd=socket(AF_INET6,SOCK_STREAM,IPPROTO_TCP);
        local.socket_result=fd>=0?0:fd;local.socket_error=fd<0?errno:0;
        if (fd>=0) close(fd);
        end(&local,ST_SOCKET,local.socket_result,local.socket_error);
    } else skip(&local,ST_SOCKET,"socket_api_missing");
    if (api && api->inet_pton) {
        begin(&local,ST_PARSE6,"numeric_parse");
        parse_one(AF_INET6,"2001:db8::1",&local.compressed,16);
        parse_one(AF_INET6,"2001:0db8:0000:0000:0000:0000:0000:0001",&local.full,16);
        parse_one(AF_INET6,"::1",&local.loopback,16);
        end(&local,ST_PARSE6,local.compressed.result,local.compressed.error);
        begin(&local,ST_PARSE4,"ipv4_parse");
        parse_one(AF_INET,"192.0.2.1",&local.ipv4,4);
        end(&local,ST_PARSE4,local.ipv4.result,local.ipv4.error);
    } else { skip(&local,ST_PARSE6,"parse_api_missing");
             skip(&local,ST_PARSE4,"parse_api_missing"); }
    /* The fixed Kernel's AF_INET6 getaddrinfo can hang indefinitely, even
     * for a numeric address. It is intentionally never called here. */
    strcpy(local.dns_skip_reason,"kernel_gai_disabled");
    skip(&local,ST_NUMERIC,"kernel_gai_disabled");
    local.dns_skip_reason[0]=0;
    if (recovery_ota_busy()) { strcpy(local.dns_skip_reason,"ota_busy");
        skip(&local,ST_DNS,"dns_ota_busy"); }
    else if (!api || !api->lwip_socket || !api->lwip_sendto ||
             !api->lwip_recvfrom || !api->lwip_select || !api->lwip_close || !api->lwip_ioctl) {
        strcpy(local.dns_skip_reason,"udp_api_missing");
        skip(&local,ST_DNS,"udp_api_missing");
    } else run_dns(&local);
    if (!local.tcp_requested) skip(&local,ST_TCP,"tcp_not_requested");
    else if (local.deep_requested && strlen(OPENM1_DIAG_LAN_IPV6_HEX)==32u)
        skip(&local,ST_TCP,"lan_target_pending");
    else if (!local.dns_ipv6_result) { strcpy(local.tcp_skip_reason,"no_valid_aaaa");
        skip(&local,ST_TCP,"no_valid_aaaa"); }
    else if (recovery_ota_busy()) { strcpy(local.tcp_skip_reason,"ota_busy");
        skip(&local,ST_TCP,"tcp_ota_busy"); }
    else if (!api || !api->lwip_ioctl || !api->lwip_connect || !api->lwip_select ||
             !api->lwip_getsockopt || !api->lwip_socket || !api->lwip_close) {
        strcpy(local.tcp_skip_reason,"tcp_api_missing");
        skip(&local,ST_TCP,"tcp_api_missing");
    } else {
        begin(&local,ST_TCP,"tcp_connect");local.tcp_skip_reason[0]=0;
        tcp_connect_probe(&local,local.dns_bytes);
        end(&local,ST_TCP,local.tcp_result,local.tcp_error);
    }
    if (local.deep_requested) deep_probe(&local,api);
    strcpy(local.verdict,ipv6_probe_verdict(local.api_table_present,local.socket_result,
        local.tcp_attempted,local.tcp_connected));
finished:
    strcpy(local.stage,"complete");local.finished_ms=mico_rtos_get_time();
    local.running=0;local.done=1;publish(&local);
    openm1_log_info("NETWORK","IPv6 probe %s socket=%d parse=%d AAAA=%d TCP=%s",
        local.verdict,local.socket_result,local.compressed.result,local.dns_result,
        local.tcp_outcome);
    mico_rtos_delete_thread(NULL);
}
static int start_probe(int mode)
{
    micoMemInfo_t *memory;OSStatus err;uint32_t now=mico_rtos_get_time();
    if (!probe_ready) return -1;
    if (recovery_ota_busy()) return -2;
    if (!wifi_manager_station_ready()) return -3;
    memory=MicoGetMemoryInfo();
    if (!memory || memory->free_memory<=IPV6_DIAGNOSTIC_WORKER_STACK+9216u+8192u) return -4;
    mico_rtos_lock_mutex(&result_mutex);
    if (result.running) { mico_rtos_unlock_mutex(&result_mutex);return -5; }
    if (result.done && (uint32_t)(now-result.finished_ms)<IPV6_PROBE_RETRY_GAP_MS) {
        mico_rtos_unlock_mutex(&result_mutex);return -7;
    }
    init_result(&result);result.running=1;result.started_ms=now;
    result.tcp_requested=mode!=0;result.deep_requested=mode==2;
    strcpy(result.verdict,"running");
    mico_rtos_unlock_mutex(&result_mutex);
    err=mico_rtos_create_thread(&probe_thread,MICO_APPLICATION_PRIORITY,
        "openm1_ipv6_probe",ipv6_probe_worker,IPV6_DIAGNOSTIC_WORKER_STACK,0);
    if (err!=kNoErr) {
        mico_rtos_lock_mutex(&result_mutex);result.running=0;result.done=1;
        result.finished_ms=mico_rtos_get_time();result.socket_result=err;
        strcpy(result.verdict,"start_failed");mico_rtos_unlock_mutex(&result_mutex);
        return -6;
    }
    return 0;
}
int ipv6_diagnostic_start(void) { return start_probe(0); }
int ipv6_diagnostic_start_tcp(void)
{
#if OPENM1_IPV6_TCP_EXPERIMENT_ENABLED
    return start_probe(1);
#else
    return -8;
#endif
}
int ipv6_diagnostic_start_deep(void) { return start_probe(2); }
void ipv6_diagnostic_status_json(char *out,size_t capacity)
{
    ipv6_probe_status_t s;const stage_info_t *a,*k,*p,*v,*n,*d,*t;
    char so_error_text[16];
    if (!out || !capacity) return;
    if (!probe_ready) { snprintf(out,capacity,"{\"available\":false}");return; }
    mico_rtos_lock_mutex(&result_mutex);s=result;mico_rtos_unlock_mutex(&result_mutex);
    if (s.ipv6_detail.so_error_checked)
        snprintf(so_error_text,sizeof(so_error_text),"%d",s.ipv6_detail.so_error);
    else strcpy(so_error_text,"null");
    a=&s.steps[ST_API];k=&s.steps[ST_SOCKET];p=&s.steps[ST_PARSE6];
    v=&s.steps[ST_PARSE4];n=&s.steps[ST_NUMERIC];d=&s.steps[ST_DNS];t=&s.steps[ST_TCP];
#define ST_ARGS(x) (x)->attempted,(x)->done,(unsigned long)(x)->started_ms,(unsigned long)(x)->finished_ms,(x)->result,(x)->error
    snprintf(out,capacity,
      "{\"available\":true,\"manual_only\":true,\"verdict\":\"%s\",\"running\":%s,\"done\":%s,\"stage\":\"%s\",\"dns_call_overdue\":false,\"kernel_ipv6_getaddrinfo_enabled\":false,\"api_table_present\":%s,"
      "\"socket_result\":%d,\"socket_errno\":%d,\"parse_result\":%d,\"parse_errno\":%d,\"parse_status\":\"%s\",\"parse_output_hex\":\"%s\","
      "\"parse_full_result\":%d,\"parse_full_errno\":%d,\"parse_full_output_hex\":\"%s\","
      "\"parse_loopback_result\":%d,\"parse_loopback_errno\":%d,\"parse_loopback_output_hex\":\"%s\","
      "\"parse_ipv4_result\":%d,\"parse_ipv4_errno\":%d,\"parse_ipv4_output_hex\":\"%s\","
      "\"numeric_getaddrinfo_result\":%d,\"numeric_getaddrinfo_errno\":%d,\"numeric_getaddrinfo_ipv6\":false,\"numeric_address\":\"\","
      "\"aaaa_result\":%d,\"aaaa_errno\":%d,\"aaaa_rcode\":%d,\"aaaa_ipv6\":%s,\"aaaa_address\":\"%s\",\"dns_name\":\"%s\",\"dns_server\":\"%s\",\"dns_outcome\":\"%s\","
      "\"dns_primary_result\":%d,\"dns_fallback_result\":%d,\"dns_elapsed_ms\":%lu,\"dns_skip_reason\":\"%s\","
      "\"ipv6_tcp\":{\"requested\":%s,\"attempted\":%s,\"connected\":%s,\"host\":\"%s\",\"port\":443,\"connect_result\":%d,\"connect_errno\":%d,\"so_error\":%s,\"so_error_checked\":%s,\"elapsed_ms\":%lu,\"outcome\":\"%s\",\"skip_reason\":\"%s\"},"
      "\"stage_results\":{\"api\":[%d,%d,%lu,%lu,%d,%d],\"socket\":[%d,%d,%lu,%lu,%d,%d],\"ipv6_parse\":[%d,%d,%lu,%lu,%d,%d],\"ipv4_parse\":[%d,%d,%lu,%lu,%d,%d],\"kernel_getaddrinfo_disabled\":[%d,%d,%lu,%lu,%d,%d],\"aaaa_dns\":[%d,%d,%lu,%lu,%d,%d],\"ipv6_tcp\":[%d,%d,%lu,%lu,%d,%d]},"
      "\"started_ms\":%lu,\"finished_ms\":%lu,\"mqtt_ipv6_feasible\":%s,\"mqtt_ipv6_enabled\":false}",
      s.verdict,s.running?"true":"false",s.done?"true":"false",s.stage,
      s.api_table_present?"true":"false",s.socket_result,s.socket_error,
      s.compressed.result,s.compressed.error,ipv6_parse_status(s.compressed.result),s.compressed.bytes,
      s.full.result,s.full.error,s.full.bytes,s.loopback.result,s.loopback.error,s.loopback.bytes,
      s.ipv4.result,s.ipv4.error,s.ipv4.bytes,s.numeric_result,s.numeric_error,
      s.dns_result,s.dns_error,s.dns_rcode,s.dns_ipv6_result?"true":"false",
      s.dns_address,s.dns_name,s.dns_server,s.dns_outcome,s.dns_primary_result,
      s.dns_fallback_result,(unsigned long)s.dns_elapsed_ms,s.dns_skip_reason,
      s.tcp_requested?"true":"false",s.tcp_attempted?"true":"false",
      s.tcp_connected?"true":"false",s.dns_name,s.tcp_result,s.tcp_error,
      so_error_text,
      s.ipv6_detail.so_error_checked?"true":"false",
      (unsigned long)s.tcp_elapsed_ms,s.tcp_outcome,s.tcp_skip_reason,
      ST_ARGS(a),ST_ARGS(k),ST_ARGS(p),ST_ARGS(v),ST_ARGS(n),ST_ARGS(d),ST_ARGS(t),
      (unsigned long)s.started_ms,(unsigned long)s.finished_ms,s.tcp_connected?"true":"false");
#undef ST_ARGS
}
static void tcp_json(char *out,size_t capacity,const tcp_detail_t *d)
{
    char local[40],local_json[44],so_error[16];
    format_address(d->local_address,d->local_family==AF_INET?4:16,
                   local,sizeof(local));
    if (d->getsockname_result==0 &&
        d->local_length>=(d->local_family==AF_INET6?
            sizeof(struct sockaddr_in6):sizeof(struct sockaddr_in)))
        snprintf(local_json,sizeof(local_json),"\"%s\"",local);
    else strcpy(local_json,"null");
    if (d->so_error_checked) snprintf(so_error,sizeof(so_error),"%d",d->so_error);
    else strcpy(so_error,"null");
    snprintf(out,capacity,
      "{\"attempted\":%s,\"socket_fd\":%d,\"socket_errno\":%d,\"socket_elapsed_ms\":%lu,"
      "\"nonblocking_result\":%d,\"nonblocking_errno\":%d,"
      "\"connect_result\":%d,\"connect_errno\":%d,\"kernel_errno_unavailable\":%s,"
      "\"select_result\":%d,\"select_errno\":%d,\"writable\":%s,\"exception\":%s,"
      "\"getsockopt_result\":%d,\"getsockopt_errno\":%d,\"so_error_checked\":%s,\"so_error\":%s,"
      "\"getsockname_result\":%d,\"getsockname_errno\":%d,\"local_length\":%u,"
      "\"local_family\":%d,\"local_port\":%d,\"local_address_hex\":%s,"
      "\"connect_addr_length\":%u,\"elapsed_ms\":%lu,\"outcome\":\"%s\"}",
      d->attempted?"true":"false",d->socket_fd,d->socket_errno,
      (unsigned long)d->socket_elapsed_ms,d->nonblocking_result,d->nonblocking_errno,
      d->connect_result,d->connect_errno,
      d->connect_result<0 && d->connect_errno==0?"true":"false",
      d->select_result,d->select_errno,d->writable?"true":"false",
      d->exception?"true":"false",d->getsockopt_result,d->getsockopt_errno,
      d->so_error_checked?"true":"false",so_error,d->getsockname_result,
      d->getsockname_errno,d->local_length,d->local_family,d->local_port,
      local_json,d->address_length,(unsigned long)d->elapsed_ms,d->outcome);
}
void ipv6_diagnostic_deep_json(char *out,size_t capacity)
{
    ipv6_probe_status_t s;char ipv4[780],ipv6[780],bound[40],target[40],source_status[28];
    const char *detail;
    uint8_t configured[16];
    if (!out || !capacity) return;
    if (!probe_ready) { snprintf(out,capacity,"{\"available\":false}");return; }
    mico_rtos_lock_mutex(&result_mutex);s=result;mico_rtos_unlock_mutex(&result_mutex);
    tcp_json(ipv4,sizeof(ipv4),&s.ipv4_detail);
    tcp_json(ipv6,sizeof(ipv6),&s.ipv6_detail);
    format_address(s.bind_detail.returned_address,16,bound,sizeof(bound));
    if (configured_lan_ipv6(configured)) format_address(configured,16,target,sizeof(target));
    else snprintf(target,sizeof(target),"%s",s.dns_address);
    if (!s.ipv6_detail.attempted || s.ipv6_detail.getsockname_result)
        strcpy(source_status,"ipv6_address_not_confirmed");
    else {
        static const uint8_t zero[16]={0};
        strcpy(source_status,memcmp(s.ipv6_detail.local_address,zero,16)?
               "source_address_observed":"ipv6_address_not_confirmed");
    }
    if (s.socket_result==IPV6_PROBE_NOT_ATTEMPTED) detail="socket_api_unavailable";
    else if (s.tcp_connected) detail="ipv6_tcp_connected";
    else if (s.tcp_attempted && !strcmp(s.tcp_outcome,"timeout")) detail="connect_timeout";
    else if (s.tcp_attempted && !strcmp(s.tcp_outcome,"no_route")) detail="connect_no_route";
    else if (s.tcp_attempted && !strcmp(s.tcp_outcome,"refused")) detail="connect_refused";
    else if (s.tcp_attempted) detail="connect_socket_error";
    else if (s.socket_result==0) detail="ipv6_socket_created";
    else detail="ipv6_interface_unknown";
    snprintf(out,capacity,
      "{\"manual_only\":true,\"running\":%s,\"done\":%s,\"stage\":\"%s\","
      "\"verdict\":\"%s\",\"detailed_verdict\":\"%s\","
      "\"ipv6_interface_query\":\"unavailable\",\"ipv6_route\":\"%s\","
      "\"source_address_status\":\"%s\",\"wildcard_is_interface_address\":false,"
      "\"abi\":{\"sockaddr_in6_size\":%u,\"sin6_len\":%u,\"sin6_family\":%u,"
      "\"sin6_port\":%u,\"sin6_port_host\":%u,\"sin6_flowinfo\":0,\"sin6_scope_id\":0,"
      "\"target_address_hex\":\"%s\",\"connect_addr_length\":%u},"
      "\"wildcard_bind\":{\"attempted\":%s,\"socket_fd\":%d,\"socket_errno\":%d,"
      "\"bind_result\":%d,\"bind_errno\":%d,\"request_length\":%u,"
      "\"request_family\":10,\"request_port\":0,"
      "\"getsockname_result\":%d,\"getsockname_errno\":%d,\"returned_length\":%u,"
      "\"returned_family\":%d,\"returned_port\":%d,\"returned_address_hex\":\"%s\","
      "\"elapsed_ms\":%lu},"
      "\"ipv4_target\":\"%s\",\"ipv4_port\":%d,\"ipv4_tcp\":%s,"
      "\"ipv6_target\":\"%s\",\"ipv6_port\":%d,\"ipv6_tcp\":%s,"
      "\"mqtt_ipv6_enabled\":false}",
      s.running?"true":"false",s.done?"true":"false",s.stage,
      s.verdict,detail,s.tcp_connected?"observed_for_target":"unknown",source_status,
      (unsigned)sizeof(struct sockaddr_in6),(unsigned)sizeof(struct sockaddr_in6),
      (unsigned)AF_INET6,(unsigned)htons(configured_lan_ipv6(configured)?
          OPENM1_DIAG_LAN_IPV6_PORT:443),
      (unsigned)(configured_lan_ipv6(configured)?OPENM1_DIAG_LAN_IPV6_PORT:443),target,
      (unsigned)sizeof(struct sockaddr_in6),
      s.bind_detail.attempted?"true":"false",s.bind_detail.socket_fd,
      s.bind_detail.socket_errno,s.bind_detail.bind_result,s.bind_detail.bind_errno,
      s.bind_detail.request_length,s.bind_detail.getsockname_result,
      s.bind_detail.getsockname_errno,s.bind_detail.returned_length,
      s.bind_detail.returned_family,s.bind_detail.returned_port,bound,
      (unsigned long)s.bind_detail.elapsed_ms,
      OPENM1_DIAG_IPV4_TARGET,OPENM1_DIAG_IPV4_PORT,ipv4,target,
      configured_lan_ipv6(configured)?OPENM1_DIAG_LAN_IPV6_PORT:443,ipv6);
}
