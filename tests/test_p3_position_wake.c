/* SPDX-License-Identifier: MIT */
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include "p3_position_wake.h"

int main(void)
{
    struct p3_position_wake_state s = {0};
    assert(!p3_position_wake_fresh_episode(&s, 25, 1000));
    assert(!p3_position_wake_fresh_episode(&s, 25, 50000));
    assert(p3_position_wake_fresh_episode(&s, 26, 50001));
    assert(!p3_position_wake_fresh_episode(&s, 27, 51000));
    assert(!p3_position_wake_fresh_episode(&s, 27, 90000));
    assert(p3_position_wake_fresh_episode(&s, 28, 92000));
    assert(!p3_position_wake_fresh_episode(&s, 101, 150000));
    assert(s.last_position == 28);
    assert(!p3_position_wake_fresh_episode(NULL, 50, 1));
    struct p3_position_wake_state wrap = {
        .initialized = true, .last_position = 10, .last_change_ms = UINT32_MAX-4000U
    };
    assert(p3_position_wake_fresh_episode(&wrap, 20, 7000U));
    assert(!p3_position_wake_fresh_episode(&wrap, 21, 7001U));
    return 0;
}
