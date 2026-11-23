/* SPDX-License-Identifier: MIT
 * v42.0-49: bounded, event-driven RX exit after a validated target report.
 * No motor command or timer peripheral is touched by this pure policy.
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>

/* First bench threshold. Physical timing/power still requires HW-954 tests. */
#define P3_HW_RX_GOAL_SILENCE_MS 3000U

static inline bool p3_goal_silence_due(bool fresh_target_report,
                                      uint32_t last_rx_byte_age_ms,
                                      bool rx_ring_drained)
{
    return fresh_target_report &&
           rx_ring_drained &&
           last_rx_byte_age_ms >= P3_HW_RX_GOAL_SILENCE_MS;
}
