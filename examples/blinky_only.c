/*
 * Smallest possible FreeRTOS demo: one task toggles the LEDs at a
 * periodic delay and prints a counter. The test harness counts these
 * lines to confirm the scheduler is preempting and vTaskDelay is
 * releasing the CPU to idle.
 *
 * Copyright (c) 2026 EnjoyDigital <florent@enjoy-digital.fr>
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <stdio.h>
#include <stdint.h>

#include <generated/csr.h>

#include "FreeRTOS.h"
#include "task.h"

#include "port_litex.h"

/* Kept short enough to hit the iteration count inside a typical sim
 * timeout (see test/test_blinky.py). */
#define BLINK_PERIOD_MS    200
#define BLINK_ITERATIONS   8

static void blinky_task(void *arg)
{
    (void)arg;
    uint32_t pattern = 0x1;

    for (int i = 0; i < BLINK_ITERATIONS; i++) {
#if defined(CSR_LEDS_BASE)
        leds_out_write(pattern);
#endif
        printf("[blinky] tick=%d pattern=0x%02x\n",
               i, (unsigned)pattern);
        pattern = (pattern << 1) | (pattern >> 7);
        pattern &= 0xff;
        vTaskDelay(pdMS_TO_TICKS(BLINK_PERIOD_MS));
    }

    litex_done();
}

void app_main(void)
{
    BaseType_t rc = xTaskCreate(
        blinky_task, "blinky",
        configMINIMAL_STACK_SIZE * 2, NULL,
        tskIDLE_PRIORITY + 1, NULL);

    configASSERT(rc == pdPASS);
    (void)rc;
}
