#include "ipv6_probe_logic.h"
#include <assert.h>
#include <errno.h>
#include <string.h>

int main(void)
{
    /* inet_pton 1/0/-1 are recorded independently of AAAA and TCP. */
    assert(ipv6_should_query_dns(1,0,0,1));
    assert(!strcmp(ipv6_parse_status(1),"parsed"));
    assert(!strcmp(ipv6_parse_status(0),"not_parsed"));
    assert(!strcmp(ipv6_parse_status(-1),"error"));
    assert(ipv6_should_query_dns(0,0,0,1));
    assert(ipv6_should_query_dns(-1,-1,0,1));
    assert(!ipv6_should_query_dns(0,0,1,1));
    assert(!ipv6_should_query_dns(1,0,0,0));
    /* AAAA can succeed despite inet_pton failure; TCP alone proves data path. */
    assert(!strcmp(ipv6_probe_verdict(1,0,0,0),"inconclusive"));
    assert(!strcmp(ipv6_probe_verdict(1,0,1,1),"supported"));
    assert(!strcmp(ipv6_probe_verdict(1,0,1,0),"tcp_failed"));
    assert(!strcmp(ipv6_probe_verdict(1,-1,0,0),"unsupported"));
    assert(ipv6_addrinfo_valid(10,28,28,10,28));
    assert(ipv6_addrinfo_valid(10,28,28,10,0));
    assert(!ipv6_addrinfo_valid(2,28,28,2,28));
    assert(!ipv6_addrinfo_valid(10,16,28,10,16));
    assert(!ipv6_addrinfo_valid(10,28,28,2,28));
    assert(!ipv6_addrinfo_valid(10,28,28,10,40));
    assert(ipv6_connect_outcome(1,-32768,0,0,-32768,0)==IPV6_CONNECT_CONNECTED);
    assert(ipv6_connect_outcome(0,1,1,0,0,0)==IPV6_CONNECT_CONNECTED);
    assert(ipv6_connect_outcome(0,0,0,0,-32768,0)==IPV6_CONNECT_TIMEOUT);
    assert(ipv6_connect_outcome(0,1,1,0,0,ENETUNREACH)==IPV6_CONNECT_NO_ROUTE);
    assert(ipv6_connect_outcome(0,1,1,0,0,ECONNREFUSED)==IPV6_CONNECT_REFUSED);
    assert(ipv6_connect_outcome(0,-1,0,0,-32768,EIO)==IPV6_CONNECT_SOCKET_ERROR);
    assert(!strcmp(ipv6_connect_outcome_name(IPV6_CONNECT_NO_ROUTE),"no_route"));
    return 0;
}
