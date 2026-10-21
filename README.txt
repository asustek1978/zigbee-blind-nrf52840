nice!nano Zigbee prototype board definition — NOT A FLASHABLE FIRMWARE.

Copy the boards directory into C:\ncs\projects\zigbee_blind_nrf52840\.

This defines SoC, 802.15.4 radio, and UART0 at TX P0.20 / RX P0.17 / 9600.
It does NOT establish UF2 bootloader memory compatibility.
The included nrf52840_partition.dtsi describes stock flash partitions, NOT the nice!nano S140/UF2 layout.
Do not flash a firmware built from this configuration yet.

To test board discovery from the ncs-zigbee workspace:
  python -m west boards --board-root C:/ncs/projects/zigbee_blind_nrf52840 | findstr nicenano

If discovery fails, inspect board.yml vendor support for the installed Zephyr version.
The original Nordic sample still uses DK LEDs and router mode; it cannot compile for this board unchanged.
