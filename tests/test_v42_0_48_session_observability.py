"""Static integration checks for v42.0-48 passive session diagnostics.

The host state-machine test separately verifies behavioral accounting.
These tests intentionally do NOT claim the physical build is validated.
"""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
RX = (ROOT / "src/p3_soft_uart.c").read_text()
APP = (ROOT / "src/main.c").read_text()
TUYA = (ROOT / "src/tuya_uart_hybrid.c").read_text()
LEDGER = (ROOT / "src/p3_session_diag.c").read_text()
CONVERTER = (ROOT / "zigbee2mqtt/external_converters/p3_nrf52840.mjs").read_text()
CMAKE = (ROOT / "CMakeLists.txt").read_text()


class SessionObservabilityContract(unittest.TestCase):
    def test_cmakelists_and_idle_loop_integration(self):
        self.assertIn("src/p3_session_diag.c", CMAKE)
        self.assertIn("p3_session_diag_begin();", RX)
        self.assertIn("p3_session_diag_note_dma_bytes(count);", RX)
        self.assertIn("p3_session_diag_end((enum p3_session_exit_reason)cause,", RX)
        self.assertIn("p3_session_diag_publish_pending();", RX)
        self.assertIn("p3_session_diag_note_valid_frame();", APP)

    def test_only_accepted_fresh_mcu_reports_count(self):
        self.assertIn("if (accepted) {\n        /* Only freshly validated DP1/DP3", TUYA)
        self.assertIn("p3_session_diag_note_mcu_report(", TUYA)
        self.assertIn("command_report.dp1_present && command_report.dp1 == 1U", TUYA)
        self.assertIn("command_report.dp3_present, command_report.dp3", TUYA)
        self.assertIn("p3_session_diag_note_tx_target(frame.monitor_target);", TUYA)
        self.assertIn("if (completed) {\n            /* Track only a successfully transmitted", TUYA)
        self.assertIn("s->target == dp3", LEDGER)

    def test_exit_policies_not_changed(self):
        self.assertIn("#define P3_HW_RX_STOP_GUARD_MS 1500U", RX)
        self.assertIn("#define P3_HW_RX_MAX_MS 120000U", RX)
        self.assertIn("if (p3_hw_rx_timeout_due(", RX)
        self.assertIn("if (p3_hw_rx_stop_guard_due(", RX)
        self.assertIn("p3_session_diag_get(&session_stats);", APP)
        self.assertNotIn("tuya_uart_submit_action(", LEDGER)
        self.assertNotIn("TASKS_STARTTX", LEDGER)
        self.assertNotIn("p3_soft_uart_request_motor_rx(", LEDGER)

    def test_z2m_fields_match_basic_attribute_ids(self):
        fields = [
            "session_last_id", "session_last_bytes",
            "session_last_valid_frames", "session_last_stop",
            "session_last_goal", "session_last_target",
            "session_last_dp3", "session_last_silence_ms",
            "session_last_reason", "session_active_id",
            "session_total", "session_stop_guard_exits",
            "session_watchdog_exits", "session_watchdog_goal_no_stop",
        ]
        for i, field in enumerate(fields):
            with self.subTest(field=field):
                self.assertIn(
                    f"{field}: 0x{0xF18B + i:X}", CONVERTER)
                self.assertIn(f"blind_ctx.{field}", APP)


if __name__ == "__main__":
    unittest.main()
