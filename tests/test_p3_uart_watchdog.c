/* SPDX-License-Identifier: MIT
 * v42.0-43: host tests of the SAME pure watchdog/transition predicates
 * included by firmware src/p3_soft_uart.c. Not a hardware ISR timing test.
 */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include "p3_uart_watchdog.h"

int main(void)
{
    const uint32_t session_started = 1000U;
    const uint32_t limit = 120000U;
    assert(!p3_hw_rx_timeout_due(session_started + limit - 1U,
                                  session_started, limit));
    assert(p3_hw_rx_timeout_due(session_started + limit,
                                session_started, limit));
    assert(p3_hw_rx_timeout_due(session_started + limit + 30000U,
                                session_started, limit));
    /* A recently repeated DP1 report (100000 ms) MUST NOT extend a
     * hardware session that started at 1000 ms.
     */
    const uint32_t repeated_dp1_ms = 100000U;
    const uint32_t now_ms = session_started + limit + 1U;
    assert(now_ms - repeated_dp1_ms < limit);
    assert(p3_hw_rx_timeout_due(now_ms, session_started, limit));
    /* STOP holdoff, timestamp fixed on FIRST fresh STOP only. */
    assert(!p3_hw_rx_stop_guard_due(1000U, 0U, 1500U));
    assert(!p3_hw_rx_stop_guard_due(2499U, 1000U, 1500U));
    assert(p3_hw_rx_stop_guard_due(2500U, 1000U, 1500U));
    /* Retransmitted identical CLOSE and OPEN reports must not re-arm. */
    assert(!p3_hw_rx_motion_transition(2U, 2U, false));
    assert(!p3_hw_rx_motion_transition(0U, 0U, false));
    assert(p3_hw_rx_motion_transition(1U, 2U, false));
    assert(p3_hw_rx_motion_transition(2U, 0U, false));
    assert(p3_hw_rx_motion_transition(3U, 0U, false));
    assert(!p3_hw_rx_motion_transition(2U, 0U, true));
    assert(!p3_hw_rx_motion_transition(1U, 2U, true));
    assert(!p3_hw_rx_motion_transition(2U, 1U, false));
    /* k_uptime_get_32 wraps without invalidating unsigned difference. */
    assert(!p3_hw_rx_timeout_due(1100U, UINT32_MAX - 1000U, 120000U));
    assert(p3_hw_rx_timeout_due(121000U, UINT32_MAX - 1000U, 120000U));
    return 0;
}
