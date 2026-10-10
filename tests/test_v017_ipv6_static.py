#!/usr/bin/env python3
"""Guard bounded DNS, real-time stage publication and stable WLAN ownership."""
from pathlib import Path
import re
p=Path('openm1/ipv6_diagnostic.c').read_text()
d=Path('openm1/dns_aaaa_logic.c').read_text()
w=Path('openm1/wifi_manager.c').read_text()
page=Path('openm1/recovery_page.html').read_text()
assert 'getaddrinfo(' not in re.sub(r'/\*.*?\*/','',p,flags=re.S)
assert 'freeaddrinfo(' not in p
assert 'dns_exchange(' in p and 'IPV6_DNS_SERVER_TIMEOUT_MS 3000u' in p
assert 'sendto(fd,query' in p and 'recvfrom(fd,reply' in p
assert 'select(fd+1,&readable' in p and 'close(fd);return answer;' in p
assert 'source.sin_port!=htons(53)' in p and 'memcmp(&source.sin_addr.s_addr,server_ip,4)' in p
assert 'wifi_manager_station_dns(station_dns)' in p
assert 'IPV6_FALLBACK_RESOLVER "223.5.5.5"' in p
assert 'publish(p)' in p and 'begin(&local,ST_SOCKET' in p and 'end(&local,ST_SOCKET' in p
assert 'mico_rtos_lock_mutex(&result_mutex);result=*p;mico_rtos_unlock_mutex(&result_mutex)' in p
assert 'mico_rtos_lock_mutex(&result_mutex);s=result;mico_rtos_unlock_mutex(&result_mutex)' in p
assert 'stage_results' in p and 'started_ms' in p and 'finished_ms' in p
assert 'begin(&local,ST_PARSE6' in p and 'begin(&local,ST_PARSE4' in p
assert 'begin(p,ST_DNS' in p and 'end(p,ST_DNS' in p
assert 'kernel_ipv6_getaddrinfo_enabled\\\":false' in p
assert 'memcpy(target.sin6_addr.s6_addr,address,16)' in p
assert 'IPV6_DIAGNOSTIC_WORKER_STACK 3072u' in Path('openm1/ipv6_diagnostic.h').read_text()
for needle in ('read_name','target>=at-2','read16(p)!=id','DNS_AAAA_TRUNCATED','DNS_AAAA_NO_RECORD','same_name(owner,question)'):
    assert needle in d
assert 'wifi_manager_station_dns(char out[16])' in w and 'micoWlanGetIPStatus' not in w.split('void wifi_manager_station_dns(char out[16])',1)[1].split('}',1)[0]
assert '固定 Kernel 的 IPv6 getaddrinfo 已在实机卡住' in page
assert 'WIFI_AP_AUTO_CLOSE_SUPPORTED 0' in Path('openm1/wifi_station_logic.h').read_text()
assert 'OPENM1_FACTORY_RESET_DRY_RUN 1' in Path('openm1/button_manager.h').read_text()
print('V017_IPV6_STATIC_PASS: bounded UDP AAAA, staged status, no Kernel DNS or WLAN changes')
