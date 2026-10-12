#include "ap_security.h"
#include <string.h>
int ap_security_password_valid(const char *password)
{
    size_t i,n;
    if (!password) return 0;
    n=strlen(password);
    if (n<8 || n>63) return 0;
    for (i=0;i<n;i++) if ((unsigned char)password[i]<32 ||
                          (unsigned char)password[i]>126) return 0;
    return 1;
}
