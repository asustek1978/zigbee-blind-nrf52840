"""v42.0-45 static contract: calibrated SAADC and bounded position-based
new motion detection. Physical HW-954 bench verification remains required.
"""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]
NEW = {
    "position_wake_count": "F175",
    "position_wake_suppressed": "F176",
    "position_wake_last_ms": "F177",
    "position_wake_latched": "F178",
}

class TestV42045(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        def read(p): return (ROOT / p).read_text(encoding="utf-8")
        cls.main = read("src/main.c")
        cls.rx = read("src/p3_soft_uart.c")
        cls.uart = read("src/tuya_uart_hybrid.c")
        cls.adc = read("src/p3_power_diag.c")
        cls.overlay = read("app.overlay")
        cls.helper = read("include/p3_position_wake.h")
        cls.js = read("zigbee2mqtt/external_converters/p3_nrf52840.js")
        cls.mjs = read("zigbee2mqtt/external_converters/p3_nrf52840.mjs")

    def test_calibrated_adc_but_not_faked_battery(self):
        self.assertEqual(self.overlay.count("ADC_ACQ_TIME(ADC_ACQ_TIME_MICROSECONDS, 10)"), 2)
        self.assertIn("NRF_SAADC_VDDHDIV5", self.overlay)
        self.assertIn("seq.calibrate = true;", self.adc)
        self.assertIn("seq.oversampling = 4;", self.adc)
        self.assertIn("seq.buffer_size = sizeof(raw)", self.adc)
        self.assertIn("P3_SAADC_SAMPLE_PERIOD_MS 600000U", self.adc)
        self.assertIn("diagnosticAttributes.motor_dp_battery", self.js)
        self.assertNotIn("result.battery = result.saadc_vddh_mv", self.js)

    def test_position_wake_valid_changed_and_lockout_limited(self):
        self.assertIn("P3_POSITION_WAKE_QUIET_MS 10000U", self.helper)
        self.assertIn("if (position == state->last_position)", self.helper)
        self.assertIn("if (!state->initialized)", self.helper)
        self.assertIn("p3_position_wake_fresh_episode(", self.rx)
        self.assertIn("if (accepted && changes.position && motor_state.position_valid)", self.uart)
        self.assertIn("p3_soft_uart_note_changed_position(motor_state.position)", self.uart)
        self.assertIn("if (atomic_get(&hw_direct_motion_lockout) == 0)", self.rx)
        self.assertIn("atomic_get(&p3_position_wake_latched) != 0", self.rx)
        self.assertIn("atomic_cas(&p3_position_wake_latched, 0, 1)", self.rx)
        self.assertIn("atomic_clear(&p3_position_wake_latched);", self.rx)
        self.assertIn("p3_soft_uart_note_explicit_motor_command();", self.uart)
        self.assertIn("p3_hw_rx_timeout_due(now, anchor_ms, P3_HW_RX_MAX_MS)", self.rx)
        self.assertIn("P3_HW_RX_MAX_MS 120000U", self.rx)
        self.assertIn("const bool hold_direct = direct && p3_hw_rx_wanted(now);", self.rx)

    def test_zcl_mapping(self):
        self.assertEqual(self.js, self.mjs)
        self.assertIn("blind_sw_build_id[] = {7, '4', '2', '.', '0', '-', '4', '7'}", self.main)
        all_ids = re.findall(r"#define P3_\w+ 0x(F[0-9A-F]{3})U", self.main)
        self.assertEqual(len(all_ids), len(set(all_ids)))
        for field, attr in NEW.items():
            with self.subTest(field=field):
                self.assertIn(f"#define P3_{field.upper()} 0x{attr}U", self.main)
                self.assertIn(f"{field}: 0x{attr},", self.js)
                self.assertIn(f"blind_ctx.{field}", self.main)
        self.assertIn("key: ['refresh_all_diagnostics']", self.js)

if __name__ == "__main__":
    unittest.main()
