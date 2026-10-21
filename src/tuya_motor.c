/* SPDX-License-Identifier: MIT */
#include "tuya_motor.h"
#include "tuya_frame.h"

#include <string.h>

#define TUYA_CLASSIC_CONTROL_COMMAND 0x06U
#define TUYA_DP_ACTION 0x01U
#define TUYA_DP_POSITION_TARGET 0x02U
#define TUYA_TYPE_ENUM 0x04U
#define TUYA_TYPE_VALUE 0x02U

size_t tuya_motor_encode_action(enum tuya_motor_action action,
                                uint8_t *out, size_t capacity)
{
    struct tuya_frame packet = {0};

    if (out == NULL || action < TUYA_MOTOR_OPEN || action > TUYA_MOTOR_CLOSE) {
        return 0U;
    }

    packet.format = TUYA_WIRE_CLASSIC;
    packet.version = 0x00U;
    packet.command = TUYA_CLASSIC_CONTROL_COMMAND;
    packet.length = 5U;
    packet.data[0] = TUYA_DP_ACTION;
    packet.data[1] = TUYA_TYPE_ENUM;
    packet.data[2] = 0U;
    packet.data[3] = 1U;
    packet.data[4] = (uint8_t)action;
    return tuya_encode(&packet, out, capacity);
}

size_t tuya_motor_encode_position(uint8_t percent,
                                  uint8_t *out, size_t capacity)
{
    struct tuya_frame packet = {0};

    if (out == NULL || percent > 100U) {
        return 0U;
    }

    packet.format = TUYA_WIRE_CLASSIC;
    packet.version = 0x00U;
    packet.command = TUYA_CLASSIC_CONTROL_COMMAND;
    packet.length = 8U;
    packet.data[0] = TUYA_DP_POSITION_TARGET;
    packet.data[1] = TUYA_TYPE_VALUE;
    packet.data[2] = 0U;
    packet.data[3] = 4U;
    packet.data[4] = 0U;
    packet.data[5] = 0U;
    packet.data[6] = 0U;
    packet.data[7] = percent;
    return tuya_encode(&packet, out, capacity);
}
