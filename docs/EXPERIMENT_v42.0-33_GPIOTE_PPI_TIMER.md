# v42.0-33 — P3 9600 baud GPIOTE → PPI → TIMER3, experimental

## Purpose
Correct the remaining software UART framing errors in v42.0-32 without restoring the permanently enabled UARTE (v42.0-29: 1.39–1.41 mA).

### Actual previous measurements
- v42.0-31: 0.04–0.05 mA idle; 0.22–0.25 mA active; 0 valid Tuya frames / 165 rejected by 356 seconds.
- v42.0-32: 0.03–0.05 mA idle; 0.22–0.29 mA active. At 180 seconds: 16 valid, 47 rejected, 48 software RX errors, 867 bytes. At 231 seconds: 48 valid, 117 rejected, 118 software RX errors, 2282 bytes. First bytes correct: 55 AA 03 07 00 00 03 02; last error=2 (framing).
- **Target**: approach v42.0-29 reliability of 233 valid / 1 rejected over 20 minutes, while retaining the ~0.03–0.05 mA idle of v42.0-32.

## What changed
1. UART0 remains disabled, motor UART TX physically disconnected.
2. GPIO0.17 input with pull-up, configured as edge-falling via Zephyr/nrfx.
3. Inspect GPIOTE channel registration to find the Zephyr-allocated event for P0.17, rather than reserving channel 0 unilaterally. Fail startup if not a high-accuracy event channel.
4. Allocate two PPI channels and one channel group via nrfx allocator. PPI channel 1 triggers TIMER3 START with a fork to CLEAR on the first falling edge. Channel 2 disables their shared group on the *same* event so subsequent bit transitions cannot restart the timer. **This same-event group-disable behavior must be verified on HW-954; compilation alone is not proof of correct ordering.**
5. First timer comparison is 156 us after the start edge; subsequent samples 104 us apart (9600 baud approximation). Handle full 8N1 data, stop, and contiguous next-start samples as in v42.0-32.
6. On idle sample: stop TIMER3, pre-arm first-sample compare, re-enable PPI group.
7. Record max TIMER3 ISR lag and count of ISR lags >35 us, to detect interference from Zigbee preemption. They are timing indicators, not guaranteed measures of true sampling phase error.

## New Zigbee2MQTT read-only diagnostics
- `rx_max_isr_lag_us` (Basic 0xF12D; formerly stress_mismatch)
- `rx_late_isr_count` (Basic 0xF12A; formerly stress_failed)
- Existing: `rx_header_count`, `rx_error_count`, `rx_first4`, `rx_next4`, `rx_last_error`, `tuya_frames_ok`, `tuya_frames_rejected`, `stress_rx_bytes`, `stress_elapsed_s`, `uart_test_phase`, `diag_last_command`, `diag_last_format`

## Test plan and stop conditions
- Gate 1: Confirm GitHub Actions **green**, including UF2 layout, CONFIG_NRFX_PPI, and no motor TX.
- Gate 2: Use only a spare HW-954. Connect motor TXD → nRF P0.17, GND common; leave nRF TX disconnected.
- Gate 3: 30 s startup delay. `uart_test_phase=7` means RX initialization succeeded; `5` means failed (e.g. GPIOTE event/PPI unavailable) — DO NOT proceed if 5.
- Gate 4: With motor idle, measure current and compare with 0.03–0.05 mA from v42.0-32. Check Zigbee connection remains stable.
- Gate 5: Physical CLOSE → STOP, OPEN → STOP; at 150–240 s inspect RX bytes, 55 AA header, valid/rejected frames, framing errors, new ISR lag counters; stop and revert on resets, unexpected motion or damaged Zigbee link.
- Gate 6: If reliable, continue 20-minute passive test, log at 1200 s and repeat after long idle. Remember: RX test stops after 20 minutes.
- No motor commands are transmitted in this firmware; it is **not** a substitute for the stable controller v41.3.
- If the receiver still corrupts many frames or current rises materially, compare to v42.0-32; revert to v42.0-29 for reliable hardware RX baseline.

## Status
Experimental source on branch `experiment/v42-0-33-gpiote-ppi-timer`. CI compilation and board runtime are not yet independently confirmed. Never mark this build production or fully tested just because a UF2 artifact exists.
