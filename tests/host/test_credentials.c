#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "credentials.h"
#include "credentials_wordlist.h"

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

struct u32_sequence {
    const uint32_t *values;
    size_t count;
    size_t offset;
    size_t calls;
};

static bool u32_sequence_random(uint8_t *buf, size_t len, void *ctx)
{
    struct u32_sequence *sequence = (struct u32_sequence *)ctx;
    CHECK(len == sizeof(uint32_t));
    CHECK(sequence->offset < sequence->count);

    const uint32_t value = sequence->values[sequence->offset++];
    sequence->calls++;
    buf[0] = (uint8_t)(value & 0xffU);
    buf[1] = (uint8_t)((value >> 8) & 0xffU);
    buf[2] = (uint8_t)((value >> 16) & 0xffU);
    buf[3] = (uint8_t)((value >> 24) & 0xffU);
    return true;
}

static bool failing_random(uint8_t *buf, size_t len, void *ctx)
{
    (void)buf;
    (void)len;
    (void)ctx;
    return false;
}

static void test_human_password_uses_six_words(void)
{
    char password[128];
    uint8_t counter = 0;

    CHECK(credentials_generate_human_password(password, sizeof(password), deterministic_random, &counter) == CREDENTIALS_OK);

    unsigned separators = 0;
    for (const char *p = password; *p != '\0'; ++p) {
        CHECK(*p != '-');
        if (*p == ' ') {
            separators++;
        }
    }
    CHECK(separators == CREDENTIALS_WIFI_PASSWORD_WORD_COUNT - 1);
    CHECK(CREDENTIALS_WIFI_PASSWORD_WORD_COUNT == 6U);
}

static void test_human_password_uses_eff_large_wordlist_endpoints(void)
{
    char password[128];
    const uint32_t values[] = {0U, CREDENTIALS_WORDLIST_COUNT - 1U, 0U, 0U, 0U, 0U};
    struct u32_sequence sequence = {
        .values = values,
        .count = sizeof(values) / sizeof(values[0]),
    };

    CHECK(credentials_generate_human_password(password, sizeof(password), u32_sequence_random, &sequence) == CREDENTIALS_OK);
    CHECK(strncmp(password, "abacus zoom abacus ", strlen("abacus zoom abacus ")) == 0);
}

static void test_phrase_policy_rejects_legacy_hyphenated_secrets(void)
{
    CHECK(credentials_human_phrase_matches_policy("alpha bravo charlie delta echo foxtrot", 6));
    CHECK(credentials_human_phrase_matches_policy("alpha bravo", 2));
    CHECK(!credentials_human_phrase_matches_policy("alpha-bravo-charlie-delta-echo-foxtrot", 6));
    CHECK(!credentials_human_phrase_matches_policy("alpha bravo charlie", 2));
    CHECK(!credentials_human_phrase_matches_policy("alpha  bravo", 2));
    CHECK(!credentials_human_phrase_matches_policy(" alpha bravo", 2));
    CHECK(!credentials_human_phrase_matches_policy("alpha bravo ", 2));
}

static void test_human_phrase_compacts_for_operational_password(void)
{
    char compact[128];

    CHECK(credentials_compact_human_phrase("alpha bravo charlie delta echo foxtrot", 6, compact, sizeof(compact)));
    CHECK(strcmp(compact, "alphabravocharliedeltaechofoxtrot") == 0);
    CHECK(!credentials_compact_human_phrase("Alpha bravo", 2, compact, sizeof(compact)));
    CHECK(!credentials_compact_human_phrase("alpha  bravo", 2, compact, sizeof(compact)));
}

static void test_wifi_qr_payload_uses_standard_wifi_format(void)
{
    char payload[128];

    CHECK(credentials_wifi_qr_payload("Serialite", "alpha bravo charlie delta echo foxtrot", payload, sizeof(payload)));
    CHECK(strcmp(payload, "WIFI:S:Serialite;T:WPA;P:alphabravocharliedeltaechofoxtrot;;") == 0);
}

static void test_wifi_qr_payload_escapes_reserved_characters(void)
{
    char payload[128];

    CHECK(credentials_wifi_qr_payload("KV;M", "alpha:bravo,slash\\quote\"", payload, sizeof(payload)));
    CHECK(strcmp(payload, "WIFI:S:KV\\;M;T:WPA;P:alpha\\:bravo\\,slash\\\\quote\\\";;") == 0);
}

static void test_human_wifi_password_always_fits_wpa_passphrase_limit(void)
{
    char password[128];
    const uint32_t values[] = {2U, 2U, 2U, 2U, 2U, 2U};
    struct u32_sequence sequence = {
        .values = values,
        .count = sizeof(values) / sizeof(values[0]),
    };

    CHECK(credentials_generate_human_password(password, sizeof(password), u32_sequence_random, &sequence) == CREDENTIALS_OK);
    CHECK(strlen(password) <= 63U);
}

static void test_human_password_rejects_out_of_range_random_values(void)
{
    char password[128];
    const uint32_t values[] = {
        UINT32_MAX, 0U,
        UINT32_MAX, 0U,
        UINT32_MAX, 0U,
        UINT32_MAX, 0U,
        UINT32_MAX, 0U,
        UINT32_MAX, 0U,
    };
    struct u32_sequence sequence = {
        .values = values,
        .count = sizeof(values) / sizeof(values[0]),
    };

    CHECK(credentials_generate_human_password(password, sizeof(password), u32_sequence_random, &sequence) == CREDENTIALS_OK);
    CHECK(strcmp(password, "abacus abacus abacus abacus abacus abacus") == 0);
    CHECK(sequence.calls == 12U);
}

static void test_human_password_random_failure_propagates(void)
{
    char password[128];

    CHECK(credentials_generate_human_password(password, sizeof(password), failing_random, NULL) == CREDENTIALS_ERR_RANDOM_FAILED);
}

static void test_human_password_output_too_small_fails(void)
{
    char password[8];
    const uint32_t values[] = {0U, 0U, 0U, 0U, 0U, 0U};
    struct u32_sequence sequence = {
        .values = values,
        .count = sizeof(values) / sizeof(values[0]),
    };

    CHECK(credentials_generate_human_password(password, sizeof(password), u32_sequence_random, &sequence) == CREDENTIALS_ERR_OUTPUT_TOO_SMALL);
}

static void test_wordlist_only_has_lowercase_words(void)
{
    for (size_t i = 0; i < CREDENTIALS_WORDLIST_COUNT; ++i) {
        const char *word = CREDENTIALS_WORDLIST[i];
        CHECK(word[0] != '\0');
        for (const char *p = word; *p != '\0'; ++p) {
            CHECK(*p >= 'a' && *p <= 'z');
        }
    }
}

static void test_every_word_yields_a_valid_compactable_phrase(void)
{
    for (uint32_t i = 0; i < CREDENTIALS_WORDLIST_COUNT; ++i) {
        const uint32_t values[] = {i, i, i, i, i, i};
        struct u32_sequence sequence = {
            .values = values,
            .count = sizeof(values) / sizeof(values[0]),
        };
        char password[128];
        char compact[128];
        CHECK(credentials_generate_human_password(password, sizeof(password), u32_sequence_random, &sequence) == CREDENTIALS_OK);
        CHECK(credentials_human_phrase_matches_policy(password, CREDENTIALS_WIFI_PASSWORD_WORD_COUNT));
        CHECK(credentials_compact_human_phrase(password, CREDENTIALS_WIFI_PASSWORD_WORD_COUNT, compact, sizeof(compact)));
    }
}

int main(void)
{
    test_human_password_uses_six_words();
    test_human_password_uses_eff_large_wordlist_endpoints();
    test_phrase_policy_rejects_legacy_hyphenated_secrets();
    test_human_phrase_compacts_for_operational_password();
    test_wifi_qr_payload_uses_standard_wifi_format();
    test_wifi_qr_payload_escapes_reserved_characters();
    test_human_wifi_password_always_fits_wpa_passphrase_limit();
    test_human_password_rejects_out_of_range_random_values();
    test_human_password_random_failure_propagates();
    test_human_password_output_too_small_fails();
    test_wordlist_only_has_lowercase_words();
    test_every_word_yields_a_valid_compactable_phrase();
    return 0;
}
