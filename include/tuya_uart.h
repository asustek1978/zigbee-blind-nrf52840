/* SPDX-License-Identifier: MIT */
#ifndef TUYA_UART_H_
#define TUYA_UART_H_

#include "tuya_motor.h"
#include "tuya_motor_state.h"

#include <stdbool.h>
#include <stdint.h>

/* Receive state is always available; motor TX is OFF by default (Kconfig).
 * True means accepted into a bounded UART queue, NOT physical completion.
 * Never auto-issue movement on boot or Zigbee network join.
 */
bool tuya_uart_submit_action(enum tuya_motor_action action);
bool tuya_uart_submit_position(uint8_t percent);
bool tuya_uart_submit_hw_calibration(uint8_t command);
void tuya_uart_get_state(struct tuya_motor_state *out);

/* UART RX-only telemetry. No interpretation of DP3/DP13 as calibrated
 * physical lift/battery levels is implied. Values reset on boot.
 */
struct tuya_uart_diagnostics {
    uint32_t rx_bytes;
    uint32_t valid_frames;
    uint32_t rejected_frames;
    uint32_t dropped_bytes;
    uint32_t accepted_reports;
    uint8_t last_command;
    uint8_t last_format;
    uint8_t rx_ready;
    uint8_t diag_startup_requests_sent; /* 0..4; full 7-byte UART frames */
    uint8_t diag_startup_finished;      /* 1 after one-time attempt ends */
};

void tuya_uart_get_diagnostics(struct tuya_uart_diagnostics *out);

#endif
