#include "time_sync.h"
#include "config_store.h"
#include "json_min.h"
#include "wifi_manager.h"
#include "recovery.h"
#include "system_stats.h"
#include "openm1_log.h"
#include "mico_socket.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* MX1290 moc_api.c exports these wrappers, but the fixed SDK's public
 * headers only declare their entries in the MOC API table. */
extern OSStatus MicoRtcSetTime(time_t value);
extern OSStatus MicoRtcGetTime(time_t *value);

/* A short-lived worker performs DNS and one UDP request. HTTP only queues it. */
static mico_mutex_t time_mutex;
static mico_thread_t time_thread;
static int time_ready;
static struct {
    uint32_t station_ready_since_ms,next_due_ms,last_success_ms,last_success_utc;
    uint32_t last_duration_ms,failures;
    int running,manual_requested,verified,last_error;
    char server[65];
    uint8_t enabled;
    uint16_t interval_minutes;
    char result[32];
} time_state;

static void time_worker(mico_thread_arg_t arg)
{
    char server[65];
    struct hostent *host;
    struct sockaddr_in target,from;
    socklen_t from_len=sizeof(from);
    uint8_t request[48]={0},response[64],token[8];
    fd_set readfds;
    struct timeval timeout;
    uint32_t started=mico_rtos_get_time(),utc=0,now;
    int fd=-1,n,error=0,selected;
    time_t readback;
    (void)arg;
    mico_rtos_lock_mutex(&time_mutex);
    memcpy(server,time_state.server,sizeof(server));
    mico_rtos_unlock_mutex(&time_mutex);
    if (!wifi_manager_station_ready() || recovery_ota_busy()) {error=1;goto finish;}
    host=gethostbyname(server);
    if (!host || !host->h_addr_list || !host->h_addr_list[0] || host->h_length!=4) {error=2;goto finish;}
    if (recovery_ota_busy() || !wifi_manager_station_ready()) {error=1;goto finish;}
    memset(&target,0,sizeof(target));target.sin_len=sizeof(target);target.sin_family=AF_INET;
    target.sin_port=htons(123);memcpy(&target.sin_addr,host->h_addr_list[0],4);
    fd=socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);
    if (fd<0) {error=3;goto finish;}
    request[0]=0x23; /* NTP version 4, client mode 3. */
    now=mico_rtos_get_time();
    token[0]=0x4f;token[1]=0x4d;token[2]=0x31;token[3]=0x54;
    token[4]=(uint8_t)(now>>24);token[5]=(uint8_t)(now>>16);
    token[6]=(uint8_t)(now>>8);token[7]=(uint8_t)now;
    memcpy(request+40,token,8);
    if (sendto(fd,request,sizeof(request),0,(struct sockaddr*)&target,sizeof(target))!=(int)sizeof(request)) {
        error=4;goto finish;
    }
    FD_ZERO(&readfds);FD_SET(fd,&readfds);
    timeout.tv_sec=OPENM1_NTP_RECEIVE_TIMEOUT_MS/1000u;
    timeout.tv_usec=(OPENM1_NTP_RECEIVE_TIMEOUT_MS%1000u)*1000u;
    selected=select(fd+1,&readfds,NULL,NULL,&timeout);
    if (selected<=0) {error=selected==0?5:6;goto finish;}
    memset(&from,0,sizeof(from));
    n=recvfrom(fd,response,sizeof(response),0,(struct sockaddr*)&from,&from_len);
    if (n<48 || from.sin_family!=AF_INET || from.sin_port!=target.sin_port ||
        from.sin_addr.s_addr!=target.sin_addr.s_addr ||
        !time_sync_parse_reply(response,(size_t)(n<0?0:n),token,&utc)) {error=7;goto finish;}
    if (recovery_ota_busy() || !wifi_manager_station_ready()) {error=1;goto finish;}
    if (MicoRtcSetTime((time_t)utc)!=kNoErr ||
        MicoRtcGetTime(&readback)!=kNoErr || readback<(time_t)(utc-2u) || readback>(time_t)(utc+2u)) {
        error=8;goto finish;
    }
finish:
    if (fd>=0) close(fd);
    now=mico_rtos_get_time();
    mico_rtos_lock_mutex(&time_mutex);
    time_state.last_duration_ms=(uint32_t)(now-started);
    time_state.last_error=error;
    if (!error) {
        time_state.verified=1;time_state.failures=0;
        time_state.last_success_ms=now;time_state.last_success_utc=utc;
        time_state.next_due_ms=now+(uint32_t)time_state.interval_minutes*60000u;
        strcpy(time_state.result,"synchronized");
    } else {
        if (time_state.failures<100) time_state.failures++;
        time_state.next_due_ms=now+(time_state.failures<5?time_state.failures*30000u:300000u);
        strcpy(time_state.result,"failed");
    }
    time_state.running=0;
    mico_rtos_unlock_mutex(&time_mutex);
    if (error) openm1_log_warn("SYSTEM","NTP sync failed stage=%d",error);
    else openm1_log_info("SYSTEM","NTP synchronized UTC=%lu",(unsigned long)utc);
    mico_rtos_delete_thread(NULL);
}
OSStatus time_sync_init(void)
{
    OSStatus result=mico_rtos_init_mutex(&time_mutex);
    if (result!=kNoErr) return result;
    config_store_get_time_settings(time_state.server,&time_state.interval_minutes,&time_state.enabled);
    strcpy(time_state.result,"not_synced");
    time_ready=1;
    return kNoErr;
}
void time_sync_tick(void)
{
    uint32_t now=mico_rtos_get_time();
    char server[65];uint16_t minutes;uint8_t enabled;
    int due,manual,station_ready,ota_busy,low_memory;
    OSStatus result;
    if (!time_ready) return;
    config_store_get_time_settings(server,&minutes,&enabled);
    station_ready=wifi_manager_station_ready();
    ota_busy=recovery_ota_busy();
    low_memory=system_stats_low_memory_safe_mode();
    mico_rtos_lock_mutex(&time_mutex);
    memcpy(time_state.server,server,sizeof(server));
    time_state.interval_minutes=minutes;time_state.enabled=enabled;
    if (!station_ready) {
        time_state.station_ready_since_ms=0;
        mico_rtos_unlock_mutex(&time_mutex);return;
    }
    if (!time_state.station_ready_since_ms) time_state.station_ready_since_ms=now?now:1;
    manual=time_state.manual_requested;
    due=manual || (enabled &&
        (uint32_t)(now-time_state.station_ready_since_ms)>=20000u &&
        (!time_state.next_due_ms || (int32_t)(now-time_state.next_due_ms)>=0));
    if (!due || time_state.running || ota_busy || low_memory ||
        system_stats_stack_fault_quiet_remaining_ms()) {mico_rtos_unlock_mutex(&time_mutex);return;}
    time_state.running=1;time_state.manual_requested=0;
    strcpy(time_state.result,"running");
    mico_rtos_unlock_mutex(&time_mutex);
    if (!system_stats_begin_optional_thread(OPENM1_NTP_WORKER_STACK)) result=kNoMemoryErr;
    else {
        result=mico_rtos_create_thread(&time_thread,MICO_APPLICATION_PRIORITY,
            "openm1_ntp",time_worker,OPENM1_NTP_WORKER_STACK,0);
        system_stats_end_thread_creation();
    }
    if (result!=kNoErr) {
        mico_rtos_lock_mutex(&time_mutex);
        time_state.running=0;time_state.last_error=9;
        time_state.next_due_ms=now+30000u;
        strcpy(time_state.result,"worker_unavailable");
        mico_rtos_unlock_mutex(&time_mutex);
    }
}
int time_sync_request(void)
{
    if (!time_ready || recovery_ota_busy()) return -1;
    mico_rtos_lock_mutex(&time_mutex);
    if (time_state.running || time_state.manual_requested) {mico_rtos_unlock_mutex(&time_mutex);return -1;}
    time_state.manual_requested=1;
    mico_rtos_unlock_mutex(&time_mutex);
    return 0;
}
int time_sync_configure(const char *body,size_t length)
{
    openm1_config_t config;
    json_min_field_t time_fields[3];
    int count,i,have_enabled=0,have_server=0,have_minutes=0;
    if (!time_ready || !body || length>256 || recovery_ota_busy()) return -2;
    count=json_min_parse(body,length,time_fields,3);
    if (count!=3) return -1;
    config_store_get(&config);
    for (i=0;i<count;i++) {
        json_min_field_t *f=&time_fields[i];
        if (!strcmp(f->key,"enabled") && f->kind=='b') {
            config.ntp_enabled=!strcmp(f->value,"true");have_enabled=1;
        } else if (!strcmp(f->key,"server") && f->kind=='s' &&
            time_sync_server_valid(f->value)) {
            snprintf(config.ntp_server,sizeof(config.ntp_server),"%s",f->value);have_server=1;
        } else if (!strcmp(f->key,"interval_minutes") && f->kind=='n') {
            char *end;unsigned long value=strtoul(f->value,&end,10);
            if (*end || value<15 || value>1440) return -1;
            config.ntp_interval_minutes=(uint16_t)value;have_minutes=1;
        } else return -1;
    }
    if (!have_enabled || !have_server || !have_minutes) return -1;
    return config_store_save(&config)==kNoErr?0:-2;
}
void time_sync_status_json(char *out,size_t capacity)
{
    uint32_t now=mico_rtos_get_time(),utc=0,remaining=0;
    char server[65],result[32];uint32_t last_success,duration,failures,next;
    int running,verified,error,enabled;unsigned minutes;
    if (!time_ready) {snprintf(out,capacity,"{\"available\":false}");return;}
    mico_rtos_lock_mutex(&time_mutex);
    memcpy(server,time_state.server,sizeof(server));
    memcpy(result,time_state.result,sizeof(result));
    last_success=time_state.last_success_ms;duration=time_state.last_duration_ms;
    failures=time_state.failures;next=time_state.next_due_ms;
    running=time_state.running;verified=time_state.verified;error=time_state.last_error;
    enabled=time_state.enabled;minutes=time_state.interval_minutes;
    if (verified) utc=time_state.last_success_utc+(uint32_t)(now-last_success)/1000u;
    mico_rtos_unlock_mutex(&time_mutex);
    if (next && (int32_t)(next-now)>0) remaining=next-now;
    snprintf(out,capacity,"{\"current_utc\":%lu,\"current_cst\":%lu,\"ntp_verified\":%s,"
        "\"server\":\"%s\",\"auto_sync\":%s,\"interval_minutes\":%u,\"running\":%s,"
        "\"last_success_ms\":%lu,\"last_result\":\"%s\",\"last_duration_ms\":%lu,"
        "\"last_error_code\":%d,\"consecutive_failures\":%lu,\"next_sync_remaining_ms\":%lu}",
        (unsigned long)utc,(unsigned long)(utc?utc+28800u:0u),verified?"true":"false",
        server,enabled?"true":"false",minutes,running?"true":"false",
        (unsigned long)last_success,result,(unsigned long)duration,error,
        (unsigned long)failures,(unsigned long)remaining);
}
