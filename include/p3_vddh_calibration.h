/* SPDX-License-Identifier: MIT
 * v42.0-50: ONE-POINT bench calibration of internal VDDH telemetry.
 * User's two meters: 3598 mV on HW-954 power input; SAADC: 3505 mV.
 * This is an estimate of BOARD INPUT voltage, NOT BAT+ voltage.
 * Raw SAADC and standard ZCL BatteryVoltage remain untouched.
 */
#pragma once
#include <stdint.h>

#define P3_VDDH_CAL_REFERENCE_MV 3598U
#define P3_VDDH_CAL_RAW_MV 3505U

static inline uint32_t p3_vddh_input_estimate_mv(uint32_t raw_mv)
{
    /* Integer rounding half-up; never turn unknown zero into a fake voltage.
     * Raw SAADC measurements are limited to <=20000 mV; 32-bit math is safe.
     */
    if (raw_mv == 0U || raw_mv > 20000U) return 0U;
    return (raw_mv * P3_VDDH_CAL_REFERENCE_MV +
            P3_VDDH_CAL_RAW_MV / 2U) / P3_VDDH_CAL_RAW_MV;
}
