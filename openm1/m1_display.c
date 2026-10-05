#include "m1_display.h"
#include "m1_uart.h"
#include "config_store.h"
#include "recovery.h"
#include "mico_hal/mico_pwm.h"
#include <stdio.h>
#include <string.h>

#define DISPLAY_ECHO_SUPPRESS_MS 500u
#define M1_WIFI_ICON_PWM MICO_PWM_5
#define M1_RED_X_PWM MICO_PWM_4
#define M1_NETWORK_TEST_DURATION_MS 5000u

static mico_mutex_t display_mutex;
static mico_mutex_t network_apply_mutex;
static mico_timer_t network_blink_timer;
static int display_ready;
static m1_display_status_t state;
static uint32_t last_command_ms;
static uint8_t last_command_on;
static int network_blink_timer_initialized;
static int network_apply_ready;
static int network_applied_valid;
static m1_net_display_state_t network_test_target;
static uint32_t network_test_deadline_ms;
static int pwm_error_to_log;
static int blink_timer_stop_pending;

/* Each helper runs with display_mutex held. A failed call never changes the
 * cached running flag, so a later state transition can retry safely. */
static int wifi_pwm_start(void)
{
    OSStatus err;
    if (state.wifi_pwm_running) return 0;
    err=MicoPwmStart(M1_WIFI_ICON_PWM);
    if (err==kNoErr) state.wifi_pwm_running=true;
    else { state.last_pwm_result=err; pwm_error_to_log=1; }
    return err;
}
static int wifi_pwm_stop(void)
{
    OSStatus err;
    if (!state.wifi_pwm_running) return 0;
    err=MicoPwmStop(M1_WIFI_ICON_PWM);
    if (err==kNoErr) state.wifi_pwm_running=false;
    else { state.last_pwm_result=err; pwm_error_to_log=2; }
    return err;
}
static int red_x_pwm_start(void)
{
    OSStatus err;
    if (state.red_x_pwm_running) return 0;
    err=MicoPwmStart(M1_RED_X_PWM);
    if (err==kNoErr) state.red_x_pwm_running=true;
    else { state.last_pwm_result=err; pwm_error_to_log=3; }
    return err;
}
static int red_x_pwm_stop(void)
{
    OSStatus err;
    if (!state.red_x_pwm_running) return 0;
    err=MicoPwmStop(M1_RED_X_PWM);
    if (err==kNoErr) state.red_x_pwm_running=false;
    else { state.last_pwm_result=err; pwm_error_to_log=4; }
    return err;
}

static void network_blink_timer_handler(void *arg)
{
    (void)arg;
    mico_rtos_lock_mutex(&display_mutex);
    if (state.network_pwm_ready && state.blink_timer_running &&
        state.network_applied_target==M1_NET_DISPLAY_DISCONNECTED) {
        int result=state.wifi_pwm_running?wifi_pwm_stop():wifi_pwm_start();
        if (result!=kNoErr) {
            state.blink_timer_running=false;
            state.network_pwm_ready=false;
            network_applied_valid=0;
            blink_timer_stop_pending=1;
        }
    }
    mico_rtos_unlock_mutex(&display_mutex);
}

static void log_pwm_error(void)
{
    int error,kind;
    mico_rtos_lock_mutex(&display_mutex);
    error=state.last_pwm_result;
    kind=pwm_error_to_log;
    pwm_error_to_log=0;
    mico_rtos_unlock_mutex(&display_mutex);
    if (kind) printf("DISPLAY: PWM%d %s failed: %d\r\n",
                     kind<=2?5:4,(kind==1 || kind==3)?"start":"stop",error);
}

static void apply_network_output(void)
{
    m1_net_display_state_t target;
    m1_display_network_output_t output;
    OSStatus err;
    int was_blinking,okay=1;
    if (!display_ready) return;
    mico_rtos_lock_mutex(&network_apply_mutex);
    mico_rtos_lock_mutex(&display_mutex);
    if (!state.network_pwm_ready) { mico_rtos_unlock_mutex(&display_mutex); mico_rtos_unlock_mutex(&network_apply_mutex); return; }
    target=state.network_test_active?network_test_target:state.network_target;
    if (network_applied_valid && state.network_applied_target==target) {
        mico_rtos_unlock_mutex(&display_mutex); mico_rtos_unlock_mutex(&network_apply_mutex); return;
    }
    was_blinking=state.blink_timer_running;
    state.blink_timer_running=false; /* in-flight callback observes false */
    mico_rtos_unlock_mutex(&display_mutex);
    if (was_blinking && network_blink_timer_initialized) {
        err=mico_rtos_stop_timer(&network_blink_timer);
        if (err!=kNoErr) { printf("DISPLAY: blink timer stop failed: %d\r\n",err); okay=0; }
    }
    m1_display_network_output_for_state(target,&output);
    mico_rtos_lock_mutex(&display_mutex);
    if (output.wifi_on) { if (wifi_pwm_start()!=kNoErr) okay=0; }
    else { if (wifi_pwm_stop()!=kNoErr) okay=0; }
    if (output.red_x_on) { if (red_x_pwm_start()!=kNoErr) okay=0; }
    else { if (red_x_pwm_stop()!=kNoErr) okay=0; }
    state.network_applied_target=target;
    network_applied_valid=okay;
    mico_rtos_unlock_mutex(&display_mutex);
    if (output.wifi_blink && okay && network_blink_timer_initialized) {
        err=mico_rtos_start_timer(&network_blink_timer);
        mico_rtos_lock_mutex(&display_mutex);
        state.blink_timer_running=(err==kNoErr);
        if (err!=kNoErr) {state.last_pwm_result=err;network_applied_valid=0;okay=0;}
        mico_rtos_unlock_mutex(&display_mutex);
        if (err!=kNoErr) printf("DISPLAY: blink timer start failed: %d\r\n",err);
    }
    mico_rtos_unlock_mutex(&network_apply_mutex);
    log_pwm_error();
    if (okay) printf("DISPLAY: network -> %s\r\n",target==M1_NET_DISPLAY_DISCONNECTED?"blink":
                     target==M1_NET_DISPLAY_ONLINE?"online":"no_internet");
}

int m1_display_build_brightness_frame(uint8_t level, bool on, uint8_t out[12])
{
    if (!out || level<1 || level>4) return -1;
    memset(out,0,12);
    out[0]=0x23; out[1]=0x02; out[2]=(uint8_t)(level*25u);
    out[3]=on?1u:0u; out[11]=0x21;
    return 0;
}

static void frame_for_current(uint8_t out[12])
{
    uint8_t last;
    bool on;
    mico_rtos_lock_mutex(&display_mutex);
    last=state.last_nonzero_brightness;
    on=state.screen_on;
    mico_rtos_unlock_mutex(&display_mutex);
    m1_display_build_brightness_frame(last,on,out);
}

static void record_command(bool on)
{
    mico_rtos_lock_mutex(&display_mutex);
    last_command_ms=mico_rtos_get_time();
    last_command_on=on?1u:0u;
    mico_rtos_unlock_mutex(&display_mutex);
}

static int persist_brightness(void)
{
    uint8_t level,last;
    mico_rtos_lock_mutex(&display_mutex);
    level=state.brightness_level;
    last=state.last_nonzero_brightness;
    mico_rtos_unlock_mutex(&display_mutex);
    if (config_store_save_brightness(level,last)!=kNoErr) {
        printf("DISPLAY: brightness persistence deferred\r\n");
        return -1;
    }
    return 0;
}

OSStatus m1_display_init(void)
{
    openm1_config_t config;
    OSStatus err=mico_rtos_init_mutex(&display_mutex);
    if (err!=kNoErr) return err;
    config_store_get(&config);
    state.brightness_level=config.brightness_level<=4?config.brightness_level:4;
    state.last_nonzero_brightness=(config.last_nonzero_brightness>=1 &&
                                   config.last_nonzero_brightness<=4)?config.last_nonzero_brightness:4;
    state.screen_on=state.brightness_level!=0;
    state.network_target=M1_NET_DISPLAY_DISCONNECTED;
    display_ready=1;
    err=mico_rtos_init_mutex(&network_apply_mutex);
    if (err!=kNoErr) {
        printf("DISPLAY: network PWM initialization failed: mutex %d\r\n",err);
        return kNoErr;
    }
    network_apply_ready=1;
    err=MicoPwmInitialize(M1_WIFI_ICON_PWM,M1_DISPLAY_PWM_FREQUENCY_HZ,
                          M1_DISPLAY_PWM_DUTY_PERCENT);
    if (err!=kNoErr) {
        printf("DISPLAY: network PWM initialization failed: PWM5 %d\r\n",err);
        return kNoErr;
    }
    err=MicoPwmInitialize(M1_RED_X_PWM,M1_DISPLAY_PWM_FREQUENCY_HZ,
                          M1_DISPLAY_PWM_DUTY_PERCENT);
    if (err!=kNoErr) {
        printf("DISPLAY: network PWM initialization failed: PWM4 %d\r\n",err);
        MicoPwmStop(M1_WIFI_ICON_PWM);
        return kNoErr;
    }
    err=MicoPwmStop(M1_WIFI_ICON_PWM);
    if (err!=kNoErr) {
        printf("DISPLAY: PWM5 stop failed: %d\r\n",err);
        printf("DISPLAY: network PWM initialization failed\r\n");
        return kNoErr;
    }
    err=MicoPwmStop(M1_RED_X_PWM);
    if (err!=kNoErr) {
        printf("DISPLAY: PWM4 stop failed: %d\r\n",err);
        printf("DISPLAY: network PWM initialization failed\r\n");
        return kNoErr;
    }
    err=mico_rtos_init_timer(&network_blink_timer,M1_WIFI_BLINK_INTERVAL_MS,
                             network_blink_timer_handler,NULL);
    if (err!=kNoErr) {
        printf("DISPLAY: network PWM initialization failed: timer %d\r\n",err);
        return kNoErr;
    }
    network_blink_timer_initialized=1;
    mico_rtos_lock_mutex(&display_mutex);
    state.network_pwm_ready=true;
    mico_rtos_unlock_mutex(&display_mutex);
    apply_network_output(); /* Recovery boot always starts with a blinking icon. */
    return kNoErr;
}

int m1_display_set_brightness(uint8_t level)
{
    uint8_t frame[12],last;
    int result;
    if (!display_ready || level>4 || recovery_ota_busy()) return -1;
    mico_rtos_lock_mutex(&display_mutex);
    last=level?level:state.last_nonzero_brightness;
    mico_rtos_unlock_mutex(&display_mutex);
    m1_display_build_brightness_frame(last,level!=0,frame);
    result=m1_uart_send_display_frame(frame);
    if (result) return result;
    mico_rtos_lock_mutex(&display_mutex);
    state.brightness_level=level;
    state.screen_on=level!=0;
    state.last_nonzero_brightness=last;
    mico_rtos_unlock_mutex(&display_mutex);
    record_command(level!=0);
    return persist_brightness();
}

int m1_display_screen_on(void)
{
    uint8_t last;
    if (!display_ready) return -1;
    mico_rtos_lock_mutex(&display_mutex); last=state.last_nonzero_brightness; mico_rtos_unlock_mutex(&display_mutex);
    return m1_display_set_brightness(last);
}

int m1_display_screen_off(void) { return m1_display_set_brightness(0); }

int m1_display_sync(void)
{
    uint8_t frame[12];
    int result;
    if (!display_ready || recovery_ota_busy()) return -1;
    frame_for_current(frame);
    result=m1_uart_send_display_frame(frame);
    if (!result) record_command(frame[3]!=0);
    return result;
}

void m1_display_sync_from_uart_worker(void)
{
    uint8_t frame[12];
    if (!display_ready || recovery_ota_busy()) return;
    frame_for_current(frame);
    if (m1_uart_send_display_frame_from_worker(frame)==0) record_command(frame[3]!=0);
}

void m1_display_handle_brightness_event(uint8_t value)
{
    uint8_t frame[12],last,on=value?1u:0u;
    uint32_t now=mico_rtos_get_time();
    int suppress,same_state;
    if (!display_ready || recovery_ota_busy()) return;
    mico_rtos_lock_mutex(&display_mutex);
    suppress=last_command_ms && now-last_command_ms<=DISPLAY_ECHO_SUPPRESS_MS &&
             last_command_on==on;
    same_state=state.screen_on==(on!=0);
    last=state.last_nonzero_brightness;
    state.screen_on=on!=0;
    state.brightness_level=on?last:0;
    mico_rtos_unlock_mutex(&display_mutex);
    if (suppress || same_state) return;
    m1_display_build_brightness_frame(last,on!=0,frame);
    if (m1_uart_send_display_frame_from_worker(frame)==0) {
        record_command(on!=0);
        persist_brightness();
    }
}

void m1_display_set_network_state(m1_net_display_state_t target)
{
    if (!display_ready || target>M1_NET_DISPLAY_NO_INTERNET) return;
    mico_rtos_lock_mutex(&display_mutex);
    state.network_target=target;
    mico_rtos_unlock_mutex(&display_mutex);
    if (network_apply_ready) apply_network_output();
}

int m1_display_network_test(const char *mode)
{
    m1_net_display_state_t target;
    int automatic,applied;
    if (!mode) return -1;
    if (!strcmp(mode,"blink")) target=M1_NET_DISPLAY_DISCONNECTED;
    else if (!strcmp(mode,"online")) target=M1_NET_DISPLAY_ONLINE;
    else if (!strcmp(mode,"no_internet")) target=M1_NET_DISPLAY_NO_INTERNET;
    else if (!strcmp(mode,"auto")) target=M1_NET_DISPLAY_DISCONNECTED;
    else return -1;
    automatic=!strcmp(mode,"auto");
    if (!network_apply_ready) return -2;
    mico_rtos_lock_mutex(&display_mutex);
    if (!state.network_pwm_ready) { mico_rtos_unlock_mutex(&display_mutex); return -2; }
    state.network_test_active=!automatic;
    network_test_target=target;
    network_test_deadline_ms=mico_rtos_get_time()+M1_NETWORK_TEST_DURATION_MS;
    mico_rtos_unlock_mutex(&display_mutex);
    apply_network_output();
    mico_rtos_lock_mutex(&display_mutex);
    applied=network_applied_valid;
    mico_rtos_unlock_mutex(&display_mutex);
    return applied?0:-2;
}

void m1_display_network_test_tick(void)
{
    int expired=0,stop_timer=0;
    if (!display_ready) return;
    mico_rtos_lock_mutex(&display_mutex);
    if (blink_timer_stop_pending) {stop_timer=1;blink_timer_stop_pending=0;}
    if (state.network_test_active &&
        (int32_t)(mico_rtos_get_time()-network_test_deadline_ms)>=0) {
        state.network_test_active=false;
        expired=1;
    }
    mico_rtos_unlock_mutex(&display_mutex);
    if (stop_timer && network_apply_ready) {
        mico_rtos_lock_mutex(&network_apply_mutex);
        mico_rtos_stop_timer(&network_blink_timer);
        mico_rtos_unlock_mutex(&network_apply_mutex);
    }
    if (expired) apply_network_output();
    log_pwm_error();
}

void m1_display_get_status(m1_display_status_t *out)
{
    if (!out) return;
    memset(out,0,sizeof(*out));
    if (!display_ready) return;
    mico_rtos_lock_mutex(&display_mutex); *out=state; mico_rtos_unlock_mutex(&display_mutex);
}

void m1_display_status_json(char *out, size_t capacity)
{
    m1_display_status_t s;
    m1_display_get_status(&s);
    snprintf(out,capacity,
             "{\"brightness\":%u,\"screen_on\":%s,\"last_nonzero_brightness\":%u,"
             "\"network_target\":\"%s\",\"wifi_icon\":\"%s\",\"red_x\":%s,"
             "\"network_pwm_ready\":%s,\"wifi_pwm_running\":%s,\"red_x_pwm_running\":%s,"
             "\"blink_timer_running\":%s,\"network_test_active\":%s,\"last_pwm_result\":%d,"
             "\"pwm_frequency_hz\":%u,\"pwm_duty_percent\":20,\"wifi_blink_interval_ms\":%u,"
             "\"wifi_icon_protocol\":\"pwm\",\"wifi_icon_protocol_reverse_verified\":true,"
             "\"wifi_icon_hardware_verified\":false}",
             (unsigned)s.brightness_level,s.screen_on?"true":"false",
             (unsigned)s.last_nonzero_brightness,
             s.network_target==M1_NET_DISPLAY_ONLINE?"online":s.network_target==M1_NET_DISPLAY_NO_INTERNET?"no_internet":"blink",
             !s.network_pwm_ready?"off":s.blink_timer_running?"blink":s.wifi_pwm_running?"solid":"off",
             s.red_x_pwm_running?"true":"false",s.network_pwm_ready?"true":"false",
             s.wifi_pwm_running?"true":"false",s.red_x_pwm_running?"true":"false",
             s.blink_timer_running?"true":"false",s.network_test_active?"true":"false",
             s.last_pwm_result,(unsigned)M1_DISPLAY_PWM_FREQUENCY_HZ,
             (unsigned)M1_WIFI_BLINK_INTERVAL_MS);
}
