#include "mico.h"
#include "mico_system.h"
#include "wifi_manager.h"
#include "web_server.h"
#include "m1_uart.h"

void appRestoreDefault_callback(void * const data, uint32_t size)
{
    memset(data, 0, size);
}

int main(void)
{
    mico_Context_t *context;
    printf("================================\r\nOpenM1\r\nVersion: v0.0.1\r\nBoard: MK3080B\r\nPlatform: EMW3080BE\r\n================================\r\n");
    context = mico_system_context_init(0);
    if (context == NULL || mico_system_init(context) != kNoErr) {
        printf("OpenM1: system init failed\r\n");
        return -1;
    }
    wifi_manager_start();
    web_server_start();
    m1_uart_start();
    while (1) mico_thread_sleep(1);
}
