# Copyright (c) 2026 EnjoyDigital <florent@enjoy-digital.fr>
# SPDX-License-Identifier: BSD-2-Clause
#
# Pytest fixtures shared by the simulation tests. Each test invokes
# sim/run_sim.py with a specific demo and asserts on the captured UART
# output.

import subprocess
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parent.parent


def run_demo(demo: str, timeout: float = 300.0, send: str | None = None,
             expect: str | None = None):
    """Build the firmware with `demo` and run it in litex_sim. Returns
    (returncode, captured_output)."""
    cmd = [
        str(REPO_ROOT / "sim" / "run_sim.py"),
        "--demo", demo,
        "--timeout", str(timeout),
    ]
    if send is not None:
        cmd += ["--send", send]
    if expect is not None:
        cmd += ["--expect", expect]
    proc = subprocess.run(
        cmd, capture_output=True, text=True,
        timeout=timeout + 60,
    )
    return proc.returncode, proc.stdout + proc.stderr
