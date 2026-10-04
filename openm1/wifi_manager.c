#include "wifi_manager.h"
#include "recovery.h"
#include <stdio.h>
#include <string.h>

/* Recovery is started before this module. No saved credentials are read at boot. */
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
    int cancel;
} wifi_status_t;

static wifi_status_t status = { .state = "disconnected" };
static network_InitTypeDef_st sta_config;
static mico_mutex_t status_mutex;
static mico_thread_t sta_thread;
static int manager_ready;
typedef struct { char ssid[33]; int rssi; unsigned channel; wlan_sec_type_t security; } scan_ap_t;
static scan_ap_t scan_aps[WIFI_SCAN_MAX_AP];
static unsigned scan_count;
static char scan_state[12]="idle";
static uint32_t scan_started_ms;
static char scan_json[3300];
static int scan_registered;
static void lock_status(void);
static void unlock_status(void);

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
    status.rssi=0;
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
static void set_failure(const char *message)
{
    lock_status();
    snprintf(status.state,sizeof(status.state),"failed");
    snprintf(status.message,sizeof(status.message),"%s",message);
    clear_network();
    memset(&sta_config,0,sizeof(sta_config));
    status.active=0;
    unlock_status();
    printf("WIFI: STA connect failed\r\n");
}
static void wifi_sta_worker(mico_thread_arg_t arg)
{
    LinkStatusTypeDef link;
    IPStatusTypedef ip;
    OSStatus err;
    unsigned elapsed;
    int cancelled;
    (void)arg;
    lock_status();
    cancelled=status.cancel;
    if (!cancelled) {
        err=StartNetwork(&sta_config);
        printf("WIFI: StartNetwork result = %d\r\n",err);
    } else err=kGeneralErr;
    unlock_status();
    if (cancelled) goto cancelled_exit;
    if (err!=kNoErr) {
        micoWlanSuspendStation();
        lock_status(); cancelled=status.cancel; unlock_status();
        if (cancelled) goto cancelled_exit;
        set_failure("连接失败，请检查 Wi-Fi 名称、密码及信号。");
        goto exit;
    }
    printf("WIFI: waiting for link\r\n");
    for (elapsed=0;elapsed<WIFI_STA_CONNECT_TIMEOUT_MS;elapsed+=500) {
        mico_thread_msleep(500);
        lock_status(); cancelled=status.cancel; unlock_status();
        if (cancelled) goto cancelled_exit;
        memset(&link,0,sizeof(link)); memset(&ip,0,sizeof(ip));
        ip.ip[sizeof(ip.ip)-1]=0;
        if (micoWlanGetLinkStatus(&link)==kNoErr && link.is_connected==1 &&
            micoWlanGetIPStatus(&ip,Station)==kNoErr) {
            ip.ip[sizeof(ip.ip)-1]=0;
            if (!valid_ip(ip.ip)) continue;
            lock_status();
            if (status.cancel) { unlock_status(); goto cancelled_exit; }
            snprintf(status.state,sizeof(status.state),"connected");
            copy_ip(status.ip,ip.ip); copy_ip(status.gateway,ip.gate);
            copy_ip(status.netmask,ip.mask); copy_ip(status.dns,ip.dns);
            status.rssi=link.rssi;
            status.message[0]=0; status.active=0;
            unlock_status();
            printf("WIFI: link connected\r\nWIFI: DHCP IP = %s\r\nWIFI: RSSI = %d\r\n",ip.ip,link.rssi);
            goto exit;
        }
    }
    printf("WIFI: STA connect timeout\r\n");
    micoWlanSuspendStation();
    lock_status(); cancelled=status.cancel; unlock_status();
    if (cancelled) goto cancelled_exit;
    set_failure("连接超时，请检查 Wi-Fi 名称和密码。");
    goto exit;
cancelled_exit:
    lock_status();
    snprintf(status.state,sizeof(status.state),"disconnected");
    status.message[0]=0; status.active=0; status.cancel=0;
    clear_network();
    memset(&sta_config,0,sizeof(sta_config));
    unlock_status();
exit:
    mico_rtos_delete_thread(NULL);
}
OSStatus wifi_manager_init(void)
{
    OSStatus err=mico_rtos_init_mutex(&status_mutex);
    if (err!=kNoErr) return err;
    manager_ready=1;
    err=mico_system_notify_register(mico_notify_WIFI_SCAN_ADV_COMPLETED,(void *)scan_complete,NULL);
    scan_registered=(err==kNoErr);
    if (!scan_registered) printf("WIFI: scan callback registration failed = %d\r\n",err);
    printf("WIFI: manager initialized\r\n");
    return kNoErr;
}
int wifi_manager_connect(const char *ssid, const char *password)
{
    OSStatus err;
    size_t pass_len;
    if (!manager_ready || !WIFI_STA_CONNECT_SUPPORTED) return -3;
    if (!valid_input(ssid,sizeof(sta_config.wifi_ssid)-1) || !password) return -1;
    pass_len=strlen(password);
    if (pass_len>=sizeof(sta_config.wifi_key)) return -1;
    /* An empty key selects an open network; secured networks use a passphrase. */
    if (pass_len && !valid_input(password,sizeof(sta_config.wifi_key)-1)) return -1;
    lock_status();
    if (status.active) { unlock_status(); return -2; }
    if (!strcmp(status.state,"connected")) { unlock_status(); return -4; }
    status.active=1; status.cancel=0;
    snprintf(status.state,sizeof(status.state),"connecting");
    snprintf(status.ssid,sizeof(status.ssid),"%s",ssid);
    snprintf(status.message,sizeof(status.message),"正在连接……");
    clear_network();
    memset(&sta_config,0,sizeof(sta_config));
    sta_config.wifi_mode=Station;
    memcpy(sta_config.wifi_ssid,ssid,strlen(ssid)+1);
    memcpy(sta_config.wifi_key,password,pass_len+1);
    sta_config.dhcpMode=DHCP_Client;
    unlock_status();
    printf("WIFI: STA connect requested\r\nWIFI: SSID = %s\r\n",ssid);
    err=mico_rtos_create_thread(&sta_thread,MICO_APPLICATION_PRIORITY,"openm1_sta",
                               wifi_sta_worker,WIFI_STA_WORKER_STACK,0);
    if (err!=kNoErr) { set_failure("无法启动 Wi-Fi 连接线程。"); return -3; }
    return 0;
}
int wifi_manager_disconnect(void)
{
    OSStatus err;
    if (!manager_ready) return -1;
    lock_status(); status.cancel=1; unlock_status();
    err=micoWlanSuspendStation(); /* Never call micoWlanSuspend(): it also stops SoftAP. */
    lock_status();
    snprintf(status.state,sizeof(status.state),"disconnected");
    status.ssid[0]=0; status.message[0]=0;
    clear_network();
    if (!status.active) { status.cancel=0; memset(&sta_config,0,sizeof(sta_config)); }
    unlock_status();
    printf("WIFI: STA disconnected\r\n");
    return err==kNoErr?0:-1;
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
    IPStatusTypedef ap;
    LinkStatusTypeDef link;
    IPStatusTypedef ip;
    char ssid[66],message[194];
    int recovery_ap=0, linked=0;
    memset(&ap,0,sizeof(ap));
    if (micoWlanGetIPStatus(&ap,Soft_AP)==kNoErr) {
        ap.ip[sizeof(ap.ip)-1]=0;
        if (!strcmp(ap.ip,RECOVERY_IP)) recovery_ap=1;
    }
    lock_status();
    if (!status.active) {
        memset(&link,0,sizeof(link)); memset(&ip,0,sizeof(ip));
        if (micoWlanGetLinkStatus(&link)==kNoErr && link.is_connected==1 &&
            micoWlanGetIPStatus(&ip,Station)==kNoErr) {
            ip.ip[sizeof(ip.ip)-1]=0;
            if (valid_ip(ip.ip)) {
                linked=1;
                snprintf(status.state,sizeof(status.state),"connected");
                copy_ip(status.ip,ip.ip); copy_ip(status.gateway,ip.gate);
                copy_ip(status.netmask,ip.mask); copy_ip(status.dns,ip.dns);
                status.rssi=link.rssi;
            }
        }
        if (!linked && !strcmp(status.state,"connected")) {
            snprintf(status.state,sizeof(status.state),"disconnected");
            clear_network();
        }
    }
    snapshot=status;
    unlock_status();
    json_string(ssid,sizeof(ssid),snapshot.ssid);
    json_string(message,sizeof(message),snapshot.message);
    snprintf(out,out_size,
      "{\"recovery_ap\":%s,\"recovery_ssid\":\"%s\",\"mac\":\"%s\",\"sta_state\":\"%s\",\"ssid\":\"%s\",\"ip\":\"%s\",\"gateway\":\"%s\",\"netmask\":\"%s\",\"dns\":\"%s\",\"rssi\":%d,\"message\":\"%s\",\"sta_connect_supported\":true,\"scan_supported\":%s}",
      recovery_ap?"true":"false",recovery_ssid(),recovery_mac(),snapshot.state,ssid,snapshot.ip,snapshot.gateway,
      snapshot.netmask,snapshot.dns,snapshot.rssi,message,scan_registered?"true":"false");
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
    if (!strcmp(scan_state,"scanning")) { unlock_status(); return -1; }
    strcpy(scan_state,"scanning");
    scan_count=0;
    scan_started_ms=mico_rtos_get_time();
    unlock_status();
    micoWlanStartScanAdv();
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
    LinkStatusTypeDef link;
    IPStatusTypedef ip;
    memset(&link,0,sizeof(link)); memset(&ip,0,sizeof(ip));
    if (micoWlanGetLinkStatus(&link)!=kNoErr || link.is_connected!=1 ||
        micoWlanGetIPStatus(&ip,Station)!=kNoErr) return 0;
    ip.ip[sizeof(ip.ip)-1]=0;
    return valid_ip(ip.ip);
}
int wifi_manager_station_rssi(void)
{
    LinkStatusTypeDef link;
    memset(&link,0,sizeof(link));
    return micoWlanGetLinkStatus(&link)==kNoErr && link.is_connected?link.rssi:0;
}
void wifi_manager_station_ip(char out[16])
{
    IPStatusTypedef ip;
    memset(&ip,0,sizeof(ip)); out[0]=0;
    if (micoWlanGetIPStatus(&ip,Station)==kNoErr) {
        ip.ip[sizeof(ip.ip)-1]=0;
        if (valid_ip(ip.ip)) copy_ip(out,ip.ip);
    }
}
