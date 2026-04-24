# Copyright (c) 2026 EnjoyDigital <florent@enjoy-digital.fr>
# SPDX-License-Identifier: BSD-2-Clause

from conftest import run_demo


def test_producer_consumer_fifo():
    """The producer task pushes a 16-element Fibonacci sequence onto a
    4-slot queue; the consumer pops and verifies the sequence. Tests
    both blocking xQueueSend (producer blocks once the queue is full)
    and FIFO ordering across task switches."""
    rc, out = run_demo("queues", timeout=240)
    assert rc == 0, f"sim failed (rc={rc}):\n{out}"

    sent = [int(l.split("send=", 1)[1]) for l in out.splitlines()
            if "[prod] send=" in l]
    recv = [int(l.split("recv=", 1)[1]) for l in out.splitlines()
            if "[cons] recv=" in l]

    assert sent == recv, f"FIFO order broken\nsent={sent}\nrecv={recv}"
    assert len(sent) == 16, f"expected 16 samples, got {len(sent)}"

    # Fib(1..16) checksum.
    assert f"[cons] sum={sum(sent)} ok" in out
