/* SPDX-License-Identifier: MIT
 * v42.0-46 passive, in-RAM per-command telemetry.
 * A relevant MCU report is NOT a protocol ACK nor proof of motor movement.
 * Only a fresh DP3 target match constitutes a reported position reached.
 * No UART output, motor retry, calibration or Zigbee traffic originates here.
 */
#ifndef P3_COMMAND_MONITOR_H_
#define P3_COMMAND_MONITOR_H_
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#define P3_CMD_MONITOR_TIMEOUT_MS 180000U
#define P3_CMD_TARGET_TOLERANCE_PERCENT 1U

enum p3_cmd_monitor_kind {
    P3_CMD_KIND_NONE = 0,
    P3_CMD_KIND_OPEN = 1,
    P3_CMD_KIND_STOP = 2,
    P3_CMD_KIND_CLOSE = 3,
    P3_CMD_KIND_POSITION = 4,
};

enum p3_cmd_monitor_result {
    P3_CMD_RESULT_NONE = 0,
    P3_CMD_RESULT_TARGET_REPORTED = 1,
    P3_CMD_RESULT_STOP_REPORTED = 2,
    P3_CMD_RESULT_TIMEOUT_NO_REPORT = 3,
    P3_CMD_RESULT_TIMEOUT_NO_TARGET = 4,
    P3_CMD_RESULT_SUPERSEDED = 5,
    P3_CMD_RESULT_TX_FAILED = 6,
};

struct p3_command_monitor {
    uint32_t queue_accepted;
    uint32_t tx_success;
    uint32_t tx_failed;
    uint32_t mcu_report_after_tx;
    uint32_t position_reached_reported;
    uint32_t stop_reported;
    uint32_t timed_out_no_report;
    uint32_t timed_out_no_target;
    uint32_t superseded;
    uint32_t last_sent_ms;
    uint32_t last_report_delay_ms;
    uint32_t last_result_ms;
    uint32_t since_ms;
    uint8_t last_kind;
    uint8_t last_target;
    uint8_t last_result;
    uint8_t pending_kind;
    uint8_t pending_target;
    bool pending;
    bool report_seen;
};

static inline bool p3_cmd_monitor_target_match(uint8_t target, uint8_t pos)
{
    if (target > 100U || pos > 100U) return false;
    const uint8_t diff = pos > target ? pos - target : target - pos;
    return diff <= P3_CMD_TARGET_TOLERANCE_PERCENT;
}

static inline void p3_cmd_monitor_expire(struct p3_command_monitor *m,
                                         uint32_t now)
{
    if (!m || !m->pending ||
        (uint32_t)(now - m->since_ms) < P3_CMD_MONITOR_TIMEOUT_MS) return;
    if (m->report_seen) {
        m->timed_out_no_target++;
        m->last_result = P3_CMD_RESULT_TIMEOUT_NO_TARGET;
    } else {
        m->timed_out_no_report++;
        m->last_result = P3_CMD_RESULT_TIMEOUT_NO_REPORT;
    }
    m->last_result_ms = now;
    m->pending = false;
}

static inline void p3_cmd_monitor_tx_completed(
    struct p3_command_monitor *m, uint8_t kind, uint8_t target,
    bool sent, uint32_t now)
{
    if (!m || kind == P3_CMD_KIND_NONE ||
        kind > P3_CMD_KIND_POSITION) return;
    p3_cmd_monitor_expire(m, now);
    if (!sent) {
        m->tx_failed++;
        m->last_result = P3_CMD_RESULT_TX_FAILED;
        m->last_result_ms = now;
        return;
    }
    m->tx_success++;
    if (m->pending) {
        m->superseded++;
        m->last_result = P3_CMD_RESULT_SUPERSEDED;
        m->last_result_ms = now;
    }
    m->pending = true;
    m->report_seen = false;
    m->since_ms = now;
    m->last_sent_ms = now;
    m->last_kind = kind;
    m->last_target = kind == P3_CMD_KIND_STOP ? 255U : target;
    m->pending_kind = kind;
    m->pending_target = target;
    m->last_report_delay_ms = UINT32_MAX;
    m->last_result = P3_CMD_RESULT_NONE;
}

/* Called ONLY for an accepted classic, parsed MCU report (cmd 0x07).
 * DP1/DP3 presence is validated by the caller, never stale cached state.
 */
static inline void p3_cmd_monitor_mcu_report(
    struct p3_command_monitor *m,
    bool has_dp1, uint8_t dp1,
    bool has_dp3, uint8_t dp3, uint32_t now)
{
    if (!m) return;
    p3_cmd_monitor_expire(m, now);
    if (!m->pending) return;
    if (!has_dp1 && !has_dp3) return;
    if (!m->report_seen) {
        m->mcu_report_after_tx++;
        m->report_seen = true;
        m->last_report_delay_ms = (uint32_t)(now - m->since_ms);
    }
    if (m->pending_kind == P3_CMD_KIND_STOP) {
        if (has_dp1 && dp1 == 1U) {
            m->stop_reported++;
            m->last_result = P3_CMD_RESULT_STOP_REPORTED;
            m->last_result_ms = now;
            m->pending = false;
        }
    } else if (has_dp3 &&
               p3_cmd_monitor_target_match(m->pending_target, dp3)) {
        m->position_reached_reported++;
        m->last_result = P3_CMD_RESULT_TARGET_REPORTED;
        m->last_result_ms = now;
        m->pending = false;
    }
}
#endif /* P3_COMMAND_MONITOR_H_ */
