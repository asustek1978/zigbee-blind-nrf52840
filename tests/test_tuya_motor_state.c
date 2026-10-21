/* SPDX-License-Identifier: MIT */
#include "tuya_motor_state.h"
#include "tuya_frame.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static struct tuya_frame frame(uint8_t ver, uint8_t cmd, const uint8_t *dp,
                               size_t length)
{
    struct tuya_frame f = {.format=TUYA_WIRE_CLASSIC,.version=ver,
                           .command=cmd,.length=(uint16_t)length};
    assert(length <= TUYA_MAX_DATA);
    if (length != 0) memcpy(f.data, dp, length);
    return f;
}

static void test_from_original_wbr3_capture(void)
{
    /* DP3=62 and DP13=40, both classic version 0x03 cmd 0x07. */
    const uint8_t pos[] = {3,2,0,4,0,0,0,62};
    const uint8_t bat[] = {13,2,0,4,0,0,0,40};
    const uint8_t stop[] = {1,4,0,1,1};
    struct tuya_motor_state state = {0};
    struct tuya_motor_changes ch = {0};
    struct tuya_frame f = frame(3,7,pos,sizeof(pos));
    assert(tuya_motor_apply_report(&f,&state,&ch));
    assert(state.position_valid && state.position == 62 && ch.position);
    assert(!state.battery_valid && !state.motion_valid);
    f = frame(3,7,bat,sizeof(bat));
    assert(tuya_motor_apply_report(&f,&state,&ch));
    assert(state.battery_valid && state.battery == 40 && ch.battery && !ch.position);
    f = frame(0,7,stop,sizeof(stop));
    assert(tuya_motor_apply_report(&f,&state,&ch));
    assert(state.motion_valid && state.motion == 1 && ch.motion);
    f = frame(0,7,stop,sizeof(stop));
    assert(tuya_motor_apply_report(&f,&state,&ch));
    assert(!ch.motion && state.report_count == 4);
}

static void test_multi_dp_and_atomic_malformed_rejection(void)
{
    const uint8_t both[] = {3,2,0,4,0,0,0,99,13,2,0,4,0,0,0,75};
    const uint8_t malformed[] = {3,2,0,4,0,0,0,100,13,2,0,4,0,0};
    struct tuya_motor_state state = {0};
    struct tuya_motor_changes change = {0};
    struct tuya_frame f = frame(3,7,both,sizeof(both));
    assert(tuya_motor_apply_report(&f,&state,&change));
    assert(state.position == 99 && state.battery == 75);
    assert(change.position && change.battery);
    f = frame(3,7,malformed,sizeof(malformed));
    assert(!tuya_motor_apply_report(&f,&state,&change));
    assert(state.position == 99 && state.battery == 75 && state.report_count == 1);
}

static void test_rejects_bad_types_ranges_and_directions(void)
{
    const uint8_t invalid_position[] = {3,2,0,4,0,0,0,101};
    const uint8_t invalid_battery[] = {13,2,0,4,0,0,0,200};
    const uint8_t wrong_type[] = {3,4,0,1,60};
    const uint8_t target[] = {2,2,0,4,0,0,0,55};
    struct tuya_motor_state state = {0};
    struct tuya_motor_changes changes = {0};
    struct tuya_frame f = frame(3,7,invalid_position,sizeof(invalid_position));
    assert(tuya_motor_apply_report(&f,&state,&changes));
    assert(!state.position_valid);
    f = frame(3,7,invalid_battery,sizeof(invalid_battery));
    assert(tuya_motor_apply_report(&f,&state,&changes));
    assert(!state.battery_valid);
    f = frame(3,7,wrong_type,sizeof(wrong_type));
    assert(tuya_motor_apply_report(&f,&state,&changes));
    assert(!state.position_valid);
    f = frame(3,7,target,sizeof(target));
    assert(tuya_motor_apply_report(&f,&state,&changes));
    assert(!state.position_valid);
    f = frame(0,6,target,sizeof(target));
    assert(!tuya_motor_apply_report(&f,&state,&changes));
    f.format = TUYA_WIRE_ZIGBEE;
    assert(!tuya_motor_apply_report(&f,&state,&changes));
    assert(state.report_count == 4);
}

static void test_unknown_and_empty(void)
{
    struct tuya_motor_state state = {0};
    struct tuya_motor_changes change = {0};
    const uint8_t other[] = {102,2,0,4,0,0,0,5};
    struct tuya_frame f = frame(3,7,other,sizeof(other));
    assert(tuya_motor_apply_report(&f,&state,&change));
    assert(!change.position && !change.battery && !change.motion);
    f.length = 0;
    assert(!tuya_motor_apply_report(&f,&state,&change));
    assert(!tuya_motor_apply_report(NULL,&state,&change));
}

int main(void)
{
    test_from_original_wbr3_capture();
    test_multi_dp_and_atomic_malformed_rejection();
    test_rejects_bad_types_ranges_and_directions();
    test_unknown_and_empty();
    puts("Tuya P3 parsed report/state tests: PASS");
    return 0;
}
