/* SPDX-License-Identifier: MIT
 * v42.0-48: read-only per-session UARTE0 RX observability.
 * No motor commands, watchdog actions or mode-switch decisions here.
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>

/* Values are stable for Zigbee2MQTT; 0 means unknown/not set. */
enum p3_session_exit_reason {
    P3_SESSION_EXIT_NONE = 0,
    P3_SESSION_EXIT_STOP_GUARD = 1,
    P3_SESSION_EXIT_WATCHDOG = 2,
    P3_SESSION_EXIT_OTHER = 3,
    P3_SESSION_EXIT_SHUTDOWN = 4
};

struct p3_session_snapshot {
    uint32_t id;              /* most recent completed hardware RX session */
    uint32_t bytes;           /* copied from RXD.AMOUNT; includes bytes later dropped */
    uint32_t valid_frames;    /* parser-valid Tuya frames processed for session */
    uint32_t stop_seen;       /* fresh, accepted MCU DP1=STOP (0/1) */
    uint32_t goal_seen;       /* fresh, accepted MCU DP3 == transmitted target */
    uint32_t target;          /* 0..100, or 255 if no successful movement TX */
    uint32_t last_dp3;        /* 0..100, or 255 if none */
    uint32_t silence_ms;      /* elapsed since last observed UARTE RXDRDY at exit */
    uint32_t reason;          /* enum p3_session_exit_reason */
    uint32_t active_id;       /* currently running session, 0 if none */
    uint32_t total_sessions;
    uint32_t stop_guard_exits;
    uint32_t watchdog_exits;
    uint32_t watchdog_goal_no_stop; /* key control population for v42.0-49 */
};

/* Calls from RX owner thread, except note_tx_target from TX worker. */
void p3_session_diag_begin(void);
void p3_session_diag_note_dma_bytes(uint32_t count);
void p3_session_diag_note_valid_frame(void);
void p3_session_diag_note_mcu_report(bool fresh_stop, bool dp3_valid, uint8_t dp3);
void p3_session_diag_note_tx_target(uint8_t target);
void p3_session_diag_end(enum p3_session_exit_reason reason, uint32_t silence_ms);
void p3_session_diag_publish_pending(void);
void p3_session_diag_get(struct p3_session_snapshot *out);
