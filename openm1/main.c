#include "mico.h"
#include "mico_wlan.h"
#include "recovery.h"
#include "wifi_manager.h"
#include "wifi_station_logic.h"
#include "m1_uart.h"
#include "config_store.h"
#include "mqtt_manager.h"
#include "m1_display.h"
#include "network_health.h"
#include "system_stats.h"
#include "openm1_log.h"
#include "worker_retry_logic.h"
#include "button_manager.h"

/* Implemented by the pinned SDK's MiCO/net/mocIP/mico/mico_socket.c. */
extern char *sethostname(char *name);

/* Overrides MiCO/core/mico_config.c's weak 1500-byte runtime default.
 * The separate MOC user header already declares a 4096-byte app stack. */
uint32_t app_stack_size = 4096u;

static int boot_free_heap(void)
{
    micoMemInfo_t *memory=MicoGetMemoryInfo();
    return memory?memory->free_memory:-1;
}
static void boot_heap_log(const char *phase)
{
    system_stats_note_heap(1);
    openm1_log_info("BOOT","free heap %s = %d",phase,boot_free_heap());
}

static void boot_log_init_error(const char *module,OSStatus error)
{
    if (error!=kNoErr)
        openm1_log_error("BOOT","%s init failed: %d, heap=%d",module,error,boot_free_heap());
}

#define OPENM1_HOUSEKEEPING_STACK 3072u
static mico_thread_t housekeeping_thread;
static void housekeeping_worker(mico_thread_arg_t arg)
{
    unsigned long counter=0;
    int network_health_started=0,network_health_failed_once=0;
    uint32_t network_health_failed_at=0,health_retry_remaining;
    micoMemInfo_t *memory;
    uint32_t last_overflow_count=0;
    uint32_t now,last_heartbeat_ms=mico_rtos_get_time(),fault_count,quiet_remaining;
    char fault_task[OPENM1_STACK_TASK_NAME_MAX];
    (void)arg;
    for (;;) {
        mico_thread_msleep(1000);
        now=mico_rtos_get_time();
        system_stats_note_heap(0);
        button_manager_tick();
        fault_count=system_stats_stack_overflow_count();
        quiet_remaining=system_stats_stack_fault_quiet_remaining_ms();
        if (fault_count!=last_overflow_count) {
            uint32_t delta=fault_count-last_overflow_count;
            last_overflow_count=fault_count;
            system_stats_last_stack_overflow_task(fault_task,sizeof(fault_task));
            openm1_log_error("SYSTEM","stack overflow task=%s count=%lu delta=%lu",
                             fault_task,(unsigned long)fault_count,(unsigned long)delta);
        }
        health_retry_remaining=network_health_retry_remaining(now,network_health_failed_at,network_health_failed_once);
        if (!network_health_started) {
            const char *reason="none";
            if (!wifi_manager_control_running() || !wifi_manager_station_ready()) reason="waiting_station";
            else if (recovery_ota_busy()) reason="ota_busy";
            else if (quiet_remaining) reason="stack_fault_cooldown";
            else if (system_stats_low_memory_safe_mode()) reason="low_memory";
            else if (health_retry_remaining) reason="worker_create_failed";
            else if (!system_stats_begin_optional_thread(NETWORK_HEALTH_WORKER_STACK)) reason="heap_reserve";
            else {
                OSStatus health_result;
                network_health_set_start_diagnostic("none",0,1);
                health_result=network_health_init();
                network_health_started=health_result==kNoErr;
                if (!network_health_started) {
                    network_health_failed_once=1;
                    network_health_failed_at=mico_rtos_get_time();
                    reason="worker_create_failed";
                    health_retry_remaining=NETWORK_HEALTH_RETRY_MS;
                }
                system_stats_end_thread_creation();
                boot_log_init_error("network health",health_result);
                boot_heap_log("after network health");
                network_health_set_start_diagnostic(reason,health_retry_remaining,0);
                continue; /* Stagger optional thread creation. */
            }
            network_health_set_start_diagnostic(reason,health_retry_remaining,0);
        } else {
            network_health_set_start_diagnostic("none",0,0);
        }
        if (now>=WIFI_BOOT_AUTO_CONNECT_GRACE_MS && wifi_manager_control_running() &&
            !quiet_remaining && !system_stats_low_memory_safe_mode() && !recovery_ota_busy() &&
            (!wifi_manager_station_ready() || network_health_started || network_health_failed_once)) {
            int mqtt_was_ready_for_cpu=mqtt_manager_ready_for_cpu();
            mqtt_manager_maybe_start(0);
            if (system_stats_stack_fault_cpu_ready() && mqtt_was_ready_for_cpu)
                system_stats_maybe_start_cpu();
        }
        if ((uint32_t)(now-last_heartbeat_ms)>=10000u) {
            last_heartbeat_ms=now;
            memory=MicoGetMemoryInfo();
            printf("RECOVERY: alive %lu, free heap = %d\r\n",++counter,
                   memory?memory->free_memory:-1);
        }
    }
}

int main(void)
{
    static network_InitTypeDef_st wifi_config;
    mico_Context_t *context;
    char rf_version[64] = {0};
    uint8_t mac[6] = {0};
    char recovery_ssid[32] = RECOVERY_FALLBACK_SSID;
    char mac_text[18] = {0};
    OSStatus result,ap_result,http_result,wifi_result;
    unsigned attempt;
    int mac_valid;
    static const unsigned retry_delay_ms[] = {500u,1000u};

    result=openm1_log_init();
    if (result!=kNoErr) printf("BOOT: RAM logger unavailable: %d\r\n",result);
    openm1_log_info("BOOT","app_thread stack configured = %lu",(unsigned long)app_stack_size);
    openm1_log_info("BOOT","OpenM1 v0.6.13");

    printf("================================\r\n"
           "OpenM1\r\n"
           "Version: 0.6.13\r\n"
           "Board: MK3080B\r\n"
           "Kernel: 3080B002.023\r\n"
           "================================\r\n");
    printf("RECOVERY: main entered\r\n");
    printf("RECOVERY: system context init\r\n");
    context = mico_system_context_init(sizeof(openm1_config_t));
    if (context == NULL) {
        printf("RECOVERY: system context failed\r\n");
        return -1;
    }
    printf("RECOVERY: system context initialized\r\n");

    /* Preserve the v0.0.3 hardware verified Wi-Fi sequence and delays. */
    printf("RECOVERY: MicoInit\r\n");
    result = MicoInit();
    openm1_log_info("BOOT","MicoInit result = %d", result);
    result=system_stats_init(); /* Mutexes and heap counters only; no CPU thread. */
    if (result!=kNoErr) system_stats_enter_safe_mode();
    boot_log_init_error("system stats",result);
    result=system_stats_register_stack_diagnostic();
    boot_log_init_error("stack diagnostic",result);
    mico_thread_msleep(500);
    MicoGetRfVer(rf_version, sizeof(rf_version));
    rf_version[sizeof(rf_version) - 1] = '\0';
    recovery_set_rf(rf_version);
    openm1_log_info("BOOT","RF = %s",recovery_rf());
    printf("RECOVERY: calling micoWlanPowerOn\r\n");
    result = micoWlanPowerOn();
    openm1_log_info("BOOT","WlanPowerOn result = %d",result);
    mico_thread_msleep(500);

    /* The MOC wrapper returns void. Reject clearly invalid/uninitialized MACs. */
    mico_wlan_get_mac_address(mac);
    mac_valid=memcmp(mac, "\0\0\0\0\0\0", sizeof(mac)) != 0 &&
        memcmp(mac, "\xff\xff\xff\xff\xff\xff", sizeof(mac)) != 0 &&
        (mac[0] & 1u) == 0;
    if (mac_valid) {
        snprintf(recovery_ssid, sizeof(recovery_ssid), "OpenM1-%02X%02X%02X",
                 mac[3], mac[4], mac[5]);
        snprintf(mac_text, sizeof(mac_text), "%02X:%02X:%02X:%02X:%02X:%02X",
                 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        openm1_log_info("BOOT","MAC = %s",mac_text);
    } else {
        printf("RECOVERY: MAC read failed, using fallback SSID\r\n");
    }
    recovery_set_identity(recovery_ssid, mac_text);
    recovery_prepare_hostname(mac,mac_valid);
    if (sethostname(recovery_hostname()))
        openm1_log_info("BOOT","DHCP hostname = %s",recovery_hostname());
    else
        printf("DEVICE: DHCP hostname setup unavailable\r\n");
    openm1_log_info("RECOVERY","SSID = %s",recovery_ssid);

    printf("RECOVERY: starting SoftAP\r\n");
    memset(&wifi_config, 0, sizeof(wifi_config));
    wifi_config.wifi_mode = Soft_AP;
    snprintf(wifi_config.wifi_ssid, sizeof(wifi_config.wifi_ssid), "%s", recovery_ssid);
    memcpy(wifi_config.local_ip_addr, RECOVERY_IP, sizeof(RECOVERY_IP));
    memcpy(wifi_config.net_mask, "255.255.255.0", sizeof("255.255.255.0"));
    memcpy(wifi_config.gateway_ip_addr, RECOVERY_IP, sizeof(RECOVERY_IP));
    memcpy(wifi_config.dnsServer_ip_addr, RECOVERY_IP, sizeof(RECOVERY_IP));
    wifi_config.dhcpMode = DHCP_Server;
    printf("RECOVERY: SoftAP SSID = %s\r\n", recovery_ssid);
    ap_result=kGeneralErr;
    for (attempt=0;attempt<3;attempt++) {
        if (attempt) mico_thread_msleep(retry_delay_ms[attempt-1]);
        openm1_log_info("RECOVERY","SoftAP start attempt %u",attempt+1);
        printf("RECOVERY: calling StartNetwork(SoftAP)\r\n");
        ap_result=StartNetwork(&wifi_config);
        openm1_log_info("RECOVERY","SoftAP result = %d",ap_result);
        if (ap_result==kNoErr) break;
    }
    if (ap_result==kNoErr) {
        openm1_log_info("RECOVERY","SoftAP ready");
        printf("RECOVERY: WiFi ready\r\n");
        printf("RECOVERY: IP = %s\r\n", RECOVERY_IP);
    } else {
        printf("RECOVERY: initial SoftAP unavailable; AP policy will retry\r\n");
    }
    recovery_ota_partition_log();
    http_result=recovery_http_start();
    boot_log_init_error("HTTP",http_result);
    if (http_result==kNoErr) {
        unsigned wait_count;
        for (wait_count=0;wait_count<30 && !recovery_http_ready();wait_count++)
            mico_thread_msleep(100);
        if (!recovery_http_ready())
            openm1_log_warn("RECOVERY","HTTP listener not ready; boot continues");
        else openm1_log_info("RECOVERY","HTTP ready");
    }
    printf("BOOT: phase 1 recovery ready\r\n");
    boot_heap_log("after HTTP");
    result=config_store_init();
    boot_log_init_error("config",result);
    openm1_log_info("BOOT","config init result = %d",result);
    result=button_manager_init();
    boot_log_init_error("button manager",result);
    wifi_result=wifi_manager_init();
    boot_log_init_error("Wi-Fi manager",wifi_result);
    openm1_log_info("BOOT","Wi-Fi manager init result = %d",wifi_result);
    if (wifi_result==kNoErr)
        wifi_manager_set_initial_ap_state(ap_result==kNoErr);
    system_stats_set_boot_phase("core");
    result=m1_display_init();
    boot_log_init_error("display",result);
    openm1_log_info("BOOT","display init result = %d",result);
    mico_thread_msleep(250);
    boot_heap_log("after display");
    result=m1_uart_init();
    boot_log_init_error("UART",result);
    openm1_log_info("BOOT","UART init result = %d",result);
    mico_thread_msleep(250);
    boot_heap_log("after UART");
    printf("BOOT: core services initialized\r\n");
    boot_heap_log("before Wi-Fi control");
    boot_log_init_error("Wi-Fi boot policy",wifi_manager_apply_boot_settings());
    {
        unsigned tries;
        for (tries=0;tries<10 && !wifi_manager_control_running();tries++)
            mico_thread_msleep(100);
    }
    boot_heap_log("after Wi-Fi control");
    if (!wifi_manager_control_running()) {
        system_stats_enter_safe_mode();
        openm1_log_error("WIFI","control worker unavailable; entering Recovery safe mode");
    } else {
        system_stats_set_boot_phase("wifi");
        openm1_log_info("WIFI","control worker ready");
        mico_thread_msleep(500);
        if (boot_free_heap()<(int)OPENM1_MIN_HEAP_RESERVE) {
            system_stats_enter_safe_mode();
            openm1_log_warn("BOOT","low memory safe mode");
        }
    }
    boot_heap_log("before MQTT");
    if (!system_stats_low_memory_safe_mode())
        boot_log_init_error("MQTT",mqtt_manager_init());
    boot_heap_log("after MQTT");
    printf("BOOT: optional managers initialized; workers remain deferred\r\n");
    printf("BOOT: subsystem initialization complete, free heap = %d\r\n",boot_free_heap());
    result=mico_rtos_create_thread(&housekeeping_thread,MICO_APPLICATION_PRIORITY,
                                   "openm1_housekeeping",housekeeping_worker,
                                   OPENM1_HOUSEKEEPING_STACK,0);
    boot_log_init_error("housekeeping",result);
    openm1_log_info("BOOT","main release, free heap=%d",boot_free_heap());
    return 0;
}
