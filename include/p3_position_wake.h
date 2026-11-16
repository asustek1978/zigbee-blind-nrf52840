/* SPDX-License-Identifier: MIT
 * v42.0-45: pure, wrap-safe position evidence for a NEW motion episode.
 * Only validated, changed MCU DP3 may be passed here. No UART side effects.
 */
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define P3_POSITION_WAKE_QUIET_MS 10000U

struct p3_position_wake_state {
    bool initialized;
    uint8_t last_position;
    uint32_t last_change_ms;
};

/* Returns true only on a changed position after at least QUIET_MS without
 * another position change. The first observation initializes the baseline.
 * Caller separately requires watchdog lockout and a once-per-episode latch.
 */
static inline bool p3_position_wake_fresh_episode(
    struct p3_position_wake_state *state, uint8_t position, uint32_t now_ms)
{
    if (state == NULL || position > 100U) {
        return false;
    }
    if (!state->initialized) {
        state->initialized = true;
        state->last_position = position;
        state->last_change_ms = now_ms;
        return false;
    }
    if (position == state->last_position) {
        return false;
    }
    const bool quiet = (uint32_t)(now_ms - state->last_change_ms) >=
                       P3_POSITION_WAKE_QUIET_MS;
    state->last_position = position;
    state->last_change_ms = now_ms;
    return quiet;
}
