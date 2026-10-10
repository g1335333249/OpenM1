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

#if AF_INET6 != 10 || AF_INET != 2
#error "IPv6 diagnostic requires the fixed MiCO socket ABI"
#endif

#define IPV6_PRIMARY_DNS "www.ustc.edu.cn"
#define IPV6_FALLBACK_DNS "ipv6.mirrors.ustc.edu.cn"
/* Official AliDNS public IPv4 resolver. Only tried when Station DNS fails. */
#ifndef IPV6_FALLBACK_RESOLVER
#define IPV6_FALLBACK_RESOLVER "223.5.5.5"
#endif
#define IPV6_PROBE_RETRY_GAP_MS 30000u
#define IPV6_DNS_SERVER_TIMEOUT_MS 3000u
#define IPV6_STAGE_COUNT 7
extern const mico_api_t *lib_api_p;

typedef enum { ST_API,ST_SOCKET,ST_PARSE6,ST_PARSE4,ST_NUMERIC,ST_DNS,ST_TCP } stage_id_t;
typedef struct { uint32_t started_ms,finished_ms; int attempted,done,result,error; } stage_info_t;
typedef struct { int result,error; char bytes[40]; } parse_case_t;
typedef struct {
    int running,done,tcp_requested,api_table_present,socket_result,socket_error;
    int dns_result,dns_error,dns_rcode,dns_ipv6_result,dns_primary_result,dns_fallback_result;
    int numeric_result,numeric_error,numeric_ipv6;
    int tcp_attempted,tcp_connected,tcp_result,tcp_error,tcp_so_error;
    uint32_t tcp_elapsed_ms,dns_elapsed_ms,started_ms,finished_ms;
    parse_case_t compressed,full,loopback,ipv4;
    stage_info_t steps[IPV6_STAGE_COUNT];
    uint8_t dns_bytes[16];
    char verdict[20],dns_name[36],dns_address[40],numeric_address[40];
    char dns_server[16],dns_outcome[20],dns_skip_reason[24];
    char tcp_skip_reason[24],tcp_outcome[20],stage[24];
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
static void tcp_connect_probe(ipv6_probe_status_t *p,const uint8_t address[16])
{
    struct sockaddr_in6 target;
    unsigned long nonblocking=1;struct timeval timeout;
    fd_set writable,exception;
    int fd,rc,error=0,option_rc=IPV6_PROBE_NOT_ATTEMPTED,selected=IPV6_PROBE_NOT_ATTEMPTED;
    socklen_t error_size=sizeof(error);
    uint32_t started=mico_rtos_get_time();ipv6_connect_outcome_t outcome;
    memset(&target,0,sizeof(target));target.sin6_len=sizeof(target);
    target.sin6_family=AF_INET6;target.sin6_port=htons(443);
    memcpy(target.sin6_addr.s6_addr,address,16);
    p->tcp_attempted=1;errno=0;
    fd=socket(AF_INET6,SOCK_STREAM,IPPROTO_TCP);
    if (fd<0) { p->tcp_result=fd;p->tcp_error=errno;
        strcpy(p->tcp_outcome,"socket_error");goto finished; }
    errno=0;
    if (ioctl(fd,FIONBIO,&nonblocking)<0) { p->tcp_result=-1;p->tcp_error=errno;
        strcpy(p->tcp_outcome,"nonblocking_failed");close(fd);goto finished; }
    errno=0;rc=connect(fd,(struct sockaddr *)&target,sizeof(target));
    p->tcp_result=rc;p->tcp_error=errno;
    if (rc==0) outcome=IPV6_CONNECT_CONNECTED;
    else if (errno==ENETUNREACH || errno==EHOSTUNREACH || errno==ECONNREFUSED)
        outcome=ipv6_connect_outcome(0,1,0,0,0,errno);
    else {
        FD_ZERO(&writable);FD_ZERO(&exception);
        FD_SET(fd,&writable);FD_SET(fd,&exception);
        timeout.tv_sec=IPV6_TCP_CONNECT_TIMEOUT_MS/1000u;
        timeout.tv_usec=(IPV6_TCP_CONNECT_TIMEOUT_MS%1000u)*1000u;
        errno=0;selected=select(fd+1,NULL,&writable,&exception,&timeout);
        if (selected>0) {
            errno=0;option_rc=getsockopt(fd,SOL_SOCKET,SO_ERROR,&error,&error_size);
            p->tcp_error=option_rc<0?errno:error;p->tcp_so_error=error;
        } else if (selected<0) p->tcp_error=errno;
        outcome=ipv6_connect_outcome(0,selected,
            selected>0 && FD_ISSET(fd,&writable),selected>0 && FD_ISSET(fd,&exception),
            option_rc,selected>0?p->tcp_so_error:p->tcp_error);
    }
    p->tcp_connected=outcome==IPV6_CONNECT_CONNECTED;
    strcpy(p->tcp_outcome,ipv6_connect_outcome_name(outcome));close(fd);
finished:
    p->tcp_elapsed_ms=(uint32_t)(mico_rtos_get_time()-started);
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
static int start_probe(int include_tcp)
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
    result.tcp_requested=include_tcp;strcpy(result.verdict,"running");
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
void ipv6_diagnostic_status_json(char *out,size_t capacity)
{
    ipv6_probe_status_t s;const stage_info_t *a,*k,*p,*v,*n,*d,*t;
    if (!out || !capacity) return;
    if (!probe_ready) { snprintf(out,capacity,"{\"available\":false}");return; }
    mico_rtos_lock_mutex(&result_mutex);s=result;mico_rtos_unlock_mutex(&result_mutex);
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
      "\"ipv6_tcp\":{\"requested\":%s,\"attempted\":%s,\"connected\":%s,\"host\":\"%s\",\"port\":443,\"connect_result\":%d,\"connect_errno\":%d,\"so_error\":%d,\"elapsed_ms\":%lu,\"outcome\":\"%s\",\"skip_reason\":\"%s\"},"
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
      s.tcp_so_error,(unsigned long)s.tcp_elapsed_ms,s.tcp_outcome,s.tcp_skip_reason,
      ST_ARGS(a),ST_ARGS(k),ST_ARGS(p),ST_ARGS(v),ST_ARGS(n),ST_ARGS(d),ST_ARGS(t),
      (unsigned long)s.started_ms,(unsigned long)s.finished_ms,s.tcp_connected?"true":"false");
#undef ST_ARGS
}
