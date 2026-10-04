#include "mico.h"
#include "m1_uart.h"

static void uart_thread(mico_thread_arg_t arg)
{
    uint8_t byte;
    unsigned count = 0;
    (void)arg;
    while (1) {
        if (mico_uart_recv(MICO_UART_FOR_APP, &byte, 1, 100) == kNoErr) {
            if (count == 0) printf("M1 UART RX:");
            printf(" %02X", byte);
            if (++count == 16) { printf("\r\n"); count = 0; }
        } else if (count) {
            printf("\r\n");
            count = 0;
        }
    }
}

void m1_uart_start(void)
{
    mico_uart_config_t config;
    memset(&config, 0, sizeof(config));
    config.baud_rate = 115200;
    config.data_width = DATA_WIDTH_8BIT;
    config.parity = NO_PARITY;
    config.stop_bits = STOP_BITS_1;
    config.flow_control = FLOW_CONTROL_DISABLED;
    if (mico_uart_init(MICO_UART_FOR_APP, &config, NULL) == kNoErr)
        mico_rtos_create_thread(NULL, MICO_APPLICATION_PRIORITY, "m1uart", uart_thread, 0x600, 0);
}
