/* SPDX-License-Identifier: MIT
 * v42.0-48: passive UARTE0 session ledger. It observes the existing RX
 * owner's begin/end events and accepted MCU reports. It never decides when
 * to keep hardware on, never transmits, and never changes motor state.
 */
#include "p3_session_diag.h"

#include <zephyr/kernel.h>
#include <string.h>

static struct k_spinlock ledger_lock;
static struct p3_session_snapshot current;
static struct p3_session_snapshot pending;
static struct p3_session_snapshot published;
static uint32_t next_id;
static bool session_active;
static bool pending_valid;
static uint32_t total_sessions;
static uint32_t stop_guard_exits;
static uint32_t watchdog_exits;
static uint32_t watchdog_goal_no_stop;
static uint32_t goal_silence_exits;

static void publish_locked(void)
{
    if (!pending_valid) return;
    published = pending;
    total_sessions++;
    if (pending.reason == P3_SESSION_EXIT_STOP_GUARD) stop_guard_exits++;
    if (pending.reason == P3_SESSION_EXIT_GOAL_SILENCE) goal_silence_exits++;
    if (pending.reason == P3_SESSION_EXIT_WATCHDOG) {
        watchdog_exits++;
        if (pending.goal_seen && !pending.stop_seen) watchdog_goal_no_stop++;
    }
    pending_valid = false;
}

void p3_session_diag_begin(void)
{
    k_spinlock_key_t key = k_spin_lock(&ledger_lock);
    /* The normal path publishes after the RX ring drains; if a second
     * session starts first, publish existing observations unchanged.
     */
    publish_locked();
    memset(&current, 0, sizeof(current));
    current.id = ++next_id;
    current.target = 255U;
    current.last_dp3 = 255U;
    session_active = true;
    k_spin_unlock(&ledger_lock, key);
}

void p3_session_diag_note_dma_bytes(uint32_t count)
{
    k_spinlock_key_t key = k_spin_lock(&ledger_lock);
    if (session_active) current.bytes += count;
    k_spin_unlock(&ledger_lock, key);
}

void p3_session_diag_note_valid_frame(void)
{
    k_spinlock_key_t key = k_spin_lock(&ledger_lock);
    /* UARTE DMA can finish and put its final packet in the RX ring BEFORE
     * the parser has consumed the frame. Count that in pending session.
     */
    if (session_active) current.valid_frames++;
    else if (pending_valid) pending.valid_frames++;
    k_spin_unlock(&ledger_lock, key);
}

void p3_session_diag_note_mcu_report(bool fresh_stop, bool dp3_valid, uint8_t dp3)
{
    k_spinlock_key_t key = k_spin_lock(&ledger_lock);
    struct p3_session_snapshot *s =
        session_active ? &current : (pending_valid ? &pending : NULL);
    if (s != NULL) {
        if (fresh_stop) s->stop_seen = 1U;
        if (dp3_valid && dp3 <= 100U) {
            s->last_dp3 = dp3;
            /* Strict equality; do not classify an unrelated cached DP3
             * or the last target of a previous hardware RX session.
             */
            if (s->target <= 100U && s->target == dp3) s->goal_seen = 1U;
        }
    }
    k_spin_unlock(&ledger_lock, key);
}

void p3_session_diag_note_tx_target(uint8_t target)
{
    k_spinlock_key_t key = k_spin_lock(&ledger_lock);
    if (session_active) {
        current.target = target <= 100U ? target : 255U;
        /* A newer successfully transmitted command supersedes an earlier
         * position target; do not attribute its old DP3 to this target.
         */
        current.goal_seen = 0U;
    }
    k_spin_unlock(&ledger_lock, key);
}

bool p3_session_diag_goal_ready(void)
{
    k_spinlock_key_t key = k_spin_lock(&ledger_lock);
    /* The parser has completed a valid MCU frame; a report from a prior
     * session, a different DP3 or a superseded TX cannot allow exit.
     */
    bool ready = session_active && current.valid_frames > 0U &&
        current.goal_seen != 0U && current.target <= 100U &&
        current.last_dp3 == current.target;
    k_spin_unlock(&ledger_lock, key);
    return ready;
}

void p3_session_diag_end(enum p3_session_exit_reason reason, uint32_t silence_ms)
{
    k_spinlock_key_t key = k_spin_lock(&ledger_lock);
    if (session_active) {
        publish_locked();
        pending = current;
        pending.reason = (uint32_t)reason;
        pending.silence_ms = silence_ms;
        pending_valid = true;
        session_active = false;
    }
    k_spin_unlock(&ledger_lock, key);
}

void p3_session_diag_publish_pending(void)
{
    k_spinlock_key_t key = k_spin_lock(&ledger_lock);
    publish_locked();
    k_spin_unlock(&ledger_lock, key);
}

void p3_session_diag_get(struct p3_session_snapshot *out)
{
    if (out == NULL) return;
    k_spinlock_key_t key = k_spin_lock(&ledger_lock);
    *out = published;
    out->active_id = session_active ? current.id : 0U;
    out->total_sessions = total_sessions;
    out->stop_guard_exits = stop_guard_exits;
    out->watchdog_exits = watchdog_exits;
    out->watchdog_goal_no_stop = watchdog_goal_no_stop;
    out->goal_silence_exits = goal_silence_exits;
    k_spin_unlock(&ledger_lock, key);
}
