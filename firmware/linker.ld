/* Copyright (c) 2026 EnjoyDigital <florent@enjoy-digital.fr>
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Everything lives in main_ram: code, rodata, data, bss, FreeRTOS heap,
 * ISR stack, main/boot stack. The LiteX integrated SRAM region is too
 * small (~8 KiB) to host the FreeRTOS heap, so we do not use it.
 *
 * Section layout note: .data uses `.data : ALIGN(16)` rather than
 * `. = ALIGN(16); .data : { ... }`. The latter moves the alignment
 * padding *inside* the section, so LOADADDR(.data) != _fdata and
 * crt0's copy loop silently clobbers the first words of .data (most
 * visibly picolibc's stdout FILE object). Keep the alignment outside.
 */

INCLUDE generated/output_format.ld
ENTRY(_start)

__DYNAMIC = 0;

INCLUDE generated/regions.ld

SECTIONS
{
    .text :
    {
        _ftext = .;
        *crt0*(.text)
        KEEP(*crt0*(.text))
        KEEP(*(.text.isr))
        KEEP(*(.text.freertos_risc_v_trap_handler))
        KEEP(*(.text.freertos_risc_v_exception_handler))
        KEEP(*(.text.freertos_risc_v_interrupt_handler))
        KEEP(*(.text.freertos_risc_v_mtimer_interrupt_handler))
        *(.text .stub .text.* .gnu.linkonce.t.*)
        _etext = .;
    } > main_ram

    .rodata :
    {
        . = ALIGN(8);
        _frodata = .;
        *(.rodata .rodata.* .gnu.linkonce.r.*)
        *(.rodata1)
        *(.got .got.*)
        *(.toc .toc.*)
        . = ALIGN(8);
        _erodata = .;
    } > main_ram

    .data : ALIGN(16)
    {
        _fdata = .;
        *(.data .data.* .gnu.linkonce.d.*)
        *(.data1)
        _gp = ALIGN(16);
        *(.sdata .sdata.* .gnu.linkonce.s.*)
        . = ALIGN(8);
        _edata = .;
    } > main_ram

    .bss : ALIGN(8)
    {
        _fbss = .;
        *(.dynsbss)
        *(.sbss .sbss.* .gnu.linkonce.sb.*)
        *(.scommon)
        *(.dynbss)
        *(.bss .bss.* .gnu.linkonce.b.*)
        *(COMMON)
        . = ALIGN(8);
        _ebss = .;
        _end = .;
    } > main_ram
}

/* `.data` is already in main_ram (init-in-place); the load/virtual
 * addresses are equal, so crt0's copy loop is a no-op. Keep these
 * symbols defined so crt0 still links. */
PROVIDE(_fdata_rom = LOADADDR(.data));
PROVIDE(_edata_rom = LOADADDR(.data) + SIZEOF(.data));

/* Main/boot stack grows down from the top of main_ram. Once the
 * scheduler starts, tasks run on their own stacks allocated from the
 * FreeRTOS heap, so we only need enough headroom here for the pre-
 * scheduler banner + task creation. */
PROVIDE(_fstack = ORIGIN(main_ram) + LENGTH(main_ram) - 4);

PROVIDE(_emain_ram = ORIGIN(main_ram) + LENGTH(main_ram));
