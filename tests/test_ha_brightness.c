#include "ha_brightness.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    uint8_t level=99;
    const char *valid="01234";
    const char *invalid[]={"5","-1","1.5","abc","04x"," 4 ",""};
    size_t i;
    for (i=0;i<5;i++) {
        assert(ha_brightness_parse(valid+i,1,&level));
        assert(level==i);
    }
    for (i=0;i<sizeof(invalid)/sizeof(invalid[0]);i++)
        assert(!ha_brightness_parse(invalid[i],strlen(invalid[i]),&level));
    assert(!ha_brightness_parse(NULL,1,&level));
    assert(!ha_brightness_parse(valid,0,&level));
    assert(!ha_brightness_parse(valid,1,NULL));
    /* The next byte is deliberately another digit: payloadlen remains 1. */
    assert(ha_brightness_parse("4x",1,&level) && level==4);
    {
        volatile ha_brightness_pending_t pending={0};
        ha_brightness_enqueue(&pending,"3",1);
        assert(pending.pending && pending.level==3);
        assert(ha_brightness_take(&pending,0,&level) && level==3);
        assert(!ha_brightness_take(&pending,0,&level));
        ha_brightness_enqueue(&pending,"2",1);
        assert(!ha_brightness_take(&pending,1,&level));
        assert(!ha_brightness_take(&pending,0,&level));
        ha_brightness_enqueue(&pending,"5",1);
        assert(ha_brightness_take_invalid(&pending));
        assert(!ha_brightness_take_invalid(&pending));
    }
    for (i=0;i<5;i++) {
        char json[240],expected[32];
        snprintf(expected,sizeof(expected),"\"brightness\":%u",(unsigned)i);
        assert(ha_brightness_format_state(json,sizeof(json),"23.5","46.2","12","0.023",(uint8_t)i,1234,-48)==0);
        assert(strstr(json,expected));
    }
    {char tiny[8]; assert(ha_brightness_format_state(tiny,sizeof(tiny),"null","null","null","null",4,0,0)<0);}
    {char json[240]; assert(ha_brightness_format_state(json,sizeof(json),"null","null","null","null",5,0,0)<0);}
    puts("PASS bounded Home Assistant brightness payload parsing");
    return 0;
}
