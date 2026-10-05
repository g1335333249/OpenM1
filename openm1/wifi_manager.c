#include "wifi_manager.h"
#include "recovery.h"
#include "network_health.h"
#include "config_store.h"
#include "wifi_settings.h"
#include "wifi_station_logic.h"
#include <stdio.h>
#include <string.h>

/* Recovery is started before this module; boot settings are applied later. */
typedef struct {
    char state[16];
    char ssid[32];
    char ip[16];
    char gateway[16];
    char netmask[16];
    char dns[16];
    char message[96];
    int rssi;
    int active;
    int link_cached,ip_valid_cached;
    int supervisor_running;
    int last_wlan_error;
    uint32_t link_up_since_ms,disconnect_count,reconnect_attempt_count;
    uint32_t reconnect_success_count,last_disconnect_ms,last_reconnect_attempt_ms;
    uint32_t last_reconnect_success_ms,last_connect_ms,current_backoff_ms;
    uint32_t backoff_until_ms;
    uint32_t consecutive_bad_samples,consecutive_good_samples;
    int station_started_once,cleanup_required,boot_auto_connect_waiting;
    uint32_t boot_auto_connect_not_before_ms;
    int recovery_ap_observed_on,recovery_ap_policy_closed,recovery_ap_restoring;
    uint32_t recovery_ap_restore_count,recovery_ap_last_probe_ms,recovery_ap_last_restore_ms;
    int recovery_ap_last_error;
} wifi_status_t;

static wifi_status_t status = { .state = "disconnected" };
static wifi_station_desired_t desired_station;
static mico_mutex_t status_mutex;
static mico_mutex_t wlan_control_mutex;
static mico_thread_t station_supervisor_thread;
static mico_thread_t ap_policy_thread;
static int manager_ready;
static int station_supervisor_ready;
static int ap_policy_worker_ready;
#define WIFI_AP_POLICY_STACK 2048u
#define WIFI_AP_CLOSE_GRACE_MS 3000u
#define WIFI_AP_MONITOR_INTERVAL_MS 1000u
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
static int recovery_ap_probe(void);

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
/* Lock order: never hold status_mutex while taking wlan_control_mutex or calling WLAN HAL. */
static OSStatus station_suspend(void)
{
    OSStatus err;
    mico_rtos_lock_mutex(&wlan_control_mutex);
    printf("WIFI: calling SuspendStation\r\n");
    err=micoWlanSuspendStation();
    printf("WIFI: SuspendStation result = %d\r\n",err);
    mico_rtos_unlock_mutex(&wlan_control_mutex);
    return err;
}
static OSStatus station_start(network_InitTypeDef_st *config)
{
    OSStatus err;
    mico_rtos_lock_mutex(&wlan_control_mutex);
    printf("WIFI: calling StartNetwork(STA)\r\n");
    err=StartNetwork(config);
    printf("WIFI: StartNetwork(STA) result = %d\r\n",err);
    mico_rtos_unlock_mutex(&wlan_control_mutex);
    return err;
}
static void station_snapshot(LinkStatusTypeDef *link,IPStatusTypedef *ip,int *good,int *last_error)
{
    OSStatus err;
    memset(link,0,sizeof(*link)); memset(ip,0,sizeof(*ip));
    mico_rtos_lock_mutex(&wlan_control_mutex);
    err=micoWlanGetLinkStatus(link);
    if (err!=kNoErr) { *good=0; *last_error=err; goto done; }
    if (link->is_connected!=1) { *good=0; *last_error=0; goto done; }
    err=micoWlanGetIPStatus(ip,Station);
    ip->ip[sizeof(ip->ip)-1]=0;
    *good=(err==kNoErr && valid_ip(ip->ip));
    *last_error=err==kNoErr?0:err;
done:
    mico_rtos_unlock_mutex(&wlan_control_mutex);
}
static void station_cache_good(const LinkStatusTypeDef *link,const IPStatusTypedef *ip)
{
    copy_ip(status.ip,ip->ip); copy_ip(status.gateway,ip->gate);
    copy_ip(status.netmask,ip->mask); copy_ip(status.dns,ip->dns);
    status.rssi=link->rssi;
    status.link_cached=1; status.ip_valid_cached=1;
}
static int scan_is_active(void)
{
    int active;
    lock_status();
    if (!strcmp(scan_state,"scanning") && mico_rtos_get_time()-scan_started_ms>15000)
        strcpy(scan_state,"failed");
    active=!strcmp(scan_state,"scanning");
    unlock_status();
    return active;
}
static uint32_t desired_revision;
static int disconnect_requested;

static void wifi_station_supervisor_worker(mico_thread_arg_t arg)
{
    station_phase_t phase=STATION_IDLE;
    wifi_station_samples_t samples={0,0};
    wifi_station_desired_t desired;
    LinkStatusTypeDef link;
    IPStatusTypedef ip;
    network_InitTypeDef_st local_config;
    uint32_t seen_revision=0,connect_started=0,next_attempt=0;
    unsigned failure_index=0,attempt_serial=0;
    int good,last_error,manual_disconnect,retry,boot_waiting;
    OSStatus err;
    (void)arg;
    lock_status(); status.supervisor_running=1; unlock_status();
    printf("WIFI: station supervisor started\r\n");
    for (;;) {
        uint32_t now=mico_rtos_get_time();
        lock_status();
        desired=desired_station;
        manual_disconnect=disconnect_requested;
        disconnect_requested=0;
        if (seen_revision!=desired_revision) {
            seen_revision=desired_revision;
            failure_index=0;
            attempt_serial=0;
            if (phase==STATION_BACKOFF) phase=STATION_IDLE;
        }
        unlock_status();
        if (manual_disconnect || (!desired.want_connected && phase==STATION_CONNECTING)) {
            int started_once;
            lock_status(); started_once=status.station_started_once; unlock_status();
            if (started_once) {
                printf("WIFI: manual disconnect\r\n");
                station_suspend();
            }
            phase=STATION_IDLE;
            wifi_station_samples_reset(&samples);
            lock_status();
            status.active=0; status.current_backoff_ms=0; status.backoff_until_ms=0;
            status.cleanup_required=0;
            unlock_status();
            mico_thread_msleep(WIFI_STATION_MONITOR_INTERVAL_MS);
            continue;
        }
        if (phase==STATION_IDLE && desired.want_connected && !scan_is_active() && !recovery_ota_busy()) {
            int ap_observed,started_once,cleanup_required;
            uint32_t last_disconnect;
            lock_status();
            ap_observed=status.recovery_ap_observed_on;
            last_disconnect=status.last_disconnect_ms;
            started_once=status.station_started_once;
            cleanup_required=status.cleanup_required;
            if (desired.source==WIFI_DESIRED_AUTO && !started_once &&
                status.boot_auto_connect_waiting &&
                (int32_t)(now-status.boot_auto_connect_not_before_ms)>=0)
                status.boot_auto_connect_waiting=0;
            boot_waiting=status.boot_auto_connect_waiting;
            unlock_status();
            if (boot_waiting) {
                mico_thread_msleep(WIFI_STATION_MONITOR_INTERVAL_MS);
                continue;
            }
            /* Give the AP policy its first chance to restore Recovery after a loss. */
            if (!ap_observed && last_disconnect && now-last_disconnect<2000u) {
                mico_thread_msleep(WIFI_STATION_MONITOR_INTERVAL_MS); continue;
            }
            memset(&local_config,0,sizeof(local_config));
            local_config.wifi_mode=Station;
            memcpy(local_config.wifi_ssid,desired.ssid,strlen(desired.ssid)+1);
            memcpy(local_config.wifi_key,desired.password,strlen(desired.password)+1);
            local_config.dhcpMode=DHCP_Client;
            retry=attempt_serial>0;
            attempt_serial++;
            lock_status();
            status.active=1;
            status.current_backoff_ms=0; status.backoff_until_ms=0;
            snprintf(status.state,sizeof(status.state),"connecting");
            snprintf(status.ssid,sizeof(status.ssid),"%s",desired.ssid);
            snprintf(status.message,sizeof(status.message),retry?"正在重新连接……":"正在连接……");
            clear_network();
            if (retry) {
                status.reconnect_attempt_count++;
                status.last_reconnect_attempt_ms=now;
            }
            unlock_status();
            if (retry) printf("WIFI: reconnect attempt %u starting\r\n",attempt_serial-1);
            else printf("WIFI: station connecting\r\n");
            if (wifi_station_should_cleanup(started_once,cleanup_required)) {
                printf("WIFI: cleanup required before retry\r\n");
                printf("WIFI: Station cleanup before retry\r\n");
                station_suspend();
                mico_thread_msleep(400);
            } else if (!started_once) {
                printf("WIFI: first station start, cleanup skipped\r\n");
            } else {
                printf("WIFI: reconnect without cleanup\r\n");
            }
            if (scan_is_active()) { phase=STATION_IDLE; continue; }
            lock_status(); manual_disconnect=disconnect_requested || !desired_station.want_connected ||
                desired_revision!=seen_revision; unlock_status();
            if (manual_disconnect) { phase=STATION_IDLE; continue; }
            err=station_start(&local_config);
            memset(&local_config,0,sizeof(local_config));
            lock_status();
            status.station_started_once=1;
            status.cleanup_required=wifi_station_cleanup_for_next_attempt(
                err==kNoErr?WIFI_STATION_FIRST_START:WIFI_STATION_START_ERROR);
            status.last_wlan_error=err;
            unlock_status();
            if (err!=kNoErr) {
                printf("WIFI: STA start failed: %d\r\n",err);
                phase=STATION_BACKOFF;
            } else {
                phase=STATION_CONNECTING; connect_started=mico_rtos_get_time();
                wifi_station_samples_reset(&samples);
                printf("WIFI: StartNetwork result = %d\r\n",err);
            }
        }
        if (phase==STATION_CONNECTING || phase==STATION_CONNECTED) {
            station_snapshot(&link,&ip,&good,&last_error);
            lock_status(); manual_disconnect=disconnect_requested ||
                desired_revision!=seen_revision ||
                (!desired_station.want_connected && phase==STATION_CONNECTING);
            unlock_status();
            if (manual_disconnect) { mico_thread_msleep(WIFI_STATION_MONITOR_INTERVAL_MS); continue; }
            lock_status(); status.last_wlan_error=last_error; unlock_status();
            if (phase==STATION_CONNECTING) {
                if (good && wifi_station_note_good(&samples)) {
                    now=mico_rtos_get_time();
                    lock_status();
                    station_cache_good(&link,&ip);
                    snprintf(status.state,sizeof(status.state),"connected");
                    status.active=0; status.message[0]=0;
                    status.link_up_since_ms=now; status.last_connect_ms=now;
                    status.consecutive_good_samples=samples.good_samples;
                    status.consecutive_bad_samples=0;
                    if (attempt_serial>1) {
                        status.reconnect_success_count++;
                        status.last_reconnect_success_ms=now;
                    }
                    status.current_backoff_ms=0; status.backoff_until_ms=0;
                    unlock_status();
                    printf("WIFI: station connected, IP = %s, RSSI = %d\r\n",ip.ip,link.rssi);
                    if (attempt_serial>1) printf("WIFI: station reconnected\r\n");
                    phase=STATION_CONNECTED; wifi_station_reset_backoff(&failure_index);
                    network_health_notify_link_ready();
                } else if (!good) {
                    wifi_station_note_bad(&samples);
                    lock_status(); status.consecutive_good_samples=0;
                    status.consecutive_bad_samples=samples.bad_samples; unlock_status();
                } else {
                    lock_status(); status.consecutive_good_samples=samples.good_samples;
                    status.consecutive_bad_samples=0; unlock_status();
                }
                if (phase==STATION_CONNECTING && (uint32_t)(mico_rtos_get_time()-connect_started)>=WIFI_STA_CONNECT_TIMEOUT_MS) {
                    printf("WIFI: STA connect timeout\r\n");
                    lock_status(); status.cleanup_required=wifi_station_cleanup_for_next_attempt(WIFI_STATION_CONNECT_TIMEOUT); unlock_status();
                    phase=STATION_BACKOFF;
                }
            } else if (good) {
                wifi_station_note_good(&samples);
                lock_status(); station_cache_good(&link,&ip);
                status.consecutive_bad_samples=0;
                status.consecutive_good_samples=samples.good_samples; unlock_status();
            } else if (wifi_station_note_bad(&samples)) {
                now=mico_rtos_get_time();
                lock_status();
                status.disconnect_count++; status.last_disconnect_ms=now;
                status.consecutive_bad_samples=samples.bad_samples;
                status.consecutive_good_samples=0;
                status.active=0;
                snprintf(status.state,sizeof(status.state),"disconnected");
                snprintf(status.message,sizeof(status.message),"家庭 Wi-Fi 链路已丢失，正在恢复。");
                clear_network();
                status.cleanup_required=wifi_station_cleanup_for_next_attempt(WIFI_STATION_LINK_LOST);
                status.recovery_ap_policy_closed=0;
                unlock_status();
                printf("WIFI: station link lost after 3 bad samples\r\n");
                network_health_notify_link_down();
                phase=wifi_station_phase_after_loss(desired.want_connected);
                failure_index=0;
            } else {
                lock_status(); status.consecutive_bad_samples=samples.bad_samples;
                status.consecutive_good_samples=0; unlock_status();
            }
        }
        if (phase==STATION_BACKOFF) {
            uint32_t delay;
            lock_status(); desired=desired_station; unlock_status();
            if (!desired.want_connected) { phase=STATION_IDLE; continue; }
            delay=wifi_station_backoff_ms(failure_index++);
            if (failure_index>5) failure_index=5;
            now=mico_rtos_get_time(); next_attempt=now+delay;
            lock_status(); status.active=0; status.current_backoff_ms=delay;
            status.backoff_until_ms=next_attempt;
            if (!strcmp(status.state,"connecting")) {
                snprintf(status.state,sizeof(status.state),"failed");
                snprintf(status.message,sizeof(status.message),"连接失败，等待自动重试。");
            }
            unlock_status();
            printf("WIFI: reconnect attempt %u in %lu ms\r\n",attempt_serial,(unsigned long)delay);
            while ((int32_t)(mico_rtos_get_time()-next_attempt)<0) {
                uint32_t revision;
                lock_status(); desired=desired_station; manual_disconnect=disconnect_requested;
                revision=desired_revision; unlock_status();
                if (revision!=seen_revision) { failure_index=0; attempt_serial=0; break; }
                if (!desired.want_connected || manual_disconnect) break;
                mico_thread_msleep(WIFI_STATION_MONITOR_INTERVAL_MS);
            }
            phase=STATION_IDLE;
            continue;
        }
        mico_thread_msleep(WIFI_STATION_MONITOR_INTERVAL_MS);
    }
}
OSStatus wifi_manager_init(void)
{
    OSStatus err=mico_rtos_init_mutex(&status_mutex);
    if (err!=kNoErr) return err;
    err=mico_rtos_init_mutex(&wlan_control_mutex);
    if (err!=kNoErr) return err;
    manager_ready=1;
    recovery_ap_probe(); /* Observe hardware; an initial StartNetwork result is not proof. */
    err=mico_system_notify_register(mico_notify_WIFI_SCAN_ADV_COMPLETED,(void *)scan_complete,NULL);
    scan_registered=(err==kNoErr);
    if (!scan_registered) printf("WIFI: scan callback registration failed = %d\r\n",err);
    err=mico_rtos_create_thread(&station_supervisor_thread,MICO_APPLICATION_PRIORITY,
                               "openm1_sta_supervisor",wifi_station_supervisor_worker,
                               WIFI_STATION_SUPERVISOR_STACK,0);
    station_supervisor_ready=(err==kNoErr);
    if (err!=kNoErr) {
        micoMemInfo_t *memory=MicoGetMemoryInfo();
        printf("WIFI: station supervisor start failed: %d, free heap = %d\r\n",
               err,memory?memory->free_memory:-1);
    }
    printf("WIFI: manager initialized\r\n");
    return err;
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
    mico_rtos_lock_mutex(&wlan_control_mutex);
    printf("WIFI: calling StartNetwork(SoftAP)\r\n");
    err=StartNetwork(&config);
    printf("WIFI: StartNetwork(SoftAP) result = %d\r\n",err);
    mico_rtos_unlock_mutex(&wlan_control_mutex);
    return err;
}

static int recovery_ap_probe(void)
{
    IPStatusTypedef ap;
    OSStatus err;
    int observed;
    memset(&ap,0,sizeof(ap));
    mico_rtos_lock_mutex(&wlan_control_mutex);
    err=micoWlanGetIPStatus(&ap,Soft_AP);
    mico_rtos_unlock_mutex(&wlan_control_mutex);
    ap.ip[sizeof(ap.ip)-1]=0;
    observed=err==kNoErr && !strcmp(ap.ip,RECOVERY_IP);
    lock_status();
    status.recovery_ap_observed_on=observed;
    status.recovery_ap_last_probe_ms=mico_rtos_get_time();
    if (observed) status.recovery_ap_last_error=0;
    else if (!status.recovery_ap_last_error)
        status.recovery_ap_last_error=err==kNoErr?-1:err;
    unlock_status();
    return observed;
}

static int station_matches_saved(const char *saved_ssid)
{
    int match;
    lock_status();
    match=!status.active && !strcmp(status.state,"connected") &&
          !strcmp(status.ssid,saved_ssid);
    unlock_status();
    return match;
}

static void ap_policy_worker(mico_thread_arg_t arg)
{
    uint32_t eligible_since=0,next_restore_ms=0;
    unsigned restore_failure_index=0;
    openm1_config_t config;
    OSStatus err;
    int eligible,base_eligible,observed,policy_closed,ota_busy,station_ready;
    (void)arg;
    for (;;) {
        uint32_t now=mico_rtos_get_time();
        config_store_get(&config);
        station_ready=wifi_manager_station_ready();
        ota_busy=recovery_ota_busy();
        base_eligible=wifi_ap_policy_can_close(&config,station_ready,
                                              station_matches_saved(config.wifi_ssid),0);
        eligible=base_eligible && !ota_busy;
        observed=recovery_ap_probe();
        lock_status();
        if (!base_eligible) status.recovery_ap_policy_closed=0;
        policy_closed=status.recovery_ap_policy_closed;
        unlock_status();
        if (!eligible) {
            eligible_since=0;
        }
        /* A running OTA over Station must not be interrupted just to reopen an
         * intentionally closed AP. A lost Station always restores Recovery. */
        if (wifi_recovery_ap_needs_restore(eligible,policy_closed,observed) &&
            (!ota_busy || !station_ready) &&
            (next_restore_ms==0 || (int32_t)(now-next_restore_ms)>=0)) {
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
                restore_failure_index=0;
                next_restore_ms=0;
                printf("WIFI: Recovery AP restored after station loss or startup failure\r\n");
            } else {
                uint32_t delay=wifi_station_backoff_ms(restore_failure_index++);
                next_restore_ms=mico_rtos_get_time()+delay;
                printf("WIFI: Recovery AP restore failed: %d; retry in %lu ms\r\n",
                       err,(unsigned long)delay);
            }
        } else if (observed) {
            restore_failure_index=0;
            next_restore_ms=0;
        }
        if (eligible && observed) {
            now=mico_rtos_get_time();
            if (!eligible_since) {
                eligible_since=now?now:1;
                printf("WIFI: STA ready, Recovery AP will close in 3 seconds\r\n");
            } else if (now-eligible_since>=WIFI_AP_CLOSE_GRACE_MS) {
                config_store_get(&config);
                if (!wifi_ap_policy_can_close(&config,wifi_manager_station_ready(),
                                              station_matches_saved(config.wifi_ssid),
                                              recovery_ota_busy())) {
                    eligible_since=0;
                    mico_thread_msleep(WIFI_AP_MONITOR_INTERVAL_MS);
                    continue;
                }
                mico_rtos_lock_mutex(&wlan_control_mutex);
                printf("WIFI: calling SuspendSoftAP\r\n");
                err=micoWlanSuspendSoftAP();
                printf("WIFI: SuspendSoftAP result = %d\r\n",err);
                mico_rtos_unlock_mutex(&wlan_control_mutex);
                mico_thread_msleep(400);
                observed=recovery_ap_probe();
                if (err==kNoErr && !observed) {
                    lock_status(); status.recovery_ap_policy_closed=1; unlock_status();
                    printf("WIFI: Recovery AP stopped after STA became ready\r\n");
                } else {
                    printf("WIFI: Recovery AP stop not confirmed: %d\r\n",err);
                }
                eligible_since=0;
            }
        } else if (!eligible) {
            eligible_since=0;
        }
        mico_thread_msleep(WIFI_AP_MONITOR_INTERVAL_MS);
    }
}

OSStatus wifi_manager_apply_boot_settings(void)
{
    openm1_config_t config;
    OSStatus err;
    if (!manager_ready) return kNotPreparedErr;
    err=mico_rtos_create_thread(&ap_policy_thread,MICO_APPLICATION_PRIORITY,
                               "openm1_ap_policy",ap_policy_worker,WIFI_AP_POLICY_STACK,0);
    ap_policy_worker_ready=(err==kNoErr);
    if (err!=kNoErr) {
        micoMemInfo_t *memory=MicoGetMemoryInfo();
        printf("WIFI: AP policy worker start failed: %d, free heap = %d\r\n",
               err,memory?memory->free_memory:-1);
    }
    config_store_get(&config);
    if (station_supervisor_ready && config.wifi_auto_connect && config.wifi_ssid[0]) {
        printf("WIFI: auto-connect starting\r\n");
        printf("WIFI: auto-connect SSID = %s\r\n",config.wifi_ssid);
        lock_status();
        wifi_station_desire(&desired_station,config.wifi_ssid,config.wifi_password,WIFI_DESIRED_AUTO);
        status.boot_auto_connect_waiting=1;
        status.boot_auto_connect_not_before_ms=mico_rtos_get_time()+WIFI_BOOT_AUTO_CONNECT_GRACE_MS;
        desired_revision++;
        unlock_status();
        printf("WIFI: station connection desired: %s\r\n",config.wifi_ssid);
        printf("WIFI: boot auto-connect grace %u ms\r\n",WIFI_BOOT_AUTO_CONNECT_GRACE_MS);
    }
    memset(config.wifi_password,0,sizeof(config.wifi_password));
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
    if (!ap_policy_worker_ready && config.ap_disable_after_sta_connected) {
        memset(config.wifi_password,0,sizeof(config.wifi_password)); return -4;
    }
    err=config_store_save(&config);
    if (err==kNoErr) {
        lock_status();
        if (desired_station.source==WIFI_DESIRED_AUTO) {
            if (!config.wifi_auto_connect || !config.wifi_ssid[0] ||
                strcmp(desired_station.ssid,config.wifi_ssid)) {
                wifi_station_drop_auto_desired(&desired_station);
                desired_revision++;
            } else {
                memcpy(desired_station.password,config.wifi_password,
                       sizeof(desired_station.password));
            }
        }
        unlock_status();
    }
    memset(config.wifi_password,0,sizeof(config.wifi_password));
    return err==kNoErr?0:-4;
}
int wifi_manager_connect(const char *ssid, const char *password)
{
    size_t pass_len;
    if (!manager_ready || !station_supervisor_ready || !WIFI_STA_CONNECT_SUPPORTED) return -3;
    if (!valid_input(ssid,sizeof(desired_station.ssid)-1) || !password) return -1;
    pass_len=strlen(password);
    if (pass_len>=sizeof(desired_station.password)) return -1;
    /* An empty key selects an open network; secured networks use a passphrase. */
    if (pass_len && !valid_input(password,sizeof(desired_station.password)-1)) return -1;
    lock_status();
    if (status.active || !strcmp(scan_state,"scanning")) { unlock_status(); return -2; }
    if (!strcmp(status.state,"connected")) { unlock_status(); return -4; }
    wifi_station_desire(&desired_station,ssid,password,WIFI_DESIRED_MANUAL);
    desired_revision++;
    disconnect_requested=0;
    status.boot_auto_connect_waiting=0;
    status.active=1;
    snprintf(status.state,sizeof(status.state),"connecting");
    snprintf(status.ssid,sizeof(status.ssid),"%s",ssid);
    snprintf(status.message,sizeof(status.message),"正在连接……");
    clear_network();
    unlock_status();
    printf("WIFI: STA connect requested\r\nWIFI: SSID = %s\r\n",ssid);
    return 0;
}
int wifi_manager_disconnect(void)
{
    if (!manager_ready || !station_supervisor_ready) return -1;
    lock_status();
    wifi_station_manual_disconnect(&desired_station);
    desired_revision++;
    disconnect_requested=1;
    status.boot_auto_connect_waiting=0;
    snprintf(status.state,sizeof(status.state),"disconnected");
    status.ssid[0]=0; status.message[0]=0;
    status.active=0; status.current_backoff_ms=0; status.backoff_until_ms=0;
    clear_network();
    unlock_status();
    printf("WIFI: manual disconnect; runtime reconnect paused\r\n");
    network_health_notify_link_down();
    return 0;
}
static void json_string(char *out, size_t capacity, const char *input)
{
    size_t n=0; unsigned char c;
    if (!capacity) return;
    while ((c=(unsigned char)*input++) && n+2<capacity) {
        if (c=='"' || c=='\\') { if (n+3>=capacity) break; out[n++]='\\'; out[n++]=c; }
        else if (c>=32) out[n++]=c;
    }
    out[n]=0;
}
void wifi_manager_status_json(char *out, size_t out_size)
{
    wifi_status_t snapshot;
    wifi_station_desired_t desired;
    char ssid[66],saved_ssid[66],message[194];
    int scan_supported;
    const char *ap_state;
    uint32_t link_uptime=0,backoff_remaining=0;
    openm1_config_t config;
    lock_status();
    snapshot=status;
    desired=desired_station;
    scan_supported=scan_registered;
    unlock_status();
    memset(desired.password,0,sizeof(desired.password));
    ap_state=snapshot.recovery_ap_restoring?"restoring":
        snapshot.recovery_ap_observed_on?"on":
        snapshot.recovery_ap_policy_closed?"policy_closed":"off";
    if (snapshot.link_cached && snapshot.ip_valid_cached && snapshot.link_up_since_ms)
        link_uptime=mico_rtos_get_time()-snapshot.link_up_since_ms;
    if (snapshot.current_backoff_ms &&
        (int32_t)(snapshot.backoff_until_ms-mico_rtos_get_time())>0)
        backoff_remaining=snapshot.backoff_until_ms-mico_rtos_get_time();
    config_store_get(&config);
    json_string(ssid,sizeof(ssid),snapshot.ssid);
    json_string(saved_ssid,sizeof(saved_ssid),config.wifi_ssid);
    json_string(message,sizeof(message),snapshot.message);
    snprintf(out,out_size,
      "{\"recovery_ap\":%s,\"recovery_ap_state\":\"%s\",\"recovery_ssid\":\"%s\",\"mac\":\"%s\",\"sta_state\":\"%s\",\"ssid\":\"%s\",\"ip\":\"%s\",\"gateway\":\"%s\",\"netmask\":\"%s\",\"dns\":\"%s\",\"rssi\":%d,\"message\":\"%s\",\"sta_connect_supported\":true,\"scan_supported\":%s,\"auto_connect\":%s,\"ap_disable_after_connect\":%s,\"saved_ssid\":\"%s\","
      "\"supervisor_running\":%s,\"want_connected\":%s,\"desired_source\":\"%s\",\"manual_disconnect_latched\":%s,\"link_cached\":%s,\"ip_valid_cached\":%s,\"disconnect_count\":%lu,\"reconnect_attempts\":%lu,\"reconnect_successes\":%lu,\"last_disconnect_ms\":%lu,\"last_reconnect_attempt_ms\":%lu,\"last_reconnect_success_ms\":%lu,\"current_backoff_ms\":%lu,\"link_uptime_ms\":%lu,\"consecutive_bad_samples\":%lu,\"consecutive_good_samples\":%lu,\"last_wlan_error\":%d,"
      "\"station_started_once\":%s,\"cleanup_required\":%s,\"boot_auto_connect_grace_ms\":%u,\"boot_auto_connect_waiting\":%s,"
      "\"recovery_ap_observed\":%s,\"recovery_ap_policy_closed\":%s,\"recovery_ap_restore_count\":%lu,\"recovery_ap_last_probe_ms\":%lu,\"recovery_ap_last_restore_ms\":%lu,\"recovery_ap_last_error\":%d}",
      snapshot.recovery_ap_observed_on?"true":"false",ap_state,recovery_ssid(),recovery_mac(),snapshot.state,ssid,snapshot.ip,snapshot.gateway,
      snapshot.netmask,snapshot.dns,snapshot.rssi,message,scan_supported?"true":"false",
      config.wifi_auto_connect?"true":"false",config.ap_disable_after_sta_connected?"true":"false",saved_ssid,
      snapshot.supervisor_running?"true":"false",desired.want_connected?"true":"false",
      desired.source==WIFI_DESIRED_MANUAL?"manual":desired.source==WIFI_DESIRED_AUTO?"auto":"none",
      desired.manual_disconnect_latched?"true":"false",snapshot.link_cached?"true":"false",
      snapshot.ip_valid_cached?"true":"false",(unsigned long)snapshot.disconnect_count,
      (unsigned long)snapshot.reconnect_attempt_count,(unsigned long)snapshot.reconnect_success_count,
      (unsigned long)snapshot.last_disconnect_ms,(unsigned long)snapshot.last_reconnect_attempt_ms,
      (unsigned long)snapshot.last_reconnect_success_ms,(unsigned long)backoff_remaining,
      (unsigned long)link_uptime,(unsigned long)snapshot.consecutive_bad_samples,
      (unsigned long)snapshot.consecutive_good_samples,snapshot.last_wlan_error,
      snapshot.station_started_once?"true":"false",snapshot.cleanup_required?"true":"false",
      WIFI_BOOT_AUTO_CONNECT_GRACE_MS,snapshot.boot_auto_connect_waiting?"true":"false",
      snapshot.recovery_ap_observed_on?"true":"false",
      snapshot.recovery_ap_policy_closed?"true":"false",
      (unsigned long)snapshot.recovery_ap_restore_count,
      (unsigned long)snapshot.recovery_ap_last_probe_ms,
      (unsigned long)snapshot.recovery_ap_last_restore_ms,
      snapshot.recovery_ap_last_error);
    memset(config.wifi_password,0,sizeof(config.wifi_password));
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
int wifi_manager_start_scan(void)
{
    if (!manager_ready || !scan_registered) return -2;
    if (recovery_ota_busy()) return -3;
    lock_status();
    if (!strcmp(scan_state,"scanning") || status.active) { unlock_status(); return -1; }
    strcpy(scan_state,"scanning");
    scan_count=0;
    scan_started_ms=mico_rtos_get_time();
    unlock_status();
    mico_rtos_lock_mutex(&wlan_control_mutex);
    printf("WIFI: calling StartScanAdv\r\n");
    micoWlanStartScanAdv();
    printf("WIFI: StartScanAdv requested\r\n");
    mico_rtos_unlock_mutex(&wlan_control_mutex);
    return 0;
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
