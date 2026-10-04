#include "ha_policy.h"
#include <assert.h>
#include <stdio.h>
int main(void)
{
    assert(!ha_policy_can_enable(0,0,0)); /* MQTT not configured -> HTTP 409 */
    assert(!ha_policy_can_enable(1,0,0)); /* configured but stopped -> HTTP 409 */
    assert(!ha_policy_can_enable(1,1,0)); /* started but disconnected -> HTTP 409 */
    assert(ha_policy_can_enable(1,1,1));  /* all three conditions -> accepted */
    puts("PASS Home Assistant three-condition backend policy");
    return 0;
}
