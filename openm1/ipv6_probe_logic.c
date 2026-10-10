#include "ipv6_probe_logic.h"
#include <errno.h>

ipv6_connect_outcome_t ipv6_connect_outcome(int connected, int select_result,
                                            int writable, int exception,
                                            int socket_error_result, int error_code)
{
    if (connected) return IPV6_CONNECT_CONNECTED;
    if (select_result==0) return IPV6_CONNECT_TIMEOUT;
    if (select_result<0 || socket_error_result<0) return IPV6_CONNECT_SOCKET_ERROR;
    if (error_code==ENETUNREACH || error_code==EHOSTUNREACH) return IPV6_CONNECT_NO_ROUTE;
    if (error_code==ECONNREFUSED) return IPV6_CONNECT_REFUSED;
    if (error_code==ETIMEDOUT) return IPV6_CONNECT_TIMEOUT;
    if (select_result>0 && writable && !exception && error_code==0)
        return IPV6_CONNECT_CONNECTED;
    return IPV6_CONNECT_SOCKET_ERROR;
}
const char *ipv6_connect_outcome_name(ipv6_connect_outcome_t outcome)
{
    switch (outcome) {
    case IPV6_CONNECT_CONNECTED: return "connected";
    case IPV6_CONNECT_TIMEOUT: return "timeout";
    case IPV6_CONNECT_NO_ROUTE: return "no_route";
    case IPV6_CONNECT_REFUSED: return "refused";
    case IPV6_CONNECT_SOCKET_ERROR: return "socket_error";
    default: return "not_tested";
    }
}
int ipv6_addrinfo_valid(int family,unsigned addrlen,unsigned structlen,
                        int sockaddr_family,unsigned sockaddr_len)
{
    /* AF_INET6 is 10 in the fixed MiCO socket ABI. A zero sin6_len is
     * accepted because some lwIP versions leave it unset. */
    return family==10 && sockaddr_family==10 && addrlen>=structlen &&
           (!sockaddr_len || (sockaddr_len>=structlen && sockaddr_len<=addrlen));
}
int ipv6_should_query_dns(int parse_result,int socket_result,
                          int ota_busy,int dns_api_present)
{
    /* A failed numeric parser or socket creation must not suppress AAAA. */
    (void)parse_result;
    (void)socket_result;
    return !ota_busy && dns_api_present;
}
const char *ipv6_probe_verdict(int api_present,int socket_result,int tcp_attempted,
                               int tcp_connected)
{
    if (tcp_connected) return "supported";
    if (tcp_attempted) return "tcp_failed";
    if (!api_present || (socket_result<0 && socket_result!=IPV6_PROBE_NOT_ATTEMPTED))
        return "unsupported";
    return "inconclusive";
}
const char *ipv6_parse_status(int result)
{
    if (result==1) return "parsed";
    if (result==0) return "not_parsed";
    if (result==IPV6_PROBE_NOT_ATTEMPTED) return "not_tested";
    return "error";
}
