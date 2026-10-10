#include "wifi_manager.h"
#include "openm1_log.h"
#include "recovery.h"
#include "network_health.h"
#include "config_store.h"
#include "wifi_settings.h"
#include "wifi_station_logic.h"
#include "m1_display.h"
#include <stdio.h>
#include <string.h>

typedef struct {
    char saved_ssid[32];
    uint8_t auto_connect;
    uint8_t disable_ap_after_connect;
} wifi_policy_t;

typedef struct {
    char state[16],ssid[32],ip[16],gateway[16],netmask[16],dns[16],message[96];
    int rssi,active,link_cached,ip_valid_cached,last_wlan_error;
    int wifi_control_worker_running,station_armed,station_started_once;
    wifi_station_phase_t station_phase;
    uint32_t link_up_since_ms,disconnect_count,last_disconnect_ms,last_connect_ms;
    uint32_t native_reconnect_since_ms,native_reconnect_success_count;
    uint32_t last_native_reconnect_success_ms,station_rearm_count,last_station_rearm_ms;
    uint32_t consecutive_bad_samples,consecutive_good_samples;
    uint32_t boot_auto_connect_not_before_ms;
    int boot_auto_connect_waiting;
    char last_wlan_operation[32];
    int last_link_query_result,last_ip_query_result,last_query_link_connected;
    int last_query_ip_valid,last_query_rssi;
    uint32_t wifi_control_loop_count,last_wifi_control_tick_ms;
    int recovery_ap_observed_on,recovery_ap_policy_closed,recovery_ap_restoring;
    uint32_t recovery_ap_restore_count,recovery_ap_last_probe_ms,recovery_ap_last_restore_ms;
    uint32_t ap_probe_failures;
    int recovery_ap_last_error;
} wifi_status_t;

static wifi_status_t status={.state="disconnected",.station_phase=WIFI_STA_IDLE};
static wifi_policy_t policy;
static wifi_station_desired_t desired_station;
static mico_mutex_t status_mutex;
static mico_thread_t wifi_control_thread;
static int manager_ready,control_worker_created;
static uint32_t desired_revision;
static int disconnect_requested,scan_requested;
#define WIFI_SCAN_TIMEOUT_MS 15000u

typedef struct { char ssid[33]; int rssi; unsigned channel; wlan_sec_type_t security; } scan_ap_t;
static scan_ap_t scan_aps[WIFI_SCAN_MAX_AP];
static unsigned scan_count;
static char scan_state[12]="idle";
static uint32_t scan_started_ms;
static int scan_registered;
/* 32 compact transition snapshots. The old 3300-byte scan JSON buffer is
 * streamed by HTTP, so this history does not raise the permanent RAM budget. */
#define WIFI_HISTORY_CAPACITY 32u
typedef struct {
    uint32_t sequence,uptime_ms,disconnect_count,native_reconnect_count;
    uint32_t station_rearm_count,ap_restore_count;
    int32_t link_query_result,ip_query_result,wlan_error;
    int16_t rssi,event_code;
    uint8_t reason,phase,link_connected,ip_valid,health_state;
    uint8_t display_target,pwm_applied,ap_observed;
} wifi_history_record_t;
static wifi_history_record_t wifi_history[WIFI_HISTORY_CAPACITY];
static uint32_t history_next_sequence=1;
static unsigned history_head,history_count;
static volatile uint32_t wifi_connect_fail_count,wifi_fatal_error_count;
static volatile uint32_t wifi_event_bits;
static volatile int last_wifi_event_code,last_wifi_connect_fail_error;
#define WIFI_EVENT_STATION_UP (1u<<0)
#define WIFI_EVENT_STATION_DOWN (1u<<1)
#define WIFI_EVENT_AP_UP (1u<<2)
#define WIFI_EVENT_AP_DOWN (1u<<3)
static void wifi_connect_failed_notice(OSStatus err,void *arg)
{ (void)arg; last_wifi_connect_fail_error=err; wifi_connect_fail_count++; }
static void wifi_fatal_notice(void *arg)
{ (void)arg; wifi_fatal_error_count++; }
static void wifi_status_notice(WiFiEvent event,void *arg)
{
    uint32_t bit=0;
    (void)arg;
    switch (event) {
    case NOTIFY_STATION_UP: bit=WIFI_EVENT_STATION_UP; break;
    case NOTIFY_STATION_DOWN: bit=WIFI_EVENT_STATION_DOWN; break;
    case NOTIFY_AP_UP: bit=WIFI_EVENT_AP_UP; break;
    case NOTIFY_AP_DOWN: bit=WIFI_EVENT_AP_DOWN; break;
    default: return;
    }
    last_wifi_event_code=event;
    __atomic_fetch_or(&wifi_event_bits,bit,__ATOMIC_RELAXED);
}
static void lock_status(void);
static void unlock_status(void);
static int scan_is_active(void);
static void scan_complete(ScanResult_adv *result, void *arg)
{
    unsigned i,j,limit;
    scan_ap_t item;
    (void)arg;
    lock_status();
    scan_count=0;
    if (!result || (signed char)result->ApNum<0 || !result->ApList) {
        strcpy(scan_state,"failed");
        unlock_status();
        return;
    }
    limit=(unsigned char)result->ApNum;
    if (limit>100) limit=100;
    for (i=0;i<limit;i++) {
        size_t n=strnlen(result->ApList[i].ssid,32);
        int existing=-1;
        if (!n) continue;
        for (j=0;j<scan_count;j++)
            if (strlen(scan_aps[j].ssid)==n && !memcmp(scan_aps[j].ssid,result->ApList[i].ssid,n)) { existing=(int)j; break; }
        if (existing>=0 && scan_aps[existing].rssi>=result->ApList[i].rssi) continue;
        if (existing<0 && scan_count>=WIFI_SCAN_MAX_AP) {
            int weakest=0;
            for (j=1;j<scan_count;j++) if (scan_aps[j].rssi<scan_aps[weakest].rssi) weakest=(int)j;
            if (result->ApList[i].rssi<=scan_aps[weakest].rssi) continue;
            existing=weakest;
        }
        memset(&item,0,sizeof(item));
        memcpy(item.ssid,result->ApList[i].ssid,n);
        item.rssi=result->ApList[i].rssi;
        item.channel=(unsigned char)result->ApList[i].channel;
        item.security=result->ApList[i].security;
        if (existing<0) scan_aps[scan_count++]=item;
        else scan_aps[existing]=item;
    }
    for (i=0;i<scan_count;i++) for (j=i+1;j<scan_count;j++)
        if (scan_aps[j].rssi>scan_aps[i].rssi) { item=scan_aps[i];scan_aps[i]=scan_aps[j];scan_aps[j]=item; }
    strcpy(scan_state,"ready");
    unlock_status();
}

static void lock_status(void) { if (manager_ready) mico_rtos_lock_mutex(&status_mutex); }
static void unlock_status(void) { if (manager_ready) mico_rtos_unlock_mutex(&status_mutex); }
static void clear_network(void)
{
    status.ip[0]=0; status.gateway[0]=0; status.netmask[0]=0; status.dns[0]=0;
    status.rssi=0; status.link_cached=0; status.ip_valid_cached=0;
    status.link_up_since_ms=0;
}
static void copy_ip(char out[16], const char in[16])
{
    memcpy(out,in,16); out[15]=0;
}
static int valid_ip(const char *ip)
{
    return ip[0] && strcmp(ip,"0.0.0.0")!=0;
}
static int valid_input(const char *s, size_t max)
{
    size_t n=0;
    if (!s) return 0;
    while (s[n]) {
        unsigned char c=(unsigned char)s[n];
        if (c<32 || c==127 || n>=max) return 0;
        n++;
    }
    return n>0;
}

/* WLAN HAL calls below are owned by wifi_control_worker only. */
static void wlan_operation(const char *name)
{
    lock_status(); snprintf(status.last_wlan_operation,sizeof(status.last_wlan_operation),"%s",name); unlock_status();
}
static OSStatus station_suspend(void)
{
    OSStatus err;
    wlan_operation("suspend_station");
    openm1_log_info("WIFI","calling SuspendStation");
    err=micoWlanSuspendStation();
    openm1_log_info("WIFI","SuspendStation result = %d",err);
    return err;
}
static OSStatus station_start(const wifi_station_desired_t *desired)
{
    network_InitTypeDef_st config;
    OSStatus err;
    memset(&config,0,sizeof(config));
    config.wifi_mode=Station;
    memcpy(config.wifi_ssid,desired->ssid,strlen(desired->ssid)+1);
    memcpy(config.wifi_key,desired->password,strlen(desired->password)+1);
    config.dhcpMode=DHCP_Client;
    config.wifi_retry_interval=WIFI_NATIVE_RETRY_INTERVAL_MS;
    wlan_operation("start_station");
    openm1_log_info("WIFI","calling StartNetwork(STA)");
    err=StartNetwork(&config);
    openm1_log_info("WIFI","StartNetwork(STA) result = %d",err);
    memset(config.wifi_key,0,sizeof(config.wifi_key));
    return err;
}
static void station_snapshot(LinkStatusTypeDef *link,IPStatusTypedef *ip,int *good,int *last_error)
{
    OSStatus err,ip_err=-32768;
    int ip_queried=0;
    memset(link,0,sizeof(*link)); memset(ip,0,sizeof(*ip));
    wlan_operation("get_station_status");
    err=micoWlanGetLinkStatus(link);
    if (err==kNoErr && link->is_connected==1) {
        ip_queried=1;
        ip_err=micoWlanGetIPStatus(ip,Station);
        ip->ip[sizeof(ip->ip)-1]=0;
    }
    *good=err==kNoErr && link->is_connected==1 && ip_err==kNoErr && valid_ip(ip->ip);
    *last_error=err!=kNoErr?err:ip_queried?ip_err:0;
    lock_status();
    status.last_link_query_result=err;
    status.last_ip_query_result=ip_err;
    status.last_query_link_connected=err==kNoErr?link->is_connected:-1;
    status.last_query_ip_valid=ip_err==kNoErr?valid_ip(ip->ip):-1;
    status.last_query_rssi=err==kNoErr?link->rssi:0;
    unlock_status();
}
static void station_cache_good(const LinkStatusTypeDef *link,const IPStatusTypedef *ip)
{
    copy_ip(status.ip,ip->ip); copy_ip(status.gateway,ip->gate);
    copy_ip(status.netmask,ip->mask); copy_ip(status.dns,ip->dns);
    status.rssi=link->rssi; status.link_cached=1; status.ip_valid_cached=1;
}
static OSStatus recovery_ap_start(void)
{
    network_InitTypeDef_st config;
    OSStatus err;
    memset(&config,0,sizeof(config));
    config.wifi_mode=Soft_AP;
    snprintf(config.wifi_ssid,sizeof(config.wifi_ssid),"%s",recovery_ssid());
    memcpy(config.local_ip_addr,RECOVERY_IP,sizeof(RECOVERY_IP));
    memcpy(config.net_mask,"255.255.255.0",sizeof("255.255.255.0"));
    memcpy(config.gateway_ip_addr,RECOVERY_IP,sizeof(RECOVERY_IP));
    memcpy(config.dnsServer_ip_addr,RECOVERY_IP,sizeof(RECOVERY_IP));
    config.dhcpMode=DHCP_Server;
    wlan_operation("start_softap");
    openm1_log_info("WIFI","calling StartNetwork(SoftAP)");
    err=StartNetwork(&config);
    openm1_log_info("WIFI","StartNetwork(SoftAP) result = %d",err);
    return err;
}
static int recovery_ap_probe(void)
{
    IPStatusTypedef ap;
    OSStatus err;
    int observed;
    memset(&ap,0,sizeof(ap));
    wlan_operation("get_softap_status");
    err=micoWlanGetIPStatus(&ap,Soft_AP);
    ap.ip[sizeof(ap.ip)-1]=0;
    observed=err==kNoErr && !strcmp(ap.ip,RECOVERY_IP);
    lock_status();
    status.recovery_ap_observed_on=observed;
    status.recovery_ap_last_probe_ms=mico_rtos_get_time();
    if (observed) status.recovery_ap_last_error=0;
    else if (!status.recovery_ap_last_error) status.recovery_ap_last_error=err==kNoErr?-1:err;
    unlock_status();
    return observed;
}
static int scan_is_active(void)
{
    int active;
    lock_status();
    if (!strcmp(scan_state,"scanning") && mico_rtos_get_time()-scan_started_ms>WIFI_SCAN_TIMEOUT_MS)
        strcpy(scan_state,"failed");
    active=!strcmp(scan_state,"scanning");
    unlock_status();
    return active;
}
static void display_station_fallback(int station_ready)
{
    if (!network_health_available())
        m1_display_set_network_state(station_ready?M1_NET_DISPLAY_ONLINE:M1_NET_DISPLAY_DISCONNECTED);
}
static void ap_control_step(uint32_t now,wifi_station_phase_t phase,
                            uint32_t *eligible_since,uint32_t *next_restore_ms,
                            unsigned *restore_failure_index,uint32_t *next_close_ms,
                            int *rearm_after_restore,uint32_t *rearm_after_restore_at,
                            int *ap_observed,unsigned *ap_probe_failures,
                            uint32_t *ap_missing_since,int *ap_down_confirmed)
{
    wifi_policy_t snapshot;
    int observed=*ap_observed,policy_closed,eligible,station_ready,ota_busy;
    OSStatus err;
    lock_status();
    snapshot=policy;
    station_ready=phase==WIFI_STA_CONNECTED && status.link_cached && status.ip_valid_cached &&
                  status.consecutive_bad_samples==0;
    eligible=wifi_ap_close_eligible(snapshot.auto_connect,snapshot.disable_ap_after_connect,
             station_ready,snapshot.saved_ssid[0] && !strcmp(status.ssid,snapshot.saved_ssid),0);
    if (!eligible) status.recovery_ap_policy_closed=0;
    policy_closed=status.recovery_ap_policy_closed;
    unlock_status();
    ota_busy=recovery_ota_busy();
    if (ota_busy) eligible=0;
    if (!eligible) *eligible_since=0;
    if (wifi_recovery_ap_needs_restore(eligible,policy_closed,observed) &&
        wifi_ap_restore_confident(now,*ap_missing_since,*ap_probe_failures,*ap_down_confirmed) &&
        (now>=WIFI_AP_BOOT_SELF_HEAL_HOLD_MS || *ap_down_confirmed) &&
        (!ota_busy || !station_ready) &&
        (!*next_restore_ms || (int32_t)(now-*next_restore_ms)>=0)) {
        openm1_log_warn("WIFI","Recovery AP missing confirmed; restoring");
        wifi_manager_history_note(WIFI_HISTORY_AP_MISSING_CONFIRMED);
        lock_status(); status.recovery_ap_restoring=1; unlock_status();
        err=recovery_ap_start();
        mico_thread_msleep(500);
        observed=recovery_ap_probe();
        *ap_observed=observed;
        lock_status();
        status.recovery_ap_restoring=0;
        status.recovery_ap_last_error=err==kNoErr?(observed?0:-1):err;
        if (observed) {
            status.recovery_ap_restore_count++;
            status.recovery_ap_last_restore_ms=mico_rtos_get_time();
        }
        unlock_status();
        if (observed) {
            *restore_failure_index=0; *next_restore_ms=0;
            *ap_probe_failures=0; *ap_missing_since=0; *ap_down_confirmed=0;
            lock_status(); status.ap_probe_failures=0; unlock_status();
            if (*rearm_after_restore)
                *rearm_after_restore_at=mico_rtos_get_time()+2000u;
            openm1_log_info("WIFI","Recovery AP restored");
            wifi_manager_history_note(WIFI_HISTORY_AP_RESTORED);
        } else {
            uint32_t delay=wifi_ap_restore_backoff_ms((*restore_failure_index)++);
            *next_restore_ms=mico_rtos_get_time()+delay;
            openm1_log_error("WIFI","AP restore failed: %d; retry in %lu ms",err,(unsigned long)delay);
            wifi_manager_history_note(WIFI_HISTORY_AP_RESTORE_FAILED);
        }
    } else if (observed) {
        *restore_failure_index=0; *next_restore_ms=0;
    }
    if (eligible && observed) {
        if (!*eligible_since) {
            *eligible_since=now?now:1;
            printf("WIFI: STA ready; Recovery AP safety window active\r\n");
        } else if (wifi_ap_close_timing_ready(now,*eligible_since,*next_close_ms)) {
            /* Re-read policy and OTA before the destructive operation. */
            lock_status();
            snapshot=policy;
            station_ready=status.link_cached && status.ip_valid_cached &&
                          !strcmp(status.state,"connected") &&
                          !strcmp(status.ssid,snapshot.saved_ssid);
            unlock_status();
            if (snapshot.auto_connect && snapshot.disable_ap_after_connect &&
                station_ready && !recovery_ota_busy()) {
                LinkStatusTypeDef final_link;
                IPStatusTypedef final_ip;
                int final_good,final_error;
                station_snapshot(&final_link,&final_ip,&final_good,&final_error);
                if (!final_good) { *eligible_since=0; return; }
                wlan_operation("suspend_softap");
                openm1_log_info("WIFI","calling SuspendSoftAP");
                err=micoWlanSuspendSoftAP();
                openm1_log_info("WIFI","SuspendSoftAP result = %d",err);
                mico_thread_msleep(500);
                observed=recovery_ap_probe();
                *ap_observed=observed;
                if (err==kNoErr && !observed) {
                    lock_status(); status.recovery_ap_policy_closed=1; unlock_status();
                    *ap_probe_failures=0; *ap_missing_since=0; *ap_down_confirmed=0;
                    lock_status(); status.ap_probe_failures=0; unlock_status();
                    openm1_log_info("WIFI","Recovery AP closed after STA ready");
                    wifi_manager_history_note(WIFI_HISTORY_AP_CLOSED);
                } else {
                    *next_close_ms=mico_rtos_get_time()+WIFI_AP_CLOSE_RETRY_MS;
                    openm1_log_warn("WIFI","AP close unconfirmed: %d; retry 30s",err);
                }
            }
            *eligible_since=0;
        }
    }
}

static void wifi_control_worker(mico_thread_arg_t arg)
{
    wifi_station_desired_t desired;
    wifi_station_samples_t samples={0,0};
    LinkStatusTypeDef link;
    IPStatusTypedef ip;
    wifi_station_phase_t phase=WIFI_STA_IDLE;
    uint32_t seen_revision=0,arm_since=0,native_since=0,last_rearm=0;
    uint32_t rearm_wait_until=0,ap_eligible_since=0,next_ap_restore=0,next_ap_close=0;
    uint32_t rearm_after_restore_at=0;
    uint32_t next_ap_probe_ms=0,ap_missing_since=0,ap_down_confirm_at=0;
    int rearm_after_restore=0;
    unsigned ap_restore_failures=0,ap_probe_failures=0;
    int armed=0,good,last_error,disconnect,do_scan,boot_waiting;
    int ap_observed=0,ap_down_confirmed=0,station_started_once=0;
    int failed_start_needs_cleanup=0;
    uint32_t seen_connect_fail=0,seen_fatal=0;
    OSStatus err;
    (void)arg;
    lock_status();
    ap_observed=status.recovery_ap_observed_on;
    status.wifi_control_worker_running=1;
    unlock_status();
    next_ap_probe_ms=ap_observed?mico_rtos_get_time()+WIFI_AP_PROBE_INTERVAL_MS:0;
    openm1_log_info("WIFI","control worker starting");
    openm1_log_info("WIFI","native retry interval = %u ms",WIFI_NATIVE_RETRY_INTERVAL_MS);
    for (;;) {
        uint32_t now=mico_rtos_get_time();
        /* OTA owns the active transport. Do not probe, scan or mutate WLAN
         * while prepare/upload/verification/activation holds the reservation. */
        if (recovery_ota_busy()) { mico_thread_msleep(WIFI_CONTROL_INTERVAL_MS); continue; }
        uint32_t events=__atomic_exchange_n(&wifi_event_bits,0,__ATOMIC_ACQ_REL);
        int changed;
        if (events&WIFI_EVENT_AP_DOWN) {
            openm1_log_warn("WIFI","AP_DOWN event received");
            wifi_manager_history_note(WIFI_HISTORY_AP_DOWN);
        }
        if (events&WIFI_EVENT_AP_UP) {
            openm1_log_info("WIFI","AP_UP event received");
            wifi_manager_history_note(WIFI_HISTORY_AP_UP);
        }
        if (events&WIFI_EVENT_STATION_DOWN)
            wifi_manager_history_note(WIFI_HISTORY_STATION_DOWN_EVENT);
        if (events&WIFI_EVENT_STATION_UP)
            wifi_manager_history_note(WIFI_HISTORY_STATION_UP_EVENT);
        if (wifi_connect_fail_count!=seen_connect_fail) {
            seen_connect_fail=wifi_connect_fail_count;
            openm1_log_warn("WIFI","connect failed: %d (count=%lu)",
                            last_wifi_connect_fail_error,(unsigned long)seen_connect_fail);
            wifi_manager_history_note(WIFI_HISTORY_CONNECT_FAILED);
        }
        if (wifi_fatal_error_count!=seen_fatal) {
            seen_fatal=wifi_fatal_error_count;
            openm1_log_error("WIFI","fatal event (count=%lu)",(unsigned long)seen_fatal);
            wifi_manager_history_note(WIFI_HISTORY_WLAN_FATAL);
        }
        if ((events&WIFI_EVENT_AP_DOWN) &&
            (!(events&WIFI_EVENT_AP_UP) || last_wifi_event_code==NOTIFY_AP_DOWN))
            ap_down_confirm_at=now+1000u;
        if ((events&WIFI_EVENT_AP_UP) &&
            (!(events&WIFI_EVENT_AP_DOWN) || last_wifi_event_code==NOTIFY_AP_UP)) {
            ap_observed=1; ap_probe_failures=0; ap_missing_since=0;
            ap_down_confirm_at=0; ap_down_confirmed=0;
            lock_status(); status.recovery_ap_observed_on=1; status.ap_probe_failures=0; unlock_status();
        }
        lock_status();
        status.wifi_control_loop_count++;
        status.last_wifi_control_tick_ms=now;
        desired=desired_station;
        changed=desired_revision!=seen_revision;
        seen_revision=desired_revision;
        disconnect=disconnect_requested;
        disconnect_requested=0;
        do_scan=scan_requested;
        scan_requested=0;
        boot_waiting=status.boot_auto_connect_waiting;
        if (boot_waiting && (int32_t)(now-status.boot_auto_connect_not_before_ms)>=0) {
            status.boot_auto_connect_waiting=0;
            boot_waiting=0;
        }
        unlock_status();
        /* Commands are applied before any WLAN observation or AP policy operation. */
        if (disconnect) {
            if (armed || station_started_once) {
                err=station_suspend(); lock_status(); status.last_wlan_error=err; unlock_status();
            }
            armed=0; phase=WIFI_STA_IDLE; native_since=0; rearm_wait_until=0;
            failed_start_needs_cleanup=0;
            wifi_station_samples_reset(&samples);
            lock_status();
            status.station_armed=0; status.station_phase=phase;
            status.active=0; status.ssid[0]=0; status.message[0]=0;
            snprintf(status.state,sizeof(status.state),"disconnected");
            status.recovery_ap_policy_closed=0;
            clear_network();
            unlock_status();
            network_health_notify_link_down();
            display_station_fallback(0);
            wifi_manager_history_note(WIFI_HISTORY_MANUAL_DISCONNECT);
        } else if (changed && desired.want_connected && armed) {
            /* Explicit SSID/credential replacement, never ordinary link-loss retry. */
            printf("WIFI: explicit Station network switch\r\n");
            err=station_suspend();
            lock_status();
            status.last_wlan_error=err; status.recovery_ap_policy_closed=0;
            clear_network();
            unlock_status();
            mico_thread_msleep(500);
            armed=0; phase=WIFI_STA_IDLE; native_since=0;
            wifi_station_samples_reset(&samples);
            lock_status(); status.station_phase=phase; status.station_armed=0; unlock_status();
            network_health_notify_link_down();
            display_station_fallback(0);
            wifi_manager_history_note(WIFI_HISTORY_EXPLICIT_SWITCH);
        } else if (changed && !desired.want_connected && armed && phase!=WIFI_STA_CONNECTED) {
            /* AUTO was disabled while its initial association was still pending. */
            err=station_suspend();
            armed=0; phase=WIFI_STA_IDLE; native_since=0;
            wifi_station_samples_reset(&samples);
            lock_status();
            status.station_armed=0; status.last_wlan_error=err;
            status.active=0; status.ssid[0]=0;
            snprintf(status.state,sizeof(status.state),"disconnected");
            status.recovery_ap_policy_closed=0;
            clear_network();
            unlock_status();
            network_health_notify_link_down();
            display_station_fallback(0);
        }
        if (do_scan) {
            if (!armed && !desired.want_connected && !recovery_ota_busy()) {
                wlan_operation("start_scan");
                printf("WIFI: calling StartScanAdv\r\n");
                micoWlanStartScanAdv();
                printf("WIFI: StartScanAdv requested\r\n");
            } else {
                lock_status(); strcpy(scan_state,"failed"); unlock_status();
            }
        }
        if (armed) {
            station_snapshot(&link,&ip,&good,&last_error);
            lock_status(); status.last_wlan_error=last_error; unlock_status();
            if (phase==WIFI_STA_CONNECTED) {
                if (good) {
                    int recovered_sample=samples.bad_samples!=0;
                    wifi_station_note_good(&samples);
                    lock_status(); station_cache_good(&link,&ip);
                    status.consecutive_bad_samples=0; status.consecutive_good_samples=samples.good_samples; unlock_status();
                    if (recovered_sample) wifi_manager_history_note(WIFI_HISTORY_STATION_SAMPLE_RECOVERED);
                } else if (wifi_station_note_bad(&samples)) {
                    phase=desired.want_connected?WIFI_STA_NATIVE_RECONNECT_WAIT:WIFI_STA_IDLE;
                    native_since=desired.want_connected?now:0;
                    lock_status();
                    if (status.recovery_ap_policy_closed && desired.want_connected)
                        rearm_after_restore=1;
                    status.disconnect_count++; status.last_disconnect_ms=now;
                    status.station_phase=phase;
                    status.consecutive_bad_samples=samples.bad_samples;
                    status.consecutive_good_samples=0; status.active=0;
                    status.native_reconnect_since_ms=native_since;
                    status.recovery_ap_policy_closed=0;
                    snprintf(status.state,sizeof(status.state),"disconnected");
                    snprintf(status.message,sizeof(status.message),"家庭 Wi-Fi 链路已丢失，等待 MiCO 自动重连。");
                    clear_network(); unlock_status();
                    openm1_log_warn("WIFI","Station link lost; native reconnect waiting");
                    network_health_notify_link_down();
                    display_station_fallback(0);
                    wifi_manager_history_note(WIFI_HISTORY_STATION_LOST);
                    if (!desired.want_connected) {
                        /* Saved AUTO credentials were cleared during this connection. */
                        err=station_suspend();
                        armed=0;
                        lock_status(); status.station_armed=0; status.last_wlan_error=err; unlock_status();
                    }
                } else {
                    lock_status(); status.consecutive_bad_samples=samples.bad_samples; unlock_status();
                    wifi_manager_history_note(WIFI_HISTORY_STATION_BAD);
                }
            } else {
                if (good && wifi_station_note_good(&samples)) {
                    int native_recovered=phase==WIFI_STA_NATIVE_RECONNECT_WAIT ||
                                         phase==WIFI_STA_REARM_WAIT;
                    phase=WIFI_STA_CONNECTED; native_since=0; rearm_wait_until=0;
                    lock_status(); station_cache_good(&link,&ip);
                    status.station_phase=phase;
                    snprintf(status.state,sizeof(status.state),"connected");
                    status.active=0; status.message[0]=0;
                    status.link_up_since_ms=now; status.last_connect_ms=now;
                    status.native_reconnect_since_ms=0;
                    status.consecutive_good_samples=samples.good_samples;
                    status.consecutive_bad_samples=0;
                    if (native_recovered) {
                        status.native_reconnect_success_count++;
                        status.last_native_reconnect_success_ms=now;
                    }
                    unlock_status();
                    openm1_log_info("WIFI","Station connected IP=%s RSSI=%d",ip.ip,link.rssi);
                    if (native_recovered) openm1_log_info("WIFI","Station recovered by native reconnect");
                    network_health_notify_link_ready();
                    display_station_fallback(1);
                    wifi_manager_history_note(WIFI_HISTORY_STATION_CONNECTED);
                } else if (!good) {
                    wifi_station_note_bad(&samples);
                    lock_status(); status.consecutive_good_samples=0;
                    status.consecutive_bad_samples=samples.bad_samples; unlock_status();
                } else {
                    lock_status(); status.consecutive_good_samples=samples.good_samples; unlock_status();
                }
                if (phase==WIFI_STA_ARMED_CONNECTING && !native_since &&
                    (uint32_t)(now-arm_since)>=WIFI_NATIVE_RECONNECT_GRACE_MS) {
                    phase=WIFI_STA_NATIVE_RECONNECT_WAIT; native_since=arm_since;
                    lock_status(); status.native_reconnect_since_ms=native_since; unlock_status();
                }
            }
        }
        /* A normal AP probe is at most once per five seconds. AP_DOWN gets one
         * exceptional confirmation probe after a one-second settling delay. */
        if ((ap_down_confirm_at && (int32_t)(now-ap_down_confirm_at)>=0) ||
            (!ap_down_confirm_at && (int32_t)(now-next_ap_probe_ms)>=0)) {
            int confirmed_down=ap_down_confirm_at!=0;
            ap_observed=recovery_ap_probe();
            next_ap_probe_ms=mico_rtos_get_time()+WIFI_AP_PROBE_INTERVAL_MS;
            ap_down_confirm_at=0;
            if (ap_observed) {
                ap_probe_failures=0; ap_missing_since=0; ap_down_confirmed=0;
            } else {
                if (!ap_missing_since) ap_missing_since=now?now:1;
                ap_probe_failures=wifi_ap_missing_after_probe(ap_probe_failures,0);
                if (confirmed_down) ap_down_confirmed=1;
            }
            lock_status(); status.ap_probe_failures=ap_probe_failures; unlock_status();
        }
        /* AP is restored before an explicit Station arm/re-arm. */
        ap_control_step(now,phase,&ap_eligible_since,&next_ap_restore,&ap_restore_failures,
                        &next_ap_close,&rearm_after_restore,&rearm_after_restore_at,
                        &ap_observed,&ap_probe_failures,&ap_missing_since,&ap_down_confirmed);
        if (rearm_after_restore && rearm_after_restore_at &&
            (int32_t)(mico_rtos_get_time()-rearm_after_restore_at)>=0) {
            /* Only after a policy-closed AP was restored: re-arm STA once, without suspend. */
            rearm_after_restore=0; rearm_after_restore_at=0;
            if (armed && desired.want_connected && !good && !recovery_ota_busy() &&
                (int32_t)(now-WIFI_BOOT_AUTO_CONNECT_GRACE_MS)>=0) {
                openm1_log_warn("WIFI","one-time Station arm after Recovery AP restore");
                err=station_start(&desired);
                arm_since=mico_rtos_get_time(); native_since=arm_since;
                lock_status(); status.last_wlan_error=err; unlock_status();
            }
        }
        if (desired.want_connected && !disconnect && !scan_is_active() && !recovery_ota_busy() &&
            (int32_t)(now-WIFI_BOOT_AUTO_CONNECT_GRACE_MS)>=0) {
            if (!armed && !boot_waiting && (!rearm_wait_until || (int32_t)(now-rearm_wait_until)>=0)) {
                if (failed_start_needs_cleanup) {
                    printf("WIFI: controlled cleanup after failed Station start\r\n");
                    station_suspend();
                    mico_thread_msleep(500);
                    failed_start_needs_cleanup=0;
                    lock_status();
                    status.station_rearm_count++;
                    status.last_station_rearm_ms=mico_rtos_get_time();
                    unlock_status();
                }
                if (!station_started_once) openm1_log_info("WIFI","first Station arm");
                else printf("WIFI: Station arm\r\n");
                err=station_start(&desired);
                armed=err==kNoErr;
                arm_since=mico_rtos_get_time();
                wifi_station_samples_reset(&samples);
                phase=armed?WIFI_STA_ARMED_CONNECTING:WIFI_STA_REARM_WAIT;
                lock_status();
                status.station_started_once=1; status.station_armed=armed; status.station_phase=phase;
                status.last_wlan_error=err; status.active=1;
                snprintf(status.ssid,sizeof(status.ssid),"%s",desired.ssid);
                snprintf(status.state,sizeof(status.state),err==kNoErr?"connecting":"failed");
                if (err!=kNoErr) snprintf(status.message,sizeof(status.message),"Station 启动失败，稍后重试。");
                unlock_status();
                if (!station_started_once) wifi_manager_history_note(WIFI_HISTORY_FIRST_ARM);
                station_started_once=1;
                if (err!=kNoErr) {
                    openm1_log_error("WIFI","STA start failed: %d",err);
                    failed_start_needs_cleanup=1;
                    rearm_wait_until=arm_since+(last_rearm?WIFI_CONTROLLED_REARM_MIN_INTERVAL_MS:5000u);
                    last_rearm=arm_since;
                }
            } else if (armed && (phase==WIFI_STA_NATIVE_RECONNECT_WAIT || phase==WIFI_STA_REARM_WAIT) &&
                       wifi_station_rearm_due(now,native_since?native_since:arm_since,last_rearm)) {
                openm1_log_warn("WIFI","native reconnect timeout; controlled re-arm");
                err=station_suspend();
                lock_status(); status.last_wlan_error=err; status.recovery_ap_policy_closed=0; unlock_status();
                mico_thread_msleep(500);
                err=station_start(&desired);
                last_rearm=mico_rtos_get_time(); arm_since=last_rearm;
                armed=err==kNoErr; native_since=0;
                rearm_wait_until=err==kNoErr?0:last_rearm+WIFI_NATIVE_RECONNECT_GRACE_MS;
                phase=armed?WIFI_STA_ARMED_CONNECTING:WIFI_STA_REARM_WAIT;
                wifi_station_samples_reset(&samples);
                lock_status();
                status.station_armed=armed; status.station_phase=phase; status.station_rearm_count++;
                status.last_station_rearm_ms=last_rearm; status.last_wlan_error=err;
                status.native_reconnect_since_ms=0;
                unlock_status();
                wifi_manager_history_note(WIFI_HISTORY_STATION_REARM);
            }
        }
        lock_status(); status.station_phase=phase; status.station_armed=armed; unlock_status();
        memset(desired.password,0,sizeof(desired.password));
        mico_thread_msleep(WIFI_CONTROL_INTERVAL_MS);
    }
}

OSStatus wifi_manager_init(void)
{
    OSStatus err=mico_rtos_init_mutex(&status_mutex);
    if (err!=kNoErr) return err;
    status.last_link_query_result=-32768;
    status.last_ip_query_result=-32768;
    status.last_query_link_connected=-1;
    status.last_query_ip_valid=-1;
    manager_ready=1;
    err=mico_system_notify_register(mico_notify_WIFI_SCAN_ADV_COMPLETED,(void *)scan_complete,NULL);
    scan_registered=err==kNoErr;
    if (!scan_registered) printf("WIFI: scan callback registration failed = %d\r\n",err);
    err=mico_system_notify_register(mico_notify_WIFI_CONNECT_FAILED,(void *)wifi_connect_failed_notice,NULL);
    if (err!=kNoErr) printf("WIFI: connect-fail notification registration failed = %d\r\n",err);
    err=mico_system_notify_register(mico_notify_WIFI_Fatal_ERROR,(void *)wifi_fatal_notice,NULL);
    if (err!=kNoErr) printf("WIFI: fatal notification registration failed = %d\r\n",err);
    err=mico_system_notify_register(mico_notify_WIFI_STATUS_CHANGED,(void *)wifi_status_notice,NULL);
    if (err!=kNoErr) printf("WIFI: status notification registration failed = %d\r\n",err);
    openm1_log_info("WIFI","manager initialized");
    return kNoErr;
}
void wifi_manager_set_initial_ap_state(int started)
{
    if (!manager_ready) return;
    lock_status();
    status.recovery_ap_observed_on=started!=0;
    status.recovery_ap_last_error=started?0:-1;
    status.ap_probe_failures=0;
    unlock_status();
}
void wifi_manager_note_recovery_activity(void)
{
    uint32_t until,now;
    if (!manager_ready) return;
    now=mico_rtos_get_time();
    lock_status();
    if (desired_station.source==WIFI_DESIRED_AUTO &&
        desired_station.want_connected && !status.station_started_once) {
        until=now+WIFI_RECOVERY_WEB_ACTIVITY_HOLD_MS;
        if ((int32_t)(until-status.boot_auto_connect_not_before_ms)>0)
            status.boot_auto_connect_not_before_ms=until;
        status.boot_auto_connect_waiting=1;
    }
    unlock_status();
}
OSStatus wifi_manager_apply_boot_settings(void)
{
    openm1_config_t config;
    OSStatus err;
    if (!manager_ready) return kNotPreparedErr;
    config_store_get(&config);
    lock_status();
    snprintf(policy.saved_ssid,sizeof(policy.saved_ssid),"%s",config.wifi_ssid);
    policy.auto_connect=config.wifi_auto_connect;
    policy.disable_ap_after_connect=config.ap_disable_after_sta_connected;
    if (config.wifi_auto_connect && config.wifi_ssid[0]) {
        wifi_station_desire(&desired_station,config.wifi_ssid,config.wifi_password,WIFI_DESIRED_AUTO);
        status.boot_auto_connect_waiting=1;
        status.boot_auto_connect_not_before_ms=WIFI_BOOT_AUTO_CONNECT_GRACE_MS;
        desired_revision++;
    }
    unlock_status();
    if (config.wifi_auto_connect && config.wifi_ssid[0]) {
        openm1_log_info("WIFI","auto-connect desired SSID=%s",config.wifi_ssid);
        openm1_log_info("WIFI","rescue window %u ms",WIFI_BOOT_AUTO_CONNECT_GRACE_MS);
    }
    memset(config.wifi_password,0,sizeof(config.wifi_password));
    err=mico_rtos_create_thread(&wifi_control_thread,MICO_APPLICATION_PRIORITY,
                                "openm1_wifi_control",wifi_control_worker,
                                WIFI_CONTROL_WORKER_STACK,0);
    control_worker_created=err==kNoErr;
    if (err!=kNoErr) {
        micoMemInfo_t *memory=MicoGetMemoryInfo();
        openm1_log_error("WIFI","control worker start failed: %d, heap=%d",
                         err,memory?memory->free_memory:-1);
    }
    return err;
}
void wifi_manager_settings_json(char *out,size_t capacity)
{
    openm1_config_t config;
    config_store_get(&config);
    wifi_settings_json(&config,out,capacity);
    memset(config.wifi_password,0,sizeof(config.wifi_password));
}
int wifi_manager_save_settings(const char *body,size_t length)
{
    openm1_config_t config;
    OSStatus err;
    int result;
    if (recovery_ota_busy()) return -3;
    config_store_get(&config);
    result=wifi_settings_apply_json(body,length,&config);
    if (result) { memset(config.wifi_password,0,sizeof(config.wifi_password)); return result; }
    if (!control_worker_created && config.ap_disable_after_sta_connected) {
        memset(config.wifi_password,0,sizeof(config.wifi_password)); return -4;
    }
    err=config_store_save(&config);
    if (err==kNoErr) {
        lock_status();
        snprintf(policy.saved_ssid,sizeof(policy.saved_ssid),"%s",config.wifi_ssid);
        policy.auto_connect=config.wifi_auto_connect;
        policy.disable_ap_after_connect=config.ap_disable_after_sta_connected;
        if (desired_station.source==WIFI_DESIRED_AUTO) {
            if (!config.wifi_auto_connect || !config.wifi_ssid[0]) {
                wifi_station_drop_auto_desired(&desired_station);
                desired_revision++;
            } else if (strcmp(desired_station.ssid,config.wifi_ssid) ||
                       strcmp(desired_station.password,config.wifi_password)) {
                wifi_station_desire(&desired_station,config.wifi_ssid,config.wifi_password,WIFI_DESIRED_AUTO);
                desired_revision++;
            }
        }
        unlock_status();
    }
    memset(config.wifi_password,0,sizeof(config.wifi_password));
    return err==kNoErr?0:-4;
}
int wifi_manager_connect(const char *ssid,const char *password)
{
    size_t pass_len;
    if (recovery_ota_busy()) return -2;
    if (!manager_ready || !control_worker_created || !WIFI_STA_CONNECT_SUPPORTED) return -3;
    if (!valid_input(ssid,sizeof(desired_station.ssid)-1) || !password) return -1;
    pass_len=strlen(password);
    if (pass_len>=sizeof(desired_station.password)) return -1;
    if (pass_len && !valid_input(password,sizeof(desired_station.password)-1)) return -1;
    lock_status();
    if (scan_requested || !strcmp(scan_state,"scanning")) { unlock_status(); return -2; }
    wifi_station_desire(&desired_station,ssid,password,WIFI_DESIRED_MANUAL);
    desired_revision++; disconnect_requested=0; status.boot_auto_connect_waiting=0;
    status.active=1;
    snprintf(status.state,sizeof(status.state),"connecting");
    snprintf(status.ssid,sizeof(status.ssid),"%s",ssid);
    snprintf(status.message,sizeof(status.message),"正在连接……");
    unlock_status();
    openm1_log_info("WIFI","STA connect requested SSID=%s",ssid);
    return 0;
}
int wifi_manager_disconnect(void)
{
    if (recovery_ota_busy()) return -2;
    if (!manager_ready || !control_worker_created) return -1;
    lock_status();
    wifi_station_manual_disconnect(&desired_station);
    desired_revision++; disconnect_requested=1;
    status.boot_auto_connect_waiting=0;
    unlock_status();
    openm1_log_info("WIFI","manual disconnect; reconnect paused");
    return 0;
}
static void json_string(char *out,size_t capacity,const char *input)
{
    size_t n=0; unsigned char c;
    if (!capacity) return;
    while ((c=(unsigned char)*input++) && n+2<capacity) {
        if (c=='"' || c=='\\') { if (n+3>=capacity) break; out[n++]='\\'; out[n++]=c; }
        else if (c>=32) out[n++]=c;
    }
    out[n]=0;
}
static const char *station_phase_name(wifi_station_phase_t phase)
{
    switch (phase) {
    case WIFI_STA_IDLE: return "idle";
    case WIFI_STA_ARMED_CONNECTING: return "connecting";
    case WIFI_STA_CONNECTED: return "connected";
    case WIFI_STA_NATIVE_RECONNECT_WAIT: return "native_reconnect_wait";
    case WIFI_STA_REARM_WAIT: return "rearm_wait";
    default: return "unknown";
    }
}
static const char *history_reason_name(uint8_t reason)
{
    static const char *const names[]={
        "station_bad_sample","station_sample_recovered","station_lost",
        "station_connected","controlled_rearm","ap_down_event","ap_up_event",
        "ap_restored","ap_closed","network_health_changed","display_target_changed",
        "pwm_applied_changed","wlan_fatal","connect_failed",
        "manual_disconnect","first_station_arm","explicit_network_switch",
        "station_up_event","station_down_event","ap_missing_confirmed",
        "ap_restore_failed"
    };
    return reason<sizeof(names)/sizeof(names[0])?names[reason]:"unknown";
}
static const char *history_health_name(uint8_t state)
{
    return state==NETWORK_CHECKING?"checking":state==NETWORK_ONLINE?"online":
           state==NETWORK_NO_INTERNET?"no_internet":"no_wifi";
}
static const char *history_display_name(uint8_t target)
{
    return target==M1_NET_DISPLAY_ONLINE?"solid":
           target==M1_NET_DISPLAY_NO_INTERNET?"solid_red_x":
           target==M1_NET_DISPLAY_DISCONNECTED?"blink":"uninitialized";
}
void wifi_manager_history_note(wifi_history_reason_t reason)
{
    wifi_history_record_t *entry;
    m1_display_status_t display;
    network_health_state_t health;
    if (!manager_ready) return;
    /* Snapshot other modules before status_mutex: these locks never nest. */
    health=network_health_current_state();
    m1_display_get_status(&display);
    lock_status();
    entry=&wifi_history[history_head];
    entry->sequence=history_next_sequence++;
    entry->uptime_ms=mico_rtos_get_time();
    entry->disconnect_count=status.disconnect_count;
    entry->native_reconnect_count=status.native_reconnect_success_count;
    entry->station_rearm_count=status.station_rearm_count;
    entry->ap_restore_count=status.recovery_ap_restore_count;
    entry->link_query_result=status.last_link_query_result;
    entry->ip_query_result=status.last_ip_query_result;
    entry->wlan_error=(reason==WIFI_HISTORY_AP_DOWN ||
                       reason==WIFI_HISTORY_AP_MISSING_CONFIRMED ||
                       reason==WIFI_HISTORY_AP_RESTORE_FAILED)?
                      status.recovery_ap_last_error:status.last_wlan_error;
    entry->rssi=(int16_t)status.last_query_rssi;
    entry->event_code=(int16_t)(reason==WIFI_HISTORY_AP_DOWN?NOTIFY_AP_DOWN:
        reason==WIFI_HISTORY_AP_UP?NOTIFY_AP_UP:
        reason==WIFI_HISTORY_STATION_DOWN_EVENT?NOTIFY_STATION_DOWN:
        reason==WIFI_HISTORY_STATION_UP_EVENT?NOTIFY_STATION_UP:last_wifi_event_code);
    entry->reason=(uint8_t)reason;
    entry->phase=(uint8_t)status.station_phase;
    entry->link_connected=(uint8_t)(status.last_query_link_connected+1);
    entry->ip_valid=(uint8_t)(status.last_query_ip_valid+1);
    entry->health_state=(uint8_t)health;
    entry->display_target=(uint8_t)display.network_target;
    entry->pwm_applied=(uint8_t)display.network_applied_target;
    entry->ap_observed=(uint8_t)status.recovery_ap_observed_on;
    history_head=(history_head+1u)%WIFI_HISTORY_CAPACITY;
    if (history_count<WIFI_HISTORY_CAPACITY) history_count++;
    unlock_status();
}
void wifi_manager_history_snapshot(uint32_t *oldest,uint32_t *newest,unsigned *count)
{
    lock_status();
    if (count) *count=history_count;
    if (oldest) *oldest=history_count?history_next_sequence-history_count:0;
    if (newest) *newest=history_count?history_next_sequence-1u:0;
    unlock_status();
}
int wifi_manager_history_record_json(uint32_t sequence,char *out,size_t capacity)
{
    wifi_history_record_t entry;
    unsigned i;
    int found=0,n;
    if (!out || !capacity) return -1;
    lock_status();
    for (i=0;i<history_count;i++) {
        unsigned slot=(history_head+WIFI_HISTORY_CAPACITY-history_count+i)%WIFI_HISTORY_CAPACITY;
        if (wifi_history[slot].sequence==sequence) { entry=wifi_history[slot]; found=1; break; }
    }
    unlock_status();
    if (!found) return 0;
    n=snprintf(out,capacity,
      "{\"seq\":%lu,\"uptime_ms\":%lu,\"reason\":\"%s\",\"station_phase\":\"%s\","
      "\"link_query_result\":%ld,\"ip_query_result\":%ld,\"link_connected\":%d,"
      "\"ip_valid\":%d,\"rssi\":%d,\"wlan_error\":%ld,\"wifi_event_code\":%d,"
      "\"disconnect_count\":%lu,\"native_reconnect_count\":%lu,\"station_rearm_count\":%lu,"
      "\"ap_restore_count\":%lu,\"ap_observed\":%s,\"network_health\":\"%s\","
      "\"display_target\":\"%s\",\"pwm_applied\":\"%s\"}",
      (unsigned long)entry.sequence,(unsigned long)entry.uptime_ms,
      history_reason_name(entry.reason),station_phase_name((wifi_station_phase_t)entry.phase),
      (long)entry.link_query_result,(long)entry.ip_query_result,(int)entry.link_connected-1,
      (int)entry.ip_valid-1,(int)entry.rssi,(long)entry.wlan_error,(int)entry.event_code,
      (unsigned long)entry.disconnect_count,(unsigned long)entry.native_reconnect_count,
      (unsigned long)entry.station_rearm_count,(unsigned long)entry.ap_restore_count,
      entry.ap_observed?"true":"false",history_health_name(entry.health_state),
      history_display_name(entry.display_target),history_display_name(entry.pwm_applied));
    return n>=0 && (size_t)n<capacity?n:-1;
}
void wifi_manager_status_json(char *out,size_t out_size)
{
    wifi_status_t snapshot;
    wifi_policy_t saved;
    wifi_desired_source_t source;
    int want,latch,scan_supported;
    char ssid[66],saved_ssid[66],message[194];
    const char *ap_state;
    uint32_t link_uptime=0;
    uint32_t now=mico_rtos_get_time();
    uint32_t rescue_remaining=(int32_t)(now-WIFI_BOOT_AUTO_CONNECT_GRACE_MS)<0?
        WIFI_BOOT_AUTO_CONNECT_GRACE_MS-now:0;
    lock_status();
    snapshot=status; saved=policy;
    source=desired_station.source;
    want=desired_station.want_connected;
    latch=desired_station.manual_disconnect_latched;
    scan_supported=scan_registered;
    unlock_status();
    if (source==WIFI_DESIRED_AUTO && !snapshot.station_started_once &&
        (int32_t)(snapshot.boot_auto_connect_not_before_ms-now)>0 &&
        snapshot.boot_auto_connect_not_before_ms-now>rescue_remaining)
        rescue_remaining=snapshot.boot_auto_connect_not_before_ms-now;
    ap_state=snapshot.recovery_ap_restoring?"restoring":
        snapshot.recovery_ap_observed_on?"on":
        snapshot.recovery_ap_policy_closed?"policy_closed":"off";
    if (snapshot.link_cached && snapshot.ip_valid_cached && snapshot.link_up_since_ms)
        link_uptime=mico_rtos_get_time()-snapshot.link_up_since_ms;
    json_string(ssid,sizeof(ssid),snapshot.ssid);
    json_string(saved_ssid,sizeof(saved_ssid),saved.saved_ssid);
    json_string(message,sizeof(message),snapshot.message);
    snprintf(out,out_size,
      "{\"recovery_ap\":%s,\"recovery_ap_state\":\"%s\",\"recovery_ssid\":\"%s\",\"mac\":\"%s\",\"dhcp_hostname\":\"%s\",\"sta_state\":\"%s\",\"ssid\":\"%s\",\"ip\":\"%s\",\"gateway\":\"%s\",\"netmask\":\"%s\",\"dns\":\"%s\",\"rssi\":%d,\"message\":\"%s\",\"sta_connect_supported\":true,\"scan_supported\":%s,\"auto_connect\":%s,\"ap_disable_after_connect\":%s,\"saved_ssid\":\"%s\","
      "\"wifi_control_worker_running\":%s,\"wifi_control_loop_count\":%lu,\"last_wifi_control_tick_ms\":%lu,\"want_connected\":%s,\"desired_source\":\"%s\",\"manual_disconnect_latched\":%s,\"link_cached\":%s,\"ip_valid_cached\":%s,\"link_uptime_ms\":%lu,\"disconnect_count\":%lu,\"last_disconnect_ms\":%lu,\"last_connect_ms\":%lu,\"consecutive_bad_samples\":%lu,\"consecutive_good_samples\":%lu,"
      "\"station_phase\":\"%s\",\"station_armed\":%s,\"station_started_once\":%s,\"native_retry_interval_ms\":%u,\"native_reconnect_waiting\":%s,\"native_reconnect_since_ms\":%lu,\"native_reconnect_successes\":%lu,\"last_native_reconnect_success_ms\":%lu,\"station_rearm_count\":%lu,\"last_station_rearm_ms\":%lu,\"last_wlan_operation\":\"%s\",\"last_wlan_error\":%d,\"boot_auto_connect_grace_ms\":%u,\"boot_auto_connect_waiting\":%s,"
      "\"boot_auto_connect_remaining_ms\":%lu,\"ap_probe_failures\":%lu,\"ap_probe_interval_ms\":%u,\"ap_missing_threshold\":%u,\"recovery_ap_observed\":%s,\"recovery_ap_policy_closed\":%s,\"recovery_ap_restore_count\":%lu,\"recovery_ap_last_probe_ms\":%lu,\"recovery_ap_last_restore_ms\":%lu,\"recovery_ap_last_error\":%d,\"ap_boot_failsafe_ms\":%u,\"ap_stable_before_close_ms\":%u,\"wifi_connect_fail_count\":%lu,\"last_wifi_connect_fail_error\":%d,\"wifi_fatal_error_count\":%lu,\"last_wifi_event\":\"%s\",\"last_wifi_event_code\":%d}",
      snapshot.recovery_ap_observed_on?"true":"false",ap_state,recovery_ssid(),recovery_mac(),recovery_hostname(),snapshot.state,ssid,
      snapshot.ip,snapshot.gateway,snapshot.netmask,snapshot.dns,snapshot.rssi,message,
      scan_supported?"true":"false",saved.auto_connect?"true":"false",
      saved.disable_ap_after_connect?"true":"false",saved_ssid,
      snapshot.wifi_control_worker_running?"true":"false",(unsigned long)snapshot.wifi_control_loop_count,
      (unsigned long)snapshot.last_wifi_control_tick_ms,want?"true":"false",
      source==WIFI_DESIRED_MANUAL?"manual":source==WIFI_DESIRED_AUTO?"auto":"none",latch?"true":"false",
      snapshot.link_cached?"true":"false",snapshot.ip_valid_cached?"true":"false",(unsigned long)link_uptime,
      (unsigned long)snapshot.disconnect_count,(unsigned long)snapshot.last_disconnect_ms,
      (unsigned long)snapshot.last_connect_ms,(unsigned long)snapshot.consecutive_bad_samples,
      (unsigned long)snapshot.consecutive_good_samples,station_phase_name(snapshot.station_phase),
      snapshot.station_armed?"true":"false",snapshot.station_started_once?"true":"false",
      WIFI_NATIVE_RETRY_INTERVAL_MS,snapshot.station_phase==WIFI_STA_NATIVE_RECONNECT_WAIT?"true":"false",
      (unsigned long)snapshot.native_reconnect_since_ms,(unsigned long)snapshot.native_reconnect_success_count,
      (unsigned long)snapshot.last_native_reconnect_success_ms,(unsigned long)snapshot.station_rearm_count,
      (unsigned long)snapshot.last_station_rearm_ms,snapshot.last_wlan_operation,snapshot.last_wlan_error,
      WIFI_BOOT_AUTO_CONNECT_GRACE_MS,snapshot.boot_auto_connect_waiting?"true":"false",
      (unsigned long)rescue_remaining,(unsigned long)snapshot.ap_probe_failures,
      WIFI_AP_PROBE_INTERVAL_MS,WIFI_AP_MISSING_THRESHOLD,
      snapshot.recovery_ap_observed_on?"true":"false",snapshot.recovery_ap_policy_closed?"true":"false",
      (unsigned long)snapshot.recovery_ap_restore_count,(unsigned long)snapshot.recovery_ap_last_probe_ms,
      (unsigned long)snapshot.recovery_ap_last_restore_ms,snapshot.recovery_ap_last_error,
      WIFI_AP_BOOT_FAILSAFE_MS,WIFI_AP_STABLE_BEFORE_CLOSE_MS,
      (unsigned long)wifi_connect_fail_count,last_wifi_connect_fail_error,
      (unsigned long)wifi_fatal_error_count,
      last_wifi_event_code==NOTIFY_STATION_UP?"station_up":
      last_wifi_event_code==NOTIFY_STATION_DOWN?"station_down":
      last_wifi_event_code==NOTIFY_AP_UP?"ap_up":
      last_wifi_event_code==NOTIFY_AP_DOWN?"ap_down":"none",
      last_wifi_event_code);
}
int wifi_manager_start_scan(void)
{
    if (!manager_ready || !control_worker_created || !scan_registered) return -2;
    if (recovery_ota_busy()) return -3;
    lock_status();
    if (scan_requested || !strcmp(scan_state,"scanning") || status.station_armed ||
        desired_station.want_connected || status.station_phase!=WIFI_STA_IDLE) {
        unlock_status(); return -1;
    }
    strcpy(scan_state,"scanning"); scan_count=0;
    scan_started_ms=mico_rtos_get_time(); scan_requested=1;
    unlock_status();
    return 0;
}

static const char *security_name(wlan_sec_type_t security)
{
    switch (security) {
    case SECURITY_TYPE_NONE: return "OPEN";
    case SECURITY_TYPE_WEP: return "WEP";
    case SECURITY_TYPE_WPA_TKIP:
    case SECURITY_TYPE_WPA_AES: return "WPA";
    case SECURITY_TYPE_WPA2_TKIP:
    case SECURITY_TYPE_WPA2_AES:
    case SECURITY_TYPE_WPA2_MIXED: return "WPA2";
    default: return "UNKNOWN";
    }
}
int wifi_manager_scan_snapshot(char state_out[12],unsigned *count_out)
{
    if (!state_out || !count_out) return 0;
    if (!scan_registered) { strcpy(state_out,"failed"); *count_out=0; return 0; }
    lock_status();
    if (!strcmp(scan_state,"scanning") && mico_rtos_get_time()-scan_started_ms>15000)
        strcpy(scan_state,"failed");
    strcpy(state_out,scan_state);
    *count_out=scan_count;
    unlock_status();
    return 1;
}
int wifi_manager_scan_record_json(unsigned index,char *out,size_t capacity)
{
    scan_ap_t ap;
    char escaped[70];
    int n;
    if (!out || !capacity) return -1;
    lock_status();
    if (index>=scan_count) { unlock_status(); return 0; }
    ap=scan_aps[index];
    unlock_status();
    json_string(escaped,sizeof(escaped),ap.ssid);
    n=snprintf(out,capacity,"{\"ssid\":\"%s\",\"rssi\":%d,\"security\":\"%s\",\"channel\":%u}",
               escaped,ap.rssi,security_name(ap.security),ap.channel);
    return n>=0 && (size_t)n<capacity?n:-1;
}
int wifi_manager_station_ready(void)
{
    int ready;
    lock_status();
    ready=status.link_cached && status.ip_valid_cached && !strcmp(status.state,"connected");
    unlock_status();
    return ready;
}
int wifi_manager_control_running(void)
{
    int running;
    lock_status(); running=status.wifi_control_worker_running; unlock_status();
    return running;
}
int wifi_manager_station_link(void)
{
    int linked;
    lock_status(); linked=status.link_cached; unlock_status();
    return linked;
}
int wifi_manager_station_rssi(void)
{
    int rssi;
    lock_status(); rssi=status.link_cached?status.rssi:0; unlock_status();
    return rssi;
}
void wifi_manager_station_ip(char out[16])
{
    if (!out) return;
    lock_status();
    if (status.ip_valid_cached) copy_ip(out,status.ip);
    else out[0]=0;
    unlock_status();
}
