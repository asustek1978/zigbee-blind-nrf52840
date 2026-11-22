/* SPDX-License-Identifier: MIT */
#include <assert.h>
#include <stdint.h>
#include "p3_command_monitor.h"
int main(void)
{
    struct p3_command_monitor m = {0};
    assert(p3_cmd_monitor_target_match(50, 49));
    assert(!p3_cmd_monitor_target_match(50, 48));
    assert(!p3_cmd_monitor_target_match(50, 255));
    m.queue_accepted++;
    p3_cmd_monitor_tx_completed(&m, P3_CMD_KIND_POSITION, 50, true, 1000);
    assert(m.pending && m.tx_success == 1);
    p3_cmd_monitor_mcu_report(&m, false, 0, false, 0, 1001);
    assert(!m.report_seen);
    p3_cmd_monitor_mcu_report(&m, true, 0, true, 20, 1040);
    assert(m.report_seen && m.mcu_report_after_tx == 1);
    assert(m.last_report_delay_ms == 40);
    p3_cmd_monitor_mcu_report(&m, false, 0, true, 49, 1200);
    assert(!m.pending && m.position_reached_reported == 1);
    assert(m.last_result == P3_CMD_RESULT_TARGET_REPORTED);
    p3_cmd_monitor_mcu_report(&m, false, 0, true, 50, 1300);
    assert(m.position_reached_reported == 1); /* no duplicate */
    p3_cmd_monitor_tx_completed(&m, P3_CMD_KIND_STOP, 255, true, 2000);
    p3_cmd_monitor_mcu_report(&m, true, 0, false, 0, 2100);
    assert(m.pending);
    p3_cmd_monitor_mcu_report(&m, true, 1, false, 0, 2150);
    assert(m.stop_reported == 1 && !m.pending);
    p3_cmd_monitor_tx_completed(&m, P3_CMD_KIND_OPEN, 0, true, 3000);
    p3_cmd_monitor_expire(&m, 3000 + P3_CMD_MONITOR_TIMEOUT_MS - 1);
    assert(m.pending);
    p3_cmd_monitor_expire(&m, 3000 + P3_CMD_MONITOR_TIMEOUT_MS);
    assert(m.timed_out_no_report == 1 && !m.pending);
    p3_cmd_monitor_tx_completed(&m, P3_CMD_KIND_CLOSE, 100, true, 200000);
    p3_cmd_monitor_mcu_report(&m, false, 0, true, 30, 200010);
    p3_cmd_monitor_expire(&m, 380000);
    assert(m.timed_out_no_target == 1);
    p3_cmd_monitor_tx_completed(&m, P3_CMD_KIND_POSITION, 40, true, 400000);
    p3_cmd_monitor_tx_completed(&m, P3_CMD_KIND_POSITION, 50, true, 400100);
    assert(m.superseded == 1 && m.pending && m.pending_target == 50);
    p3_cmd_monitor_tx_completed(&m, P3_CMD_KIND_STOP, 255, false, 400200);
    assert(m.tx_failed == 1 && m.pending); /* failed STOP cannot preempt */
    p3_cmd_monitor_mcu_report(&m, false, 0, true, 50, 400300);
    assert(m.position_reached_reported == 2);
    struct p3_command_monitor wrap = {0};
    p3_cmd_monitor_tx_completed(&wrap, P3_CMD_KIND_POSITION, 75, true, UINT32_MAX - 100);
    p3_cmd_monitor_expire(&wrap, 20);
    assert(wrap.pending);
    p3_cmd_monitor_expire(&wrap, P3_CMD_MONITOR_TIMEOUT_MS + 4);
    assert(!wrap.pending && wrap.timed_out_no_report == 1);
    return 0;
}
