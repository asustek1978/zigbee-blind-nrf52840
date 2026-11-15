"""v42.0-42 static contract for guarded full-UARTE RX while motor runs.

Static contracts do NOT prove nRF peripheral register timing, line signal
quality, current consumption, motor behavior or hardware race freedom.
"""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]
FIELDS = {
    "hw_direct_active": "F15D",
    "hw_direct_enters": "F15E",
    "hw_direct_exits": "F15F",
    "hw_direct_bytes": "F160",
    "hw_direct_restarts": "F161",
    "hw_direct_max_timeouts": "F162",
    "hw_direct_prearm_timeouts": "F163",
    "hw_direct_last_switch_ms": "F164",
    "hw_direct_hw_errors": "F165",
    "hw_direct_empty_dma": "F166",
    "hw_direct_rxto_timeouts": "F167",
}

class TestConditionalHardwareRx(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.rx = (ROOT / "src/p3_soft_uart.c").read_text(encoding="utf-8")
        cls.h = (ROOT / "include/p3_soft_uart.h").read_text(encoding="utf-8")
        cls.main = (ROOT / "src/main.c").read_text(encoding="utf-8")
        cls.motor = (ROOT / "src/tuya_uart_hybrid.c").read_text(encoding="utf-8")
        cls.js = (ROOT / "zigbee2mqtt/external_converters/p3_nrf52840.js").read_text(encoding="utf-8")
        cls.mjs = (ROOT / "zigbee2mqtt/external_converters/p3_nrf52840.mjs").read_text(encoding="utf-8")

    def test_zcl_attributes_and_converter_stay_in_sync(self):
        self.assertEqual(self.js, self.mjs)
        self.assertIn("blind_sw_build_id[] = {7, '4', '2', '.', '0', '-', '4', '3'}", self.main)
        for name, attr in FIELDS.items():
            with self.subTest(name=name):
                macro = "P3_" + name.upper()
                self.assertIn(f"#define {macro} 0x{attr}U", self.main)
                self.assertIn(f"{macro}, &blind_ctx.{name}", self.main)
                self.assertIn(f"blind_ctx.{name} = p3_soft_uart_{name}();", self.main)
                self.assertIn(f"uint32_t p3_soft_uart_{name}(void)", self.h)
                self.assertIn(f"uint32_t p3_soft_uart_{name}(void)", self.rx)
                self.assertIn(f"{name}: 0x{attr},", self.js)
        self.assertIn("key: ['refresh_all_diagnostics']", self.js)
        self.assertIn("e.numeric('refresh_all_diagnostics', ea.STATE_GET)", self.js)
        ids = re.findall(r"#define P3_\w+ 0x(F[0-9A-F]{3})U", self.main)
        self.assertEqual(len(ids), len(set(ids)))

    def test_hardware_only_owned_by_rx_thread(self):
        self.assertIn("static void p3_hw_rx_maybe_enter(void)", self.rx)
        self.assertIn("p3_hw_rx_maybe_enter();", self.rx)
        self.assertIn("void p3_soft_uart_request_motor_rx(void)", self.h)
        self.assertIn("p3_soft_uart_request_motor_rx();", self.motor)
        self.assertIn("p3_soft_uart_direct_rx_active()", self.motor)
        self.assertNotIn("NRF_UARTE0", self.motor)
        self.assertIn("NRF_UARTE1->TASKS_STARTTX = 1U", self.motor)

    def test_bounded_hardware_mode_and_fresh_stop(self):
        self.assertIn("#define P3_HW_RX_STOP_GUARD_MS 1500U", self.rx)
        self.assertIn("#define P3_HW_RX_MAX_MS 120000U", self.rx)
        self.assertIn("p3_hw_rx_stop_guard_due(now, stop_ms, P3_HW_RX_STOP_GUARD_MS)", self.rx)
        self.assertIn("atomic_inc(&hw_direct_max_timeouts)", self.rx)
        self.assertIn("hw_direct_session_start_ms", self.rx)
        self.assertIn("hw_direct_motion_lockout", self.rx)
        self.assertIn("hw_direct_saw_byte", self.rx)
        self.assertIn("atomic_inc(&hw_direct_restarts)", self.rx)
        self.assertIn("atomic_inc(&hw_direct_hw_errors)", self.rx)
        self.assertIn("atomic_inc(&hw_direct_empty_dma)", self.rx)
        self.assertIn("atomic_inc(&hw_direct_rxto_timeouts)", self.rx)
        self.assertIn("atomic_inc(&hw_direct_exits)", self.rx)
        self.assertIn("tuya_foreach_report_dp(frame, p3_capture_fresh_motion, &fresh_motion)", self.motor)
        self.assertIn("fresh_motion.present", self.motor)
        self.assertIn("if (accepted && motor_state.motion_valid)", self.motor)

    def test_motor_tx_remains_bounded_and_calibration_disabled(self):
        self.assertIn("const uint32_t deadline_ms = frame.is_stop ? 20U : 80U;", self.motor)
        self.assertIn("p3_soft_uart_note_prearm_timeout();", self.motor)
        self.assertIn("k_msgq_put(&p3_tx_queue, &msg, K_NO_WAIT)", self.motor)
        self.assertIn("return false; /* Calibration remains disabled", self.motor)
        self.assertNotIn("uart_poll_out(", self.motor)
        self.assertIn("#define P3_FIRST_SAMPLE_US 156U", self.rx)
        self.assertIn("#define P3_BIT_US 104U", self.rx)
        self.assertIn("#define P3_UART_IDLE_MS 15U", self.rx)
        self.assertIn("p3_uart_begin_dma();", self.rx)

if __name__ == "__main__":
    unittest.main()
