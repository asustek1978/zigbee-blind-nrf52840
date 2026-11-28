"""v42.0-50: source-only guardrails for calibrated INPUT voltage telemetry.
Does not prove physical absolute voltage or BAT+ equivalence.
"""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]

class TestV42050CalibratedVoltage(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        def read(p): return (ROOT / p).read_text(encoding="utf-8")
        cls.main = read("src/main.c")
        cls.adc = read("src/p3_power_diag.c")
        cls.hdr = read("include/p3_power_diag.h")
        cls.policy = read("include/p3_vddh_calibration.h")
        cls.z2m = read("zigbee2mqtt/external_converters/p3_nrf52840.mjs")

    def test_isolated_one_point_calibration(self):
        self.assertIn("#define P3_VDDH_CAL_REFERENCE_MV 3598U", self.policy)
        self.assertIn("#define P3_VDDH_CAL_RAW_MV 3505U", self.policy)
        self.assertIn("p3_vddh_input_estimate_mv(", self.adc)
        self.assertIn("p3_power_diag_vddh_calibrated_mv", self.hdr)
        self.assertIn("p3_power_diag_valid_mask() & 1U", self.adc)
        self.assertIn("saadc_vddh_calibrated_mv", self.main)
        self.assertIn("#define P3_SAADC_VDDH_CALIBRATED_MV 0xF19AU", self.main)
        self.assertIn("saadc_vddh_calibrated_mv: 0xF19A", self.z2m)
        self.assertIn("blind_sw_build_id[] = {7, '4', '2', '.', '0', '-', '5', '0'}", self.main)

    def test_no_fake_battery_or_extra_uart(self):
        self.assertIn("blind_ctx.battery_voltage = 0xffU;", self.main)
        self.assertNotIn("battery_voltage = p3_power_diag_vddh_calibrated_mv", self.main)
        self.assertNotIn("battery_remaining_half = p3_power_diag_vddh_calibrated_mv", self.main)
        self.assertNotIn("result.battery = result.saadc_vddh_calibrated_mv", self.z2m)
        self.assertIn("P3_SAADC_SAMPLE_PERIOD_MS 600000U", self.adc)
        self.assertNotIn("tuya_uart_submit_", self.adc)
        self.assertNotIn("tuya_uart_submit_", self.policy)

if __name__ == "__main__":
    unittest.main()
