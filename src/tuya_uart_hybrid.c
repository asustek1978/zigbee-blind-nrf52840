/* SPDX-License-Identifier: MIT
 * v42.0-36 EXPERIMENT ONLY: Tuya P3 Zigbee-to-UART motor commands
 * over independent, duty-cycled UARTE1 TX (P0.20, 9600 8N1).
 *
 * The proven v42.0-35 P0.17 GPIO/TIMER3 -> UARTE0 EasyDMA RX
 * remains untouched. No auto-TX on boot/join, retries, calibration,
 * MCU start-up queries, or periodic heartbeat.
 *
 * Requires physical connection P0.20 -> P3 motor RXD.
 */
#include "tuya_uart.h"
#include "tuya_frame.h"
#include "tuya_motor.h"
#include "tuya_motor_state.h"
#include "p3_soft_uart.h" /* passive RX motion-state classification only */
#include "p3_session_diag.h"

#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/devicetree.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/util.h>
#include <hal/nrf_gpio.h>
#include <hal/nrf_uarte.h>

#define P3_TX_PIN                20U
#define P3_TX_FRAME_CAP          32U
#define P3_TX_QUEUE_DEPTH        8U
#define P3_TX_FRAME_TIMEOUT_MS   65U
#define P3_TX_STOP_TIMEOUT_MS    25U
#define P3_PSEL_DISCONNECTED     0xFFFFFFFFUL
#define P3_UARTE_9600_REGISTER   0x00275000UL
#define P3_UARTE_ENABLE_REGISTER 8U

BUILD_ASSERT(DT_NODE_EXISTS(DT_NODELABEL(uart1)), "UARTE1 node required");
BUILD_ASSERT(!DT_NODE_HAS_STATUS(DT_NODELABEL(uart1), okay),
             "Zephyr UART1 driver must remain disabled: direct UARTE1 TX owns it");
BUILD_ASSERT(!(IS_ENABLED(CONFIG_P3_MCU_INIT_ENABLE) ||
               IS_ENABLED(CONFIG_P3_DIAG_STARTUP_QUERIES) ||
               IS_ENABLED(CONFIG_P3_HW_CALIBRATION_ENABLE)),
             "v42.0-36 forbids automatic UART traffic and calibration");

struct p3_tx_msg {
    uint8_t bytes[P3_TX_FRAME_CAP];
    uint8_t length;
    uint32_t queued_at_ms;
    uint32_t zigbee_at_ms;
    bool local_finish; /* v42.0-42: no effect on original motor payload */
    bool is_stop;
    uint8_t monitor_kind;
    uint8_t monitor_target;
};

K_MSGQ_DEFINE(p3_tx_queue, sizeof(struct p3_tx_msg), P3_TX_QUEUE_DEPTH, 4);
K_MUTEX_DEFINE(p3_state_mutex);
K_MUTEX_DEFINE(p3_diag_mutex);
static struct tuya_motor_state motor_state;
static struct tuya_uart_diagnostics p3_diag = {
    .last_command = 0xffU,
    .last_format = 0xffU,
};
static uint8_t tx_dma[P3_TX_FRAME_CAP] __aligned(4);
static atomic_t rx_ready;
static atomic_t rx_bytes_total;
static atomic_t pending_zigbee_timestamp;
static atomic_t response_pending;
static atomic_t last_tx_ms;

/* Defined by src/main.c, ensures ZCL state updates on the ZBOSS thread. */
extern void p3_zcl_schedule_sync(void);

/* One delayed, read-only timeout check per successful UART transmission.
 * No motor operation, no polling and no retry is performed by this work.
 */
static void p3_command_monitor_timeout_work(struct k_work *work)
{
    ARG_UNUSED(work);
    k_mutex_lock(&p3_diag_mutex, K_FOREVER);
    p3_cmd_monitor_expire(&p3_diag.command_monitor, k_uptime_get_32());
    k_mutex_unlock(&p3_diag_mutex);
    /* Publish timeout result through existing ZBOSS update path, no UART TX. */
    p3_zcl_schedule_sync();
}
K_WORK_DELAYABLE_DEFINE(p3_cmd_monitor_timeout, p3_command_monitor_timeout_work);

/* Inspect fresh, actually present DP1 / DP3 from validated MCU reports.
 * Do NOT count other DPs or the previously cached MCU state as responses.
 */
struct p3_command_report {
    bool dp1_present;
    bool dp3_present;
    uint8_t dp1;
    uint8_t dp3;
};
static void p3_command_report_capture(const struct tuya_dp *dp, void *user)
{
    struct p3_command_report *r = user;
    uint32_t val;
    if (!tuya_dp_as_uint32(dp, &val)) return;
    if (dp->id == P3_DP_MOTION && dp->type == 0x04U &&
        dp->length == 1U && val <= 2U) {
        r->dp1_present = true;
        r->dp1 = (uint8_t)val;
    } else if (dp->id == P3_DP_POSITION && dp->type == 0x02U &&
               dp->length == 4U && val <= 100U) {
        r->dp3_present = true;
        r->dp3 = (uint8_t)val;
    }
}

void tuya_uart_set_rx_ready(bool ready)
{
    atomic_set(&rx_ready, ready ? 1 : 0);
    p3_zcl_schedule_sync();
}

void tuya_uart_note_rx_byte(void)
{
    /* Keep RX hot path lock-free, especially during Zigbee activity. */
    atomic_inc(&rx_bytes_total);
}

/* Unlike a cached motor state, an ACTUAL received DP1 identifies a fresh
 * MCU motion report. Position/battery-only reports must not refresh STOP.
 */
struct p3_fresh_motion {
    bool present;
    uint8_t value;
};
static void p3_capture_fresh_motion(const struct tuya_dp *dp, void *user)
{
    struct p3_fresh_motion *motion = user;
    uint32_t value;
    if (dp->id == P3_DP_MOTION && dp->type == 0x04U &&
        dp->length == 1U && tuya_dp_as_uint32(dp, &value) &&
        value <= 2U) {
        motion->present = true;
        motion->value = (uint8_t)value;
    }
}

void tuya_uart_accept_report(const struct tuya_frame *frame)
{
    struct tuya_motor_changes changes = {0};
    bool accepted;
    uint8_t reported_motion = 0xffU;
    struct p3_fresh_motion fresh_motion = {0};
    struct p3_command_report command_report = {0};
    if (frame == NULL) {
        return;
    }
    k_mutex_lock(&p3_state_mutex, K_FOREVER);
    accepted = tuya_motor_apply_report(frame, &motor_state, &changes);
    if (accepted && motor_state.motion_valid) {
        reported_motion = motor_state.motion; /* actual last MCU DP1 */
    }
    k_mutex_unlock(&p3_state_mutex);
    /* Only a validated fresh DP1 drives the hardware-mode hold/release.
     * This is executed by the RX thread AFTER the frame was parsed.
     */
    if (accepted && reported_motion <= 2U &&
        tuya_foreach_report_dp(frame, p3_capture_fresh_motion, &fresh_motion) &&
        fresh_motion.present) {
        p3_soft_uart_set_mcu_motion(reported_motion);
    }
    /* A VALID changed MCU DP3 is independent evidence of physical
     * movement, including after a power watchdog suppressed stale DP1.
     * apply_report() sets changes.position only for an accepted DP3
     * different from the last known value. Never wake on repeated reports.
     */
    if (accepted && changes.position && motor_state.position_valid) {
        p3_soft_uart_note_changed_position(motor_state.position);
    }

    /* Existing RX path already validated this MCU report. The monitor
     * only observes DP1/DP3 fields and NEVER drives motor actions.
     */
    if (accepted) {
        (void)tuya_foreach_report_dp(frame, p3_command_report_capture,
                                    &command_report);
    }
    if (accepted) {
        /* Only freshly validated DP1/DP3 from this MCU report count.
         * No cached motor position and no command outcome is inferred.
         */
        p3_session_diag_note_mcu_report(
            command_report.dp1_present && command_report.dp1 == 1U,
            command_report.dp3_present, command_report.dp3);
    }
    bool monitor_progress = false;
    k_mutex_lock(&p3_diag_mutex, K_FOREVER);
    if (accepted) {
        const uint32_t report_before =
            p3_diag.command_monitor.mcu_report_after_tx;
        const uint32_t reached_before =
            p3_diag.command_monitor.position_reached_reported;
        const uint32_t stopped_before =
            p3_diag.command_monitor.stop_reported;
        p3_cmd_monitor_mcu_report(
            &p3_diag.command_monitor,
            command_report.dp1_present, command_report.dp1,
            command_report.dp3_present, command_report.dp3,
            k_uptime_get_32());
        monitor_progress =
            p3_diag.command_monitor.mcu_report_after_tx != report_before ||
            p3_diag.command_monitor.position_reached_reported != reached_before ||
            p3_diag.command_monitor.stop_reported != stopped_before;
    }
    p3_diag.valid_frames++;
    p3_diag.last_command = frame->command;
    p3_diag.last_format = (uint8_t)frame->format;
    if (accepted) {
        p3_diag.accepted_reports++;
    }
    if (atomic_cas(&response_pending, 1, 0)) {
        p3_diag.last_rx_after_tx_ms =
            k_uptime_get_32() - (uint32_t)atomic_get(&last_tx_ms);
    }
    k_mutex_unlock(&p3_diag_mutex);
    if (accepted && (changes.motion || changes.position ||
                     changes.battery || monitor_progress)) {
        p3_zcl_schedule_sync();
    }
}

void tuya_uart_set_rejected_count(uint32_t rejected)
{
    k_mutex_lock(&p3_diag_mutex, K_FOREVER);
    p3_diag.rejected_frames = rejected;
    k_mutex_unlock(&p3_diag_mutex);
}

void tuya_uart_get_state(struct tuya_motor_state *out)
{
    if (!out) {
        return;
    }
    k_mutex_lock(&p3_state_mutex, K_FOREVER);
    *out = motor_state;
    k_mutex_unlock(&p3_state_mutex);
}

void tuya_uart_get_diagnostics(struct tuya_uart_diagnostics *out)
{
    if (!out) {
        return;
    }
    k_mutex_lock(&p3_diag_mutex, K_FOREVER);
    *out = p3_diag;
    k_mutex_unlock(&p3_diag_mutex);
    out->rx_bytes = (uint32_t)atomic_get(&rx_bytes_total);
    out->rx_ready = (uint8_t)atomic_get(&rx_ready);
}

void tuya_uart_mark_zigbee_command(void)
{
    atomic_set(&pending_zigbee_timestamp, (atomic_val_t)k_uptime_get_32());
    k_mutex_lock(&p3_diag_mutex, K_FOREVER);
    p3_diag.zigbee_commands++;
    k_mutex_unlock(&p3_diag_mutex);
    /* v42.0-37: do NOT schedule a Zigbee report before queueing TX.
     * The ZCL callback is about to submit the motor command. A successful
     * k_msgq_put immediately wakes transmit_main's K_FOREVER waiter.
     * That TX worker schedules diagnostics after the hardware transfer.
     */
}

static bool queue_motor_frame(const uint8_t *bytes, size_t length,
                              bool is_stop, uint8_t kind, uint8_t target)
{
    struct p3_tx_msg msg = {0};
    if (!IS_ENABLED(CONFIG_P3_UART_TX_ENABLE) ||
        !atomic_get(&rx_ready) || bytes == NULL ||
        length < 7U || length > sizeof(msg.bytes) ||
        bytes[0] != 0x55U || bytes[1] != 0xaaU ||
        bytes[2] != 0x00U || bytes[3] != 0x06U) {
        return false;
    }

    memcpy(msg.bytes, bytes, length);
    msg.length = (uint8_t)length;
    msg.is_stop = is_stop;
    msg.monitor_kind = kind;
    msg.monitor_target = target;
    msg.queued_at_ms = k_uptime_get_32();
    msg.zigbee_at_ms = (uint32_t)atomic_set(&pending_zigbee_timestamp, 0);
    /* STOP cancels queued future movement. An already transmitting
     * UART frame is allowed to finish; it is not retransmitted.
     */
    if (is_stop) {
        k_msgq_purge(&p3_tx_queue);
    }
    /* If the command was accepted, request hardware listening BEFORE TX.
     * The TX thread independently waits for the RX owner's ready flag.
     */
    const bool accepted = k_msgq_put(&p3_tx_queue, &msg, K_NO_WAIT) == 0;
    if (accepted) {
        k_mutex_lock(&p3_diag_mutex, K_FOREVER);
        p3_diag.command_monitor.queue_accepted++;
        k_mutex_unlock(&p3_diag_mutex);
        p3_soft_uart_note_explicit_motor_command();
        p3_soft_uart_request_motor_rx();
    }
    return accepted;
}

bool tuya_uart_submit_action(enum tuya_motor_action action)
{
    uint8_t frame[P3_TX_FRAME_CAP];
    size_t n = tuya_motor_encode_action(action, frame, sizeof(frame));
    return queue_motor_frame(frame, n, action == TUYA_MOTOR_STOP,
        action == TUYA_MOTOR_OPEN ? P3_CMD_KIND_OPEN :
        (action == TUYA_MOTOR_CLOSE ? P3_CMD_KIND_CLOSE : P3_CMD_KIND_STOP),
        action == TUYA_MOTOR_OPEN ? 0U :
        (action == TUYA_MOTOR_CLOSE ? 100U : 255U));
}

bool tuya_uart_submit_position(uint8_t percent)
{
    uint8_t frame[P3_TX_FRAME_CAP];
    if (!IS_ENABLED(CONFIG_P3_POSITION_TARGET_ENABLE) || percent > 100U) {
        return false;
    }
    size_t n = tuya_motor_encode_position(percent, frame, sizeof(frame));
    return queue_motor_frame(frame, n, false, P3_CMD_KIND_POSITION, percent);
}

bool tuya_uart_submit_hw_calibration(uint8_t command)
{
    ARG_UNUSED(command);
    return false; /* Calibration remains disabled in the first TX trial. */
}
bool tuya_uart_submit_local_finish(void)
{
    return false;
}

/* Independent UARTE1 TX, no interaction with UARTE0 RX:
 * Zephyr driver for uart1 must be disabled in DeviceTree.
 * GPIO idles HIGH, which is the UART mark state, even when UARTE1 is off.
 */
static void p3_tx_pin_idle(void)
{
    NRF_UARTE1->ENABLE = 0U;
    NRF_UARTE1->PSEL.TXD = P3_PSEL_DISCONNECTED;
    nrf_gpio_pin_set(P3_TX_PIN);
    nrf_gpio_cfg_output(P3_TX_PIN);
}

static bool p3_transmit_one(const struct p3_tx_msg *frame)
{
    /* SRAM-backed EasyDMA buffer must remain stable until TXSTOPPED. */
    memcpy(tx_dma, frame->bytes, frame->length);

    NRF_UARTE1->ENABLE = 0U;
    NRF_UARTE1->PSEL.RXD = P3_PSEL_DISCONNECTED;
    NRF_UARTE1->PSEL.RTS = P3_PSEL_DISCONNECTED;
    NRF_UARTE1->PSEL.CTS = P3_PSEL_DISCONNECTED;
    NRF_UARTE1->CONFIG = 0U; /* no parity, no hardware flow control */
    NRF_UARTE1->BAUDRATE = P3_UARTE_9600_REGISTER;
    NRF_UARTE1->PSEL.TXD = P3_TX_PIN;
    NRF_UARTE1->TXD.PTR = (uint32_t)(uintptr_t)tx_dma;
    NRF_UARTE1->TXD.MAXCNT = frame->length;
    NRF_UARTE1->EVENTS_ENDTX = 0U;
    NRF_UARTE1->EVENTS_TXSTOPPED = 0U;
    NRF_UARTE1->ENABLE = P3_UARTE_ENABLE_REGISTER;
    NRF_UARTE1->TASKS_STARTTX = 1U;

    bool endtx = false;
    for (uint32_t ms = 0; ms < P3_TX_FRAME_TIMEOUT_MS; ms++) {
        if (NRF_UARTE1->EVENTS_ENDTX != 0U) {
            endtx = true;
            break;
        }
        k_msleep(1);
    }
    NRF_UARTE1->TASKS_STOPTX = 1U;
    bool stopped = false;
    for (uint32_t ms = 0; ms < P3_TX_STOP_TIMEOUT_MS; ms++) {
        if (NRF_UARTE1->EVENTS_TXSTOPPED != 0U) {
            stopped = true;
            break;
        }
        k_msleep(1);
    }
    p3_tx_pin_idle();
    return endtx && stopped && NRF_UARTE1->TXD.AMOUNT == frame->length;
}

static void transmit_main(void *a, void *b, void *c)
{
    ARG_UNUSED(a);
    ARG_UNUSED(b);
    ARG_UNUSED(c);
    /* No auto UART packets. Only an explicitly received ZCL command
     * can add a frame to the queue.
     */
    p3_tx_pin_idle();
    for (;;) {
        struct p3_tx_msg frame;
        if (k_msgq_get(&p3_tx_queue, &frame, K_FOREVER) != 0) {
            /* Queue purge (STOP priority) may wake a waiting consumer. */
            continue;
        }
        if (!IS_ENABLED(CONFIG_P3_UART_TX_ENABLE) ||
            !atomic_get(&rx_ready)) {
            continue;
        }
        /* Wake the RX owner before transmitting. A STOP has a shorter
         * deadline so a failed/late mode switch cannot delay braking.
         * On timeout preserve the pre-existing TX behavior and record it.
         */
        p3_soft_uart_request_motor_rx();
        bool hw_ready = p3_soft_uart_direct_rx_active();
        const uint32_t deadline_ms = frame.is_stop ? 20U : 80U;
        for (uint32_t waited = 0U; !hw_ready && waited < deadline_ms;
             waited += 2U) {
            k_msleep(2);
            hw_ready = p3_soft_uart_direct_rx_active();
        }
        if (!hw_ready) {
            p3_soft_uart_note_prearm_timeout();
        }
        const uint32_t started = k_uptime_get_32();
        bool completed = p3_transmit_one(&frame);
        const uint32_t done = k_uptime_get_32();
        k_mutex_lock(&p3_diag_mutex, K_FOREVER);
        p3_cmd_monitor_tx_completed(
            &p3_diag.command_monitor, frame.monitor_kind,
            frame.monitor_target, completed, done);
        if (completed) {
            p3_diag.tx_frames++;
            p3_diag.last_queue_to_tx_ms = started - frame.queued_at_ms;
            p3_diag.last_zigbee_to_tx_ms = frame.zigbee_at_ms ?
                started - frame.zigbee_at_ms : UINT32_MAX;
            p3_diag.last_uart_tx_ms = done - started;
            p3_diag.last_rx_after_tx_ms = UINT32_MAX;
        } else {
            p3_diag.dropped_bytes += frame.length;
        }
        k_mutex_unlock(&p3_diag_mutex);
        if (completed) {
            /* Track only a successfully transmitted explicit target.
             * STOP or a non-position command publishes 255 (unknown).
             */
            p3_session_diag_note_tx_target(frame.monitor_target);
            /* Passive deadline, not a motor watchdog or retry. */
            (void)k_work_reschedule(&p3_cmd_monitor_timeout,
                                    K_MSEC(P3_CMD_MONITOR_TIMEOUT_MS));
            atomic_set(&last_tx_ms, (atomic_val_t)done);
            atomic_set(&response_pending, 1);
        }
        p3_zcl_schedule_sync();
    }
}

K_THREAD_DEFINE(p3_tx_tid, 1536, transmit_main, NULL, NULL, NULL,
                K_PRIO_PREEMPT(8), 0, 0);
