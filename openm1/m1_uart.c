#include "m1_uart.h"
#include "m1_sensor.h"
#include "mico_hal/mico_uart.h"
#include "RingBufferUtils.h"
#include <stdio.h>
#include <string.h>

static uint8_t rx_storage[M1_UART_RX_RING_SIZE];
static ring_buffer_t rx_ring;
static uint8_t history[M1_UART_HISTORY_SIZE];
static mico_mutex_t uart_mutex;
static mico_thread_t uart_thread;
static uint32_t desired_baud=M1_UART_DEFAULT_BAUD;
static uint32_t active_baud;
static uint32_t rx_bytes;
static uint32_t last_rx_ms;
static unsigned history_head,history_count;
static int worker_ready,uart_online;

static int supported_baud(uint32_t baud)
{
    return baud==9600 || baud==19200 || baud==38400 || baud==57600 || baud==115200;
}

static OSStatus open_uart(uint32_t baud)
{
    mico_uart_config_t cfg;
    OSStatus err;
    memset(&cfg,0,sizeof(cfg));
    cfg.baud_rate=baud;
    cfg.data_width=DATA_WIDTH_8BIT;
    cfg.parity=NO_PARITY;
    cfg.stop_bits=STOP_BITS_1;
    cfg.flow_control=FLOW_CONTROL_DISABLED;
    err=ring_buffer_init(&rx_ring,rx_storage,sizeof(rx_storage));
    if (err!=kNoErr) return err;
    return MicoUartInitialize(MICO_UART_FOR_APP,&cfg,&rx_ring);
}

static void record_rx(const uint8_t *data, unsigned length)
{
    unsigned i;
    mico_rtos_lock_mutex(&uart_mutex);
    for (i=0;i<length;i++) {
        history[history_head]=data[i];
        history_head=(history_head+1)%M1_UART_HISTORY_SIZE;
        if (history_count<M1_UART_HISTORY_SIZE) history_count++;
    }
    rx_bytes+=length;
    last_rx_ms=mico_rtos_get_time();
    mico_rtos_unlock_mutex(&uart_mutex);
    m1_sensor_parse(data,length);
}

void m1_uart_worker(mico_thread_arg_t arg)
{
    uint8_t batch[128];
    uint32_t wanted,available,now,last_log=0;
    unsigned count;
    OSStatus err;
    (void)arg;
    for (;;) {
        mico_rtos_lock_mutex(&uart_mutex);
        wanted=desired_baud;
        mico_rtos_unlock_mutex(&uart_mutex);
        if (!uart_online || wanted!=active_baud) {
            if (uart_online) {
                MicoUartFinalize(MICO_UART_FOR_APP);
                uart_online=0;
            }
            err=open_uart(wanted);
            mico_rtos_lock_mutex(&uart_mutex);
            uart_online=(err==kNoErr);
            active_baud=uart_online?wanted:0;
            mico_rtos_unlock_mutex(&uart_mutex);
            if (err!=kNoErr) {
                printf("SENSOR: UART1 init failed, error=%d; retrying\r\n",err);
                mico_thread_msleep(2000);
                continue;
            }
            printf("SENSOR: UART1 receive ready, %lu 8N1\r\n",(unsigned long)wanted);
        }
        available=MicoUartGetLengthInBuffer(MICO_UART_FOR_APP);
        if (!available) {
            mico_thread_msleep(10);
        } else {
            count=available>sizeof(batch)?sizeof(batch):(unsigned)available;
            if (MicoUartRecv(MICO_UART_FOR_APP,batch,count,100)==kNoErr)
                record_rx(batch,count);
            else mico_thread_msleep(10);
        }
        now=mico_rtos_get_time();
        if (now-last_log>=10000) {
            m1_sensor_snapshot_t snapshot;
            m1_sensor_get_snapshot(&snapshot);
            mico_rtos_lock_mutex(&uart_mutex);
            printf("SENSOR: UART rx=%lu frames=%lu errors=%lu\r\n",
                   (unsigned long)rx_bytes,(unsigned long)snapshot.frame_count,
                   (unsigned long)snapshot.parser_error_count);
            mico_rtos_unlock_mutex(&uart_mutex);
            last_log=now;
        }
    }
}

OSStatus m1_uart_init(void)
{
    OSStatus err=mico_rtos_init_mutex(&uart_mutex);
    if (err!=kNoErr) return err;
    err=m1_sensor_init();
    if (err!=kNoErr) return err;
    err=mico_rtos_create_thread(&uart_thread,MICO_APPLICATION_PRIORITY,"openm1_uart",
                                m1_uart_worker,M1_UART_WORKER_STACK,0);
    if (err==kNoErr) worker_ready=1;
    return err;
}

int m1_uart_set_baud(uint32_t baud)
{
    if (!supported_baud(baud)) return -1;
    if (!worker_ready) return -2;
    mico_rtos_lock_mutex(&uart_mutex);
    desired_baud=baud;
    mico_rtos_unlock_mutex(&uart_mutex);
    return 0;
}

void m1_uart_status_json(char *out, size_t capacity)
{
    uint32_t baud,bytes,last;
    int online;
    m1_sensor_snapshot_t s;
    m1_sensor_get_snapshot(&s);
    if (worker_ready) {
        mico_rtos_lock_mutex(&uart_mutex);
        baud=active_baud;bytes=rx_bytes;last=last_rx_ms;online=uart_online;
        mico_rtos_unlock_mutex(&uart_mutex);
    } else { baud=0;bytes=0;last=0;online=0; }
    snprintf(out,capacity,
             "{\"uart\":\"MICO_UART_1\",\"tx\":\"GPIO9\",\"rx\":\"GPIO10\",\"baud\":%lu,"
             "\"online\":%s,\"rx_bytes\":%lu,\"valid_frames\":%lu,\"invalid_frames\":%lu,"
             "\"last_rx_ms\":%lu,\"last_rx_age_ms\":%lu,\"parser\":\"zm1-type1-partial\",\"protocol_verified\":false,\"passive_rx_only\":true}",
             (unsigned long)baud,online?"true":"false",(unsigned long)bytes,
             (unsigned long)s.frame_count,(unsigned long)s.parser_error_count,(unsigned long)last,
             (unsigned long)(last?mico_rtos_get_time()-last:0));
}

void m1_uart_raw_json(char *out, size_t capacity)
{
    uint8_t recent[128];
    unsigned i,count=0,start;
    uint32_t last=0;
    char hex[128*3+1];
    if (worker_ready) {
        mico_rtos_lock_mutex(&uart_mutex);
        count=history_count>sizeof(recent)?sizeof(recent):history_count;
        start=(history_head+M1_UART_HISTORY_SIZE-count)%M1_UART_HISTORY_SIZE;
        for (i=0;i<count;i++) recent[i]=history[(start+i)%M1_UART_HISTORY_SIZE];
        last=last_rx_ms;
        mico_rtos_unlock_mutex(&uart_mutex);
    }
    for (i=0;i<count;i++) snprintf(hex+i*3,sizeof(hex)-i*3,"%02X%s",recent[i],i+1<count?" ":"");
    if (!count) hex[0]=0;
    snprintf(out,capacity,"{\"bytes\":%u,\"hex\":\"%s\",\"last_rx_ms\":%lu}",
             count,hex,(unsigned long)last);
}
