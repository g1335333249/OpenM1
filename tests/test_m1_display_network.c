#include "m1_display.h"
#include <assert.h>

int main(void)
{
    m1_display_network_output_t out;
    assert(M1_DISPLAY_PWM_FREQUENCY_HZ==50000u);
    assert(M1_DISPLAY_PWM_DUTY_PERCENT==20.0f);
    assert(M1_WIFI_BLINK_INTERVAL_MS==150u);
    m1_display_network_output_for_state(M1_NET_DISPLAY_DISCONNECTED,&out);
    assert(out.wifi_blink && out.wifi_on && !out.red_x_on);
    m1_display_network_output_for_state(M1_NET_DISPLAY_ONLINE,&out);
    assert(!out.wifi_blink && out.wifi_on && !out.red_x_on);
    m1_display_network_output_for_state(M1_NET_DISPLAY_NO_INTERNET,&out);
    assert(!out.wifi_blink && out.wifi_on && out.red_x_on);
    return 0;
}
