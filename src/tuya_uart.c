/* SPDX-License-Identifier: MIT */
/* Tuya P3 UART worker. UART TX is compile-time disabled by default. */
#include "tuya_frame.h"
#include "tuya_motor.h"
#include "tuya_motor_state.h"
#include "tuya_uart.h"

#include <string.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/util.h>

LOG_MODULE_REGISTER(tuya_p3, LOG_LEVEL_INF);

#define MOTOR_UART_NODE DT_ALIAS(motor_uart)
BUILD_ASSERT(DT_NODE_EXISTS(MOTOR_UART_NODE), "motor-uart alias required");

#define P3_TX_FRAME_CAP 32U
#define P3_TX_QUEUE_DEPTH 8U

static const struct device *const motor_uart = DEVICE_DT_GET(MOTOR_UART_NODE);

K_MSGQ_DEFINE(p3_rx_queue, sizeof(uint8_t), 256, 1);
K_MUTEX_DEFINE(p3_state_mutex);
static struct tuya_motor_state motor_state;
static atomic_t rx_dropped;
static atomic_t rx_dropped_total;
static atomic_t rx_bytes_total;
static atomic_t uart_ready;
static atomic_t pending_zigbee_timestamp;
static atomic_t last_tx_ms;
static atomic_t waiting_for_response;
static atomic_t diag_startup_requests_sent;
static atomic_t diag_startup_finished;
K_SEM_DEFINE(p3_uart_rx_started, 0, 1);
/* Diagnostic startup is mutually exclusive with general motor UART TX.
 * Even with this feature enabled, cover/position Zigbee commands are
 * rejected and cannot reach the TX worker.
 */
BUILD_ASSERT(!(IS_ENABLED(CONFIG_P3_DIAG_STARTUP_QUERIES) &&
               IS_ENABLED(CONFIG_P3_UART_TX_ENABLE)),
             "Diagnostic startup UART TX cannot coexist with motor movement TX");
BUILD_ASSERT(!(IS_ENABLED(CONFIG_P3_DIAG_STARTUP_QUERIES) &&
               IS_ENABLED(CONFIG_P3_MCU_INIT_ENABLE)),
             "Diagnostic startup UART TX cannot coexist with legacy MCU init");
K_MUTEX_DEFINE(p3_diag_mutex);
static struct tuya_uart_diagnostics p3_diag = {
    .last_command = 0xffU,
    .last_format = 0xffU,
};

struct p3_tx_msg {
    uint8_t bytes[P3_TX_FRAME_CAP];
    uint8_t length;
    uint32_t queued_at_ms;
    uint32_t zigbee_at_ms;
};

K_MSGQ_DEFINE(p3_tx_queue, sizeof(struct p3_tx_msg), P3_TX_QUEUE_DEPTH, 4);

/* Implemented in main.c: schedules ZCL changes on the Zigbee scheduler. */
extern void p3_zcl_schedule_sync(void);

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
    out->dropped_bytes = (uint32_t)atomic_get(&rx_dropped_total);
    out->rx_ready = (uint8_t)atomic_get(&uart_ready);
    out->diag_startup_requests_sent =
        (uint8_t)atomic_get(&diag_startup_requests_sent);
    out->diag_startup_finished =
        (uint8_t)atomic_get(&diag_startup_finished);
}

void tuya_uart_mark_zigbee_command(void)
{
    atomic_set(&pending_zigbee_timestamp, (atomic_val_t)k_uptime_get_32());
    k_mutex_lock(&p3_diag_mutex, K_FOREVER);
    p3_diag.zigbee_commands++;
    k_mutex_unlock(&p3_diag_mutex);
    p3_zcl_schedule_sync();
}

static bool queue_motor_frame(const uint8_t *bytes, size_t length, bool is_stop)
{
    struct p3_tx_msg msg = {0};

    if (!IS_ENABLED(CONFIG_P3_UART_TX_ENABLE) ||
        !atomic_get(&uart_ready) ||
        length == 0U || length > sizeof(msg.bytes)) {
        return false;
    }

    memcpy(msg.bytes, bytes, length);
    msg.length = (uint8_t)length;
    msg.queued_at_ms = k_uptime_get_32();
    msg.zigbee_at_ms = (uint32_t)atomic_set(&pending_zigbee_timestamp, 0);

    /* STOP drops any outstanding future requests; current transmission
     * cannot be interrupted byte-for-byte, but never retry/loop commands.
     */
    if (is_stop) {
        k_msgq_purge(&p3_tx_queue);
    }

    return k_msgq_put(&p3_tx_queue, &msg, K_NO_WAIT) == 0;
}

bool tuya_uart_submit_action(enum tuya_motor_action action)
{
    uint8_t encoded[P3_TX_FRAME_CAP];
    size_t count;

    if (!IS_ENABLED(CONFIG_P3_UART_TX_ENABLE)) {
        return false;
    }
    count = tuya_motor_encode_action(action, encoded, sizeof(encoded));
    return queue_motor_frame(encoded, count, action == TUYA_MOTOR_STOP);
}

bool tuya_uart_submit_position(uint8_t percent)
{
    uint8_t encoded[P3_TX_FRAME_CAP];
    size_t count;

    if (!IS_ENABLED(CONFIG_P3_UART_TX_ENABLE) ||
        !IS_ENABLED(CONFIG_P3_ZCL_POSITION_ENABLE)) {
        return false;
    }
    count = tuya_motor_encode_position(percent, encoded, sizeof(encoded));
    return queue_motor_frame(encoded, count, false);
}


bool tuya_uart_submit_hw_calibration(uint8_t command)
{
    uint8_t bytes[P3_TX_FRAME_CAP];
    size_t length;
    if (!IS_ENABLED(CONFIG_P3_HW_CALIBRATION_ENABLE)) return false;
    length = tuya_motor_encode_hw_calibration(command, bytes, sizeof(bytes));
    /* Reset/start purge stale position commands; finish must not be delayed. */
    return queue_motor_frame(bytes, length, true);
}

/* A WBR3 capture after physically waking the battery motor showed
 * heartbeat, product query, work-mode query, then DP status query.
 * No periodic heartbeat interval was established in that capture.
 * Startup transmission is a SEPARATE opt-in feature, disabled by default.
 * This never sends a motor movement command.
 */
static void enqueue_captured_startup(void)
{
    const enum tuya_motor_request requests[] = {
        TUYA_MOTOR_HEARTBEAT,
        TUYA_MOTOR_PRODUCT_QUERY,
        TUYA_MOTOR_WORK_MODE_QUERY,
        TUYA_MOTOR_STATE_QUERY
    };
    uint8_t bytes[P3_TX_FRAME_CAP];

    if (!IS_ENABLED(CONFIG_P3_MCU_INIT_ENABLE)) {
        return;
    }

    for (size_t i = 0; i < ARRAY_SIZE(requests); ++i) {
        size_t count = tuya_motor_encode_request(requests[i], bytes, sizeof(bytes));
        if (!queue_motor_frame(bytes, count, false)) {
            LOG_WRN("P3 startup request 0x%02x could not be queued", requests[i]);
            return;
        }
    }
    LOG_INF("P3 captured startup sequence queued (no movement)");
}

static void motor_uart_isr(const struct device *device, void *user_data)
{
    uint8_t data[32];
    int n;

    ARG_UNUSED(user_data);
    if (!uart_irq_update(device) || !uart_irq_rx_ready(device)) {
        return;
    }
    do {
        n = uart_fifo_read(device, data, sizeof(data));
        for (int i = 0; i < n; ++i) {
            if (k_msgq_put(&p3_rx_queue, &data[i], K_NO_WAIT) != 0) {
                atomic_inc(&rx_dropped);
                atomic_inc(&rx_dropped_total);
            }
        }
    } while (n > 0);
}

static void process_report(const struct tuya_frame *frame)
{
    struct tuya_motor_changes changes = {0};
    bool valid;

    k_mutex_lock(&p3_state_mutex, K_FOREVER);
    valid = tuya_motor_apply_report(frame, &motor_state, &changes);
    k_mutex_unlock(&p3_state_mutex);

    if (valid && (changes.position || changes.battery || changes.motion)) {
        LOG_INF("P3 report state changed: motion=%u lift=%u battery=%u",
                changes.motion, changes.position, changes.battery);
    }
}

/* Single consumer handles parsed frames; all statistics are diagnostic.
 * Never treat UART data as an acknowledgment of a completed movement.
 */
static void receive_main(void *a, void *b, void *c)
{
    struct tuya_parser parser;
    struct tuya_frame frame;
    uint8_t byte;
    int err;

    ARG_UNUSED(a);
    ARG_UNUSED(b);
    ARG_UNUSED(c);

    if (!device_is_ready(motor_uart)) {
        LOG_ERR("Tuya P3 UART not ready");
        return;
    }
    tuya_parser_init(&parser);
    err = uart_irq_callback_user_data_set(motor_uart, motor_uart_isr, NULL);
    if (err != 0) {
        LOG_ERR("Tuya P3 UART RX interrupt failed: %d", err);
        return;
    }
    uart_irq_rx_enable(motor_uart);
    atomic_set(&uart_ready, 1);
    k_sem_give(&p3_uart_rx_started);
    p3_zcl_schedule_sync();
    enqueue_captured_startup();
    LOG_INF("Tuya P3 9600 RX active; motor movement TX %s, startup diag %s",
            IS_ENABLED(CONFIG_P3_UART_TX_ENABLE) ? "enabled (prototype)" : "DISABLED",
            IS_ENABLED(CONFIG_P3_DIAG_STARTUP_QUERIES) ? "ONE TIME" : "disabled");

    for (;;) {
        uint32_t prior_rejected = parser.rejected;

        k_msgq_get(&p3_rx_queue, &byte, K_FOREVER);
        atomic_inc(&rx_bytes_total);
        if (tuya_parser_feed(&parser, byte, &frame)) {
            if (atomic_cas(&waiting_for_response, 1, 0)) {
                k_mutex_lock(&p3_diag_mutex, K_FOREVER);
                p3_diag.last_rx_after_tx_ms = k_uptime_get_32() - (uint32_t)atomic_get(&last_tx_ms);
                k_mutex_unlock(&p3_diag_mutex);
            }
            k_mutex_lock(&p3_diag_mutex, K_FOREVER);
            p3_diag.valid_frames++;
            p3_diag.last_command = frame.command;
            p3_diag.last_format = (uint8_t)frame.format;
            p3_diag.rejected_frames = parser.rejected;
            k_mutex_unlock(&p3_diag_mutex);

            LOG_DBG("P3 UART format=%u cmd=0x%02x len=%u",
                    (unsigned int)frame.format, frame.command, frame.length);
            process_report(&frame);
            /* Refresh read-only Zigbee diagnostics on the ZBOSS thread. */
            p3_zcl_schedule_sync();
        } else if (parser.rejected != prior_rejected) {
            k_mutex_lock(&p3_diag_mutex, K_FOREVER);
            p3_diag.rejected_frames = parser.rejected;
            k_mutex_unlock(&p3_diag_mutex);
            p3_zcl_schedule_sync();
        }
        if (atomic_get(&rx_dropped) > 0) {
            LOG_WRN("P3 UART RX overflow: %ld bytes dropped",
                    (long)atomic_set(&rx_dropped, 0));
        }
    }
}

static void transmit_main(void *a, void *b, void *c)
{
    struct p3_tx_msg frame;

    ARG_UNUSED(a);
    ARG_UNUSED(b);
    ARG_UNUSED(c);

    if (IS_ENABLED(CONFIG_P3_DIAG_STARTUP_QUERIES)) {
        uint8_t request[7];

        /* The four observed service frames are sent once after RX is
         * armed. No ZCL path can trigger TX and no command uses DP=1/2.
         */
        k_sem_take(&p3_uart_rx_started, K_FOREVER);
        for (size_t step = 0; step < TUYA_DIAGNOSTIC_STARTUP_STEPS; ++step) {
            const size_t count =
                tuya_motor_encode_diag_startup_step(step, request, sizeof(request));
            if (count != sizeof(request) ||
                request[0] != 0x55U || request[1] != 0xaaU ||
                request[2] != 0x00U || request[4] != 0x00U ||
                request[5] != 0x00U ||
                (request[3] != 0x00U && request[3] != 0x01U &&
                 request[3] != 0x02U && request[3] != 0x08U)) {
                LOG_ERR("P3 diagnostic TX allowlist violation; halted");
                break;
            }
            for (size_t i = 0; i < count; ++i) {
                uart_poll_out(motor_uart, request[i]);
            }
            atomic_inc(&diag_startup_requests_sent);
            p3_zcl_schedule_sync();
            /* Give the MCU time to reply; no retransmission or poll loop. */
            k_msleep(150);
        }
        atomic_set(&diag_startup_finished, 1);
        p3_zcl_schedule_sync();
        LOG_INF("P3 one-time diagnostic startup sent %ld/4 service frames",
                (long)atomic_get(&diag_startup_requests_sent));
        return; /* TX worker exits permanently after this attempt. */
    }

    if (!IS_ENABLED(CONFIG_P3_UART_TX_ENABLE)) {
        /* A permanently blocked TX worker must not wake for any reason. */
        return;
    }

    for (;;) {
        k_msgq_get(&p3_tx_queue, &frame, K_FOREVER);
        if (!atomic_get(&uart_ready)) {
            LOG_WRN("P3 TX skipped: UART not ready");
            continue;
        }

        const uint32_t tx_started = k_uptime_get_32();
        for (size_t i = 0; i < frame.length; ++i) {
            /* uart_poll_out() transmits a byte, without retransmission.
             * Original WBR3/P3 startup and acknowledgments require testing.
             */
            uart_poll_out(motor_uart, frame.bytes[i]);
        }

        const uint32_t tx_done = k_uptime_get_32();
        k_mutex_lock(&p3_diag_mutex, K_FOREVER);
        p3_diag.tx_frames++;
        p3_diag.last_zigbee_to_tx_ms = frame.zigbee_at_ms ?
            tx_started - frame.zigbee_at_ms : UINT32_MAX;
        p3_diag.last_queue_to_tx_ms = tx_started - frame.queued_at_ms;
        p3_diag.last_uart_tx_ms = tx_done - tx_started;
        p3_diag.last_rx_after_tx_ms = UINT32_MAX;
        k_mutex_unlock(&p3_diag_mutex);
        atomic_set(&last_tx_ms, (atomic_val_t)tx_done);
        atomic_set(&waiting_for_response, 1);
        p3_zcl_schedule_sync();
        LOG_INF("P3 tx completed: queue=%u ms uart=%u ms cmd=0x%02x (no MCU ack guaranteed)",
                (unsigned)(tx_started - frame.queued_at_ms),
                (unsigned)(tx_done - tx_started),
                frame.length > 3U ? frame.bytes[3] : 0U);
        LOG_DBG("P3 frame sent cmd=0x%02x (not confirmed by motor)",
                frame.length > 3U ? frame.bytes[3] : 0U);
    }
}

K_THREAD_DEFINE(tuya_rx_tid, 2048, receive_main, NULL, NULL, NULL,
                K_PRIO_PREEMPT(8), 0, 0);
K_THREAD_DEFINE(tuya_tx_tid, 1024, transmit_main, NULL, NULL, NULL,
                K_PRIO_PREEMPT(8), 0, 0);
