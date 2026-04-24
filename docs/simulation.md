# Simulation

## Two-phase flow

LiteX's default `litex_sim` flow regenerates and rebuilds the Verilator
simulator on every run (`build_sim.sh` starts with `rm -rf obj_dir/`).
For a test harness that re-runs dozens of times per CI pass, that's
prohibitive, so `sim/run_sim.py` splits the work:

1. **First invocation** — run the full `litex_sim` pipeline once, which
   produces `build/sim/gateware/obj_dir/Vsim` (a compiled Verilator
   simulator, ~50 MB).
2. **Subsequent invocations** — rebuild `firmware.bin`, convert it to
   the text memory-image file Vsim reads at reset
   (`sim_main_ram.init`), and re-launch the cached `Vsim` directly.
   Seconds instead of minutes.

The harness watches UART output for the sentinel markers:

- `[rtos] done` — firmware finished successfully (exit 0)
- `[rtos] fail` — firmware reported an error (exit 1)
- `--timeout` elapsed — exit 2

Use `--expect STR` to also require a specific string in the output
before declaring success. Use `--keep-running` to stay attached after
`done` (useful for interactive shell exploration).

## SoC options

`sim/gen_soc.py` accepts:

- `--ram-size HEX` — size of `integrated_main_ram` (default
  `0x01000000` = 16 MiB). A full_demo build fits comfortably in 256
  KiB, so shrinking is possible.
- `--output-dir PATH` — where to drop the generated SoC (default
  `build/sim`).
- `--force` — regenerate even if the output directory looks valid.

If you need a different CPU variant or peripherals, edit `gen_soc.py`
(or wrap it); it just shells out to `python -m litex.tools.litex_sim`
and all of that tool's flags are available.

## The `sim_main_ram.init` format

One 32-bit little-endian word per line, as 8 hex characters:

```python
for i in range(0, len(firmware_bin), 4):
    w = int.from_bytes(firmware_bin[i:i+4], "little")
    f.write(f"{w:08x}\n")
```

Vsim reads this file at startup via `$readmemh`. The main_ram base is
set by LiteX (`MAIN_RAM_BASE = 0x40000000`), so the first word in the
file is placed at `main_ram[0]` — the firmware's `_start` reset vector.

## Determinism

The simulated system clock is 1 MHz — slow for wall-clock comparisons
but identical from run to run. Each FreeRTOS tick therefore represents
1000 cycles of simulated time; a `vTaskDelay(pdMS_TO_TICKS(200))` is
200 k cycles, which is ~several seconds of wall time under Verilator.

## Driving the shell from Python

`sim/run_sim.py --send "STR"` waits for `[shell] ready` on UART, then
writes `STR\n` to the simulator's stdin. Under the hood the Vsim model
wires stdin → UART RX, so the string arrives at the shell task through
the same stream buffer a real keyboard would use.

The test in `test/test_full_demo.py::test_full_demo_shell_stop` uses
this path to type `stop` and confirm the clean-shutdown marker.

## Hooks for your own tests

`test/conftest.py` exposes `run_demo(name, timeout=..., send=..., expect=...)`
which handles the build + sim cycle and returns `(returncode, captured)`.
Drop a new `test_*.py` next to the existing ones and pytest picks it up
automatically.
