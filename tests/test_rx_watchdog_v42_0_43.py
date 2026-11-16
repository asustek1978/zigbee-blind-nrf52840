"""v42.0-43: protection against renewed CLOSE/OPEN reports keeping UART awake.

These checks assert the actual firmware paths use the host-tested pure
watchdog predicates. They do not validate nRF52 electrical/IRQ behavior.
"""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]

class TestHardwareRxWatchdog(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.src = (ROOT / "src/p3_soft_uart.c").read_text(encoding="utf-8")
        cls.h = (ROOT / "include/p3_soft_uart.h").read_text(encoding="utf-8")
        cls.predicate = (ROOT / "include/p3_uart_watchdog.h").read_text(encoding="utf-8")
        cls.main = (ROOT / "src/main.c").read_text(encoding="utf-8")
        cls.motor = (ROOT / "src/tuya_uart_hybrid.c").read_text(encoding="utf-8")
        cls.js = (ROOT / "zigbee2mqtt/external_converters/p3_nrf52840.js").read_text(encoding="utf-8")
        cls.mjs = (ROOT / "zigbee2mqtt/external_converters/p3_nrf52840.mjs").read_text(encoding="utf-8")

    def test_absolute_deadline_bound_to_hardware_start(self):
        self.assertIn('#include "p3_uart_watchdog.h"', self.src)
        self.assertIn('direct ? &hw_direct_session_start_ms : &hw_rx_request_ms', self.src)
        self.assertIn('p3_hw_rx_timeout_due(now, anchor_ms, P3_HW_RX_MAX_MS)', self.src)
        self.assertIn('atomic_set(&hw_direct_session_start_ms, (atomic_val_t)k_uptime_get_32());', self.src)
        self.assertIn('if (direct) {\n                atomic_inc(&hw_direct_max_timeouts);', self.src)
        self.assertIn('atomic_set(&hw_direct_motion_lockout, 1);', self.src)
        self.assertIn('p3_hw_rx_stop_guard_due(now, stop_ms, P3_HW_RX_STOP_GUARD_MS)', self.src)
        self.assertIn('if (hold_direct &&', self.src)
        self.assertIn('if (direct) {\n            if (hold_direct &&', self.src)

    def test_repeated_motion_does_not_extend_requested_time(self):
        motion = self.src.split('void p3_soft_uart_set_mcu_motion(uint8_t dp1)', 1)[1].split(
            'uint32_t p3_soft_uart_reported_motion(void)', 1)[0]
        self.assertIn('p3_hw_rx_motion_transition((uint8_t)previous, dp1,', motion)
        self.assertIn('atomic_get(&hw_direct_motion_lockout) != 0', motion)
        self.assertIn('atomic_clear(&hw_direct_motion_lockout);', motion)
        self.assertNotIn('atomic_set(&hw_rx_request_ms', motion)
        self.assertIn('new_dp1 != 1U &&', self.predicate)
        self.assertIn('old_dp1 != new_dp1 && !motion_lockout', self.predicate)

    def test_explicit_commands_not_blocked_by_lockout(self):
        request = self.src.split('void p3_soft_uart_request_motor_rx(void)', 1)[1].split(
            'bool p3_soft_uart_direct_rx_active(void)', 1)[0]
        self.assertIn('atomic_set(&hw_direct_motion_lockout, 0);', request)
        self.assertIn('if (!atomic_get(&hw_rx_requested))', request)
        self.assertLess(request.index('atomic_set(&hw_rx_request_ms'), request.index('atomic_set(&hw_rx_requested, 1);'))
        self.assertLess(request.index('atomic_clear(&hw_rx_stop_ms);'), request.index('atomic_set(&hw_rx_requested, 1);'))
        self.assertIn('atomic_clear(&hw_rx_stop_ms);', request)
        self.assertIn('p3_soft_uart_request_motor_rx();', self.motor)
        self.assertIn('frame.is_stop ? 20U : 80U', self.motor)

    def test_diagnostics_and_old_contracts_preserved(self):
        self.assertEqual(self.js, self.mjs)
        self.assertIn("blind_sw_build_id[] = {7, '4', '2', '.', '0', '-', '4', '4'}", self.main)
        for field, attr in [('hw_direct_session_start_ms', 'F168'), ('hw_direct_motion_lockout', 'F169')]:
            self.assertIn(f"#define P3_{field.upper()} 0x{attr}U", self.main)
            self.assertIn(f"{field}: 0x{attr},", self.js)
            self.assertIn(f"blind_ctx.{field} = p3_soft_uart_{field}();", self.main)
            self.assertIn(f"uint32_t p3_soft_uart_{field}(void)", self.h)
        self.assertIn("key: ['refresh_all_diagnostics']", self.js)
        self.assertIn("P3_UART_IDLE_MS 15U", self.src)
        self.assertIn("P3_HW_RX_MAX_MS 120000U", self.src)

if __name__ == '__main__':
    unittest.main()
