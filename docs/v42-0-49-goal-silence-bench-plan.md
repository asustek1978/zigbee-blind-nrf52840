# v42.0-49: experimental goal + 3-second UART silence exit

**Not a production firmware.** The motor on test curtain #3 must be attended during bench motion.
Leave both kitchen curtains on validated v41.3; do not flash them.

## What changed from v42.0-48

- Keep GPIOTE/PPI first-bit synchronization, GPIO/TIMER3 fallback, Tuya framing,
  UARTE1 TX, local-button behavior and the existing calibration rules.
- Preserve the **fresh DP1=STOP + 1,500 ms** release guard.
- Preserve the **absolute 120,000 ms** hardware RX watchdog.
- Add an earlier release **only** when all of the following hold:
  1. A new explicit movement command was successfully transmitted in this RX session.
  2. A fully parsed, accepted MCU report has fresh `DP3` exactly equal to the latest TX target.
  3. A later valid `DP3` has not contradicted that target.
  4. There have been no UARTE0 RXDRDY bytes for at least **3,000 ms**.
  5. The software RX ring is drained; no DMA ENDRX is pending.
- A newly *queued* explicit motor command cancels prior goal evidence immediately,
  before its UARTE1 TX finishes. A new successful TX target must be reported afresh.
- The new exit reason is **5 = GOAL_SILENCE**. Read-only cumulative ZCL Basic
  attribute **0xF199 = session_goal_silence_exits**.
- No STOP, calibration, retry, wake probe, or other motor frame is sent by
  this release policy. Only receiver power mode is affected.

## Baseline captured in v42.0-48

- 11 completed hardware RX sessions; 7 terminated by the 120-second watchdog.
- 6 of those 7 watchdog exits had a validated target DP3 but no DP1 STOP.
- 893 accepted Tuya frames; 0 rejected; 0 software framing errors;
  0 ring overflows; 0 PPI fallbacks; 0 late PPI IRQs.
- Test motor battery showed 70%, not a current-consumption measurement.

## Installation / Zigbee2MQTT

1. Only use the UF2 artifact from a **successful v42.0-49 GitHub Actions**
   workflow with passing host, compilation, and UF2 range checks. The artifact
   remains `BENCH-UNVERIFIED` until physical testing.
2. Replace exactly **one** loaded external converter, `p3_nrf52840.mjs`
   (or `.js` if that is the one configured), with the file from the same
   v42.0-49 branch. Restart Zigbee2MQTT.
3. Converter is device-aware using Zigbee Basic `SWBuildID`:
   - **41.3** kitchen devices: cover and battery only; no unsupported v42 reads.
   - **42.0-48** test devices: 25 bench counters; excludes `0xF199`.
   - **42.0-49** test devices: 26 bench counters including `0xF199`.
   - Unknown `SWBuildID`: conservative cover/battery UI, no unsafe reads.
4. If the v49 test device exposes no bench diagnostics, check the cached
   `SWBuildID` in Zigbee2MQTT. Refresh/re-interview **only the test
   device** to update that metadata if necessary. No factory reset or Zigbee
   re-pair is required for this experiment. Do not re-interview the kitchen
   devices merely to enable these diagnostics.

## Controlled HW-954 bench test (curtain #3)

1. At rest, record `ppi_enabled`, `ppi_starts`, `ppi_fallbacks`,
   `ppi_late_irqs`, `tuya_frames_ok`, `tuya_frames_rejected`,
   `rx_framing_errors`, `rx_ring_overflows`,
   `rx_bad_stop_post_exit_30s`, `session_goal_silence_exits`,
   `session_watchdog_exits` and `session_total`.
2. Command a supervised safe move to 0%, then to 100%. Read bench
   counters after each completed move. Normal `DP3=target` without STOP
   should exit with `session_last_reason=5`, `session_last_goal=1`,
   `session_last_silence_ms>=3000`, and increment
   `session_goal_silence_exits` without incrementing the watchdog counter.
3. Test a new/reversed command **before** the 3-second quiet period:
   the earlier goal must not force a premature RX exit.
4. Wait for `hw_direct_active=0`. With **no preceding Zigbee
   command**, trigger a known safe **local/physical** MCU report
   (e.g. verified local curtain control) to test truly spontaneous idle RX.
   Look for increased `ppi_starts` and fresh valid reports with zero
   new framing/rejected counters. A Zigbee-triggered move does NOT prove
   independent GPIO/PPI waking because it pre-arms UARTE0.
5. Observe multiple return-to-idle cycles, errors within 30 seconds of
   switching back, and how fast the direct receiver stops. Inspect current
   consumption using a meter if battery life is the main objective.
6. If no validated target arrives, receiver must **not** early-exit;
   original STOP guard or watchdog must remain effective.

**Stop criteria:** lost or stale reported positions, wrong/unsafe motion,
unresponsive STOP, repeated RX framing errors, new ring overflow, or loss
of Zigbee connectivity. Return test #3 to the already-known working
firmware as appropriate; leave v41.3 kitchen motors untouched.
