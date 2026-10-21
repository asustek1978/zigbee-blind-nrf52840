#!/usr/bin/env python3
"""Inspect diagnostic UF2 flash ranges. Success is NOT approval to flash."""
import argparse
import struct
from pathlib import Path

MAGIC = (0x0A324655, 0x9E5D5157, 0x0AB16F30)
FAMILY = 0xADA52840
APP_START, APP_END = 0x26000, 0xEC000  # Preliminary board Devicetree map


def validate(path: Path) -> None:
    contents = path.read_bytes()
    if not contents or len(contents) % 512:
        raise ValueError("Invalid UF2 file size")
    total_blocks = len(contents) // 512
    seen_blocks, seen_addresses = set(), set()
    minimum, maximum = APP_END, APP_START
    for index in range(total_blocks):
        block = contents[index * 512:(index + 1) * 512]
        magic0, magic1, flags, address, size, number, total, family = struct.unpack_from(
            "<8I", block
        )
        last_magic = struct.unpack_from("<I", block, 508)[0]
        if (magic0, magic1, last_magic) != MAGIC:
            raise ValueError(f"Invalid UF2 magic at block {index}")
        if not flags & 0x2000 or flags & 0x1 or family != FAMILY:
            raise ValueError(f"Unexpected UF2 flags/family at block {index}")
        if size != 256 or address % 256:
            raise ValueError(f"Invalid UF2 payload size/alignment at block {index}")
        if total != total_blocks or number in seen_blocks or number >= total:
            raise ValueError(f"Invalid UF2 block sequence at block {index}")
        if address in seen_addresses:
            raise ValueError(f"Duplicate UF2 target address: 0x{address:x}")
        if not (APP_START <= address and address + size <= APP_END):
            raise ValueError(f"Block {index} targets forbidden flash region at 0x{address:x}")
        minimum, maximum = min(minimum, address), max(maximum, address + size)
        seen_blocks.add(number)
        seen_addresses.add(address)

    print(f"PASS: {total_blocks} UF2 blocks, family 0x{FAMILY:08x}")
    print(f"Application image: 0x{minimum:08x}..0x{maximum:08x} (end exclusive)")
    print(f"Allowed: 0x{APP_START:08x}..0x{APP_END:08x}")
    print("DIAGNOSTIC ONLY: bootloader compatibility not verified; router template")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("uf2", type=Path)
    validate(parser.parse_args().uf2)
