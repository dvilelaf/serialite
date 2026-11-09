#include "credentials.h"

#include <stdio.h>

#include "credentials_wordlist.h"

enum {
    PASSWORD_RANDOM_BYTES = sizeof(uint32_t),
};

static credentials_result_t credentials_random_u32(
    credentials_random_fn_t random_fn,
    void *random_ctx,
    uint32_t *out)
{
    uint8_t bytes[PASSWORD_RANDOM_BYTES] = {0};
    if (!random_fn(bytes, sizeof(bytes), random_ctx)) {
        return CREDENTIALS_ERR_RANDOM_FAILED;
    }

    *out = ((uint32_t)bytes[0]) |
           ((uint32_t)bytes[1] << 8) |
           ((uint32_t)bytes[2] << 16) |
           ((uint32_t)bytes[3] << 24);
    return CREDENTIALS_OK;
}

static credentials_result_t credentials_select_word_index(
    credentials_random_fn_t random_fn,
    void *random_ctx,
    size_t *out_index)
{
    const uint64_t random_space = (uint64_t)UINT32_MAX + 1ULL;
    const uint32_t unbiased_limit = (uint32_t)(random_space - (random_space % CREDENTIALS_WORDLIST_COUNT));

    for (;;) {
        uint32_t value = 0;
        const credentials_result_t result = credentials_random_u32(random_fn, random_ctx, &value);
        if (result != CREDENTIALS_OK) {
            return result;
        }
        if (value < unbiased_limit) {
            *out_index = (size_t)(value % CREDENTIALS_WORDLIST_COUNT);
            return CREDENTIALS_OK;
        }
    }
}

static credentials_result_t credentials_append_word(
    char *out,
    size_t out_size,
    size_t *used,
    const char *word,
    bool prepend_separator)
{
    if (*used >= out_size) {
        return CREDENTIALS_ERR_OUTPUT_TOO_SMALL;
    }

    const int written = snprintf(
        out + *used,
        out_size - *used,
        prepend_separator ? " %s" : "%s",
        word);
    if (written < 0 || (size_t)written >= out_size - *used) {
        return CREDENTIALS_ERR_OUTPUT_TOO_SMALL;
    }
    *used += (size_t)written;
    return CREDENTIALS_OK;
}

static credentials_result_t credentials_generate_human_password_with_word_count(
    char *out,
    size_t out_size,
    size_t word_count,
    credentials_random_fn_t random_fn,
    void *random_ctx)
{
    if (out == NULL || out_size == 0 || word_count == 0 || random_fn == NULL) {
        return CREDENTIALS_ERR_INVALID_ARG;
    }

    const char *selected_words[CREDENTIALS_WIFI_PASSWORD_WORD_COUNT] = {0};
    if (word_count > CREDENTIALS_WIFI_PASSWORD_WORD_COUNT) {
        return CREDENTIALS_ERR_INVALID_ARG;
    }

    for (size_t i = 0; i < word_count; ++i) {
        size_t word_index = 0;
        const credentials_result_t result = credentials_select_word_index(random_fn, random_ctx, &word_index);
        if (result != CREDENTIALS_OK) {
            return result;
        }
        selected_words[i] = CREDENTIALS_WORDLIST[word_index];
    }

    size_t used = 0;
    for (size_t i = 0; i < word_count; ++i) {
        const credentials_result_t result = credentials_append_word(
            out,
            out_size,
            &used,
            selected_words[i],
            i != 0);
        if (result != CREDENTIALS_OK) {
            return result;
        }
    }
    return CREDENTIALS_OK;
}

credentials_result_t credentials_generate_human_password(
    char *out,
    size_t out_size,
    credentials_random_fn_t random_fn,
    void *random_ctx)
{
    const credentials_result_t result = credentials_generate_human_password_with_word_count(
        out,
        out_size,
        CREDENTIALS_WIFI_PASSWORD_WORD_COUNT,
        random_fn,
        random_ctx);
    if (result == CREDENTIALS_OK && !credentials_human_phrase_matches_policy(out, CREDENTIALS_WIFI_PASSWORD_WORD_COUNT)) {
        out[0] = '\0';
        return CREDENTIALS_ERR_INVALID_ARG;
    }
    return result;
}

bool credentials_human_phrase_matches_policy(const char *phrase, size_t word_count)
{
    if (phrase == NULL || phrase[0] == '\0' || word_count == 0) {
        return false;
    }

    size_t words = 1;
    bool previous_space = false;
    for (const char *p = phrase; *p != '\0'; ++p) {
        if (*p != ' ' && (*p < 'a' || *p > 'z')) {
            return false;
        }
        if (*p == ' ') {
            if (p == phrase || previous_space || p[1] == '\0') {
                return false;
            }
            words++;
            previous_space = true;
        } else {
            previous_space = false;
        }
    }
    return words == word_count;
}

bool credentials_compact_human_phrase(const char *phrase, size_t word_count, char *out, size_t out_size)
{
    if (!credentials_human_phrase_matches_policy(phrase, word_count) || out == NULL || out_size == 0) {
        return false;
    }

    size_t used = 0;
    for (const char *p = phrase; *p != '\0'; ++p) {
        if (*p == ' ') {
            continue;
        }
        if (used + 1U >= out_size) {
            return false;
        }
        out[used++] = *p;
    }
    out[used] = '\0';
    return used > 0;
}

static bool credentials_wifi_qr_append_escaped(const char *value, char *out, size_t out_size, size_t *used)
{
    if (value == NULL || out == NULL || used == NULL || *used >= out_size) {
        return false;
    }

    for (const char *p = value; *p != '\0'; ++p) {
        const bool escape = *p == '\\' || *p == ';' || *p == ',' || *p == ':' || *p == '"';
        const size_t needed = escape ? 2U : 1U;
        if (*used + needed >= out_size) {
            return false;
        }
        if (escape) {
            out[(*used)++] = '\\';
        }
        out[(*used)++] = *p;
    }
    out[*used] = '\0';
    return true;
}

static bool credentials_wifi_qr_append_literal(const char *literal, char *out, size_t out_size, size_t *used)
{
    if (literal == NULL || out == NULL || used == NULL || *used >= out_size) {
        return false;
    }
    for (const char *p = literal; *p != '\0'; ++p) {
        if (*used + 1U >= out_size) {
            return false;
        }
        out[(*used)++] = *p;
    }
    out[*used] = '\0';
    return true;
}

bool credentials_wifi_qr_payload(
    const char *ssid,
    const char *password,
    char *out,
    size_t out_size)
{
    if (ssid == NULL || ssid[0] == '\0' ||
        password == NULL || password[0] == '\0' ||
        out == NULL || out_size == 0) {
        return false;
    }

    char compact_password[128];
    const char *qr_password = password;
    if (credentials_compact_human_phrase(password, CREDENTIALS_WIFI_PASSWORD_WORD_COUNT, compact_password, sizeof(compact_password))) {
        qr_password = compact_password;
    }

    size_t used = 0;
    out[0] = '\0';
    /*
     * Many mobile WiFi QR parsers accept fields in any order, but the most
     * interoperable form starts with S before T. Keep this conservative
     * because provisioning must work from stock phone camera apps.
     */
    return credentials_wifi_qr_append_literal("WIFI:S:", out, out_size, &used) &&
           credentials_wifi_qr_append_escaped(ssid, out, out_size, &used) &&
           credentials_wifi_qr_append_literal(";T:WPA;P:", out, out_size, &used) &&
           credentials_wifi_qr_append_escaped(qr_password, out, out_size, &used) &&
           credentials_wifi_qr_append_literal(";;", out, out_size, &used);
}
