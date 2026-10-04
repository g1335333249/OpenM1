#pragma once
#include <stddef.h>
typedef struct { char key[32]; char value[129]; char kind; } json_min_field_t;
/* Strict flat JSON object; kind is 's' string, 'n' integer, or 'b' boolean. */
int json_min_parse(const char *body, size_t length, json_min_field_t *fields, size_t capacity);
const json_min_field_t *json_min_find(const json_min_field_t *fields, int count, const char *key);
