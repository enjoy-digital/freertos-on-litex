# Copyright (c) 2026 EnjoyDigital <florent@enjoy-digital.fr>
# SPDX-License-Identifier: BSD-2-Clause

from conftest import run_demo


def test_full_demo_pipeline():
    """Runs the pipeline unattended — it self-terminates after the
    display timer has fired a fixed number of times. Confirms that
    sensors, fusion (mutex + queue), software timer, watchdog
    (notification + timeout), and the run-time stats trace all come
    through intact."""
    rc, out = run_demo("full_demo", timeout=360)
    assert rc == 0, f"sim failed (rc={rc}):\n{out}"

    # Display timer fired at least as many times as the iteration cap.
    display_lines = [l for l in out.splitlines() if "[display] t=" in l]
    assert len(display_lines) >= 4, f"expected 4+ display cycles, got {len(display_lines)}"

    # Fusion produced samples (each display line reports accumulated samples).
    last = display_lines[-1]
    assert "samples=" in last

    # Runtime-stats trace emitted — confirms configGENERATE_RUN_TIME_STATS
    # plumbing and the Timer service task is alive.
    assert "---- run-time stats ----" in out
    assert "Tmr Svc" in out
    assert "IDLE" in out


def test_full_demo_shell_stop():
    """Sends `stop` over UART once the shell signals ready — exercises
    the UART RX IRQ → stream buffer → shell task path, and confirms a
    clean early shutdown."""
    rc, out = run_demo("full_demo", send="stop", timeout=180)
    assert rc == 0, f"sim failed (rc={rc}):\n{out}"
    assert "[shell] shutting down" in out
