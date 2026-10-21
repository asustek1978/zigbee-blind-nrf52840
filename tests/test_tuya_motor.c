/* SPDX-License-Identifier: MIT */
#include "tuya_motor.h"
#include "tuya_frame.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static const uint8_t open_capture[] = {
    0x55,0xaa,0x00,0x06,0x00,0x05,0x01,0x04,0x00,0x01,0x00,0x10
};
static const uint8_t stop_capture[] = {
    0x55,0xaa,0x00,0x06,0x00,0x05,0x01,0x04,0x00,0x01,0x01,0x11
};
static const uint8_t close_capture[] = {
    0x55,0xaa,0x00,0x06,0x00,0x05,0x01,0x04,0x00,0x01,0x02,0x12
};
static const uint8_t set_50_capture[] = {
    0x55,0xaa,0x00,0x06,0x00,0x08,0x02,0x02,0x00,0x04,0x00,0x00,0x00,0x32,0x47
};
/* Captured MCU -> WBR3, classic framing, version 0x03, cmd 0x07. */
static const uint8_t position_62_capture[] = {
    0x55,0xaa,0x03,0x07,0x00,0x08,0x03,0x02,0x00,0x04,0x00,0x00,0x00,0x3e,0x58
};
static const uint8_t dp13_40_capture[] = {
    0x55,0xaa,0x03,0x07,0x00,0x08,0x0d,0x02,0x00,0x04,0x00,0x00,0x00,0x28,0x4c
};

static struct {
    uint8_t id;
    uint32_t number;
    unsigned int count;
} captured;

static void accept_dp(const struct tuya_dp *dp, void *context)
{
    uint32_t decoded = 0U;
    (void)context;
    assert(tuya_dp_as_uint32(dp, &decoded));
    captured.id = dp->id;
    captured.number = decoded;
    captured.count++;
}

static void test_motor_commands(void)
{
    uint8_t encoded[32];

    assert(tuya_motor_encode_action(TUYA_MOTOR_OPEN, encoded, sizeof(encoded))
           == sizeof(open_capture));
    assert(memcmp(encoded, open_capture, sizeof(open_capture)) == 0);

    assert(tuya_motor_encode_action(TUYA_MOTOR_STOP, encoded, sizeof(encoded))
           == sizeof(stop_capture));
    assert(memcmp(encoded, stop_capture, sizeof(stop_capture)) == 0);

    assert(tuya_motor_encode_action(TUYA_MOTOR_CLOSE, encoded, sizeof(encoded))
           == sizeof(close_capture));
    assert(memcmp(encoded, close_capture, sizeof(close_capture)) == 0);

    assert(tuya_motor_encode_position(50U, encoded, sizeof(encoded))
           == sizeof(set_50_capture));
    assert(memcmp(encoded, set_50_capture, sizeof(set_50_capture)) == 0);
}

static void test_invalid_control_and_bounds(void)
{
    uint8_t out[32] = {0};

    assert(tuya_motor_encode_action((enum tuya_motor_action)3, out, sizeof(out)) == 0U);
    assert(tuya_motor_encode_action(TUYA_MOTOR_OPEN, NULL, sizeof(out)) == 0U);
    assert(tuya_motor_encode_action(TUYA_MOTOR_OPEN, out, 11) == 0U);
    assert(tuya_motor_encode_position(101U, out, sizeof(out)) == 0U);
    assert(tuya_motor_encode_position(50U, NULL, sizeof(out)) == 0U);
    assert(tuya_motor_encode_position(50U, out, 14) == 0U);

    assert(tuya_motor_encode_position(0U, out, sizeof(out)) == 15U);
    assert(out[13] == 0x00U);
    assert(tuya_motor_encode_position(100U, out, sizeof(out)) == 15U);
    assert(out[13] == 100U);
}

static void verify_report(const uint8_t *bytes, size_t length,
                          uint8_t expected_id, uint32_t expected_number)
{
    struct tuya_parser parser;
    struct tuya_frame decoded = {0};
    bool frame_done = false;
    size_t i;

    tuya_parser_init(&parser);
    for (i = 0; i < length; ++i) {
        if (tuya_parser_feed(&parser, bytes[i], &decoded)) {
            frame_done = true;
        }
    }
    assert(frame_done);
    assert(decoded.format == TUYA_WIRE_CLASSIC);
    assert(decoded.version == 0x03U);
    assert(decoded.command == 0x07U);
    captured.count = 0U;
    assert(tuya_foreach_report_dp(&decoded, accept_dp, NULL));
    assert(captured.count == 1U);
    assert(captured.id == expected_id);
    assert(captured.number == expected_number);
}

static void test_real_mcu_reports(void)
{
    verify_report(position_62_capture, sizeof(position_62_capture), 3U, 62U);
    verify_report(dp13_40_capture, sizeof(dp13_40_capture), 13U, 40U);
}


static void test_captured_wbr3_startup(void)
{
    /* Real motor wake sequence Oct 19. Check exact classic UART bytes. */
    const uint8_t heartbeat[] = {0x55,0xaa,0,0,0,0,0xff};
    const uint8_t product[] =   {0x55,0xaa,0,1,0,0,0x00};
    const uint8_t mode[] =      {0x55,0xaa,0,2,0,0,0x01};
    const uint8_t query[] =     {0x55,0xaa,0,8,0,0,0x07};
    const uint8_t status03[] =  {0x55,0xaa,0,3,0,1,3,0x06};
    const uint8_t status04[] =  {0x55,0xaa,0,3,0,1,4,0x07};
    uint8_t out[32];

    assert(tuya_motor_encode_request(TUYA_MOTOR_HEARTBEAT, out, sizeof(out)) == 7U);
    assert(memcmp(out, heartbeat, sizeof(heartbeat)) == 0);
    assert(tuya_motor_encode_request(TUYA_MOTOR_PRODUCT_QUERY, out, sizeof(out)) == 7U);
    assert(memcmp(out, product, sizeof(product)) == 0);
    assert(tuya_motor_encode_request(TUYA_MOTOR_WORK_MODE_QUERY, out, sizeof(out)) == 7U);
    assert(memcmp(out, mode, sizeof(mode)) == 0);
    assert(tuya_motor_encode_request(TUYA_MOTOR_STATE_QUERY, out, sizeof(out)) == 7U);
    assert(memcmp(out, query, sizeof(query)) == 0);
    assert(tuya_motor_encode_legacy_network_status(3U, out, sizeof(out)) == 8U);
    assert(memcmp(out, status03, sizeof(status03)) == 0);
    assert(tuya_motor_encode_legacy_network_status(4U, out, sizeof(out)) == 8U);
    assert(memcmp(out, status04, sizeof(status04)) == 0);

    assert(tuya_motor_encode_request((enum tuya_motor_request)0x07, out, sizeof(out)) == 0);
    assert(tuya_motor_encode_request(TUYA_MOTOR_HEARTBEAT, out, 6) == 0);
    assert(tuya_motor_encode_legacy_network_status(5, out, sizeof(out)) == 0);

    /* Captured MCU heartbeat reply, without mistaking it for movement DP. */
    const uint8_t reply[] = {0x55,0xaa,0x03,0,0,1,0,0x03};
    struct tuya_parser parser;
    struct tuya_frame decoded;
    bool received = false;
    tuya_parser_init(&parser);
    for (size_t i=0; i<sizeof(reply); ++i) {
        if (tuya_parser_feed(&parser, reply[i], &decoded)) {
            received = true;
        }
    }
    assert(received);
    assert(decoded.version == 3U && decoded.command == 0U);
    assert(decoded.length == 1U && decoded.data[0] == 0U);
}

int main(void)
{
    test_captured_wbr3_startup();
    test_motor_commands();
    test_invalid_control_and_bounds();
    test_real_mcu_reports();
    puts("P3 observed UART capture regression tests: PASS");
    return 0;
}
