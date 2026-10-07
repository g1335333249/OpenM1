#include "mqtt_bounded_read.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    uint32_t now;
    int wait_result,recv_result,socket_error;
    const unsigned char *data;
    unsigned cursor,available,wait_calls,pause_calls;
} fake_t;
static uint32_t now_ms(void *p) { return ((fake_t *)p)->now; }
static int wait_readable(void *p,int fd,uint32_t timeout)
{
    fake_t *f=p;
    (void)fd;
    f->wait_calls++;
    if (f->wait_result==0) f->now+=timeout;
    return f->wait_result;
}
static int recv_bytes(void *p,int fd,unsigned char *buffer,int length)
{
    fake_t *f=p;
    unsigned take;
    (void)fd;
    if (f->recv_result<=0) return f->recv_result;
    take=(unsigned)length;
    if (take>(unsigned)f->recv_result) take=(unsigned)f->recv_result;
    if (take>f->available-f->cursor) take=f->available-f->cursor;
    if (!take) return 0;
    memcpy(buffer,f->data+f->cursor,take);
    f->cursor+=take;
    f->now++;
    return (int)take;
}
static int socket_error(void *p,int fd) { (void)fd;return ((fake_t *)p)->socket_error; }
static void pause_ms(void *p,uint32_t ms) { fake_t *f=p;f->now+=ms;f->pause_calls++; }
static const openm1_mqtt_read_ops_t ops={now_ms,wait_readable,recv_bytes,socket_error,pause_ms};
int main(void)
{
    fake_t f={0};
    openm1_mqtt_read_state_t state;
    unsigned char bytes[8]={0};
    unsigned i;
    openm1_mqtt_read_reset(&state);
    /* An idle socket returns zero, remains usable, and never busy-spins. */
    f.wait_result=0;
    for (i=0;i<150;i++)
        assert(openm1_mqtt_read_bounded(&state,&ops,&f,3,bytes,1,200)==0);
    assert(f.now==30000 && state.read_timeout_count==150 && f.wait_calls==150);
    assert(state.peer_close_count==0 && state.last_socket_error==0);
    /* Fragmented payload is collected without exceeding the total deadline. */
    openm1_mqtt_read_reset(&state);
    { static const unsigned char stream[]={0x30,0x03,'a','b','c'};
      f.wait_result=1;f.recv_result=1;f.data=stream;f.available=sizeof(stream);f.cursor=0;
      assert(openm1_mqtt_read_bounded(&state,&ops,&f,3,bytes,1,200)==1);
      assert(openm1_mqtt_read_bounded(&state,&ops,&f,3,bytes,1,200)==1);
      assert(openm1_mqtt_read_bounded(&state,&ops,&f,3,bytes,3,200)==3);
      assert(memcmp(bytes,"abc",3)==0 && state.phase==0);
    }
    /* A readable socket closed by its peer is an error, not idle. */
    f.wait_result=1;f.recv_result=0;
    assert(openm1_mqtt_read_bounded(&state,&ops,&f,3,bytes,1,200)==OPENM1_MQTT_READ_PEER_CLOSED);
    assert(state.peer_close_count==1);
    /* A real recv error and a select error are both negative. */
    f.recv_result=-1;f.socket_error=47;
    assert(openm1_mqtt_read_bounded(&state,&ops,&f,3,bytes,1,200)==OPENM1_MQTT_READ_SOCKET_ERROR);
    assert(state.last_socket_error==47);
    f.wait_result=-1;
    assert(openm1_mqtt_read_bounded(&state,&ops,&f,3,bytes,1,200)==OPENM1_MQTT_READ_SOCKET_ERROR);
    /* Spurious readable + no SO_ERROR waits again with a 1 ms pause. */
    f.wait_result=1;f.socket_error=0;f.pause_calls=0;
    assert(openm1_mqtt_read_bounded(&state,&ops,&f,3,bytes,1,3)==0);
    assert(f.pause_calls>0);
    /* MQTT fixed header and Remaining Length are decoded before body malloc. */
    openm1_mqtt_read_reset(&state);
    { static const unsigned char stream[]={0x30,0x88,0x08};
      f.data=stream;f.available=sizeof(stream);f.cursor=0;f.recv_result=1;
      assert(openm1_mqtt_read_bounded(&state,&ops,&f,3,bytes,1,200)==1);
      assert(openm1_mqtt_read_bounded(&state,&ops,&f,3,bytes,1,200)==1);
      assert(openm1_mqtt_read_bounded(&state,&ops,&f,3,bytes,1,200)==OPENM1_MQTT_READ_PROTOCOL_ERROR);
    }
    assert(openm1_mqtt_read_bounded(&state,&ops,&f,3,bytes,1025,200)==OPENM1_MQTT_READ_PROTOCOL_ERROR);
    puts("PASS bounded MQTT read: idle 30s, peer close, socket errors, 1024B limit");
    return 0;
}
