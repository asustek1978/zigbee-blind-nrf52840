/* SPDX-License-Identifier: MIT */
#include "tuya_frame.h"

#include <string.h>

void tuya_parser_init(struct tuya_parser *parser)
{
    memset(parser, 0, sizeof(*parser));
}

static void parser_reset(struct tuya_parser *parser, uint8_t last_byte)
{
    parser->used = 0;
    parser->expected = 0;
    parser->rejected++;
    if (last_byte == 0x55U) {
        parser->buffer[0] = last_byte;
        parser->used = 1;
    }
}

bool tuya_parser_feed(struct tuya_parser *parser, uint8_t byte,
                      struct tuya_frame *out)
{
    uint16_t header_size;
    uint16_t length_offset;
    uint16_t payload_length;
    uint16_t expected;
    uint16_t i;
    uint8_t sum = 0;

    if (parser->used == 0U) {
        if (byte == 0x55U) {
            parser->buffer[0] = byte;
            parser->used = 1;
        }
        return false;
    }

    if (parser->used == 1U) {
        if (byte == 0xaaU) {
            parser->buffer[1] = byte;
            parser->used = 2;
        } else if (byte != 0x55U) {
            parser_reset(parser, byte);
        }
        return false;
    }

    if (parser->used >= TUYA_MAX_FRAME) {
        parser_reset(parser, byte);
        return false;
    }
    parser->buffer[parser->used++] = byte;

    if (parser->used < 3U) {
        return false;
    }
    /* Version 0x02 denotes Tuya Zigbee-MCU framing with 16-bit sequence. */
    header_size = parser->buffer[2] == 0x02U ? 8U : 6U;
    length_offset = (uint16_t)(header_size - 2U);

    if (parser->used == header_size) {
        payload_length = (uint16_t)(((uint16_t)parser->buffer[length_offset] << 8) |
                                     parser->buffer[length_offset + 1U]);
        if (payload_length > TUYA_MAX_DATA) {
            parser_reset(parser, byte);
            return false;
        }
        parser->expected = (uint16_t)(header_size + payload_length + 1U);
    }

    expected = parser->expected;
    if (expected == 0U || parser->used < expected) {
        return false;
    }
    if (parser->used != expected) {
        parser_reset(parser, byte);
        return false;
    }

    for (i = 0; i < expected - 1U; ++i) {
        sum = (uint8_t)(sum + parser->buffer[i]);
    }
    if (sum != parser->buffer[expected - 1U]) {
        parser_reset(parser, byte);
        return false;
    }

    out->format = header_size == 8U ? TUYA_WIRE_ZIGBEE : TUYA_WIRE_CLASSIC;
    out->version = parser->buffer[2];
    out->sequence = header_size == 8U ?
        (uint16_t)(((uint16_t)parser->buffer[3] << 8) | parser->buffer[4]) : 0U;
    out->command = parser->buffer[header_size - 3U];
    out->length = (uint16_t)(expected - header_size - 1U);
    if (out->length > 0U) {
        memcpy(out->data, &parser->buffer[header_size], out->length);
    }
    parser->used = 0U;
    parser->expected = 0U;
    return true;
}

static bool is_dp_report(const struct tuya_frame *frame)
{
    if (frame->format == TUYA_WIRE_ZIGBEE) {
        /* Tuya Zigbee UART v0x02 DP commands, both directions.
         * 0x04: module->MCU set; 0x05: MCU response;
         * 0x06/0x2c: MCU reports; 0x2a: group; 0x27: advertising.
         * Acknowledgment frames of length 0/1 are not DP structures.
         */
        return frame->command == 0x04U || frame->command == 0x05U ||
               frame->command == 0x06U || frame->command == 0x27U ||
               frame->command == 0x2aU || frame->command == 0x2cU;
    }
    /* Classic Tuya UART: 0x06 set and 0x07 MCU DP report. */
    return frame->command == 0x06U || frame->command == 0x07U;
}

bool tuya_foreach_report_dp(const struct tuya_frame *frame,
                            tuya_dp_callback_t callback, void *user)
{
    size_t offset = 0;
    size_t remaining;
    size_t length;
    int pass;

    if (!is_dp_report(frame) || !callback || frame->length > TUYA_MAX_DATA) {
        return false;
    }
    /* Validate whole payload before delivering any DP value. */
    for (pass = 0; pass < 2; ++pass) {
        offset = 0;
        while (offset < frame->length) {
            struct tuya_dp dp;
            remaining = (size_t)frame->length - offset;
            if (remaining < 4U) {
                return false;
            }
            length = ((size_t)frame->data[offset + 2U] << 8) |
                      frame->data[offset + 3U];
            if (length > remaining - 4U) {
                return false;
            }
            dp.id = frame->data[offset];
            dp.type = frame->data[offset + 1U];
            dp.length = (uint16_t)length;
            dp.value = &frame->data[offset + 4U];
            if (pass == 1) {
                callback(&dp, user);
            }
            offset += 4U + length;
        }
    }
    return true;
}

bool tuya_dp_as_uint32(const struct tuya_dp *dp, uint32_t *value)
{
    if (dp->type == 0x02U && dp->length == 4U) {
        *value = ((uint32_t)dp->value[0] << 24) |
                 ((uint32_t)dp->value[1] << 16) |
                 ((uint32_t)dp->value[2] << 8) | dp->value[3];
        return true;
    }
    if ((dp->type == 0x04U || dp->type == 0x01U) && dp->length == 1U) {
        *value = dp->value[0];
        return true;
    }
    return false;
}

size_t tuya_encode(const struct tuya_frame *frame, uint8_t *output,
                   size_t capacity)
{
    size_t header_size = frame->format == TUYA_WIRE_ZIGBEE ? 8U : 6U;
    size_t full_size = header_size + frame->length + 1U;
    size_t i;
    uint8_t sum = 0U;

    if (frame->length > TUYA_MAX_DATA || capacity < full_size ||
        (frame->format != TUYA_WIRE_CLASSIC && frame->format != TUYA_WIRE_ZIGBEE)) {
        return 0U;
    }
    output[0] = 0x55U;
    output[1] = 0xaaU;
    output[2] = frame->version;
    if (frame->format == TUYA_WIRE_ZIGBEE) {
        if (frame->version != 0x02U) {
            return 0U;
        }
        output[3] = (uint8_t)(frame->sequence >> 8);
        output[4] = (uint8_t)frame->sequence;
    } else if (frame->version == 0x02U) {
        return 0U;
    }
    output[header_size - 3U] = frame->command;
    output[header_size - 2U] = (uint8_t)(frame->length >> 8);
    output[header_size - 1U] = (uint8_t)frame->length;
    if (frame->length > 0U) {
        memcpy(&output[header_size], frame->data, frame->length);
    }
    for (i = 0; i < full_size - 1U; ++i) {
        sum = (uint8_t)(sum + output[i]);
    }
    output[full_size - 1U] = sum;
    return full_size;
}
