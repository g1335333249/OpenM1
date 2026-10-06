#include "mico.h"
#include "mico_wlan.h"
#include "recovery.h"
#include "wifi_manager.h"
#include "m1_uart.h"
#include "config_store.h"
#include "mqtt_manager.h"
#include "m1_display.h"
#include "network_health.h"
#include "system_stats.h"

static int boot_free_heap(void)
{
    micoMemInfo_t *memory=MicoGetMemoryInfo();
    return memory?memory->free_memory:-1;
}
static void boot_heap_log(const char *phase)
{
    system_stats_note_heap(1);
    printf("BOOT: free heap %s = %d\r\n",phase,boot_free_heap());
}

static void boot_log_init_error(const char *module,OSStatus error)
{
    if (error!=kNoErr)
        printf("BOOT: %s initialization failed: %d, free heap = %d\r\n",
               module,error,boot_free_heap());
}

int main(void)
{
    network_InitTypeDef_st wifi_config;
    mico_Context_t *context;
    char rf_version[64] = {0};
    uint8_t mac[6] = {0};
    char recovery_ssid[32] = RECOVERY_FALLBACK_SSID;
    char mac_text[18] = {0};
    OSStatus result,ap_result,http_result,wifi_result;
    unsigned attempt;
    static const unsigned retry_delay_ms[] = {500u,1000u};
    unsigned long counter = 0;
    micoMemInfo_t *memory;
    int network_health_started=0;

    printf("================================\r\n"
           "OpenM1\r\n"
           "Version: 0.6.3\r\n"
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
    printf("RECOVERY: MicoInit result = %d\r\n", result);
    result=system_stats_init(); /* Mutexes and heap counters only; no CPU thread. */
    if (result!=kNoErr) system_stats_enter_safe_mode();
    boot_log_init_error("system stats",result);
    result=system_stats_register_stack_diagnostic();
    boot_log_init_error("stack diagnostic",result);
    mico_thread_msleep(500);
    MicoGetRfVer(rf_version, sizeof(rf_version));
    rf_version[sizeof(rf_version) - 1] = '\0';
    recovery_set_rf(rf_version);
    printf("RECOVERY: RF = %s\r\n", recovery_rf());
    printf("RECOVERY: calling micoWlanPowerOn\r\n");
    result = micoWlanPowerOn();
    printf("RECOVERY: micoWlanPowerOn result = %d\r\n", result);
    mico_thread_msleep(500);

    /* The MOC wrapper returns void. Reject clearly invalid/uninitialized MACs. */
    mico_wlan_get_mac_address(mac);
    if (memcmp(mac, "\0\0\0\0\0\0", sizeof(mac)) != 0 &&
        memcmp(mac, "\xff\xff\xff\xff\xff\xff", sizeof(mac)) != 0 &&
        (mac[0] & 1u) == 0) {
        snprintf(recovery_ssid, sizeof(recovery_ssid), "OpenM1-%02X%02X%02X",
                 mac[3], mac[4], mac[5]);
        snprintf(mac_text, sizeof(mac_text), "%02X:%02X:%02X:%02X:%02X:%02X",
                 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        printf("RECOVERY: MAC = %s\r\n", mac_text);
    } else {
        printf("RECOVERY: MAC read failed, using fallback SSID\r\n");
    }
    recovery_set_identity(recovery_ssid, mac_text);
    printf("RECOVERY: SSID = %s\r\n", recovery_ssid);

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
        printf("RECOVERY: SoftAP start attempt %u\r\n",attempt+1);
        printf("RECOVERY: calling StartNetwork(SoftAP)\r\n");
        ap_result=StartNetwork(&wifi_config);
        printf("RECOVERY: StartNetwork(SoftAP) result = %d\r\n",ap_result);
        if (ap_result==kNoErr) break;
    }
    if (ap_result==kNoErr) {
        printf("RECOVERY: SoftAP ready\r\n");
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
            printf("RECOVERY: HTTP listener not ready yet; continuing boot\r\n");
    }
    printf("BOOT: phase 1 recovery ready\r\n");
    boot_heap_log("after HTTP");
    boot_log_init_error("config",config_store_init());
    wifi_result=wifi_manager_init();
    boot_log_init_error("Wi-Fi manager",wifi_result);
    system_stats_set_boot_phase("core");
    boot_log_init_error("display",m1_display_init());
    mico_thread_msleep(250);
    boot_heap_log("after display");
    boot_log_init_error("UART",m1_uart_init());
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
        printf("WIFI: control worker unavailable; entering Recovery safe mode\r\n");
    } else {
        system_stats_set_boot_phase("wifi");
        printf("BOOT: Wi-Fi control started\r\n");
        mico_thread_msleep(500);
        if (boot_free_heap()<(int)OPENM1_MIN_HEAP_RESERVE) {
            system_stats_enter_safe_mode();
            printf("BOOT: low memory safe mode; optional workers deferred\r\n");
        }
    }
    boot_heap_log("before MQTT");
    if (!system_stats_low_memory_safe_mode())
        boot_log_init_error("MQTT",mqtt_manager_init());
    boot_heap_log("after MQTT");
    printf("BOOT: optional managers initialized; workers remain deferred\r\n");
    printf("BOOT: subsystem initialization complete, free heap = %d\r\n",boot_free_heap());
    for (;;) {
        mico_thread_msleep(10000);
        system_stats_note_heap(0);
        if (!network_health_started && wifi_manager_control_running() &&
            wifi_manager_station_ready() && !system_stats_low_memory_safe_mode() &&
            system_stats_begin_optional_thread(NETWORK_HEALTH_WORKER_STACK)) {
            network_health_started=1;
            boot_log_init_error("network health",network_health_init());
            system_stats_end_thread_creation();
            boot_heap_log("after network health");
        }
        if (mico_rtos_get_time()>=30000u && wifi_manager_control_running() &&
            !system_stats_stack_overflow_count() && !system_stats_low_memory_safe_mode()) {
            mqtt_manager_maybe_start(0);
            mico_thread_msleep(300);
            system_stats_maybe_start_cpu();
        }
        memory = MicoGetMemoryInfo();
        printf("RECOVERY: alive %lu, free heap = %d\r\n", ++counter,
               memory ? memory->free_memory : -1);
    }
}
