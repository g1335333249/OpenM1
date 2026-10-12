#pragma once
#include <stddef.h>
#include <stdint.h>

/* Pure protocol helpers: no IPv6 socket or kernel resolver is used. */
int socks5_ipv6_parse(const char *text,uint8_t out[16]);
int socks5_connect_request(const char *host,uint16_t port,uint8_t *out,size_t capacity,size_t *length);
int socks5_reply_header(const uint8_t header[4],size_t *address_length);
