#include "button_manager.h"
#include "config_store.h"
#include "openm1_log.h"
#include "recovery.h"
#include <stdio.h>

static mico_mutex_t button_mutex;
static int button_ready;
static button_reset_state_t reset_state;
static uint32_t first_long_ms,last_long_ms;
static uint32_t short_press_frames,button_long_press_frames,invalid_long_press_frames;
static uint32_t factory_reset_attempts,factory_reset_successes,dry_run_confirmations;
static int last_reset_error;

static const char *state_name(button_reset_state_t state)
{
    switch (state) {
    case BUTTON_RESET_WAITING_CONFIRMATION: return "waiting_confirmation";
    case BUTTON_RESET_REQUESTED: return "reset_requested";
    case BUTTON_RESET_IN_PROGRESS: return "reset_in_progress";
    case BUTTON_RESET_COMPLETED: return "reset_completed";
    case BUTTON_RESET_FAILED: return "reset_failed";
    default: return "idle";
    }
}

OSStatus button_manager_init(void)
{
    OSStatus err=mico_rtos_init_mutex(&button_mutex);
    if (err==kNoErr) button_ready=1;
    return err;
}

void button_manager_note_short_press(void)
{
    if (!button_ready) return;
    mico_rtos_lock_mutex(&button_mutex);
    short_press_frames++;
    mico_rtos_unlock_mutex(&button_mutex);
}

void button_manager_note_invalid_long_frame(void)
{
    if (button_ready) {
        mico_rtos_lock_mutex(&button_mutex);
        invalid_long_press_frames++;
        mico_rtos_unlock_mutex(&button_mutex);
    }
    openm1_log_warn("BUTTON","type 0x04 frame has unexpected payload");
}

void button_manager_note_long_press(void)
{
    uint32_t now=mico_rtos_get_time(),elapsed=0;
    int action=0;
    if (!button_ready) return;
    mico_rtos_lock_mutex(&button_mutex);
    button_long_press_frames++;
    last_long_ms=now;
    mico_rtos_unlock_mutex(&button_mutex);
    /* Never hold button_mutex while consulting OTA state or writing a log. */
    if (recovery_ota_busy()) {
        mico_rtos_lock_mutex(&button_mutex);
        if (reset_state==BUTTON_RESET_WAITING_CONFIRMATION ||
            reset_state==BUTTON_RESET_REQUESTED) action=4;
        reset_state=BUTTON_RESET_IDLE;
        mico_rtos_unlock_mutex(&button_mutex);
        if (action) openm1_log_warn("FACTORY","canceled because OTA is busy");
        return;
    }
    mico_rtos_lock_mutex(&button_mutex);
    if (reset_state==BUTTON_RESET_WAITING_CONFIRMATION) {
        elapsed=(uint32_t)(now-first_long_ms);
        if (elapsed>BUTTON_CONFIRMATION_WINDOW_MS) {
            reset_state=BUTTON_RESET_IDLE;
            action=3;
        } else if (elapsed>=BUTTON_MIN_EVENT_GAP_MS) {
            reset_state=BUTTON_RESET_REQUESTED;
            action=2;
        } /* An early repeat is ignored; the original deadline is unchanged. */
    }
    if (reset_state==BUTTON_RESET_IDLE || reset_state==BUTTON_RESET_COMPLETED ||
        reset_state==BUTTON_RESET_FAILED) {
        reset_state=BUTTON_RESET_WAITING_CONFIRMATION;
        first_long_ms=now;
        if (action!=3) action=1;
    }
    mico_rtos_unlock_mutex(&button_mutex);
    openm1_log_info("BUTTON","long press event received");
    if (action==1) openm1_log_info("FACTORY","confirmation started, timeout=15000ms; release not observable");
    else if (action==2) openm1_log_info("FACTORY","second long press confirmed; release not observable");
    else if (action==3) openm1_log_info("FACTORY","confirmation expired; new window started");
}

void button_manager_tick(void)
{
    uint32_t now=mico_rtos_get_time();
    int action=0;
    OSStatus result;
    if (!button_ready) return;
    if (recovery_ota_busy()) {
        mico_rtos_lock_mutex(&button_mutex);
        if (reset_state==BUTTON_RESET_WAITING_CONFIRMATION ||
            reset_state==BUTTON_RESET_REQUESTED) action=1;
        reset_state=BUTTON_RESET_IDLE;
        mico_rtos_unlock_mutex(&button_mutex);
        if (action) openm1_log_warn("FACTORY","canceled because OTA is busy");
        return;
    }
    mico_rtos_lock_mutex(&button_mutex);
    if (reset_state==BUTTON_RESET_WAITING_CONFIRMATION &&
        (uint32_t)(now-first_long_ms)>BUTTON_CONFIRMATION_WINDOW_MS) {
        reset_state=BUTTON_RESET_IDLE;
        action=2;
    } else if (reset_state==BUTTON_RESET_REQUESTED) {
#if OPENM1_FACTORY_RESET_DRY_RUN
        dry_run_confirmations++;
        reset_state=BUTTON_RESET_COMPLETED;
        action=3;
#else
        reset_state=BUTTON_RESET_IN_PROGRESS;
        factory_reset_attempts++;
        action=4;
#endif
    }
    mico_rtos_unlock_mutex(&button_mutex);
    if (action==2) openm1_log_info("FACTORY","confirmation expired");
    if (action==3) openm1_log_info("FACTORY","dry run confirmed; no configuration changed");
#if !OPENM1_FACTORY_RESET_DRY_RUN
    if (action!=4) return;
    /* Recheck immediately before the only persistent operation. */
    if (recovery_ota_busy()) {
        mico_rtos_lock_mutex(&button_mutex);
        reset_state=BUTTON_RESET_IDLE;
        mico_rtos_unlock_mutex(&button_mutex);
        openm1_log_warn("FACTORY","canceled because OTA is busy");
        return;
    }
    result=config_store_factory_reset();
    mico_rtos_lock_mutex(&button_mutex);
    if (result==kNoErr) {
        factory_reset_successes++;
        reset_state=BUTTON_RESET_COMPLETED;
    } else {
        last_reset_error=result;
        reset_state=BUTTON_RESET_FAILED;
    }
    mico_rtos_unlock_mutex(&button_mutex);
    if (result!=kNoErr) {
        openm1_log_error("FACTORY","defaults save failed, error=%d; no reboot",result);
        return;
    }
    openm1_log_info("FACTORY","defaults persisted successfully; retained HA discovery may remain at Broker");
    openm1_log_info("FACTORY","rebooting after factory reset");
    MicoSystemReboot();
#else
    (void)result;
#endif
}

void button_manager_status_json(char *out,size_t capacity)
{
    button_reset_state_t state;
    uint32_t now=mico_rtos_get_time(),remaining=0,first,last,short_count,long_count,invalid,attempts,successes,dry_runs;
    int error;
    if (!out || !capacity) return;
    if (!button_ready) { snprintf(out,capacity,"{\"available\":false}"); return; }
    mico_rtos_lock_mutex(&button_mutex);
    state=reset_state;first=first_long_ms;last=last_long_ms;
    short_count=short_press_frames;long_count=button_long_press_frames;
    invalid=invalid_long_press_frames;attempts=factory_reset_attempts;
    successes=factory_reset_successes;dry_runs=dry_run_confirmations;error=last_reset_error;
    mico_rtos_unlock_mutex(&button_mutex);
    if (state==BUTTON_RESET_WAITING_CONFIRMATION) {
        uint32_t elapsed=(uint32_t)(now-first);
        if (elapsed<=BUTTON_CONFIRMATION_WINDOW_MS)
            remaining=BUTTON_CONFIRMATION_WINDOW_MS-elapsed;
        else state=BUTTON_RESET_IDLE;
    }
    snprintf(out,capacity,
      "{\"available\":true,\"long_press_frames\":%lu,\"short_press_frames\":%lu,"
      "\"invalid_long_press_frames\":%lu,\"last_long_press_ms\":%lu,"
      "\"factory_reset_state\":\"%s\",\"confirmation_remaining_ms\":%lu,"
      "\"confirmation_window_ms\":%u,\"minimum_event_gap_ms\":%u,"
      "\"factory_reset_dry_run\":%s,\"factory_reset_attempts\":%lu,"
      "\"factory_reset_successes\":%lu,\"dry_run_confirmations\":%lu,"
      "\"last_reset_error\":\"%s\"}",
      (unsigned long)long_count,(unsigned long)short_count,(unsigned long)invalid,
      (unsigned long)last,state_name(state),(unsigned long)remaining,
      BUTTON_CONFIRMATION_WINDOW_MS,BUTTON_MIN_EVENT_GAP_MS,
      OPENM1_FACTORY_RESET_DRY_RUN?"true":"false",(unsigned long)attempts,
      (unsigned long)successes,(unsigned long)dry_runs,
      error==0?"none":"config_save_failed");
}
