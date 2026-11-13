# v42.0-40 — software UART start-edge / STOP-bit / motor-state diagnostics

Base: `experiment/v42-0-39-rx-error-breakdown`. **Diagnostic-only experimental branch**. No new TX, no automatic movement, no OTA or bootloader changes.

## Evidence prompting the experiment
From the user's HW-954 with v42.0-39, `stress_elapsed_s=1189`, `tuya_frames_ok=318`, `tuya_frames_rejected=0`, `rx_error_count=31`, `rx_framing_errors=31`, `rx_ring_overflows=0`, `uart_handoff_errors=0`, `uart_hardware_errors=0`; `rx_software_starts=387`, `rx_software_nonheader=38`. All recorded RX errors belonged to **the software STOP-bit check**. This **does not demonstrate** the GPIO line is noisy or prove bad UART timing; it only locates the rejecting check.

The code does not yet determine whether these arise on the GPIO-triggered first byte or during the software back-to-back byte path. It also cannot know physical movement in real time; a validated MCU DP1 can be used as a delayed proxy.

## Safety contract
We keep unmodified:
- `P3_FIRST_SAMPLE_US 156U` (first timer compare), `P3_BIT_US 104U` (subsequent samples).
- TIMER3 wake on P0.17, first `0x55` byte processed by software, subsequent bytes through independent UARTE0 EasyDMA.
- `P3_UART_IDLE_MS 15U`, UARTE0 power gating and 1 s sleepy Zigbee parent poll.
- Independent UARTE1 P0.20 **only for user-originated Zigbee motor commands**, STOP queue behavior, no boot commands.
- Existing counters and Basic-cluster attribute numbers.
- No filter, debounce or recalibration changes in this experiment. In the GPIO start callback, new timestamps/counters are recorded **after** TIMER3 is armed. In TIMER3 ISR, the additional classification runs **only on invalid STOP**, after the preexisting fast `0x55` handoff branch.

## New read-only Zigbee2MQTT diagnostics (ZCL Basic)

| Field | Attribute | Meaning |
|---|---|---|
| `rx_gpio_start_events` | `0xF141` | Accepted P0.17 falling-edge GPIO start events |
| `rx_last_gpio_start_ms` | `0xF142` | Last accepted start's system uptime, **ms**, coarse resolution |
| `rx_bad_stop_gpio` | `0xF143` | Invalid software STOP on first byte originating in a GPIO edge |
| `rx_bad_stop_chained` | `0xF144` | Invalid software STOP on subsequently sampled software byte |
| `rx_bad_stop_reported_idle` | `0xF145` | Invalid software STOP while last validated MCU DP1=1 (STOP) |
| `rx_bad_stop_reported_moving` | `0xF146` | Invalid software STOP while last validated MCU DP1=0/2 (OPEN/CLOSE) |
| `rx_bad_stop_motion_unknown` | `0xF147` | Invalid software STOP before receiving any accepted DP1 |
| `rx_last_bad_stop_ms` | `0xF148` | Uptime in ms when latest software STOP check failed |
| `rx_last_bad_stop_byte` | `0xF149` | Eight sampled data bits from latest failed software STOP attempt; **not valid UART byte** |
| `rx_reported_motion` | `0xF14A` | Last validated MCU DP1 (0 OPEN, 1 STOP, 2 CLOSE, 3 unknown) |
| `rx_gpio_starts_reported_idle` | `0xF14B` | GPIO start edges with DP1=STOP |
| `rx_gpio_starts_reported_moving` | `0xF14C` | GPIO start edges with DP1=OPEN/CLOSE |
| `rx_gpio_starts_motion_unknown` | `0xF14D` | GPIO start edges when no DP1 is known |

**Required cumulative invariants**, once attributes are read from the same approximate time window:
- `rx_bad_stop_gpio + rx_bad_stop_chained = rx_framing_errors`.
- `rx_bad_stop_reported_idle + rx_bad_stop_reported_moving + rx_bad_stop_motion_unknown = rx_framing_errors`.
- `rx_error_count = rx_framing_errors + rx_ring_overflows + uart_hardware_errors + uart_handoff_errors`.
- `rx_gpio_start_events = rx_gpio_starts_reported_idle + rx_gpio_starts_reported_moving + rx_gpio_starts_motion_unknown`.

All count values are volatile (reset with the board). Multiple Zigbee ZCL read transactions are **not an atomic snapshot**, so transient differences are expected; wait ~5 seconds for published diagnostic refresh and re-read. No performance metric should be inferred from `rx_last_gpio_start_ms - rx_last_bad_stop_ms` as UART timing is much faster than the coarse ms clock.

**Movement classification caveat:** DP1 comes from the last **validated MCU report**, not a physical motion detector. Its age is not yet tracked and it may lag movement or be stale, especially after motion stops. Categorization shows a correlation to the *reported* state only. No outbound Zigbee command alone changes this classification.

## Test protocol
1. Keep a rollback image of v42.0-37/v42.0-39, disable scheduled curtain automation temporarily and verify STOP command is accessible. Compile through GitHub Actions and verify source/UF2 checksum first.
2. Upgrade external Zigbee2MQTT converter with the `p3_nrf52840.mjs` from this branch and restart Zigbee2MQTT (does **not** reboot MCU or reset its counters).
3. Flash the v42.0-40 **experimental** UF2 onto the bench device; do **not** assume the modified build maintains current consumption.
4. Reboot nRF52840. Read `uart_test_phase=7`, current in quiet state, all new fields and `rx_framing_errors` after 2, 10 and 30 minutes without commanding the motor.
5. When physically safe, command OPEN briefly then STOP; take measurements before and after moving and several minutes after it stops. Compare the three motion-state STOP-error buckets, the GPIO vs chained STOP buckets, **the three GPIO-start denominators**, and rejected Tuya frames. Per-state error totals alone are biased by different traffic volume; compare rates where the source is GPIO (bad-stop-on-GPIO / GPIO-starts-in-state). The chained STOP errors are not included in these GPIO-only ratios.
6. If **bad stops are predominantly GPIO-first**, investigate spurious P0.17 falling-edge events and actual start-bit alignment; if **mostly chained**, investigate `P3_RX_NEXT_START` resynchronization. If mostly when last MCU DP1 reports moving, measure motor-induced interference; correlation alone is not proof.
7. If power rises materially from the previously measured v42.0-37 baseline **0.03–0.05 mA** or data reception regresses, revert to the saved build. Test over multiple runs before changing `P3_FIRST_SAMPLE_US` or adding an input filter.

## OTA
This branch does **not** support OTA. v42.0-38 is an audit/plan, not a working Zigbee2MQTT OTA installer.
