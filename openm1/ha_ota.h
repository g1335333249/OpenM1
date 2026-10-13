#pragma once
#include <stddef.h>
int ha_ota_url_valid(const char *url);
int ha_ota_sha256_valid(const char *hex);
int ha_ota_config_complete(const char *url,const char *version,const char *sha256);
int ha_ota_install_payload_valid(const void *payload,size_t length,int retained);
int ha_ota_configure(const char *body,size_t length);
void ha_ota_config_json(char *out,size_t capacity);
