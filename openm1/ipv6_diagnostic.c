#include "ipv6_diagnostic.h"
#include "ipv6_probe_logic.h"
#include "wifi_manager.h"
#include "recovery.h"
#include "openm1_log.h"
#include "mico_socket.h"
#include "moc_api.h"
#include <errno.h>
#include <stdio.h>
#include <string.h>

#if AF_INET6 != 10
#error "IPv6 diagnostic requires the fixed MiCO AF_INET6 ABI"
#endif

#define IPV6_PRIMARY_DNS "www.ustc.edu.cn"
#define IPV6_FALLBACK_DNS "ipv6.mirrors.ustc.edu.cn"
#define IPV6_PROBE_RETRY_GAP_MS 30000u
extern const mico_api_t *lib_api_p;

typedef struct {
    int result,error;
    char bytes[40];
} parse_case_t;
typedef struct {
    int running,done,tcp_requested,api_table_present;
    int socket_result,socket_error;
    int dns_result,dns_error,dns_ipv6_result,dns_primary_result,dns_fallback_result;
    int numeric_result,numeric_error,numeric_ipv6;
    int tcp_attempted,tcp_connected,tcp_result,tcp_error,tcp_so_error;
    uint32_t tcp_elapsed_ms,dns_elapsed_ms,started_ms,finished_ms;
    parse_case_t compressed,full,loopback,ipv4;
    char verdict[20],dns_name[36],dns_address[40],numeric_address[40];
    char dns_skip_reason[24],tcp_skip_reason[24],tcp_outcome[20],stage[24];
} ipv6_probe_status_t;
static ipv6_probe_status_t result;
static mico_mutex_t result_mutex;
static mico_thread_t probe_thread;
static int probe_ready;

static void init_result(ipv6_probe_status_t *probe)
{
    memset(probe,0,sizeof(*probe));
    probe->socket_result=IPV6_PROBE_NOT_ATTEMPTED;
    probe->compressed.result=IPV6_PROBE_NOT_ATTEMPTED;
    probe->full.result=IPV6_PROBE_NOT_ATTEMPTED;
    probe->loopback.result=IPV6_PROBE_NOT_ATTEMPTED;
    probe->ipv4.result=IPV6_PROBE_NOT_ATTEMPTED;
    probe->numeric_result=IPV6_PROBE_NOT_ATTEMPTED;
    probe->dns_result=IPV6_PROBE_NOT_ATTEMPTED;
    probe->dns_primary_result=IPV6_PROBE_NOT_ATTEMPTED;
    probe->dns_fallback_result=IPV6_PROBE_NOT_ATTEMPTED;
    probe->tcp_result=IPV6_PROBE_NOT_ATTEMPTED;
    strcpy(probe->verdict,"not_tested");
    strcpy(probe->dns_name,IPV6_PRIMARY_DNS);
    strcpy(probe->tcp_outcome,"not_tested");
    strcpy(probe->dns_skip_reason,"not_started");
    strcpy(probe->tcp_skip_reason,"not_requested");
    strcpy(probe->stage,"not_started");
}

static void update_stage(const char *stage)
{
    mico_rtos_lock_mutex(&result_mutex);
    snprintf(result.stage,sizeof(result.stage),"%s",stage);
    mico_rtos_unlock_mutex(&result_mutex);
}

OSStatus ipv6_diagnostic_init(void)
{
    OSStatus err=mico_rtos_init_mutex(&result_mutex);
    if (err!=kNoErr) return err;
    init_result(&result);
    probe_ready=1;
    return kNoErr;
}

static void format_address(const uint8_t *bytes,unsigned count,char *out,size_t capacity)
{
    size_t used=0;
    unsigned i;
    if (!out || !capacity) return;
    out[0]=0;
    for (i=0;i<count;i++) {
        int n;
        if (used+3u+(count==16 && (i&1) && i!=15?1u:0u)>capacity) break;
        n=snprintf(out+used,capacity-used,"%02X%s",bytes[i],
                   count==16 && (i&1) && i!=15?":":"");
        if (n<0 || (size_t)n>=capacity-used) break;
        used+=(size_t)n;
    }
}
static void parse_one(int family,const char *input,parse_case_t *item,unsigned byte_count)
{
    uint8_t address[16];
    memset(address,0xA5,sizeof(address));
    errno=0;
    item->result=inet_pton(family,input,address);
    item->error=errno;
    format_address(address,byte_count,item->bytes,sizeof(item->bytes));
}
static const struct sockaddr_in6 *find_ipv6(struct addrinfo *head)
{
    struct addrinfo *item;
    static const uint8_t unspecified[16]={0};
    for (item=head;item;item=item->ai_next) {
        const struct sockaddr_in6 *address;
        if (!item->ai_addr || item->ai_addrlen<sizeof(struct sockaddr_in6) ||
            item->ai_family!=AF_INET6) continue;
        address=(const struct sockaddr_in6 *)item->ai_addr;
        if (ipv6_addrinfo_valid(item->ai_family,(unsigned)item->ai_addrlen,
                                sizeof(*address),address->sin6_family,address->sin6_len) &&
            memcmp(address->sin6_addr.s6_addr,unspecified,16))
            return address;
    }
    return NULL;
}
static int lookup_ipv6(const char *name,const char *service,
                       char *address_text,size_t capacity,int *lookup_error,
                       int *valid_address,struct addrinfo **owned)
{
    struct addrinfo hints;
    const struct sockaddr_in6 *address;
    int rc;
    memset(&hints,0,sizeof(hints));
    hints.ai_family=AF_INET6;
    hints.ai_socktype=SOCK_STREAM;
    errno=0;
    *owned=NULL; *valid_address=0;
    rc=getaddrinfo(name,service,&hints,owned);
    *lookup_error=errno;
    if (rc) return rc;
    address=find_ipv6(*owned);
    if (address) {
        format_address(address->sin6_addr.s6_addr,16,address_text,capacity);
        *valid_address=1;
    }
    return rc;
}
static void tcp_connect_probe(ipv6_probe_status_t *probe,const struct sockaddr_in6 *address)
{
    struct sockaddr_in6 target=*address;
    unsigned long nonblocking=1;
    struct timeval timeout;
    fd_set writable,exception;
    int fd,rc,error=0,option_rc=IPV6_PROBE_NOT_ATTEMPTED,selected=IPV6_PROBE_NOT_ATTEMPTED;
    socklen_t error_size=sizeof(error);
    uint32_t started=mico_rtos_get_time();
    ipv6_connect_outcome_t outcome;
    target.sin6_len=sizeof(target);
    target.sin6_family=AF_INET6;
    target.sin6_port=htons(443);
    probe->tcp_attempted=1;
    errno=0;
    fd=socket(AF_INET6,SOCK_STREAM,IPPROTO_TCP);
    if (fd<0) {
        probe->tcp_result=fd; probe->tcp_error=errno;
        strcpy(probe->tcp_outcome,"socket_error");
        goto finished;
    }
    errno=0;
    if (ioctl(fd,FIONBIO,&nonblocking)<0) {
        probe->tcp_result=-1; probe->tcp_error=errno;
        strcpy(probe->tcp_outcome,"nonblocking_failed");
        close(fd); goto finished;
    }
    errno=0;
    rc=connect(fd,(struct sockaddr *)&target,sizeof(target));
    probe->tcp_result=rc;
    probe->tcp_error=errno;
    if (rc==0) {
        outcome=IPV6_CONNECT_CONNECTED;
    } else if (errno==ENETUNREACH || errno==EHOSTUNREACH || errno==ECONNREFUSED) {
        outcome=ipv6_connect_outcome(0,1,0,0,0,errno);
    } else {
        FD_ZERO(&writable); FD_ZERO(&exception);
        FD_SET(fd,&writable); FD_SET(fd,&exception);
        timeout.tv_sec=IPV6_TCP_CONNECT_TIMEOUT_MS/1000u;
        timeout.tv_usec=(IPV6_TCP_CONNECT_TIMEOUT_MS%1000u)*1000u;
        errno=0;
        selected=select(fd+1,NULL,&writable,&exception,&timeout);
        if (selected>0) {
            errno=0;
            option_rc=getsockopt(fd,SOL_SOCKET,SO_ERROR,&error,&error_size);
            if (option_rc<0) probe->tcp_error=errno;
            else probe->tcp_error=error;
            probe->tcp_so_error=error;
        } else if (selected<0) probe->tcp_error=errno;
        outcome=ipv6_connect_outcome(0,selected,
            selected>0 && FD_ISSET(fd,&writable),
            selected>0 && FD_ISSET(fd,&exception),option_rc,
            selected>0?probe->tcp_so_error:probe->tcp_error);
    }
    probe->tcp_connected=outcome==IPV6_CONNECT_CONNECTED;
    strcpy(probe->tcp_outcome,ipv6_connect_outcome_name(outcome));
    close(fd);
finished:
    probe->tcp_elapsed_ms=(uint32_t)(mico_rtos_get_time()-started);
}
static void ipv6_probe_worker(mico_thread_arg_t arg)
{
    ipv6_probe_status_t local;
    const lwip_api_t *api=lib_api_p?lib_api_p->lwip_apis:NULL;
    struct addrinfo *addresses=NULL;
    const struct sockaddr_in6 *address;
    uint32_t dns_started;
    int fd,valid;
    (void)arg;
    init_result(&local);
    mico_rtos_lock_mutex(&result_mutex);
    local.started_ms=result.started_ms;
    local.tcp_requested=result.tcp_requested;
    mico_rtos_unlock_mutex(&result_mutex);
    local.api_table_present=api && api->lwip_socket && api->lwip_close &&
        api->inet_pton && api->lwip_getaddrinfo && api->lwip_freeaddrinfo &&
        api->lwip_ioctl && api->lwip_connect && api->lwip_select && api->lwip_getsockopt;
    if (recovery_ota_busy()) {
        strcpy(local.verdict,"canceled");
        strcpy(local.dns_skip_reason,"ota_busy");
        strcpy(local.tcp_skip_reason,"ota_busy");
        goto finished;
    }
    if (api && api->lwip_socket && api->lwip_close) {
        update_stage("socket");
        errno=0;
        fd=socket(AF_INET6,SOCK_STREAM,IPPROTO_TCP);
        local.socket_result=fd>=0?0:fd;
        local.socket_error=fd<0?errno:0;
        if (fd>=0) close(fd);
    }
    if (api && api->inet_pton) {
        update_stage("numeric_parse");
        parse_one(AF_INET6,"2001:db8::1",&local.compressed,16);
        parse_one(AF_INET6,"2001:0db8:0000:0000:0000:0000:0000:0001",&local.full,16);
        parse_one(AF_INET6,"::1",&local.loopback,16);
        parse_one(AF_INET,"192.0.2.1",&local.ipv4,4);
    }
    /* Numeric getaddrinfo is independent of the Kernel inet_pton result. */
    if (api && api->lwip_getaddrinfo && api->lwip_freeaddrinfo && !recovery_ota_busy()) {
        update_stage("numeric_getaddrinfo");
        local.numeric_result=lookup_ipv6("2001:db8::1",NULL,local.numeric_address,
            sizeof(local.numeric_address),&local.numeric_error,&valid,&addresses);
        local.numeric_ipv6=valid;
        if (addresses) { freeaddrinfo(addresses); addresses=NULL; }
    }
    if (!api || !api->lwip_getaddrinfo || !api->lwip_freeaddrinfo) {
        strcpy(local.dns_skip_reason,"api_missing");
        strcpy(local.tcp_skip_reason,"dns_api_missing");
        goto verdict;
    }
    if (!ipv6_should_query_dns(local.compressed.result,local.socket_result,
        recovery_ota_busy(),1)) {
        strcpy(local.dns_skip_reason,"ota_busy");
        strcpy(local.tcp_skip_reason,"ota_busy");
        goto verdict;
    }
    local.dns_skip_reason[0]=0;
    dns_started=mico_rtos_get_time();
    update_stage("aaaa_primary");
    local.dns_primary_result=lookup_ipv6(IPV6_PRIMARY_DNS,"443",local.dns_address,
        sizeof(local.dns_address),&local.dns_error,&valid,&addresses);
    local.dns_result=local.dns_primary_result;
    local.dns_ipv6_result=valid;
    strcpy(local.dns_name,IPV6_PRIMARY_DNS);
    if (addresses && (!valid || !local.tcp_requested)) { freeaddrinfo(addresses); addresses=NULL; }
    local.dns_elapsed_ms=(uint32_t)(mico_rtos_get_time()-dns_started);
    if (!valid && local.dns_elapsed_ms<IPV6_DNS_SOFT_BUDGET_MS && !recovery_ota_busy()) {
        update_stage("aaaa_fallback");
        if (addresses) { freeaddrinfo(addresses); addresses=NULL; }
        local.dns_fallback_result=lookup_ipv6(IPV6_FALLBACK_DNS,"443",local.dns_address,
            sizeof(local.dns_address),&local.dns_error,&valid,&addresses);
        local.dns_result=local.dns_fallback_result;
        local.dns_ipv6_result=valid;
        strcpy(local.dns_name,IPV6_FALLBACK_DNS);
        local.dns_elapsed_ms=(uint32_t)(mico_rtos_get_time()-dns_started);
    }
    if (!valid && local.dns_fallback_result==IPV6_PROBE_NOT_ATTEMPTED &&
        local.dns_elapsed_ms>=IPV6_DNS_SOFT_BUDGET_MS)
        strcpy(local.dns_skip_reason,"fallback_skipped_slow");
    if (!valid) {
        strcpy(local.tcp_skip_reason,"no_valid_aaaa");
    } else if (!local.tcp_requested) {
        strcpy(local.tcp_skip_reason,"not_requested");
    } else if (recovery_ota_busy()) {
        strcpy(local.tcp_skip_reason,"ota_busy");
    } else if (!api->lwip_ioctl || !api->lwip_connect || !api->lwip_select ||
               !api->lwip_getsockopt || !api->lwip_socket || !api->lwip_close) {
        strcpy(local.tcp_skip_reason,"tcp_api_missing");
    } else {
        address=find_ipv6(addresses);
        if (address) {
            update_stage("tcp_connect");
            local.tcp_skip_reason[0]=0;
            tcp_connect_probe(&local,address);
        } else strcpy(local.tcp_skip_reason,"bad_sockaddr");
    }
    if (addresses) freeaddrinfo(addresses);
verdict:
    strcpy(local.verdict,ipv6_probe_verdict(local.api_table_present,local.socket_result,
        local.tcp_attempted,local.tcp_connected));
finished:
    strcpy(local.stage,"complete");
    mico_rtos_lock_mutex(&result_mutex);
    local.finished_ms=mico_rtos_get_time();
    local.running=0; local.done=1;
    result=local;
    mico_rtos_unlock_mutex(&result_mutex);
    openm1_log_info("NETWORK","manual IPv6 probe: %s socket=%d parse=%d AAAA=%d TCP=%s",
        local.verdict,local.socket_result,local.compressed.result,local.dns_result,local.tcp_outcome);
    mico_rtos_delete_thread(NULL);
}
static int start_probe(int include_tcp)
{
    micoMemInfo_t *memory;
    OSStatus err;
    uint32_t now=mico_rtos_get_time();
    if (!probe_ready) return -1;
    if (recovery_ota_busy()) return -2;
    if (!wifi_manager_station_ready()) return -3;
    memory=MicoGetMemoryInfo();
    /* Leave more than the OTA 5120+4096 preflight after this transient stack. */
    if (!memory || memory->free_memory<=IPV6_DIAGNOSTIC_WORKER_STACK+9216u+8192u) return -4;
    mico_rtos_lock_mutex(&result_mutex);
    if (result.running) { mico_rtos_unlock_mutex(&result_mutex); return -5; }
    if (result.done && (uint32_t)(now-result.finished_ms)<IPV6_PROBE_RETRY_GAP_MS) {
        mico_rtos_unlock_mutex(&result_mutex); return -7;
    }
    init_result(&result);
    result.running=1; result.started_ms=now; result.tcp_requested=include_tcp;
    strcpy(result.verdict,"running");
    mico_rtos_unlock_mutex(&result_mutex);
    err=mico_rtos_create_thread(&probe_thread,MICO_APPLICATION_PRIORITY,
                               "openm1_ipv6_probe",ipv6_probe_worker,
                               IPV6_DIAGNOSTIC_WORKER_STACK,0);
    if (err!=kNoErr) {
        mico_rtos_lock_mutex(&result_mutex);
        result.running=0; result.done=1; result.finished_ms=mico_rtos_get_time();
        result.socket_result=err;
        strcpy(result.verdict,"start_failed");
        mico_rtos_unlock_mutex(&result_mutex);
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
    ipv6_probe_status_t snapshot;
    int dns_overdue;
    if (!out || !capacity) return;
    if (!probe_ready) { snprintf(out,capacity,"{\"available\":false}"); return; }
    mico_rtos_lock_mutex(&result_mutex);
    snapshot=result;
    mico_rtos_unlock_mutex(&result_mutex);
    dns_overdue=snapshot.running &&
        (!strncmp(snapshot.stage,"aaaa_",5) || !strcmp(snapshot.stage,"numeric_getaddrinfo")) &&
        (uint32_t)(mico_rtos_get_time()-snapshot.started_ms)>=IPV6_DNS_SOFT_BUDGET_MS;
    snprintf(out,capacity,
      "{\"available\":true,\"manual_only\":true,\"verdict\":\"%s\",\"running\":%s,\"done\":%s,\"stage\":\"%s\",\"dns_call_overdue\":%s,\"api_table_present\":%s,"
      "\"socket_result\":%d,\"socket_errno\":%d,\"parse_result\":%d,\"parse_errno\":%d,\"parse_status\":\"%s\",\"parse_output_hex\":\"%s\","
      "\"parse_full_result\":%d,\"parse_full_errno\":%d,\"parse_full_output_hex\":\"%s\","
      "\"parse_loopback_result\":%d,\"parse_loopback_errno\":%d,\"parse_loopback_output_hex\":\"%s\","
      "\"parse_ipv4_result\":%d,\"parse_ipv4_errno\":%d,\"parse_ipv4_output_hex\":\"%s\","
      "\"numeric_getaddrinfo_result\":%d,\"numeric_getaddrinfo_errno\":%d,\"numeric_getaddrinfo_ipv6\":%s,\"numeric_address\":\"%s\","
      "\"aaaa_result\":%d,\"aaaa_errno\":%d,\"aaaa_ipv6\":%s,\"aaaa_address\":\"%s\",\"dns_name\":\"%s\","
      "\"dns_primary_result\":%d,\"dns_fallback_result\":%d,\"dns_elapsed_ms\":%lu,\"dns_skip_reason\":\"%s\","
      "\"ipv6_tcp\":{\"requested\":%s,\"attempted\":%s,\"connected\":%s,\"host\":\"%s\",\"port\":443,"
      "\"connect_result\":%d,\"connect_errno\":%d,\"so_error\":%d,\"elapsed_ms\":%lu,\"outcome\":\"%s\",\"skip_reason\":\"%s\"},"
      "\"started_ms\":%lu,\"finished_ms\":%lu,\"mqtt_ipv6_feasible\":%s,\"mqtt_ipv6_enabled\":false}",
      snapshot.verdict,snapshot.running?"true":"false",snapshot.done?"true":"false",snapshot.stage,
      dns_overdue?"true":"false",
      snapshot.api_table_present?"true":"false",snapshot.socket_result,snapshot.socket_error,
      snapshot.compressed.result,snapshot.compressed.error,
      ipv6_parse_status(snapshot.compressed.result),snapshot.compressed.bytes,
      snapshot.full.result,snapshot.full.error,snapshot.full.bytes,
      snapshot.loopback.result,snapshot.loopback.error,snapshot.loopback.bytes,
      snapshot.ipv4.result,snapshot.ipv4.error,snapshot.ipv4.bytes,
      snapshot.numeric_result,snapshot.numeric_error,snapshot.numeric_ipv6?"true":"false",snapshot.numeric_address,
      snapshot.dns_result,snapshot.dns_error,snapshot.dns_ipv6_result?"true":"false",snapshot.dns_address,snapshot.dns_name,
      snapshot.dns_primary_result,snapshot.dns_fallback_result,(unsigned long)snapshot.dns_elapsed_ms,snapshot.dns_skip_reason,
      snapshot.tcp_requested?"true":"false",snapshot.tcp_attempted?"true":"false",snapshot.tcp_connected?"true":"false",
      snapshot.dns_name,snapshot.tcp_result,snapshot.tcp_error,snapshot.tcp_so_error,
      (unsigned long)snapshot.tcp_elapsed_ms,snapshot.tcp_outcome,snapshot.tcp_skip_reason,
      (unsigned long)snapshot.started_ms,(unsigned long)snapshot.finished_ms,
      snapshot.tcp_connected?"true":"false");
}
