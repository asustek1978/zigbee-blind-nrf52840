# P3 nRF52840 v42.0-43 — non-renewable direct UART RX watchdog

Status: **experimental, hardware validation required**. This is a fix to
v42.0-42 on branch `experiment/v42-0-40-rx-start-stop-motion-diag`.
Do not install unattended with a curtain attached.

## Observed defect in v42.0-42

After nine Zigbee motor commands, `Hw direct active = 1`,
`Hw direct enters = 3`, `Hw direct exits = 2`,
`Hw direct max timeouts = 0` while `Stress elapsed s = 486`,
`Tuya frames ok = 662`, `Rx error count = 0`. The latest
motor DP1 reported CLOSE, even though DP3 position had reached 100%.
Full UARTE0 RX could stay active for longer than the intended 120 s.

Root cause: every *repeated* valid MCU DP1=OPEN/CLOSE report called
`p3_soft_uart_request_motor_rx()`, resetting `hw_rx_request_ms`.
The power watchdog compared uptime against this frequently renewed
timestamp rather than the moment hardware RX was actually enabled.

## Fix

- At a successful UARTE0 `EVENTS_RXSTARTED`, latch
  `hw_direct_session_start_ms` exactly once for the duration of
  the current hardware RX session.
- Compare now - session_start (unsigned 32-bit subtraction, modulo
  wraparound) against **120000 ms**; new MCU DP1 reports and even
  additional Zigbee motor commands cannot move the deadline.
- Only a real, validated **change** in DP1 motion state can
  auto-request full UART RX. Identical repeated DP1 reports are ignored
  for RX state transitions. Fresh DP1=STOP begins a **1500 ms** guard,
  with subsequent STOP reports unable to extend it.
- On the **120 s watchdog**: stop and power down full UARTE0 RX, return
  to GPIO/TIMER3 + short DMA hybrid idle, increment
  `hw_direct_max_timeouts`, and set `hw_direct_motion_lockout=1`.
  While locked out, repeated MCU OPEN/CLOSE cannot re-arm. A fresh
  validated STOP clears lockout. An accepted explicit Zigbee command
  is always allowed to start a new bounded session.
- No change to P0.17/P0.20 UART wiring, UARTE1 motor TX, Zigbee poll,
  original calibration and position mapping. The watchdog switches RX
  mode; it **does not command the motor to stop**.

## New Zigbee2MQTT Basic attributes

| Expose | ID | Meaning |
| --- | --- | --- |
| hw_direct_session_start_ms | 0xF168 | uptime ms of the last full hardware RX entry |
| hw_direct_motion_lockout | 0xF169 | 1 after 120 s direct-RX timeout, until STOP/explicit command |

Previous `hw_direct_active`, `hw_direct_enters`,
`hw_direct_exits`, `hw_direct_max_timeouts`, `hw_direct_bytes`,
all UARTE hardware faults, and `refresh_all_diagnostics` remain.

## Bench qualification

1. Confirm GitHub Actions compiles the firmware, validates UF2 layout and
   passes the v42.0-43 host C watchdog and source contract tests.
2. Save baseline v42.0-42 counters and keep its known-working UF2 for rollback.
3. With motor securely fixed and physically separated from the curtain,
   flash v42.0-43; check Zigbee Basic SWBuildID **42.0-43**.
4. Issue a single commanded OPEN or CLOSE. Check hardware RX enters,
   UARTE bytes, 0 software framing errors, no UART hw errors.
5. After the motor sends a fresh STOP, verify `Hw direct active=0`
   about 1.5 s later and `Hw direct exits` increases.
6. Test the failure mode **without a new STOP** on a controlled bench:
   the receiver must exit no later than 120 s after
   `hw_direct_session_start_ms`, increase
   `hw_direct_max_timeouts`, and set motion lockout=1.
   A persistent/repeated DP1=CLOSE must NOT re-arm it.
7. A new explicit Zigbee command must still be accepted; after a new
   confirmed STOP, motion lockout must clear. Verify normal
   first-byte low-power hybrid receiver and current draw at rest.
8. Measure current draw at rest and while direct UART active.
   Confirm batteries, motor, UART timing, physical travel and STOP behavior.

Zero UART framing errors do **not** alone prove zero frame loss. If any
command malfunctions, switch off unattended test automations and revert
to v42.0-42 or the last confirmed working firmware.
