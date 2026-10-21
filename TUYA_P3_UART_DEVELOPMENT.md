# Tuya P3 UART protocol: receive-only diagnostic integration

This development branch now contains a bounded, checksum-validated Tuya MCU
serial-frame parser in `include/tuya_frame.h` and `src/tuya_frame.c`.
Zephyr's `src/tuya_uart.c` attaches to UART0 / alias `motor-uart`, on P0.20
TX / P0.17 RX, 9600 8N1, using interrupt-driven RX and a queued worker.
No UART transmit operation is performed.

Two formats are recognized:

- **Classic Tuya MCU:** `55 AA ver cmd length_hi length_lo payload sum`; DP
  reports use command 0x07.
- **Tuya Zigbee MCU:** `55 AA 02 seq_hi seq_lo cmd length_hi length_lo payload sum`;
  DP reports use command 0x06 or 0x2C. This format is **not equivalent**
  to Classic UART and must not be confused with it.

DP items are parsed as ID/type/length/value, including multiple items
within one report. Malformed frames, invalid checksums and overlong payloads
are rejected. The parser has host tests executed in GitHub Actions.

**No motor movement is enabled.** Exact commands for DP1 and assignments
for DP3 (position) and DP13 (battery) remain unverified for our P3 motor.
Received DPs only produce diagnostics; position and battery ZCL attributes
are not modified. All Window Covering movement commands continue to return
an error. The frame encoder exists solely for tests/future work and is not
connected to UART TX.

The runtime application still has no verified logging transport; `LOG_INF`
requires a future RTT or comparable debug backend to observe actual frames.

## Next verification
1. Obtain motor-specific UART captures showing frame version, command,
   DP IDs/types and direction values (OPEN/STOP/CLOSE).
2. Determine whether startup handshake and acknowledgments are necessary
   before the module can replace WBR3 reliably.
3. Add correct UART TX commands only after captured evidence.
4. Add Zigbee reporting only after position/battery semantics are confirmed.
5. Verify physical flash map/S140/UF2 bootloader before flashing.

This is a **compile/test prototype**, not a hardware-validated binary.

## Passive capture utility

See [TUYA_P3_CAPTURE_WINDOWS.md](TUYA_P3_CAPTURE_WINDOWS.md). The host decoder supports all documented DP-carrying command IDs; no UART TX is added.

## Previously captured motor-specific data

Real UART WBR3 ↔ P3 captures from October 19–20 are already available. See [P3_CONFIRMED_UART.md](P3_CONFIRMED_UART.md). Do not repeat captures solely to rediscover OPEN/STOP/CLOSE and DP2 set position; command encoders and host regression tests now use those exact frames.

## Current firmware implementation

The previously reported pending TX and DP status items are now implemented **behind OFF-by-default Kconfig gates**. See [P3_ZIGBEE2MQTT.md](P3_ZIGBEE2MQTT.md) and `src/main.c`, `src/tuya_uart.c`. The diagnostic instructions above describe the original receive-only stage, not the current optional bench TX build.
