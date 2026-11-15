/* SPDX-License-Identifier: MIT
 * v42.0-43: pure, host-testable safety predicates for the motor RX mode.
 * The direct RX watchdog must use physical session start time, never the
 * timestamp of repeated MCU DP1=OPEN/CLOSE reports.
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>

/* Unsigned subtraction is intentionally safe across k_uptime_get_32 wrap. */
static inline bool p3_hw_rx_timeout_due(uint32_t now_ms,
                                        uint32_t session_start_ms,
                                        uint32_t limit_ms)
{
    return (uint32_t)(now_ms - session_start_ms) >= limit_ms;
}

static inline bool p3_hw_rx_stop_guard_due(uint32_t now_ms,
                                           uint32_t first_stop_ms,
                                           uint32_t guard_ms)
{
    return first_stop_ms != 0U &&
           (uint32_t)(now_ms - first_stop_ms) >= guard_ms;
}

static inline bool p3_hw_rx_motion_transition(uint8_t old_dp1,
                                               uint8_t new_dp1,
                                               bool motion_lockout)
{
    return new_dp1 <= 2U && new_dp1 != 1U &&
           old_dp1 != new_dp1 && !motion_lockout;
}
