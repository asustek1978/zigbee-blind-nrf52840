#!/usr/bin/env python3
"""Passive Tuya WBR3/P3 serial capture and offline decode. NEVER transmits bytes.

Capture with one or two independent 3.3 V UART receivers (RX + common GND only).
Optional dependency for capture: python -m pip install pyserial
Decode and tests require Python standard library only.
"""
from __future__ import annotations

import argparse
import json
import queue
import re
import sys
import threading
import time
from datetime import datetime, timezone
from pathlib import Path

MAX_PAYLOAD = 128
DP_COMMANDS = {
    "classic": {0x06: "set DP", 0x07: "report DP"},
    "zigbee": {
        0x04: "module -> MCU DP",
        0x05: "MCU DP response",
        0x06: "MCU DP report",
        0x27: "MCU DP advertising",
        0x2A: "group DP",
        0x2C: "MCU DP report (no linkage)",
    },
}
DP_TYPES = {
    0x00: "raw", 0x01: "bool", 0x02: "value",
    0x03: "string", 0x04: "enum", 0x05: "bitmap",
}


def parse_dps(payload: bytes):
    """Return (DPs, error); do not present an incomplete DP list as valid."""
    parsed = []
    cursor = 0
    while cursor < len(payload):
        if len(payload) - cursor < 4:
            return None, "truncated DP header"
        dp_id, dp_type = payload[cursor], payload[cursor + 1]
        length = int.from_bytes(payload[cursor + 2:cursor + 4], "big")
        cursor += 4
        if length > len(payload) - cursor:
            return None, "truncated DP value"
        raw = payload[cursor:cursor + length]
        cursor += length
        dp = {"id": dp_id, "type": dp_type, "type_name": DP_TYPES.get(dp_type, "unknown"),
              "length": length, "hex": raw.hex(" ").upper()}
        if dp_type in (1, 4) and length == 1:
            dp["number"] = raw[0]
        elif dp_type == 2 and length == 4:
            dp["number"] = int.from_bytes(raw, "big")
        elif dp_type == 5 and length in (1, 2, 4):
            dp["number"] = int.from_bytes(raw, "big")
        parsed.append(dp)
    return parsed, None


class FrameDecoder:
    def __init__(self):
        self.buffer = bytearray()
        self.rejected = 0
        self.frames = 0

    def feed(self, data: bytes) -> list[dict]:
        self.buffer.extend(data)
        found = []
        while True:
            start = self.buffer.find(b"\x55\xaa")
            if start < 0:
                # Preserve a possible first header byte across chunks.
                self.buffer[:] = b"\x55" if self.buffer.endswith(b"\x55") else b""
                break
            if start:
                del self.buffer[:start]
            if len(self.buffer) < 3:
                break
            zigbee = self.buffer[2] == 2
            header_len = 8 if zigbee else 6
            if len(self.buffer) < header_len:
                break
            payload_len = int.from_bytes(self.buffer[header_len-2:header_len], "big")
            if payload_len > MAX_PAYLOAD:
                self.rejected += 1
                del self.buffer[0]
                continue
            total = header_len + payload_len + 1
            if len(self.buffer) < total:
                break
            packet = bytes(self.buffer[:total])
            if (sum(packet[:-1]) & 0xFF) != packet[-1]:
                self.rejected += 1
                del self.buffer[0]
                continue
            del self.buffer[:total]
            proto = "zigbee" if zigbee else "classic"
            command = packet[5] if zigbee else packet[3]
            frame = {
                "format": proto, "version": packet[2],
                "sequence": int.from_bytes(packet[3:5], "big") if zigbee else None,
                "command": command, "command_hex": f"0x{command:02X}",
                "command_name": DP_COMMANDS.get(proto, {}).get(command, "other"),
                "payload_hex": packet[header_len:-1].hex(" ").upper(),
                "raw_hex": packet.hex(" ").upper(),
                "length": payload_len,
            }
            if command in DP_COMMANDS[proto] and payload_len:
                dps, error = parse_dps(packet[header_len:-1])
                if error:
                    # A one-byte response can be an acknowledgement, NOT a DP.
                    if payload_len == 1 and command in (0x05, 0x06, 0x2C):
                        frame["ack_status"] = packet[header_len]
                    else:
                        frame["dp_parse_error"] = error
                else:
                    frame["dps"] = dps
            found.append(frame)
            self.frames += 1
        return found


def summarize(direction: str, frame: dict) -> str:
    dp_text = ""
    if frame.get("dps"):
        dp_text = " " + ", ".join(
            f"DP{dp['id']}:{dp['type_name']}={dp.get('number', dp['hex'])}"
            for dp in frame["dps"]
        )
    if "ack_status" in frame:
        dp_text = f" ACK={frame['ack_status']}"
    if "dp_parse_error" in frame:
        dp_text = f" DP_PARSE_ERROR={frame['dp_parse_error']}"
    return (f"{direction}: {frame['format']} cmd={frame['command_hex']} "
            f"seq={frame['sequence']} len={frame['length']}{dp_text}")


def parse_hex(value: str) -> bytes:
    """Parse hex-dump with spaces/commas/0x notation; never interpret as commands."""
    cleaned = re.sub(r"0[xX]", "", value)
    cleaned = re.sub(r"[\s,;:\-]", "", cleaned)
    if not cleaned or not re.fullmatch(r"[0-9A-Fa-f]+", cleaned) or len(cleaned) % 2:
        raise ValueError("Expected an even number of hexadecimal digits")
    return bytes.fromhex(cleaned)


def decode_file(path: Path) -> int:
    parsers = {}
    displayed = 0
    for lineno, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        if not line.strip():
            continue
        try:
            record = json.loads(line)
        except json.JSONDecodeError:
            raise ValueError(f"line {lineno}: not a JSONL record")
        if record.get("kind") != "raw":
            continue
        direction = record.get("direction", "unknown")
        parser = parsers.setdefault(direction, FrameDecoder())
        for frame in parser.feed(parse_hex(record["hex"])):
            print(f"{record.get('utc','')} {summarize(direction, frame)}")
            print("  RAW:", frame["raw_hex"])
            displayed += 1
    print(f"Decoded {displayed} frames from {path}")
    return displayed


def capture_port(label: str, port: str, baud: int, q: queue.Queue,
                 stop: threading.Event) -> None:
    try:
        import serial  # install pyserial ONLY for actual serial capture
        with serial.Serial(port=port, baudrate=baud, bytesize=8, parity="N",
                           stopbits=1, timeout=0.15, write_timeout=0) as ser:
            q.put({"kind": "info", "direction": label, "port": port})
            while not stop.is_set():
                data = ser.read(256)
                if data:
                    q.put({"kind": "raw", "direction": label,
                           "utc": datetime.now(timezone.utc).isoformat(timespec="milliseconds"),
                           "hex": data.hex(" ").upper()})
            # No write(), no serial transmit at any point in the program.
    except (ImportError, OSError, Exception) as exc:
        q.put({"kind": "error", "direction": label, "port": port, "error": str(exc)})
        stop.set()


def capture(args) -> int:
    ports = [("WBR3->P3", args.wbr3_tx), ("P3->WBR3", args.p3_tx)]
    ports = [(label, port) for label, port in ports if port]
    if not ports:
        raise ValueError("Provide --wbr3-tx and/or --p3-tx")
    if len({p for _, p in ports}) != len(ports):
        raise ValueError("Use separate receiver ports for the two TX signals")
    try:
        import serial  # noqa: F401
    except ImportError as exc:
        raise RuntimeError("Run: py -m pip install pyserial") from exc

    stop = threading.Event()
    q = queue.Queue()
    parsers = {label: FrameDecoder() for label, _ in ports}
    threads = [threading.Thread(target=capture_port,
                               args=(label, port, args.baud, q, stop), daemon=True)
               for label, port in ports]
    Path(args.out).parent.mkdir(parents=True, exist_ok=True)
    deadline = time.monotonic() + args.seconds if args.seconds > 0 else None
    error = False
    with Path(args.out).open("w", encoding="utf-8", buffering=1) as output:
        output.write(json.dumps({"kind": "session", "baud": args.baud,
                                 "ports": dict(ports),
                                 "mode": "RX-only, no bytes transmitted"}) + "\n")
        for thread in threads:
            thread.start()
        print("Listening in RECEIVE-ONLY mode; Ctrl+C to finish.")
        try:
            while not stop.is_set():
                if deadline is not None and time.monotonic() >= deadline:
                    break
                try:
                    record = q.get(timeout=0.2)
                except queue.Empty:
                    continue
                output.write(json.dumps(record, ensure_ascii=True) + "\n")
                if record["kind"] == "raw":
                    for frame in parsers[record["direction"]].feed(parse_hex(record["hex"])):
                        event = {"kind": "frame", "utc": record["utc"],
                                 "direction": record["direction"], **frame}
                        output.write(json.dumps(event, ensure_ascii=True) + "\n")
                        print(f'{event["utc"]} {summarize(event["direction"], event)}', flush=True)
                if record["kind"] == "error":
                    error = True
                    print(f'{record["direction"]} ERROR: {record["error"]}', file=sys.stderr)
        except KeyboardInterrupt:
            pass
        finally:
            stop.set()
            for thread in threads:
                thread.join(timeout=0.5)
            while not q.empty():
                record = q.get_nowait()
                output.write(json.dumps(record, ensure_ascii=True) + "\n")
            print(f"Capture saved to {args.out}")
    return 1 if error else 0


def main(argv=None):
    parser = argparse.ArgumentParser(description="Tuya MCU UART passive sniffer (never transmits)")
    sub = parser.add_subparsers(dest="mode", required=True)
    cap = sub.add_parser("capture", help="Capture real UART RX from one or two adapters")
    cap.add_argument("--wbr3-tx", metavar="COMx", help="Receiver RX wired to WBR3 TX")
    cap.add_argument("--p3-tx", metavar="COMx", help="Receiver RX wired to P3 TX")
    cap.add_argument("--baud", type=int, default=9600)
    cap.add_argument("--seconds", type=float, default=120)
    cap.add_argument("--out", default="tuya_p3_capture.jsonl")
    dec = sub.add_parser("decode", help="Read captured JSONL or paste a hex frame")
    exclusive = dec.add_mutually_exclusive_group(required=True)
    exclusive.add_argument("--file", type=Path)
    exclusive.add_argument("--hex", dest="hex_data")
    args = parser.parse_args(argv)
    try:
        if args.mode == "capture":
            return capture(args)
        if args.hex_data:
            parser_instance = FrameDecoder()
            frames = parser_instance.feed(parse_hex(args.hex_data))
            for frame in frames:
                print(summarize("manual", frame))
                print(json.dumps(frame, indent=2))
            return 0 if frames else 1
        return 0 if decode_file(args.file) else 1
    except (OSError, ValueError, RuntimeError) as exc:
        print(f"Error: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
