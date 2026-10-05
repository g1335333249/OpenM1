#pragma once
#include <stdint.h>
typedef int OSStatus;
typedef int mico_mutex_t;
typedef void *mico_thread_arg_t;
typedef struct { int placeholder; } mico_Context_t;
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
