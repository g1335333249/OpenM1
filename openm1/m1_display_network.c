#include "m1_display.h"

void m1_display_network_output_for_state(m1_net_display_state_t target,
                                         m1_display_network_output_t *out)
{
    if (!out) return;
    out->wifi_blink=(target==M1_NET_DISPLAY_DISCONNECTED);
    out->wifi_on=true; /* enter blink with the Wi-Fi carrier on */
    out->red_x_on=(target==M1_NET_DISPLAY_NO_INTERNET);
}
