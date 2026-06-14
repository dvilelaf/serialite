#include "local_pairing.h"

#include <string.h>

local_pairing_result_t local_pairing_generate_code(
    char out[LOCAL_PAIRING_CODE_BUF_LEN],
    local_pairing_random_fn_t random_fn,
    void *random_ctx)
{
    if (out == NULL || random_fn == NULL) {
        return LOCAL_PAIRING_ERR_INVALID_ARG;
    }

    uint8_t random_bytes[LOCAL_PAIRING_CODE_LEN];
    if (!random_fn(random_bytes, sizeof(random_bytes), random_ctx)) {
        memset(random_bytes, 0, sizeof(random_bytes));
        return LOCAL_PAIRING_ERR_RANDOM_FAILED;
    }

    for (size_t i = 0; i < LOCAL_PAIRING_CODE_LEN; ++i) {
        out[i] = (char)('0' + (random_bytes[i] % 10U));
    }
    out[LOCAL_PAIRING_CODE_LEN] = '\0';
    memset(random_bytes, 0, sizeof(random_bytes));
    return LOCAL_PAIRING_OK;
}

bool local_pairing_init(local_pairing_state_t *state, const char *code)
{
    if (state == NULL) {
        return false;
    }
    memset(state, 0, sizeof(*state));
    if (!local_pairing_code_format_valid(code)) {
        state->required = true;
        state->locked = true;
        return false;
    }
    memcpy(state->code, code, LOCAL_PAIRING_CODE_BUF_LEN);
    state->required = true;
    return true;
}

bool local_pairing_code_format_valid(const char *code)
{
    if (code == NULL) {
        return false;
    }
    for (size_t i = 0; i < LOCAL_PAIRING_CODE_LEN; ++i) {
        if (code[i] < '0' || code[i] > '9') {
            return false;
        }
    }
    return code[LOCAL_PAIRING_CODE_LEN] == '\0';
}

bool local_pairing_required(const local_pairing_state_t *state)
{
    return state != NULL && state->required;
}

bool local_pairing_locked(const local_pairing_state_t *state)
{
    return state != NULL && state->locked;
}

bool local_pairing_verify_and_consume(local_pairing_state_t *state, const char *candidate)
{
    if (state == NULL) {
        return false;
    }
    if (!state->required) {
        return true;
    }
    if (state->locked) {
        return false;
    }
    if (!local_pairing_code_format_valid(candidate) ||
        strncmp(state->code, candidate, LOCAL_PAIRING_CODE_BUF_LEN) != 0) {
        if (state->failed_attempts < LOCAL_PAIRING_MAX_ATTEMPTS) {
            state->failed_attempts++;
        }
        if (state->failed_attempts >= LOCAL_PAIRING_MAX_ATTEMPTS) {
            state->locked = true;
            memset(state->code, 0, sizeof(state->code));
        }
        return false;
    }
    memset(state->code, 0, sizeof(state->code));
    state->failed_attempts = 0;
    state->required = false;
    return true;
}
