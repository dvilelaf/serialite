#include "ui_web_url_policy.h"

#include <assert.h>
#include <string.h>

int main(void)
{
    assert(strcmp(ui_web_url_for_transport(false), "http://192.168.4.1") == 0);
    assert(strcmp(ui_web_url_for_transport(true), "https://kvm.local") == 0);
    return 0;
}
