#include <stdio.h>
#include <stdlib.h>

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
    return 0;
}
