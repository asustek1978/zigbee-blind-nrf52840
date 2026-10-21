#!/usr/bin/env python3
"""Inspect diagnostic UF2 flash ranges. Success is NOT approval to flash."""
import argparse
import struct
from pathlib import Path

MAGIC = (0x0A324655, 0x9E5D5157, 0x0AB16F30)
FAMILY = 0xADA52840
# Adafruit nRF52 Bootloader 0.6.0: USER_FLASH_END=0xAD000.
# App region is intentionally smaller than the 0xEC000 flash/data boundary.
APP_START, APP_END = 0x26000, 0xAD000
RAM_START, RAM_END = 0x20000000, 0x20040000


def validate(path: Path) -> None:
    contents = path.read_bytes()
    if not contents or len(contents) % 512:
        raise ValueError("Invalid UF2 file size")
    total_blocks = len(contents) // 512
    seen_blocks, seen_addresses = set(), set()
    minimum, maximum = APP_END, APP_START
    first_payload = None
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
        if address == APP_START:
            first_payload = block[32:32 + size]
        minimum, maximum = min(minimum, address), max(maximum, address + size)
        seen_blocks.add(number)
        seen_addresses.add(address)

    if minimum != APP_START or first_payload is None:
        raise ValueError("Missing application vector table at 0x26000")
    initial_sp, reset_vector = struct.unpack_from("<II", first_payload)
    if not (RAM_START < initial_sp <= RAM_END and initial_sp % 8 == 0):
        raise ValueError(f"Invalid initial Cortex-M stack pointer 0x{initial_sp:08x}")
    reset_pc = reset_vector & ~1
    if not (reset_vector & 1 and APP_START <= reset_pc < maximum):
        raise ValueError(f"Invalid Thumb reset vector 0x{reset_vector:08x}")

    print(f"PASS: {total_blocks} UF2 blocks, family 0x{FAMILY:08x}")
    print(f"Vector table: SP 0x{initial_sp:08x}, Reset 0x{reset_vector:08x}")
    print(f"Application image: 0x{minimum:08x}..0x{maximum:08x} (end exclusive)")
    print(f"Allowed: 0x{APP_START:08x}..0x{APP_END:08x}")
    print("Adafruit UF2 v0.6.0 application write limit 0x000ad000 verified.")
    print("DIAGNOSTIC ONLY: physical bootloader compatibility not established.")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("uf2", type=Path)
    validate(parser.parse_args().uf2)
