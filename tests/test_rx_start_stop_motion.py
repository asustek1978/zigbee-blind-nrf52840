"""Static regression contract for v42.0-40 passive UART diagnostics.

Tests verify instrumentation and stable source constants, not real-time ISR
latency, energy consumption, compiled firmware, or real motor movement.
"""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]


class TestRxStartStopMotion(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.rx = (ROOT / "src/p3_soft_uart.c").read_text(encoding="utf-8")
        cls.h = (ROOT / "include/p3_soft_uart.h").read_text(encoding="utf-8")
        cls.main = (ROOT / "src/main.c").read_text(encoding="utf-8")
        cls.motor = (ROOT / "src/tuya_uart_hybrid.c").read_text(encoding="utf-8")
        cls.z2m = (
            ROOT / "zigbee2mqtt/external_converters/p3_nrf52840.mjs"
        ).read_text(encoding="utf-8")

    def test_power_wake_and_zigbee_timing_unchanged(self):
        self.assertIn("#define P3_FIRST_SAMPLE_US 156U", self.rx)
        self.assertIn("#define P3_BIT_US 104U", self.rx)
        self.assertIn("#define P3_UART_IDLE_MS 15U", self.rx)
        self.assertIn("NRF_UARTE0->TASKS_STARTRX = 1U", self.rx)
        self.assertIn("NRF_UARTE0->TASKS_STOPRX = 1U", self.rx)
        self.assertIn("nrf_uarte_disable(NRF_UARTE0)", self.rx)
        self.assertIn("p3_uart_begin_dma();", self.rx)
        self.assertIn("#define P3_ZIGBEE_POLL_INTERVAL_MS 1000U", self.main)
        self.assertIn("zb_zdo_pim_set_long_poll_interval(P3_ZIGBEE_POLL_INTERVAL_MS)", self.main)
        self.assertNotIn("k_sleep(K_SECONDS(30))", self.main)

    def test_exposed_attribute_ids_are_unique(self):
        attrs = {
            "RX_GPIO_START_EVENTS": ("rx_gpio_start_events", "F141"),
            "RX_LAST_GPIO_START_MS": ("rx_last_gpio_start_ms", "F142"),
            "RX_BAD_STOP_GPIO": ("rx_bad_stop_gpio", "F143"),
            "RX_BAD_STOP_CHAINED": ("rx_bad_stop_chained", "F144"),
            "RX_BAD_STOP_REPORTED_IDLE": ("rx_bad_stop_reported_idle", "F145"),
            "RX_BAD_STOP_REPORTED_MOVING": ("rx_bad_stop_reported_moving", "F146"),
            "RX_BAD_STOP_MOTION_UNKNOWN": ("rx_bad_stop_motion_unknown", "F147"),
            "RX_LAST_BAD_STOP_MS": ("rx_last_bad_stop_ms", "F148"),
            "RX_LAST_BAD_STOP_BYTE": ("rx_last_bad_stop_byte", "F149"),
            "RX_REPORTED_MOTION": ("rx_reported_motion", "F14A"),
        }
        for symbolic, (field, attr) in attrs.items():
            with self.subTest(field=field):
                self.assertIn(f"#define P3_{symbolic} 0x{attr}U", self.main)
                self.assertIn(f"P3_{symbolic}, &blind_ctx.{field}", self.main)
                self.assertIn(f"{field}: 0x{attr},", self.z2m)
                self.assertIn(f"p3_soft_uart_{field}(", self.h) if field in (
                    "rx_gpio_start_events",
                ) else None
        attr_numbers = re.findall(r"#define P3_(?:RX|UART)_\w+ 0x(F\w+)U", self.main)
        self.assertEqual(len(attr_numbers), len(set(attr_numbers)))

    def test_only_real_mcu_dp1_updates_motion_tag(self):
        self.assertIn("tuya_motor_apply_report(frame, &motor_state, &changes)", self.motor)
        self.assertIn("if (accepted && motor_state.motion_valid)", self.motor)
        self.assertIn("p3_soft_uart_set_mcu_motion(reported_motion);", self.motor)
        self.assertIn("if (dp1 <= 2U)", self.rx)
        self.assertIn("rx_reported_motion = ATOMIC_INIT(3)", self.rx)
        self.assertNotIn("p3_soft_uart_set_mcu_motion(", self.main)

    def test_bad_stop_count_partition(self):
        self.assertIn("atomic_inc(&framing_errors);", self.rx)
        self.assertIn("if (rx_origin_gpio)", self.rx)
        self.assertIn("atomic_inc(&rx_bad_stop_gpio);", self.rx)
        self.assertIn("atomic_inc(&rx_bad_stop_chained);", self.rx)
        self.assertIn("atomic_inc(&rx_bad_stop_reported_idle);", self.rx)
        self.assertIn("atomic_inc(&rx_bad_stop_reported_moving);", self.rx)
        self.assertIn("atomic_inc(&rx_bad_stop_motion_unknown);", self.rx)
        self.assertIn("rx_origin_gpio = true; /* tag byte whose start was a GPIO edge */", self.rx)
        self.assertIn("rx_origin_gpio = false; /* follow-on byte, not GPIO wake */", self.rx)

    def test_arm_timer_before_diagnostic_start_timestamp(self):
        gpio_code = self.rx.split("static void rx_start_irq(", 1)[1].split(
            "static void p3_uarte_service(", 1
        )[0]
        self.assertLess(
            gpio_code.index("nrf_timer_task_trigger(NRF_TIMER3, NRF_TIMER_TASK_START);"),
            gpio_code.index("atomic_inc(&rx_gpio_start_events);"),
        )
        self.assertIn("k_uptime_get_32()", gpio_code)

    def test_bad_stop_instrumentation_not_on_header_success_path(self):
        stop_handler = self.rx.split("if (rx_phase == P3_RX_STOP) {", 1)[1].split(
            "/* No 0x55 preamble", 1
        )[0]
        self.assertLess(stop_handler.index("p3_uart_begin_dma();"), stop_handler.index("atomic_inc(&framing_errors);"))
        self.assertIn("atomic_set(&rx_last_bad_stop_byte", stop_handler)
        self.assertIn("atomic_set(&rx_last_bad_stop_ms", stop_handler)

    def test_aggregated_rx_error_formula_unchanged(self):
        self.assertIn(
            "blind_ctx.rx_error_count = blind_ctx.rx_framing_errors +",
            self.main,
        )
        self.assertIn("p3_soft_uart_hardware_errors() +", self.main)
        self.assertIn("p3_soft_uart_handoff_errors();", self.main)


if __name__ == "__main__":
    unittest.main()
