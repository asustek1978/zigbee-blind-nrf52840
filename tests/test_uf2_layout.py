"""Check UF2 addresses, Cortex-M vector and Adafruit bootloader 0.6.0 bound."""
from __future__ import annotations

import io
import struct
import tempfile
import unittest
from contextlib import redirect_stdout
from pathlib import Path

from check_uf2_layout import validate, FAMILY, MAGIC, APP_END, APP_START


def block(number, total, address, sp=0x20040000, reset=0x26041):
    payload = bytearray(256)
    struct.pack_into("<II", payload, 0, sp, reset)
    data = bytearray(512)
    struct.pack_into("<8I", data, 0,
                     MAGIC[0], MAGIC[1], 0x2000,
                     address, len(payload), number, total, FAMILY)
    data[32:32 + len(payload)] = payload
    struct.pack_into("<I", data, 508, MAGIC[2])
    return bytes(data)


def run_validate(data):
    with tempfile.TemporaryDirectory() as tmp:
        filename = Path(tmp) / "sample.uf2"
        filename.write_bytes(data)
        with redirect_stdout(io.StringIO()) as out:
            validate(filename)
            return out.getvalue()


class LayoutTest(unittest.TestCase):
    def test_valid_application_and_vectors(self):
        report = run_validate(block(0, 2, APP_START)
                              + block(1, 2, APP_START + 256))
        self.assertIn("PASS: 2 UF2 blocks", report)
        self.assertIn("Vector table:", report)

    def test_rejects_reserved_and_bootloader(self):
        for forbidden in (APP_END, 0xEC000, 0xF4000):
            with self.subTest(address=hex(forbidden)):
                with self.assertRaisesRegex(ValueError, "forbidden flash region"):
                    run_validate(block(0, 2, APP_START) + block(1, 2, forbidden))

    def test_rejects_missing_first_vector(self):
        with self.assertRaisesRegex(ValueError, "Missing application vector"):
            run_validate(block(0, 1, APP_START + 256))

    def test_rejects_non_ram_stack(self):
        with self.assertRaisesRegex(ValueError, "stack pointer"):
            run_validate(block(0, 1, APP_START, sp=0x10000000))

    def test_rejects_non_thumb_or_external_reset(self):
        for reset in (0x26040, 0xAD001):
            with self.subTest(reset=hex(reset)):
                with self.assertRaisesRegex(ValueError, "reset vector"):
                    run_validate(block(0, 1, APP_START, reset=reset))

    def test_rejects_corrupt_magic_and_duplicate_blocks(self):
        wrong = bytearray(block(0, 1, APP_START))
        wrong[0] ^= 1
        with self.assertRaisesRegex(ValueError, "UF2 magic"):
            run_validate(wrong)
        with self.assertRaisesRegex(ValueError, "block sequence"):
            run_validate(block(0, 2, APP_START) + block(0, 2, APP_START + 256))


if __name__ == "__main__":
    unittest.main()
