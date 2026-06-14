#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "credentials.h"

#define CHECK(expr) do { \
    if (!(expr)) { \
        fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__, __LINE__, #expr); \
        exit(1); \
    } \
} while (0)

static bool deterministic_random(uint8_t *buf, size_t len, void *ctx)
{
    uint8_t *counter = (uint8_t *)ctx;
    for (size_t i = 0; i < len; ++i) {
        buf[i] = (*counter)++;
    }
    return true;
}

static void test_human_password_uses_four_words(void)
{
    char password[64];
    uint8_t counter = 0;

    CHECK(credentials_generate_human_password(password, sizeof(password), deterministic_random, &counter) == CREDENTIALS_OK);

    unsigned separators = 0;
    for (const char *p = password; *p != '\0'; ++p) {
        if (*p == '-') {
            separators++;
        }
    }
    CHECK(separators == CREDENTIALS_PASSWORD_WORD_COUNT - 1);
    CHECK(strlen(password) >= 20);
    CHECK(strlen(password) <= 32);
}

static void test_rotation_requires_local_display_and_safe_persistence(void)
{
    CHECK(credential_rotation_policy_evaluate(true, true) == CREDENTIAL_ROTATION_ACCEPT);
    CHECK(credential_rotation_policy_evaluate(false, true) == CREDENTIAL_ROTATION_REJECT_NO_LOCAL_DISPLAY);
    CHECK(credential_rotation_policy_evaluate(true, false) == CREDENTIAL_ROTATION_REJECT_PERSISTENCE_UNSAFE);
}

static void test_rotation_policy_names_are_stable(void)
{
    CHECK(strcmp(credential_rotation_policy_result_name(CREDENTIAL_ROTATION_ACCEPT), "accept") == 0);
    CHECK(strcmp(credential_rotation_policy_result_name(CREDENTIAL_ROTATION_REJECT_NO_LOCAL_DISPLAY), "no_local_display") == 0);
    CHECK(strcmp(credential_rotation_policy_result_name(CREDENTIAL_ROTATION_REJECT_PERSISTENCE_UNSAFE), "persistence_unsafe") == 0);
    CHECK(strcmp(credential_rotation_policy_result_name((credential_rotation_policy_result_t)99), "unknown") == 0);
}

int main(void)
{
    test_human_password_uses_four_words();
    test_rotation_requires_local_display_and_safe_persistence();
    test_rotation_policy_names_are_stable();
    return 0;
}
