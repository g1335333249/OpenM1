#pragma once
#include <stdint.h>

#define IPV6_PROBE_NOT_ATTEMPTED (-32768)
#define IPV6_TCP_CONNECT_TIMEOUT_MS 3000u
#define IPV6_DNS_SOFT_BUDGET_MS 10000u

/* Pure diagnostic classification; error codes are reported separately. */
typedef enum {
    IPV6_CONNECT_NOT_TESTED,
    IPV6_CONNECT_CONNECTED,
    IPV6_CONNECT_TIMEOUT,
    IPV6_CONNECT_NO_ROUTE,
    IPV6_CONNECT_REFUSED,
    IPV6_CONNECT_SOCKET_ERROR
} ipv6_connect_outcome_t;

ipv6_connect_outcome_t ipv6_connect_outcome(int connected, int select_result,
                                            int writable, int exception,
                                            int socket_error_result, int error_code);
const char *ipv6_connect_outcome_name(ipv6_connect_outcome_t outcome);
int ipv6_addrinfo_valid(int family, unsigned addrlen, unsigned structlen,
                        int sockaddr_family, unsigned sockaddr_len);
int ipv6_should_query_dns(int parse_result,int socket_result,
                          int ota_busy,int dns_api_present);
const char *ipv6_probe_verdict(int api_present,int socket_result,int tcp_attempted,
                               int tcp_connected);
const char *ipv6_parse_status(int result);
