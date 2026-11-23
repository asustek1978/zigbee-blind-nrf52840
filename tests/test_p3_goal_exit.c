/* SPDX-License-Identifier: MIT
 * Pure host timing policy test: no MCU command, power-state or Zephyr runtime.
 */
#include <assert.h>
#include "p3_goal_exit.h"

int main(void)
{
    assert(!p3_goal_silence_due(false, 3000U, true));
    assert(!p3_goal_silence_due(true, 0U, true));
    assert(!p3_goal_silence_due(true, 2999U, true));
    assert(!p3_goal_silence_due(true, 3000U, false));
    assert(p3_goal_silence_due(true, 3000U, true));
    assert(p3_goal_silence_due(true, 3500U, true));
    assert(!p3_goal_silence_due(false, 120000U, true));
    return 0;
}
