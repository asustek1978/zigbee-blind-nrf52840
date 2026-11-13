/* SPDX-License-Identifier: MIT
 * v42.0-33 EXPERIMENT ONLY — low-power P3 9600 8N1 RX, P0.17.
 *
 * GPIOTE edge -> allocated PPI channel -> TIMER3 CLEAR + START.
 * A second allocated PPI channel disables their PPI group on that
 * very edge, preventing data-bit falling edges from restarting TIMER3.
 *
 * No motor UART TX. UARTE0 disabled. TIMER3 resource ownership,
 * runtime interrupt jitter, and GPIOTE/PPI reservations require
 * hardware verification under the actual Zigbee R23 stack.
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
#include <hal/nrf_gpiote.h>
#include <hal/nrf_uarte.h>
#include <nrfx_ppi.h>
#include "p3_soft_uart.h"

#define P3_RX_PIN 17U
#define P3_RING_SIZE 2048U
#define P3_BIT_US 104U
#define P3_FIRST_SAMPLE_US 156U
#define P3_LATE_IRQ_THRESHOLD_US 35U

RING_BUF_DECLARE(p3_soft_ring, P3_RING_SIZE);
K_SEM_DEFINE(p3_soft_rx_sem, 0, 1);

static const struct device *const gpio = DEVICE_DT_GET(DT_NODELABEL(gpio0));
static nrf_ppi_channel_t ppi_start;
static nrf_ppi_channel_t ppi_disarm;
static nrf_ppi_channel_group_t ppi_group;
static atomic_t framing_errors;
static atomic_t overflow_errors;
static atomic_t start_count;
static atomic_t timer_late_count;
static atomic_t timer_max_lag_us;
static uint8_t shift_byte;
static uint8_t bit_index;

enum p3_rx_phase {
    P3_RX_DATA,
    P3_RX_STOP,
    P3_RX_NEXT_START,
};
static volatile enum p3_rx_phase rx_phase;

/* Zephyr GPIO driver has already allocated the GPIOTE input event
 * channel for P0.17. Discover that channel from the actual peripheral
 * configuration; DO NOT hardcode a channel or reconfigure it behind
 * the Zephyr driver's back. Return 0 on failure (not a valid address).
 */
static uint32_t p3_rx_gpiote_event_address(void)
{
    for (uint32_t i = 0; i < 8U; i++) {
        uint32_t cfg = NRF_GPIOTE->CONFIG[i];
        uint32_t mode = (cfg & GPIOTE_CONFIG_MODE_Msk) >>
                        GPIOTE_CONFIG_MODE_Pos;
        uint32_t pin = (cfg & GPIOTE_CONFIG_PSEL_Msk) >>
                       GPIOTE_CONFIG_PSEL_Pos;
        uint32_t polarity = (cfg & GPIOTE_CONFIG_POLARITY_Msk) >>
                            GPIOTE_CONFIG_POLARITY_Pos;
        if (mode == GPIOTE_CONFIG_MODE_Event &&
            pin == P3_RX_PIN &&
            polarity == GPIOTE_CONFIG_POLARITY_HiToLo) {
            return (uint32_t)&NRF_GPIOTE->EVENTS_IN[i];
        }
    }
    return 0U;
}

static void timer3_irq(const void *unused)
{
    ARG_UNUSED(unused);
    if (!nrf_timer_event_check(NRF_TIMER3, NRF_TIMER_EVENT_COMPARE0)) {
        return;
    }
    nrf_timer_event_clear(NRF_TIMER3, NRF_TIMER_EVENT_COMPARE0);

    /* SHORT COMPARE0->CLEAR resets the counter at the compare event.
     * CAPTURE1 measures how late the CPU reached this ISR in us.
     * This is diagnostic only, not a correction for already late samples.
     */
    nrf_timer_task_trigger(NRF_TIMER3, NRF_TIMER_TASK_CAPTURE1);
    uint32_t lag = nrf_timer_cc_get(NRF_TIMER3, NRF_TIMER_CC_CHANNEL1);
    if (lag > P3_LATE_IRQ_THRESHOLD_US) {
        atomic_inc(&timer_late_count);
    }
    atomic_val_t max = atomic_get(&timer_max_lag_us);
    while (lag > (uint32_t)max &&
           !atomic_cas(&timer_max_lag_us, max, (atomic_val_t)lag)) {
        max = atomic_get(&timer_max_lag_us);
    }

    const bool level = nrf_gpio_pin_read(P3_RX_PIN) != 0;
    if (rx_phase == P3_RX_DATA) {
        if (bit_index == 0U) {
            atomic_inc(&start_count);
        }
        if (level) {
            shift_byte |= (uint8_t)(1U << bit_index);
        }
        bit_index++;
        if (bit_index == 8U) {
            rx_phase = P3_RX_STOP;
        }
        nrf_timer_cc_set(NRF_TIMER3, NRF_TIMER_CC_CHANNEL0, P3_BIT_US);
        return;
    }

    if (rx_phase == P3_RX_STOP) {
        if (level) {
            if (ring_buf_put(&p3_soft_ring, &shift_byte, 1U) != 1U) {
                atomic_inc(&overflow_errors);
            } else {
                k_sem_give(&p3_soft_rx_sem);
            }
        } else {
            atomic_inc(&framing_errors);
        }
        rx_phase = P3_RX_NEXT_START;
        nrf_timer_cc_set(NRF_TIMER3, NRF_TIMER_CC_CHANNEL0, P3_BIT_US);
        return;
    }

    if (!level) {
        /* Consecutive byte: sampled middle of its start bit.
         * The next sample, after one bit time, is data bit 0.
         */
        shift_byte = 0U;
        bit_index = 0U;
        rx_phase = P3_RX_DATA;
        nrf_timer_cc_set(NRF_TIMER3, NRF_TIMER_CC_CHANNEL0, P3_BIT_US);
        return;
    }

    /* Confirmed idle: stop high-frequency TIMER3. PPI group was
     * disabled on the first edge; rearm hardware event reception.
     * There is still a finite IRQ rearm gap; count corrupt frames.
     */
    nrf_timer_task_trigger(NRF_TIMER3, NRF_TIMER_TASK_STOP);
    nrf_timer_event_clear(NRF_TIMER3, NRF_TIMER_EVENT_COMPARE0);
    nrf_timer_cc_set(NRF_TIMER3, NRF_TIMER_CC_CHANNEL0,
                     P3_FIRST_SAMPLE_US);
    rx_phase = P3_RX_DATA;
    shift_byte = 0U;
    bit_index = 0U;
    (void)nrfx_ppi_group_enable(ppi_group);
}

static int p3_ppi_setup(uint32_t event_addr)
{
    nrfx_err_t err = nrfx_ppi_channel_alloc(&ppi_start);
    if (err != NRFX_SUCCESS) {
        return -ENOSPC;
    }
    err = nrfx_ppi_channel_alloc(&ppi_disarm);
    if (err != NRFX_SUCCESS) {
        (void)nrfx_ppi_channel_free(ppi_start);
        return -ENOSPC;
    }
    err = nrfx_ppi_group_alloc(&ppi_group);
    if (err != NRFX_SUCCESS) {
        (void)nrfx_ppi_channel_free(ppi_disarm);
        (void)nrfx_ppi_channel_free(ppi_start);
        return -ENOSPC;
    }

    /* Event -> START with a fork to CLEAR in hardware. */
    err = nrfx_ppi_channel_assign(ppi_start, event_addr,
            (uint32_t)&NRF_TIMER3->TASKS_START);
    if (err == NRFX_SUCCESS) {
        err = nrfx_ppi_channel_fork_assign(ppi_start,
            (uint32_t)&NRF_TIMER3->TASKS_CLEAR);
    }
    if (err == NRFX_SUCCESS) {
        /* Same event -> PPI group disable, only once per burst. */
        err = nrfx_ppi_channel_assign(ppi_disarm, event_addr,
            nrfx_ppi_task_addr_group_disable_get(ppi_group));
    }
    if (err == NRFX_SUCCESS) {
        err = nrfx_ppi_channel_include_in_group(ppi_start, ppi_group);
    }
    if (err == NRFX_SUCCESS) {
        err = nrfx_ppi_channel_include_in_group(ppi_disarm, ppi_group);
    }
    if (err != NRFX_SUCCESS) {
        (void)nrfx_ppi_group_free(ppi_group);
        (void)nrfx_ppi_channel_free(ppi_disarm);
        (void)nrfx_ppi_channel_free(ppi_start);
        return -EIO;
    }
    return 0;
}

int p3_soft_uart_start(void)
{
    if (!device_is_ready(gpio)) {
        return -ENODEV;
    }
    /* UARTE0 not used and motor TX remains physically disconnected. */
    nrf_uarte_task_trigger(NRF_UARTE0, NRF_UARTE_TASK_STOPRX);
    nrf_uarte_disable(NRF_UARTE0);

    int rc = gpio_pin_configure(gpio, P3_RX_PIN,
                                GPIO_INPUT | GPIO_PULL_UP);
    if (rc != 0) {
        return rc;
    }
    /* Let Zephyr/nrfx GPIO allocate the GPIOTE event channel. */
    rc = gpio_pin_interrupt_configure(gpio, P3_RX_PIN,
                                      GPIO_INT_EDGE_FALLING);
    if (rc != 0) {
        return rc;
    }
    uint32_t event_addr = p3_rx_gpiote_event_address();
    if (event_addr == 0U) {
        return -ENODEV; /* Fail closed; do not claim RX ready. */
    }

    nrf_timer_task_trigger(NRF_TIMER3, NRF_TIMER_TASK_STOP);
    nrf_timer_mode_set(NRF_TIMER3, NRF_TIMER_MODE_TIMER);
    nrf_timer_bit_width_set(NRF_TIMER3, NRF_TIMER_BIT_WIDTH_32);
    nrf_timer_prescaler_set(NRF_TIMER3, 4U); /* 1 MHz */
    nrf_timer_shorts_enable(NRF_TIMER3,
                            NRF_TIMER_SHORT_COMPARE0_CLEAR_MASK);
    nrf_timer_cc_set(NRF_TIMER3, NRF_TIMER_CC_CHANNEL0,
                     P3_FIRST_SAMPLE_US);
    nrf_timer_event_clear(NRF_TIMER3, NRF_TIMER_EVENT_COMPARE0);
    rx_phase = P3_RX_DATA;
    shift_byte = 0U;
    bit_index = 0U;

    rc = p3_ppi_setup(event_addr);
    if (rc != 0) {
        return rc;
    }
    IRQ_CONNECT(TIMER3_IRQn, 3, timer3_irq, NULL, 0);
    nrf_timer_int_enable(NRF_TIMER3, NRF_TIMER_INT_COMPARE0_MASK);
    irq_enable(TIMER3_IRQn);
    /* No active timer and no CPU GPIO callback in idle. */
    return nrfx_ppi_group_enable(ppi_group) == NRFX_SUCCESS ? 0 : -EIO;
}

int p3_soft_uart_read(uint8_t *byte, k_timeout_t timeout)
{
    if (ring_buf_get(&p3_soft_ring, byte, 1U) == 1U) {
        return 1;
    }
    if (k_sem_take(&p3_soft_rx_sem, timeout) != 0) {
        return 0;
    }
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
uint32_t p3_soft_uart_max_isr_lag_us(void)
{
    return (uint32_t)atomic_get(&timer_max_lag_us);
}
uint32_t p3_soft_uart_late_isr_count(void)
{
    return (uint32_t)atomic_get(&timer_late_count);
}
