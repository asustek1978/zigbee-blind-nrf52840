# P3 nRF52840 v42.0-44 — SAADC rails / passive idle UART diagnostics

**Unverified experimental bench build.** Base: measured working v42.0-43
non-renewable 120 s watchdog. The prior experimental branch remains intact.

## Important: battery voltage is NOT established from VDDH alone

nRF52840 has internal SAADC sources `VDD` and `VDDH/5`, but the actual
HW-954 v0.0.5 schematic (BAT+, RAW, charger and regulators) has **not
been verified**. nRF52840 is not itself a battery-charge controller.

This firmware reads **supply rails only**. It will not modify standard
`battery` percentage (still original motor DP13), standard Zigbee
BatteryVoltage, or invoke any battery charger.

Zephyr SAADC driver is used for two internal sources:

- channel 0: `NRF_SAADC_VDDHDIV5`, internal 0.6 V reference,
  1/2 gain; computed physical VDDH = measured mV × 5.
- channel 1: `NRF_SAADC_VDD`, internal 0.6 V reference, 1/6 gain;
  measured VDD = mV.
- sample once ~30 s after startup, then every 10 min, skipping the
  cycle if full UARTE0 motor RX is active. No GPIO or resistive divider
  connected; no automatic battery capacity conversion.

### SAADC Zigbee Basic read-only fields

| Attribute | Z2M | Description |
|---|---|---|
| 0xF16A | saadc_vddh_mv | nRF52840 VDDH rail, **NOT verified BAT+**, millivolts |
| 0xF16B | saadc_vdd_mv | nRF52840 VDD rail, millivolts |
| 0xF16C | saadc_valid_mask | bit 0=VDDH valid, bit 1=VDD valid |
| 0xF16D | saadc_samples | attempted rail sample cycles |
| 0xF16E | saadc_last_ms | last sample attempt uptime ms |
| 0xF16F | saadc_last_error | signed error reinterpreted as unsigned 32-bit; 0 means success |

On the bench, first measure BAT+/GND and 3.3 V rail with a multimeter
(ideally while BAT+ is moderately discharged). Compare them with VDDH
and VDD readings. **Never assume equal voltages are the same circuit
without confirming the board wiring.**

## Idle UART: passive error classification

The prior v42.0-43 logs showed 12 bad STOP samples while last accepted
MCU DP1=1 (idle), comprising 2 first-byte GPIO failures and 10 chained
software failures. All UARTE0 hardware errors were zero. That alone does
not distinguish noise/false-start bursts from real UART baud timing
errors; there was no confirmed lost Tuya frame.

This version deliberately does **not** change GPIO/TIMER3 sampling, RX
timing, DMA, Zigbee motor commands or the 120 s watchdog. Instead it adds:

| Attribute | Z2M | Description |
|---|---|---|
| 0xF170 | rx_idle_fault_episodes | bad STOP clusters separated by >250 ms |
| 0xF171 | rx_bad_stop_post_exit_30s | bad STOP samples within 30 s after direct RX exit |
| 0xF172 | rx_bad_stop_late_or_no_exit | bad STOP samples outside that window |
| 0xF173 | rx_last_bad_stop_gpio_age_ms | age of last accepted GPIO start when last bad STOP sampled |
| 0xF174 | hw_direct_last_exit_ms | uptime of last full-hardware RX exit |

### Test procedure

1. Back up installed v42.0-43 UF2, settings and Z2M converter.
2. Flash v42.0-44 **only to a secured motor on bench**. Check
   SWBuildID `42.0-44`, avoid unattended shutter automations.
3. Update only the matching JS/MJS converter (one installed file),
   restart Zigbee2MQTT. Start with `Refresh all diagnostics`.
4. Wait at least 40 s after boot and with `hw_direct_active=0`.
   Confirm `saadc_valid_mask=3` and credible rail readings; log
   `saadc_last_error`. If status is not 3, the voltage is invalid.
5. Measure BAT+ and VDD using a meter. Record voltage and temperature;
   repeat at a different battery state before labeling an ADC rail
   as battery. Do NOT expose an inferred SoC percentage.
6. With the motor secured and free to travel, do 5 OPEN/CLOSE/POSITION
   commands; ensure `Tuya frames rejected`, `Rx framing errors`,
   `Hw direct hw errors` and `Hw direct prearm timeouts` are stable.
7. In stable idle, leave untouched 10–30 min. Refresh diagnostics.
   If software bad STOP errors recur, report F170..F174, existing
   GPIO-vs-chained counters, timestamps, error byte, and total frames.
8. Verify full UARTE0 direct mode exits within 120 s if no MCU STOP
   report arrives, and remains locked out until fresh STOP or command.
9. Measure whole-device low-power current after ADC sample and after
   hardware UARTE0 exit; software no longer assumes current draw.

**Do not** connect both P3 battery charging and HW-954 USB charging
without validating charger power paths. ADC rails cannot by themselves
prove charger operation or safe charge current.

### Firmware quality gate

GitHub Actions should pass Python static contracts for v42.0-44,
compile C with Nordic SDK Zigbee R23 v1.4.0, and verify flash layout
and UF2. These checks are **not** evidence of hardware ADC accuracy,
absence of extra battery drain or motor safety.
