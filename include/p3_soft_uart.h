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
