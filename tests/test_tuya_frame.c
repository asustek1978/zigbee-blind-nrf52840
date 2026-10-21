/* SPDX-License-Identifier: MIT */
#include "tuya_frame.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static int dp_calls;
static uint8_t last_id;
static uint32_t last_value;

static void read_dp(const struct tuya_dp *dp, void *unused)
{
    (void)unused;
    dp_calls++;
    last_id = dp->id;
    assert(tuya_dp_as_uint32(dp, &last_value));
}

static bool feed(struct tuya_parser *parser, const uint8_t *buf, size_t size,
                 struct tuya_frame *frame)
{
    bool decoded = false;
    for (size_t i = 0; i < size; ++i) {
        if (tuya_parser_feed(parser, buf[i], frame)) {
            decoded = true;
        }
    }
    return decoded;
}

static void test_classic_report(void)
{
    const uint8_t raw[] = {0x55,0xaa,0,7,0,5,3,1,0,1,1,0x11};
    struct tuya_parser parser;
    struct tuya_frame out;
    tuya_parser_init(&parser);
    dp_calls = 0;
    assert(!feed(&parser, raw, 3, &out));
    assert(feed(&parser, raw + 3, sizeof(raw) - 3, &out));
    assert(out.format == TUYA_WIRE_CLASSIC);
    assert(out.version == 0 && out.command == 7 && out.length == 5);
    assert(tuya_foreach_report_dp(&out, read_dp, NULL));
    assert(dp_calls == 1 && last_id == 3 && last_value == 1);
}

static void test_zigbee_report(void)
{
    struct tuya_parser parser;
    struct tuya_frame out = {0};
    struct tuya_frame input = {0};
    uint8_t raw[TUYA_MAX_FRAME];
    size_t n;
    tuya_parser_init(&parser);
    input.format = TUYA_WIRE_ZIGBEE;
    input.version = 2;
    input.sequence = 0x1234;
    input.command = 6;
    input.length = 8;
    const uint8_t dp[] = {3,2,0,4,0,0,0,82};
    memcpy(input.data, dp, sizeof(dp));
    n = tuya_encode(&input, raw, sizeof(raw));
    assert(n == 17);
    assert(raw[2] == 2 && raw[3] == 0x12 && raw[4] == 0x34);
    dp_calls = 0;
    assert(feed(&parser, raw, n, &out));
    assert(out.format == TUYA_WIRE_ZIGBEE);
    assert(out.sequence == 0x1234 && out.length == 8 && out.command == 6);
    assert(tuya_foreach_report_dp(&out, read_dp, NULL));
    assert(dp_calls == 1 && last_id == 3 && last_value == 82);
    /* Tuya Zigbee DP commands in both directions, not just reports. */
    const uint8_t commands[] = {0x04, 0x05, 0x27, 0x2a, 0x2c};
    for (size_t i = 0; i < sizeof(commands); ++i) {
        input.command = commands[i];
        n = tuya_encode(&input, raw, sizeof(raw));
        dp_calls = 0;
        assert(feed(&parser, raw, n, &out));
        assert(tuya_foreach_report_dp(&out, read_dp, NULL));
        assert(dp_calls == 1 && last_id == 3 && last_value == 82);
    }
    /* Classic 0x06 is also a DP command, not only 0x07. */
    input.format = TUYA_WIRE_CLASSIC;
    input.version = 0;
    input.command = 0x06;
    dp_calls = 0;
    n = tuya_encode(&input, raw, sizeof(raw));
    assert(feed(&parser, raw, n, &out));
    assert(tuya_foreach_report_dp(&out, read_dp, NULL));
    assert(dp_calls == 1);
}

static void test_recovery_and_corruption(void)
{
    uint8_t raw[] = {0x55,0xaa,0,7,0,5,3,1,0,1,1,0x11};
    struct tuya_parser parser;
    struct tuya_frame out;
    tuya_parser_init(&parser);
    const uint8_t noise[] = {1,0,0x55,0x55,0x55,0xaa};
    assert(!feed(&parser, noise, sizeof(noise), &out));
    tuya_parser_init(&parser);
    raw[11] ^= 1U;
    assert(!feed(&parser, raw, sizeof(raw), &out));
    assert(parser.rejected > 0);
    raw[11] ^= 1U;
    assert(feed(&parser, raw, sizeof(raw), &out));
    assert(out.command == 7);
    const uint8_t absurd_header[] = {0x55,0xaa,0,7,0xff,0xff};
    assert(!feed(&parser, absurd_header, sizeof(absurd_header), &out));
    assert(parser.rejected > 1);
    assert(feed(&parser, raw, sizeof(raw), &out));
}

static void test_no_partial_dp_callbacks(void)
{
    struct tuya_frame frame = {.format=TUYA_WIRE_CLASSIC,.command=7,.length=11};
    const uint8_t bad_two_dp[] = {3,4,0,1,40, 13,2,0,4,0,0};
    memcpy(frame.data, bad_two_dp, sizeof(bad_two_dp));
    dp_calls = 0;
    assert(!tuya_foreach_report_dp(&frame, read_dp, NULL));
    assert(dp_calls == 0);
    frame.command = 0x01;  /* Non-DP protocol command. */
    assert(!tuya_foreach_report_dp(&frame, read_dp, NULL));
    frame.format = TUYA_WIRE_ZIGBEE;
    frame.command = 0x04;  /* DP, but second value truncated. */
    assert(!tuya_foreach_report_dp(&frame, read_dp, NULL));
    assert(dp_calls == 0);
}

static void test_encode_limits(void)
{
    struct tuya_frame frame = {.format=TUYA_WIRE_ZIGBEE,.version=2,.length=129};
    uint8_t output[TUYA_MAX_FRAME];
    assert(tuya_encode(&frame, output, sizeof(output)) == 0);
    frame.length = 0;
    assert(tuya_encode(&frame, output, 4) == 0);
    assert(tuya_encode(&frame, output, sizeof(output)) == 9);
    frame.version = 3;
    assert(tuya_encode(&frame, output, sizeof(output)) == 0);
}

int main(void)
{
    test_classic_report();
    test_zigbee_report();
    test_recovery_and_corruption();
    test_no_partial_dp_callbacks();
    test_encode_limits();
    puts("Tuya classic + Zigbee MCU frame parser unit tests: PASS");
    return 0;
}
