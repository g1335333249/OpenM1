#include "ipv6_diagnostic.h"
#include "wifi_manager.h"
#include "recovery.h"
#include "openm1_log.h"
#include "mico_socket.h"
#include "moc_api.h"
#include <errno.h>
#include <stdio.h>
#include <string.h>

extern const mico_api_t *lib_api_p;
typedef struct {
    int running,done,api_table_present;
    int socket_result,socket_error,parse_result,parse_error;
    int dns_result,dns_error,dns_ipv6_result;
    uint32_t started_ms,finished_ms;
    char verdict[16];
} ipv6_probe_status_t;
static ipv6_probe_status_t result;
static mico_mutex_t result_mutex;
static mico_thread_t probe_thread;
static int probe_ready;

OSStatus ipv6_diagnostic_init(void)
{
    OSStatus err=mico_rtos_init_mutex(&result_mutex);
    if (err!=kNoErr) return err;
    strcpy(result.verdict,"not_tested");
    probe_ready=1;
    return kNoErr;
}

static void ipv6_probe_worker(mico_thread_arg_t arg)
{
    ipv6_probe_status_t local;
    const lwip_api_t *api=lib_api_p?lib_api_p->lwip_apis:NULL;
    struct in6_addr address;
    struct addrinfo hints,*addresses=NULL,*cursor;
    int fd;
    (void)arg;
    memset(&local,0,sizeof(local));
    local.api_table_present=api && api->lwip_socket && api->lwip_close &&
        api->inet_pton && api->lwip_getaddrinfo && api->lwip_freeaddrinfo;
    /* -32768 means a stage was not attempted because an earlier stage failed. */
    local.socket_result=-32768; local.parse_result=-32768; local.dns_result=-32768;
    if (!local.api_table_present || recovery_ota_busy()) {
        strcpy(local.verdict,local.api_table_present?"canceled":"unsupported");
        goto finished;
    }
    errno=0;
    fd=socket(AF_INET6,SOCK_STREAM,IPPROTO_TCP);
    local.socket_result=fd>=0?0:fd;
    local.socket_error=fd<0?errno:0;
    if (fd>=0) close(fd);
    errno=0;
    local.parse_result=inet_pton(AF_INET6,"2001:db8::1",&address);
    local.parse_error=local.parse_result==1?0:errno;
    if (local.socket_result<0 || local.parse_result!=1) {
        strcpy(local.verdict,"unsupported");
        goto finished;
    }
    if (recovery_ota_busy()) { strcpy(local.verdict,"canceled"); goto finished; }
    /* A fixed public AAAA name avoids accepting arbitrary URL or DNS input.
     * DNS failure alone is inconclusive: routers can filter AAAA queries. */
    memset(&hints,0,sizeof(hints));
    hints.ai_family=AF_INET6;
    hints.ai_socktype=SOCK_STREAM;
    errno=0;
    local.dns_result=getaddrinfo("ipv6.google.com",NULL,&hints,&addresses);
    local.dns_error=local.dns_result?errno:0;
    if (!local.dns_result) {
        for (cursor=addresses;cursor;cursor=cursor->ai_next)
            if (cursor->ai_family==AF_INET6 && cursor->ai_addr &&
                cursor->ai_addrlen>=sizeof(struct sockaddr_in6)) {
                local.dns_ipv6_result=1; break;
            }
    }
    if (addresses) freeaddrinfo(addresses);
    if (local.dns_result || !local.dns_ipv6_result) strcpy(local.verdict,"inconclusive");
    else strcpy(local.verdict,"supported");
finished:
    mico_rtos_lock_mutex(&result_mutex);
    result.api_table_present=local.api_table_present;
    result.socket_result=local.socket_result; result.socket_error=local.socket_error;
    result.parse_result=local.parse_result; result.parse_error=local.parse_error;
    result.dns_result=local.dns_result; result.dns_error=local.dns_error;
    result.dns_ipv6_result=local.dns_ipv6_result;
    strcpy(result.verdict,local.verdict);
    result.finished_ms=mico_rtos_get_time();
    result.done=1; result.running=0;
    mico_rtos_unlock_mutex(&result_mutex);
    openm1_log_info("NETWORK","manual IPv6 capability probe: %s (socket=%d parse=%d AAAA=%d)",
                    local.verdict,local.socket_result,local.parse_result,local.dns_result);
    mico_rtos_delete_thread(NULL);
}

int ipv6_diagnostic_start(void)
{
    micoMemInfo_t *memory;
    OSStatus err;
    if (!probe_ready) return -1;
    if (recovery_ota_busy()) return -2;
    if (!wifi_manager_station_ready()) return -3;
    memory=MicoGetMemoryInfo();
    /* Leave more than the OTA 5120+4096 preflight after this transient stack. */
    if (!memory || memory->free_memory<=IPV6_DIAGNOSTIC_WORKER_STACK+9216u+8192u) return -4;
    mico_rtos_lock_mutex(&result_mutex);
    if (result.running) { mico_rtos_unlock_mutex(&result_mutex); return -5; }
    memset(&result,0,sizeof(result));
    result.running=1; result.started_ms=mico_rtos_get_time();
    strcpy(result.verdict,"running");
    mico_rtos_unlock_mutex(&result_mutex);
    err=mico_rtos_create_thread(&probe_thread,MICO_APPLICATION_PRIORITY,
                               "openm1_ipv6_probe",ipv6_probe_worker,
                               IPV6_DIAGNOSTIC_WORKER_STACK,0);
    if (err!=kNoErr) {
        mico_rtos_lock_mutex(&result_mutex);
        result.running=0; result.done=1;
        result.socket_result=err;
        strcpy(result.verdict,"start_failed");
        mico_rtos_unlock_mutex(&result_mutex);
        return -6;
    }
    return 0;
}

void ipv6_diagnostic_status_json(char *out,size_t capacity)
{
    ipv6_probe_status_t snapshot;
    if (!out || !capacity) return;
    if (!probe_ready) { snprintf(out,capacity,"{\"available\":false}"); return; }
    mico_rtos_lock_mutex(&result_mutex);
    snapshot=result;
    mico_rtos_unlock_mutex(&result_mutex);
    snprintf(out,capacity,
      "{\"available\":true,\"manual_only\":true,\"verdict\":\"%s\","
      "\"running\":%s,\"done\":%s,\"api_table_present\":%s,"
      "\"socket_result\":%d,\"socket_errno\":%d,\"parse_result\":%d,\"parse_errno\":%d,"
      "\"aaaa_result\":%d,\"aaaa_errno\":%d,\"aaaa_ipv6\":%s,"
      "\"started_ms\":%lu,\"finished_ms\":%lu,\"dns_name\":\"ipv6.google.com\","
      "\"mqtt_ipv6_enabled\":false}",
      snapshot.verdict,snapshot.running?"true":"false",snapshot.done?"true":"false",
      snapshot.api_table_present?"true":"false",snapshot.socket_result,snapshot.socket_error,
      snapshot.parse_result,snapshot.parse_error,snapshot.dns_result,snapshot.dns_error,
      snapshot.dns_ipv6_result?"true":"false",
      (unsigned long)snapshot.started_ms,(unsigned long)snapshot.finished_ms);
}
