/* SPDX-License-Identifier: MIT
 * v42.0-50: 3598mV multimeter / 3505mV raw SAADC VDDH.
 * Pure host test; no hardware power or motor traffic.
 */
#include <assert.h>
#include <stdint.h>
#include "p3_vddh_calibration.h"

int main(void)
{
    assert(P3_VDDH_CAL_REFERENCE_MV == 3598U);
    assert(P3_VDDH_CAL_RAW_MV == 3505U);
    assert(p3_vddh_input_estimate_mv(0U) == 0U);
    assert(p3_vddh_input_estimate_mv(3505U) == 3598U);
    assert(p3_vddh_input_estimate_mv(20000U) > 20000U); /* no clamping of normal raw */
    assert(p3_vddh_input_estimate_mv(20001U) == 0U); /* reject invalid raw */
    for (uint32_t raw = 3000U; raw <= 4300U; ++raw) {
        uint32_t corrected = p3_vddh_input_estimate_mv(raw);
        uint32_t expected = (raw * 3598U + 1752U) / 3505U;
        assert(corrected == expected);
        if (raw > 3000U)
            assert(corrected >= p3_vddh_input_estimate_mv(raw - 1U));
    }
    return 0;
}
