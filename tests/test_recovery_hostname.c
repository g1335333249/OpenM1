#include "recovery.h"
#include <assert.h>
#include <string.h>

int main(void)
{
    const uint8_t sample[6]={0xB0,0xF8,0x93,0x25,0x6F,0x4A};
    const uint8_t invalid[6]={0};
    char *stable=recovery_hostname();
    assert(!strcmp(stable,"OpenM1"));
    recovery_prepare_hostname(sample,1);
    assert(recovery_hostname()==stable);
    assert(!strcmp(stable,"OpenM1-256F4A"));
    recovery_prepare_hostname(invalid,0);
    assert(!strcmp(stable,"OpenM1"));
    recovery_prepare_hostname(NULL,0);
    assert(!strcmp(stable,"OpenM1"));
    return 0;
}
