# Captured Tuya P3 motor UART protocol — 19–20 October 2026

This document records **previously measured UART exchanges** from the original
WBR3 ↔ P3 installation, instead of requesting a redundant new capture.
Hardware equipment used: CP2102 USB–UART receiver. Serial: **9600 8N1**.

## Confirmed: WBR3 → P3

All examples use classic Tuya framing: `55 AA 00 06 LEN_H LEN_L DP CHECKSUM`.

| Operation | Hex capture | Status |
|---|---|---|
| OPEN (DP1 enum=0) | `55 AA 00 06 00 05 01 04 00 01 00 10` | observed |
| STOP (DP1 enum=1) | `55 AA 00 06 00 05 01 04 00 01 01 11` | observed |
| CLOSE (DP1 enum=2) | `55 AA 00 06 00 05 01 04 00 01 02 12` | observed |
| SET POSITION=50 (DP2 value=50) | `55 AA 00 06 00 08 02 02 00 04 00 00 00 32 47` | observed |

The previous experiments also included physically opening, closing and setting
a 49% target. That observed target matched DP2=49 in the UART log. The exact
0/100 *physical end-stop orientation* has not been independently established.

## Confirmed: P3 → WBR3

Reports captured with **protocol version 0x03**, command **0x07**:

| Field | Captured hex | Value |
|---|---|---|
| DP3 (position-related) | `55 AA 03 07 00 08 03 02 00 04 00 00 00 3E 58` | 62 |
| DP13 (battery-related) | `55 AA 03 07 00 08 0D 02 00 04 00 00 00 28 4C` | 40 |

Original log also showed DP1=1, DP7=2, DP12=0, DP101=0, DP102=0,
DP103=28, DP104=10, DP105=50, DP106=100 and DP108 as seven zero bytes.
Product identifier `edfol3hy9ljq9ccv`, software version `1.0.0`.

**Important distinctions:** DP3=62 was observed; mapping it to the physical
current position is plausible but still requires runtime correlation.
DP13=40 was observed and coincided with a reported battery level of 40%;
a separate calibration/voltage verification has not been completed. They must
NOT be silently treated as confirmed ZCL reporting semantics.

## Firmware status

`src/tuya_motor.c` encodes all FOUR observed WBR3→P3 requests exactly.
`tests/test_tuya_motor.c` checks emitted bytes byte-for-byte and validates
both observed MCU→WBR3 reports.

**Encoding is not transmitting.** No live UART TX calls or motor commands
have been enabled in the runtime. Movement Zigbee callbacks still reject
requests until physical bootloader compatibility and failure/acknowledgment
handling are validated. Data are currently diagnostic only.

## Still required, without repeating existing captures

- Verify the physical nice!nano UF2 bootloader / reserved flash offset,
  SoftDevice assumptions and Zigbee stack launch.
- Design motor initialization/acknowledgments and failure handling.
- Verify mapping of DP3 and DP13 to accurate Zigbee attributes, and end-stop
  polarity for percentage positioning.
- Later, field-test with safety limits and reconnect behavior.

Do **not** flash this diagnostic image based solely on CI success.
