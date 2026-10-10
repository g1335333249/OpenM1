#include "dns_aaaa_logic.h"
#include <string.h>

static unsigned read16(const uint8_t *p) { return ((unsigned)p[0]<<8)|p[1]; }
static void put16(uint8_t *p,unsigned value) { p[0]=(uint8_t)(value>>8);p[1]=(uint8_t)value; }
static int same_name(const char *a,const char *b)
{
    while (*a && *b) {
        unsigned x=(unsigned char)*a++,y=(unsigned char)*b++;
        if (x>='A' && x<='Z') x+=32;
        if (y>='A' && y<='Z') y+=32;
        if (x!=y) return 0;
    }
    return !*a && !*b;
}
int dns_aaaa_make_query(const char *name,uint16_t id,uint8_t *out,size_t capacity)
{
    size_t pos=12,start=0,i,n;
    if (!name || !out || capacity<18 || !*name) return -1;
    memset(out,0,12);put16(out,id);put16(out+2,0x0100);put16(out+4,1);
    for (i=0;;i++) {
        if (name[i]!='.' && name[i]) continue;
        n=i-start;
        if (!n || n>63 || pos+1+n+5>capacity || i>253) return -1;
        out[pos++]=(uint8_t)n;
        memcpy(out+pos,name+start,n);pos+=n;
        if (!name[i]) break;
        start=i+1;
    }
    out[pos++]=0;put16(out+pos,28);pos+=2;put16(out+pos,1);pos+=2;
    return (int)pos;
}
/* Bounded compression walker: cursor advances only over bytes in the caller's
 * record, while jumps resolve the name elsewhere in the same packet. */
static int read_name(const uint8_t *p,size_t len,size_t *cursor,char *out,size_t cap)
{
    size_t at=*cursor,used=0,tail=0;unsigned jumps=0;
    int jumped=0;
    if (!cap) return 0;
    for (;;) {
        unsigned label;
        if (at>=len || ++jumps>128) return 0;
        label=p[at++];
        if ((label&0xc0)==0xc0) {
            size_t target;
            if (at>=len || ++jumps>128) return 0;
            target=((size_t)(label&0x3f)<<8)|p[at++];
            if (target<12 || target>=len || target>=at-2) return 0;
            if (!jumped) { tail=at; jumped=1; }
            at=target;continue;
        }
        if (label&0xc0) return 0;
        if (!label) { out[used]=0;*cursor=jumped?tail:at;return 1; }
        if (label>63 || at+label>len || used+label+1>=cap) return 0;
        if (used) out[used++]='.';
        memcpy(out+used,p+at,label);used+=label;at+=label;
    }
}
dns_aaaa_result_t dns_aaaa_parse(const uint8_t *p,size_t len,uint16_t id,
                                  const char *question,uint8_t address[16],int *rcode)
{
    char requested[64],owner[64],alias[64];
    size_t offset,answers;
    unsigned count,pass;
    int query_len;
    uint8_t query[96];
    if (rcode) *rcode=0;
    if (!p || !question || !address || strlen(question)>=sizeof(requested) ||
        len<12 || len>DNS_AAAA_PACKET_MAX) return DNS_AAAA_MALFORMED;
    query_len=dns_aaaa_make_query(question,id,query,sizeof(query));
    if (query_len<0) return DNS_AAAA_MALFORMED;
    if (read16(p)!=id || !(p[2]&0x80) || (p[2]&0x78) || !(p[2]&0x01)) return DNS_AAAA_WRONG_REPLY;
    if (p[2]&0x02) return DNS_AAAA_TRUNCATED;
    if (rcode) *rcode=p[3]&15;
    if ((p[3]&15)!=0) return DNS_AAAA_SERVER_ERROR;
    if (read16(p+4)!=1) return DNS_AAAA_MALFORMED;
    offset=12;
    if (!read_name(p,len,&offset,owner,sizeof(owner)) || offset+4>len)
        return DNS_AAAA_MALFORMED;
    if (!same_name(owner,question) || read16(p+offset)!=28 || read16(p+offset+2)!=1)
        return DNS_AAAA_WRONG_REPLY;
    offset+=4;answers=offset;count=read16(p+6);
    if (count>64) return DNS_AAAA_MALFORMED;
    memcpy(requested,question,strlen(question)+1);
    for (pass=0;pass<8;pass++) {
        size_t at=answers;unsigned i;int next=0;
        for (i=0;i<count;i++) {
            unsigned type,klass,rdlen;size_t data;
            if (!read_name(p,len,&at,owner,sizeof(owner)) || at+10>len) return DNS_AAAA_MALFORMED;
            type=read16(p+at);klass=read16(p+at+2);rdlen=read16(p+at+8);
            at+=10;data=at;
            if (rdlen>len-at) return DNS_AAAA_MALFORMED;
            at+=rdlen;
            if (klass!=1 || !same_name(owner,requested)) continue;
            if (type==28 && rdlen==16) { memcpy(address,p+data,16);return DNS_AAAA_OK; }
            if (type==5 && !next) {
                size_t end=data;
                if (!read_name(p,len,&end,alias,sizeof(alias)) || end!=data+rdlen)
                    return DNS_AAAA_MALFORMED;
                next=1;
            }
        }
        if (!next || same_name(alias,requested)) break;
        memcpy(requested,alias,strlen(alias)+1);
    }
    return DNS_AAAA_NO_RECORD;
}
const char *dns_aaaa_result_name(dns_aaaa_result_t result)
{
    switch (result) {
    case DNS_AAAA_OK:return "ok";
    case DNS_AAAA_NO_RECORD:return "no_aaaa";
    case DNS_AAAA_SERVER_ERROR:return "server_error";
    case DNS_AAAA_TRUNCATED:return "truncated";
    case DNS_AAAA_MALFORMED:return "malformed";
    default:return "wrong_reply";
    }
}
uint32_t dns_aaaa_remaining_ms(uint32_t started,uint32_t now,uint32_t limit)
{
    uint32_t elapsed=(uint32_t)(now-started);
    return elapsed>=limit?0:limit-elapsed;
}
