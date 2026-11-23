"""Static v42.0-46 contract for passive command outcome ZCL telemetry."""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]
FIELDS = {
    "cmd_queued": "F179",
    "cmd_tx_ok": "F17A",
    "cmd_tx_failed": "F17B",
    "cmd_mcu_report": "F17C",
    "cmd_position_reported": "F17D",
    "cmd_stop_reported": "F17E",
    "cmd_timeout_no_report": "F17F",
    "cmd_timeout_no_target": "F180",
    "cmd_superseded": "F181",
    "cmd_pending": "F182",
    "cmd_last_kind": "F183",
    "cmd_last_target": "F184",
    "cmd_last_result": "F185",
    "cmd_last_report_delay_ms": "F186",
}

class TestV42046(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        def read(path): return (ROOT / path).read_text(encoding="utf-8")
        cls.main = read("src/main.c")
        cls.rx = read("src/p3_soft_uart.c")
        cls.uart = read("src/tuya_uart_hybrid.c")
        cls.hdr = read("include/p3_command_monitor.h")
        cls.js = read("zigbee2mqtt/external_converters/p3_nrf52840.js")
        cls.mjs = read("zigbee2mqtt/external_converters/p3_nrf52840.mjs")
        cls.ci = read(".github/workflows/zigbee-build.yml")

    def test_zcl_mapping_and_build_id(self):
        self.assertEqual(self.js, self.mjs)
        self.assertIn("blind_sw_build_id[] = {7, '4', '2', '.', '0', '-', '4', '9'}", self.main)
        ids = re.findall(r"#define P3_\w+ 0x(F[0-9A-F]{3})U", self.main)
        self.assertEqual(len(ids), len(set(ids)))
        for name, attr in FIELDS.items():
            with self.subTest(attr=name):
                self.assertIn(f"#define P3_{name.upper()} 0x{attr}U", self.main)
                self.assertIn(f"{name}: 0x{attr},", self.js)
                self.assertIn(f"blind_ctx.{name}", self.main)

    def test_no_motor_side_effects(self):
        self.assertIn("P3_CMD_MONITOR_TIMEOUT_MS 180000U", self.hdr)
        self.assertIn("P3_CMD_TARGET_TOLERANCE_PERCENT 1U", self.hdr)
        self.assertIn("p3_cmd_monitor_expire(", self.hdr)
        self.assertIn("k_work_reschedule(&p3_cmd_monitor_timeout,", self.uart)
        self.assertIn("p3_cmd_monitor_tx_completed(", self.uart)
        self.assertIn("p3_cmd_monitor_mcu_report(", self.uart)
        self.assertIn("p3_cmd_monitor_target_match(", self.hdr)
        self.assertIn("tuya_foreach_report_dp(frame, p3_command_report_capture,", self.uart)
        self.assertIn("k_msgq_purge(&p3_tx_queue);", self.uart)
        self.assertIn("p3_soft_uart_request_motor_rx();", self.uart)
        self.assertIn("P3_HW_RX_MAX_MS 120000U", self.rx)
        # no motor command issued from the monitoring worker
        worker = self.uart.split("static void p3_command_monitor_timeout_work")[1]
        worker = worker.split("K_WORK_DELAYABLE_DEFINE", 1)[0]
        self.assertNotIn("tuya_uart_submit_", worker)
        self.assertNotIn("queue_motor_frame", worker)
        self.assertNotIn("p3_soft_uart_request_motor_rx", worker)

    def test_report_is_not_ack(self):
        self.assertIn("NOT a protocol ACK", self.hdr)
        self.assertIn("command_report.dp1_present", self.uart)
        self.assertIn("command_report.dp3_present", self.uart)
        self.assertIn("changes.position", self.uart)
        self.assertIn("cmd_mcu_report", self.js)
        self.assertIn("NOT proof of execution", self.js)

if __name__ == "__main__":
    unittest.main()
