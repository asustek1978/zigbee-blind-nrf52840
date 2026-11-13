# v42.0-34: P3 9600-baud hybrid TIMER3-to-UARTE receive experiment

## Why this experiment exists
We want TIMER3 to receive the first byte while waking the hardware UARTE and then let UARTE EasyDMA receive the remainder. The v42.0-29 all-on UARTE baseline was 1.39–1.41 mA, but it read Tuya frames reliably (233 valid, 1 rejected at 1200 s). Pure software UART with TIMER3 saved current but lost frames: v42.0-32 had 0.03–0.05 mA idle, 0.22–0.29 mA during motor activity, 48 valid vs 117 rejected at 231 s. GPIOTE/PPI v42.0-33 was worse: only 6 valid vs 133 rejected with 219 receive errors at 155 s.

## Receiver sequence (bench only)
1. UARTE0 is DISABLED in idle. GPIO P0.17 waits for a falling edge. 30-second test startup delay as before.
2. TIMER3 takes the falling edge, samples the incoming UART bits (at ~156us after initial edge, then ~104us per bit), and reconstructs the first byte.
3. At the first data-bit sample UARTE0 is ENABLED, but the actual UARTE receiver is not yet started. This gives the hardware ~0.8ms warm-up time while TIMER3 finishes the first byte.
4. If the byte has valid STOP bit and equals Tuya header 0x55, the software RX pushes it into the parser's ring. In the middle of that STOP bit, TIMER3 stops and UARTE STARTRX starts. UARTE uses an EasyDMA buffer (1024 bytes) for 0xAA and subsequent bytes. This handover is risky: there are only ~52 microseconds until a tightly packed next start bit at 9600 baud. There is NO guaranteed success without hardware validation.
5. If the software byte isn't 0x55, UARTE is disabled again, and TIMER3 continues software sampling until it detects 0x55.
6. After ~15ms with no RXDRDY activity (checked by the main thread about every 3ms), trigger STOPRX, wait for RXTO (at most 10ms), then append the DMA buffer's RXD.AMOUNT bytes to the existing RX ring. Switch UARTE OFF and re-arm the GPIO edge interrupt. A full buffer also triggers STOPRX.
7. After the 20-minute trial, stop UARTE and TIMER3 explicitly.

## Safety
- Motor TX remains physically disconnected: P3 TXD -> P0.17, GND -> GND, no nRF TX connection to motor.
- No UART motor commands are sent. This is a passive diagnostic only, not a controller firmware.
- UART0 must remain Zephyr deferred-init; firmware accesses the peripheral registers directly, so another driver must NOT own UARTE0 concurrently.
- Direct UARTE register setup is nRF52840-specific. Statically configured baud rate is 9600 baud, 8N1, HW flow control disabled.
- No assumption that the first 0xAA will be received correctly is made. A second-byte miss is possible if STARTRX latency exceeds the ~52us stop-bit margin. In that case diagnose/revert rather than claiming the design solved RX.
- UART is not continuously on during idle. During motor traffic UARTE can use more current than TIMER3-only; record both idle and active current.
- Build, code review and live benchmarking are required before flashing. Never flash to the only working blind.

## Read-only Zigbee2MQTT v42.0-34 diagnostics
- `uart_wake_count`: Basic 0xF135 (PPI/GPIO legacy diagnostic ID reused) — accepted 0x55 wake attempts.
- `uart_dma_bytes`: Basic 0xF136 — bytes recovered through EasyDMA.
- `uart_handoff_errors`: Basic 0xF12A — UARTE did not start or handoff failed; not a comprehensive count of silent frame loss.
- `uart_hardware_errors`: Basic 0xF12F — UARTE hardware ERRORSRC events.
- `rx_error_count`: software UART framing + ring buffer overflow + hardware errors + handoff errors.
- `rx_last_error`: 1 overflow, 2 software framing, 3 handoff, 4 UARTE hardware.
- Existing: `uart_test_phase`, `stress_elapsed_s`, `rx_first4`, `rx_next4`, `rx_header_count`, `tuya_frames_ok`, `tuya_frames_rejected`, `stress_rx_bytes`, `diag_last_command`.

## Acceptance checks
- CI must build, validate UF2 layout and compile the independent Zigbee2MQTT converter. If not green, do not flash.
- On spare HW-954 only: confirm phase=7 after ~30sec; record initial idle current.
- Motor CLOSE→STOP, OPEN→STOP using ORIGINAL motor buttons. Compare `rx_first4`, `rx_header_count`, `tuya_frames_ok` vs rejects; inspect `uart_dma_bytes`, `uart_wake_count`, handoff errors.
- Compare passive receive success and current with v42.0-29 and 42.0-32. A new stream should start with 55 AA; if bytes captured are systematically wrong or UARTE never wakes, stop and revert.
- If pass, run to 1200sec and recheck Zigbee stability. Receiver explicitly stops after 20 min; don't infer continuous operation.
- Production criteria: near-zero packet loss, no unsolicited motor commands, Zigbee stable, and acceptable average current including UARTE wake periods.

## Status
New experimental source only. As of initial commit, no confirmed green GitHub Actions build or physical hardware test.
