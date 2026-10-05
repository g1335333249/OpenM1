#pragma once
#include <stdint.h>
typedef int OSStatus;
typedef int mico_mutex_t;
typedef void *mico_thread_arg_t;
typedef struct { int placeholder; } mico_Context_t;
typedef void (*timer_handler_t)(void *);
typedef struct { void *handle; timer_handler_t function; void *arg; } mico_timer_t;
#define kNoErr 0
#define kNotPreparedErr (-1)
#define kParamErr (-2)
OSStatus mico_rtos_init_mutex(mico_mutex_t *mutex);
OSStatus mico_rtos_lock_mutex(mico_mutex_t *mutex);
OSStatus mico_rtos_unlock_mutex(mico_mutex_t *mutex);
uint32_t mico_rtos_get_time(void);
mico_Context_t *mico_system_context_get(void);
void *mico_system_context_get_user_data(mico_Context_t *context);
OSStatus mico_system_context_update(mico_Context_t *context);
OSStatus mico_rtos_init_timer(mico_timer_t *timer,uint32_t milliseconds,timer_handler_t callback,void *arg);
OSStatus mico_rtos_start_timer(mico_timer_t *timer);
OSStatus mico_rtos_stop_timer(mico_timer_t *timer);
