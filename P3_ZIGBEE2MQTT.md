# P3 nRF52840 Zigbee2MQTT integration (prototype)

This firmware advertises **model** `P3-NRF52840` and **manufacturer**
`LocalDIY` in the Zigbee Basic cluster, endpoint **10**.

The endpoint exposes server clusters:
- Basic `0x0000`, Identify `0x0003`, Groups `0x0004`, Scenes `0x0005`
- Window Covering `0x0102` (lift-only, HA device ID `0x0202`)
- Power Configuration `0x0001` (BatteryVoltage unknown `0xFF`,
  BatteryPercentageRemaining unknown `0xFF` until real status is enabled)

## Zigbee2MQTT converter

When the board is finally safe to flash and can join a network, copy
[zigbee2mqtt/external_converters/p3_nrf52840.mjs](zigbee2mqtt/external_converters/p3_nrf52840.mjs)
into Zigbee2MQTT's external converter directory and load it through the
Zigbee2MQTT interface / external converter configuration appropriate to the
installed release. The converter uses standard `fz.cover_position_tilt`,
`fz.battery`, `tz.cover_state`, `tz.cover_position_tilt` and endpoint 10.
It configures reporting for position and half-percent battery.

Device-specific power/position behavior is controlled by application Kconfig:

| Option | Default | Effect |
|---|---|---|
| `CONFIG_P3_UART_TX_ENABLE` | **n** | Physically send verified UART commands on P0.20 |
| `CONFIG_P3_MCU_INIT_ENABLE` | **n** | Replay observed non-movement heartbeat/product/mode/DP query once at boot when TX is enabled |
| `CONFIG_P3_ZCL_POSITION_ENABLE` | **n** | DP3 -> ZCL lift percentage, allow position targeting |
| `CONFIG_P3_ZCL_POSITION_INVERT` | **n** | Convert DP3 and ZCL target with 100-value when polarity is reversed |
| `CONFIG_P3_ZCL_BATTERY_ENABLE` | **n** | DP13 (0-100) -> ZCL BatteryPercentageRemaining (0-200) |

Default firmware ALWAYS rejects Open/Close/Stop/Go To Position Zigbee commands;
this is deliberate so **no unintended motor movement** can occur on power-up.
The reported current position and battery start as **unknown**.

After hardware qualification only, use a dedicated overlay for enabled features;
do not edit production defaults blindly. `tests/p3_bench_features.conf` exists
**only for CI compilation** of branches through the full enabled code paths,
not for loading into the device.

## Validation that remains required

1. Prove nice!nano **HW-954 / UF2 0.6.0 / S140 6.1.1** bootloader and flash map
   match the compiled Zigbee application. The current `0x26000` app partition
   and `0xf4000` bootloader range are **provisional**.
2. Verify original P3 UART TX/RX electrical levels and independent battery
   feeding. Do not USB-power/program nice!nano while connected to the P3
   battery charging circuit until isolation is validated.
3. Verify actual Zigbee join/interview, receive report timing, battery drain,
   Zigbee2MQTT commands and reporting.
4. Confirm lift endpoint orientation (0=open, 100=closed), and check DP13
   across different charge levels. No invented voltage report is included.
5. Establish motor handshake/timeout/negative acknowledgment semantics.
   A UART queue acceptance means **accepted**, not physical completion.
   Frames are sent **once**, without dangerous repeat/retry behavior.

## Regression tests

CI additionally checks Zigbee2MQTT converter syntax using Node.js. CI always runs pure host C command-capture and DP-state tests, a passive
Python UART decoder test, the **default safe firmware build** with bounds
check, and a **separate bench-only enabled firmware compilation**. Only the
SAFE configuration files are archived, not executable firmware UF2 binaries.
