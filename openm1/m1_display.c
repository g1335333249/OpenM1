#include "m1_display.h"
#include "m1_uart.h"
#include "config_store.h"
#include "recovery.h"
#include <stdio.h>
#include <string.h>

#define DISPLAY_ECHO_SUPPRESS_MS 500u

static mico_mutex_t display_mutex;
static int display_ready;
static m1_display_status_t state;
static uint32_t last_command_ms;
static uint8_t last_command_on;

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
    if (!display_ready) return;
    mico_rtos_lock_mutex(&display_mutex);
    state.network_target=target;
    mico_rtos_unlock_mutex(&display_mutex);
    /* No icon or red-X TX: reference zM1 control protocol remains unverified. */
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
             "\"wifi_icon\":\"unknown\",\"red_x\":false,\"wifi_icon_protocol_verified\":false}",
             (unsigned)s.brightness_level,s.screen_on?"true":"false",
             (unsigned)s.last_nonzero_brightness);
}
