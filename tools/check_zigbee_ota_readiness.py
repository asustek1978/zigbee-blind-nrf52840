#!/usr/bin/env python3
"""Fail-closed Zigbee OTA feasibility audit for the HW-954 flash layout.

This is NOT an OTA updater, bootloader or OTA image packager.
The script prevents a normal UF2 image from being mislabelled as Zigbee OTA.
"""
from __future__ import annotations

import argparse
import json
import re
from pathlib import Path

_FLASH_SIZE = 0x100000
_REQUIRED = (
    "softdevice_partition",
    "slot0_partition",
    "uf2_app_guard",
    "zboss_nvram",
    "zboss_product_config",
    "storage_partition",
    "uf2_bootloader_partition",
)


def extract_partition(dts: str, label: str) -> tuple[int, int] | None:
    pattern = (
        rf"\b{re.escape(label)}\s*:\s*partition@[0-9a-fA-F]+\s*\{{"
        rf"(?P<body>.*?)\}};"
    )
    match = re.search(pattern, dts, re.DOTALL)
    if not match:
        return None
    reg = re.search(r"\breg\s*=\s*<\s*(0x[0-9a-fA-F]+)\s+(0x[0-9a-fA-F]+)\s*>", match["body"])
    if not reg:
        return None
    return int(reg[1], 16), int(reg[2], 16)


def inspect(dts: str, conf: str) -> dict:
    partitions = {
        p: extract_partition(dts, p)
        for p in _REQUIRED + ("slot1_partition", "boot_partition")
    }
    reasons: list[str] = []
    for name in _REQUIRED:
        if partitions[name] is None:
            reasons.append(f"missing known reserved partition: {name}")

    expected_protected = {
        "softdevice_partition": (0x00000, 0x26000),
        "uf2_app_guard": (0xAD000, 0x40000),
        "zboss_nvram": (0xED000, 0x4000),
        "zboss_product_config": (0xF1000, 0x1000),
        "storage_partition": (0xF2000, 0x2000),
        "uf2_bootloader_partition": (0xF4000, 0xC000),
    }
    for name, bounds in expected_protected.items():
        if partitions[name] != bounds:
            reasons.append(f"protected region changed: {name} expected={bounds} actual={partitions[name]}")

    actual = {name: p for name, p in partitions.items() if p is not None}
    sorted_parts = sorted(actual.items(), key=lambda x: x[1][0])
    for name, (offset, size) in sorted_parts:
        if offset < 0 or size <= 0 or offset + size > _FLASH_SIZE:
            reasons.append(f"out-of-range partition: {name}")
    for (name_a, (offset_a, size_a)), (name_b, (offset_b, _)) in zip(
        sorted_parts, sorted_parts[1:]
    ):
        if offset_a + size_a > offset_b:
            reasons.append(f"overlapping partitions: {name_a}/{name_b}")

    if partitions["slot1_partition"] is None:
        reasons.append("no secondary OTA image slot (slot1_partition)")
    if partitions["boot_partition"] is None:
        reasons.append("no MCUboot boot_partition; UF2 bootloader is not an OTA installer")
    if "CONFIG_BOOTLOADER_MCUBOOT=y" not in conf:
        reasons.append("application not built for MCUboot-signed images")
    if "CONFIG_ZIGBEE_FOTA=y" not in conf:
        reasons.append("no Zigbee OTA Upgrade client installed")
    # The checked-in DTS is NOT a readback of the actual device.
    # Never infer bootloader compatibility solely from build inputs.
    reasons.append("physical HW-954 bootloader and recovery path have not been verified")
    if partitions["uf2_app_guard"] is not None:
        reasons.append("existing 0xAD000..0xED000 guard is protected; never use it as staging")

    return {
        "board": "HW-954 nRF52840",
        "flash_bytes": _FLASH_SIZE,
        "application_bytes_available": partitions["slot0_partition"][1] if partitions["slot0_partition"] else None,
        "protected_uf2_guard_bytes": partitions["uf2_app_guard"][1] if partitions["uf2_app_guard"] else None,
        "zigbee_ota_ready": not reasons,
        "ota_image_generation_allowed": not reasons,
        "reasons": reasons,
        "partitions": {
            name: {"offset": value[0], "size": value[1]} if value else None
            for name, value in partitions.items()
        },
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("devicetree", type=Path)
    parser.add_argument("prj_conf", type=Path)
    parser.add_argument(
        "--require-ready", action="store_true",
        help="fail unless the board meets ALL necessary (not sufficient) OTA prerequisites",
    )
    args = parser.parse_args()
    report = inspect(
        args.devicetree.read_text(encoding="utf-8"),
        args.prj_conf.read_text(encoding="utf-8"),
    )
    print(json.dumps(report, indent=2, ensure_ascii=False))
    if args.require_ready and not report["zigbee_ota_ready"]:
        print("BLOCKED: do not generate or publish an OTA upgrade image", flush=True)
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
