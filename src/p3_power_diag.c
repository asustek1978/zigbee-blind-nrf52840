/* SPDX-License-Identifier: MIT
 * v42.0-45 safe diagnostics: low-duty-cycle SAADC of internal VDDH/5, VDD.
 * These are supply rails, not a verified battery tap. Never convert to SOC.
 * ADC configuration uses Zephyr's nRF SAADC driver to avoid ownership races.
 */
#include <errno.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/devicetree.h>
#include <zephyr/sys/atomic.h>

#include "p3_power_diag.h"
#include "p3_vddh_calibration.h"
#include "p3_soft_uart.h"

#define P3_SAADC_NODE DT_PATH(zephyr_user)
#define P3_SAADC_SAMPLE_PERIOD_MS 600000U

static const struct adc_dt_spec rail_vddh =
    ADC_DT_SPEC_GET_BY_IDX(P3_SAADC_NODE, 0);
static const struct adc_dt_spec rail_vdd =
    ADC_DT_SPEC_GET_BY_IDX(P3_SAADC_NODE, 1);
static atomic_t rail_vddh_mv;
static atomic_t rail_vdd_mv;
static atomic_t rail_valid_mask;
static atomic_t rail_samples;
static atomic_t rail_last_ms;
static atomic_t rail_last_error;

/* Samples only when direct RX inactive; configure Zephyr driver once.
 * Unused VDDH output remains a diagnostic, not a BatteryVoltage report.
 */
static int sample_rail(const struct adc_dt_spec *channel, uint32_t scale,
                       atomic_t *destination)
{
    int16_t raw = 0;
    struct adc_sequence seq = {0};
    int rc = adc_sequence_init_dt(channel, &seq);
    if (rc != 0) return rc;
    seq.buffer = &raw;
    seq.buffer_size = sizeof(raw);
    /* Nordic SAADC supports offset calibration via the Zephyr ADC API.
     * One 16x oversampled, offset-calibrated conversion per channel,
     * sampled only every 10 minutes. Calibration does NOT remove gain
     * / reference tolerances or prove VDDH is the P3 battery rail.
     */
    seq.calibrate = true;
    seq.oversampling = 4; /* 2^4 = 16 conversions per reading */
    rc = adc_read_dt(channel, &seq);
    if (rc != 0) return rc;
    int32_t mv = raw;
    rc = adc_raw_to_millivolts_dt(channel, &mv);
    if (rc != 0) return rc;
    if (mv <= 0 || mv > 5000 || ((uint32_t)mv * scale) > 20000U) {
        return -ERANGE;
    }
    atomic_set(destination, (atomic_val_t)((uint32_t)mv * scale));
    return 0;
}

static void p3_power_diag_thread(void *a, void *b, void *c)
{
    ARG_UNUSED(a); ARG_UNUSED(b); ARG_UNUSED(c);
    int rc_h = -ENODEV, rc_v = -ENODEV;
    if (adc_is_ready_dt(&rail_vddh)) {
        rc_h = adc_channel_setup_dt(&rail_vddh);
    }
    if (adc_is_ready_dt(&rail_vdd)) {
        rc_v = adc_channel_setup_dt(&rail_vdd);
    }
    for (;;) {
        if (!p3_soft_uart_direct_rx_active()) {
            uint32_t mask = 0;
            int err = 0;
            if (rc_h == 0) {
                err = sample_rail(&rail_vddh, 5U, &rail_vddh_mv);
                if (err == 0) mask |= 1U;
            }
            if (rc_v == 0) {
                const int rc = sample_rail(&rail_vdd, 1U, &rail_vdd_mv);
                if (rc == 0) mask |= 2U;
                else if (err == 0) err = rc;
            }
            if (rc_h != 0 && err == 0) err = rc_h;
            if (rc_v != 0 && err == 0) err = rc_v;
            atomic_set(&rail_valid_mask, (atomic_val_t)mask);
            atomic_set(&rail_last_error, (atomic_val_t)err);
            atomic_set(&rail_last_ms, (atomic_val_t)k_uptime_get_32());
            atomic_inc(&rail_samples);
            /* No unsolicited Zigbee traffic here. ZBOSS Basic values are
             * mirrored by the existing diagnostic refresh loop.
             */
        }
        k_sleep(K_MSEC(P3_SAADC_SAMPLE_PERIOD_MS));
    }
}
K_THREAD_DEFINE(p3_power_diag_tid, 1280, p3_power_diag_thread,
                NULL, NULL, NULL, K_PRIO_PREEMPT(10), 0, 30000);

uint32_t p3_power_diag_vddh_mv(void) { return (uint32_t)atomic_get(&rail_vddh_mv); }
/* Software-only scaling of the existing VDDH measurement. Zero when the
 * most recent sample failed. Sampling cadence and RX ownership untouched.
 */
uint32_t p3_power_diag_vddh_calibrated_mv(void)
{
    if ((p3_power_diag_valid_mask() & 1U) == 0U) return 0U;
    return p3_vddh_input_estimate_mv(p3_power_diag_vddh_mv());
}
uint32_t p3_power_diag_vdd_mv(void) { return (uint32_t)atomic_get(&rail_vdd_mv); }
uint32_t p3_power_diag_valid_mask(void) { return (uint32_t)atomic_get(&rail_valid_mask); }
uint32_t p3_power_diag_samples(void) { return (uint32_t)atomic_get(&rail_samples); }
uint32_t p3_power_diag_last_ms(void) { return (uint32_t)atomic_get(&rail_last_ms); }
uint32_t p3_power_diag_last_error(void) { return (uint32_t)atomic_get(&rail_last_error); }
