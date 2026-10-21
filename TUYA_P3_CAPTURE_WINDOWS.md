# Tuya P3 / WBR3 passive UART capture — Windows

## Purpose

Identify actual motor-specific serial protocol and DP values **without transmitting
anything**, so the nice!nano can eventually replace WBR3. Firmware UART TX and
Zigbee movement actions deliberately remain disabled. The parser is a diagnostic
aid, not a substitute for a measured motor-specific UART capture.

Tuya documents two distinct wire formats:

- Classic MCU: `55 AA ver cmd len_hi len_lo data checksum`;
  typical DP set `0x06`, report `0x07`.
- Zigbee MCU: `55 AA 02 seq_hi seq_lo cmd len_hi len_lo data checksum`;
  DP receive `0x04`, MCU DP reply `0x05`, proactive DP `0x06`,
  grouped DP `0x2A`, passive report `0x2C`.

References:
- https://developer.tuya.com/en/docs/mcu-standard-protocol/mcusdk-zigbee-uart-protocol?id=Kdg17v4544p37
- https://developer.tuya.com/en/docs/iot/tuya-cloud-universal-serial-port-access-protocol?id=K9eigf2el456o

**Important:** None of the above proves that the exact WBR3/P3 motor uses
one format or a particular mapping. Even an apparent DP1/DP3/DP13 mapping
must be confirmed using captured request/response pairs.

## Connections: passive only

Only collect signals from a safely powered, accessible low-voltage circuit.
Identify UART TX/RX and **measure their actual logic voltage first**.
The adapters must accept the measured logic voltage, usually 3.3 V.
Avoid probing equipment with exposed mains wiring; have a qualified
technician perform the test if mains-voltage circuitry is accessible.

Use two 3.3-V-compatible USB–TTL UART receivers (or a suitable logic analyzer)
for both directions simultaneously:

| Adapter input (RX only) | Existing original wire | Capture direction |
|---|---|---|
| Receiver A RX | WBR3 TX -> P3 RX | WBR3 -> P3 |
| Receiver B RX | P3 TX -> WBR3 RX | P3 -> WBR3 |
| Both receiver GND | Circuit low-voltage GND | Common reference |

**Never** connect either adapter's TX or 3V3/5V/VCC power output.
Do not cut or disconnect the original WBR3–P3 serial lines for passive
capture. A single receiver is also usable but captures only one direction
per run. Observe electrical safety and avoid changing the circuit.

## Capture

Windows PowerShell, from the repository root:

```powershell
py -m pip install pyserial
py tools/tuya_capture.py capture --wbr3-tx COM5 --p3-tx COM6 --baud 9600 --seconds 120 --out tuya_p3_capture.jsonl
```

Replace COM5/COM6 with your actual two serial port names.
The tool **only calls Serial.read**, never `Serial.write`.

With **one receiver**, capture module TX first:

```powershell
py tools/tuya_capture.py capture --wbr3-tx COM5 --seconds 120 --out wbr3_tx.jsonl
```

Then physically move only the adapter's RX input to P3 TX and capture again:

```powershell
py tools/tuya_capture.py capture --p3-tx COM5 --seconds 120 --out p3_tx.jsonl
```

The JSONL retains UTC timestamps, original raw byte chunks, direction labels,
full reconstructed frames and DP data. A partial frame or unrecognized frame
is still preserved as raw bytes.

After capture, offline decode and inspect:

```powershell
py tools/tuya_capture.py decode --file tuya_p3_capture.jsonl
py tools/tuya_capture.py decode --hex "55 AA 02 00 01 04 00 05 01 04 00 01 00 0C"
```

Last line is illustrative only; use actual captured data for conclusions.

## Capture sequence

1. Keep original WBR3 and P3 connected. Capture approximately 20 seconds
   after normal power-on, while no motion command is issued.
2. Start movement using the **original, existing** control.
   Capture separate OPEN, STOP, CLOSE actions, one at a time, with a
   visible pause between actions. Avoid operating the motor near an
   unsafe end stop.
3. Capture several known intermediate setpoints (e.g. 25%, 50%, 75%).
   Record what physical position each action really produced.
4. Capture charging/battery-status changes if convenient.
5. Save captures and a short text timeline mapping action to timestamp.

Example notes:

```text
12:43:02  idle/power-on
12:43:29  original remote OPEN
12:43:34  original remote STOP
12:43:45  original remote CLOSE
12:44:02  original remote set 50%
```

Share both JSONL capture and notes after reviewing for any identifiers
(serial numbers, PAN IDs, installation codes, product credentials). Then
we can determine actual DP IDs, values, UART direction, checksums,
boot handshakes, and safe TX behavior.

## Not ready to flash

Passing host tests and CI merely verifies parsing and building. It does
**not** validate the nice!nano UF2/S140 memory map or battery behavior.
Do not install the diagnostic UF2 on the device.
