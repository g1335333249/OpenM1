#include "json_min.h"
#include <string.h>

static void spaces(const char **p,const char *end)
{
    while (*p<end && (**p==' '||**p=='\t'||**p=='\n'||**p=='\r')) ++*p;
}
static int string(const char **p,const char *end,char *out,size_t capacity)
{
    size_t n=0;
    if (*p>=end || *(*p)++!='"') return 0;
    while (*p<end && **p!='"') {
        unsigned char c=(unsigned char)*(*p)++;
        if (c=='\\') {
            if (*p>=end) return 0;
            c=(unsigned char)*(*p)++;
            if (c!='"' && c!='\\' && c!='/') return 0;
        }
        if (c<32 || n+1>=capacity) return 0;
        out[n++]=(char)c;
    }
    if (*p>=end) return 0;
    ++*p;out[n]=0;
    return 1;
}
int json_min_parse(const char *body,size_t length,json_min_field_t *fields,size_t capacity)
{
    const char *p=body,*end=body+length;
    int count=0;
    spaces(&p,end);
    if (p>=end || *p++!='{') return -1;
    spaces(&p,end);
    if (p<end && *p=='}') { p++;spaces(&p,end);return p==end?0:-1; }
    while (p<end && (size_t)count<capacity) {
        json_min_field_t *f=&fields[count];
        int i;
        memset(f,0,sizeof(*f));
        if (!string(&p,end,f->key,sizeof(f->key))) return -1;
        for (i=0;i<count;i++) if (!strcmp(fields[i].key,f->key)) return -1;
        spaces(&p,end);if (p>=end || *p++!=':') return -1;
        spaces(&p,end);if (p>=end) return -1;
        if (*p=='"') {
            f->kind='s';if (!string(&p,end,f->value,sizeof(f->value))) return -1;
        } else if (*p=='t'||*p=='f') {
            size_t n=*p=='t'?4:5;
            if (end-p<(ptrdiff_t)n || memcmp(p,*p=='t'?"true":"false",n)) return -1;
            memcpy(f->value,p,n);f->value[n]=0;f->kind='b';p+=n;
        } else {
            size_t n=0;
            f->kind='n';
            while (p<end && *p>='0' && *p<='9' && n+1<sizeof(f->value)) f->value[n++]=*p++;
            if (!n) return -1;
            f->value[n]=0;
        }
        count++;
        spaces(&p,end);
        if (p<end && *p==',') {p++;spaces(&p,end);continue;}
        if (p<end && *p=='}') {p++;spaces(&p,end);return p==end?count:-1;}
        return -1;
    }
    return -1;
}
const json_min_field_t *json_min_find(const json_min_field_t *fields,int count,const char *key)
{
    int i;for (i=0;i<count;i++) if (!strcmp(fields[i].key,key)) return &fields[i];
    return NULL;
}
