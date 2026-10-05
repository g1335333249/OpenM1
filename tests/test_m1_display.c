#include "m1_display.h"
#include "m1_uart.h"
#include "config_store.h"
#include "mico_hal/mico_pwm.h"
#include <assert.h>
#include <setjmp.h>
#include <string.h>

/* Advance one worker iteration without a real thread or PWM hardware. */
#include "../openm1/m1_display.c"

static uint32_t now=1000;
static jmp_buf yield_point;
static int yield_enabled,mutex_depth;
static int wifi_pwm_on,red_x_pwm_on,pwm_init_count,pwm_calls;
static int fail_next_pwm_start;
static uint8_t last_frame[12];
static int sent;
static openm1_config_t saved;
static mico_thread_function_t created_worker;

OSStatus mico_rtos_init_mutex(mico_mutex_t *m) { *m=1; return 0; }
OSStatus mico_rtos_lock_mutex(mico_mutex_t *m) { assert(*m); mutex_depth++; return 0; }
OSStatus mico_rtos_unlock_mutex(mico_mutex_t *m) { assert(*m && mutex_depth>0); mutex_depth--; return 0; }
uint32_t mico_rtos_get_time(void) { return now; }
void mico_thread_msleep(uint32_t ms)
{ now+=ms; if (yield_enabled) longjmp(yield_point,1); assert(0); }
OSStatus mico_rtos_create_thread(mico_thread_t *thread,uint8_t priority,const char *name,
                                  mico_thread_function_t function,uint32_t stack,mico_thread_arg_t arg)
{ assert(thread && priority==MICO_APPLICATION_PRIORITY &&
         !strcmp(name,"openm1_net_display") && stack==2048 && !arg);
  created_worker=function; return 0; }
OSStatus MicoPwmInitialize(mico_pwm_t pwm,uint32_t frequency,float duty)
{ assert((pwm==MICO_PWM_5||pwm==MICO_PWM_4) && frequency==50000 && duty==20.0f);
  pwm_init_count++; return 0; }
OSStatus MicoPwmStart(mico_pwm_t pwm)
{ assert(mutex_depth==0); pwm_calls++;
  if (fail_next_pwm_start) { fail_next_pwm_start=0; return -7; }
  if (pwm==MICO_PWM_5) wifi_pwm_on=1; else if (pwm==MICO_PWM_4) red_x_pwm_on=1; else assert(0);
  return 0; }
OSStatus MicoPwmStop(mico_pwm_t pwm)
{ assert(mutex_depth==0); pwm_calls++;
  if (pwm==MICO_PWM_5) wifi_pwm_on=0; else if (pwm==MICO_PWM_4) red_x_pwm_on=0; else assert(0);
  return 0; }
int recovery_ota_busy(void) { return 0; }
void config_store_get(openm1_config_t *out) { *out=saved; }
OSStatus config_store_save_brightness(uint8_t b,uint8_t last)
{ saved.brightness_level=b; saved.last_nonzero_brightness=last; return 0; }
int m1_uart_send_display_frame(const uint8_t frame[12])
{ memcpy(last_frame,frame,12); sent++; return 0; }
int m1_uart_send_display_frame_from_worker(const uint8_t frame[12])
{ return m1_uart_send_display_frame(frame); }

static void run_worker_start(void)
{ yield_enabled=1; if (setjmp(yield_point)==0) created_worker(0); yield_enabled=0; }
static void run_step(void)
{ yield_enabled=1; if (setjmp(yield_point)==0) network_display_step(); yield_enabled=0; }

int main(void)
{
    uint8_t frame[12],expected[12]={0x23,0x02,0,1,0,0,0,0,0,0,0,0x21};
    m1_display_status_t snapshot;
    int calls;
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
    assert(pwm_init_count==2 && pwm_calls==0 && created_worker);
    run_worker_start();
    assert(wifi_pwm_on && !red_x_pwm_on && now==1150);
    run_step(); assert(!wifi_pwm_on && !red_x_pwm_on && now==1300);
    calls=pwm_calls;
    m1_display_set_network_state(M1_NET_DISPLAY_ONLINE);
    assert(pwm_calls==calls);
    run_step(); assert(wifi_pwm_on && !red_x_pwm_on && now==1400);
    calls=pwm_calls;
    run_step(); assert(pwm_calls==calls);
    m1_display_set_network_state(M1_NET_DISPLAY_NO_INTERNET);
    run_step(); assert(wifi_pwm_on && red_x_pwm_on);
    calls=pwm_calls;
    assert(m1_display_network_test("blink")==0 && pwm_calls==calls);
    run_step(); assert(wifi_pwm_on && !red_x_pwm_on);
    run_step(); assert(!wifi_pwm_on && !red_x_pwm_on);
    now+=5000; run_step();
    assert(wifi_pwm_on && red_x_pwm_on);
    assert(m1_display_network_test("online")==0);
    run_step(); assert(wifi_pwm_on && !red_x_pwm_on);
    assert(m1_display_network_test("no_internet")==0);
    run_step(); assert(wifi_pwm_on && red_x_pwm_on);
    assert(m1_display_network_test("auto")==0);
    run_step(); assert(wifi_pwm_on && red_x_pwm_on);
    assert(m1_display_network_test("channel=4")==-1);
    m1_display_get_status(&snapshot);
    assert(snapshot.display_worker_running && !snapshot.network_test_active);
    assert(snapshot.network_target==M1_NET_DISPLAY_NO_INTERNET);
    m1_display_set_network_state(M1_NET_DISPLAY_DISCONNECTED);
    run_step(); run_step();
    fail_next_pwm_start=1;
    run_step();
    m1_display_get_status(&snapshot);
    assert(!snapshot.network_pwm_ready && snapshot.last_pwm_result==-7);
    calls=pwm_calls;
    run_step(); assert(pwm_calls==calls); /* no 150 ms error loop */

    assert(m1_display_set_brightness(2)==0 && last_frame[2]==0x32 && last_frame[3]==1);
    assert(m1_display_set_brightness(0)==0 && last_frame[2]==0x32 && last_frame[3]==0);
    assert(saved.brightness_level==0 && saved.last_nonzero_brightness==2);
    sent=0; now=1100; m1_display_handle_brightness_event(0);
    assert(sent==0);
    now=1700; m1_display_handle_brightness_event(1);
    assert(sent==1 && last_frame[2]==0x32 && last_frame[3]==1);
    now=1750; m1_display_handle_brightness_event(1);
    assert(sent==1);
    now=2300; m1_display_handle_brightness_event(1);
    assert(sent==1);
    now=2400; m1_display_handle_brightness_event(0);
    assert(sent==2 && last_frame[2]==0x32 && last_frame[3]==0);
    return 0;
}
