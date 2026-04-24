# Running on real hardware

The firmware is agnostic about whether it runs in `litex_sim` or on an
FPGA — the glue only reads `CSR_*` / `CONFIG_*` definitions that LiteX
emits for both targets. The only assumptions are:

- CPU is VexRiscv "standard" or compatible (any variant that exposes
  LiteX's `CSR_IRQ_MASK` / `CSR_IRQ_PENDING` CSRs).
- `timer0` is present and routed to `TIMER0_INTERRUPT`.
- `uart` is present and routed to `UART_INTERRUPT`.
- At least ~80 KiB of RAM (64 KiB heap + stacks + code; a 512 KiB
  main_ram is comfortable).

The reference target used during bring-up is the Digilent Arty A7-35T
— it's what most LiteX docs assume and has the right mix of LEDs,
switches, and a built-in UART bridge.

## Building for Arty A7

```sh
# 1. Generate an Arty SoC with the same CPU + peripherals settings the
#    firmware expects. Adjust --cpu-type / --uart-name / --with-ethernet
#    as you wish; FreeRTOS doesn't care about the peripheral set as long
#    as timer0 + uart are routed.
python3 -m litex_boards.targets.digilent_arty \
    --build \
    --cpu-type vexriscv \
    --integrated-main-ram-size 0x10000 \
    --timer-uptime \
    --output-dir build/arty

# 2. Build the firmware against that SoC output tree.
make -C firmware BUILD_DIRECTORY=$(pwd)/build/arty DEMO=full_demo

# 3. Load the bitstream, load the firmware.
openFPGALoader --board arty_a7_35 build/arty/gateware/digilent_arty.bit
litex_term --kernel firmware/firmware.bin /dev/ttyUSB1
```

Once `litex_term` connects you should see the same boot banner and
demo output as in the simulator. The only visible difference is that
wall-clock time matches tick time (a 500 ms `vTaskDelay` actually takes
500 ms), so the `full_demo` display loop prints once a second rather
than once per several seconds.

## LEDs and switches

`examples/blinky_only.c` and `examples/full_demo.c` write to
`leds_out` via `leds_out_write()` only if `CSR_LEDS_BASE` is defined,
so the same binary works against SoCs with or without LEDs (e.g. the
simulator doesn't bring up LEDs by default). On Arty the 4 on-board
LEDs reflect the low 4 bits of the rotating pattern.

## Clock / tick rate

`configCPU_CLOCK_HZ` reads `CONFIG_CLOCK_FREQUENCY` from
`generated/soc.h`, which LiteX populates with the effective sys_clk
frequency. The default Arty A7 build uses 100 MHz. `configTICK_RATE_HZ`
stays at 1 kHz; `vPortSetupTimerInterrupt` computes the timer0 reload
as `configCPU_CLOCK_HZ / configTICK_RATE_HZ`, so the tick is correct
regardless of the platform clock.

## Caveats on real hardware

- The shell command `reboot` writes `CSR_CTRL_RESET`; this only
  resets the SoC if LiteX's ctrl module was instantiated with reset
  enabled (the default on most target files).
- The runtime-stats counter (`ulPortGetRunTimeCounterValue`) wraps
  every `2^32 / CONFIG_CLOCK_FREQUENCY` seconds. On a 100 MHz Arty
  that's ~43 s. The relative `vTaskGetRunTimeStats` percentages remain
  meaningful through the wrap.
- No SD card / SPI flash bindings are wired. Adding them is an
  exercise in creating a FreeRTOS task that talks to the respective
  LiteX CSRs and presenting a clean API to the shell.
