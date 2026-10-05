#pragma once
#include "mico.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    M1_NET_DISPLAY_DISCONNECTED,
    M1_NET_DISPLAY_ONLINE,
    M1_NET_DISPLAY_NO_INTERNET
} m1_net_display_state_t;

#define M1_DISPLAY_PWM_FREQUENCY_HZ 50000u
#define M1_DISPLAY_PWM_DUTY_PERCENT 20.0f
#define M1_WIFI_BLINK_INTERVAL_MS 150u

typedef struct {
    bool wifi_blink;
    bool wifi_on;
    bool red_x_on;
} m1_display_network_output_t;

typedef struct {
    uint8_t brightness_level;
    uint8_t last_nonzero_brightness;
    bool screen_on;
    m1_net_display_state_t network_target;
    m1_net_display_state_t network_applied_target;
    bool network_pwm_ready;
    bool wifi_pwm_running;
    bool red_x_pwm_running;
    bool blink_timer_running;
    bool network_test_active;
    int last_pwm_result;
} m1_display_status_t;

OSStatus m1_display_init(void);
int m1_display_build_brightness_frame(uint8_t level, bool on, uint8_t out[12]);
int m1_display_set_brightness(uint8_t level);
int m1_display_screen_on(void);
int m1_display_screen_off(void);
int m1_display_sync(void);
void m1_display_sync_from_uart_worker(void);
void m1_display_handle_brightness_event(uint8_t value);
void m1_display_set_network_state(m1_net_display_state_t target);
void m1_display_network_output_for_state(m1_net_display_state_t target,
                                         m1_display_network_output_t *out);
int m1_display_network_test(const char *mode);
void m1_display_network_test_tick(void);
void m1_display_get_status(m1_display_status_t *out);
void m1_display_status_json(char *out, size_t capacity);
