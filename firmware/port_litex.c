/*
 * LiteX/VexRiscv port glue for FreeRTOS.
 *
 * Responsibilities:
 *   - Program LiteX timer0 for periodic tick (vPortSetupTimerInterrupt).
 *   - Dispatch LiteX IRQs from the FreeRTOS RISC-V trap handler
 *     (freertos_risc_v_application_interrupt_handler).
 *   - Implement a tiny UART layer: polling TX/RX for boot + pre-scheduler
 *     work, and an IRQ-driven RX path that hands bytes to a stream
 *     buffer so the shell task can block cleanly.
 *   - Implement run-time-stats counter and assert/hook stubs.
 *
 * Copyright (c) 2026 EnjoyDigital <florent@enjoy-digital.fr>
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <generated/csr.h>
#include <generated/soc.h>
#include <irq.h>
#include <system.h>
#include <libbase/uart.h>

#include "FreeRTOS.h"
#include "task.h"
#include "stream_buffer.h"

#include "port_litex.h"

/* Declarations for symbols referenced by the FreeRTOS RISC-V port's
 * portASM.S (weak overrides in our case). */
extern void freertos_risc_v_application_interrupt_handler(void);
extern void freertos_risc_v_application_exception_handler(void);

/* --------------------------------------------------------------------
 * mie / mtvec helpers
 * ------------------------------------------------------------------ */

/* Bit 11 of mie = Machine External Interrupt Enable. LiteX routes all
 * device IRQs through MEI on VexRiscv. We do NOT enable MTI (bit 7):
 * there is no mtime on this CPU, and the FreeRTOS tick comes from
 * LiteX timer0 as a regular external IRQ. */
#define MIE_MEI     (1u << 11)

static inline void set_mie(uint32_t bits)
{
    __asm volatile ("csrs mie, %0" :: "r" (bits));
}

/* --------------------------------------------------------------------
 * Timer0 tick
 *
 * LiteX timer0 is a 32-bit countdown timer. When VALUE reaches 0 and
 * RELOAD is non-zero the timer reloads and raises its "zero" event.
 * We use it as a periodic tick source.
 * ------------------------------------------------------------------ */

#ifndef TIMER0_INTERRUPT
#error "SoC does not expose TIMER0_INTERRUPT; rebuild with --timer-uptime."
#endif

#define TICK_RELOAD    (configCPU_CLOCK_HZ / configTICK_RATE_HZ)

void vPortSetupTimerInterrupt(void)
{
    /* Disable while reprogramming. */
    timer0_en_write(0);
    timer0_load_write(TICK_RELOAD);
    timer0_reload_write(TICK_RELOAD);
    timer0_en_write(1);

    /* Clear any stale event, enable the "zero" event IRQ. */
    timer0_ev_pending_write(timer0_ev_pending_read());
    timer0_ev_enable_write(1);

    /* Unmask the timer line at the LiteX IRQ controller. */
    irq_setmask(irq_getmask() | (1u << TIMER0_INTERRUPT));

    /* Enable machine external interrupts on the CPU. Global IE is left
     * off here — xPortStartFirstTask will flip it on when restoring the
     * first task's mstatus. */
    set_mie(MIE_MEI);
}

/* --------------------------------------------------------------------
 * Run-time stats counter
 *
 * FreeRTOS asks for a free-running counter that ticks roughly 10-100x
 * faster than the scheduler. timer0_uptime_cycles (enabled by the
 * `--timer-uptime` litex_sim flag) is exactly that — a 64-bit cycle
 * counter. We return the low 32 bits.
 * ------------------------------------------------------------------ */

uint32_t ulPortGetRunTimeCounterValue(void)
{
#if defined(CSR_TIMER0_UPTIME_CYCLES_ADDR)
    timer0_uptime_latch_write(1);
    return (uint32_t)timer0_uptime_cycles_read();
#else
    /* Fall back to the LiteX cycle CSR on CPUs where uptime is absent.
     * Loses wall-clock meaning under runtime stats but stays monotonic. */
    uint32_t v;
    __asm volatile ("rdcycle %0" : "=r" (v));
    return v;
#endif
}

/* --------------------------------------------------------------------
 * UART
 *
 * We deliberately do NOT call libbase's uart_init() — that wires a
 * ring-buffer and `irq_attach` into libbase's own isr() dispatcher,
 * which we bypass. Instead we access the UART CSRs directly and own
 * the interrupt path.
 * ------------------------------------------------------------------ */

#define UART_EV_TX    0x1
#define UART_EV_RX    0x2

void litex_uart_poll_putc(char c)
{
#ifdef CSR_UART_BASE
    while (uart_txfull_read()) { }
    uart_rxtx_write((uint8_t)c);
    if (c == '\n')
        litex_uart_poll_putc('\r');
#else
    (void)c;
#endif
}

int litex_uart_poll_getc_nonblock(void)
{
#ifdef CSR_UART_BASE
    if (uart_rxempty_read())
        return -1;
    int c = uart_rxtx_read();
    uart_ev_pending_write(UART_EV_RX);
    return c;
#else
    return -1;
#endif
}

/* Override libbase's uart_write so picolibc's litex_putc falls through
 * to us (polling). Printing from a task just busy-waits for the TX
 * FIFO — trivially fast on a 16-byte FIFO drained at 115200 baud. */
void uart_write(char c)
{
    litex_uart_poll_putc(c);
}

/* Override libbase's uart_read as well, in case any pulled-in object
 * references it. Polling — the FreeRTOS path is litex_uart_getc(). */
char uart_read(void)
{
    int c;
    do { c = litex_uart_poll_getc_nonblock(); } while (c < 0);
    return (char)c;
}

int uart_read_nonblock(void)
{
#ifdef CSR_UART_BASE
    return !uart_rxempty_read();
#else
    return 0;
#endif
}

/* No-op uart_init; libc/stdio.c does not reference it, but libbase
 * headers declare it — keep it defined so nothing else drags in
 * libbase/uart.o. */
void uart_init(void) { }

void uart_sync(void)
{
#ifdef CSR_UART_BASE
    while (!uart_txempty_read()) { }
#endif
}

/* --- IRQ-driven RX, task-visible through a stream buffer --- */

#define UART_RX_STREAM_BYTES   128

static StreamBufferHandle_t rx_stream = NULL;

void litex_uart_rx_enable(void)
{
    if (rx_stream != NULL)
        return;

    rx_stream = xStreamBufferCreate(UART_RX_STREAM_BYTES, 1);
    configASSERT(rx_stream != NULL);

#ifdef CSR_UART_BASE
    /* Clear any stale event, then enable RX event delivery. */
    uart_ev_pending_write(UART_EV_RX | UART_EV_TX);
    uart_ev_enable_write(UART_EV_RX);
    irq_setmask(irq_getmask() | (1u << UART_INTERRUPT));
#endif
}

static void uart_isr_handler(BaseType_t *pxHigherPriorityTaskWoken)
{
#ifdef CSR_UART_BASE
    while (!uart_rxempty_read()) {
        uint8_t b = (uint8_t)uart_rxtx_read();
        uart_ev_pending_write(UART_EV_RX);
        if (rx_stream != NULL) {
            (void)xStreamBufferSendFromISR(rx_stream, &b, 1,
                                           pxHigherPriorityTaskWoken);
            /* Overflow is silent — shell tolerates dropped bytes, and
             * asserting here would kill the whole system over a noisy
             * line. */
        }
    }
#else
    (void)pxHigherPriorityTaskWoken;
#endif
}

char litex_uart_getc(void)
{
    configASSERT(rx_stream != NULL);
    uint8_t b = 0;
    while (xStreamBufferReceive(rx_stream, &b, 1, portMAX_DELAY) == 0) { }
    return (char)b;
}

int litex_uart_getc_nonblock(void)
{
    if (rx_stream == NULL)
        return litex_uart_poll_getc_nonblock();
    uint8_t b;
    if (xStreamBufferReceive(rx_stream, &b, 1, 0) == 1)
        return (int)(unsigned)b;
    return -1;
}

/* --------------------------------------------------------------------
 * Timer IRQ handler
 * ------------------------------------------------------------------ */

static void timer0_isr_handler(BaseType_t *pxHigherPriorityTaskWoken)
{
    /* Acknowledge before calling into the kernel; LiteX events are
     * level-driven until cleared. */
    timer0_ev_pending_write(timer0_ev_pending_read());

    /* xTaskIncrementTick is the "from ISR" tick function on RISC-V —
     * upstream portASM.S calls it directly from the timer interrupt
     * path. It returns pdTRUE when a higher-priority task was
     * unblocked, in which case we must switch context before leaving
     * the trap. */
    if (xTaskIncrementTick() != pdFALSE) {
        *pxHigherPriorityTaskWoken = pdTRUE;
    }
}

/* --------------------------------------------------------------------
 * Top-level interrupt dispatch — called by FreeRTOS portASM.S on any
 * asynchronous trap (machine external interrupt in our case).
 *
 * Context is already saved on the ISR stack, a0-a2 have been clobbered
 * by the port. We just read LiteX irq_pending(), walk the bits, and
 * call the per-source handler. If any handler sets xHigher, we call
 * vTaskSwitchContext so the port's RESTORE_CONTEXT picks up the new
 * pxCurrentTCB.
 * ------------------------------------------------------------------ */

void freertos_risc_v_application_interrupt_handler(void)
{
    BaseType_t xHigher = pdFALSE;
    uint32_t pending = irq_pending() & irq_getmask();

    while (pending) {
        unsigned i = __builtin_ctz(pending);
        pending &= pending - 1;

        if (i == TIMER0_INTERRUPT) {
            timer0_isr_handler(&xHigher);
        }
#ifdef UART_INTERRUPT
        else if (i == UART_INTERRUPT) {
            uart_isr_handler(&xHigher);
        }
#endif
        else {
            /* Spurious or unhandled — disable the source so we don't
             * live-lock on a misconfigured peripheral. */
            irq_setmask(irq_getmask() & ~(1u << i));
        }
    }

    if (xHigher != pdFALSE) {
        vTaskSwitchContext();
    }
}

/* The port's synchronous-exception handler is weak — override it so
 * unexpected traps terminate the sim run with a visible marker rather
 * than spinning silently. */
void freertos_risc_v_application_exception_handler(void)
{
    uint32_t mcause, mepc;
    __asm volatile ("csrr %0, mcause" : "=r" (mcause));
    __asm volatile ("csrr %0, mepc"   : "=r" (mepc));
    printf("\n*** unhandled exception: mcause=0x%08lx mepc=0x%08lx\n",
           (unsigned long)mcause, (unsigned long)mepc);
    litex_fail("exception");
}

/* --------------------------------------------------------------------
 * FreeRTOS hooks
 * ------------------------------------------------------------------ */

void vApplicationMallocFailedHook(void)
{
    printf("\n*** malloc failed (heap exhausted)\n");
    litex_fail("malloc");
}

void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{
    (void)xTask;
    printf("\n*** stack overflow in task '%s'\n", pcTaskName ? pcTaskName : "?");
    litex_fail("stack");
}

void vAssertCalled(const char *file, int line)
{
    printf("\n*** configASSERT failed: %s:%d\n", file, line);
    litex_fail("assert");
}

/* --------------------------------------------------------------------
 * Halt / reboot
 * ------------------------------------------------------------------ */

void litex_reboot(void)
{
#if defined(CSR_CTRL_BASE)
    ctrl_reset_write(1);
#endif
    for (;;) { }
}

static void halt_with(const char *marker) __attribute__((noreturn));
static void halt_with(const char *marker)
{
    /* Give the UART a moment to drain in case the caller had been
     * printing. Disable IRQs first so this message can't be diced up
     * by a late tick. */
    __asm volatile ("csrc mstatus, 8");
    fputs(marker, stdout);
    fputc('\n', stdout);
#if defined(CSR_UART_BASE)
    while (!uart_txempty_read()) { }
#endif
    for (;;) { }
}

void litex_done(void)
{
    halt_with(PORT_LITEX_DONE_MARKER);
}

void litex_fail(const char *why)
{
    /* Keep the one-liner that the harness scrapes in a fixed shape;
     * optional context goes on the line *before* it. */
    if (why) {
        fputs("[rtos] failing reason: ", stdout);
        fputs(why, stdout);
        fputc('\n', stdout);
    }
    halt_with(PORT_LITEX_FAIL_MARKER);
}
