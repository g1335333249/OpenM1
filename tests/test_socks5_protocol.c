#include "socks5_protocol.h"
#include <assert.h>
#include <string.h>

int main(void)
{
    uint8_t bytes[270];size_t length=0,address_length=0;
    const uint8_t ipv6[]={0x24,0x0e,0x03,0x0d,0x03,0xc4,0x98,0x01,
                          0,0,0,0,0,0,0x08,0xf1};
    assert(socks5_ipv6_parse("240e:30d:3c4:9801::8f1",bytes)==0);
    assert(memcmp(bytes,ipv6,16)==0);
    assert(socks5_ipv6_parse("240e:030d:03c4:9801:0000:0000:0000:08f1",bytes)==0);
    assert(memcmp(bytes,ipv6,16)==0);
    assert(socks5_ipv6_parse("::1",bytes)==0 && bytes[15]==1);
    assert(socks5_ipv6_parse("fe80::1",bytes)==-2);
    assert(socks5_ipv6_parse("2001:::1",bytes)<0);
    assert(socks5_ipv6_parse("2001::1%wlan0",bytes)<0);
    assert(socks5_connect_request("240e:30d:3c4:9801::8f1",1883,bytes,sizeof(bytes),&length)==0);
    assert(length==22 && bytes[3]==4 && memcmp(bytes+4,ipv6,16)==0);
    assert(bytes[20]==7 && bytes[21]==91);
    assert(socks5_connect_request("192.0.2.1",1883,bytes,sizeof(bytes),&length)==0);
    assert(length==10 && bytes[3]==1 && bytes[4]==192 && bytes[7]==1);
    assert(socks5_connect_request("mqtt.example.org",1883,bytes,sizeof(bytes),&length)==0);
    assert(bytes[3]==3 && bytes[4]==16 && length==23);
    assert(socks5_connect_request("bad host",1883,bytes,sizeof(bytes),&length)<0);
    assert(socks5_connect_request("mqtt.example.org",0,bytes,sizeof(bytes),&length)<0);
    assert(socks5_connect_request("mqtt.example.org",1883,bytes,6,&length)<0);
    assert(socks5_reply_header((const uint8_t[]){5,0,0,4},&address_length)==0 && address_length==16);
    assert(socks5_reply_header((const uint8_t[]){5,5,0,1},&address_length)==5 && address_length==4);
    assert(socks5_reply_header((const uint8_t[]){4,0,0,1},&address_length)<0);
    return 0;
}
