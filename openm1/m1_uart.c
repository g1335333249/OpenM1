#include "m1_uart.h"
#include "openm1_log.h"
#include "m1_sensor.h"
#include "m1_display.h"
#include "recovery.h"
#include "mico_hal/mico_uart.h"
#include "RingBufferUtils.h"
#include <stdio.h>
#include <string.h>

/* Hardware-verified sensor request. Brightness frames are validated below. */
static const uint8_t zm1_sensor_request[12] = {
    0x23,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x21
};

static uint8_t rx_storage[M1_UART_RX_RING_SIZE];
static ring_buffer_t rx_ring;
static uint8_t history[M1_UART_HISTORY_SIZE];
static mico_mutex_t uart_mutex;
static mico_thread_t uart_thread;
static uint32_t desired_baud=M1_UART_DEFAULT_BAUD;
static uint32_t active_baud;
static uint32_t rx_bytes;
static uint32_t last_rx_ms;
static uint32_t tx_bytes,tx_frames,last_tx_ms;
static int display_sync_sent,display_sync_result;
static uint32_t manual_request,manual_completed;
static int manual_result,manual_pending,manual_kind;
static uint8_t manual_frame[12];
static uint32_t last_sensor_request_ms;
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
    unsigned i,first=0,show;
    mico_rtos_lock_mutex(&uart_mutex);
    if (!rx_bytes) first=1;
    for (i=0;i<length;i++) {
        history[history_head]=data[i];
        history_head=(history_head+1)%M1_UART_HISTORY_SIZE;
        if (history_count<M1_UART_HISTORY_SIZE) history_count++;
    }
    rx_bytes+=length;
    last_rx_ms=mico_rtos_get_time();
    mico_rtos_unlock_mutex(&uart_mutex);
    if (first) {
        show=length>64?64:length;
        printf("SENSOR: first RX data received\r\nSENSOR: RX first bytes:\r\n");
        for (i=0;i<show;i++) printf("%02X%s",data[i],i+1<show?" ":"\r\n");
    }
    m1_sensor_parse(data,length);
}

static int valid_display_frame(const uint8_t frame[12])
{
    unsigned i;
    if (!frame || frame[0]!=0x23 || frame[1]!=0x02 || frame[11]!=0x21 ||
        frame[3]>1 || (frame[2]!=0x19 && frame[2]!=0x32 &&
                         frame[2]!=0x4b && frame[2]!=0x64)) return 0;
    for (i=4;i<11;i++) if (frame[i]) return 0;
    return 1;
}

int m1_uart_send_display_frame_from_worker(const uint8_t frame[12])
{
    OSStatus err;
    if (!valid_display_frame(frame) || !uart_online || desired_baud!=active_baud ||
        recovery_ota_busy()) return -2;
    err=MicoUartSend(MICO_UART_FOR_APP,(uint8_t *)frame,12);
    mico_rtos_lock_mutex(&uart_mutex);
    last_tx_ms=mico_rtos_get_time();
    display_sync_result=err;
    if (err==kNoErr) {
        tx_bytes+=12;
        tx_frames++;
        display_sync_sent=1;
    }
    mico_rtos_unlock_mutex(&uart_mutex);
    printf("DISPLAY: TX %02X %02X %02X %02X 00 00 00 00 00 00 00 21, result=%d\r\n",
           frame[0],frame[1],frame[2],frame[3],err);
    return err==kNoErr?0:-4;
}

static OSStatus send_zm1_sensor_request(int manual)
{
    OSStatus err;
    if (!uart_online || desired_baud!=active_baud || recovery_ota_busy()) return kNotPreparedErr;
    if (manual) printf("SENSOR: TX 23 01 00 00 00 00 00 00 00 00 00 21\r\n");
    err=MicoUartSend(MICO_UART_FOR_APP,zm1_sensor_request,sizeof(zm1_sensor_request));
    mico_rtos_lock_mutex(&uart_mutex);
    last_tx_ms=mico_rtos_get_time();
    if (err==kNoErr) {
        tx_bytes+=sizeof(zm1_sensor_request);
        tx_frames++;
    }
    last_sensor_request_ms=mico_rtos_get_time();
    mico_rtos_unlock_mutex(&uart_mutex);
    if (manual || err!=kNoErr) printf("SENSOR: sensor request result = %d\r\n",err);
    return err;
}

void m1_uart_worker(mico_thread_arg_t arg)
{
    uint8_t batch[128];
    uint8_t queued_frame[12];
    uint32_t wanted,available,now,last_log=0,last_error_log=0,last_invalid_log=0,last_invalid_count=0;
    unsigned count;
    OSStatus err;
    (void)arg;
    for (;;) {
        int pending=0,kind=0;
        uint32_t request=0;
        mico_rtos_lock_mutex(&uart_mutex);
        wanted=desired_baud;
        mico_rtos_unlock_mutex(&uart_mutex);
        if (!uart_online || wanted!=active_baud) {
            if (uart_online) {
                mico_rtos_lock_mutex(&uart_mutex);
                uart_online=0;
                mico_rtos_unlock_mutex(&uart_mutex);
                MicoUartFinalize(MICO_UART_FOR_APP);
            }
            err=open_uart(wanted);
            mico_rtos_lock_mutex(&uart_mutex);
            uart_online=(err==kNoErr);
            active_baud=uart_online?wanted:0;
            mico_rtos_unlock_mutex(&uart_mutex);
            if (err!=kNoErr) {
                printf("SENSOR: UART1 init failed, error=%d; retrying\r\n",err);
                now=mico_rtos_get_time();
                if (!last_error_log || now-last_error_log>=30000u) {
                    openm1_log_error("UART","UART1 init failed: %d",err);
                    last_error_log=now;
                }
                mico_thread_msleep(2000);
                continue;
            }
            openm1_log_info("UART","UART1 ready, %lu 8N1",(unsigned long)wanted);
            printf("SENSOR: UART1 ready\r\n");
            mico_thread_msleep(400);
            m1_display_sync_from_uart_worker();
            last_sensor_request_ms=mico_rtos_get_time();
        }
        mico_rtos_lock_mutex(&uart_mutex);
        if (manual_pending) {
            pending=1; request=manual_request;kind=manual_kind;manual_pending=0;
            if (kind==1) memcpy(queued_frame,manual_frame,sizeof(queued_frame));
        }
        mico_rtos_unlock_mutex(&uart_mutex);
        if (pending) {
            err=kind==2?send_zm1_sensor_request(1):m1_uart_send_display_frame_from_worker(queued_frame);
            mico_rtos_lock_mutex(&uart_mutex);
            manual_result=err; manual_completed=request;
            mico_rtos_unlock_mutex(&uart_mutex);
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
        if (uart_online && desired_baud==active_baud && !recovery_ota_busy() &&
            now-last_sensor_request_ms>=M1_SENSOR_POLL_INTERVAL_MS)
            send_zm1_sensor_request(0);
        if (now-last_log>=10000) {
            m1_sensor_snapshot_t snapshot;
            m1_sensor_get_snapshot(&snapshot);
            mico_rtos_lock_mutex(&uart_mutex);
            printf("SENSOR: UART rx=%lu total=%lu sensor=%lu time=%lu type0f=%lu type18=%lu invalid=%lu\r\n",
                   (unsigned long)rx_bytes,(unsigned long)snapshot.total_frames,
                   (unsigned long)snapshot.sensor_frames,(unsigned long)snapshot.time_frames,
                   (unsigned long)snapshot.type0f_frames,(unsigned long)snapshot.type18_frames,
                   (unsigned long)snapshot.invalid_frames);
            mico_rtos_unlock_mutex(&uart_mutex);
            if (snapshot.invalid_frames>last_invalid_count &&
                (!last_invalid_log || now-last_invalid_log>=60000u)) {
                openm1_log_warn("UART","protocol errors total=%lu",(unsigned long)snapshot.invalid_frames);
                last_invalid_log=now;
            }
            last_invalid_count=snapshot.invalid_frames;
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
    m1_sensor_set_brightness_callback(m1_display_handle_brightness_event);
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

static int request_fixed_command(int kind, const uint8_t frame[12])
{
    uint32_t request;
    unsigned waited;
    int result;
    if (!worker_ready) return -2;
    mico_rtos_lock_mutex(&uart_mutex);
    if (!uart_online || desired_baud!=active_baud || recovery_ota_busy()) {
        mico_rtos_unlock_mutex(&uart_mutex);
        return -2;
    }
    if (manual_pending || manual_request!=manual_completed) {
        mico_rtos_unlock_mutex(&uart_mutex);
        return -3;
    }
    request=++manual_request;
    manual_kind=kind;
    if (kind==1) memcpy(manual_frame,frame,sizeof(manual_frame));
    manual_pending=1;
    mico_rtos_unlock_mutex(&uart_mutex);
    for (waited=0;waited<3000;waited+=20) {
        mico_thread_msleep(20);
        mico_rtos_lock_mutex(&uart_mutex);
        if (manual_completed==request) {
            result=manual_result;
            mico_rtos_unlock_mutex(&uart_mutex);
            return result==kNoErr?0:-4;
        }
        mico_rtos_unlock_mutex(&uart_mutex);
    }
    return -5;
}

int m1_uart_send_init_command(void)
{
    return m1_display_sync();
}

int m1_uart_request_sensors(void)
{
    return request_fixed_command(2,NULL);
}

int m1_uart_send_display_frame(const uint8_t frame[12])
{
    if (!valid_display_frame(frame)) return -1;
    return request_fixed_command(1,frame);
}

void m1_uart_status_json(char *out, size_t capacity)
{
    uint32_t baud,bytes,last,tx_count,tx_frame_count,last_tx;
    int online,sent,tx_result;
    m1_sensor_snapshot_t s;
    m1_sensor_get_snapshot(&s);
    if (worker_ready) {
        mico_rtos_lock_mutex(&uart_mutex);
        baud=active_baud;bytes=rx_bytes;last=last_rx_ms;online=uart_online;
        tx_count=tx_bytes;tx_frame_count=tx_frames;last_tx=last_tx_ms;
        sent=display_sync_sent;tx_result=display_sync_result;
        mico_rtos_unlock_mutex(&uart_mutex);
    } else { baud=0;bytes=0;last=0;online=0;tx_count=0;tx_frame_count=0;last_tx=0;sent=0;tx_result=0; }
    snprintf(out,capacity,
             "{\"uart\":\"MICO_UART_1\",\"tx\":\"GPIO9\",\"rx\":\"GPIO10\",\"baud\":%lu,"
             "\"online\":%s,\"rx_bytes\":%lu,\"total_frames\":%lu,\"sensor_frames\":%lu,"
             "\"time_frames\":%lu,\"type0f_frames\":%lu,\"brightness_frames\":%lu,\"type18_frames\":%lu,"
             "\"unknown_frames\":%lu,\"invalid_frames\":%lu,\"m1_datetime\":\"%s\","
             "\"type0f_last_value\":%u,\"type0f_last_rx_ms\":%lu,"
             "\"last_rx_ms\":%lu,\"last_rx_age_ms\":%lu,\"tx_bytes\":%lu,\"tx_frames\":%lu,"
             "\"last_tx_ms\":%lu,\"last_tx_age_ms\":%lu,\"init_command_sent\":%s,\"init_command_result\":%d,"
             "\"parser\":\"zm1-type1-verified\",\"protocol_verified\":true,\"passive_rx_only\":false,\"fixed_init_command_only\":false,\"fixed_commands_only\":true,\"sensor_auto_polling\":true,\"sensor_poll_interval_ms\":2000}",
             (unsigned long)baud,online?"true":"false",(unsigned long)bytes,
             (unsigned long)s.total_frames,(unsigned long)s.sensor_frames,
             (unsigned long)s.time_frames,(unsigned long)s.type0f_frames,
             (unsigned long)s.type0f_frames,
             (unsigned long)s.type18_frames,(unsigned long)s.unknown_frames,
             (unsigned long)s.invalid_frames,s.m1_datetime,
             (unsigned)s.type0f_last_value,(unsigned long)s.type0f_last_rx_ms,
             (unsigned long)last,
             (unsigned long)(last?mico_rtos_get_time()-last:0),
             (unsigned long)tx_count,(unsigned long)tx_frame_count,(unsigned long)last_tx,
             (unsigned long)(last_tx?mico_rtos_get_time()-last_tx:0),
             sent?"true":"false",tx_result);
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
