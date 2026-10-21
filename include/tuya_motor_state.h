/* SPDX-License-Identifier: MIT */
#ifndef TUYA_MOTOR_STATE_H_
#define TUYA_MOTOR_STATE_H_

#include "tuya_frame.h"

#include <stdbool.h>
#include <stdint.h>

/* Values observed from the ORIGINAL Tuya P3 UART, not from estimates. */
#define P3_DP_MOTION 1U
#define P3_DP_TARGET 2U
#define P3_DP_POSITION 3U
#define P3_DP_BATTERY 13U

struct tuya_motor_state {
    uint8_t motion;
    uint8_t position;
    uint8_t battery;
    bool motion_valid;
    bool position_valid;
    bool battery_valid;
    uint32_t report_count;
};

struct tuya_motor_changes {
    bool motion;
    bool position;
    bool battery;
};

/* Accept MCU-originated classic DP reports only (cmd=0x07, ver 0 or 3).
 * Wrong types, invalid lengths, invalid percentages, module-originated
 * commands and malformed multi-DP payloads NEVER change the state.
 */
bool tuya_motor_apply_report(const struct tuya_frame *frame,
                             struct tuya_motor_state *state,
                             struct tuya_motor_changes *changes);

#endif
