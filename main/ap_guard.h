#pragma once

/*
 * Stops the AP after AP_EXPOSURE_IDLE_TIMEOUT_MS without WiFi or web clients,
 * and closes terminal connections once the last WiFi client has left.
 */

#include <stdbool.h>

#include "esp_err.h"

esp_err_t ap_guard_start(void);
/* True once the guard has stopped the AP for inactivity. */
bool ap_guard_stopped_ap(void);
