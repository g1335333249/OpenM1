#pragma once
#include <stdint.h>
typedef int OSStatus;
typedef int mico_mutex_t;
typedef int mico_thread_t;
typedef void *mico_thread_arg_t;
typedef void (*mico_thread_function_t)(mico_thread_arg_t);
typedef struct { int placeholder; } mico_Context_t;
typedef struct { int num_of_chunks,total_memory,allocted_memory,free_memory; } micoMemInfo_t;
typedef void (*timer_handler_t)(void *);
typedef struct { void *handle; timer_handler_t function; void *arg; } mico_timer_t;
typedef enum { mico_notify_Stack_Overflow_ERROR } mico_notify_types_t;
#define kNoErr 0
#define kNotPreparedErr (-1)
#define kParamErr (-2)
#define MICO_APPLICATION_PRIORITY 5
OSStatus mico_rtos_init_mutex(mico_mutex_t *mutex);
micoMemInfo_t *MicoGetMemoryInfo(void);
OSStatus mico_rtos_lock_mutex(mico_mutex_t *mutex);
OSStatus mico_rtos_unlock_mutex(mico_mutex_t *mutex);
uint32_t mico_rtos_get_time(void);
void mico_thread_msleep(uint32_t milliseconds);
OSStatus mico_rtos_create_thread(mico_thread_t *thread,uint8_t priority,const char *name,
                                  mico_thread_function_t function,uint32_t stack_size,
                                  mico_thread_arg_t arg);
mico_Context_t *mico_system_context_get(void);
void *mico_system_context_get_user_data(mico_Context_t *context);
OSStatus mico_system_context_update(mico_Context_t *context);
void MicoSystemReboot(void);
OSStatus mico_rtos_init_timer(mico_timer_t *timer,uint32_t milliseconds,timer_handler_t callback,void *arg);
OSStatus mico_rtos_start_timer(mico_timer_t *timer);
OSStatus mico_rtos_stop_timer(mico_timer_t *timer);
OSStatus mico_system_notify_register(mico_notify_types_t type,void *handler,void *arg);
