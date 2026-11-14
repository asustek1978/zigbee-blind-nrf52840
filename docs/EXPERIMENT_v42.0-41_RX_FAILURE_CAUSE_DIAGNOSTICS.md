# v42.0-41 — UART STOP / UARTE ERRORSRC / DMA handoff cause diagnostics

**Branch:** `experiment/v42-0-40-rx-start-stop-motion-diag` (same existing branch, not a separate branch).

**Status:** EXPERIMENTAL, not automatically validated on HW-954 by a passing GitHub Actions build.
This update adds read-only diagnostics, not a UART timing fix or a motor-control feature.

## Why this exists

User's v42.0-40 prolonged motor test (5599 s):
- `tuya_frames_ok = 3407`, `tuya_frames_rejected = 1`;
- `rx_error_count = 105`: `rx_framing_errors = 99`, `rx_ring_overflows = 0`,
  `uart_hardware_errors = 5`, `uart_handoff_errors = 1`;
- `rx_bad_stop_gpio = 23`, `rx_bad_stop_chained = 76`;
- `uart_last_errorsrc = 4` (UARTE FRAMING flag);
- `zigbee_motor_commands = tx_frames = 45`, `tx_failed_bytes = 0`;
- battery is reported as 100% using MCU DP13 fallback in the Zigbee2MQTT converter.

These counters do **not** prove that motor movement generates electrical interference,
nor that each reported software UART error implies a lost Tuya frame. They show
where further diagnosis is useful.

## Protected behavior

Unmodified from v42.0-40:
- `P3_FIRST_SAMPLE_US = 156`, `P3_BIT_US = 104`; no changes to TIMER3
  sampling state-machine transitions or automatic retries;
- `P3_UART_IDLE_MS = 15`, UARTE0 EasyDMA receive after the software
  `0x55` preamble, peripheral disabled after idle;
- Zigbee End Device 1000 ms poll interval;
- UARTE1 P0.20 motor TX only for explicitly requested Zigbee commands;
- no boot motor movement/queries, no changes to motor STOP, positioning,
  local calibration or battery DP13 handling;
- the previously existing error and Zigbee attributes keep their IDs.

Diagnostics are recorded **only on the existing error paths**. The success
path from `0x55` to `TASKS_STARTRX` remains unchanged. The Zigbee
thread copies read-only counters periodically; no extra Zigbee polling loop
or MCU requests are added.

## New Zigbee Basic read-only attributes

| Attribute | Zigbee2MQTT field | Meaning |
|---|---|---|
| 0xF14E | `rx_bad_stop_gpio_last_ms` | Uptime in ms of last bad software STOP from GPIO edge |
| 0xF14F | `rx_bad_stop_chained_last_ms` | Uptime in ms of last bad chained software STOP |
| 0xF150 | `rx_handoff_unprepared` | UARTE0 wasn't prepared when trying DMA handoff |
| 0xF151 | `rx_handoff_rxto_timeout` | STOPRX/RXTO wait expired |
| 0xF152 | `rx_handoff_amount_oversize` | RXD.AMOUNT exceeded allocated DMA buffer size |
| 0xF153 | `rx_handoff_no_rxstarted` | Missing RXSTARTED event |
| 0xF154 | `rx_handoff_zero_amount` | RXD.AMOUNT was zero *with RXSTARTED present* |
| 0xF155 | `rx_handoff_last_ms` | Uptime in ms of latest handoff fault |
| 0xF156 | `rx_handoff_last_reason` | Reason 1..5 in same order as above; 0 until any fault |
| 0xF157 | `rx_hw_framing_events` | UARTE ERRORSRC FRAMING (bit 0x4) |
| 0xF158 | `rx_hw_overrun_events` | UARTE ERRORSRC OVERRUN (bit 0x1) |
| 0xF159 | `rx_hw_break_events` | UARTE ERRORSRC BREAK (bit 0x8) |
| 0xF15A | `rx_hw_parity_events` | UARTE ERRORSRC PARITY (bit 0x2) |
| 0xF15B | `rx_hw_last_ms` | Last UARTE hardware event uptime in ms |
| 0xF15C | `rx_hw_last_amount` | RXD.AMOUNT observed when last UARTE ERROR was serviced |

A single UARTE ERROR event may have **multiple ERRORSRC flags**. Therefore
the sum of 0xF157–0xF15A is not guaranteed to equal `uart_hardware_errors`
(which counts events). Unknown ERRORSRC bit patterns also remain possible.

### Sanity checks (one consistent snapshot)

1. `rx_bad_stop_gpio + rx_bad_stop_chained = rx_framing_errors`.
2. `rx_handoff_unprepared + rx_handoff_rxto_timeout +
   rx_handoff_amount_oversize + rx_handoff_no_rxstarted +
   rx_handoff_zero_amount = uart_handoff_errors`.
   The original code increments once if both no RXSTARTED and zero amount:
   reason 4 takes precedence over 5. A truly oversize amount can also
   coincide with one of these, and will contribute **two pre-existing
   handoff errors** as it did in v42.0-40.
3. `rx_error_count = rx_framing_errors + rx_ring_overflows +
   uart_hardware_errors + uart_handoff_errors`.

These counters reset when HW-954 reboots, not on a Zigbee2MQTT restart.
Separate Zigbee read transactions are not an atomic snapshot. Refresh after
about 5 seconds before assuming a transient inequality is a bug.

## Test / rollback sequence

1. Save a working v42.0-40 UF2 and converter, along with the prior snapshot.
   Disable the temporary repeated curtain OPEN/CLOSE automation.
2. Check successful GitHub Actions CI on the **exact v42.0-41 commit**,
   inspect UF2 validity and compare `SOURCE_COMMIT.txt` and `SHA256SUMS.txt`.
3. With motor UART and battery disconnected, flash only the test HW-954.
   Before reconnecting, verify wiring and safe STOP access.
4. Install **one** converter from the same branch:
   `zigbee2mqtt/external_converters/p3_nrf52840.js` (or `.mjs`, not both);
   restart Zigbee2MQTT. Verify Zigbee Basic `swBuildId=42.0-41`.
5. Confirm `uart_test_phase=7`; check low-power idle current against the
   previously measured ~0.03–0.05 mA **for v42.0-37**. This is a historical
   reference, not a guaranteed current for v42.0-41.
6. Collect new/old fields at 2, 10 and 30 minutes while stationary,
   then send a supervised brief OPEN -> STOP and CLOSE -> STOP. Check physical
   movement and no unsafe behavior.
7. Collect `tuya_frames_ok/rejected`, `rx_bad_stop_*_last_ms`,
   `rx_handoff_*`, `rx_hw_*`, `uart_last_errorsrc`, `rx_error_count`,
   `uart_queue_to_tx_ms`, `uart_rx_after_tx_ms` and motor battery.
8. If control fails, consumption increases materially, Zigbee disconnects,
   or UART errors worsen: stop test and revert to saved v42.0-40 UF2 +
   matching converter. Do not change timer delays blindly.

## Limitations

- v42.0-41 **classifies errors; it does not yet fix them**.
- Time fields are coarse CPU uptime in ms, not UART bit timing.
- The last MCU DP1 field reflects **last validated report**, not a real-time
  physical motion sensor.
- Neither compiler success nor artifact creation proves correct operation
  on the physical motor, stable power consumption or 100% reliability.
- This experiment does not provide OTA firmware installation.
