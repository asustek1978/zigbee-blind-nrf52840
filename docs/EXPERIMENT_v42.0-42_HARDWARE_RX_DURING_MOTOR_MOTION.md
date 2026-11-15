# v42.0-42 — UARTE0 hardware RX during P3 motor movement (BENCH ONLY)

Status: **experimental / hardware validation pending**. Built on the same branch
`experiment/v42-0-40-rx-start-stop-motion-diag` as the v42.0-41 UART
failure-cause diagnostic baseline. This changes receiver runtime behavior.

## Why

v42.0-41 received 282 complete Tuya frames and 31 software STOP-bit
errors after several movement commands with a 10µF / 50V electrolytic
capacitor on HW-954 B+/B−. During the last four-command window, 23 new
software STOP-bit failures were recorded (5 first-byte GPIO / 18 chained),
all while the last validated P3 DP1 indicated OPEN/CLOSE. There were no
hardware UARTE ERRORSRC events or DMA handoff failures. The diagnostics
do not prove how many bytes/frames, if any, were actually lost.

## New behavior

1. At boot, preserve low-power hybrid GPIO/TIMER3 leading-0x55 decoder,
   with UARTE0 disabled between UART bursts. No automatic motor traffic.
2. Only accepted explicit Zigbee OPEN/CLOSE/STOP/POSITION UART frames
   pre-arm UARTE0. Their TX worker waits at most **80 ms**, or **20 ms**
   for STOP, for confirmed `EVENTS_RXSTARTED`; on failure it sends by the
   existing UARTE1 path and increments `hw_direct_prearm_timeouts`.
   All hardware RX ownership stays with the RX thread.
3. Independently, a **fresh validated motor-origin DP1=0 or DP1=2**
   requests hardware mode. Physical-button movement can switch after its
   first correctly decoded DP1, not necessarily before the first packet.
4. In hardware mode UARTE0 + EasyDMA receives **all incoming bytes**.
   DMA is stopped and harvested after 15 ms inter-burst idle, then
   re-armed while the motor is active. The RX stream is still passed
   through the existing Tuya parser; other motor-control behavior is
   unchanged.
5. Only a **fresh validated motor DP1=1 (STOP)** starts the 1500 ms
   holdoff before returning to the hybrid idle receiver. Cached STOP
   from position/battery reports cannot do this.
6. If STOP never arrives, a 120000 ms maximum direct-RX timer reverts
   to hybrid mode and increments `hw_direct_max_timeouts`. The watchdog
   **does not send an actual STOP command to the motor**.
7. Both direct UARTE0 RX and separate UARTE1 TX run at 9600 8N1.
   UART 3.3 V logic is mandatory; no changes to the battery wiring.

## New Z2M diagnostics (Basic cluster, readonly)

| Field | Attribute | Meaning |
| --- | --- | --- |
| hw_direct_active | 0xF15D | 1 = direct UARTE0 RX active |
| hw_direct_enters | 0xF15E | Number of hardware RX entries |
| hw_direct_exits | 0xF15F | Number of returns to low-power RX |
| hw_direct_bytes | 0xF160 | DMA bytes in direct hardware mode |
| hw_direct_restarts | 0xF161 | Number of UARTE0 direct DMA burst rearms |
| hw_direct_max_timeouts | 0xF162 | 120 s power-watchdog activations |
| hw_direct_prearm_timeouts | 0xF163 | TX sent before hardware RX ready |
| hw_direct_last_switch_ms | 0xF164 | Last mode switch uptime, ms |

`refresh_all_diagnostics` still reads all attributes in batches; the
`N/A` value for the refresh action is expected.

## Bench validation checklist

1. Do **not** replace v42.0-41 until the GitHub Actions Nordic compile,
   UF2 bounds and static regression checks have all passed.
2. Save the complete baseline counters before power-off. Standalone
   SWBuildID must read **42.0-42** after flashing.
3. With motor secured on table and clear of loose wires, wake MCU,
   pair/reconnect Z2M. Check that `hw_direct_active=0` and that idle
   current is comparable to the previously measured ~0.03–0.05 mA
   v42.0-37 benchmark (not assumed valid for this build).
4. Open once and observe `hw_direct_active=1` with new
   `hw_direct_enters`; verify MCU returns a fresh STOP, then
   `hw_direct_active=0` ~1.5 s later (plus diagnostic refresh delay).
5. Perform two complete open/close cycles. Compare deltas
   `rx_framing_errors`, `tuya_frames_ok`, `tuya_frames_rejected`,
   `hw_direct_bytes`, `hw_direct_prearm_timeouts`,
   `hw_direct_max_timeouts` and `uart_hardware_errors`.
6. Measure active current while motor moves and current at rest; direct
   UARTE0 listening will be higher current than sleeping hybrid.
7. If RX no longer updates, stop automated movement and revert to
   known good v42.0-41. Zero software framing errors alone do not prove
   all frames were captured.

**No real-world stability, IRQ timing, power or response measurements
have yet been obtained for v42.0-42.**
