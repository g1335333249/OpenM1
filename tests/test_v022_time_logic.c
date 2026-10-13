#include "time_sync.h"
#include <assert.h>
#include <string.h>

int main(void)
{
    uint8_t reply[48]={0},token[8]={1,2,3,4,5,6,7,8};
    uint32_t utc=0;
    assert(time_sync_server_valid("ntp.aliyun.com"));
    assert(time_sync_server_valid("ntp1.tencent.com"));
    assert(time_sync_server_valid("192.168.1.1"));
    assert(!time_sync_server_valid("999.168.1.1"));
    assert(!time_sync_server_valid("192.168.1"));
    assert(!time_sync_server_valid(""));
    assert(!time_sync_server_valid("-invalid.example"));
    assert(!time_sync_server_valid("bad-.example"));
    assert(!time_sync_server_valid("bad..example"));
    assert(!time_sync_server_valid("ntp.example/path"));
    reply[0]=0x24;reply[1]=2;
    memcpy(reply+24,token,8);
    /* 2024-01-01 00:00:00 UTC in NTP seconds. */
    reply[40]=0xe9;reply[41]=0x3c;reply[42]=0x7f;reply[43]=0x80;
    assert(time_sync_parse_reply(reply,sizeof(reply),token,&utc));
    assert(utc>=1700000000u && utc<1800000000u);
    assert(!time_sync_parse_reply(reply,47,token,&utc));
    reply[24]^=1;assert(!time_sync_parse_reply(reply,sizeof(reply),token,&utc));reply[24]^=1;
    reply[1]=0;assert(!time_sync_parse_reply(reply,sizeof(reply),token,&utc));reply[1]=2;
    reply[0]=0xe4;assert(!time_sync_parse_reply(reply,sizeof(reply),token,&utc));
    return 0;
}
