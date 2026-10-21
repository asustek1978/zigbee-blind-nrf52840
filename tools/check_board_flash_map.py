#!/usr/bin/env python3
"""Fail CI if our HW-954 application/data Flash boundaries drift.

These bounds match Adafruit nRF52 Bootloader *tag 0.6.0*, not a
physical-device bootloader dump; this is only a static consistency check.
"""
import argparse
import re
from pathlib import Path

# name, start, end exclusive
EXPECTED = (
    ("softdevice_partition", 0x00000, 0x26000),
    ("slot0_partition", 0x26000, 0xAD000),
    ("uf2_app_guard", 0xAD000, 0xED000),
    ("zboss_nvram", 0xED000, 0xF1000),
    ("zboss_product_config", 0xF1000, 0xF2000),
    ("storage_partition", 0xF2000, 0xF4000),
    ("uf2_bootloader_partition", 0xF4000, 0x100000),
)

# Each partition declaration has one reg property; comments cannot affect
# matches because the expected register data are always inside braces.
PARTITION = re.compile(
    r"(?P<name>[a-zA-Z][a-zA-Z0-9_]*):\s*partition@(?P<node>[0-9a-fA-F]+)"
    r"\s*\{(?P<body>[^{}]*)\}",
    flags=re.MULTILINE,
)
REG = re.compile(r"\breg\s*=\s*<\s*(0x[\da-fA-F]+)\s+(0x[\da-fA-F]+)\s*>\s*;")

def validate(text):
    found = []
    for match in PARTITION.finditer(text):
        reg = REG.search(match.group("body"))
        if not reg:
            raise ValueError(f"Missing reg for {match.group('name')}")
        start, size = (int(x, 16) for x in reg.groups())
        label = match.group("name")
        if start != int(match.group("node"), 16):
            raise ValueError(f"Partition offset mismatch: {label}")
        if start % 4096 or size % 4096 or size <= 0:
            raise ValueError(f"Partition not 4K aligned: {label}")
        found.append((label, start, start + size))

    expected = list(EXPECTED)
    if sorted(found, key=lambda x: x[1]) != expected:
        raise ValueError(
            "HW-954 layout diverges from vetted ranges.\n"
            f"Expected: {expected!r}\n"
            f"Found: {sorted(found, key=lambda x: x[1])!r}"
        )
    if sum(end - start for _, start, end in found) != 0x100000:
        raise ValueError("Flash areas do not cover complete nRF52840 1MiB")
    print("PASS: exact contiguous 4KiB-aligned HW-954 flash map")
    for label, start, end in found:
        print(f"  {label}: 0x{start:05x}..0x{end:05x}, {(end-start)//1024}KiB")
    print("STATIC ONLY: real HW-954 bootloader/UICR still requires verification")

if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("dts", type=Path)
    args = parser.parse_args()
    validate(args.dts.read_text(encoding="utf-8"))
