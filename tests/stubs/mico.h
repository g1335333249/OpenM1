#pragma once
#include <stdint.h>
typedef int OSStatus;
typedef int mico_mutex_t;
#define kNoErr 0
OSStatus mico_rtos_init_mutex(mico_mutex_t *mutex);
OSStatus mico_rtos_lock_mutex(mico_mutex_t *mutex);
OSStatus mico_rtos_unlock_mutex(mico_mutex_t *mutex);
uint32_t mico_rtos_get_time(void);
