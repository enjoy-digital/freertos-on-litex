# Copyright (c) 2026 EnjoyDigital <florent@enjoy-digital.fr>
# SPDX-License-Identifier: BSD-2-Clause

from conftest import run_demo


def test_blinky_preemption():
    """Confirms that vTaskDelay actually yields and the tick interrupt
    is driving the scheduler. If either were broken the blinky task
    would either never run past iteration 0 (no tick → no wake) or run
    back-to-back without delay (no preemption)."""
    rc, out = run_demo("blinky_only", timeout=240)
    assert rc == 0, f"sim failed (rc={rc}):\n{out}"

    # One line per blink iteration, exactly.
    lines = [l for l in out.splitlines() if "[blinky] tick=" in l]
    assert len(lines) == 8, f"expected 8 blink lines, got {len(lines)}:\n{out}"

    # The LED rotation pattern should walk 0x01 → 0x02 → 0x04 … → 0x80.
    expected = [1 << i for i in range(8)]
    observed = [int(l.split("pattern=0x", 1)[1], 16) for l in lines]
    assert observed == expected, f"pattern sequence off: {observed}"
