/* v42.0-11: no UART device, RX IRQ, or UART worker threads. */
#include "tuya_uart.h"
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>

static atomic_t command_count;

void tuya_uart_mark_zigbee_command(void)
{
    atomic_inc(&command_count);
}

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

bool tuya_uart_submit_local_finish(void)
{
    return false;
}

void tuya_uart_get_state(struct tuya_motor_state *out)
{
    if (out != NULL) {
        memset(out, 0, sizeof(*out));
    }
}

void tuya_uart_get_diagnostics(struct tuya_uart_diagnostics *out)
{
    if (out != NULL) {
        memset(out, 0, sizeof(*out));
        out->last_command = 0xffU;
        out->last_format = 0xffU;
        out->zigbee_commands = (uint32_t)atomic_get(&command_count);
    }
}
