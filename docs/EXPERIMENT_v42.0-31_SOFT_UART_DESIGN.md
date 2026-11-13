# v42.0-31 — GPIOTE + TIMER software UART design (NOT FLASHABLE)

## Objective
Reduce HW-954 nRF52840 consumption from measured 1.39–1.41 mA in v42.0-29 continuous UARTE RX, while retaining passive, lossless Tuya P3 9600 baud 8N1 RX on P0.17. The v42.0-22 UART PM loopback drew 0.02–0.04 mA in a *different test mode*; it is not proof that full Zigbee + GPIO software RX will reach that figure.

## Why the proposed first-byte handoff is not safe
UART 9600 8N1 bit time is 104.1667 us, a byte occupies 1.04167 ms. Motor frames may contain back-to-back bytes. Starting UARTE after receiving byte 0x55 by GPIO does not guarantee that byte 0xAA can be captured; the UARTE must be armed before its start bit. Do NOT deploy a hybrid receiver without a demonstrated inter-byte idle gap. A GPIO-only full-frame receiver avoids this handoff.

## Proposed peripheral pipeline (requires verified implementation)
1. UARTE0 remains suspended/deinitialized; pin P0.17 is an input with pull-up.
2. GPIOTE detects the falling edge of a start bit while the receiver is idle.
3. TIMER in hardware-timed capture/compare mode samples the first data bit at 1.5 bit periods after the start edge (156.25 us), then at 1-bit intervals (104.1667 us). Sampling jitter must be bounded; use PPI/DPPI or a verified high-priority timer IRQ, not k_sleep/k_timer.
4. Assemble eight LSB-first bits and verify the stop bit at 9.5 bit periods. Feed complete bytes to a bounded ring and the existing tuya_parser_feed.
5. For continuous bytes, re-arm falling-edge detection in time for the next start bit, accounting for the 0.5-bit margin after stop-bit center. Do not busy-wait in an ISR.
6. Zigbee uses TIMER2 according to the board DTS; select a free TIMER instance after verifying generated device tree and Nordic Zigbee stack reservations. Confirm PPI/GPIOTE allocation conflicts and ISR priority.
7. Provide read-only Zigbee diagnostics: received bytes, valid frames, rejected frames, framing errors, ring overflow, missed starts, maximum sampling jitter. Motor TX remains physically and logically disabled.

## Required validation gates before flashing
- Build against pinned ncs-zigbee R23 v1.4.0; inspect Zephyr/nrfx IRQ and peripheral ownership.
- Prove byte recovery on 0x55 0xAA 0x03 0x07 and back-to-back 9600 8N1 test streams.
- Verify Zigbee channel 11 operation, no timer or interrupt conflict, and bootloader layout.
- Test motor CLOSE/STOP and OPEN/STOP, 20 min passive capture, then activity after prolonged idle. Compare to v42.0-29 (233 valid, 1 rejected, 3337 bytes at 1200s).
- Measure board-only current in idle and during bursts; record average current and peak.
- Revert to v42.0-29 if frame loss, resets, or Zigbee degradation.

## Current status
Architecture/design only. No receiver source implementation, successful CI build, UF2, or hardware validation yet. Never label this branch as a flash-ready firmware.
