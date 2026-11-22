"""v42.0-44: source contract for SAADC rail diagnostics and passive idle UART faults.

Static tests do not prove HW-954 VDDH is connected to the P3 BAT+ rail,
nor verify ADC accuracy, power consumption or nRF interrupt behavior.
"""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]
SENSORS = {
    "saadc_vddh_mv": "F16A",
    "saadc_vdd_mv": "F16B",
    "saadc_valid_mask": "F16C",
    "saadc_samples": "F16D",
    "saadc_last_ms": "F16E",
    "saadc_last_error": "F16F",
    "rx_idle_fault_episodes": "F170",
    "rx_bad_stop_post_exit_30s": "F171",
    "rx_bad_stop_late_or_no_exit": "F172",
    "rx_last_bad_stop_gpio_age_ms": "F173",
    "hw_direct_last_exit_ms": "F174",
}

class TestV440Diagnostic(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.main = (ROOT/"src/main.c").read_text()
        cls.rx = (ROOT/"src/p3_soft_uart.c").read_text()
        cls.power = (ROOT/"src/p3_power_diag.c").read_text()
        cls.h = (ROOT/"include/p3_power_diag.h").read_text()
        cls.overlay = (ROOT/"app.overlay").read_text()
        cls.prj = (ROOT/"prj.conf").read_text()
        cls.cmake = (ROOT/"CMakeLists.txt").read_text()
        cls.js = (ROOT/"zigbee2mqtt/external_converters/p3_nrf52840.js").read_text()
        cls.mjs = (ROOT/"zigbee2mqtt/external_converters/p3_nrf52840.mjs").read_text()

    def test_all_zcl_sensor_attributes_and_converter_sync(self):
        self.assertEqual(self.js, self.mjs)
        self.assertIn("blind_sw_build_id[] = {7, '4', '2', '.', '0', '-', '4', '7'}", self.main)
        ids = re.findall(r"#define P3_\w+ 0x(F[0-9A-F]{3})U", self.main)
        self.assertEqual(len(ids), len(set(ids)))
        for name, attr in SENSORS.items():
            with self.subTest(name=name):
                self.assertIn(f"#define P3_{name.upper()} 0x{attr}U", self.main)
                self.assertIn(f"{name}: 0x{attr},", self.js)
                self.assertIn(f"blind_ctx.{name}", self.main)
        self.assertIn("key: ['refresh_all_diagnostics']", self.js)

    def test_battery_not_guessed_from_vddh(self):
        self.assertIn("CONFIG_ADC=y", self.prj)
        self.assertIn("NRF_SAADC_VDDHDIV5", self.overlay)
        self.assertIn("NRF_SAADC_VDD>", self.overlay)
        self.assertIn("ADC_DT_SPEC_GET_BY_IDX", self.power)
        self.assertIn("adc_read_dt", self.power)
        self.assertIn("adc_raw_to_millivolts_dt", self.power)
        self.assertIn("sample_rail(&rail_vddh, 5U", self.power)
        self.assertIn("P3_SAADC_SAMPLE_PERIOD_MS 600000U", self.power)
        self.assertIn("!p3_soft_uart_direct_rx_active()", self.power)
        self.assertIn("p3_power_diag.c", self.cmake)
        self.assertIn("battery path unverified", self.js)
        self.assertIn("diagnosticAttributes.motor_dp_battery", self.js)
        self.assertNotIn("result.battery = result.saadc_vddh_mv", self.js)
        self.assertNotIn("cover.open", self.power)

    def test_idle_uart_is_passive_in_error_path(self):
        self.assertIn("if (rx_phase == P3_RX_STOP) {", self.rx)
        self.assertIn("atomic_inc(&rx_idle_fault_episodes);", self.rx)
        self.assertIn("atomic_inc(&rx_bad_stop_post_exit_30s);", self.rx)
        self.assertIn("atomic_inc(&rx_bad_stop_late_or_no_exit);", self.rx)
        self.assertIn("atomic_set(&rx_last_bad_stop_gpio_age_ms,", self.rx)
        self.assertIn("atomic_set(&hw_direct_last_exit_ms,", self.rx)
        self.assertIn("atomic_inc(&framing_errors);", self.rx)
        self.assertIn("p3_hw_rx_timeout_due(now, anchor_ms, P3_HW_RX_MAX_MS)", self.rx)
        self.assertIn("p3_uart_begin_dma();", self.rx)
        self.assertIn("P3_HW_RX_MAX_MS 120000U", self.rx)
        self.assertIn("P3_BIT_US 104U", self.rx)

if __name__ == "__main__":
    unittest.main()
