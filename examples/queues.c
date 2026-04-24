/*
 * Queue demo: a producer task pushes 16 integers (a short Fibonacci
 * sequence) onto a queue, a consumer task pops them and verifies the
 * sequence. Exercises xQueueSend / xQueueReceive with blocking and
 * confirms FIFO ordering across task switches.
 *
 * On success the consumer prints a checksum and exits the run via
 * litex_done(). Any mismatch fails the run.
 *
 * Copyright (c) 2026 EnjoyDigital <florent@enjoy-digital.fr>
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <stdio.h>
#include <stdint.h>

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

#include "port_litex.h"

#define QUEUE_LEN        4         /* deliberately small → producer blocks */
#define SAMPLE_COUNT     16

static QueueHandle_t q;

static void producer_task(void *arg)
{
    (void)arg;

    uint32_t a = 1, b = 1;
    for (int i = 0; i < SAMPLE_COUNT; i++) {
        uint32_t value = a;
        printf("[prod] send=%u\n", (unsigned)value);
        if (xQueueSend(q, &value, portMAX_DELAY) != pdPASS) {
            litex_fail("queue send");
        }
        uint32_t n = a + b;
        a = b;
        b = n;
    }

    vTaskDelete(NULL);
}

static void consumer_task(void *arg)
{
    (void)arg;

    uint32_t expected_a = 1, expected_b = 1;
    uint32_t sum = 0;

    for (int i = 0; i < SAMPLE_COUNT; i++) {
        uint32_t got = 0;
        if (xQueueReceive(q, &got, pdMS_TO_TICKS(5000)) != pdPASS) {
            litex_fail("queue receive");
        }
        if (got != expected_a) {
            printf("[cons] mismatch: got %u expected %u\n",
                   (unsigned)got, (unsigned)expected_a);
            litex_fail("mismatch");
        }
        printf("[cons] recv=%u\n", (unsigned)got);
        sum += got;
        uint32_t n = expected_a + expected_b;
        expected_a = expected_b;
        expected_b = n;
    }

    printf("[cons] sum=%u ok\n", (unsigned)sum);
    litex_done();
}

void app_main(void)
{
    q = xQueueCreate(QUEUE_LEN, sizeof(uint32_t));
    configASSERT(q != NULL);

    xTaskCreate(producer_task, "prod",
                configMINIMAL_STACK_SIZE * 2, NULL,
                tskIDLE_PRIORITY + 1, NULL);

    xTaskCreate(consumer_task, "cons",
                configMINIMAL_STACK_SIZE * 2, NULL,
                tskIDLE_PRIORITY + 2, NULL);
}
