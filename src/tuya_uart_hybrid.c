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
    bool local_finish;
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
static atomic_t pending_zigbee_timestamp;
static atomic_t response_pending;
static atomic_t last_tx_ms;

/* Defined by src/main.c, ensures ZCL state updates on the ZBOSS thread. */
extern void p3_zcl_schedule_sync(void);

void tuya_uart_set_rx_ready(bool ready)
{
    atomic_set(&rx_ready, ready ? 1 : 0);
    p3_zcl_schedule_sync();
}

void tuya_uart_note_rx_byte(void)
{
    k_mutex_lock(&p3_diag_mutex, K_FOREVER);
    p3_diag.rx_bytes++;
    k_mutex_unlock(&p3_diag_mutex);
}

void tuya_uart_accept_report(const struct tuya_frame *frame)
{
    struct tuya_motor_changes changes = {0};
    bool accepted;
    if (frame == NULL) {
        return;
    }
    k_mutex_lock(&p3_state_mutex, K_FOREVER);
    accepted = tuya_motor_apply_report(frame, &motor_state, &changes);
    k_mutex_unlock(&p3_state_mutex);

    k_mutex_lock(&p3_diag_mutex, K_FOREVER);
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
    if (accepted && (changes.motion || changes.position || changes.battery)) {
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
    out->rx_ready = (uint8_t)atomic_get(&rx_ready);
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
        !atomic_get(&rx_ready) || bytes == NULL ||
        length < 7U || length > sizeof(msg.bytes) ||
        bytes[0] != 0x55U || bytes[1] != 0xaaU ||
        bytes[2] != 0x00U || bytes[3] != 0x06U) {
        return false;
    }

    memcpy(msg.bytes, bytes, length);
    msg.length = (uint8_t)length;
    msg.queued_at_ms = k_uptime_get_32();
    msg.zigbee_at_ms = (uint32_t)atomic_set(&pending_zigbee_timestamp, 0);
    /* STOP cancels queued future movement. An already transmitting
     * UART frame is allowed to finish; it is not retransmitted.
     */
    if (is_stop) {
        k_msgq_purge(&p3_tx_queue);
    }
    return k_msgq_put(&p3_tx_queue, &msg, K_NO_WAIT) == 0;
}

bool tuya_uart_submit_action(enum tuya_motor_action action)
{
    uint8_t frame[P3_TX_FRAME_CAP];
    size_t n = tuya_motor_encode_action(action, frame, sizeof(frame));
    return queue_motor_frame(frame, n, action == TUYA_MOTOR_STOP);
}

bool tuya_uart_submit_position(uint8_t percent)
{
    uint8_t frame[P3_TX_FRAME_CAP];
    if (!IS_ENABLED(CONFIG_P3_POSITION_TARGET_ENABLE) || percent > 100U) {
        return false;
    }
    size_t n = tuya_motor_encode_position(percent, frame, sizeof(frame));
    return queue_motor_frame(frame, n, false);
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
        k_msgq_get(&p3_tx_queue, &frame, K_FOREVER);
        if (!IS_ENABLED(CONFIG_P3_UART_TX_ENABLE) ||
            !atomic_get(&rx_ready)) {
            continue;
        }
        const uint32_t started = k_uptime_get_32();
        bool completed = p3_transmit_one(&frame);
        const uint32_t done = k_uptime_get_32();
        k_mutex_lock(&p3_diag_mutex, K_FOREVER);
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
            atomic_set(&last_tx_ms, (atomic_val_t)done);
            atomic_set(&response_pending, 1);
        }
        p3_zcl_schedule_sync();
    }
}

K_THREAD_DEFINE(p3_tx_tid, 1536, transmit_main, NULL, NULL, NULL,
                K_PRIO_PREEMPT(8), 0, 0);
