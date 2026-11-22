"""v42.0-47 regression: invalid software STOP must release TIMER3 and
return to GPIO low-power edge capture; valid-byte paths must be preserved.

Source checks do not establish interrupt race freedom, pulse fidelity,
physical motor behaviour or power draw. Verify on HW-954 before deployment.
"""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]


class TestV42047BadStopRecovery(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.rx = (ROOT / "src/p3_soft_uart.c").read_text(encoding="utf-8")
        cls.main = (ROOT / "src/main.c").read_text(encoding="utf-8")
        cls.ci = (ROOT / ".github/workflows/zigbee-build.yml").read_text(
            encoding="utf-8")

    def test_firmware_id_and_selected_baseline(self):
        self.assertIn(
            "blind_sw_build_id[] = {7, '4', '2', '.', '0', '-', '4', '8'}",
            self.main)
        self.assertIn("#define P3_FIRST_SAMPLE_US 156U", self.rx)
        self.assertIn("#define P3_BIT_US 104U", self.rx)
        self.assertIn("#define P3_UART_IDLE_MS 15U", self.rx)
        self.assertIn("#define P3_HW_RX_MAX_MS 120000U", self.rx)
        self.assertIn("#define P3_HW_RX_STOP_GUARD_MS 1500U", self.rx)
        self.assertIn("experiment/v42-0-47-bad-stop-gpio-recovery", self.ci)

    def test_invalid_stop_exits_before_chaining(self):
        stop = self.rx.split("if (rx_phase == P3_RX_STOP) {", 1)[1]
        stop = stop.split("/* No 0x55 preamble:", 1)[0]
        expected = re.compile(
            r"atomic_set\(&rx_last_bad_stop_ms, error_ms\);\s*"
            r"/\* v42\.0-47:.*?\*/\s*"
            r"p3_soft_uart_abort_bad_stop\(\);\s*return;"
        )
        self.assertRegex(stop, expected)
        self.assertIn("atomic_inc(&framing_errors);", stop)
        self.assertIn("if (rx_origin_gpio)", stop)
        self.assertIn("atomic_inc(&rx_bad_stop_chained);", stop)

    def test_recovery_release_and_no_motor_or_dma_side_effects(self):
        recovery = self.rx.split(
            "static void p3_soft_uart_abort_bad_stop(void)", 1)[1].split(
            "static void timer3_irq", 1)[0]
        wanted = [
            "nrf_timer_task_trigger(NRF_TIMER3, NRF_TIMER_TASK_STOP)",
            "nrf_timer_int_disable(NRF_TIMER3, NRF_TIMER_INT_COMPARE0_MASK)",
            "nrf_timer_event_clear(NRF_TIMER3, NRF_TIMER_EVENT_COMPARE0)",
            "if (uart_prepared)",
            "p3_uart_disable()",
            "rx_phase = P3_RX_DATA",
            "shift_byte = 0U",
            "bit_index = 0U",
            "atomic_clear(&active)",
        ]
        for token in wanted:
            with self.subTest(token=token):
                self.assertIn(token, recovery)
        self.assertLess(recovery.index("p3_uart_disable()"),
                        recovery.index("atomic_clear(&active)"))
        for forbidden in ("TASKS_STARTRX", "TASKS_STARTTX", "k_busy_wait",
                          "k_sleep(", "p3_soft_uart_request_motor_rx",
                          "gpio_pin_interrupt_configure"):
            self.assertNotIn(forbidden, recovery)

    def test_valid_header_and_nonheader_paths_preserved(self):
        stop = self.rx.split("if (rx_phase == P3_RX_STOP) {", 1)[1]
        self.assertIn("if (shift_byte == 0x55U) {\n                p3_uart_begin_dma();\n                return;", stop)
        self.assertIn("atomic_inc(&software_nonheader_bytes);", stop)
        self.assertIn("rx_phase = P3_RX_NEXT_START;", stop)
        self.assertIn("rx_origin_gpio = false; /* follow-on byte, not GPIO wake */", stop)
        gpio = self.rx.split("static void rx_start_irq(", 1)[1].split(
            "static void p3_uarte_service(", 1)[0]
        self.assertIn("!atomic_cas(&active, 0, 1)", gpio)
        self.assertIn("GPIO_INT_EDGE_FALLING", self.rx)

    def test_prior_instrumentation_still_present(self):
        for needle in ("atomic_inc(&rx_bad_stop_gpio);",
                       "atomic_inc(&rx_bad_stop_chained);",
                       "atomic_inc(&rx_idle_fault_episodes);",
                       "atomic_inc(&rx_bad_stop_post_exit_30s);",
                       "atomic_inc(&rx_bad_stop_late_or_no_exit);",
                       "atomic_inc(&hw_direct_max_timeouts);"):
            self.assertIn(needle, self.rx)


if __name__ == "__main__":
    unittest.main()
