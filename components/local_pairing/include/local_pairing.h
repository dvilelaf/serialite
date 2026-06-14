#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define LOCAL_PAIRING_CODE_LEN 6U
#define LOCAL_PAIRING_CODE_BUF_LEN (LOCAL_PAIRING_CODE_LEN + 1U)
#define LOCAL_PAIRING_MAX_ATTEMPTS 5U

typedef bool (*local_pairing_random_fn_t)(uint8_t *buf, size_t len, void *ctx);

typedef enum {
    LOCAL_PAIRING_OK = 0,
    LOCAL_PAIRING_ERR_INVALID_ARG,
    LOCAL_PAIRING_ERR_RANDOM_FAILED,
} local_pairing_result_t;

typedef struct {
    char code[LOCAL_PAIRING_CODE_BUF_LEN];
    size_t failed_attempts;
    bool required;
    bool locked;
} local_pairing_state_t;

local_pairing_result_t local_pairing_generate_code(
    char out[LOCAL_PAIRING_CODE_BUF_LEN],
    local_pairing_random_fn_t random_fn,
    void *random_ctx);
bool local_pairing_init(local_pairing_state_t *state, const char *code);
bool local_pairing_code_format_valid(const char *code);
bool local_pairing_required(const local_pairing_state_t *state);
bool local_pairing_locked(const local_pairing_state_t *state);
bool local_pairing_verify_and_consume(local_pairing_state_t *state, const char *candidate);
