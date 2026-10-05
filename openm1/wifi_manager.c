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
} wifi_status_t;

static wifi_status_t status = { .state = "disconnected" };
static wifi_station_desired_t desired_station;
static mico_mutex_t status_mutex;
static mico_mutex_t wlan_control_mutex;
static mico_thread_t station_supervisor_thread;
static mico_thread_t ap_policy_thread;
static int manager_ready;
static int station_supervisor_ready;
static int recovery_ap_active;
static int ap_policy_worker_ready;
#define WIFI_AP_POLICY_STACK 2048u
#define WIFI_AP_CLOSE_GRACE_MS 3000u
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
/* Lock order: never hold status_mutex while taking wlan_control_mutex or calling WLAN HAL. */
static OSStatus station_suspend(void)
{
    OSStatus err;
    mico_rtos_lock_mutex(&wlan_control_mutex);
    err=micoWlanSuspendStation();
    mico_rtos_unlock_mutex(&wlan_control_mutex);
    return err;
}
static OSStatus station_start(network_InitTypeDef_st *config)
{
    OSStatus err;
    mico_rtos_lock_mutex(&wlan_control_mutex);
    err=StartNetwork(config);
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
    int good,last_error,manual_disconnect,retry;
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
            station_suspend();
            phase=STATION_IDLE;
            wifi_station_samples_reset(&samples);
            lock_status(); status.active=0; status.current_backoff_ms=0; status.backoff_until_ms=0; unlock_status();
            mico_thread_msleep(WIFI_STATION_MONITOR_INTERVAL_MS);
            continue;
        }
        if (phase==STATION_IDLE && desired.want_connected && !scan_is_active() && !recovery_ota_busy()) {
            int ap_active;
            uint32_t last_disconnect;
            lock_status(); ap_active=recovery_ap_active; last_disconnect=status.last_disconnect_ms; unlock_status();
            /* Give the AP policy its first chance to restore Recovery after a loss. */
            if (!ap_active && last_disconnect && now-last_disconnect<2000u) {
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
            station_suspend(); /* harmless if already disconnected; clears stale DHCP state */
            mico_thread_msleep(400);
            if (scan_is_active()) { phase=STATION_IDLE; continue; }
            lock_status(); manual_disconnect=disconnect_requested || !desired_station.want_connected ||
                desired_revision!=seen_revision; unlock_status();
            if (manual_disconnect) { phase=STATION_IDLE; continue; }
            err=station_start(&local_config);
            memset(&local_config,0,sizeof(local_config));
            lock_status(); status.last_wlan_error=err; unlock_status();
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
    recovery_ap_active=1; /* main() has already started Recovery SoftAP. */
    err=mico_system_notify_register(mico_notify_WIFI_SCAN_ADV_COMPLETED,(void *)scan_complete,NULL);
    scan_registered=(err==kNoErr);
    if (!scan_registered) printf("WIFI: scan callback registration failed = %d\r\n",err);
    err=mico_rtos_create_thread(&station_supervisor_thread,MICO_APPLICATION_PRIORITY,
                               "openm1_sta_supervisor",wifi_station_supervisor_worker,
                               WIFI_STATION_SUPERVISOR_STACK,0);
    station_supervisor_ready=(err==kNoErr);
    if (err!=kNoErr) printf("WIFI: station supervisor unavailable: %d\r\n",err);
    printf("WIFI: manager initialized\r\n");
    return err;
}

static OSStatus recovery_ap_start(void)
{
    network_InitTypeDef_st config;
    memset(&config,0,sizeof(config));
    config.wifi_mode=Soft_AP;
    snprintf(config.wifi_ssid,sizeof(config.wifi_ssid),"%s",recovery_ssid());
    memcpy(config.local_ip_addr,RECOVERY_IP,sizeof(RECOVERY_IP));
    memcpy(config.net_mask,"255.255.255.0",sizeof("255.255.255.0"));
    memcpy(config.gateway_ip_addr,RECOVERY_IP,sizeof(RECOVERY_IP));
    memcpy(config.dnsServer_ip_addr,RECOVERY_IP,sizeof(RECOVERY_IP));
    config.dhcpMode=DHCP_Server;
    mico_rtos_lock_mutex(&wlan_control_mutex);
    { OSStatus err=StartNetwork(&config);
      mico_rtos_unlock_mutex(&wlan_control_mutex);
      return err; }
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
    uint32_t eligible_since=0;
    openm1_config_t config;
    OSStatus err;
    int eligible,active;
    (void)arg;
    for (;;) {
        config_store_get(&config);
        eligible=wifi_ap_policy_can_close(&config,wifi_manager_station_ready(),
                                         station_matches_saved(config.wifi_ssid),recovery_ota_busy());
        lock_status(); active=recovery_ap_active; unlock_status();
        if (!eligible) {
            eligible_since=0;
            /* During Station OTA keep a working Station connection untouched.
             * If it has already failed, restore Recovery even while OTA is busy. */
            if (!active && (!recovery_ota_busy() || !wifi_manager_station_ready())) {
                err=recovery_ap_start();
                if (err==kNoErr) {
                    lock_status(); recovery_ap_active=1; unlock_status();
                    printf("WIFI: Recovery AP restored after STA loss or policy change\r\n");
                } else printf("WIFI: Recovery AP restore failed: %d\r\n",err);
            }
        } else if (active) {
            uint32_t now=mico_rtos_get_time();
            if (!eligible_since) {
                eligible_since=now?now:1;
                printf("WIFI: STA ready, Recovery AP will close in 3 seconds\r\n");
            } else if (now-eligible_since>=WIFI_AP_CLOSE_GRACE_MS) {
                config_store_get(&config);
                if (wifi_ap_policy_can_close(&config,wifi_manager_station_ready(),
                                             station_matches_saved(config.wifi_ssid),
                                             recovery_ota_busy())) {
                    mico_rtos_lock_mutex(&wlan_control_mutex);
                    err=micoWlanSuspendSoftAP();
                    mico_rtos_unlock_mutex(&wlan_control_mutex);
                    if (err==kNoErr) {
                        lock_status(); recovery_ap_active=0; unlock_status();
                        printf("WIFI: Recovery AP stopped after STA became ready\r\n");
                    } else printf("WIFI: Recovery AP stop failed: %d\r\n",err);
                }
                eligible_since=0;
            }
        }
        mico_thread_msleep(1000);
    }
}

OSStatus wifi_manager_apply_boot_settings(void)
{
    openm1_config_t config;
    OSStatus err;
    if (!manager_ready || !station_supervisor_ready) return kNotPreparedErr;
    err=mico_rtos_create_thread(&ap_policy_thread,MICO_APPLICATION_PRIORITY,
                               "openm1_ap_policy",ap_policy_worker,WIFI_AP_POLICY_STACK,0);
    ap_policy_worker_ready=(err==kNoErr);
    if (err!=kNoErr) {
        printf("WIFI: AP policy worker unavailable: %d; Recovery AP stays on\r\n",err);
    }
    config_store_get(&config);
    if (config.wifi_auto_connect && config.wifi_ssid[0]) {
        printf("WIFI: auto-connect starting\r\nWIFI: auto-connect SSID = %s\r\n",config.wifi_ssid);
        lock_status();
        wifi_station_desire(&desired_station,config.wifi_ssid,config.wifi_password,WIFI_DESIRED_AUTO);
        desired_revision++;
        unlock_status();
        printf("WIFI: station connection desired: %s\r\n",config.wifi_ssid);
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
    int recovery_ap,scan_supported;
    uint32_t link_uptime=0,backoff_remaining=0;
    openm1_config_t config;
    lock_status();
    snapshot=status;
    desired=desired_station;
    recovery_ap=recovery_ap_active;
    scan_supported=scan_registered;
    unlock_status();
    memset(desired.password,0,sizeof(desired.password));
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
      "\"supervisor_running\":%s,\"want_connected\":%s,\"desired_source\":\"%s\",\"manual_disconnect_latched\":%s,\"link_cached\":%s,\"ip_valid_cached\":%s,\"disconnect_count\":%lu,\"reconnect_attempts\":%lu,\"reconnect_successes\":%lu,\"last_disconnect_ms\":%lu,\"last_reconnect_attempt_ms\":%lu,\"last_reconnect_success_ms\":%lu,\"current_backoff_ms\":%lu,\"link_uptime_ms\":%lu,\"consecutive_bad_samples\":%lu,\"consecutive_good_samples\":%lu,\"last_wlan_error\":%d}",
      recovery_ap?"true":"false",recovery_ap?"on":"off",recovery_ssid(),recovery_mac(),snapshot.state,ssid,snapshot.ip,snapshot.gateway,
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
      (unsigned long)snapshot.consecutive_good_samples,snapshot.last_wlan_error);
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
    micoWlanStartScanAdv();
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
