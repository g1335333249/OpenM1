#include "ha_ota.h"
#include <assert.h>

int main(void)
{
    const char *sha="0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
    assert(ha_ota_url_valid("http://192.168.1.20:8000/OpenM1-v0.6.22.ota.bin"));
    assert(ha_ota_url_valid("http://10.0.0.2/fw.bin"));
    assert(!ha_ota_url_valid("https://192.168.1.20/fw.bin"));
    assert(!ha_ota_url_valid("http://8.8.8.8/fw.bin"));
    assert(!ha_ota_url_valid("http://192.168.1.20/fw.bin?token=x"));
    assert(!ha_ota_url_valid("http://192.168.1.20/\"bad.bin"));
    assert(ha_ota_sha256_valid(sha));
    assert(!ha_ota_sha256_valid("deadbeef"));
    assert(ha_ota_config_complete("http://192.168.1.20/fw.bin","0.6.23",sha));
    assert(!ha_ota_config_complete("http://192.168.1.20/fw.bin","bad version",sha));
    assert(ha_ota_install_payload_valid("INSTALL",7,0));
    assert(!ha_ota_install_payload_valid("INSTALL",7,1));
    assert(!ha_ota_install_payload_valid("INSTALL",6,0));
    assert(!ha_ota_install_payload_valid("BAD",3,0));
    assert(!ha_ota_install_payload_valid(0,7,0));
    return 0;
}
