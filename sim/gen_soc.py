#!/usr/bin/env python3
"""
Generate a LiteX/VexRiscv simulator SoC suitable for freertos-on-litex.

Usage:
    sim/gen_soc.py [--output-dir PATH] [--ram-size BYTES] [--force]

Defaults:
    --output-dir  build/sim        (relative to repo root)
    --ram-size    0x01000000       (16 MiB)

Skips regeneration if the directory already has
`software/include/generated/variables.mak` unless --force is passed.

First run is slow (Verilator compiles the simulator C++); subsequent
runs are fast.

Copyright (c) 2026 EnjoyDigital <florent@enjoy-digital.fr>
SPDX-License-Identifier: BSD-2-Clause
"""
import argparse
import subprocess
import sys
from pathlib import Path


def main():
    repo_root = Path(__file__).resolve().parent.parent

    ap = argparse.ArgumentParser()
    ap.add_argument("--output-dir", default=str(repo_root / "build" / "sim"))
    ap.add_argument("--ram-size",   default="0x01000000",
                    help="Integrated main-RAM size in bytes (hex ok). Default 16 MiB.")
    ap.add_argument("--force", action="store_true",
                    help="Regenerate even if the output directory already looks valid.")
    args = ap.parse_args()

    out = Path(args.output_dir).resolve()
    marker = out / "software" / "include" / "generated" / "variables.mak"
    if marker.exists() and not args.force:
        print(f"[gen_soc] reusing existing SoC at {out}")
        return 0

    out.mkdir(parents=True, exist_ok=True)

    cmd = [
        sys.executable, "-m", "litex.tools.litex_sim",
        "--cpu-type=vexriscv",
        f"--integrated-main-ram-size={args.ram_size}",
        "--libc-mode=full",
        # FreeRTOS uses timer0 as the tick source; --timer-uptime also
        # gives us a 64-bit uptime counter we reuse for the run-time
        # stats (see port_litex.c::ulPortGetRunTimeCounterValue).
        "--timer-uptime",
        f"--output-dir={out}",
        "--no-compile-gateware",
        "--non-interactive",
    ]
    print("[gen_soc]", " ".join(cmd))
    return subprocess.call(cmd)


if __name__ == "__main__":
    sys.exit(main())
