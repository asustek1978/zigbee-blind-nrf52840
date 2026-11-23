/* SPDX-License-Identifier: MIT
 * Host behavioral regression for passive hardware-UART session accounting.
 */
#include <assert.h>
#include <stdint.h>
#include "p3_session_diag.h"

static struct p3_session_snapshot snap(void)
{
    struct p3_session_snapshot s;
    p3_session_diag_get(&s);
    return s;
}

int main(void)
{
    struct p3_session_snapshot s = snap();
    assert(s.id == 0U && s.total_sessions == 0U && s.active_id == 0U);

    /* STOP guard: validated fresh STOP and a parser-valid frame. */
    p3_session_diag_begin();
    s = snap();
    assert(s.active_id == 1U);
    p3_session_diag_note_dma_bytes(18U);
    p3_session_diag_note_valid_frame();
    p3_session_diag_note_tx_target(100U);
    p3_session_diag_note_mcu_report(true, true, 100U);
    p3_session_diag_end(P3_SESSION_EXIT_STOP_GUARD, 1600U);
    /* DMA may have stopped before RX parser consumed the last packet. */
    p3_session_diag_note_valid_frame();
    p3_session_diag_publish_pending();
    s = snap();
    assert(s.id == 1U && s.bytes == 18U && s.valid_frames == 2U);
    assert(s.stop_seen == 1U && s.goal_seen == 1U);
    assert(s.target == 100U && s.last_dp3 == 100U);
    assert(s.reason == P3_SESSION_EXIT_STOP_GUARD && s.silence_ms == 1600U);
    assert(s.active_id == 0U && s.total_sessions == 1U);
    assert(s.stop_guard_exits == 1U && s.watchdog_goal_no_stop == 0U);

    /* The central hypothesis: target DP3 came but STOP was absent. */
    p3_session_diag_begin();
    p3_session_diag_note_tx_target(100U);
    p3_session_diag_note_dma_bytes(102U);
    p3_session_diag_note_valid_frame();
    p3_session_diag_note_mcu_report(false, true, 99U);
    s = snap();
    assert(s.active_id == 2U);
    p3_session_diag_note_mcu_report(false, true, 100U);
    p3_session_diag_end(P3_SESSION_EXIT_WATCHDOG, 110000U);
    p3_session_diag_publish_pending();
    s = snap();
    assert(s.id == 2U && s.bytes == 102U && s.valid_frames == 1U);
    assert(s.stop_seen == 0U && s.goal_seen == 1U);
    assert(s.last_dp3 == 100U && s.target == 100U);
    assert(s.reason == P3_SESSION_EXIT_WATCHDOG && s.silence_ms == 110000U);
    assert(s.watchdog_exits == 1U && s.watchdog_goal_no_stop == 1U);

    /* A new session cannot reuse the previous command's target. */
    p3_session_diag_begin();
    p3_session_diag_note_dma_bytes(5U);
    p3_session_diag_note_valid_frame();
    p3_session_diag_note_mcu_report(false, true, 100U);
    p3_session_diag_end(P3_SESSION_EXIT_WATCHDOG, 200U);
    p3_session_diag_publish_pending();
    s = snap();
    assert(s.id == 3U && s.target == 255U && s.goal_seen == 0U);
    assert(s.watchdog_exits == 2U && s.watchdog_goal_no_stop == 1U);

    /* Superseding CLOSE with OPEN must clear prior target success. */
    p3_session_diag_begin();
    p3_session_diag_note_tx_target(100U);
    p3_session_diag_note_mcu_report(false, true, 100U);
    p3_session_diag_note_tx_target(0U);
    p3_session_diag_end(P3_SESSION_EXIT_WATCHDOG, 999U);
    p3_session_diag_publish_pending();
    s = snap();
    assert(s.id == 4U && s.target == 0U && s.goal_seen == 0U);
    assert(s.watchdog_exits == 3U && s.watchdog_goal_no_stop == 1U);

    /* STOP command explicitly removes the pending target. */
    p3_session_diag_begin();
    p3_session_diag_note_tx_target(100U);
    p3_session_diag_note_tx_target(255U);
    p3_session_diag_note_mcu_report(true, true, 100U);
    p3_session_diag_end(P3_SESSION_EXIT_STOP_GUARD, 1500U);
    p3_session_diag_publish_pending();
    s = snap();
    assert(s.id == 5U && s.stop_seen == 1U && s.goal_seen == 0U);
    assert(s.target == 255U && s.total_sessions == 5U);
    assert(s.stop_guard_exits == 2U);

    /* v42.0-49: only a valid, live, latest-target DP3 can enable exit. */
    assert(!p3_session_diag_goal_ready());
    p3_session_diag_begin();
    assert(!p3_session_diag_goal_ready());
    p3_session_diag_note_tx_target(0U);
    p3_session_diag_note_valid_frame();
    p3_session_diag_note_mcu_report(false, true, 8U);
    assert(!p3_session_diag_goal_ready());
    p3_session_diag_note_mcu_report(false, true, 0U);
    assert(p3_session_diag_goal_ready());
    p3_session_diag_note_mcu_report(false, true, 2U);
    assert(!p3_session_diag_goal_ready());
    p3_session_diag_note_mcu_report(false, true, 0U);
    assert(p3_session_diag_goal_ready());
    p3_session_diag_note_tx_target(100U);
    assert(!p3_session_diag_goal_ready());
    p3_session_diag_note_mcu_report(false, true, 100U);
    assert(p3_session_diag_goal_ready());
    p3_session_diag_end(P3_SESSION_EXIT_GOAL_SILENCE, 3002U);
    assert(!p3_session_diag_goal_ready());
    p3_session_diag_publish_pending();
    s = snap();
    assert(s.id == 6U && s.reason == P3_SESSION_EXIT_GOAL_SILENCE);
    assert(s.target == 100U && s.last_dp3 == 100U && s.goal_seen == 1U);
    assert(s.goal_silence_exits == 1U && s.watchdog_exits == 3U);
    assert(s.stop_guard_exits == 2U && s.total_sessions == 6U);
    return 0;
}
