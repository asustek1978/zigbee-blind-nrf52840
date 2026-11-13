/* SPDX-License-Identifier: MIT
 * v42.0-31 EXPERIMENT ONLY: P0.17 GPIO start detection and TIMER3
 * sampling at 9600 8N1, without enabling UARTE0.
 * TIMER3 ownership and IRQ priority MUST be validated against Zigbee R23.
 */
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/irq.h>
#include <zephyr/sys/ring_buffer.h>
#include <zephyr/sys/atomic.h>
#include <hal/nrf_timer.h>
#include <hal/nrf_gpio.h>
#include <hal/nrf_uarte.h>
#include <zephyr/devicetree.h>
#include "p3_soft_uart.h"

#define P3_RX_PIN 17U
#define P3_RING_SIZE 2048U
#define P3_BIT_US 104U
#define P3_FIRST_SAMPLE_US 156U

RING_BUF_DECLARE(p3_soft_ring, P3_RING_SIZE);
static const struct device *const gpio = DEVICE_DT_GET(DT_NODELABEL(gpio0));
static struct gpio_callback rx_callback;
static atomic_t active;
static atomic_t framing_errors;
static atomic_t overflow_errors;
static atomic_t start_count;
static uint8_t shift_byte;
static uint8_t bit_index;
K_SEM_DEFINE(p3_soft_rx_sem, 0, 1);

static void timer3_irq(const void *unused)
{
    ARG_UNUSED(unused);
    if (!nrf_timer_event_check(NRF_TIMER3, NRF_TIMER_EVENT_COMPARE0)) {
        return;
    }
    nrf_timer_event_clear(NRF_TIMER3, NRF_TIMER_EVENT_COMPARE0);
    /* TIMER3 is reset by SHORT COMPARE0->CLEAR on each compare. */
    bool level = nrf_gpio_pin_read(P3_RX_PIN) != 0;
    if (bit_index < 8U) {
        if (level) shift_byte |= (uint8_t)(1U << bit_index);
        bit_index++;
        nrf_timer_cc_set(NRF_TIMER3, NRF_TIMER_CC_CHANNEL0, P3_BIT_US);
        return;
    }
    nrf_timer_task_trigger(NRF_TIMER3, NRF_TIMER_TASK_STOP);
    nrf_timer_int_disable(NRF_TIMER3, NRF_TIMER_INT_COMPARE0_MASK);
    if (level) {
        if (ring_buf_put(&p3_soft_ring, &shift_byte, 1U) != 1U) {
            atomic_inc(&overflow_errors);
        } else {
            k_sem_give(&p3_soft_rx_sem);
        }
    } else {
        atomic_inc(&framing_errors);
    }
    /* Re-arm only after stop-bit sample: ~52 us remain before next start. */
    atomic_clear(&active);
    gpio_pin_interrupt_configure(gpio, P3_RX_PIN, GPIO_INT_EDGE_FALLING);
}

static void rx_start_irq(const struct device *port,
                         struct gpio_callback *cb, gpio_port_pins_t pins)
{
    ARG_UNUSED(port);
    ARG_UNUSED(cb);
    ARG_UNUSED(pins);
    if (!atomic_cas(&active, 0, 1)) return;
    gpio_pin_interrupt_configure(gpio, P3_RX_PIN, GPIO_INT_DISABLE);
    shift_byte = 0U;
    bit_index = 0U;
    atomic_inc(&start_count);
    nrf_timer_task_trigger(NRF_TIMER3, NRF_TIMER_TASK_CLEAR);
    nrf_timer_cc_set(NRF_TIMER3, NRF_TIMER_CC_CHANNEL0, P3_FIRST_SAMPLE_US);
    nrf_timer_event_clear(NRF_TIMER3, NRF_TIMER_EVENT_COMPARE0);
    nrf_timer_int_enable(NRF_TIMER3, NRF_TIMER_INT_COMPARE0_MASK);
    nrf_timer_task_trigger(NRF_TIMER3, NRF_TIMER_TASK_START);
}

int p3_soft_uart_start(void)
{
    if (!device_is_ready(gpio)) return -ENODEV;
    /* Explicitly disconnect UARTE0 to keep P0.17 under GPIO ownership. */
    nrf_uarte_task_trigger(NRF_UARTE0, NRF_UARTE_TASK_STOPRX);
    nrf_uarte_enable(NRF_UARTE0, false);
    int rc = gpio_pin_configure(gpio, P3_RX_PIN, GPIO_INPUT | GPIO_PULL_UP);
    if (rc) return rc;
    gpio_init_callback(&rx_callback, rx_start_irq, BIT(P3_RX_PIN));
    rc = gpio_add_callback(gpio, &rx_callback);
    if (rc) return rc;
    nrf_timer_task_trigger(NRF_TIMER3, NRF_TIMER_TASK_STOP);
    nrf_timer_mode_set(NRF_TIMER3, NRF_TIMER_MODE_TIMER);
    nrf_timer_bit_width_set(NRF_TIMER3, NRF_TIMER_BIT_WIDTH_32);
    nrf_timer_frequency_set(NRF_TIMER3, NRF_TIMER_FREQ_1MHz);
    nrf_timer_shorts_enable(NRF_TIMER3, NRF_TIMER_SHORT_COMPARE0_CLEAR_MASK);
    IRQ_CONNECT(TIMER3_IRQn, 3, timer3_irq, NULL, 0);
    irq_enable(TIMER3_IRQn);
    return gpio_pin_interrupt_configure(gpio, P3_RX_PIN, GPIO_INT_EDGE_FALLING);
}

int p3_soft_uart_read(uint8_t *byte, k_timeout_t timeout)
{
    if (ring_buf_get(&p3_soft_ring, byte, 1U) == 1U) return 1;
    if (k_sem_take(&p3_soft_rx_sem, timeout) != 0) return 0;
    return (int)ring_buf_get(&p3_soft_ring, byte, 1U);
}

uint32_t p3_soft_uart_framing_errors(void) { return (uint32_t)atomic_get(&framing_errors); }
uint32_t p3_soft_uart_overflows(void) { return (uint32_t)atomic_get(&overflow_errors); }
uint32_t p3_soft_uart_starts(void) { return (uint32_t)atomic_get(&start_count); }
