# P3 nRF52840 v42.0-46 — passive command outcome statistics

Experimental firmware. New counters are NOT motor-safety verification.
Based on proven bench behavior and source for v42.0-45. Do not flash on
an unattended installed curtain; retain the previous UF2 for rollback.

## Why

A five-day firmware log showed 54 Zigbee motor commands, >4500 Tuya
frames and a handful of software UART framing errors. The existing
counters could only say that commands were transmitted and MCU data
arrived. They did not distinguish an actual reported target from
an unrelated MCU status or indicate when no relevant response arrived.

The P3 classic Tuya MCU interface has no command-sequence identifier
tying a DP1 / DP3 report to one particular motor command. Therefore
this module NEVER calls an arbitrary MCU status a protocol ACK.

## Command monitoring algorithm

Only explicitly queued cover commands count; no new UART frames, Zigbee
motor commands, retries, calibration packets or periodic polling added.

- A successful UART TX starts a monitoring window of **180 seconds**,
  with the original command kind and requested position. An incomplete
  earlier window is **superseded** when a newer command is successfully
  transmitted; it is not counted as missing acknowledgement.
- OPEN -> expected MCU position DP3 = 0.
- CLOSE -> DP3 = 100.
- POSITION -> exact requested 0..100.
- STOP -> expected fresh MCU motion DP1 = STOP (1).
- A subsequent accepted, fresh MCU DP1 or DP3 within the monitoring
  window increases **cmd_mcu_report** once. This is **not** a Tuya
  acknowledgement; unrelated MCU frames may be observed.
- A fresh, accepted DP3 equal to the requested setpoint to within
  **1 percentage point** increases **cmd_position_reported** once.
  This is a motor-controller-reported position, NOT an optical/physical
  confirmation of curtain movement. Multiple repeated DP3s never
  increment the same command twice.
- An accepted fresh MCU DP1 STOP after a STOP TX increments
  **cmd_stop_reported** once.
- After 180 seconds, a command with no fresh DP1/DP3 increments
  **cmd_timeout_no_report**. A command with a DP1/DP3 report but no
  matching target/STOP increments **cmd_timeout_no_target**.
- Failed UART TX increments **cmd_tx_failed**; it never starts a
  pending monitor window or resends the command.
- Accepted commands placed in the existing TX queue count as
  **cmd_queued**, but STOP can purge older queued commands; queued is
  not the same as transmitted.
- All counters reset on reboot; no additional flash writes.
- The 180-second timer is a single delayed work item, not a motor
  command and not the RX watchdog. It only updates read-only Zigbee
  Basic attributes through the existing ZBOSS sync path.

## Zigbee2MQTT read-only fields

| Attr | Name | Meaning |
| --- | --- | --- |
| F179 | cmd_queued | accepted into TX queue |
| F17A | cmd_tx_ok | successfully transmitted to MCU UART |
| F17B | cmd_tx_failed | UART TX errors |
| F17C | cmd_mcu_report | first valid DP1 or DP3 report after each TX |
| F17D | cmd_position_reported | DP3 at target +/- 1% |
| F17E | cmd_stop_reported | fresh DP1 STOP after STOP TX |
| F17F | cmd_timeout_no_report | no fresh DP1/DP3 within 180 s |
| F180 | cmd_timeout_no_target | DP1/DP3 arrived but target not reported |
| F181 | cmd_superseded | pending command displaced by newer TX |
| F182 | cmd_pending | 0/1 current pending monitoring |
| F183 | cmd_last_kind | 1 OPEN, 2 STOP, 3 CLOSE, 4 POSITION |
| F184 | cmd_last_target | 0..100, or 255 STOP |
| F185 | cmd_last_result | 0 none/pending; 1 target; 2 STOP; 3 no report timeout; 4 no target timeout; 5 superseded; 6 TX failed |
| F186 | cmd_last_report_delay_ms | UART TX completion to first fresh DP1/DP3; UINT32_MAX when none yet |

No new ZCL writes or commands. Use Refresh all diagnostics as before
to fetch the read-only counters in five-attribute batches.

## Hardware validation plan

1. Preserve working v42.0-45 UF2 and converter; record current battery
   voltage, motor endpoints and expected orientation (OPEN=0, CLOSE=100).
2. Flash CI-compiled v42.0-46, load matching JS or MJS Zigbee2MQTT
   converter, verify SWBuildID 42.0-46. Note counters reset.
3. At bench, issue one position command (for example 50%).
   Expect cmd_queued and cmd_tx_ok to increment once and
   cmd_pending=1 after UART TX. After accepted MCU DP3 reaches 49..51%,
   cmd_position_reported should increment once; pending clears.
4. Issue OPEN (0%) and CLOSE (100%); verify MCU-reported position
   and unchanged motor safety controls.
5. Issue STOP during supervised motion, verify physical immediate stop
   and cmd_stop_reported on a fresh DP1=STOP.
6. For timeout, first observe natural cases where the MCU does not
   report the requested outcome within 180 seconds. No forced
   motor obstruction or deliberately impaired safety setup.
7. Issue a later command before an earlier one finishes monitoring:
   confirm cmd_superseded increments instead of no-response timeout.
8. Compare original UART reliability counters, startup no-TX behavior,
   120 s UARTE0 watchdog, SAADC, physical button and battery estimates.
9. Verify no passive diagnostic counter causes movement, periodic UART
   TX, duplicate commands or MCU calibration.

### Caveats

- Physical motion cannot be independently proven without another
  sensor. The counters mean MCU reported a position, not that the
  curtain definitely moved correctly.
- MCU reports may be delayed, repeated or occur during overlapping
  commands. No Tuya request correlation field is available here.
- Successful UART TX is not confirmation that motor MCU parsed bytes.
- Before 180s deadline the command can still be pending.
- Software tests are not a substitute for testing real HW-954 motor
  and UART behavior. Firmware remains experimental.
