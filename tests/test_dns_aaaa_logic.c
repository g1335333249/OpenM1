#include "dns_aaaa_logic.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>

static void put16(unsigned char *p,unsigned v) { p[0]=(unsigned char)(v>>8);p[1]=(unsigned char)v; }
static size_t response(unsigned char *p,int cname)
{
    int n=dns_aaaa_make_query("www.ustc.edu.cn",0x1234,p,512);
    size_t at=(size_t)n,i;
    assert(n>0);
    p[2]=0x81;p[3]=0x80;put16(p+6,cname?2:1);
    p[at++]=0xc0;p[at++]=0x0c;
    if (cname) {
        put16(p+at,5);put16(p+at+2,1);memset(p+at+4,0,4);
        put16(p+at+8,7);at+=10;
        p[at++]=4;memcpy(p+at,"edge",4);at+=4;
        p[at++]=0xc0;p[at++]=0x10; /* ustc.edu.cn in original question */
        p[at++]=4;memcpy(p+at,"edge",4);at+=4;
        p[at++]=0xc0;p[at++]=0x10;
    }
    put16(p+at,28);put16(p+at+2,1);memset(p+at+4,0,4);
    put16(p+at+8,16);at+=10;
    for (i=0;i<16;i++) p[at++]=(unsigned char)(i+1);
    return at;
}
int main(void)
{
    unsigned char p[512],ip[16];int rcode,n;
    size_t len=response(p,0);
    assert(dns_aaaa_parse(p,len,0x1234,"www.ustc.edu.cn",ip,&rcode)==DNS_AAAA_OK);
    assert(ip[0]==1 && ip[15]==16 && rcode==0);
    put16(p+6,2);p[len++]=0xc0;p[len++]=0x0c;
    put16(p+len,28);put16(p+len+2,1);memset(p+len+4,0,4);
    put16(p+len+8,16);len+=10;memset(p+len,0xab,16);len+=16;
    assert(dns_aaaa_parse(p,len,0x1234,"www.ustc.edu.cn",ip,&rcode)==DNS_AAAA_OK);
    assert(ip[0]==1 && ip[15]==16); /* first valid AAAA, no overwrite */
    len=response(p,0);
    assert(dns_aaaa_parse(p,len,0x1235,"www.ustc.edu.cn",ip,&rcode)==DNS_AAAA_WRONG_REPLY);
    assert(dns_aaaa_parse(p,len,0x1234,"other.ustc.edu.cn",ip,&rcode)==DNS_AAAA_WRONG_REPLY);
    p[12]=0x7f;
    assert(dns_aaaa_parse(p,len,0x1234,"www.ustc.edu.cn",ip,&rcode)==DNS_AAAA_MALFORMED);
    len=response(p,0);put16(p+4,2);
    assert(dns_aaaa_parse(p,len,0x1234,"www.ustc.edu.cn",ip,&rcode)==DNS_AAAA_MALFORMED);
    len=response(p,0);
    p[2]|=2;assert(dns_aaaa_parse(p,len,0x1234,"www.ustc.edu.cn",ip,&rcode)==DNS_AAAA_TRUNCATED);
    len=response(p,0);p[3]=0x83;
    assert(dns_aaaa_parse(p,len,0x1234,"www.ustc.edu.cn",ip,&rcode)==DNS_AAAA_SERVER_ERROR && rcode==3);
    len=response(p,0);put16(p+6,0);
    assert(dns_aaaa_parse(p,len,0x1234,"www.ustc.edu.cn",ip,&rcode)==DNS_AAAA_NO_RECORD);
    len=response(p,0);assert(dns_aaaa_parse(p,len-1,0x1234,"www.ustc.edu.cn",ip,&rcode)==DNS_AAAA_MALFORMED);
    len=response(p,0);p[12]=0xc0;p[13]=0x0c;
    assert(dns_aaaa_parse(p,len,0x1234,"www.ustc.edu.cn",ip,&rcode)==DNS_AAAA_MALFORMED);
    len=response(p,1);
    assert(dns_aaaa_parse(p,len,0x1234,"www.ustc.edu.cn",ip,&rcode)==DNS_AAAA_OK);
    n=dns_aaaa_make_query("www.ustc.edu.cn",0x1234,p,sizeof(p));
    assert(n>12 && p[0]==0x12 && p[1]==0x34 && p[n-4]==0 && p[n-3]==28);
    assert(dns_aaaa_make_query("bad..name",1,p,sizeof(p))<0);
    assert(dns_aaaa_remaining_ms(100,3099,3000)==1);
    assert(dns_aaaa_remaining_ms(100,3100,3000)==0);
    assert(dns_aaaa_remaining_ms(0xfffffff0u,0x10u,3000)==2968);
    puts("DNS_AAAA_LOGIC_PASS");return 0;
}
