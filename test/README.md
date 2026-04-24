# Integration tests

Each test builds a firmware with one demo embedded, runs it under
`litex_sim` via `sim/run_sim.py`, and asserts on the captured UART
output via the `[rtos] done` / `[rtos] fail` sentinel markers.

```sh
pip install pytest
pytest -v
```

The first test invocation is slow (Verilator compiles the simulator);
subsequent runs reuse the cached `obj_dir/Vsim` and take seconds.

Individual tests:

| File                 | Demo        | Verifies                                                    |
|----------------------|-------------|-------------------------------------------------------------|
| `test_boot.py`       | blinky_only | banner + kernel version + tick rate on UART                 |
| `test_blinky.py`     | blinky_only | `vTaskDelay` releases the CPU; LED pattern sequence is right |
| `test_queues.py`     | queues      | blocking `xQueueSend`/`xQueueReceive` + FIFO ordering       |
| `test_full_demo.py`  | full_demo   | sensors → queues → fusion (mutex) → display (timer) pipeline, plus UART-RX stream buffer (shell) |
