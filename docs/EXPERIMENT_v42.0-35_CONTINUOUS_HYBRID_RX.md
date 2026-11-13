# v42.0-35 — continuous hybrid 9600-baud UART reception (BENCH ONLY)

## Baseline v42.0-34 (CONFIRMED on HW-954)
The prior **v42.0-34** was tested for 1200 seconds with Tuya P3 motor TXD -> nRF52840 P0.17 and GND common, nRF TX DISCONNECTED:
- 955 valid Tuya frames, 1 rejected, 956 completed parser decisions (99.895% of completed parses were valid)
- 956 UARTE wakeups; 12,592 EasyDMA bytes / 13,562 total accepted bytes (92.85% from DMA)
- 0 UART handoff errors; 3 UARTE hardware error events; 4 aggregate RX errors
- 955 counted `55 AA` headers; `rx_first4=117680725`, `rx_next4=33753088`, decoded first eight bytes 55 AA 03 07 00 00 03 02
- `stress_elapsed_s=1200`, `uart_test_phase=9` (the receiver correctly stopped after 20 minutes)
- Idle current 0.04–0.05 mA (user measurement); motor-active current **NOT** measured yet
- Zigbee LQI 116 at the final check
These are parser/receiver counters, **not** independent proof that all Tuya motor frames were captured or delivered.

## Only behavioral change from v42.0-34
- Remove the overall 1,200,000 ms (20-minute) termination condition in the main diagnostic loop.
- Continue receiving and updating statistics every 5 seconds for as long as powered. **Phase 7 stays active**, unlike v42.0-34 where it changed to phase 9 after 1,200 seconds.
- No changes to TIMER3 decoding of the first 0x55, UARTE wake/start timing, EasyDMA acquisition, GPIO rearm, nor ~15-ms UARTE idle cutoff.
- No motor control TX, STOP, or calibration commands; the UART TX wire remains physically disconnected.
- Existing 30-second startup remains. CPU/power budget is NOT proven for long-term use; this is another experiment.

## Diagnostic fields (requires v42.0-35 Zigbee2MQTT converter)
- `uart_wake_count` (0xF135)
- `uart_dma_bytes` (0xF136)
- `uart_handoff_errors` (0xF12A)
- `uart_hardware_errors` (0xF12F)
- `rx_header_count`, `rx_error_count`, `tuya_frames_ok`, `tuya_frames_rejected`
- `stress_rx_bytes`, `stress_elapsed_s`, `uart_test_phase`

## Test gates
1. Confirm a **green** GitHub Actions run and valid UF2 before considering installation.
2. Flash ONLY the spare HW-954; preserve stable working v41.3, tested v42.0-32, and the validated v42.0-34.
3. With motor TX connected to P0.17 and common ground, keep nRF TX disconnected. Confirm the idle power remains around 0.04–0.05 mA; report active current separately.
4. At 1200–1500 s verify `stress_elapsed_s > 1200`, phase stays `7` and frame counters continue to grow when the motor sends data.
5. Measure 6–24h stability: `uart_wake_count` vs completed Tuya parses, `uart_dma_bytes`, hardware errors, rejected frames, stable Zigbee link and idle current.
6. If crashes, excessive current, missed frames, increasing errors, or Zigbee dropouts: revert to v42.0-34 and review diagnostics. Green CI alone does not validate UART handoff correctness.

## Not production yet
The remaining 3 UARTE hardware errors in the 20-minute baseline were not broken down by NRF_UARTE0->ERRORSRC. Do not infer root cause from the aggregated counter. Future instrumentation should record those error bits separately without changing the receiving data path. The user's battery indicator remains N/A; this test does not claim battery reporting is fixed.
