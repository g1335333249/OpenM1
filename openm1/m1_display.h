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

typedef struct {
    uint8_t brightness_level;
    uint8_t last_nonzero_brightness;
    bool screen_on;
    m1_net_display_state_t network_target;
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
void m1_display_get_status(m1_display_status_t *out);
void m1_display_status_json(char *out, size_t capacity);
