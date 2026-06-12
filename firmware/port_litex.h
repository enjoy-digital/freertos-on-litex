/*
 * LiteX/VexRiscv glue for the FreeRTOS RISC-V port.
 *
 * Copyright (c) 2026 EnjoyDigital <florent@enjoy-digital.fr>
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifndef PORT_LITEX_H
#define PORT_LITEX_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Boot markers scraped by the simulation / test harness. Same sentinel
 * convention as litex_mquickjs: a single line on stdout terminates a
 * run with a pass/fail verdict. */
#define PORT_LITEX_DONE_MARKER "[rtos] done"
#define PORT_LITEX_FAIL_MARKER "[rtos] fail"

/* --------------------------------------------------------------------
 * UART
 * ------------------------------------------------------------------ */

/* Polling TX / RX. Safe to call from any context, including before the
 * scheduler has started. Uses direct CSR access — does not depend on
 * FreeRTOS state. */
void  litex_uart_poll_putc(char c);
int   litex_uart_poll_getc_nonblock(void); /* -1 if no byte available */

/* Enable UART RX interrupts and route bytes into the internal stream
 * buffer. Call after vTaskStartScheduler() (or more precisely from a
 * task). Creates the stream buffer on first call. */
void  litex_uart_rx_enable(void);

/* Task-side blocking read. Returns the next received byte, blocking
 * with FreeRTOS semantics (may suspend the caller). Only meaningful
 * after litex_uart_rx_enable(). */
char  litex_uart_getc(void);

/* Non-blocking, task-side. Returns -1 when no byte is ready. */
int   litex_uart_getc_nonblock(void);

/* --------------------------------------------------------------------
 * Firmware shutdown / reboot helpers
 * ------------------------------------------------------------------ */

/* Print the done marker and halt cleanly. Used by demos to signal to
 * the sim harness that the demo finished. */
void  litex_done(void)  __attribute__((noreturn));
void  litex_fail(const char *why) __attribute__((noreturn));

/* Reset the SoC via the LiteX ctrl register (no-op if not wired). */
void  litex_reboot(void);

#ifdef __cplusplus
}
#endif

#endif /* PORT_LITEX_H */
