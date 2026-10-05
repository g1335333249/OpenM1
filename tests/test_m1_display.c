#include "m1_display.h"
#include "m1_uart.h"
#include "config_store.h"
#include "mico_hal/mico_pwm.h"
#include <assert.h>
#include <string.h>

static uint32_t now=1000;
static uint8_t last_frame[12];
static int sent;
static mico_timer_t *blink_timer;
static int blink_timer_active;
static int wifi_pwm_on,red_x_pwm_on;
static int pwm_init_count;
static openm1_config_t saved;
OSStatus mico_rtos_init_mutex(mico_mutex_t *m) { *m=1; return 0; }
OSStatus mico_rtos_lock_mutex(mico_mutex_t *m) { assert(*m); return 0; }
OSStatus mico_rtos_unlock_mutex(mico_mutex_t *m) { assert(*m); return 0; }
uint32_t mico_rtos_get_time(void) { return now; }
OSStatus mico_rtos_init_timer(mico_timer_t *timer,uint32_t interval,timer_handler_t cb,void *arg)
{ assert(interval==150); timer->function=cb;timer->arg=arg;blink_timer=timer;return 0; }
OSStatus mico_rtos_start_timer(mico_timer_t *timer) { assert(timer==blink_timer);blink_timer_active=1;return 0; }
OSStatus mico_rtos_stop_timer(mico_timer_t *timer) { assert(timer==blink_timer);blink_timer_active=0;return 0; }
OSStatus MicoPwmInitialize(mico_pwm_t pwm,uint32_t frequency,float duty)
{ assert((pwm==MICO_PWM_5||pwm==MICO_PWM_4) && frequency==50000 && duty==20.0f);pwm_init_count++;return 0; }
OSStatus MicoPwmStart(mico_pwm_t pwm)
{ if(pwm==MICO_PWM_5)wifi_pwm_on=1;else if(pwm==MICO_PWM_4)red_x_pwm_on=1;else assert(0);return 0; }
OSStatus MicoPwmStop(mico_pwm_t pwm)
{ if(pwm==MICO_PWM_5)wifi_pwm_on=0;else if(pwm==MICO_PWM_4)red_x_pwm_on=0;else assert(0);return 0; }
int recovery_ota_busy(void) { return 0; }
void config_store_get(openm1_config_t *out) { *out=saved; }
OSStatus config_store_save_brightness(uint8_t b,uint8_t last) { saved.brightness_level=b; saved.last_nonzero_brightness=last; return 0; }
int m1_uart_send_display_frame(const uint8_t frame[12]) { memcpy(last_frame,frame,12);sent++;return 0; }
int m1_uart_send_display_frame_from_worker(const uint8_t frame[12]) { return m1_uart_send_display_frame(frame); }

int main(void)
{
    uint8_t frame[12], expected[12]={0x23,0x02,0,1,0,0,0,0,0,0,0,0x21};
    m1_display_status_t state;
    unsigned level;
    assert(m1_display_build_brightness_frame(0,1,frame)!=0);
    assert(m1_display_build_brightness_frame(5,1,frame)!=0);
    for (level=1;level<=4;level++) {
        assert(m1_display_build_brightness_frame(level,1,frame)==0);
        expected[2]=(uint8_t)(level*25);
        assert(memcmp(frame,expected,12)==0);
    }
    saved.brightness_level=4; saved.last_nonzero_brightness=4;
    assert(m1_display_init()==0);
    assert(pwm_init_count==2 && wifi_pwm_on && !red_x_pwm_on && blink_timer_active);
    blink_timer->function(blink_timer->arg);
    assert(!wifi_pwm_on && blink_timer_active);
    blink_timer->function(blink_timer->arg);
    assert(wifi_pwm_on && !red_x_pwm_on);
    m1_display_set_network_state(M1_NET_DISPLAY_ONLINE);
    assert(wifi_pwm_on && !red_x_pwm_on && !blink_timer_active);
    m1_display_set_network_state(M1_NET_DISPLAY_NO_INTERNET);
    assert(wifi_pwm_on && red_x_pwm_on && !blink_timer_active);
    assert(m1_display_network_test("blink")==0 && blink_timer_active && !red_x_pwm_on);
    now=6001; m1_display_network_test_tick();
    assert(!blink_timer_active && wifi_pwm_on && red_x_pwm_on);
    assert(m1_display_network_test("channel=4")==-1);
    assert(m1_display_network_test("auto")==0);
    assert(m1_display_set_brightness(2)==0 && last_frame[2]==0x32 && last_frame[3]==1);
    assert(m1_display_set_brightness(0)==0 && last_frame[2]==0x32 && last_frame[3]==0);
    assert(saved.brightness_level==0 && saved.last_nonzero_brightness==2);
    sent=0; now=1100; m1_display_handle_brightness_event(0);
    assert(sent==0); /* matching echo within 500 ms */
    now=1700; m1_display_handle_brightness_event(1);
    assert(sent==1 && last_frame[2]==0x32 && last_frame[3]==1);
    m1_display_get_status(&state);
    assert(state.brightness_level==2 && state.screen_on);
    now=1750; m1_display_handle_brightness_event(1);
    assert(sent==1); /* no TX loop */
    now=2300; m1_display_handle_brightness_event(1);
    assert(sent==1); /* a delayed echo also must not loop */
    now=2400; m1_display_handle_brightness_event(0);
    assert(sent==2 && last_frame[2]==0x32 && last_frame[3]==0);
    assert(saved.last_nonzero_brightness==2);
    return 0;
}
