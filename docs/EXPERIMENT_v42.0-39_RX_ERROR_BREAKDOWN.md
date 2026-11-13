# v42.0-39 — UART RX error breakdown, low-power baseline unchanged

**Base:** `experiment/v42-0-37-zigbee-fast-poll-uart-wake`. The separate OTA research branch `experiment/v42-0-38-zigbee-ota-readiness` remains untouched. This build does **not** enable Zigbee OTA.

## Why
Hardware test v42.0-37 after reboot: average quiet current approximately **0.03–0.05 mA** (user measurement), significantly faster Zigbee cover response with **1 s End Device parent long poll**, hybrid UART running in phase 7 for **1978 s**, 577 correct Tuya frames, 2 rejects, 32 combined `rx_error_count`, 0 handoff errors, 0 hardware UARTE errors. The aggregate of 32 can include both malformed software stop bits and ring overflows; it does not identify the cause. An `rx_last_error=2` is a **priority-selected class, not timestamp of the latest error**.

We are not changing the first-byte wake / UART timing without evidence. Keep the 1-second Zigbee poll, the TIMER3 GPIO start detection, the same stop-bit handling, UARTE0 EasyDMA, the **15 ms RX idle disable**, and UARTE1 TX power gating. v42.0-39 adds only diagnostic accounting.

## New read-only Zigbee Basic cluster fields
| Field in Zigbee2MQTT | Basic attr | Interpretation |
|---|---|---|
| `rx_framing_errors` | `0xF13C` | GPIO/TIMER3 decoded software stop-bit invalid. This can happen on spikes/false starts as well as bad UART data |
| `rx_ring_overflows` | `0xF13D` | Bytes dropped because ring buffer is full. Includes drops from first software byte and EasyDMA copies |
| `rx_software_starts` | `0xF13E` | Number of software bit-sampling starts, **not** number of valid Tuya packets |
| `rx_software_nonheader` | `0xF13F` | Software-decoded bytes whose value is not `0x55`. No filtering, capture rules unchanged |
| `uart_last_errorsrc` | `0xF140` | Nordic UARTE0 `ERRORSRC` bits at most recent hardware error event. Zero until hardware fault |

The existing diagnostic `rx_error_count` continues to be evaluated every ~5 seconds as `rx_framing_errors + rx_ring_overflows + uart_hardware_errors + uart_handoff_errors`. This is a **cumulative counter**, unlike `rx_last_error` which is an error-class indicator based on priority. All counters reset on nRF52840 reboot, not on Zigbee2MQTT restart.

`rx_header_count`, `tuya_frames_ok`, and `uart_wake_count` are independent counters. Reading them via Zigbee2MQTT is not an atomic snapshot; different ZCL reads / cached state can temporarily disagree. Compare changes over fixed intervals, not individual equalities.

## Procedure after passing GitHub Actions and flashing an isolated test board
1. Save the tested **v42.0-37 UF2** for rollback. v42.0-39 can move the motor through explicit Zigbee commands; disable automated curtain movement for the test.
2. Reboot nRF52840. Wait for `uart_test_phase=7`; read the five new error-source fields, existing frame counts and total error count. Note measured quiet current, should be compared to v42.0-37 **0.03–0.05 mA**.
3. Re-read at **2 min**, **10 min**, and **30 min** after power-on, preferably with the motor idle and without manually commanding the motor.
4. In a separate supervised trial, send an explicit STOP followed by OPEN briefly and STOP. Record counters immediately before and after motor motion. Do not assume movement is safe if the physical travel limits were not tested.
5. Use differential counts to isolate failure type:
   - `rx_framing_errors` rises, `rx_ring_overflows=0`: noisy or unaligned software first-byte sampling, line glitches, timing or wiring—do **not** automatically attribute it to the MCU.
   - `rx_ring_overflows` rises: receiver is not draining ring quickly enough; investigate thread scheduling, burst duration or consumers.
   - `uart_hardware_errors` grows and `uart_last_errorsrc != 0`: inspect Nordic UARTE0 error-source bits. A zero raw source doesn't prove no problem.
   - `rx_software_nonheader` increases together with software framing while no valid packets are expected: investigate pin edge noise/partial MCU traffic.
6. Compare `tuya_frames_rejected` and `tuya_frames_ok`, Zigbee LQI, UARTE wake count, and real average idle current. **If power consumption rises or RX success falls**, roll back to v42.0-37.

## Important limitations
- This version **separates the problem; it does not fix it yet**. The correct corrective change depends on which source dominates.
- No hardware UART RX/TIMER3 timing parameters are modified. One extra counter increment is in the valid-but-non-0x55 software-byte path, not in the 0x55->DMA handoff path; overhead is expected to be small, but **power consumption requires a new bench measurement**.
- The OTA branch is separate and remains nonfunctional for actual OTA installation. **Do not upload UF2 as Zigbee OTA**.
- Code commit does not prove successful Nordic/Zephyr compilation or HW-954 behavior; inspect the CI status and test on hardware before considering v42.0-39 validated.
