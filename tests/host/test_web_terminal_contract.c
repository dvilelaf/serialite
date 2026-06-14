#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "web_terminal_contract.h"

#define CHECK(expr) do { \
    if (!(expr)) { \
        fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__, __LINE__, #expr); \
        exit(1); \
    } \
} while (0)

int main(void)
{
    CHECK(web_terminal_contract_has_required_statuses());
    CHECK(web_terminal_contract_has_mobile_keys());
    CHECK(strcmp(WEB_TERMINAL_STATUS_READ_ONLY, "Input locked") == 0);
    CHECK(strcmp(WEB_TERMINAL_STATUS_WRITE_ACTIVE, "Input enabled") == 0);
    CHECK(strcmp(WEB_TERMINAL_STATUS_WRITER_BUSY, "Input busy") == 0);
    CHECK(strcmp(WEB_TERMINAL_STATUS_USB_DISCONNECTED, "USB lost") == 0);
    CHECK(strcmp(WEB_TERMINAL_ACTION_UNLOCK, "Unlock input") == 0);
    CHECK(strcmp(WEB_TERMINAL_ACTION_LOCK, "Lock input") == 0);
    return 0;
}
