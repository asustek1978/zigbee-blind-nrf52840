/* SPDX-License-Identifier: MIT */
#pragma once
#include <zephyr/kernel.h>
#include <stdint.h>
int p3_soft_uart_start(void);
void p3_soft_uart_stop(void);
int p3_soft_uart_read(uint8_t *byte, k_timeout_t timeout);
uint32_t p3_soft_uart_framing_errors(void);
uint32_t p3_soft_uart_overflows(void);
uint32_t p3_soft_uart_starts(void);
/* v42.0-34 hybrid software-first-byte / UARTE EasyDMA diagnostics. */
uint32_t p3_soft_uart_uarte_wakes(void);
uint32_t p3_soft_uart_dma_bytes(void);
uint32_t p3_soft_uart_handoff_errors(void);
uint32_t p3_soft_uart_hardware_errors(void);
/* v42.0-39, diagnostics only; no RX behavior/timing changes. */
uint32_t p3_soft_uart_software_nonheader_bytes(void);
uint32_t p3_soft_uart_last_hw_errorsrc(void);

/* v42.0-40 passive RX classification; DP1 supplied only from MCU reports. */
/* v42.0-42: request full UARTE0 RX for the next motor movement.
 * Only the RX owner thread applies the mode switch.
 */
void p3_soft_uart_request_motor_rx(void);
bool p3_soft_uart_direct_rx_active(void);
void p3_soft_uart_note_prearm_timeout(void);
void p3_soft_uart_set_mcu_motion(uint8_t dp1);
uint32_t p3_soft_uart_reported_motion(void);
uint32_t p3_soft_uart_gpio_start_events(void);
uint32_t p3_soft_uart_last_gpio_start_ms(void);
uint32_t p3_soft_uart_gpio_starts_reported_idle(void);
uint32_t p3_soft_uart_gpio_starts_reported_moving(void);
uint32_t p3_soft_uart_gpio_starts_motion_unknown(void);
uint32_t p3_soft_uart_bad_stop_gpio(void);
uint32_t p3_soft_uart_bad_stop_chained(void);
uint32_t p3_soft_uart_bad_stop_reported_idle(void);
uint32_t p3_soft_uart_bad_stop_reported_moving(void);
uint32_t p3_soft_uart_bad_stop_motion_unknown(void);
uint32_t p3_soft_uart_last_bad_stop_ms(void);
uint32_t p3_soft_uart_last_bad_stop_byte(void);

/* v42.0-41: error-only diagnostics, read on the Zigbee task. */
uint32_t p3_soft_uart_bad_stop_gpio_last_ms(void);
uint32_t p3_soft_uart_bad_stop_chained_last_ms(void);
uint32_t p3_soft_uart_handoff_unprepared(void);
uint32_t p3_soft_uart_handoff_rxto_timeout(void);
uint32_t p3_soft_uart_handoff_amount_oversize(void);
uint32_t p3_soft_uart_handoff_no_rxstarted(void);
uint32_t p3_soft_uart_handoff_zero_amount(void);
uint32_t p3_soft_uart_handoff_last_ms(void);
uint32_t p3_soft_uart_handoff_last_reason(void);
uint32_t p3_soft_uart_hw_framing_events(void);
uint32_t p3_soft_uart_hw_overrun_events(void);
uint32_t p3_soft_uart_hw_break_events(void);
uint32_t p3_soft_uart_hw_parity_events(void);
uint32_t p3_soft_uart_hw_last_ms(void);
uint32_t p3_soft_uart_hw_last_amount(void);

/* v42.0-42: exposed to Zigbee2MQTT; counters reset on boot. */
uint32_t p3_soft_uart_hw_direct_active(void);
uint32_t p3_soft_uart_hw_direct_enters(void);
uint32_t p3_soft_uart_hw_direct_exits(void);
uint32_t p3_soft_uart_hw_direct_bytes(void);
uint32_t p3_soft_uart_hw_direct_restarts(void);
uint32_t p3_soft_uart_hw_direct_max_timeouts(void);
uint32_t p3_soft_uart_hw_direct_prearm_timeouts(void);
uint32_t p3_soft_uart_hw_direct_last_switch_ms(void);
uint32_t p3_soft_uart_hw_direct_hw_errors(void);
uint32_t p3_soft_uart_hw_direct_empty_dma(void);
uint32_t p3_soft_uart_hw_direct_rxto_timeouts(void);
/* v42.0-43: absolute watchdog origin and post-timeout MCU re-arm suppression. */
uint32_t p3_soft_uart_hw_direct_session_start_ms(void);
uint32_t p3_soft_uart_hw_direct_motion_lockout(void);

/* v42.0-44: passive idle RX fault diagnostics, no timing/path changes. */
uint32_t p3_soft_uart_idle_fault_episodes(void);
uint32_t p3_soft_uart_bad_stop_post_exit_30s(void);
uint32_t p3_soft_uart_bad_stop_late_or_no_exit(void);
uint32_t p3_soft_uart_last_bad_stop_gpio_age_ms(void);
uint32_t p3_soft_uart_hw_direct_last_exit_ms(void);
