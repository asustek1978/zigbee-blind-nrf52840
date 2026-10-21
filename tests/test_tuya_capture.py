"""Pure stdlib unit tests for passive Tuya capture decoder."""
import json
import tempfile
import unittest
from pathlib import Path

from tuya_capture import FrameDecoder, parse_dps, parse_hex, decode_file, main


def make_frame(proto, command, data=b"", sequence=0x1234):
    if proto == "zigbee":
        buf = bytearray((0x55, 0xAA, 2)) + sequence.to_bytes(2, "big") + bytes((command,))
    else:
        buf = bytearray((0x55, 0xAA, 0, command))
    buf.extend(len(data).to_bytes(2, "big"))
    buf.extend(data)
    buf.append(sum(buf) & 0xFF)
    return bytes(buf)


class TestTuyaCapture(unittest.TestCase):
    def test_zigbee_receive_dp(self):
        dp = bytes((1, 4, 0, 1, 0))
        packet = make_frame("zigbee", 0x04, dp)
        parser = FrameDecoder()
        self.assertFalse(parser.feed(packet[:4]))
        frames = parser.feed(packet[4:])
        self.assertEqual(len(frames), 1)
        self.assertEqual(frames[0]["sequence"], 0x1234)
        self.assertEqual(frames[0]["dps"][0]["id"], 1)
        self.assertEqual(frames[0]["dps"][0]["number"], 0)
        self.assertEqual(frames[0]["raw_hex"], packet.hex(" ").upper())

    def test_classic_control_command(self):
        dp = bytes((1, 4, 0, 1, 2))
        f = FrameDecoder().feed(make_frame("classic", 0x06, dp))
        self.assertEqual(f[0]["dps"][0]["number"], 2)

    def test_zigbee_other_dp_cmds(self):
        dp = bytes((3, 2, 0, 4, 0, 0, 0, 57))
        for command in (0x05, 0x06, 0x27, 0x2A, 0x2C):
            with self.subTest(command=command):
                f = FrameDecoder().feed(make_frame("zigbee", command, dp))
                self.assertEqual(f[0]["dps"][0]["number"], 57)

    def test_ack_not_misreported_as_dp(self):
        f = FrameDecoder().feed(make_frame("zigbee", 0x06, b"\x01"))
        self.assertEqual(f[0]["ack_status"], 1)
        self.assertNotIn("dps", f[0])

    def test_multi_dp(self):
        dp = bytes((1, 4, 0, 1, 0, 3, 2, 0, 4, 0, 0, 0, 64))
        frames = FrameDecoder().feed(make_frame("zigbee", 0x04, dp))
        self.assertEqual([x["number"] for x in frames[0]["dps"]], [0, 64])

    def test_bad_checksum_recovery(self):
        valid = make_frame("zigbee", 0x04, b"\x01\x01\x00\x01\x01")
        broken = valid[:-1] + bytes((valid[-1] ^ 0x01,))
        parser = FrameDecoder()
        self.assertEqual(len(parser.feed(b"garbage" + broken + valid)), 1)
        self.assertGreater(parser.rejected, 0)

    def test_oversized_header_and_resync(self):
        invalid = bytes((0x55, 0xAA, 0, 6, 0xFF, 0xFF))
        packet = make_frame("classic", 7, b"\x03\x01\x00\x01\x01")
        p = FrameDecoder()
        self.assertEqual(len(p.feed(invalid + packet)), 1)
        self.assertEqual(p.rejected, 1)

    def test_partial_or_invalid_dp(self):
        self.assertEqual(parse_dps(b"\x01\x02\x00")[1], "truncated DP header")
        self.assertEqual(parse_dps(b"\x01\x02\x00\x04\x01")[1], "truncated DP value")

    def test_hex_and_cli(self):
        packet = make_frame("classic", 7, b"\x01\x04\x00\x01\x01")
        self.assertEqual(parse_hex("0x55, 0xAA"), b"\x55\xaa")
        self.assertEqual(main(["decode", "--hex", packet.hex(" ")]), 0)
        self.assertEqual(main(["decode", "--hex", "ZZ"]), 2)

    def test_offline_capture_file(self):
        p1 = make_frame("classic", 7, b"\x03\x02\x00\x04\x00\x00\x00\x32")
        p2 = make_frame("zigbee", 4, b"\x01\x04\x00\x01\x00")
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "capture.jsonl"
            with path.open("w", encoding="utf-8") as f:
                for direction, frame in (("P3->WBR3", p1), ("WBR3->P3", p2)):
                    for part in (frame[:5], frame[5:]):
                        f.write(json.dumps({"kind": "raw", "utc": "2026-10-21T00:00:00Z",
                                            "direction": direction,
                                            "hex": part.hex(" ")}) + "\n")
            self.assertEqual(decode_file(path), 2)


if __name__ == "__main__":
    unittest.main()
