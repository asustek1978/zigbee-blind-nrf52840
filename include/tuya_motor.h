/* SPDX-License-Identifier: MIT */
#ifndef TUYA_MOTOR_H_
#define TUYA_MOTOR_H_

#include <stddef.h>
#include <stdint.h>

/* WBR3 -> P3 frames verified from the original motor's UART capture.
 * These functions ONLY encode bytes. They do not transmit anything.
 */
enum tuya_motor_action {
    TUYA_MOTOR_OPEN = 0,
    TUYA_MOTOR_STOP = 1,
    TUYA_MOTOR_CLOSE = 2,
};

/* OPEN/STOP/CLOSE, Tuya Classic protocol, DP1 enum 0/1/2. */
size_t tuya_motor_encode_action(enum tuya_motor_action action,
                                uint8_t *out, size_t capacity);

/* Position setpoint in 0..100, Tuya Classic protocol, DP2 Value type.
 * End-stop orientation is NOT yet confirmed. No transmission is performed.
 */
size_t tuya_motor_encode_position(uint8_t percent,
                                  uint8_t *out, size_t capacity);

#endif
