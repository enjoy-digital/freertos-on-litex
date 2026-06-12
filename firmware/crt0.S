/*
 * Minimal RV32 startup for freertos-on-litex.
 *
 * Differs from LiteX's stock crt0 in two ways:
 *   - No `trap_entry` + `isr()` path. mtvec is set straight to the
 *     FreeRTOS trap handler; until the scheduler starts (which flips
 *     mstatus.MIE), no interrupt can fire, so this is safe from boot.
 *   - No reliance on libbase's `isr()` symbol, keeping the link set
 *     small and independent of LiteX's interrupt-table machinery.
 *
 * Copyright (c) 2026 EnjoyDigital <florent@enjoy-digital.fr>
 * SPDX-License-Identifier: BSD-2-Clause
 */

.global _start
.global main
.extern freertos_risc_v_trap_handler

_start:
    la sp, _fstack

    /* Install the FreeRTOS trap handler directly. mstatus.MIE is still
     * 0 at this point, so no interrupt can occur until the scheduler's
     * xPortStartFirstTask does `mret` with MPIE=1. */
    la a0, freertos_risc_v_trap_handler
    csrw mtvec, a0

    /* data init. With .data in main_ram and LMA == VMA this is a no-op
     * but the loop is harmless and keeps the linker script flexible. */
    la a0, _fdata
    la a1, _edata
    la a2, _fdata_rom
1:  beq  a0, a1, 2f
    lw   a3, 0(a2)
    sw   a3, 0(a0)
    addi a0, a0, 4
    addi a2, a2, 4
    j 1b
2:

    /* bss clear */
    la a0, _fbss
    la a1, _ebss
3:  beq  a0, a1, 4f
    sw   zero, 0(a0)
    addi a0, a0, 4
    j 3b
4:

    /* Enable machine external interrupts (bit 11). LiteX routes all
     * peripheral IRQs, including our tick source (timer0), through MEI
     * on VexRiscv. mstatus.MIE is flipped later by FreeRTOS. */
    li a0, 0x800
    csrw mie, a0

    call main

5:  j 5b
