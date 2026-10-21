/* SPDX-License-Identifier: MIT */
/* Receive-only Tuya P3 UART decoder. Motor transmission is disabled. */
#include "tuya_frame.h"

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>

LOG_MODULE_REGISTER(tuya_p3, LOG_LEVEL_INF);

#define MOTOR_UART_NODE DT_ALIAS(motor_uart)
BUILD_ASSERT(DT_NODE_EXISTS(MOTOR_UART_NODE), "motor-uart alias required");

static const struct device *const motor_uart = DEVICE_DT_GET(MOTOR_UART_NODE);

K_MSGQ_DEFINE(p3_rx_queue, sizeof(uint8_t), 256, 1);
static atomic_t rx_dropped;

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
            }
        }
    } while (n > 0);
}

static void describe_dp(const struct tuya_dp *dp, void *user)
{
    uint32_t number;

    ARG_UNUSED(user);
    if (tuya_dp_as_uint32(dp, &number)) {
        LOG_INF("P3 DP id=%u type=%u value=%u (unverified mapping)",
                dp->id, dp->type, number);
    } else {
        LOG_INF("P3 DP id=%u type=%u length=%u (raw/unknown)",
                dp->id, dp->type, dp->length);
    }
    /* DP1 / DP3 / DP13 remain unknown until motor-specific capture. */
}

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
        LOG_ERR("Tuya P3 UART interrupt callback failed: %d", err);
        return;
    }
    uart_irq_rx_enable(motor_uart);
    LOG_INF("Tuya P3 RX-only started (UART0 9600 8N1)");

    for (;;) {
        k_msgq_get(&p3_rx_queue, &byte, K_FOREVER);
        if (tuya_parser_feed(&parser, byte, &frame)) {
            LOG_INF("P3 frame format=%u ver=0x%02x seq=%u cmd=0x%02x len=%u",
                    (unsigned int)frame.format, frame.version, frame.sequence,
                    frame.command, frame.length);
            if (!tuya_foreach_report_dp(&frame, describe_dp, NULL)) {
                LOG_DBG("P3 non-DP command or malformed DP list");
            }
        }
        if (atomic_get(&rx_dropped) > 0) {
            LOG_WRN("P3 UART RX overflow: %ld bytes discarded",
                    (long)atomic_set(&rx_dropped, 0));
        }
    }
}

K_THREAD_DEFINE(tuya_rx_tid, 2048, receive_main, NULL, NULL, NULL,
                K_PRIO_PREEMPT(8), 0, 0);
