# Safe first UF2 image for HW-954 / Tuya P3

## What this file will be
`P3-NRF52840.uf2` is a **Zigbee application**, not a replacement for
the installed UF2 bootloader 0.6.0 or S140 6.1.1.

The sole authorized release profile for initial flashing must have
`CONFIG_P3_UART_TX_ENABLE=n`, `CONFIG_P3_MCU_INIT_ENABLE=n`,
`CONFIG_P3_ZCL_POSITION_ENABLE=n`,
`CONFIG_P3_ZCL_POSITION_INVERT=n`, and
`CONFIG_P3_ZCL_BATTERY_ENABLE=n` (all verified in CI).

The file is packaged only after the actual Nordic Zigbee R23/Zephyr
build, all host unit tests, the bootloader-aware address check and
initial Cortex-M vector validation succeed.

## Target
Physical clone HW-954; device-reported `UF2 Bootloader 0.6.0`,
`Board-ID: nRF52840-nicenano`, `SoftDevice: S140 version 6.1.1`.
The read-only `CURRENT.UF2` showed a blank/erased application partition
0x26000–0xAD000 but did not include the bootloader itself.

## First test precautions
1. Disconnect Tuya P3 battery, charging, UART, and motor wiring.
2. Connect HW-954 via its USB port alone.
3. Double-tap RESET to open the `NICENANO` UF2 volume.
4. Copy `P3-NRF52840.uf2` to that mounted device **only after
   accepting that booting the actual clone has not been field-verified**.
5. Watch for a clean Zigbee End Device join and Zigbee2MQTT interview.
   Movement commands are deliberately rejected. For a failed app, attempt
   recovery via double-tap RESET and reflash, or use SWD if needed.
6. Do not connect the motor or shared battery while powered from USB
   until power isolation and UART backfeed have been checked.

No Flash erase, UICR, bootloader, or SoftDevice region is included
in the application image. The Zigbee NVRAM partition is reserved in
the device tree but is not part of the UF2 application upload.
