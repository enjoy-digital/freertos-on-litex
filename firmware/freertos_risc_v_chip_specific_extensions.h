/*
 * LiteX/VexRiscv chip-specific extensions for the FreeRTOS RISC-V port.
 *
 * VexRiscv (as configured by LiteX for the "standard" variant) has no
 * CLINT and no memory-mapped mtime, no FPU and no additional callee-
 * saved CSRs beyond the standard integer register file. So:
 *
 *   - portasmHAS_MTIME          = 0   (tick interrupt comes from LiteX
 *                                       timer0 via the external IRQ,
 *                                       not from a memory-mapped mtimer).
 *   - portasmHAS_SIFIVE_CLINT   = 0
 *   - portasmADDITIONAL_CONTEXT_SIZE = 0
 *
 * The SAVE/RESTORE macros are empty.
 *
 * Copyright (c) 2026 EnjoyDigital <florent@enjoy-digital.fr>
 * SPDX-License-Identifier: BSD-2-Clause
 * (Derived from FreeRTOS-Kernel's RISCV_no_extensions variant, MIT.)
 */

#ifndef __FREERTOS_RISC_V_EXTENSIONS_H__
#define __FREERTOS_RISC_V_EXTENSIONS_H__

#define portasmHAS_SIFIVE_CLINT           0
#define portasmHAS_MTIME                  0
#define portasmADDITIONAL_CONTEXT_SIZE    0

.macro portasmSAVE_ADDITIONAL_REGISTERS
    .endm

    .macro portasmRESTORE_ADDITIONAL_REGISTERS
    .endm

#endif /* __FREERTOS_RISC_V_EXTENSIONS_H__ */
