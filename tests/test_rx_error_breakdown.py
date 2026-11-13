"""Static regression checks for low-power v42.0-39 RX error instrumentation.

These checks do not substitute for compilation or bench power measurement.
"""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]


class RxBreakdownTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.rx = (ROOT / "src/p3_soft_uart.c").read_text(encoding="utf-8")
        cls.main = (ROOT / "src/main.c").read_text(encoding="utf-8")
        cls.converter = (
            ROOT / "zigbee2mqtt/external_converters/p3_nrf52840.mjs"
        ).read_text(encoding="utf-8")

    def test_power_and_response_constraints_preserved(self):
        self.assertIn("#define P3_UART_IDLE_MS 15U", self.rx)
        self.assertIn("NRF_UARTE0->TASKS_STARTRX = 1U", self.rx)
        self.assertIn("NRF_UARTE0->TASKS_STOPRX = 1U", self.rx)
        self.assertIn("p3_uart_disable();", self.rx)
        self.assertIn("#define P3_ZIGBEE_POLL_INTERVAL_MS 1000U", self.main)
        self.assertIn("zb_zdo_pim_set_long_poll_interval(P3_ZIGBEE_POLL_INTERVAL_MS)", self.main)
        self.assertNotIn("k_sleep(K_SECONDS(30))", self.main)

    def test_attributes_registered_and_exposed(self):
        attrs = {
            "RX_FRAMING_ERRORS": ("rx_framing_errors", "F13C"),
            "RX_RING_OVERFLOWS": ("rx_ring_overflows", "F13D"),
            "RX_SOFTWARE_STARTS": ("rx_software_starts", "F13E"),
            "RX_SOFTWARE_NONHEADER": ("rx_software_nonheader", "F13F"),
            "UART_LAST_ERRORSRC": ("uart_last_errorsrc", "F140"),
        }
        for symbol, (key, value) in attrs.items():
            with self.subTest(symbol=symbol):
                self.assertRegex(
                    self.main,
                    rf"#define P3_{symbol} 0x{value}U",
                )
                self.assertIn(f"P3_{symbol}, &blind_ctx.{key}", self.main)
                self.assertRegex(self.converter, rf"{key}: 0x{value},")
        self.assertEqual(len(attrs), len({hex_id for _, hex_id in attrs.values()}))

    def test_existing_uart_diagnostics_still_available(self):
        self.assertIn("p3_soft_uart_framing_errors()", self.main)
        self.assertIn("p3_soft_uart_overflows()", self.main)
        self.assertIn("p3_soft_uart_hardware_errors()", self.main)
        self.assertIn("p3_soft_uart_handoff_errors()", self.main)
        self.assertIn("p3_soft_uart_software_nonheader_bytes()", self.main)
        self.assertIn("p3_soft_uart_last_hw_errorsrc()", self.main)
        self.assertIn("uart_hardware_errors: 0xF12F", self.converter)
        self.assertIn("uart_handoff_errors: 0xF12A", self.converter)
        self.assertIn("tuya_frames_rejected: 0xF134", self.converter)

    def test_no_runtime_behavior_change_in_diagnostic_branch(self):
        # Confirm diagnostic counters are read-only and the new ISR operation
        # touches only the no-preamble case, not 0x55 -> UARTE handoff.
        self.assertRegex(
            self.rx,
            r"if \(shift_byte == 0x55U\) \{\s*p3_uart_begin_dma\(\);\s*return;\s*\}\s*"
            r"/\* Count completed software bytes[\s\S]*?\*/\s*atomic_inc\(&software_nonheader_bytes\);",
        )
        self.assertIn("atomic_set(&uart_hw_last_source, (atomic_val_t)cause)", self.rx)


if __name__ == "__main__":
    unittest.main()
