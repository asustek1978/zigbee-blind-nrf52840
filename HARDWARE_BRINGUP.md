# HW-954 / P3 — verification gates before flashing

**Status:** both safe and bench-enabled Zigbee End Device firmware builds
passed GitHub Actions #18. This is still not hardware validated.

## Confirmed on actual hardware
- Original P3 motor uses classic Tuya UART 9600 8N1.
- P3 regulated rail 3.306 V; 1S battery 4.11 V.
- On P3 adapter, both VCC pads share one net and both GND pads share
  ground, without a VCC-GND short.
- P3 battery negative shares WBR3 adapter GND.
- HW-954 RAW = B+ and B- = GND (continuity).
- HW-954 USB-only test: VCC 3.298 V; RAW 4.2–4.3 V.

## UART
- HW-954 P0.20 TX -> motor RXD
- HW-954 P0.17 RX <- motor TXD
- HW-954 GND <-> motor GND
- Battery must be disconnected and USB removed during soldering.

## Power is NOT finalized
HW-954 VCC is not a validated external 3.306 V power input.
A diode in the battery-positive branch alone is not complete USB
charging/backfeed protection. Avoid connecting both P3 battery and
HW-954 USB until the clone's charger circuit and UART backfeed paths
are verified. Use an easily disconnected power link for experiments.
SS24 or 1N5819 is only a candidate, subject to voltage-drop/load
and depletion testing. Do not connect 3.306 V from P3 to RAW.

## Critical flash map unknowns
The device tree assumes firmware starts at 0x26000, NVRAM starts
0xEC000 and UF2 bootloader is at 0xF4000. Physical compatibility of
the installed HW-954 UF2 v0.6.0 / S140 v6.1.1, bootloader vectors,
flash protection and ZBOSS NV settings is NOT established.
Passing the UF2 layout validator is not proof of compatibility.
Do not flash this binary until this mapping and recovery are proven.

## Release sequence
1. Verify hardware charging isolation, safe voltage and load.
2. Verify memory layout, backup/recovery and flash compatibility.
3. Flash only independently validated non-motor diagnostic firmware.
4. Check Zigbee network join, interview, sleepy polling and current.
5. Check captured startup protocol, live DP3/DP13 mapping, polarity,
   operational status and RX feedback.
6. Test STOP and command acknowledgments; only then arm UART TX and
   exercise OPEN/CLOSE/position under physical supervision.

Default firmware intentionally rejects movement commands. Bench-enabled
configuration is a compile test, not authorization to move the motor.
