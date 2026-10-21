# Window Covering End Device development branch

This branch replaces the Nordic Zigbee Router template with a battery-oriented
**Zigbee End Device** exposing Home Automation Window Covering (device type
0x0202, server cluster 0x0102) at endpoint **10**.

## Present in this stage

- End Device role (`CONFIG_ZIGBEE_ROLE_END_DEVICE=y`).
- `zigbee_configure_sleepy_behavior(true)`, called before
  `zigbee_enable()`; 30-second keepalive, 64-minute aging timeout.
- Basic (0x0000), Identify (0x0003), Groups (0x0004),
  Scenes (0x0005), Window Covering (0x0102) server clusters.
- Roller shade type; initial position **unknown** (`0xFF`);
  status reports **not operational** until motor feedback is available.
- Commands Open, Close, Stop, Go to Lift Percentage are **rejected with
  an error**, rather than falsely acknowledged or simulated.
- UART0 pin routing is unchanged (P0.20 TX, P0.17 RX, 9600 baud).

## Not yet implemented

- Tuya P3 UART frames, command confirmation, position and battery updates.
- Zigbee Power Configuration (0x0001) battery reporting.
- Hardware sleep current or radio commissioning tests on the physical board.
- Verification of nice!nano S140/UF2 bootloader compatibility.

**Never flash the diagnostic UF2 image without confirming the actual
bootloader's flash map and app start address.** The build-time UF2 bounds
checker only verifies that the UF2 blocks remain in the provisional
application region.

## Test

GitHub Actions runs the same diagnostic build used in `main`, but on this
feature branch. After a passing build, inspect `zephyr.config`,
`zephyr.dts`, and `uf2_report.txt`; test motor UART in a later stage.
