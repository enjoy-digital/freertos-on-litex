/*
 * Boot entry for freertos-on-litex.
 *
 * Prints a banner, hands control to the selected demo's app_main() so
 * it can create tasks, then starts the scheduler. Should never return;
 * vTaskStartScheduler only comes back if it couldn't create the idle
 * task, which means the heap is too small.
 *
 * Copyright (c) 2026 EnjoyDigital <florent@enjoy-digital.fr>
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <stdio.h>

#include <generated/csr.h>
#include <generated/soc.h>

#include "FreeRTOS.h"
#include "task.h"

#include "port_litex.h"

#ifndef FREERTOS_DEMO_NAME
#define FREERTOS_DEMO_NAME "unknown"
#endif

int main(void)
{
    puts("\n--========= freertos-on-litex =========--");
    printf("FreeRTOS:        %s    heap: %u bytes\n",
           tskKERNEL_VERSION_NUMBER,
           (unsigned)configTOTAL_HEAP_SIZE);
    printf("CPU:             %s @ %u Hz   tick: %u Hz\n",
           CONFIG_CPU_HUMAN_NAME,
           (unsigned)CONFIG_CLOCK_FREQUENCY,
           (unsigned)configTICK_RATE_HZ);
    printf("Demo:            %s\n", FREERTOS_DEMO_NAME);
    puts("");

    app_main();

    /* vTaskStartScheduler does not return on success. If it does, the
     * most common cause is a heap too small for the idle + timer tasks. */
    vTaskStartScheduler();
    litex_fail("scheduler returned");
    return 0;
}
