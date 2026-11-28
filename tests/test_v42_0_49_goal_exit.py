"""Source regression for target+silence UARTE exit and read-only ZCL counter.

These checks cannot prove physical motor behavior or missed MCU wakeups.
"""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]

class TestV42049GoalExit(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        def read(p): return (ROOT / p).read_text(encoding="utf-8")
        cls.rx = read("src/p3_soft_uart.c")
        cls.main = read("src/main.c")
        cls.h = read("include/p3_session_diag.h")
        cls.c = read("src/p3_session_diag.c")
        cls.policy = read("include/p3_goal_exit.h")
        cls.z2m = read("zigbee2mqtt/external_converters/p3_nrf52840.mjs")

    def test_goal_silence_is_bounded_and_checks_validated_frame(self):
        self.assertIn("#define P3_HW_RX_GOAL_SILENCE_MS 3000U", self.policy)
        self.assertIn("p3_session_diag_goal_ready()", self.rx)
        self.assertIn("ring_buf_size_get(&p3_soft_ring) == 0U", self.rx)
        self.assertIn("NRF_UARTE0->EVENTS_ENDRX == 0U", self.rx)
        self.assertIn("P3_SESSION_EXIT_GOAL_SILENCE", self.rx)
        self.assertIn("current.valid_frames > 0U", self.c)
        self.assertIn("current.last_dp3 == current.target", self.c)
        self.assertIn("current.goal_seen != 0U", self.c)

    def test_original_safety_exits_and_motor_commands_untouched(self):
        self.assertIn("#define P3_HW_RX_MAX_MS 120000U", self.rx)
        self.assertIn("#define P3_HW_RX_STOP_GUARD_MS 1500U", self.rx)
        self.assertIn("p3_hw_rx_timeout_due(now, anchor_ms, P3_HW_RX_MAX_MS)", self.rx)
        self.assertIn("p3_hw_rx_stop_guard_due(now, stop_ms, P3_HW_RX_STOP_GUARD_MS)", self.rx)
        self.assertIn("p3_ppi_arm();", self.rx)
        self.assertNotIn("tuya_uart_submit_", self.c)
        self.assertNotIn("TASKS_STARTTX", self.c)

    def test_new_reason_and_z2m_mapping(self):
        self.assertIn("P3_SESSION_EXIT_GOAL_SILENCE = 5", self.h)
        self.assertIn("#define P3_SESSION_GOAL_SILENCE_EXITS 0xF199U", self.main)
        self.assertIn("blind_ctx.session_goal_silence_exits = session_stats.goal_silence_exits;", self.main)
        self.assertIn("session_goal_silence_exits: 0xF199", self.z2m)
        self.assertIn("5=fresh DP3 target + 3s silence", self.z2m)
        self.assertIn("'4', '2', '.', '0', '-', '5', '0'", self.main)

if __name__ == "__main__":
    unittest.main()
