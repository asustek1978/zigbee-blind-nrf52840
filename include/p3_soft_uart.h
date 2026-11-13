/* SPDX-License-Identifier: MIT */
#pragma once
#include <zephyr/kernel.h>
#include <stdint.h>
int p3_soft_uart_start(void);
int p3_soft_uart_read(uint8_t *byte, k_timeout_t timeout);
uint32_t p3_soft_uart_framing_errors(void);
uint32_t p3_soft_uart_overflows(void);
uint32_t p3_soft_uart_starts(void);
