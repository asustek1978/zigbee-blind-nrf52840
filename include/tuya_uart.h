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
void tuya_uart_get_state(struct tuya_motor_state *out);

#endif
