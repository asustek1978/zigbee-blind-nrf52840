# Experiment v42.0-32 — continuous software UART byte receive

## Motivation / measured baseline
- v42.0-29: continuously enabled UARTE 1.39–1.41 mA, 233 valid Tuya frames, 1 rejected in 20 min.
- v42.0-31 on HW-954: 0.04–0.05 mA idle; 0.22–0.25 mA while motor was moving.
- v42.0-31 at 155 s: 601 RX bytes, 0 valid, 72 rejected; at 356 s: 1363 RX bytes, 0 valid, 165 rejected. First bytes packed by diagnostic: 55 B5 07 08 20 00 00 39 instead of 55 AA ...; Zigbee LQI 153.

## Hypothesis
The v42.0-31 decoder disabled and reconfigured the GPIO interrupt at every byte boundary, leaving only roughly 52 us between sampling the stop bit and the next byte start for a back-to-back 9600 baud stream.

## Change
- GPIO RX edge stays armed, but falling edges while a byte is being received are ignored by the state machine.
- TIMER3 samples 8 data bits, 1 stop bit, and then checks at bit 10.5 for a contiguous next start bit.
- If line is low in the next-start sample, the timer continues without returning to GPIO re-arm: sampling first data bit one bit later.
- If line is high (idle), timer stops; GPIO falling-edge interrupt can begin a new sequence.
- UARTE0 still disabled, motor TX remains disabled, no UART commands sent.
- Software UART framing/overflow errors are published to RX error counters every 5 seconds (not only at 20 minutes).
- 30 second startup delay, 20 minute RX test window retained.

## Safety / limitations
This implementation remains **experimental and not validated on real hardware**. TIMER3 ownership and GPIO interrupt latency under Zigbee load have not been verified. At 9600 baud, bit time is approximately 104.17 us. A 1 MHz timer with integer intervals 104 and 156 us introduces modest quantization. GPIO callback and timer ISR latency could still corrupt reception. False starts, mid-bit jitter and GPIO->TIMER phase synchronization need live tests. The receiver is not production ready.
GitHub Actions compiling is necessary but does NOT prove runtime timing or power.

## v42.0-32 test acceptance checks
1. CI complete, all safety/UF2 layout steps pass.
2. Test on spare HW-954 only; motor TX nRF P0.20 physically disconnected. Common GND, P3 TXD -> P0.17.
3. Read Zigbee diagnostic after 30s: phase=7; RX counts initially may be zero.
4. CLOSE/STOP and OPEN/STOP via original motor buttons; periodically read counters:
   - rx_header_count must increase.
   - tuya_frames_ok must increase above zero and keep growing.
   - tuya_frames_rejected should be small compared to valid frames.
   - stress_rx_bytes must increase.
   - rx_error_count / rx_last_error monitor framing and overflow.
   - first 4 bytes should contain plausible 55 AA Tuya frames, when capture starts at boundary.
5. Read again at ~1200s (phase=9) and after quiet period / new movement.
6. Compare current vs baseline; do not claim permanent energy saving until no packet loss and stability are demonstrated.

If no valid frames, restore v42.0-31 for power baseline and v42.0-29 for good UART RX baseline. Do not touch stable v41.3.
