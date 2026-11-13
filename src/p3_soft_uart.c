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
#include "p3_soft_uart.h"

#define P3_RX_PIN 17U
#define P3_RING_SIZE 2048U
#define P3_UART_DMA_SIZE 1024U
#define P3_BIT_US 104U
#define P3_FIRST_SAMPLE_US 156U
/* UARTE stays awake only while the frame is arriving, plus idle tail.
 * RXDRDY polling every 3 ms can extend this timeout by a few ms.
 */
#define P3_UART_IDLE_MS 15U
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
static atomic_t rx_last_gpio_start_ms;
static atomic_t rx_bad_stop_gpio;
static atomic_t rx_bad_stop_chained;
static atomic_t rx_bad_stop_reported_idle;
static atomic_t rx_bad_stop_reported_moving;
static atomic_t rx_bad_stop_motion_unknown;
static atomic_t rx_last_bad_stop_ms;
static atomic_t rx_last_bad_stop_byte;
static volatile bool rx_origin_gpio;
static uint8_t shift_byte;
static uint8_t bit_index;
static bool uart_prepared;
static uint32_t uart_last_byte_ms;

enum p3_rx_phase {
    P3_RX_DATA,
    P3_RX_STOP,
    P3_RX_NEXT_START,
};
static volatile enum p3_rx_phase rx_phase;

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

static void p3_uart_begin_dma(void)
{
    /* Called at software stop bit centre (9.5 UART bit intervals).
     * Only ~52 us until next start bit. Checking RXSTARTED later
     * is diagnostic; successful byte capture MUST be measured.
     */
    if (!uart_prepared) {
        atomic_inc(&uart_handoff_errors);
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
            atomic_inc(&framing_errors);
            if (rx_origin_gpio) {
                atomic_inc(&rx_bad_stop_gpio);
            } else {
                atomic_inc(&rx_bad_stop_chained);
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
            atomic_set(&rx_last_bad_stop_ms, (atomic_val_t)k_uptime_get_32());
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
    nrf_timer_task_trigger(NRF_TIMER3, NRF_TIMER_TASK_CLEAR);
    nrf_timer_cc_set(NRF_TIMER3, NRF_TIMER_CC_CHANNEL0,
                     P3_FIRST_SAMPLE_US);
    nrf_timer_event_clear(NRF_TIMER3, NRF_TIMER_EVENT_COMPARE0);
    nrf_timer_int_enable(NRF_TIMER3, NRF_TIMER_INT_COMPARE0_MASK);
    nrf_timer_task_trigger(NRF_TIMER3, NRF_TIMER_TASK_START);
    /* Account AFTER arming TIMER3 so first-byte timing stays unchanged. */
    atomic_inc(&rx_gpio_start_events);
    atomic_set(&rx_last_gpio_start_ms, (atomic_val_t)k_uptime_get_32());
}

static void p3_uarte_service(void)
{
    if (!atomic_get(&uart_active)) {
        return;
    }
    const uint32_t now = k_uptime_get_32();
    if (NRF_UARTE0->EVENTS_RXDRDY != 0U) {
        NRF_UARTE0->EVENTS_RXDRDY = 0U;
        uart_last_byte_ms = now;
    }
    if (NRF_UARTE0->EVENTS_ERROR != 0U) {
        NRF_UARTE0->EVENTS_ERROR = 0U;
        uint32_t cause = NRF_UARTE0->ERRORSRC;
        NRF_UARTE0->ERRORSRC = cause;
        atomic_set(&uart_hw_last_source, (atomic_val_t)cause);
        atomic_inc(&uart_hw_errors);
    }
    const bool buffer_full = NRF_UARTE0->EVENTS_ENDRX != 0U;
    if (!buffer_full &&
        (uint32_t)(now - uart_last_byte_ms) < P3_UART_IDLE_MS) {
        return;
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
        atomic_inc(&uart_handoff_errors);
    } else {
        const uint32_t count = NRF_UARTE0->RXD.AMOUNT;
        if (count > 0U && count <= sizeof(p3_dma_rx)) {
            uint32_t copied = ring_buf_put(&p3_soft_ring,
                                            p3_dma_rx, count);
            atomic_add(&uart_dma_bytes, (atomic_val_t)copied);
            if (copied != count) {
                atomic_add(&overflow_errors,
                           (atomic_val_t)(count - copied));
            }
            if (copied > 0U) {
                k_sem_give(&p3_soft_rx_sem);
            }
        } else if (count > sizeof(p3_dma_rx)) {
            atomic_inc(&uart_handoff_errors);
        }
        if (NRF_UARTE0->EVENTS_RXSTARTED == 0U || count == 0U) {
            atomic_inc(&uart_handoff_errors);
        }
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
    return gpio_pin_interrupt_configure(gpio, P3_RX_PIN,
                                        GPIO_INT_EDGE_FALLING);
}

void p3_soft_uart_stop(void)
{
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
    atomic_clear(&uart_active);
    nrf_timer_task_trigger(NRF_TIMER3, NRF_TIMER_TASK_STOP);
    nrf_timer_int_disable(NRF_TIMER3, NRF_TIMER_INT_COMPARE0_MASK);
    (void)gpio_pin_interrupt_configure(gpio, P3_RX_PIN, GPIO_INT_DISABLE);
    atomic_clear(&active);
}

int p3_soft_uart_read(uint8_t *byte, k_timeout_t timeout)
{
    p3_uarte_service();
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
        return (int)ring_buf_get(&p3_soft_ring, byte, 1U);
    }
    p3_uarte_service();
    return (int)ring_buf_get(&p3_soft_ring, byte, 1U);
}

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
void p3_soft_uart_set_mcu_motion(uint8_t dp1)
{
    if (dp1 <= 2U) {
        atomic_set(&rx_reported_motion, (atomic_val_t)dp1);
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
