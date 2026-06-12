# Copyright (c) 2026 EnjoyDigital <florent@enjoy-digital.fr>
# SPDX-License-Identifier: BSD-2-Clause

from conftest import run_demo


def test_boot_banner():
    rc, out = run_demo("blinky_only", timeout=240)
    assert rc == 0, f"sim failed (rc={rc}):\n{out}"
    assert "--========= freertos-on-litex =========--" in out, out
    assert "FreeRTOS:" in out
    assert "VexRiscv" in out
    assert "tick: 1000 Hz" in out
