# HW-954 nRF52840 — Zigbee OTA migration plan (SAFE, NOT YET ENABLED)

## Current decision (v42.0-38)
**DO NOT upload Zigbee OTA files to the P3 yet.** This branch adds a reproducible OTA-readiness audit and repository tests only. It does **not** implement an OTA Upgrade client or install an OTA-capable bootloader. Normal UF2 builds remain possible. A normal `.uf2` **is not** a Zigbee OTA file.

Source audit: `tools/check_zigbee_ota_readiness.py` checks `boards/others/nicenano_zigbee/nicenano_zigbee_nrf52840.dts` and `prj.conf`; `--require-ready` exits nonzero unless all prerequisites are satisfied. Current status is deliberately **BLOCKED**, even if flags are toggled, because the physical HW-954 bootloader has not been read back and verified. CI runs the audit and attaches `OTA_READINESS_BLOCKED.json`. Never weaken the guard merely to make CI green.

## Why existing HW-954 cannot safely install a Zigbee OTA image yet
The current **source-defined** nRF52840 flash map (memory layout is not fully verified by a physical flash/UICR readback):
| Range | Size | Existing purpose |
|---|---:|---|
| 0x00000–0x25FFF | 152 KiB | Existing SoftDevice/reserved |
| 0x26000–0xACFFF | 540 KiB | Executing Zephyr/Zigbee application, UF2-writable |
| 0xAD000–0xECFFF | 256 KiB | **Read-only protected UF2 guard** |
| 0xED000–0xF0FFF | 16 KiB | ZBOSS NVRAM (network pairing) |
| 0xF1000–0xF1FFF | 4 KiB | ZBOSS product config |
| 0xF2000–0xF3FFF | 8 KiB | Zephyr settings/NVS |
| 0xF4000–0xFFFFF | 48 KiB | UF2 bootloader/reserved |

The source **does not define** a second upgrade slot or MCUboot boot partition. The protected 256-KiB gap is smaller than the full 540-KiB primary application capacity and is **not** a safe OTA storage slot. Reusing it requires a verified alternative memory map and updater; do not write there now. Zigbee packet reception itself cannot safely overwrite the executing image. A failed in-place flash replacement could brick the device, lose pairing or corrupt settings.

The current build uses `nrfconnect/ncs-zigbee v1.4.0` and the existing UF2 loader. Nordic's Zigbee FOTA library normally uses MCUboot-image verification and DFU multi-image staging, not copying a UF2 file across the Zigbee radio.

## Recommended path: Zigbee OTA through Zigbee2MQTT (not Wi-Fi or BLE)
1. **Backup and verify on a separate HW-954.** Collect the physical bootloader identity (`INFO_UF2.TXT` in USB bootloader mode), full nRF flash + UICR via SWD where permitted, Zigbee pairing behavior, application size and boot offset. Preserve the tested v41.3 and v42.0-35/v42.0-37 UF2 images. Confirm a reliable hardware recovery route before proceeding.
2. **Build a new bootloader-supported partition layout in a DIFFERENT experimental branch.** Prefer signed MCUboot images with a compatible staged update slot. If a full second slot cannot fit, evaluate the *documented* Zigbee R23 compressed image option and its reduced/absent rollback, or an external SPI flash solution. Do NOT assume 256 KiB is large enough until actual compressed image size is known. An external SPI flash chip would add hardware; a compressed upgrade might avoid it but needs validation.
3. **Preserve ZBOSS identity and persistent storage.** Keep the addresses/contents at `0xED000..0xF3FFF` intact or implement and TEST a safe one-time migration; never install firmware that automatically erases or formats them.
4. **Port and test Nordic Zigbee FOTA client** to a dedicated ZCL endpoint (not the existing Window Covering endpoint 10), configure a fixed manufacturer code, image type, hardware version, monotonically increasing signed firmware version and a trusted signing key outside the repository. Handle image download errors and power loss, and verify restart and application confirmation.
5. **Generate a legitimate Zigbee OTA artifact**, with verified header, image size, manufacturer/image type/version. Do not repackage an arbitrary UF2 blob and claim it is an OTA upgrade. Limit OTA to explicit trusted user action and one test device at a time.
6. **Publish to Zigbee2MQTT custom local OTA index** only after the firmware can actually install it; for example:
   ```yaml
   ota:
     zigbee_ota_override_index_location: my_index.json
   ```
   Then check and start the update in Zigbee2MQTT, or via its documented MQTT API `zigbee2mqtt/bridge/request/device/ota_update/check` and `.../ota_update/update` with `{"id":"P3-NRF52840"}` (replace with the device's actual friendly name). NEVER start a firmware OTA with the current v42.0-36 or v42.0-37 images.
7. **Recovery test:** cut power during an OTA download on an isolated test device, verify it still boots and maintains settings, and retry. Confirm serial rescue remains possible. Test updates and rollback if the chosen boot mode supports it.

## Responsiveness upgrade alongside OTA
- **v42.0-36:** 5000-ms Zigbee end-device parent long poll could delay an incoming Zigbee command by up to about five seconds.
- **v42.0-37:** 1000-ms long poll after join/rejoin, immediate hybrid UART RX startup instead of a 30-sec boot blackout, and `k_msgq_put` immediately wakes an already-blocked UARTE1 TX worker. This is code change only until a bench flash is confirmed. Radio polling more often increases average current and can shorten battery life.
- Unlike a powered Zigbee router, a sleepy End Device cannot be remotely awakened from true sleep on an arbitrary Zigbee packet: it must poll the parent. Thus OTA and motion-response performance must both be tested with increased poll current.

## Baseline measurements to compare
v42.0-35 at 2645 seconds: 962 good Tuya frames, 0 rejects, 3 RX errors, 0 UARTE hardware errors, 0 handoff errors, 153 Zigbee LQI. At v42.0-36 441 seconds: 519 good, 2 rejected, 41 RX errors, 4 UARTE hardware, 0 handoff, 112 Zigbee LQI. OTA is NOT a workaround for this UART RX regression. Validate v42.0-37 receives reliably before any OTA-capable migration.

## References
- Nordic Zigbee R23 FOTA: https://nrfconnectdocs.nordicsemi.com/addons/ncs-zigbee/latest/lib/zigbee_fota.html
- Nordic FOTA configuration: https://nrfconnectdocs.nordicsemi.com/addons/ncs-zigbee/latest/configuring/configuring_libraries.html
- Zephyr MCUboot firmware update requirements: https://docs.zephyrproject.org/latest/services/device_mgmt/dfu.html
- Zigbee2MQTT local firmware OTA: https://www.zigbee2mqtt.io/guide/usage/ota_updates.html
