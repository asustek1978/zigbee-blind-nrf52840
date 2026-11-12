/* SPDX-License-Identifier: MIT */
/* v42.0-15: no UART API access or UART worker threads.
 * UART0 remains enabled in DeviceTree, so Zephyr still initializes
 * its driver at boot. All motor commands are deliberately rejected.
 * Bench-only: do not attach a motor.
 */
#include "tuya_uart.h"
#include <string.h>

void tuya_uart_mark_zigbee_command(void) {}
bool tuya_uart_submit_action(enum tuya_motor_action action)
{
    (void)action;
    return false;
}
bool tuya_uart_submit_position(uint8_t percent)
{
    (void)percent;
    return false;
}
bool tuya_uart_submit_hw_calibration(uint8_t command)
{
    (void)command;
    return false;
}
bool tuya_uart_submit_local_finish(void) { return false; }
void tuya_uart_get_state(struct tuya_motor_state *out)
{
    if (out) memset(out, 0, sizeof(*out));
}
void tuya_uart_get_diagnostics(struct tuya_uart_diagnostics *out)
{
    if (out) memset(out, 0, sizeof(*out));
}
