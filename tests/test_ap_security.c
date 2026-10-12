#include "ap_security.h"
#include <assert.h>
#include <string.h>
int main(void)
{
    char long_value[65];
    memset(long_value,'A',sizeof(long_value));long_value[63]=0;
    assert(ap_security_password_valid("12345678"));
    assert(ap_security_password_valid(long_value));
    long_value[63]='A';long_value[64]=0;
    assert(!ap_security_password_valid(long_value));
    assert(!ap_security_password_valid("1234567"));
    assert(!ap_security_password_valid("abc\n12345"));
    assert(!ap_security_password_valid("汉字12345678"));
    assert(!ap_security_password_valid(NULL));
    return 0;
}
