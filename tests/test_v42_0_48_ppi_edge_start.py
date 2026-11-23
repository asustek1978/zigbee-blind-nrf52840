"""Source-level regression checks for the isolated v42.0-48 bench experiment.

These tests do not prove hardware timing, UART reliability or current draw.
A real HW-954/motor test is mandatory before considering deployment.
"""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
RX = (ROOT / "src" / "p3_soft_uart.c").read_text()
MAIN = (ROOT / "src" / "main.c").read_text()
CONV = (ROOT / "zigbee2mqtt" / "external_converters" / "p3_nrf52840.mjs").read_text()


class PpiStartSourceContract(unittest.TestCase):
    def test_gpiote_event_routes_to_timer_start_only(self):
        self.assertIn("nrfx_gppi_conn_alloc(eep, tep, &conn)", RX)
        self.assertIn("&NRF_GPIOTE->EVENTS_IN[i]", RX)
        self.assertIn("&NRF_TIMER3->TASKS_START", RX)
        self.assertIn("nrfx_gppi_conn_enable(p3_ppi_conn)", RX)
        self.assertIn("nrfx_gppi_conn_disable(p3_ppi_conn)", RX)
        self.assertIn("nrfx_gppi_conn_free(", RX)
        self.assertIn("p3_ppi_setup();", RX)
        self.assertIn("p3_ppi_arm();", RX)
        # Pinned Nordic R23 header does not provide the former API.
        self.assertNotIn("nrfx_gppi_channel_endpoints_setup", RX)
        self.assertNotIn("nrfx_gppi_channels_enable(BIT(", RX)

    def test_fallback_and_no_repeat_start_within_data_bits(self):
        self.assertIn("p3_ppi_disarm(); /* Disarm before any further falling data-bit edge. */", RX)
        self.assertIn("NRF_TIMER_TASK_CAPTURE1", RX)
        self.assertIn("if (!ppi_started)", RX)
        self.assertIn("atomic_inc(&p3_ppi_fallbacks)", RX)

    def test_motor_and_watchdog_contract_preserved(self):
        self.assertIn("#define P3_HW_RX_STOP_GUARD_MS 1500U", RX)
        self.assertIn("#define P3_HW_RX_MAX_MS 120000U", RX)
        self.assertIn("P3_UART_IDLE_MS 15U", RX)
        self.assertIn("P3_RX_PIN 17U", RX)

    def test_version_and_diagnostics(self):
        self.assertIn("'4', '2', '.', '0', '-', '4', '9'", MAIN)
        for name, attr in [
            ("ppi_enabled", "0xF187"),
            ("ppi_starts", "0xF188"),
            ("ppi_fallbacks", "0xF189"),
            ("ppi_late_irqs", "0xF18A"),
        ]:
            self.assertIn(name, CONV)
            self.assertIn(attr, CONV)


if __name__ == "__main__":
    unittest.main()
