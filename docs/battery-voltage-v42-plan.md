# HW-954 battery voltage sensing — v42 investigation (NOT FLASHABLE)

Status: research only. Stable firmware v41.3 and main branch must not be modified.

## Existing firmware (verified on main)
- prj.conf has no SAADC configuration.
- app.overlay contains the P0.11 calibration key, no ADC input.
- src/main.c initializes Power Configuration BatteryVoltage to 0xFF (unknown).
- BatteryRemainingPercentage is forwarded from Tuya motor UART DP13, not measured by the nRF52840.
- Zigbee endpoint 10 already exposes Power Configuration BatteryVoltage (0x0020, uint8, 100 mV units), but not as reportable. Zigbee2MQTT converter currently exposes only battery percentage.

## Known hardware observations
- HW-954 with USB disconnected: RAW measured 3.85 V, B+ measured 0.85 V (user measurement; wiring and measurement conditions need confirmation).
- Li-ion 1S 2000 mAh battery powers the P3 motor; SS24 diode is in the HW-954 supply path.
- HW-954 schematic / RAW-to-VDDH continuity NOT confirmed. Do not infer that VDDH follows RAW.
- Avoid charging through the HW-954 USB port until charging path and SS24 orientation are confirmed.

## Possible sensing paths
1. SAADC internal VDDHDIV5 (PSEL=0x0D), only if VDDH is actually powered by the battery-side rail and nRF52840 variant/power mode supports it.
2. Board-specific ADC divider if schematic confirms one. Other nice!nano-compatible revisions differ; never assume AIN2 or another GPIO is connected.
3. External switched/high-resistance divider as a last resort, with input voltage limits respected.

## v42 experimental implementation acceptance criteria
- Keep UART motor control, Zigbee pairing, calibration, NVS, sleepy End Device behavior and DP13 battery reporting unchanged.
- Add compile-time option disabled by default; SAADC initialization failure must not block Zigbee startup.
- Sample infrequently (e.g. every 30–60 min), avoid continuous ADC operation, average several samples.
- Verify ADC gain/reference and scaling against Nordic nRF52840 SAADC documentation and actual HW-954 supply topology.
- Report BatteryVoltage in 100 mV Zigbee ZCL units, with unknown 0xFF on invalid readings. Prefer retaining a millivolt diagnostic for precise validation.
- Mark voltage reporting read-only/reportable only after testing; update Z2M converter accordingly.
- Do not derive percentage from voltage before validating chemistry, discharge curve and SS24 voltage drop under load.
- Build CI, flash and bench-test on spare board before any production motor update.

## Verification measurements
- With USB disconnected: GND-to-RAW, GND-to-B+, GND-to-VCC, battery terminals before SS24 and HW-954 supply after SS24.
- With power disconnected, continuity RAW-to-B+ and RAW-to-VDDH only if actual chip pad or schematic is identified safely; do not probe microscopic chip pins in situ.
- Log battery voltage before/after a curtain movement to evaluate load sag.

## Sources
- Nordic nRF52840 product specification, SAADC VDDHDIV5 and power supply modes: https://docs.nordicsemi.com/r/bundle/ps_nrf52840/page/power.html
- nice!nano pinout/schematic: https://nicekeyboards.com/docs/nice-nano/pinout-schematic/
- nRFMicro wiki battery comparisons: https://github.com/joric/nrfmicro/wiki/Batteries
