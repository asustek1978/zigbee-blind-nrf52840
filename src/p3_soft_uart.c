/* SPDX-License-Identifier: MIT
 * v42.0-34 bench experiment: hybrid passive P3 RX, 9600 8N1.
 *
 * In low-power idle UARTE0 is DISABLED. GPIO P0.17 + TIMER3
 * decode the leading Tuya 0x55 byte. While TIMER3 samples the
 * first byte, UARTE0 is enabled but STARTRX is not triggered.
 * After a verified 0x55 stop bit, UARTE0 / EasyDMA take over
 * from the NEXT start bit (normally 0xAA), then stop after idle.
 *
 * The ~52 us UART handover was demonstrated on the user's HW-954
 * (v42.0-34/35); actual loss rate under motor activity is unproven.
 * Motor TX P0.20 is now physically connected and uses an INDEPENDENT
 * UARTE1 peripheral; this file owns only TIMER3 and UARTE0 RX.
 * Do not use as a production actuator until validated.
 */
#include <zephyr/kernel.h>
#include <errno.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/irq.h>
#include <zephyr/sys/ring_buffer.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/devicetree.h>
#include <hal/nrf_timer.h>
#include <hal/nrf_gpio.h>
#include <hal/nrf_uarte.h>
#include <helpers/nrfx_gppi.h>
#include "p3_soft_uart.h"
#include "p3_uart_watchdog.h"
#include "p3_position_wake.h"
#include "p3_session_diag.h"

#define P3_RX_PIN 17U
#define P3_RING_SIZE 2048U
#define P3_UART_DMA_SIZE 1024U
#define P3_BIT_US 104U
#define P3_FIRST_SAMPLE_US 156U
/* UARTE stays awake only while the frame is arriving, plus idle tail.
 * RXDRDY polling every 3 ms can extend this timeout by a few ms.
 */
#define P3_UART_IDLE_MS 15U
/* v42.0-42: bounded full UARTE0 RX during commanded or reported motion.
 * No periodic UARTE polling in battery idle; STOP is a fresh MCU DP1 only.
 */
#define P3_HW_RX_STOP_GUARD_MS 1500U
#define P3_HW_RX_MAX_MS 120000U
#define P3_UART_RXTO_WAIT_US 10000U
/* nRF52840 UARTE0: 9600 baud is 0x00275000 (Nordic PS register value). */
#define P3_UARTE_9600_REGISTER 0x00275000UL
#define P3_UARTE_ENABLE_REGISTER 8U
#define P3_PSEL_DISCONNECTED 0xFFFFFFFFUL

RING_BUF_DECLARE(p3_soft_ring, P3_RING_SIZE);
K_SEM_DEFINE(p3_soft_rx_sem, 0, 1);

static const struct device *const gpio = DEVICE_DT_GET(DT_NODELABEL(gpio0));
static struct gpio_callback rx_callback;
static uint8_t p3_dma_rx[P3_UART_DMA_SIZE] __aligned(4);
static atomic_t active;
static atomic_t uart_active;
static atomic_t framing_errors;
static atomic_t overflow_errors;
static atomic_t start_count;
static atomic_t uart_wake_count;
static atomic_t uart_dma_bytes;
static atomic_t uart_handoff_errors;
static atomic_t uart_hw_errors;
/* v42.0-39: read-only counters; no changes to IRQ timing or UART duty cycle. */
static atomic_t software_nonheader_bytes;
static atomic_t uart_hw_last_source;
/* v42.0-40: classify bad software STOP bits, never modify sampling timings.
 * DP1 is the last VALID report from the MCU: 0=open, 1=stop, 2=close.
 * 3=unknown until a valid DP1 is received; it is not a motion sensor.
 */
static atomic_t rx_reported_motion = ATOMIC_INIT(3);
static atomic_t rx_gpio_start_events;
static atomic_t rx_gpio_starts_reported_idle;
static atomic_t rx_gpio_starts_reported_moving;
static atomic_t rx_gpio_starts_motion_unknown;
static atomic_t rx_last_gpio_start_ms;
static atomic_t rx_bad_stop_gpio;
static atomic_t rx_bad_stop_chained;
static atomic_t rx_bad_stop_reported_idle;
static atomic_t rx_bad_stop_reported_moving;
static atomic_t rx_bad_stop_motion_unknown;
static atomic_t rx_last_bad_stop_ms;
static atomic_t rx_last_bad_stop_byte;
/* v42.0-41: passive error-path timestamps and reason breakdown only. */
static atomic_t rx_bad_stop_gpio_last_ms;
static atomic_t rx_bad_stop_chained_last_ms;
/* v42.0-44 passive idle-fault classification (error path ONLY). */
static atomic_t rx_idle_fault_episodes;
static atomic_t rx_bad_stop_post_exit_30s;
static atomic_t rx_bad_stop_late_or_no_exit;
static atomic_t rx_last_bad_stop_gpio_age_ms;
static atomic_t hw_direct_last_exit_ms;
static atomic_t rx_handoff_unprepared;
static atomic_t rx_handoff_rxto_timeout;
static atomic_t rx_handoff_amount_oversize;
static atomic_t rx_handoff_no_rxstarted;
static atomic_t rx_handoff_zero_amount;
static atomic_t rx_handoff_last_ms;
static atomic_t rx_handoff_last_reason;
static atomic_t rx_hw_framing_events;
static atomic_t rx_hw_overrun_events;
static atomic_t rx_hw_break_events;
static atomic_t rx_hw_parity_events;
static atomic_t rx_hw_last_ms;
static atomic_t rx_hw_last_amount;
/* v42.0-42: only the RX owner thread changes UARTE0/TIMER3 hardware.
 * ZBOSS and UARTE1 TX worker communicate through atomics and the RX semaphore.
 */
static atomic_t hw_rx_requested;
static atomic_t hw_rx_request_ms;
static atomic_t hw_rx_stop_ms;
/* v42.0-43: absolute session start never changes on repeated DP1 reports.
 * A timeout suppresses automatic re-arm until a fresh STOP or Zigbee command.
 */
static atomic_t hw_direct_session_start_ms;
static atomic_t hw_direct_motion_lockout;
/* v42.0-45: one position-confirmed new movement after watchdog may
 * re-arm full UARTE0 RX. Never re-arm from stale/repeated DP1 alone.
 * Position detector is updated from validated DP3 on the RX thread.
 */
static struct p3_position_wake_state p3_position_wake;
static atomic_t p3_position_wake_latched;
static atomic_t p3_position_wake_count;
static atomic_t p3_position_wake_suppressed;
static atomic_t p3_position_wake_last_ms;
static atomic_t hw_direct_active;
static atomic_t hw_direct_enters;
static atomic_t hw_direct_exits;
static atomic_t hw_direct_bytes;
static atomic_t hw_direct_restarts;
static atomic_t hw_direct_max_timeouts;
static atomic_t hw_direct_prearm_timeouts;
static atomic_t hw_direct_last_switch_ms;
static atomic_t hw_direct_hw_errors;
static atomic_t hw_direct_empty_dma;
static atomic_t hw_direct_rxto_timeouts;
/* v42.0-48 observation only: reason set by existing exit decisions. */
static atomic_t hw_session_exit_reason;
static bool hw_direct_saw_byte;
static volatile bool rx_origin_gpio;
static uint8_t shift_byte;
static uint8_t bit_index;
static bool uart_prepared;
static uint32_t uart_last_byte_ms;
static bool p3_ppi_available;
static bool p3_ppi_armed;
static uint8_t p3_ppi_ch;
static atomic_t p3_ppi_starts;
static atomic_t p3_ppi_fallbacks;
static atomic_t p3_ppi_late_irqs;

enum p3_rx_phase {
    P3_RX_DATA,
    P3_RX_STOP,
    P3_RX_NEXT_START,
};
static volatile enum p3_rx_phase rx_phase;

/* v42.0-48: reuse the IN event owned by Zephyr's GPIO driver.
 * The GPIOTE channel is never reconfigured or claimed by this code.
 * If a dedicated IN event is unavailable, keep the proven GPIO-ISR path.
 */
static void p3_ppi_setup(void)
{
    for (unsigned int i = 0; i < ARRAY_SIZE(NRF_GPIOTE->CONFIG); i++) {
        uint32_t c = NRF_GPIOTE->CONFIG[i];
        if (((c & GPIOTE_CONFIG_MODE_Msk) >> GPIOTE_CONFIG_MODE_Pos) != GPIOTE_CONFIG_MODE_Event ||
            ((c & GPIOTE_CONFIG_PSEL_Msk) >> GPIOTE_CONFIG_PSEL_Pos) != P3_RX_PIN ||
            ((c & GPIOTE_CONFIG_POLARITY_Msk) >> GPIOTE_CONFIG_POLARITY_Pos) != GPIOTE_CONFIG_POLARITY_HiToLo) continue;
        uint8_t ch;
        if (nrfx_gppi_channel_alloc(&ch) != NRFX_SUCCESS) return;
        nrfx_gppi_channel_endpoints_setup(ch,
            (uint32_t)(uintptr_t)&NRF_GPIOTE->EVENTS_IN[i],
            (uint32_t)(uintptr_t)&NRF_TIMER3->TASKS_START);
        p3_ppi_ch = ch;
        p3_ppi_available = true;
        return;
    }
}
static void p3_ppi_disarm(void)
{
    /* Protect the channel state against GPIO IRQ preemption while the RX
     * owner thread re-arms the idle listener. On a single-core nRF52 this
     * short critical section serializes hardware enable and software flag.
     */
    unsigned int key = irq_lock();
    if (p3_ppi_available && p3_ppi_armed) {
        nrfx_gppi_channels_disable(BIT(p3_ppi_ch));
        p3_ppi_armed = false;
    }
    irq_unlock(key);
}
static void p3_ppi_arm(void)
{
    unsigned int key = irq_lock();
    if (p3_ppi_available && !p3_ppi_armed) {
        /* Reset the stopped timer before edge. PPI does only START. */
        nrf_timer_task_trigger(NRF_TIMER3, NRF_TIMER_TASK_CLEAR);
        nrf_timer_event_clear(NRF_TIMER3, NRF_TIMER_EVENT_COMPARE0);
        nrf_timer_cc_set(NRF_TIMER3, NRF_TIMER_CC_CHANNEL0, P3_FIRST_SAMPLE_US);
        nrfx_gppi_channels_enable(BIT(p3_ppi_ch));
        p3_ppi_armed = true;
    }
    irq_unlock(key);
}

static void p3_uart_disable(void)
{
    nrf_uarte_disable(NRF_UARTE0);
    uart_prepared = false;
}

/* Configuration is prepared at boot but the receive peripheral remains
 * disabled and has no DMA operation while P3 is quiet.
 * UART0 must remain Zephyr deferred-init; its driver cannot also own it.
 */
static void p3_uarte_prepare_registers(void)
{
    NRF_UARTE0->INTENCLR = 0xFFFFFFFFUL;
    nrf_uarte_disable(NRF_UARTE0);
    NRF_UARTE0->PSEL.TXD = P3_PSEL_DISCONNECTED;
    NRF_UARTE0->PSEL.RTS = P3_PSEL_DISCONNECTED;
    NRF_UARTE0->PSEL.CTS = P3_PSEL_DISCONNECTED;
    NRF_UARTE0->PSEL.RXD = P3_RX_PIN;
    NRF_UARTE0->CONFIG = 0U; /* parity excluded, HWFC disabled */
    NRF_UARTE0->BAUDRATE = P3_UARTE_9600_REGISTER;
    NRF_UARTE0->RXD.PTR = (uint32_t)(uintptr_t)p3_dma_rx;
    NRF_UARTE0->RXD.MAXCNT = sizeof(p3_dma_rx);
    NRF_UARTE0->EVENTS_RXSTARTED = 0U;
    NRF_UARTE0->EVENTS_RXDRDY = 0U;
    NRF_UARTE0->EVENTS_ENDRX = 0U;
    NRF_UARTE0->EVENTS_RXTO = 0U;
    NRF_UARTE0->EVENTS_ERROR = 0U;
}

/* Give hardware >0.8 ms to power up while TIMER3 reconstructs 0x55.
 * Do not start RX yet: a falling data bit within 0x55 could be mistaken
 * for a UART start bit and corrupt the following 0xAA byte.
 */
static void p3_uart_warmup(void)
{
    if (!uart_prepared && !atomic_get(&uart_active)) {
        NRF_UARTE0->RXD.PTR = (uint32_t)(uintptr_t)p3_dma_rx;
        NRF_UARTE0->RXD.MAXCNT = sizeof(p3_dma_rx);
        NRF_UARTE0->EVENTS_RXSTARTED = 0U;
        NRF_UARTE0->EVENTS_RXDRDY = 0U;
        NRF_UARTE0->EVENTS_ENDRX = 0U;
        NRF_UARTE0->EVENTS_RXTO = 0U;
        NRF_UARTE0->EVENTS_ERROR = 0U;
        NRF_UARTE0->ENABLE = P3_UARTE_ENABLE_REGISTER;
        uart_prepared = true;
    }
}

/* A fresh accepted Zigbee motor command or MCU DP1=OPEN/CLOSE activates
 * direct UARTE RX. Release is allowed only after a NEW verified MCU STOP
 * plus guard, or a hard 120 s power watchdog (recorded separately).
 * All operations here are atomic; never touch Nordic peripherals elsewhere.
 */
static bool p3_hw_rx_wanted(uint32_t now)
{
    if (!atomic_get(&hw_rx_requested)) {
        return false;
    }
    /* An ACTIVE direct-RX session has an absolute deadline established
     * once, after the first verified RXSTARTED. MCU DP1=CLOSE/OPEN repeats
     * and additional commands never move that deadline.
     */
    const bool direct = atomic_get(&hw_direct_active) != 0;
    const uint32_t anchor_ms = (uint32_t)atomic_get(
        direct ? &hw_direct_session_start_ms : &hw_rx_request_ms);
    if (p3_hw_rx_timeout_due(now, anchor_ms, P3_HW_RX_MAX_MS)) {
        if (atomic_cas(&hw_rx_requested, 1, 0)) {
            if (direct) {
                atomic_inc(&hw_direct_max_timeouts);
                atomic_set(&hw_direct_motion_lockout, 1);
                atomic_set(&hw_session_exit_reason, P3_SESSION_EXIT_WATCHDOG);
            }
        }
        return false;
    }
    const uint32_t stop_ms = (uint32_t)atomic_get(&hw_rx_stop_ms);
    if (p3_hw_rx_stop_guard_due(now, stop_ms, P3_HW_RX_STOP_GUARD_MS)) {
        if (direct) atomic_set(&hw_session_exit_reason, P3_SESSION_EXIT_STOP_GUARD);
        atomic_clear(&hw_rx_requested);
        return false;
    }
    return true;
}

static void p3_hw_rx_maybe_enter(void)
{
    if (atomic_get(&hw_direct_active) || atomic_get(&uart_active) ||
        !p3_hw_rx_wanted(k_uptime_get_32())) {
        return;
    }
    /* GPIO ISR wins if it already started software sampling. In that case
     * finish the incoming frame first, then retry the transition.
     */
    if (!atomic_cas(&active, 0, 1)) {
        return;
    }
    p3_ppi_disarm();
    (void)gpio_pin_interrupt_configure(gpio, P3_RX_PIN, GPIO_INT_DISABLE);
    nrf_timer_task_trigger(NRF_TIMER3, NRF_TIMER_TASK_STOP);
    nrf_timer_int_disable(NRF_TIMER3, NRF_TIMER_INT_COMPARE0_MASK);
    p3_uart_warmup();
    /* Allow the peripheral to settle before arming raw 9600 baud RX.
     * This happens in the RX thread, never inside TIMER3 IRQ.
     */
    k_busy_wait(100);
    hw_direct_saw_byte = false;
    uart_last_byte_ms = k_uptime_get_32();
    NRF_UARTE0->TASKS_STARTRX = 1U;
    /* Do not tell the TX worker "ready" until the peripheral confirms
     * RXSTARTED. A failed arm must fall back safely to the hybrid listener.
     */
    bool started = false;
    for (uint32_t us = 0U; us < 2000U; us += 20U) {
        if (NRF_UARTE0->EVENTS_RXSTARTED != 0U) {
            started = true;
            break;
        }
        k_busy_wait(20);
    }
    if (!started) {
        NRF_UARTE0->EVENTS_RXTO = 0U;
        NRF_UARTE0->TASKS_STOPRX = 1U;
        for (uint32_t us = 0U; us < P3_UART_RXTO_WAIT_US; us += 10U) {
            if (NRF_UARTE0->EVENTS_RXTO != 0U) break;
            k_busy_wait(10);
        }
        p3_uart_disable();
        atomic_clear(&hw_rx_requested);
        atomic_clear(&active);
        (void)gpio_pin_interrupt_configure(gpio, P3_RX_PIN, GPIO_INT_EDGE_FALLING);
        return;
    }
    /* The hard watchdog begins at physical RX activation, not at the
     * last received DP1 report or TX request.
     */
    atomic_set(&hw_direct_session_start_ms, (atomic_val_t)k_uptime_get_32());
    atomic_set(&hw_session_exit_reason, P3_SESSION_EXIT_OTHER);
    p3_session_diag_begin();
    atomic_set(&uart_active, 1);
    atomic_set(&hw_direct_active, 1);
    atomic_inc(&hw_direct_enters);
    atomic_set(&hw_direct_last_switch_ms, (atomic_val_t)k_uptime_get_32());
}

/* Called only on a handoff failure. Reasons 1..5 are mutually exclusive.
 * No extra operations are added to the successful ~52 us DMA handoff path.
 */
static void p3_note_handoff_error(atomic_t *reason_counter, uint32_t reason)
{
    atomic_inc(&uart_handoff_errors);
    atomic_inc(reason_counter);
    atomic_set(&rx_handoff_last_reason, (atomic_val_t)reason);
    atomic_set(&rx_handoff_last_ms, (atomic_val_t)k_uptime_get_32());
}

static void p3_uart_begin_dma(void)
{
    /* Called at software stop bit centre (9.5 UART bit intervals).
     * Only ~52 us until next start bit. Checking RXSTARTED later
     * is diagnostic; successful byte capture MUST be measured.
     */
    if (!uart_prepared) {
        p3_note_handoff_error(&rx_handoff_unprepared, 1U);
        return;
    }
    nrf_timer_task_trigger(NRF_TIMER3, NRF_TIMER_TASK_STOP);
    nrf_timer_int_disable(NRF_TIMER3, NRF_TIMER_INT_COMPARE0_MASK);
    (void)gpio_pin_interrupt_configure(gpio, P3_RX_PIN,
                                       GPIO_INT_DISABLE);
    atomic_set(&uart_active, 1);
    uart_last_byte_ms = k_uptime_get_32();
    atomic_inc(&uart_wake_count);
    NRF_UARTE0->TASKS_STARTRX = 1U;
}

/* v42.0-47: abort only the malformed software STOP-bit path. GPIO's
 * falling-edge interrupt remains configured throughout software reception;
 * rx_start_irq rejects edges while active=1. After an invalid STOP bit we
 * must relinquish the TIMER3 sampler rather than chaining speculative bytes.
 * No UARTE RX DMA is started and no polling is added to battery idle.
 * If RX is still LOW, a future HIGH->LOW edge can re-enter via GPIO.
 */
static void p3_soft_uart_abort_bad_stop(void)
{
    nrf_timer_task_trigger(NRF_TIMER3, NRF_TIMER_TASK_STOP);
    nrf_timer_int_disable(NRF_TIMER3, NRF_TIMER_INT_COMPARE0_MASK);
    nrf_timer_event_clear(NRF_TIMER3, NRF_TIMER_EVENT_COMPARE0);
    if (uart_prepared) {
        p3_uart_disable();
    }
    rx_phase = P3_RX_DATA;
    shift_byte = 0U;
    bit_index = 0U;
    p3_ppi_arm();
    /* Clear ownership LAST so a fresh GPIO edge cannot race our reset. */
    atomic_clear(&active);
}

static void timer3_irq(const void *unused)
{
    ARG_UNUSED(unused);
    if (!nrf_timer_event_check(NRF_TIMER3, NRF_TIMER_EVENT_COMPARE0)) {
        return;
    }
    nrf_timer_event_clear(NRF_TIMER3, NRF_TIMER_EVENT_COMPARE0);
    const bool level = nrf_gpio_pin_read(P3_RX_PIN) != 0;

    if (rx_phase == P3_RX_DATA) {
        if (bit_index == 0U) {
            atomic_inc(&start_count);
            p3_uart_warmup();
        }
        if (level) {
            shift_byte |= (uint8_t)(1U << bit_index);
        }
        bit_index++;
        if (bit_index == 8U) {
            rx_phase = P3_RX_STOP;
        }
        nrf_timer_cc_set(NRF_TIMER3, NRF_TIMER_CC_CHANNEL0,
                         P3_BIT_US);
        return;
    }

    if (rx_phase == P3_RX_STOP) {
        if (level) {
            if (ring_buf_put(&p3_soft_ring, &shift_byte, 1U) != 1U) {
                atomic_inc(&overflow_errors);
            } else {
                k_sem_give(&p3_soft_rx_sem);
            }
            /* Tuya 0x55 is the first header byte. UARTE now takes RX,
             * starting during its stop bit rather than inside data.
             */
            if (shift_byte == 0x55U) {
                p3_uart_begin_dma();
                return;
            }
            /* Count completed software bytes that are not a Tuya header,
             * without filtering or changing the proven receiver path.
             */
            atomic_inc(&software_nonheader_bytes);
        } else {
            /* Error-path-only sampling: classify before the existing
             * resynchronization behavior, leaving the 0x55 handoff fast.
             */
            const atomic_val_t error_ms = (atomic_val_t)k_uptime_get_32();
            /* Group tightly consecutive software STOP-bit errors into
             * episodes, without changing existing resynchronization.
             * This runs only on the already-slow error path.
             */
            const uint32_t previous_error_ms =
                (uint32_t)atomic_get(&rx_last_bad_stop_ms);
            if (previous_error_ms == 0U ||
                (uint32_t)(error_ms - previous_error_ms) > 250U) {
                atomic_inc(&rx_idle_fault_episodes);
            }
            const uint32_t last_exit_ms =
                (uint32_t)atomic_get(&hw_direct_last_exit_ms);
            if (last_exit_ms != 0U &&
                (uint32_t)(error_ms - last_exit_ms) <= 30000U) {
                atomic_inc(&rx_bad_stop_post_exit_30s);
            } else {
                atomic_inc(&rx_bad_stop_late_or_no_exit);
            }
            atomic_set(&rx_last_bad_stop_gpio_age_ms,
                       error_ms - atomic_get(&rx_last_gpio_start_ms));
            atomic_inc(&framing_errors);
            if (rx_origin_gpio) {
                atomic_inc(&rx_bad_stop_gpio);
                atomic_set(&rx_bad_stop_gpio_last_ms, error_ms);
            } else {
                atomic_inc(&rx_bad_stop_chained);
                atomic_set(&rx_bad_stop_chained_last_ms, error_ms);
            }
            const atomic_val_t motion = atomic_get(&rx_reported_motion);
            if (motion == 1) {
                atomic_inc(&rx_bad_stop_reported_idle);
            } else if (motion == 0 || motion == 2) {
                atomic_inc(&rx_bad_stop_reported_moving);
            } else {
                atomic_inc(&rx_bad_stop_motion_unknown);
            }
            atomic_set(&rx_last_bad_stop_byte, (atomic_val_t)shift_byte);
            atomic_set(&rx_last_bad_stop_ms, error_ms);
            /* v42.0-47: do not chain after an invalid STOP bit. */
            p3_soft_uart_abort_bad_stop();
            return;
        }
        /* No 0x55 preamble: continue software sampling and keep
         * the hardware peripheral completely powered down.
         */
        if (uart_prepared) {
            p3_uart_disable();
        }
        rx_phase = P3_RX_NEXT_START;
        nrf_timer_cc_set(NRF_TIMER3, NRF_TIMER_CC_CHANNEL0,
                         P3_BIT_US);
        return;
    }

    if (!level) {
        /* Back-to-back software byte: already at middle of start bit. */
        shift_byte = 0U;
        bit_index = 0U;
        rx_phase = P3_RX_DATA;
        rx_origin_gpio = false; /* follow-on byte, not GPIO wake */
        nrf_timer_cc_set(NRF_TIMER3, NRF_TIMER_CC_CHANNEL0,
                         P3_BIT_US);
        return;
    }

    /* Idle while no valid preamble: return to GPIO edge detection. */
    nrf_timer_task_trigger(NRF_TIMER3, NRF_TIMER_TASK_STOP);
    nrf_timer_int_disable(NRF_TIMER3, NRF_TIMER_INT_COMPARE0_MASK);
    if (uart_prepared) {
        p3_uart_disable();
    }
    p3_ppi_arm();
    atomic_clear(&active);
}

static void rx_start_irq(const struct device *port,
                         struct gpio_callback *cb, gpio_port_pins_t pins)
{
    ARG_UNUSED(port);
    ARG_UNUSED(cb);
    ARG_UNUSED(pins);
    if (atomic_get(&uart_active) ||
        !atomic_cas(&active, 0, 1)) {
        return;
    }
    shift_byte = 0U;
    bit_index = 0U;
    rx_phase = P3_RX_DATA;
    rx_origin_gpio = true; /* tag byte whose start was a GPIO edge */
    const bool ppi_was_armed = p3_ppi_armed;
    p3_ppi_disarm(); /* Disarm before any further falling data-bit edge. */
    bool ppi_started = false;
    if (ppi_was_armed) {
        nrf_timer_task_trigger(NRF_TIMER3, NRF_TIMER_TASK_CAPTURE1);
        const uint32_t elapsed_ticks = nrf_timer_cc_get(NRF_TIMER3, NRF_TIMER_CC_CHANNEL1);
        /* COMPARE0_CLEAR wraps TIMER3 every 156 ticks. A late IRQ must
         * be detected from the latched compare event, not captured ticks.
         */
        const bool first_deadline_passed =
            nrf_timer_event_check(NRF_TIMER3, NRF_TIMER_EVENT_COMPARE0);
        ppi_started = elapsed_ticks != 0U || first_deadline_passed;
        if (ppi_started) {
            atomic_inc(&p3_ppi_starts);
            if (first_deadline_passed) atomic_inc(&p3_ppi_late_irqs);
        }
    }
    if (!ppi_started) {
        atomic_inc(&p3_ppi_fallbacks);
        nrf_timer_task_trigger(NRF_TIMER3, NRF_TIMER_TASK_CLEAR);
        nrf_timer_cc_set(NRF_TIMER3, NRF_TIMER_CC_CHANNEL0, P3_FIRST_SAMPLE_US);
        nrf_timer_event_clear(NRF_TIMER3, NRF_TIMER_EVENT_COMPARE0);
        nrf_timer_task_trigger(NRF_TIMER3, NRF_TIMER_TASK_START);
    }
    /* PPI mode already started TIMER3 at the physical edge. */
    nrf_timer_int_enable(NRF_TIMER3, NRF_TIMER_INT_COMPARE0_MASK);
    /* Account AFTER arming TIMER3 so first-byte timing stays unchanged. */
    atomic_inc(&rx_gpio_start_events);
    atomic_set(&rx_last_gpio_start_ms, (atomic_val_t)k_uptime_get_32());
    const atomic_val_t motion = atomic_get(&rx_reported_motion);
    if (motion == 1) {
        atomic_inc(&rx_gpio_starts_reported_idle);
    } else if (motion == 0 || motion == 2) {
        atomic_inc(&rx_gpio_starts_reported_moving);
    } else {
        atomic_inc(&rx_gpio_starts_motion_unknown);
    }
}

static void p3_uarte_service(void)
{
    if (!atomic_get(&uart_active)) {
        return;
    }
    const uint32_t now = k_uptime_get_32();
    const bool direct = atomic_get(&hw_direct_active) != 0;
    if (NRF_UARTE0->EVENTS_RXDRDY != 0U) {
        NRF_UARTE0->EVENTS_RXDRDY = 0U;
        uart_last_byte_ms = now;
        if (direct) {
            hw_direct_saw_byte = true;
        }
    }
    if (NRF_UARTE0->EVENTS_ERROR != 0U) {
        NRF_UARTE0->EVENTS_ERROR = 0U;
        uint32_t cause = NRF_UARTE0->ERRORSRC;
        NRF_UARTE0->ERRORSRC = cause;
        atomic_set(&uart_hw_last_source, (atomic_val_t)cause);
        atomic_inc(&uart_hw_errors);
        if (direct) atomic_inc(&hw_direct_hw_errors);
        /* Nordic UARTE ERRORSRC: overrun=0x1, parity=0x2,
         * framing=0x4, break=0x8. A single event may set multiple bits.
         * This records the already-observed event without extra RX polling.
         */
        if (cause & 0x01U) atomic_inc(&rx_hw_overrun_events);
        if (cause & 0x02U) atomic_inc(&rx_hw_parity_events);
        if (cause & 0x04U) atomic_inc(&rx_hw_framing_events);
        if (cause & 0x08U) atomic_inc(&rx_hw_break_events);
        atomic_set(&rx_hw_last_ms, (atomic_val_t)now);
        atomic_set(&rx_hw_last_amount, (atomic_val_t)NRF_UARTE0->RXD.AMOUNT);
    }
    const bool buffer_full = NRF_UARTE0->EVENTS_ENDRX != 0U;
    const bool hold_direct = direct && p3_hw_rx_wanted(now);
    if (!buffer_full) {
        /* When the full hardware session is still allowed, do not restart
         * DMA during silence or in the middle of a byte stream. But once
         * STOP guard / 120-second deadline has expired, MUST exit even if
         * UART RXDRDY events keep coming: otherwise an active stream could
         * postpone the watchdog indefinitely.
         */
        if (direct) {
            if (hold_direct &&
                (!hw_direct_saw_byte ||
                 (uint32_t)(now - uart_last_byte_ms) < P3_UART_IDLE_MS)) {
                return;
            }
        } else if ((uint32_t)(now - uart_last_byte_ms) < P3_UART_IDLE_MS) {
            return;
        }
    }

    NRF_UARTE0->EVENTS_RXTO = 0U;
    NRF_UARTE0->TASKS_STOPRX = 1U;
    bool rx_stopped = false;
    for (uint32_t elapsed = 0U;
         elapsed < P3_UART_RXTO_WAIT_US; elapsed += 10U) {
        if (NRF_UARTE0->EVENTS_RXTO != 0U) {
            rx_stopped = true;
            break;
        }
        k_busy_wait(10);
    }
    if (!rx_stopped) {
        p3_note_handoff_error(&rx_handoff_rxto_timeout, 2U);
        if (direct) atomic_inc(&hw_direct_rxto_timeouts);
    } else {
        const uint32_t count = NRF_UARTE0->RXD.AMOUNT;
        if (count > 0U && count <= sizeof(p3_dma_rx)) {
            if (direct) p3_session_diag_note_dma_bytes(count);
            uint32_t copied = ring_buf_put(&p3_soft_ring,
                                            p3_dma_rx, count);
            atomic_add(&uart_dma_bytes, (atomic_val_t)copied);
            if (copied != count) {
                atomic_add(&overflow_errors,
                           (atomic_val_t)(count - copied));
            }
            if (copied > 0U) {
                k_sem_give(&p3_soft_rx_sem);
                if (direct) {
                    atomic_add(&hw_direct_bytes, (atomic_val_t)copied);
                }
            }
        } else if (count > sizeof(p3_dma_rx)) {
            p3_note_handoff_error(&rx_handoff_amount_oversize, 3U);
        } else if (direct && hw_direct_saw_byte && count == 0U) {
            /* RXDRDY fired but stopped EasyDMA yielded no bytes. */
            atomic_inc(&hw_direct_empty_dma);
        }
        if (!direct &&
            (NRF_UARTE0->EVENTS_RXSTARTED == 0U || count == 0U)) {
            /* Original code counted only one failure when both are true.
             * Preserve that count; missing RXSTARTED takes priority.
             */
            if (NRF_UARTE0->EVENTS_RXSTARTED == 0U) {
                p3_note_handoff_error(&rx_handoff_no_rxstarted, 4U);
            } else {
                p3_note_handoff_error(&rx_handoff_zero_amount, 5U);
            }
        }
    }

    if (direct && rx_stopped && p3_hw_rx_wanted(k_uptime_get_32())) {
        /* The previous DMA burst is fully stopped and consumed; rearm the
         * same hardware RX without exposing another software first byte.
         */
        NRF_UARTE0->RXD.PTR = (uint32_t)(uintptr_t)p3_dma_rx;
        NRF_UARTE0->RXD.MAXCNT = sizeof(p3_dma_rx);
        NRF_UARTE0->EVENTS_RXSTARTED = 0U;
        NRF_UARTE0->EVENTS_RXDRDY = 0U;
        NRF_UARTE0->EVENTS_ENDRX = 0U;
        NRF_UARTE0->EVENTS_RXTO = 0U;
        NRF_UARTE0->EVENTS_ERROR = 0U;
        hw_direct_saw_byte = false;
        uart_last_byte_ms = k_uptime_get_32();
        NRF_UARTE0->TASKS_STARTRX = 1U;
        atomic_inc(&hw_direct_restarts);
        return;
    }
    if (direct) {
        uint32_t cause = (uint32_t)atomic_get(&hw_session_exit_reason);
        if (cause == P3_SESSION_EXIT_NONE || cause > P3_SESSION_EXIT_SHUTDOWN)
            cause = P3_SESSION_EXIT_OTHER;
        p3_session_diag_end((enum p3_session_exit_reason)cause,
            (uint32_t)(k_uptime_get_32() - uart_last_byte_ms));
        atomic_clear(&hw_direct_active);
        atomic_inc(&hw_direct_exits);
        atomic_set(&hw_direct_last_exit_ms, (atomic_val_t)k_uptime_get_32());
        atomic_set(&hw_direct_last_switch_ms, (atomic_val_t)k_uptime_get_32());
    }
    p3_uart_disable();
    atomic_clear(&uart_active);
    atomic_clear(&active);
    nrf_timer_task_trigger(NRF_TIMER3, NRF_TIMER_TASK_STOP);
    nrf_timer_event_clear(NRF_TIMER3, NRF_TIMER_EVENT_COMPARE0);
    nrf_timer_cc_set(NRF_TIMER3, NRF_TIMER_CC_CHANNEL0,
                     P3_FIRST_SAMPLE_US);
    rx_phase = P3_RX_DATA;
    shift_byte = 0U;
    bit_index = 0U;
    (void)gpio_pin_interrupt_configure(gpio, P3_RX_PIN,
                                       GPIO_INT_EDGE_FALLING);
    p3_ppi_arm();
}

int p3_soft_uart_start(void)
{
    if (!device_is_ready(gpio)) {
        return -ENODEV;
    }
    /* UART0 must have zephyr,deferred-init in DeviceTree. */
    p3_uarte_prepare_registers();
    int rc = gpio_pin_configure(gpio, P3_RX_PIN,
                                GPIO_INPUT | GPIO_PULL_UP);
    if (rc != 0) {
        return rc;
    }
    gpio_init_callback(&rx_callback, rx_start_irq, BIT(P3_RX_PIN));
    rc = gpio_add_callback(gpio, &rx_callback);
    if (rc != 0) {
        return rc;
    }
    nrf_timer_task_trigger(NRF_TIMER3, NRF_TIMER_TASK_STOP);
    nrf_timer_mode_set(NRF_TIMER3, NRF_TIMER_MODE_TIMER);
    nrf_timer_bit_width_set(NRF_TIMER3, NRF_TIMER_BIT_WIDTH_32);
    nrf_timer_prescaler_set(NRF_TIMER3, 4U);
    nrf_timer_shorts_enable(NRF_TIMER3,
                            NRF_TIMER_SHORT_COMPARE0_CLEAR_MASK);
    IRQ_CONNECT(TIMER3_IRQn, 3, timer3_irq, NULL, 0);
    irq_enable(TIMER3_IRQn);
    rc = gpio_pin_interrupt_configure(gpio, P3_RX_PIN, GPIO_INT_EDGE_FALLING);
    if (rc != 0) return rc;
    p3_ppi_setup();
    p3_ppi_arm();
    return 0;
}

void p3_soft_uart_stop(void)
{
    if (atomic_get(&hw_direct_active)) {
        p3_session_diag_end(P3_SESSION_EXIT_SHUTDOWN,
            (uint32_t)(k_uptime_get_32() - uart_last_byte_ms));
        p3_session_diag_publish_pending();
    }
    p3_ppi_disarm();
    if (p3_ppi_available) {
        nrfx_gppi_channel_free(p3_ppi_ch);
        p3_ppi_available = false;
    }
    if (atomic_get(&uart_active)) {
        NRF_UARTE0->EVENTS_RXTO = 0U;
        NRF_UARTE0->TASKS_STOPRX = 1U;
        for (uint32_t elapsed = 0U;
             elapsed < P3_UART_RXTO_WAIT_US; elapsed += 10U) {
            if (NRF_UARTE0->EVENTS_RXTO != 0U) {
                break;
            }
            k_busy_wait(10);
        }
    }
    p3_uart_disable();
    atomic_clear(&hw_rx_requested);
    atomic_clear(&hw_direct_active);
    atomic_clear(&uart_active);
    nrf_timer_task_trigger(NRF_TIMER3, NRF_TIMER_TASK_STOP);
    nrf_timer_int_disable(NRF_TIMER3, NRF_TIMER_INT_COMPARE0_MASK);
    (void)gpio_pin_interrupt_configure(gpio, P3_RX_PIN, GPIO_INT_DISABLE);
    atomic_clear(&active);
}

int p3_soft_uart_read(uint8_t *byte, k_timeout_t timeout)
{
    /* Publish only after the parser has consumed the last queued byte.
     * The read caller parses its returned byte before invoking us again.
     */
    if (ring_buf_size_get(&p3_soft_ring) == 0U)
        p3_session_diag_publish_pending();
    p3_uarte_service();
    p3_hw_rx_maybe_enter();
    if (ring_buf_get(&p3_soft_ring, byte, 1U) == 1U) {
        return 1;
    }
    /* Poll the UART event flags only while UARTE is on; in battery
     * idle, sleep normally for the caller's requested timeout.
     */
    k_timeout_t wait = atomic_get(&uart_active) ?
                       K_MSEC(3) : timeout;
    if (k_sem_take(&p3_soft_rx_sem, wait) != 0) {
        p3_uarte_service();
        p3_hw_rx_maybe_enter();
        return (int)ring_buf_get(&p3_soft_ring, byte, 1U);
    }
    p3_uarte_service();
    p3_hw_rx_maybe_enter();
    return (int)ring_buf_get(&p3_soft_ring, byte, 1U);
}

uint32_t p3_soft_uart_ppi_enabled(void) { return p3_ppi_available ? 1U : 0U; }
uint32_t p3_soft_uart_ppi_starts(void) { return (uint32_t)atomic_get(&p3_ppi_starts); }
uint32_t p3_soft_uart_ppi_fallbacks(void) { return (uint32_t)atomic_get(&p3_ppi_fallbacks); }
uint32_t p3_soft_uart_ppi_late_irqs(void) { return (uint32_t)atomic_get(&p3_ppi_late_irqs); }

uint32_t p3_soft_uart_framing_errors(void)
{
    return (uint32_t)atomic_get(&framing_errors);
}
uint32_t p3_soft_uart_overflows(void)
{
    return (uint32_t)atomic_get(&overflow_errors);
}
uint32_t p3_soft_uart_starts(void)
{
    return (uint32_t)atomic_get(&start_count);
}
uint32_t p3_soft_uart_uarte_wakes(void)
{
    return (uint32_t)atomic_get(&uart_wake_count);
}
uint32_t p3_soft_uart_dma_bytes(void)
{
    return (uint32_t)atomic_get(&uart_dma_bytes);
}
uint32_t p3_soft_uart_handoff_errors(void)
{
    return (uint32_t)atomic_get(&uart_handoff_errors);
}
uint32_t p3_soft_uart_hardware_errors(void)
{
    return (uint32_t)atomic_get(&uart_hw_errors);
}
uint32_t p3_soft_uart_software_nonheader_bytes(void)
{
    return (uint32_t)atomic_get(&software_nonheader_bytes);
}
uint32_t p3_soft_uart_last_hw_errorsrc(void)
{
    return (uint32_t)atomic_get(&uart_hw_last_source);
}

/* Only accepted DP1 status reports may update classification; do not
 * fabricate a motor state from Zigbee command requests.
 */
void p3_soft_uart_request_motor_rx(void)
{
    /* Explicit motor commands can start a new session after a watchdog,
     * but can NEVER extend an already-active direct hardware session.
     * This function is called only by accepted command / changed DP1.
     */
    atomic_set(&hw_direct_motion_lockout, 0);
    /* Clear any previous STOP BEFORE publishing a new request; otherwise
     * the RX thread could consume stale stop_ms and drop this command's
     * intended prearm while the new request is being published.
     */
    atomic_clear(&hw_rx_stop_ms);
    if (!atomic_get(&hw_rx_requested)) {
        /* Publish the anchor BEFORE advertising the request to RX thread. */
        atomic_set(&hw_rx_request_ms, (atomic_val_t)k_uptime_get_32());
        atomic_set(&hw_rx_requested, 1);
    }
    k_sem_give(&p3_soft_rx_sem);
}

bool p3_soft_uart_direct_rx_active(void)
{
    return atomic_get(&hw_direct_active) != 0;
}

void p3_soft_uart_note_prearm_timeout(void)
{
    atomic_inc(&hw_direct_prearm_timeouts);
}

/* Accepted explicit local/Zigbee motor command starts an independent
 * motion episode. Called only after the UART TX queue accepts a command.
 */
void p3_soft_uart_note_explicit_motor_command(void)
{
    atomic_clear(&p3_position_wake_latched);
}

/* Called only by UART RX thread for a DIFFERENT, valid MCU DP3.
 * A long quiet interval plus changed DP3 is evidence of a fresh physical
 * movement. Only one position-induced re-arm is permitted until an MCU
 * STOP or explicit motor command, even if the first hardware session
 * reaches its 120s watchdog; repeated DP3 cannot make it run forever.
 */
void p3_soft_uart_note_changed_position(uint8_t position)
{
    const uint32_t now_ms = k_uptime_get_32();
    const bool fresh = p3_position_wake_fresh_episode(
        &p3_position_wake, position, now_ms);
    atomic_set(&p3_position_wake_last_ms, (atomic_val_t)now_ms);

    if (atomic_get(&hw_direct_motion_lockout) == 0) {
        return;
    }
    if (!fresh || atomic_get(&p3_position_wake_latched) != 0 ||
        atomic_get(&hw_direct_active) != 0 ||
        atomic_get(&hw_rx_requested) != 0) {
        atomic_inc(&p3_position_wake_suppressed);
        return;
    }
    if (atomic_cas(&p3_position_wake_latched, 0, 1)) {
        atomic_inc(&p3_position_wake_count);
        p3_soft_uart_request_motor_rx();
    }
}

void p3_soft_uart_set_mcu_motion(uint8_t dp1)
{
    if (dp1 > 2U) return;
    const atomic_val_t previous = atomic_set(&rx_reported_motion, (atomic_val_t)dp1);
    if (dp1 == 1U) {
        /* A fresh MCU STOP completes a motion episode and resets
         * post-timeout automatic-rearm suppression.
         * Repeated STOP packets cannot prolong the 1.5 s guard.
         */
        atomic_clear(&hw_direct_motion_lockout);
        atomic_clear(&p3_position_wake_latched);
        if (atomic_get(&hw_rx_requested) &&
            atomic_get(&hw_rx_stop_ms) == 0) {
            atomic_set(&hw_rx_stop_ms, (atomic_val_t)k_uptime_get_32());
        }
        return;
    }
    /* Only a genuine DP1 state transition (STOP->OPEN/CLOSE or a
     * reversal) may initiate direct RX. Identical repeated reports
     * never change RX request timestamps and never re-arm after timeout.
     */
    if (p3_hw_rx_motion_transition((uint8_t)previous, dp1,
                                  atomic_get(&hw_direct_motion_lockout) != 0)) {
        if (atomic_get(&hw_rx_requested)) {
            atomic_clear(&hw_rx_stop_ms);
        } else {
            p3_soft_uart_request_motor_rx();
        }
    }
}
uint32_t p3_soft_uart_reported_motion(void)
{
    return (uint32_t)atomic_get(&rx_reported_motion);
}
uint32_t p3_soft_uart_gpio_start_events(void)
{
    return (uint32_t)atomic_get(&rx_gpio_start_events);
}
uint32_t p3_soft_uart_last_gpio_start_ms(void)
{
    return (uint32_t)atomic_get(&rx_last_gpio_start_ms);
}
uint32_t p3_soft_uart_gpio_starts_reported_idle(void)
{
    return (uint32_t)atomic_get(&rx_gpio_starts_reported_idle);
}
uint32_t p3_soft_uart_gpio_starts_reported_moving(void)
{
    return (uint32_t)atomic_get(&rx_gpio_starts_reported_moving);
}
uint32_t p3_soft_uart_gpio_starts_motion_unknown(void)
{
    return (uint32_t)atomic_get(&rx_gpio_starts_motion_unknown);
}
uint32_t p3_soft_uart_bad_stop_gpio(void)
{
    return (uint32_t)atomic_get(&rx_bad_stop_gpio);
}
uint32_t p3_soft_uart_bad_stop_chained(void)
{
    return (uint32_t)atomic_get(&rx_bad_stop_chained);
}
uint32_t p3_soft_uart_bad_stop_reported_idle(void)
{
    return (uint32_t)atomic_get(&rx_bad_stop_reported_idle);
}
uint32_t p3_soft_uart_bad_stop_reported_moving(void)
{
    return (uint32_t)atomic_get(&rx_bad_stop_reported_moving);
}
uint32_t p3_soft_uart_bad_stop_motion_unknown(void)
{
    return (uint32_t)atomic_get(&rx_bad_stop_motion_unknown);
}
uint32_t p3_soft_uart_last_bad_stop_ms(void)
{
    return (uint32_t)atomic_get(&rx_last_bad_stop_ms);
}
uint32_t p3_soft_uart_last_bad_stop_byte(void)
{
    return (uint32_t)atomic_get(&rx_last_bad_stop_byte);
}

/* v42.0-41: volatile, read-only diagnostics. No sampling path changes. */
uint32_t p3_soft_uart_bad_stop_gpio_last_ms(void)
{
    return (uint32_t)atomic_get(&rx_bad_stop_gpio_last_ms);
}
uint32_t p3_soft_uart_bad_stop_chained_last_ms(void)
{
    return (uint32_t)atomic_get(&rx_bad_stop_chained_last_ms);
}
uint32_t p3_soft_uart_handoff_unprepared(void)
{
    return (uint32_t)atomic_get(&rx_handoff_unprepared);
}
uint32_t p3_soft_uart_handoff_rxto_timeout(void)
{
    return (uint32_t)atomic_get(&rx_handoff_rxto_timeout);
}
uint32_t p3_soft_uart_handoff_amount_oversize(void)
{
    return (uint32_t)atomic_get(&rx_handoff_amount_oversize);
}
uint32_t p3_soft_uart_handoff_no_rxstarted(void)
{
    return (uint32_t)atomic_get(&rx_handoff_no_rxstarted);
}
uint32_t p3_soft_uart_handoff_zero_amount(void)
{
    return (uint32_t)atomic_get(&rx_handoff_zero_amount);
}
uint32_t p3_soft_uart_handoff_last_ms(void)
{
    return (uint32_t)atomic_get(&rx_handoff_last_ms);
}
uint32_t p3_soft_uart_handoff_last_reason(void)
{
    return (uint32_t)atomic_get(&rx_handoff_last_reason);
}
uint32_t p3_soft_uart_hw_framing_events(void)
{
    return (uint32_t)atomic_get(&rx_hw_framing_events);
}
uint32_t p3_soft_uart_hw_overrun_events(void)
{
    return (uint32_t)atomic_get(&rx_hw_overrun_events);
}
uint32_t p3_soft_uart_hw_break_events(void)
{
    return (uint32_t)atomic_get(&rx_hw_break_events);
}
uint32_t p3_soft_uart_hw_parity_events(void)
{
    return (uint32_t)atomic_get(&rx_hw_parity_events);
}
uint32_t p3_soft_uart_hw_last_ms(void)
{
    return (uint32_t)atomic_get(&rx_hw_last_ms);
}
uint32_t p3_soft_uart_hw_last_amount(void)
{
    return (uint32_t)atomic_get(&rx_hw_last_amount);
}
/* v42.0-42 bounded hardware-during-motion RX diagnostics. */
uint32_t p3_soft_uart_hw_direct_active(void) { return (uint32_t)atomic_get(&hw_direct_active); }
uint32_t p3_soft_uart_hw_direct_enters(void) { return (uint32_t)atomic_get(&hw_direct_enters); }
uint32_t p3_soft_uart_hw_direct_exits(void) { return (uint32_t)atomic_get(&hw_direct_exits); }
uint32_t p3_soft_uart_hw_direct_bytes(void) { return (uint32_t)atomic_get(&hw_direct_bytes); }
uint32_t p3_soft_uart_hw_direct_restarts(void) { return (uint32_t)atomic_get(&hw_direct_restarts); }
uint32_t p3_soft_uart_hw_direct_max_timeouts(void) { return (uint32_t)atomic_get(&hw_direct_max_timeouts); }
uint32_t p3_soft_uart_hw_direct_prearm_timeouts(void) { return (uint32_t)atomic_get(&hw_direct_prearm_timeouts); }
uint32_t p3_soft_uart_hw_direct_last_switch_ms(void) { return (uint32_t)atomic_get(&hw_direct_last_switch_ms); }

uint32_t p3_soft_uart_hw_direct_hw_errors(void) { return (uint32_t)atomic_get(&hw_direct_hw_errors); }
uint32_t p3_soft_uart_hw_direct_empty_dma(void) { return (uint32_t)atomic_get(&hw_direct_empty_dma); }
uint32_t p3_soft_uart_hw_direct_rxto_timeouts(void) { return (uint32_t)atomic_get(&hw_direct_rxto_timeouts); }

uint32_t p3_soft_uart_hw_direct_session_start_ms(void)
{
    return (uint32_t)atomic_get(&hw_direct_session_start_ms);
}
uint32_t p3_soft_uart_hw_direct_motion_lockout(void)
{
    return (uint32_t)atomic_get(&hw_direct_motion_lockout);
}

uint32_t p3_soft_uart_idle_fault_episodes(void) { return (uint32_t)atomic_get(&rx_idle_fault_episodes); }
uint32_t p3_soft_uart_bad_stop_post_exit_30s(void) { return (uint32_t)atomic_get(&rx_bad_stop_post_exit_30s); }
uint32_t p3_soft_uart_bad_stop_late_or_no_exit(void) { return (uint32_t)atomic_get(&rx_bad_stop_late_or_no_exit); }
uint32_t p3_soft_uart_last_bad_stop_gpio_age_ms(void) { return (uint32_t)atomic_get(&rx_last_bad_stop_gpio_age_ms); }
uint32_t p3_soft_uart_hw_direct_last_exit_ms(void) { return (uint32_t)atomic_get(&hw_direct_last_exit_ms); }

uint32_t p3_soft_uart_position_wake_count(void)
{
    return (uint32_t)atomic_get(&p3_position_wake_count);
}
uint32_t p3_soft_uart_position_wake_suppressed(void)
{
    return (uint32_t)atomic_get(&p3_position_wake_suppressed);
}
uint32_t p3_soft_uart_position_wake_last_ms(void)
{
    return (uint32_t)atomic_get(&p3_position_wake_last_ms);
}
uint32_t p3_soft_uart_position_wake_latched(void)
{
    return (uint32_t)atomic_get(&p3_position_wake_latched);
}
