#!/usr/bin/env python3
"""
Build firmware and run it inside litex_sim (VexRiscv, Verilator).

Usage:
    sim/run_sim.py [--demo NAME] [--timeout SEC] [--output-dir PATH]
                   [--expect STRING] [--keep-running] [--send STRING]

Design note — fast iteration:

    LiteX's default flow regenerates and rebuilds the Verilator
    simulator on every run (build_sim.sh starts with `rm -rf obj_dir/`).
    For a CI test harness that re-runs dozens of times, that's
    prohibitive.

    This script therefore splits the work:
      1. On first use, run `sim/gen_soc.py` plus one full `litex_sim`
         invocation to produce `obj_dir/Vsim` (several minutes).
      2. On subsequent runs, just convert the new firmware.bin into the
         `sim_main_ram.init` memory-image file that Vsim reads at reset
         and re-launch the existing Vsim directly (seconds).

By default the script waits until either:
    * the literal marker `[rtos] done` is seen on UART (exit 0)
    * the literal marker `[rtos] fail` is seen on UART (exit 1)
    * --timeout seconds elapse                          (exit 2)

Use --expect to add an extra pattern that must also appear for success.
--keep-running leaves the simulator attached to stdin/stdout for manual
interactive use (shell demo).

Copyright (c) 2026 EnjoyDigital <florent@enjoy-digital.fr>
SPDX-License-Identifier: BSD-2-Clause
"""
import argparse
import os
import shutil
import signal
import subprocess
import sys
import time
from pathlib import Path

from gen_soc import litex_sim_command


DONE_MARKER = "[rtos] done"
FAIL_MARKER = "[rtos] fail"


def run(cmd, cwd=None, env=None, check=True):
    print(f"[run_sim] $ {' '.join(str(c) for c in cmd)}")
    r = subprocess.run(cmd, cwd=cwd, env=env)
    if check and r.returncode != 0:
        sys.exit(r.returncode)
    return r.returncode


def build_firmware(repo_root: Path, build_dir: Path, demo: str) -> Path:
    fw_dir = repo_root / "firmware"
    cmd = ["make", "-C", str(fw_dir),
           f"BUILD_DIRECTORY={build_dir}",
           f"DEMO={demo}", "-j"]
    run(cmd)
    fw_bin = fw_dir / "firmware.bin"
    if not fw_bin.exists():
        sys.exit("[run_sim] firmware.bin not found after build")
    return fw_bin


def write_mem_init(fw_bin: Path, init_file: Path):
    """Convert firmware.bin to Vsim's text memory-image format: one
    32-bit little-endian word per line, as 8 hex chars."""
    data = fw_bin.read_bytes()
    pad = (-len(data)) % 4
    if pad:
        data += b"\x00" * pad
    with init_file.open("w") as f:
        for i in range(0, len(data), 4):
            w = int.from_bytes(data[i:i + 4], "little")
            f.write(f"{w:08x}\n")


def first_time_sim_build(repo_root: Path, build_dir: Path, fw_bin: Path,
                         ram_size: str) -> Path:
    """Run litex_sim the 'normal' way once to produce obj_dir/Vsim.
    Slow (~2 min on first build). Returns the Vsim path."""
    cmd = litex_sim_command(build_dir, ram_size) + [f"--ram-init={fw_bin}"]
    print("[run_sim] first-time simulator build — this takes a couple of minutes…")
    proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                            text=True, bufsize=1)
    vsim = build_dir / "gateware" / "obj_dir" / "Vsim"
    start = time.time()
    deadline = start + 1800
    try:
        while True:
            line = proc.stdout.readline()
            if line:
                sys.stdout.write(line)
                sys.stdout.flush()
            if vsim.exists() and time.time() - start > 5:
                time.sleep(1.0)
                break
            if proc.poll() is not None:
                break
            if time.time() > deadline:
                print("[run_sim] first-time build timed out")
                break
    finally:
        if proc.poll() is None:
            proc.send_signal(signal.SIGTERM)
            try:
                proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                proc.kill()
    if not vsim.exists():
        sys.exit("[run_sim] Vsim binary was not produced")
    return vsim


def watch(proc, timeout: float, expect: str | None, keep_running: bool,
          send_after: str | None) -> int:
    start = time.time()
    expect_seen = expect is None
    seen_done = False
    seen_fail = False
    sent = send_after is None
    try:
        while True:
            if proc.poll() is not None and proc.stdout.closed:
                break
            line = proc.stdout.readline()
            if not line:
                if proc.poll() is not None:
                    break
                time.sleep(0.01)
                continue
            sys.stdout.write(line)
            sys.stdout.flush()
            if DONE_MARKER in line:
                seen_done = True
                if expect_seen and not keep_running:
                    break
            if FAIL_MARKER in line:
                seen_fail = True
                break
            if expect and expect in line:
                expect_seen = True
                if seen_done and not keep_running:
                    break
            if not sent and "[shell] ready" in line:
                try:
                    proc.stdin.write(send_after + "\n")
                    proc.stdin.flush()
                except (BrokenPipeError, OSError):
                    pass
                sent = True
            if time.time() - start > timeout:
                print(f"\n[run_sim] timeout after {timeout:.1f}s", file=sys.stderr)
                return 2
    finally:
        if proc.poll() is None:
            proc.send_signal(signal.SIGTERM)
            try:
                proc.wait(timeout=3)
            except subprocess.TimeoutExpired:
                proc.kill()

    if seen_fail:
        return 1
    if seen_done and expect_seen:
        return 0
    if expect and not expect_seen:
        print(f"[run_sim] expected string not seen: {expect!r}", file=sys.stderr)
        return 1
    return 1


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--demo", default="full_demo",
                    help="Which firmware/../examples/<name>.c to build. "
                         "Default: full_demo.")
    ap.add_argument("--output-dir", type=Path, default=None)
    ap.add_argument("--ram-size", default="0x01000000")
    ap.add_argument("--timeout", type=float, default=300.0,
                    help="Wall-clock seconds to wait for the DONE marker.")
    ap.add_argument("--expect", default=None,
                    help="Additional string that must appear for success.")
    ap.add_argument("--keep-running", action="store_true",
                    help="Don't stop the simulator on the DONE marker.")
    ap.add_argument("--send", default=None,
                    help="Send this string (plus a newline) to the simulator "
                         "UART once the shell prints its ready banner.")
    args = ap.parse_args()

    repo_root = Path(__file__).resolve().parent.parent
    build_dir = (args.output_dir or (repo_root / "build" / "sim")).resolve()

    if not shutil.which("verilator"):
        sys.exit("[run_sim] verilator not found on PATH")

    marker = build_dir / "software" / "include" / "generated" / "variables.mak"
    if not marker.exists():
        run([sys.executable, str(repo_root / "sim" / "gen_soc.py"),
             f"--output-dir={build_dir}", f"--ram-size={args.ram_size}"])

    fw_bin = build_firmware(repo_root, build_dir, args.demo)

    vsim = build_dir / "gateware" / "obj_dir" / "Vsim"
    if not vsim.exists():
        vsim = first_time_sim_build(repo_root, build_dir, fw_bin, args.ram_size)

    init_file = build_dir / "gateware" / "sim_main_ram.init"
    write_mem_init(fw_bin, init_file)

    print(f"[run_sim] $ {vsim}")
    proc = subprocess.Popen(
        [str(vsim)],
        cwd=str(vsim.parent.parent),
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        bufsize=1,
        text=True,
    )
    rc = watch(proc, args.timeout, args.expect, args.keep_running, args.send)
    print(f"[run_sim] exit {rc}")
    return rc


if __name__ == "__main__":
    sys.exit(main())
