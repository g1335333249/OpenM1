#pragma once
#include <stddef.h>
#include <stdint.h>

#define DNS_AAAA_PACKET_MAX 512u
typedef enum {
    DNS_AAAA_OK=0, DNS_AAAA_NO_RECORD, DNS_AAAA_SERVER_ERROR,
    DNS_AAAA_TRUNCATED, DNS_AAAA_MALFORMED, DNS_AAAA_WRONG_REPLY
} dns_aaaa_result_t;

int dns_aaaa_make_query(const char *name,uint16_t id,uint8_t *out,size_t capacity);
dns_aaaa_result_t dns_aaaa_parse(const uint8_t *packet,size_t length,uint16_t id,
                                  const char *question,uint8_t address[16],int *rcode);
const char *dns_aaaa_result_name(dns_aaaa_result_t result);
uint32_t dns_aaaa_remaining_ms(uint32_t started,uint32_t now,uint32_t limit);
