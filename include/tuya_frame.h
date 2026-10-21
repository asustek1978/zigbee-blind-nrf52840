/* SPDX-License-Identifier: MIT */
#ifndef TUYA_FRAME_H_
#define TUYA_FRAME_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Bounded parser for both Tuya MCU serial frame families. */
#define TUYA_MAX_DATA 128U
#define TUYA_MAX_FRAME (TUYA_MAX_DATA + 9U)

enum tuya_wire_format {
    TUYA_WIRE_CLASSIC = 0, /* 55 AA ver cmd len_hi len_lo data checksum */
    TUYA_WIRE_ZIGBEE = 2, /* 55 AA 02 seq_hi seq_lo cmd len_hi len_lo data checksum */
};

struct tuya_frame {
    enum tuya_wire_format format;
    uint8_t version;
    uint16_t sequence;
    uint8_t command;
    uint16_t length;
    uint8_t data[TUYA_MAX_DATA];
};

struct tuya_parser {
    uint8_t buffer[TUYA_MAX_FRAME];
    uint16_t used;
    uint16_t expected;
    uint32_t rejected;
};

struct tuya_dp {
    uint8_t id;
    uint8_t type;
    uint16_t length;
    const uint8_t *value;
};

typedef void (*tuya_dp_callback_t)(const struct tuya_dp *dp, void *user);

void tuya_parser_init(struct tuya_parser *parser);
bool tuya_parser_feed(struct tuya_parser *parser, uint8_t value,
                      struct tuya_frame *out);
bool tuya_foreach_report_dp(const struct tuya_frame *frame,
                            tuya_dp_callback_t callback, void *user);
bool tuya_dp_as_uint32(const struct tuya_dp *dp, uint32_t *value);

/* Encoder for host tests/future protocol integration; NOT wired to UART TX. */
size_t tuya_encode(const struct tuya_frame *frame, uint8_t *output,
                   size_t capacity);

#endif
