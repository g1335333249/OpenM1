#include "button_manager.h"
#include "openm1_log.h"
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static uint32_t now_ms;
static int ota_busy,save_calls,reboot_calls,save_result;
OSStatus mico_rtos_init_mutex(mico_mutex_t *m) {*m=1;return kNoErr;}
OSStatus mico_rtos_lock_mutex(mico_mutex_t *m) {assert(*m);return kNoErr;}
OSStatus mico_rtos_unlock_mutex(mico_mutex_t *m) {assert(*m);return kNoErr;}
uint32_t mico_rtos_get_time(void) {return now_ms;}
int recovery_ota_busy(void) {return ota_busy;}
OSStatus config_store_factory_reset(void) {save_calls++;return save_result;}
void MicoSystemReboot(void) {reboot_calls++;}
void openm1_log_write(openm1_log_level_t level,const char *module,const char *fmt,...)
{(void)level;(void)module;(void)fmt;}
static void expect_state(const char *state)
{
    char json[512],needle[80];
    button_manager_status_json(json,sizeof(json));
    snprintf(needle,sizeof(needle),"\"factory_reset_state\":\"%s\"",state);
    assert(strstr(json,needle));
}
int main(void)
{
    char json[512];
    assert(button_manager_init()==kNoErr);
    button_manager_note_short_press();
    expect_state("idle");
    now_ms=1000;button_manager_note_long_press();
    expect_state("waiting_confirmation");
    now_ms=2000;button_manager_note_long_press();
    now_ms=3000;button_manager_note_long_press();
    button_manager_tick();expect_state("waiting_confirmation");
    now_ms=16001;button_manager_tick();expect_state("idle");
    /* One long-press frame during a six-second hold cannot confirm. */
    now_ms=20000;button_manager_note_long_press();
    now_ms=26000;button_manager_tick();expect_state("waiting_confirmation");
    now_ms=36000;button_manager_tick();expect_state("idle");
    now_ms=40000;button_manager_note_long_press();
    now_ms=44000;button_manager_note_long_press();expect_state("reset_requested");
    button_manager_tick();
#if OPENM1_FACTORY_RESET_DRY_RUN
    expect_state("reset_completed");
    assert(save_calls==0 && reboot_calls==0);
    button_manager_status_json(json,sizeof(json));
    assert(strstr(json,"\"dry_run_confirmations\":1"));
    assert(strstr(json,"\"factory_reset_attempts\":0"));
#else
    expect_state("reset_completed");
    assert(save_calls==1 && reboot_calls==1);
#endif
    /* OTA cancels an existing confirmation, including before persistence. */
    now_ms=50000;button_manager_note_long_press();expect_state("waiting_confirmation");
    ota_busy=1;button_manager_tick();expect_state("idle");
    now_ms=55000;button_manager_note_long_press();expect_state("idle");
    ota_busy=0;
    now_ms=60000;button_manager_note_long_press();
    now_ms=64000;button_manager_note_long_press();expect_state("reset_requested");
    ota_busy=1;button_manager_tick();expect_state("idle");
    ota_busy=0;
    /* Difference is unsigned and must survive a uint32 time wrap. */
    now_ms=UINT32_MAX-1000u;button_manager_note_long_press();
    now_ms=3500;button_manager_note_long_press();expect_state("reset_requested");
#if !OPENM1_FACTORY_RESET_DRY_RUN
    save_result=-5;
#endif
    button_manager_tick();
#if OPENM1_FACTORY_RESET_DRY_RUN
    expect_state("reset_completed");assert(save_calls==0 && reboot_calls==0);
#else
    expect_state("reset_failed");assert(save_calls==2 && reboot_calls==1);
    button_manager_status_json(json,sizeof(json));
    assert(strstr(json,"\"last_reset_error\":\"config_save_failed\""));
#endif
    puts("BUTTON_MANAGER_PASS: debounce, 15s expiry, dry-run/failed-save, OTA, wraparound");
    return 0;
}
