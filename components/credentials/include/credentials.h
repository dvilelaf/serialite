#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Six words from a 7772-word list: about 77 bits. */
#define CREDENTIALS_WIFI_PASSWORD_WORD_COUNT 6U

typedef bool (*credentials_random_fn_t)(uint8_t *buf, size_t len, void *ctx);

typedef enum {
    CREDENTIALS_OK = 0,
    CREDENTIALS_ERR_INVALID_ARG,
    CREDENTIALS_ERR_RANDOM_FAILED,
    CREDENTIALS_ERR_OUTPUT_TOO_SMALL,
} credentials_result_t;

/* Space-separated lowercase words, picked without modulo bias. */
credentials_result_t credentials_generate_human_password(
    char *out,
    size_t out_size,
    credentials_random_fn_t random_fn,
    void *random_ctx);
/* True for exactly word_count lowercase words separated by single spaces. */
bool credentials_human_phrase_matches_policy(const char *phrase, size_t word_count);
/* The phrase with spaces removed: what the WiFi driver actually uses. */
bool credentials_compact_human_phrase(const char *phrase, size_t word_count, char *out, size_t out_size);
/* WIFI: QR payload for phone cameras, with the compacted passphrase. */
bool credentials_wifi_qr_payload(
    const char *ssid,
    const char *password,
    char *out,
    size_t out_size);
