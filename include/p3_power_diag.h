/* SPDX-License-Identifier: MIT
 * v42.0-44: diagnostic INTERNAL power rails, NOT validated BAT+.
 */
#pragma once
#include <stdint.h>

uint32_t p3_power_diag_vddh_mv(void); /* VDDH via VDDH/5, NOT proven battery */
uint32_t p3_power_diag_vddh_calibrated_mv(void); /* estimate HW-954 input, NOT BAT+ */
uint32_t p3_power_diag_vdd_mv(void);  /* SoC VDD rail */
uint32_t p3_power_diag_valid_mask(void); /* bit0=VDDH bit1=VDD */
uint32_t p3_power_diag_samples(void);
uint32_t p3_power_diag_last_ms(void);
uint32_t p3_power_diag_last_error(void);
