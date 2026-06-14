#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define CREDENTIALS_ROTATED_REVEAL_MS 120000ULL
#define CREDENTIALS_WIFI_PASSWORD_WORD_COUNT 6U
#define CREDENTIALS_WEB_PASSWORD_WORD_COUNT 2U
#define CREDENTIALS_PASSWORD_WORD_COUNT CREDENTIALS_WIFI_PASSWORD_WORD_COUNT

typedef bool (*credentials_random_fn_t)(uint8_t *buf, size_t len, void *ctx);

typedef enum {
    CREDENTIALS_OK = 0,
    CREDENTIALS_ERR_INVALID_ARG,
    CREDENTIALS_ERR_RANDOM_FAILED,
    CREDENTIALS_ERR_OUTPUT_TOO_SMALL,
} credentials_result_t;

typedef enum {
    CREDENTIAL_ROTATION_ACCEPT = 0,
    CREDENTIAL_ROTATION_REJECT_NO_LOCAL_DISPLAY,
    CREDENTIAL_ROTATION_REJECT_PERSISTENCE_UNSAFE,
} credential_rotation_policy_result_t;

typedef struct {
    bool persisted_hash_configured;
    bool rtc_password_available;
} credentials_web_auth_boot_input_t;

typedef struct {
    bool use_persisted_hash;
    bool use_rtc_password;
    bool generate_runtime_password;
} credentials_web_auth_boot_decision_t;

credentials_result_t credentials_generate_human_password(
    char *out,
    size_t out_size,
    credentials_random_fn_t random_fn,
    void *random_ctx);
credentials_result_t credentials_generate_human_web_password(
    char *out,
    size_t out_size,
    credentials_random_fn_t random_fn,
    void *random_ctx);
bool credentials_human_phrase_matches_policy(const char *phrase, size_t word_count);
bool credentials_wifi_qr_payload(
    const char *ssid,
    const char *password,
    char *out,
    size_t out_size);
bool credentials_web_auth_boot_decide(
    const credentials_web_auth_boot_input_t *input,
    credentials_web_auth_boot_decision_t *decision);
credential_rotation_policy_result_t credential_rotation_policy_evaluate(
    bool local_display_ready,
    bool persistence_allowed);
const char *credential_rotation_policy_result_name(credential_rotation_policy_result_t result);
