# v42.0-45 — offset-calibrated SAADC and guarded physical movement RX

Experimental bench firmware, NOT validated on physical hardware.
v42.0-44 and the verified v42.0-43 baseline remain unchanged.

## Measurements motivating this version

- Battery by multimeter: 4.031 V.
- Internal nRF52840 VDDH/5 SAADC estimate: 3.940 V. The board power
  topology is not verified; this is NOT yet a confirmed BAT+ measurement.
- Manual button produced a valid MCU DP3 movement from 0 to 25 percent
  without an extra Zigbee motor command.
- The hybrid receiver accepted 161 new Tuya frames during the test and
  registered 7 software STOP-bit errors (2 GPIO-origin, 5 chained)
  in one episode. There were zero rejected Tuya frames.
- Direct hardware RX stayed disabled (watchdog lockout), although the
  physical motor moved and fresh, changed position reports arrived.

## SAADC

1. Set internal VDDH/5 and VDD acquisition times explicitly to 10 us.
   The Nordic nRF52840 specification requires at least 10 us for VDDH/5.
   The prior Zephyr default was also usually 10 us, so this explicit
   setting alone is NOT expected to fix a 91mV discrepancy.
2. Request supported nrfx SAADC offset calibration with each reading
   (Zephyr adc_sequence.calibrate=true).
3. Use 16x oversampling on each separate rail reading
   (adc_sequence.oversampling=4).
4. Preserve original 30-second first reading and 10-minute interval,
   no automatic battery charge percentage or gain correction.

Offset calibration does not correct reference/gain tolerance or
voltage drops along charger/regulator hardware. Measure BAT+ with
the multimeter in parallel, ideally at different charge levels.

## Physical movement receive

The 120-second UARTE0 receiver watchdog remains absolute and
non-renewable, anchored to hardware RXSTARTED. Repeated stale MCU DP1
OPEN/CLOSE is not accepted as a fresh motor-motion request.

This version processes only changed, validated DP3 position reports
from the real MCU. The first valid position provides a baseline.
A subsequent distinct DP3 after >=10 seconds without position
change is considered evidence of a separate movement episode.

If the previous watchdog lockout is active, no RX is active/requested,
and a position-based wake has not already been used in this
STOP/command episode, the RX thread requests one fresh full-hardware
UARTE0 receive session. This only arms reception; it does not
transmit commands to the motor.

Position-based re-arm can happen only once between a fresh validated
MCU STOP or accepted explicit motor command. This ensures a long
continued stream of position reports does not repeatedly restart
hardware reception after the 120-second watchdog. All existing
UART framing, hybrid sampling, 1.5-second STOP guard and 120-second
hard deadline are retained.

This is an intentionally conservative heuristic. It can miss a
manual movement with no changed DP3, or another manual movement after
a position-wake watchdog without intervening STOP. A false wake is
also possible if delayed reports give changed positions after >=10s
silence; a single 120-second cap still applies, and the latch
prevents another position-based wake until STOP/explicit command.

### New read-only Zigbee Basic diagnostics

- F175 position_wake_count: accepted changed-DP3 hardware RX requests
- F176 position_wake_suppressed: changed DP3 during lockout failing guard
- F177 position_wake_last_ms: uptime ms of most recent changed valid DP3
- F178 position_wake_latched: 1 if a position wake was used since STOP/command

## Bench validation checklist

1. Secure the motor on a test bench with a functioning local STOP.
   Preserve the working v42.0-44 UF2 and matching Z2M converter for rollback.
2. Install the new matching converter and UF2, confirm SWBuildID 42.0-45.
3. Compare SAADC VDDH and multimeter BAT+ at the same instant, without
   motor movement and again after a 10-minute ADC refresh; inspect ADC
   valid_mask, samples and last_error. Avoid assuming VDDH equals BAT+.
4. Test 0, 50, 100 and 25 percent with supervised Zigbee commands.
   Verify physical position and STOP safety.
5. In a controlled missing-STOP sequence, verify watchdog exits RX in
   120 seconds and sets motion_lockout=1.
6. After >=10 seconds of stable position, use the P3 physical button
   for a new movement. Expect a changed DP3 to increment
   position_wake_count and hw_direct_enters; expect no extra Zigbee TX.
7. Confirm repeated DP3 reports do not renew the 120-second anchor.
8. Verify the once-per-episode position-wake latch prevents repeat
   position wake after another watchdog until a fresh STOP or
   accepted explicit motor command.
9. Repeat the idle UART error diagnostics: rx_idle_fault_episodes,
   rx_bad_stop_gpio, rx_bad_stop_chained, and Tuya frames rejected.
10. Measure idle current after SAADC and after hardware RX expiry.
    Offset calibration adds a brief active ADC operation.

Code tests and Nordic build cannot verify actual motor safety,
battery wiring or current draw. Do not operate this version unattended.
