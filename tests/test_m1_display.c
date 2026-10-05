#include "m1_display.h"
#include "m1_uart.h"
#include "config_store.h"
#include <assert.h>
#include <string.h>

static uint32_t now=1000;
static uint8_t last_frame[12];
static int sent;
static openm1_config_t saved;
OSStatus mico_rtos_init_mutex(mico_mutex_t *m) { *m=1; return 0; }
OSStatus mico_rtos_lock_mutex(mico_mutex_t *m) { assert(*m); return 0; }
OSStatus mico_rtos_unlock_mutex(mico_mutex_t *m) { assert(*m); return 0; }
uint32_t mico_rtos_get_time(void) { return now; }
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
