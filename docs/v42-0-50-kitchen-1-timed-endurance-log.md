# P3 kitchen blind #1 — timed battery/endurance test, v42.0-50

## Start and scope

- **Record opened:** 2026-11-28 21:21 UTC+03:00 (18:21 UTC).
- **Actual discharge T0:** time when external charger is DISCONNECTED and
  the blind is left on battery power; if later than the record timestamp,
  note the later timestamp for an accurate runtime.
- **Device:** #1 kitchen right P3, nRF52840 HW-954, experimental firmware
  v42.0-50. #2 kitchen left stays on v41.3.
- **Objective:** battery runtime and receive/control reliability under
  NORMAL daily operation, not a 16-command/day stress test.
- **Test regime:** normal existing Home Assistant open/close automations
  (nominally two motions/day) and manual interactions only when needed.
  No extra synthetic commands, re-flashes or charging during the run.
- **Checkpoints:** ~24 h, ~48 h, then daily up to 14 days, and at
  first 90%, 80%, 50%, 20%, 10%, or loss of motor/communication.
  End when low battery or stable power loss requires recharging; do
  not deep-discharge a lithium pack.

## Baseline (last user supplied readings before test start)

| Measure | Baseline | Note |
|---|---:|---|
| MCU DP13 Battery | 100% | Coarse motor estimate, may lag |
| Cover position | 20% | Last posted, can change by automation |
| Multimeter at HW-954 input | 3884 mV | User measurement |
| nRF raw VDDH | 3870 mV | Independent SAADC |
| nRF v42.0-50 displayed calibrated VDDH | 3973 mV | **Invalid correction for board #1** |
| Experimental board #1 estimate from raw | ~3885 mV | 3870 + 15 mV, offline analysis only |
| nRF VDD | 3325 mV | Regulated rail, NOT battery |
| SAADC samples | 39 | 10-min low-duty sampling |
| SAADC last uptime | 23,430,118 ms | Stored in firmware, time since power-on |
| SAADC last error | 0 | Mask 3 (both channels valid) |
| Tuya frames OK / rejected | 888 / 0 | Since last boot |
| RX framing errors, ring overflows | 0 / 0 | |
| UART handoff errors, hardware errors | 0 / 0 | |
| PPI starts / fallbacks / late IRQs | 287 / 0 / 0 | |
| HW direct enters / exits / active | 9 / 9 / 0 | |
| Session total | 9 | |
| STOP-guard / goal-silence / watchdog exits | 5 / 2 / **2** | Two watchdogs before T0: monitor delta |
| Commands queued / TX OK / TX failed | 14 / 14 / 0 | |
| Position reported / STOP reported | 7 / 2 | |
| Command timeout no report / no target | 0 / 0 | |
| LQI | 142 | Snapshot |

### What to collect at each checkpoint

- Current timestamp and days/hours since T0
- Battery % (original P3 MCU DP13) and actual curtain position
- `saadc_vddh_mv` RAW and `saadc_samples`; fresh sample needed to
  compare voltage (sample every 10 minutes), `saadc_valid_mask` and
  `saadc_last_error`.
- Optional multimeter **at INPUT of HW-954** with charger disconnected,
  motor stopped, timed near latest SAADC sample.
- `session_total`, `session_stop_guard_exits`,
  `session_goal_silence_exits`, `session_watchdog_exits`,
  `hw_direct_active`, `hw_direct_enters`, `hw_direct_exits`;
  `tuya_frames_ok`, `tuya_frames_rejected`, `rx_framing_errors`,
  `rx_ring_overflows`, `ppi_fallbacks`, `uart_hardware_errors`,
  `cmd_tx_ok`, `cmd_tx_failed`, `cmd_timeout_no_report`,
  `cmd_timeout_no_target`.
- Battery runtime ends when the battery is too low to operate the
  motor normally or safely, not solely when DP13 reaches zero.

## Checkpoint 1 — approximately +30 minutes device uptime

User supplied the following fresh Zigbee2MQTT `Refresh bench diagnostics` snapshot
after the baseline. This is +1,800,009 ms (30 min) in `saadc_last_ms`;
the wall-clock sample time and actual charger-disconnection time were not
independently confirmed. No new multimeter reading was taken.

| Parameter | Baseline | Checkpoint 1 | Change |
|---|---:|---:|---:|
| DP13 battery | 100% | 100% | 0 pp |
| Cover position | 20% | 20% | unchanged |
| Raw SAADC VDDH | 3870 mV | 3870 mV | 0 mV |
| Displayed v50 board-#3 calibration | 3973 mV | 3973 mV | unchanged, INVALID for #1 |
| **Offline estimated board-#1 input (RAW+15)** | **3885 mV** | **3885 mV** | **0 mV** |
| SAADC samples | 39 | 42 | +3 |
| SAADC last uptime | 23,430,118 ms | 25,230,127 ms | +1,800,009 ms |
| SAADC valid mask / error | 3 / 0 | 3 / 0 | unchanged |
| Tuya frames OK / rejected | 888 / 0 | 888 / 0 | no new UART frames |
| HW direct enters / exits / active | 9 / 9 / 0 | 9 / 9 / 0 | unchanged |
| Session total | 9 | 9 | unchanged |
| STOP / goal-silence / watchdog exits | 5 / 2 / 2 | 5 / 2 / 2 | no new watchdog |
| PPI starts / fallbacks / late IRQs | 287 / 0 / 0 | 287 / 0 / 0 | unchanged |
| UART framing / overflow / hardware errors | 0 / 0 / 0 | 0 / 0 / 0 | unchanged |
| Commands TX OK / failed | 14 / 0 | 14 / 0 | unchanged |
| Linkquality | 142 | 142 | unchanged |

Interpretation: stable short idle snapshot and fresh SAADC measurements,
not an estimate of current draw or long-term runtime. Firmware remains
v42.0-50 and **no firmware calibration constant was changed**.

## Calibration caveat

- #3 reference coefficient is 3598/3505 (~+2.65%). The same coefficient
  gave +72 mV error at 3.331 V and +89 mV error at 3.884 V on #1.
- #1 RAW vs meter errors: -16 mV (3315 vs 3331), -14 mV
  (3870 vs 3884). Estimated suitable #1 calibration: +15 mV.
- **No new firmware with +15 mV was flashed in this record.**
  Do not treat displayed `saadc_vddh_calibrated_mv` as accurate for #1.
  Use RAW +15 mV in post-processing during this test.
- This is estimated INPUT voltage on HW-954, not BAT+ voltage.

## Stop / investigate conditions

- Motor stops operating, position calibration lost after normal reset,
  spontaneous resets, errors or watchdog count persistently increases,
  or sustained current draw is unexpectedly high.
- One watchdog exit does not prove motor failure; record session
  circumstances and compare delta, not raw cumulative values.
- Test does not independently collect Zigbee telemetry: manual
  snapshots from Zigbee2MQTT are needed unless external monitoring is
  separately installed.
