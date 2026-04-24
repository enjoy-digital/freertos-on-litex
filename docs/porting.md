# How FreeRTOS is wired to LiteX

This is the tour of the port. Read `docs/building.md` for the
mechanical steps; this page focuses on *why* the code looks the way it
does.

## The FreeRTOS RISC-V port, briefly

The upstream kernel ships a generic RISC-V port at
`portable/GCC/RISC-V/` built around two files:

- `portASM.S` — a trap handler (`freertos_risc_v_trap_handler`) that
  saves the full integer register file on entry, reads `mcause`, and
  dispatches synchronous exceptions vs. asynchronous interrupts.
- `port.c` — `pxPortInitialiseStack`, `xPortStartScheduler`,
  `xPortStartFirstTask`, plus a default CLINT-mtimer tick.

To land it on a board the upstream port requires:

1. A `freertos_risc_v_chip_specific_extensions.h` header picked from
   `portable/GCC/RISC-V/chip_specific_extensions/`. The one that
   matches VexRiscv (no CLINT, no FPU, no extra callee-saved state) is
   `RISCV_no_extensions` — its body is copied verbatim into
   `firmware/freertos_risc_v_chip_specific_extensions.h`. Macros:
   - `portasmHAS_SIFIVE_CLINT = 0`
   - `portasmHAS_MTIME        = 0`
   - `portasmADDITIONAL_CONTEXT_SIZE = 0`
2. An `application_interrupt_handler` (weak) that decides what a
   non-mtimer asynchronous interrupt means. With `portasmHAS_MTIME =
   0`, portASM hands *every* async interrupt to this function. We
   implement it in `firmware/port_litex.c`, reading LiteX's custom
   `CSR_IRQ_PENDING` (0xFC0) and dispatching to the LiteX timer0 and
   UART event blocks.
3. A `vPortSetupTimerInterrupt` (weak in upstream `port.c`) that
   programs whatever periodic source supplies the tick. For us that's
   LiteX's timer0 peripheral — countdown with reload, `zero` event
   unmasked at the LiteX IRQ controller level.

`configMTIME_BASE_ADDRESS = 0` and `configMTIMECMP_BASE_ADDRESS = 0` in
`FreeRTOSConfig.h` tell upstream `port.c` to skip its CLINT-tick path
entirely and leave tick setup to our weak override.

## Boot flow

1. `crt0.S` runs from `_start`:
   - Sets `sp` to `_fstack` (top of `main_ram`).
   - Installs `mtvec = freertos_risc_v_trap_handler`. Safe at boot
     because `mstatus.MIE = 0`.
   - Copies `.data` from LMA to VMA (no-op here — both are in
     `main_ram`), zeroes `.bss`.
   - Sets `mie |= 1<<11` (machine external interrupt enable). LiteX
     routes all peripheral IRQs (timer0, UART) through MEI on
     VexRiscv. The global IE stays 0 until the first task is
     restored.
   - Calls `main()`.

2. `main.c` prints the banner, calls the demo's `app_main()` (which
   creates tasks / queues / timers), then `vTaskStartScheduler()`.

3. `xPortStartScheduler` → `vPortSetupTimerInterrupt` →
   `xPortStartFirstTask`. The last function loads the first task's
   stacked context, sets `mstatus.MIE = 1`, and `ret`s into task code.
   The first tick interrupt fires ~1 ms later.

## Trap dispatch

From the tick onward, every trap (IRQ or exception) lands in
`freertos_risc_v_trap_handler` in upstream `portASM.S`:

```
save full register context onto the current task's stack
read mcause, mepc
if mcause >= 0 (synchronous exception):
    if mcause == 11 (ecall):
        call vTaskSwitchContext          (portYIELD)
    else:
        call freertos_risc_v_application_exception_handler
else (asynchronous interrupt):
    switch to ISR stack
    call freertos_risc_v_application_interrupt_handler
restore context from pxCurrentTCB->stack; mret
```

Our `application_interrupt_handler` is in `port_litex.c`:

```c
pending = irq_pending() & irq_getmask();
while (pending) {
    i = ctz(pending); pending &= pending - 1;
    switch (i) {
      case TIMER0_INTERRUPT: ack timer0_ev_pending; xTaskIncrementTick();
                             if (pdTRUE) xHigher = true;       break;
      case UART_INTERRUPT:   drain UART RX FIFO → stream buffer; break;
    }
}
if (xHigher) vTaskSwitchContext();
```

Because context save/restore is already handled by the surrounding
portASM, the handler body is just ordinary C — no attribute
gymnastics, no hand-rolled register shuffle.

## UART plumbing

We deliberately do **not** call libbase's `uart_init()`. That function
wires a ring-buffer plus `irq_attach` into libbase's own `isr()`
dispatcher, which we bypass entirely. Instead, `port_litex.c`:

- Defines `uart_write`, `uart_read`, `uart_read_nonblock`, `uart_sync`,
  `uart_init` (no-op). Because our object file is linked before
  `libbase.a`, our polling versions win; libbase's `uart.o` is never
  pulled in.
- The polling `uart_write` is safe to call from any context, including
  a task. The UART TX FIFO (16 bytes at 115200 baud) drains fast
  enough that busy-waiting on `uart_txfull_read()` never measurably
  stalls a task.
- For RX, `litex_uart_rx_enable()` creates a FreeRTOS stream buffer,
  enables the UART `RX` event, and unmasks `UART_INTERRUPT` at the
  LiteX IRQ controller. Our `application_interrupt_handler` drains the
  RX FIFO inside the ISR and pushes bytes into the stream buffer via
  `xStreamBufferSendFromISR`. The shell task reads with
  `xStreamBufferReceive(…, portMAX_DELAY)` and thus suspends cleanly
  between keystrokes.

## Run-time stats counter

`configGENERATE_RUN_TIME_STATS = 1` asks for a free-running counter
that ticks faster than the scheduler. LiteX's `--timer-uptime` option
exposes a 64-bit cycle counter at `timer0_uptime_cycles_*`. We return
the low 32 bits from `ulPortGetRunTimeCounterValue`. Good enough: the
percentages from `vTaskGetRunTimeStats` are meaningful relative numbers
even when the absolute counter wraps.

## Memory layout

| Region                  | Size      | Notes                                                 |
|-------------------------|-----------|-------------------------------------------------------|
| `main_ram`              | 16 MiB    | everything: code, rodata, data, bss, heap, stack      |
| FreeRTOS heap_4 arena   | 64 KiB    | `configTOTAL_HEAP_SIZE`                               |
| ISR stack               | 2 KiB     | `configISR_STACK_SIZE_WORDS = 512`                    |
| Per-task stacks         | 1-3 KiB   | allocated from the heap by `xTaskCreate`              |

The integrated SRAM region LiteX defines (8 KiB) is intentionally
unused — too small to host the FreeRTOS heap. All sections land in
`main_ram`.

## Things intentionally *not* ported

- **SMP.** FreeRTOS supports SMP; VexRiscv-SMP exists in LiteX. The
  single-core port is enough to showcase every primitive, and the SMP
  port of FreeRTOS has a distinct `portASM.S` that would need its own
  chip-specific extensions header.
- **`configSUPPORT_STATIC_ALLOCATION`.** Enabling adds `xTaskCreateStatic`
  et al. and a companion set of callbacks. Easy to flip on, but none
  of the current demos need it.
- **FPU context.** VexRiscv "standard" has no hard FPU.
  `configENABLE_FPU = 0`; mstatus.FS bits are never set dirty, so no
  FPU context is saved.
- **Tickless idle.** `configUSE_TICKLESS_IDLE = 0`. Would require a
  LiteX timer0 reload that can be reprogrammed in the ISR-suppress
  window; straightforward on a real board, not worth it in sim where
  wall-clock idle is cheap.
