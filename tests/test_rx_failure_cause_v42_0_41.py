"""v42.0-41 regression contract: diagnostic-only UART failure instrumentation.

Static checks, NOT proof of hardware timing, energy usage or motor behavior.
"""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]

FIELDS = {
    "rx_bad_stop_gpio_last_ms": "F14E",
    "rx_bad_stop_chained_last_ms": "F14F",
    "rx_handoff_unprepared": "F150",
    "rx_handoff_rxto_timeout": "F151",
    "rx_handoff_amount_oversize": "F152",
    "rx_handoff_no_rxstarted": "F153",
    "rx_handoff_zero_amount": "F154",
    "rx_handoff_last_ms": "F155",
    "rx_handoff_last_reason": "F156",
    "rx_hw_framing_events": "F157",
    "rx_hw_overrun_events": "F158",
    "rx_hw_break_events": "F159",
    "rx_hw_parity_events": "F15A",
    "rx_hw_last_ms": "F15B",
    "rx_hw_last_amount": "F15C",
}


class TestRxFailureCauses(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.rx = (ROOT / "src/p3_soft_uart.c").read_text(encoding="utf-8")
        cls.h = (ROOT / "include/p3_soft_uart.h").read_text(encoding="utf-8")
        cls.main = (ROOT / "src/main.c").read_text(encoding="utf-8")
        cls.js = (ROOT / "zigbee2mqtt/external_converters/p3_nrf52840.js").read_text(encoding="utf-8")
        cls.mjs = (ROOT / "zigbee2mqtt/external_converters/p3_nrf52840.mjs").read_text(encoding="utf-8")

    def test_field_mapping_complete_and_unique(self):
        self.assertEqual(self.js, self.mjs)
        for field, attr in FIELDS.items():
            macro = "P3_" + field.upper()
            name = "p3_soft_uart_" + field.removeprefix("rx_")
            with self.subTest(field=field):
                self.assertIn(f"#define {macro} 0x{attr}U", self.main)
                self.assertIn(f"{macro}, &blind_ctx.{field}", self.main)
                self.assertIn(f"blind_ctx.{field} = {name}();", self.main)
                self.assertIn(f"uint32_t {name}(void)", self.h)
                self.assertIn(f"uint32_t {name}(void)", self.rx)
                self.assertIn(f"{field}: 0x{attr},", self.js)
        numbers = re.findall(r"#define P3_\w+ 0x(F[0-9A-F]{3})U", self.main)
        self.assertEqual(len(numbers), len(set(numbers)))

    def test_handoff_failure_partition(self):
        self.assertEqual(self.rx.count("atomic_inc(&uart_handoff_errors);"), 1)
        self.assertIn("p3_note_handoff_error(&rx_handoff_unprepared, 1U);", self.rx)
        self.assertIn("p3_note_handoff_error(&rx_handoff_rxto_timeout, 2U);", self.rx)
        self.assertIn("p3_note_handoff_error(&rx_handoff_amount_oversize, 3U);", self.rx)
        self.assertIn("p3_note_handoff_error(&rx_handoff_no_rxstarted, 4U);", self.rx)
        self.assertIn("p3_note_handoff_error(&rx_handoff_zero_amount, 5U);", self.rx)

    def test_hardware_error_bits_are_only_counted_on_error(self):
        error_path = self.rx.split("if (NRF_UARTE0->EVENTS_ERROR != 0U) {", 1)[1].split(
            "const bool buffer_full", 1
        )[0]
        for bit, field in [(1, "overrun"), (2, "parity"), (4, "framing"), (8, "break")]:
            self.assertIn(f"if (cause & 0x{bit:02X}U) atomic_inc(&rx_hw_{field}_events);", error_path)
        self.assertIn("atomic_set(&rx_hw_last_ms", error_path)
        self.assertIn("atomic_set(&rx_hw_last_amount", error_path)

    def test_legacy_uart_first_byte_and_power_settings_preserved(self):
        for invariant in (
            "#define P3_FIRST_SAMPLE_US 156U",
            "#define P3_BIT_US 104U",
            "#define P3_UART_IDLE_MS 15U",
            "NRF_UARTE0->TASKS_STARTRX = 1U",
            "NRF_UARTE0->TASKS_STOPRX = 1U",
            "rx_phase = P3_RX_NEXT_START;",
        ):
            self.assertIn(invariant, self.rx)
        self.assertIn("#define P3_ZIGBEE_POLL_INTERVAL_MS 1000U", self.main)
        self.assertIn("blind_sw_build_id[] = {7, '4', '2', '.', '0', '-', '5', '0'}", self.main)
        self.assertIn("result.battery = battery;", self.js)

    def test_existing_bad_stop_counters_preserved(self):
        self.assertIn("atomic_inc(&framing_errors);", self.rx)
        self.assertIn("atomic_inc(&rx_bad_stop_gpio);", self.rx)
        self.assertIn("atomic_inc(&rx_bad_stop_chained);", self.rx)
        self.assertIn("atomic_set(&rx_last_bad_stop_ms, error_ms);", self.rx)
        self.assertIn("atomic_set(&rx_bad_stop_gpio_last_ms, error_ms);", self.rx)
        self.assertIn("atomic_set(&rx_bad_stop_chained_last_ms, error_ms);", self.rx)


if __name__ == "__main__":
    unittest.main()
