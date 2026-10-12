#include "socks5_protocol.h"
#include <string.h>

static int hex_digit(char c)
{
    if (c>='0' && c<='9') return c-'0';
    if (c>='a' && c<='f') return c-'a'+10;
    if (c>='A' && c<='F') return c-'A'+10;
    return -1;
}
int socks5_ipv6_parse(const char *text,uint8_t out[16])
{
    uint16_t words[8];
    size_t count=0,compression=8,i;
    const char *p=text;
    if (!p || !*p || strchr(p,'%') || strchr(p,'[') || strchr(p,']')) return -1;
    if (*p==':') {
        if (p[1]!=':') return -1;
        compression=0;p+=2;
    }
    while (*p) {
        unsigned value=0,digits=0;
        if (count==8) return -1;
        if (*p==':') {
            if (p[1]!=':' || compression!=8) return -1;
            compression=count;p+=2;
            if (!*p) break;
        }
        while (hex_digit(*p)>=0) {
            if (++digits>4) return -1;
            value=(value<<4)|(unsigned)hex_digit(*p++);
        }
        if (!digits || *p=='.') return -1;
        words[count++]=(uint16_t)value;
        if (*p && *p!=':') return -1;
        if (*p==':' && p[1]!=':') {
            p++;
            if (!*p) return -1;
        }
    }
    if (compression==8 && count!=8) return -1;
    if (compression!=8 && count>=8) return -1;
    if (compression!=8) {
        size_t gap=8-count;
        for (i=count;i>compression;i--) words[i+gap-1]=words[i-1];
        for (i=compression;i<compression+gap;i++) words[i]=0;
    }
    for (i=0;i<8;i++) {out[2*i]=(uint8_t)(words[i]>>8);out[2*i+1]=(uint8_t)words[i];}
    /* Link-local addresses need a scope identifier that SOCKS5 cannot carry. */
    if (out[0]==0xfe && (out[1]&0xc0)==0x80) return -2;
    return 0;
}
static int ipv4_parse(const char *host,uint8_t out[4])
{
    const char *p=host;
    size_t i;
    for (i=0;i<4;i++) {
        unsigned value=0,digits=0;
        while (*p>='0' && *p<='9') {
            if (++digits>3) return -1;
            value=value*10u+(unsigned)(*p++-'0');
        }
        if (!digits || value>255 || (i<3?*p++!='.':*p!=0)) return -1;
        out[i]=(uint8_t)value;
    }
    return 0;
}
int socks5_connect_request(const char *host,uint16_t port,uint8_t *out,size_t capacity,size_t *length)
{
    uint8_t bytes[16];
    size_t n,i,address_length;
    int type;
    if (!host || !*host || !port || !out || !length) return -1;
    n=strlen(host);
    if (n>255) return -1;
    if (strchr(host,':')) {
        if (socks5_ipv6_parse(host,bytes)) return -1;
        type=4;address_length=16;
    } else if (ipv4_parse(host,bytes)==0) {
        type=1;address_length=4;
    } else {
        if (n>128 || host[0]=='-' || host[n-1]=='-') return -1;
        for (i=0;i<n;i++) {
            char c=host[i];
            if (!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='.'||c=='-')) return -1;
        }
        type=3;address_length=n+1;
    }
    if (capacity<4+address_length+2) return -1;
    out[0]=5;out[1]=1;out[2]=0;out[3]=(uint8_t)type;
    if (type==3) {out[4]=(uint8_t)n;memcpy(out+5,host,n);}
    else memcpy(out+4,bytes,address_length);
    *length=4+address_length;
    out[(*length)++]=(uint8_t)(port>>8);
    out[(*length)++]=(uint8_t)port;
    return 0;
}
int socks5_reply_header(const uint8_t header[4],size_t *address_length)
{
    if (header[0]!=5 || header[2]!=0) return -1;
    if (header[3]==1) *address_length=4;
    else if (header[3]==4) *address_length=16;
    else if (header[3]==3) *address_length=1;
    else return -1;
    return header[1];
}
