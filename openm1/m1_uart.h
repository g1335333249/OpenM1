#pragma once
#include "mico.h"
#include <stddef.h>
#include <stdint.h>

#define M1_UART_WORKER_STACK 4096
#define M1_UART_RX_RING_SIZE 2048
#define M1_UART_HISTORY_SIZE 1024
#define M1_UART_DEFAULT_BAUD 115200u
#define M1_SENSOR_POLL_INTERVAL_MS 2000u

OSStatus m1_uart_init(void);
void m1_uart_worker(mico_thread_arg_t arg);
int m1_uart_set_baud(uint32_t baud);
int m1_uart_send_init_command(void);
int m1_uart_request_sensors(void);
int m1_uart_send_display_frame(const uint8_t frame[12]);
int m1_uart_send_display_frame_from_worker(const uint8_t frame[12]);
void m1_uart_status_json(char *out, size_t capacity);
void m1_uart_raw_json(char *out, size_t capacity);
