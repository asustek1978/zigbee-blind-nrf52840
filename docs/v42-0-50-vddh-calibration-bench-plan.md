# P3 nRF52840 v42.0-50 — input-rail SAADC calibration bench

**Do not install on kitchen motors #1 or #2 until the following test passes
on test motor #3.** v41.3 remains the kitchen rollback firmware.

## Scope of change

- Preserve all v42.0-49 UART, GPIOTE/PPI, Zigbee motor control,
  calibration and energy-saving behavior.
- New read-only Zigbee Basic (endpoint 10) **0xF19A**:
  `saadc_vddh_calibrated_mv`, in millivolts. Source is the existing
  internal SAADC VDDH measurement, not a new battery-sense pin.
- One-point raw-to-board-input correction measured on the **test HW-954**:
  multimeters VICHY VC99 and GVDA GD168B at HW-954 power input **3598 mV**,
  contemporaneous raw SAADC VDDH **3505 mV**.
  Formula: `round(raw * 3598 / 3505)` (32-bit integer, round nearest).
- Original Basic **0xF16A** `saadc_vddh_mv` is unmodified.
- Standard ZCL Power Configuration `BatteryVoltage` remains unknown
  (no fabricated voltage). `Battery` percentage remains motor DP13.
- BAT+ at that earlier measurement was **3687 mV**. Its 89 mV difference
  from HW-954 input is NOT compensated: the calibrated attribute is **not**
  direct battery-terminal voltage or state of charge.
- A single calibration point does not establish accuracy from 3.0 to 4.2 V.
  Coefficients may differ between HW-954 boards; recheck #1 separately.

## Test order

1. Confirm GitHub Actions succeeded for the matching `v42.0-50` commit,
   including `test_p3_vddh_calibration`, all firmware tests,
   Nordic build, verified UF2 layout and artifact upload.
2. Install the updated `p3_nrf52840.mjs` external converter,
   restart Zigbee2MQTT. Legacy v41.3 kitchen motors remain cover+battery;
   only SWBuildID 42.0-50 can expose/read 0xF19A.
3. Save v42.0-49 UF2 for rollback; flash **test motor #3 only**.
   Confirm Zigbee Basic SWBuildID `42.0-50`. Refresh/re-interview
   test #3 only if softwareBuildID metadata remains stale.
4. Wait for fresh SAADC sample (`saadc_samples` increases, every 10 min).
   Require `saadc_valid_mask & 1 == 1` and `saadc_last_error == 0`.
   Manually GET 0xF16A and 0xF19A, or refresh bench diagnostics.
5. With motor stopped and no charger, measure BOTH input power wires
   of HW-954 with the multimeters around the SAME sample time.
   Expect raw near previously observed 3505 mV to yield corrected ~3598 mV.
   Target agreement at bench point: within **30 mV**, or investigate
   sampling lag, wiring, rail identity and meter accuracy.
   Never assume it equals the battery-terminal voltage.
6. Verify position (0, 50, 100%), local buttons, STOP, retained motor
   calibration after power cycle, idle PPI wake, no watchdog increase,
   and no persistent rise in UART errors vs the baseline.
   Watch current consumption separately: a voltage estimate is not
   evidence of low idle current.
7. Only if #3 passes may #1 be considered for a controlled field test,
   after confirming #1 calibration survives power cycle and retaining
   v41.3 rollback. Recheck #1 own ADC vs multimeter since gain may be
   board-specific. Leave #2 on v41.3 until field results are good.

## Converter firmware/version gates

- `41.3`: cover + battery only; no v42 diagnostics requested
- `42.0-48`: 45 bench fields, no goal counter or calibration
- `42.0-49`: 46 bench fields, goal counter, no calibration
- `42.0-50`: 47 bench fields, goal counter and calibrated voltage

**Safety:** This build remains experimentally UNVERIFIED on a physical
HW-954 until user measurements confirm it. Do not silently recalibrate
or change motor parameters.
