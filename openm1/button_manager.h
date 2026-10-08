#pragma once
#include "mico.h"
#include <stddef.h>
#include <stdint.h>

/* First hardware release is intentionally simulation-only. A future change
 * must explicitly disable this after independent physical-button validation. */
#ifndef OPENM1_FACTORY_RESET_DRY_RUN
#define OPENM1_FACTORY_RESET_DRY_RUN 1
#endif
#if !OPENM1_FACTORY_RESET_DRY_RUN && !defined(OPENM1_FACTORY_RESET_EXPLICIT_ENABLE)
#error "Real factory reset requires a separate, explicitly reviewed build"
#endif
#define BUTTON_CONFIRMATION_WINDOW_MS 15000u
#define BUTTON_MIN_EVENT_GAP_MS 4000u

typedef enum {
    BUTTON_RESET_IDLE,
    BUTTON_RESET_WAITING_CONFIRMATION,
    BUTTON_RESET_REQUESTED,
    BUTTON_RESET_IN_PROGRESS,
    BUTTON_RESET_COMPLETED,
    BUTTON_RESET_FAILED
} button_reset_state_t;

OSStatus button_manager_init(void);
void button_manager_note_short_press(void);
void button_manager_note_long_press(void);
void button_manager_note_invalid_long_frame(void);
void button_manager_tick(void); /* Normal housekeeping thread, never UART callback. */
void button_manager_status_json(char *out,size_t capacity);
