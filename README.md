# freertos-on-litex

```
   ____            ___  __________  ____
  / __/______ ___ / _ \/_  __/ __ \/ __/
 / _// __/ -_) -_) , _/ / / / /_/ /\ \
/_/ /_/  \__/\__/_/|_| /_/  \____/___/
                 __   _ __      _  __
    ___  ___    / /  (_) /____ | |/_/
   / _ \/ _ \  / /__/ / __/ -_)>  <
   \___/_//_/ /____/_/\__/\__/_/|_|

       FreeRTOS on LiteX SoCs, easily!
```

Run [FreeRTOS](https://www.freertos.org/) as the firmware on a
[LiteX](https://github.com/enjoy-digital/litex) SoC, validated
end-to-end in `litex_sim` with a VexRiscv RISC-V soft core, no FPGA
required.

```
--========= freertos-on-litex =========--
FreeRTOS:        V11.3.0    heap: 65536 bytes
CPU:             VexRiscv @ 1000000 Hz   tick: 1000 Hz
Demo:            full_demo

[shell] ready. type 'help' for commands.
[display] t=0.309s  samples=2  temp=19.20C  press=1013.2 hPa
[display] t=0.608s  samples=5  temp=19.53C  press=1012.9 hPa
[display] t=0.908s  samples=7  temp=20.17C  press=1013.3 hPa
[display] t=1.208s  samples=9  temp=19.87C  press=1013.0 hPa
---- run-time stats ----
dispctl         13726           <1%
IDLE            678678          25%
sensor_p        103713          3%
sensor_t        87688           3%
blinky          28494           1%
fusion          184465          6%
watchdog        43583           1%
shell           37151           1%
Tmr Svc         1042822         38%
---- heap free: 44960 / 65536 bytes
[rtos] done
```

Why FreeRTOS? It's the canonical small RTOS: preemptive scheduler,
queues / semaphores / mutexes / software timers / event groups / task
notifications, all fitting in ~10 kB of kernel code. A LiteX SoC is a
neat target for it: a small heap and a couple of peripherals is all
the kernel needs, the VexRiscv port already exists upstream, and the
same firmware binary we exercise in `litex_sim` also runs on an Arty
A7 with no source changes.

## What actually runs on the SoC

```
host side                              |   target side (VexRiscv in sim)
---------------------------------------+----------------------------------
examples/<demo>.c  (creates tasks)     |
   |                                   |
   v                                   |
make / riscv-gcc                       |   firmware.bin loaded to main_ram
   |                                   |        |
   v                                   |        v
firmware.bin                           |   _start -> main() -> app_main()
                                       |        -> xTaskCreate(...)
                                       |        -> vTaskStartScheduler()
                                       |        -> timer0 IRQ = tick source
                                       |        -> tasks preempted, switched,
                                       |           blocked on queues/mutexes
                                       |        -> UART RX IRQ -> stream buffer
                                       |        -> console.log -> UART
```

The port is thin: ~300 lines of LiteX-side C
(`firmware/port_litex.c`) plus a custom 40-line crt0 (`firmware/crt0.S`).
Everything else — the scheduler, memory manager, IPC primitives — is
the unmodified upstream `FreeRTOS-Kernel` submodule at `V11.3.0`. See
[docs/porting.md](docs/porting.md) for the detailed wiring.

## Quick start (simulation)

Requirements: `riscv64-unknown-elf-gcc` with `rv32im/ilp32` multilib,
`verilator`, `meson`/`ninja` (for picolibc), Python 3.10+, and
[LiteX](https://github.com/enjoy-digital/litex/wiki/Installation) on
`PYTHONPATH`. See [docs/building.md](docs/building.md) for exact package
names on Ubuntu.

```sh
git clone --recursive https://github.com/enjoy-digital/freertos-on-litex
cd freertos-on-litex

# 1. Generate the simulated SoC (VexRiscv, 16 MiB main_ram, timer-uptime).
./sim/gen_soc.py

# 2. Build & run the default demo end-to-end.
./sim/run_sim.py
```

The first run takes a couple of minutes while Verilator compiles the
`Vsim` binary. After that `run_sim.py` just rebuilds `firmware.bin`,
swaps `sim_main_ram.init`, and re-launches the cached simulator —
seconds per run.

## Demos

```sh
./sim/run_sim.py --demo blinky_only              # smallest — one task + vTaskDelay
./sim/run_sim.py --demo queues                   # producer / consumer + blocking queue
./sim/run_sim.py --demo full_demo                # default — all primitives + shell
./sim/run_sim.py --demo full_demo --send "tasks" # drive the shell from stdin
```

| Demo           | Primitives exercised                                                                 |
|----------------|---------------------------------------------------------------------------------------|
| `blinky_only`  | `vTaskDelay`, preemption                                                              |
| `queues`       | `xQueueSend` / `xQueueReceive` (blocking both sides), FIFO ordering                   |
| `full_demo`    | 6 tasks + 1 software timer: queues, mutex, task notifications (take/give + timeout), stream buffer (UART RX IRQ), `vTaskList`, `vTaskGetRunTimeStats` |

`full_demo` self-terminates after a bounded number of display cycles,
so unattended runs always hit a `[rtos] done` marker. Pass
`--keep-running` if you want to stay in the shell.

## Shell commands (full_demo)

```
> help
commands: help tasks heap uptime stop reboot
> tasks
Name              State  Prio  Stack  Num
dispctl         B       1       140     7
IDLE            R       0       119     8
sensor_t        B       2       138     1
sensor_p        B       2       135     2
fusion          B       3       136     3
blinky          B       1       198     4
watchdog        B       4       167     5
shell           R       2       158     6
Tmr Svc         B       5       263     9
> heap
free=44960 / 65536 bytes  (lowest watermark=44112)
> uptime
uptime=1.513s  (1513 ticks)
> stop
[shell] shutting down
```

## Tests

```sh
pip install pytest
pytest -v
```

5 tests, each builds a firmware with one demo embedded, runs it under
`litex_sim`, and asserts on the captured UART output via the `[rtos]
done` / `[rtos] fail` markers. See [test/README.md](test/README.md).

## Repository layout

```
firmware/
    main.c                     boot banner + vTaskStartScheduler
    port_litex.c/.h            LiteX IRQ dispatch, tick timer, UART
    crt0.S                     minimal RV32 startup
    linker.ld                  everything in main_ram
    FreeRTOSConfig.h
    freertos_risc_v_chip_specific_extensions.h
    Makefile                   requires BUILD_DIRECTORY=<sim output>
    third_party/FreeRTOS-Kernel   git submodule, pinned V11.3.0
examples/                      app_main() implementations
    blinky_only.c
    queues.c
    full_demo.c
sim/
    gen_soc.py                 one-time SoC generation (VexRiscv)
    run_sim.py                 build + fast re-run harness (2-phase)
test/                          pytest integration tests (drive the sim)
docs/                          longer-form documentation
    building.md                dependencies and build flow
    simulation.md              how the sim harness works
    hardware.md                running on a real board (Arty A7)
    porting.md                 internals: how FreeRTOS is wired to LiteX
.github/workflows/ci.yml       Ubuntu CI runs the full sim test matrix
```

## LiteX hardware bindings

The demos poke CSRs directly through the generated accessors:

```c
leds_out_write(0xa5);                   // CSR_LEDS
uint32_t s = switches_in_read();        // CSR_SWITCHES
ctrl_reset_write(1);                    // CSR_CTRL_RESET
```

All under `#if defined(CSR_*_BASE)` guards — the same firmware binary
builds and runs against both the minimal `litex_sim` (no LEDs) and a
fully-instanced board build.

Adding your own binding is a two-file change: write a function in
`firmware/port_litex.c` that reads / writes the CSR, and call it from
a task in `examples/<your-demo>.c`. See
[docs/porting.md](docs/porting.md).

## Tunables

| Variable (in `firmware/FreeRTOSConfig.h`) | Default  | Notes                                                    |
|-------------------------------------------|----------|----------------------------------------------------------|
| `configTOTAL_HEAP_SIZE`                   | 64 KiB   | heap_4 arena; full_demo uses ~20 KiB                     |
| `configTICK_RATE_HZ`                      | 1000     | tick period = 1 ms simulated                             |
| `configMAX_PRIORITIES`                    | 7        |                                                          |
| `configISR_STACK_SIZE_WORDS`              | 512      | 2 KiB ISR stack                                          |
| `configGENERATE_RUN_TIME_STATS`           | 1        | uses LiteX `timer0_uptime_cycles` as the stats source    |

SoC knobs are exposed by `sim/gen_soc.py`: `--ram-size`, `--output-dir`,
`--force`.

## Caveats

- The simulated VexRiscv runs at **1 MHz**. A `vTaskDelay(200 ms)` is
  200 k cycles, i.e. several seconds of wall-clock time. Real
  hardware (100 MHz Arty) runs proportionally faster.
- First `litex_sim` invocation builds the Verilator simulator
  (`obj_dir/Vsim`), which takes a couple of minutes. `run_sim.py`
  caches it and reuses it across subsequent runs.
- Single-core only. FreeRTOS-SMP works in principle on VexRiscv-SMP
  but requires a different `portASM.S` and chip-specific extensions
  header. See [docs/porting.md](docs/porting.md#things-intentionally-not-ported).

## License

BSD-2-Clause for everything in this repository. The vendored
FreeRTOS-Kernel sources retain their upstream MIT license (see
`firmware/third_party/FreeRTOS-Kernel/LICENSE.md`).
