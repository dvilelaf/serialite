#include "web_demo_policy.h"

#include <assert.h>

static void demo_start_requires_disconnected_usb_and_no_writer(void)
{
    assert(web_demo_policy_can_start(false, WEB_SECURITY_WRITER_READ_ONLY));
    assert(!web_demo_policy_can_start(true, WEB_SECURITY_WRITER_READ_ONLY));
    assert(!web_demo_policy_can_start(false, WEB_SECURITY_WRITER_ACTIVE));
    assert(!web_demo_policy_can_start(false, WEB_SECURITY_WRITER_BUSY));
    assert(!web_demo_policy_can_start(false, WEB_SECURITY_WRITER_INVALID_SESSION));
}

static void write_acquire_is_rejected_while_demo_is_active(void)
{
    assert(web_demo_policy_can_acquire_writer(false));
    assert(!web_demo_policy_can_acquire_writer(true));
}

int main(void)
{
    demo_start_requires_disconnected_usb_and_no_writer();
    write_acquire_is_rejected_while_demo_is_active();
    return 0;
}
