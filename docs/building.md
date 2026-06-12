# Building

## Host dependencies (Ubuntu 24.04)

```sh
sudo apt-get install -y \
    build-essential \
    gcc-riscv64-unknown-elf \
    picolibc-riscv64-unknown-elf \
    verilator \
    meson ninja-build \
    python3 python3-pip
```

On other distributions: any `riscv64-unknown-elf-gcc` with `rv32im/ilp32`
multilib + picolibc, plus verilator ≥ 4.200 works. The LiteX install
itself is Python and has no platform quirks.

## LiteX on PYTHONPATH

The simplest path is the official `litex_setup.py`:

```sh
mkdir -p ~/litex && cd ~/litex
wget https://raw.githubusercontent.com/enjoy-digital/litex/master/litex_setup.py
chmod +x litex_setup.py
./litex_setup.py --init --install --user --config=standard
```

Confirm with `python3 -c "import litex; print(litex.__file__)"`. The
rest of the flow invokes `litex_sim` as a subprocess, so no extra
environment variables are needed once this import works.

## First build

```sh
git clone --recursive https://github.com/enjoy-digital/freertos-on-litex
cd freertos-on-litex

./sim/gen_soc.py          # generates build/sim/...   (1-2 min)
./sim/run_sim.py          # default demo: full_demo   (first time: ~2 min)
```

The first `run_sim.py` invocation is the slow one: Verilator compiles
the simulator C++ under `build/sim/gateware/obj_dir/`. After that, each
run only rebuilds the firmware and rewrites the `sim_main_ram.init`
memory image — seconds instead of minutes.

## Picking a demo

```sh
./sim/run_sim.py --demo blinky_only         # minimal: one task + vTaskDelay
./sim/run_sim.py --demo queues              # producer / consumer + blocking queue
./sim/run_sim.py --demo full_demo           # all primitives + UART shell (default)
./sim/run_sim.py --demo full_demo --send "tasks"
```

`--send STR` waits for the shell's ready line and types `STR\n` into
the simulated UART. Useful in CI for driving the shell end of the
full_demo without going interactive.

## Interactive mode

For a hands-on shell session:

```sh
./sim/run_sim.py --demo full_demo --keep-running
```

Type `help`, `tasks`, `heap`, `uptime`, `reboot`, `stop`.
