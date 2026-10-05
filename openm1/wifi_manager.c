#include "wifi_manager.h"
#include "recovery.h"
#include "network_health.h"
#include "config_store.h"
#include "wifi_settings.h"
#include "wifi_station_logic.h"
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
    uint32_t wifi_control_loop_count,last_wifi_control_tick_ms;
    int recovery_ap_observed_on,recovery_ap_policy_closed,recovery_ap_restoring;
    uint32_t recovery_ap_restore_count,recovery_ap_last_probe_ms,recovery_ap_last_restore_ms;
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
#define WIFI_AP_CLOSE_GRACE_MS 3000u
#define WIFI_SCAN_TIMEOUT_MS 15000u

typedef struct { char ssid[33]; int rssi; unsigned channel; wlan_sec_type_t security; } scan_ap_t;
static scan_ap_t scan_aps[WIFI_SCAN_MAX_AP];
static unsigned scan_count;
static char scan_state[12]="idle";
static uint32_t scan_started_ms;
static char scan_json[3300];
static int scan_registered;
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
    printf("WIFI: calling SuspendStation\r\n");
    err=micoWlanSuspendStation();
    printf("WIFI: SuspendStation result = %d\r\n",err);
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
    printf("WIFI: calling StartNetwork(STA)\r\n");
    err=StartNetwork(&config);
    printf("WIFI: StartNetwork(STA) result = %d\r\n",err);
    memset(config.wifi_key,0,sizeof(config.wifi_key));
    return err;
}
static void station_snapshot(LinkStatusTypeDef *link,IPStatusTypedef *ip,int *good,int *last_error)
{
    OSStatus err;
    memset(link,0,sizeof(*link)); memset(ip,0,sizeof(*ip));
    wlan_operation("get_station_status");
    err=micoWlanGetLinkStatus(link);
    if (err!=kNoErr) { *good=0; *last_error=err; return; }
    if (link->is_connected!=1) { *good=0; *last_error=0; return; }
    err=micoWlanGetIPStatus(ip,Station);
    ip->ip[sizeof(ip->ip)-1]=0;
    *good=err==kNoErr && valid_ip(ip->ip);
    *last_error=err==kNoErr?0:err;
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
    printf("WIFI: calling StartNetwork(SoftAP)\r\n");
    err=StartNetwork(&config);
    printf("WIFI: StartNetwork(SoftAP) result = %d\r\n",err);
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
static void ap_control_step(uint32_t now,wifi_station_phase_t phase,
                            uint32_t *eligible_since,uint32_t *next_restore_ms,
                            unsigned *restore_failure_index)
{
    wifi_policy_t snapshot;
    int observed,policy_closed,eligible,station_ready,ota_busy;
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
    observed=recovery_ap_probe();
    if (!eligible) *eligible_since=0;
    if (wifi_recovery_ap_needs_restore(eligible,policy_closed,observed) &&
        (!ota_busy || !station_ready) &&
        (!*next_restore_ms || (int32_t)(now-*next_restore_ms)>=0)) {
        printf("WIFI: Recovery AP missing, restoring\r\n");
        lock_status(); status.recovery_ap_restoring=1; unlock_status();
        err=recovery_ap_start();
        mico_thread_msleep(400);
        observed=recovery_ap_probe();
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
            printf("WIFI: Recovery AP restored\r\n");
        } else {
            uint32_t delay=wifi_ap_restore_backoff_ms((*restore_failure_index)++);
            *next_restore_ms=mico_rtos_get_time()+delay;
            printf("WIFI: Recovery AP restore failed: %d; retry in %lu ms\r\n",err,(unsigned long)delay);
        }
    } else if (observed) {
        *restore_failure_index=0; *next_restore_ms=0;
    }
    if (eligible && observed) {
        if (!*eligible_since) {
            *eligible_since=now?now:1;
            printf("WIFI: STA ready, Recovery AP will close in 3 seconds\r\n");
        } else if ((uint32_t)(now-*eligible_since)>=WIFI_AP_CLOSE_GRACE_MS) {
            /* Re-read policy and OTA before the destructive operation. */
            lock_status();
            snapshot=policy;
            station_ready=status.link_cached && status.ip_valid_cached &&
                          !strcmp(status.state,"connected") &&
                          !strcmp(status.ssid,snapshot.saved_ssid);
            unlock_status();
            if (snapshot.auto_connect && snapshot.disable_ap_after_connect &&
                station_ready && !recovery_ota_busy()) {
                wlan_operation("suspend_softap");
                printf("WIFI: calling SuspendSoftAP\r\n");
                err=micoWlanSuspendSoftAP();
                printf("WIFI: SuspendSoftAP result = %d\r\n",err);
                mico_thread_msleep(400);
                observed=recovery_ap_probe();
                if (err==kNoErr && !observed) {
                    lock_status(); status.recovery_ap_policy_closed=1; unlock_status();
                    printf("WIFI: Recovery AP stopped after STA became ready\r\n");
                } else printf("WIFI: Recovery AP stop not confirmed: %d\r\n",err);
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
    uint32_t rearm_wait_until=0,ap_eligible_since=0,next_ap_restore=0;
    unsigned ap_restore_failures=0;
    int armed=0,good,last_error,disconnect,do_scan,boot_waiting;
    int failed_start_needs_cleanup=0;
    OSStatus err;
    (void)arg;
    lock_status(); status.wifi_control_worker_running=1; unlock_status();
    printf("WIFI: control worker starting\r\n");
    printf("WIFI: native retry interval = %u ms\r\n",WIFI_NATIVE_RETRY_INTERVAL_MS);
    for (;;) {
        uint32_t now=mico_rtos_get_time();
        int changed;
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
            if (armed || status.station_started_once) {
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
            network_health_notify_link_down();
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
                    wifi_station_note_good(&samples);
                    lock_status(); station_cache_good(&link,&ip);
                    status.consecutive_bad_samples=0; status.consecutive_good_samples=samples.good_samples; unlock_status();
                } else if (wifi_station_note_bad(&samples)) {
                    phase=desired.want_connected?WIFI_STA_NATIVE_RECONNECT_WAIT:WIFI_STA_IDLE;
                    native_since=desired.want_connected?now:0;
                    lock_status();
                    status.disconnect_count++; status.last_disconnect_ms=now;
                    status.consecutive_bad_samples=samples.bad_samples;
                    status.consecutive_good_samples=0; status.active=0;
                    status.native_reconnect_since_ms=native_since;
                    status.recovery_ap_policy_closed=0;
                    snprintf(status.state,sizeof(status.state),"disconnected");
                    snprintf(status.message,sizeof(status.message),"家庭 Wi-Fi 链路已丢失，等待 MiCO 自动重连。");
                    clear_network(); unlock_status();
                    printf("WIFI: station link lost after 3 bad samples; waiting for native reconnect\r\n");
                    network_health_notify_link_down();
                    if (!desired.want_connected) {
                        /* Saved AUTO credentials were cleared during this connection. */
                        err=station_suspend();
                        armed=0;
                        lock_status(); status.station_armed=0; status.last_wlan_error=err; unlock_status();
                    }
                } else {
                    lock_status(); status.consecutive_bad_samples=samples.bad_samples; unlock_status();
                }
            } else {
                if (good && wifi_station_note_good(&samples)) {
                    int native_recovered=phase==WIFI_STA_NATIVE_RECONNECT_WAIT ||
                                         phase==WIFI_STA_REARM_WAIT;
                    phase=WIFI_STA_CONNECTED; native_since=0; rearm_wait_until=0;
                    lock_status(); station_cache_good(&link,&ip);
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
                    printf("WIFI: station connected, IP = %s, RSSI = %d\r\n",ip.ip,link.rssi);
                    if (native_recovered) printf("WIFI: Station recovered by MOC native reconnect\r\n");
                    network_health_notify_link_ready();
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
        /* AP is restored before an explicit Station arm/re-arm. */
        ap_control_step(now,phase,&ap_eligible_since,&next_ap_restore,&ap_restore_failures);
        if (desired.want_connected && !disconnect && !scan_is_active() && !recovery_ota_busy()) {
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
                if (!status.station_started_once) printf("WIFI: first Station arm\r\n");
                else printf("WIFI: Station arm\r\n");
                err=station_start(&desired);
                armed=err==kNoErr;
                arm_since=mico_rtos_get_time();
                wifi_station_samples_reset(&samples);
                phase=armed?WIFI_STA_ARMED_CONNECTING:WIFI_STA_REARM_WAIT;
                lock_status();
                status.station_started_once=1; status.station_armed=armed;
                status.last_wlan_error=err; status.active=1;
                snprintf(status.ssid,sizeof(status.ssid),"%s",desired.ssid);
                snprintf(status.state,sizeof(status.state),err==kNoErr?"connecting":"failed");
                if (err!=kNoErr) snprintf(status.message,sizeof(status.message),"Station 启动失败，稍后重试。");
                unlock_status();
                if (err!=kNoErr) {
                    printf("WIFI: STA start failed: %d\r\n",err);
                    failed_start_needs_cleanup=1;
                    rearm_wait_until=arm_since+(last_rearm?WIFI_CONTROLLED_REARM_MIN_INTERVAL_MS:5000u);
                    last_rearm=arm_since;
                }
            } else if (armed && (phase==WIFI_STA_NATIVE_RECONNECT_WAIT || phase==WIFI_STA_REARM_WAIT) &&
                       wifi_station_rearm_due(now,native_since?native_since:arm_since,last_rearm)) {
                printf("WIFI: native reconnect timeout, controlled re-arm\r\n");
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
                status.station_armed=armed; status.station_rearm_count++;
                status.last_station_rearm_ms=last_rearm; status.last_wlan_error=err;
                status.native_reconnect_since_ms=0;
                unlock_status();
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
    manager_ready=1;
    err=mico_system_notify_register(mico_notify_WIFI_SCAN_ADV_COMPLETED,(void *)scan_complete,NULL);
    scan_registered=err==kNoErr;
    if (!scan_registered) printf("WIFI: scan callback registration failed = %d\r\n",err);
    printf("WIFI: manager initialized\r\n");
    return kNoErr;
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
        status.boot_auto_connect_not_before_ms=mico_rtos_get_time()+WIFI_BOOT_AUTO_CONNECT_GRACE_MS;
        desired_revision++;
    }
    unlock_status();
    if (config.wifi_auto_connect && config.wifi_ssid[0]) {
        printf("WIFI: auto-connect desired SSID = %s\r\n",config.wifi_ssid);
        printf("WIFI: boot auto-connect grace = %u ms\r\n",WIFI_BOOT_AUTO_CONNECT_GRACE_MS);
    }
    memset(config.wifi_password,0,sizeof(config.wifi_password));
    err=mico_rtos_create_thread(&wifi_control_thread,MICO_APPLICATION_PRIORITY,
                                "openm1_wifi_control",wifi_control_worker,
                                WIFI_CONTROL_WORKER_STACK,0);
    control_worker_created=err==kNoErr;
    if (err!=kNoErr) {
        micoMemInfo_t *memory=MicoGetMemoryInfo();
        printf("WIFI: control worker start failed: %d, free heap = %d\r\n",
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
    printf("WIFI: STA connect requested, SSID = %s\r\n",ssid);
    return 0;
}
int wifi_manager_disconnect(void)
{
    if (!manager_ready || !control_worker_created) return -1;
    lock_status();
    wifi_station_manual_disconnect(&desired_station);
    desired_revision++; disconnect_requested=1;
    status.boot_auto_connect_waiting=0;
    unlock_status();
    printf("WIFI: manual disconnect; runtime reconnect paused\r\n");
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
void wifi_manager_status_json(char *out,size_t out_size)
{
    wifi_status_t snapshot;
    wifi_policy_t saved;
    wifi_desired_source_t source;
    int want,latch,scan_supported;
    char ssid[66],saved_ssid[66],message[194];
    const char *ap_state;
    uint32_t link_uptime=0;
    lock_status();
    snapshot=status; saved=policy;
    source=desired_station.source;
    want=desired_station.want_connected;
    latch=desired_station.manual_disconnect_latched;
    scan_supported=scan_registered;
    unlock_status();
    ap_state=snapshot.recovery_ap_restoring?"restoring":
        snapshot.recovery_ap_observed_on?"on":
        snapshot.recovery_ap_policy_closed?"policy_closed":"off";
    if (snapshot.link_cached && snapshot.ip_valid_cached && snapshot.link_up_since_ms)
        link_uptime=mico_rtos_get_time()-snapshot.link_up_since_ms;
    json_string(ssid,sizeof(ssid),snapshot.ssid);
    json_string(saved_ssid,sizeof(saved_ssid),saved.saved_ssid);
    json_string(message,sizeof(message),snapshot.message);
    snprintf(out,out_size,
      "{\"recovery_ap\":%s,\"recovery_ap_state\":\"%s\",\"recovery_ssid\":\"%s\",\"mac\":\"%s\",\"sta_state\":\"%s\",\"ssid\":\"%s\",\"ip\":\"%s\",\"gateway\":\"%s\",\"netmask\":\"%s\",\"dns\":\"%s\",\"rssi\":%d,\"message\":\"%s\",\"sta_connect_supported\":true,\"scan_supported\":%s,\"auto_connect\":%s,\"ap_disable_after_connect\":%s,\"saved_ssid\":\"%s\","
      "\"wifi_control_worker_running\":%s,\"wifi_control_loop_count\":%lu,\"last_wifi_control_tick_ms\":%lu,\"want_connected\":%s,\"desired_source\":\"%s\",\"manual_disconnect_latched\":%s,\"link_cached\":%s,\"ip_valid_cached\":%s,\"link_uptime_ms\":%lu,\"disconnect_count\":%lu,\"last_disconnect_ms\":%lu,\"last_connect_ms\":%lu,\"consecutive_bad_samples\":%lu,\"consecutive_good_samples\":%lu,"
      "\"station_phase\":\"%s\",\"station_armed\":%s,\"station_started_once\":%s,\"native_retry_interval_ms\":%u,\"native_reconnect_waiting\":%s,\"native_reconnect_since_ms\":%lu,\"native_reconnect_successes\":%lu,\"last_native_reconnect_success_ms\":%lu,\"station_rearm_count\":%lu,\"last_station_rearm_ms\":%lu,\"last_wlan_operation\":\"%s\",\"last_wlan_error\":%d,\"boot_auto_connect_grace_ms\":%u,\"boot_auto_connect_waiting\":%s,"
      "\"recovery_ap_observed\":%s,\"recovery_ap_policy_closed\":%s,\"recovery_ap_restore_count\":%lu,\"recovery_ap_last_probe_ms\":%lu,\"recovery_ap_last_restore_ms\":%lu,\"recovery_ap_last_error\":%d}",
      snapshot.recovery_ap_observed_on?"true":"false",ap_state,recovery_ssid(),recovery_mac(),snapshot.state,ssid,
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
      snapshot.recovery_ap_observed_on?"true":"false",snapshot.recovery_ap_policy_closed?"true":"false",
      (unsigned long)snapshot.recovery_ap_restore_count,(unsigned long)snapshot.recovery_ap_last_probe_ms,
      (unsigned long)snapshot.recovery_ap_last_restore_ms,snapshot.recovery_ap_last_error);
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
const char *wifi_manager_scan_json(void)
{
    unsigned i;
    size_t used;
    if (!scan_registered) return "{\"supported\":false,\"state\":\"failed\",\"networks\":[]}";
    lock_status();
    if (!strcmp(scan_state,"scanning") && mico_rtos_get_time()-scan_started_ms>15000)
        strcpy(scan_state,"failed");
    used=(size_t)snprintf(scan_json,sizeof(scan_json),"{\"supported\":true,\"state\":\"%s\",\"networks\":[",scan_state);
    for (i=0;i<scan_count && used+160<sizeof(scan_json);i++) {
        char escaped[70];
        int n;
        json_string(escaped,sizeof(escaped),scan_aps[i].ssid);
        n=snprintf(scan_json+used,sizeof(scan_json)-used,
            "%s{\"ssid\":\"%s\",\"rssi\":%d,\"security\":\"%s\",\"channel\":%u}",
            i?",":"",escaped,scan_aps[i].rssi,security_name(scan_aps[i].security),scan_aps[i].channel);
        if (n<0 || (size_t)n>=sizeof(scan_json)-used) break;
        used+=(size_t)n;
    }
    snprintf(scan_json+used,sizeof(scan_json)-used,"]}");
    unlock_status();
    return scan_json;
}
int wifi_manager_station_ready(void)
{
    int ready;
    lock_status();
    ready=status.link_cached && status.ip_valid_cached && !strcmp(status.state,"connected");
    unlock_status();
    return ready;
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
