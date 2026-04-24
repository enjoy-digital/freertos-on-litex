/*
 * Representative FreeRTOS demo — a small sensor-fusion pipeline plus a
 * UART shell. Exercises every major kernel primitive at least once:
 *
 *   sensor_temp, sensor_press  -- vTaskDelay + xQueueSend
 *   fusion                     -- xQueueReceive + xSemaphoreTake
 *                                 (mutex) + xTaskNotifyGive
 *   display                    -- software timer callback + mutex read
 *   watchdog                   -- xTaskNotifyTake with timeout
 *   blinky                     -- vTaskDelay, LED CSR
 *   shell                      -- stream buffer (UART RX IRQ) + vTaskGetRunTimeStats
 *
 * After DISPLAY_ITERATIONS display cycles the display task prints the
 * run-time stats and signals end-of-run via litex_done(). This keeps
 * unattended sim invocations (CI) bounded without depending on the
 * shell receiving a `stop` command.
 *
 * Copyright (c) 2026 EnjoyDigital <florent@enjoy-digital.fr>
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include <generated/csr.h>

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"
#include "timers.h"

#include "port_litex.h"

/* ----- tunables ---------------------------------------------------- */

#define SENSOR_PERIOD_MS      100    /* per sensor, simulated time */
#define DISPLAY_PERIOD_MS     300
#define BLINKY_PERIOD_MS      150
#define WATCHDOG_TIMEOUT_MS   (DISPLAY_PERIOD_MS * 4)
#define DISPLAY_ITERATIONS    4

/* ----- shared state ------------------------------------------------ */

typedef struct {
    int      temp_c_q8;     /* Q8.8 so we can print a .2f without FPU  */
    uint32_t press_hpa;
    uint32_t samples;
} fusion_state_t;

static QueueHandle_t     q_temp;
static QueueHandle_t     q_press;
static SemaphoreHandle_t fusion_mutex;
static TimerHandle_t     display_timer;
static TaskHandle_t      watchdog_handle;
static TaskHandle_t      display_handle;
static fusion_state_t    fusion;
static volatile uint32_t display_cycles = 0;

/* ----- sensors ----------------------------------------------------- */

static void sensor_temp_task(void *arg)
{
    (void)arg;
    /* Pseudo-random-ish walk around 21.0 °C. */
    int q8 = 21 * 256;
    uint32_t seed = 0xC0FFEEu;
    for (;;) {
        seed = seed * 1664525u + 1013904223u;
        q8 += ((int)(seed & 0x3F)) - 32; /* ±~0.12 °C per tick */
        int v = q8;
        xQueueSend(q_temp, &v, portMAX_DELAY);
        vTaskDelay(pdMS_TO_TICKS(SENSOR_PERIOD_MS));
    }
}

static void sensor_press_task(void *arg)
{
    (void)arg;
    uint32_t p = 1013 * 10;  /* deci-hPa, i.e. 1013.0 */
    uint32_t seed = 0xBEEFu;
    for (;;) {
        seed = seed * 1103515245u + 12345u;
        p += ((int)(seed & 0x7)) - 3;
        xQueueSend(q_press, &p, portMAX_DELAY);
        vTaskDelay(pdMS_TO_TICKS(SENSOR_PERIOD_MS));
    }
}

/* ----- fusion ------------------------------------------------------ */

static void fusion_task(void *arg)
{
    (void)arg;
    int      acc_t = 0;
    uint32_t acc_p = 0;
    uint32_t n = 0;

    for (;;) {
        int t;
        uint32_t p;
        if (xQueueReceive(q_temp,  &t, pdMS_TO_TICKS(1000)) != pdTRUE) continue;
        if (xQueueReceive(q_press, &p, pdMS_TO_TICKS(1000)) != pdTRUE) continue;

        /* Simple moving average over the last 4 samples. */
        acc_t = (acc_t * 3 + t) / 4;
        acc_p = (acc_p * 3 + p) / 4;
        n++;

        xSemaphoreTake(fusion_mutex, portMAX_DELAY);
        fusion.temp_c_q8 = acc_t;
        fusion.press_hpa = acc_p;        /* in deci-hPa */
        fusion.samples   = n;
        xSemaphoreGive(fusion_mutex);

        /* Heartbeat to the watchdog. */
        xTaskNotifyGive(watchdog_handle);
    }
}

/* ----- display (software timer) ------------------------------------ */

static void display_cb(TimerHandle_t t)
{
    (void)t;

    fusion_state_t snap;
    xSemaphoreTake(fusion_mutex, portMAX_DELAY);
    snap = fusion;
    xSemaphoreGive(fusion_mutex);

    int tc_whole = snap.temp_c_q8 / 256;
    int tc_frac  = ((snap.temp_c_q8 % 256) * 100) / 256;
    if (tc_frac < 0) tc_frac = -tc_frac;

    uint32_t p_whole = snap.press_hpa / 10;
    uint32_t p_frac  = snap.press_hpa % 10;

    uint32_t ms = (uint32_t)xTaskGetTickCount() * (1000u / configTICK_RATE_HZ);
    printf("[display] t=%u.%03us  samples=%u  temp=%d.%02dC  press=%u.%u hPa\n",
           (unsigned)(ms / 1000), (unsigned)(ms % 1000),
           (unsigned)snap.samples,
           tc_whole, tc_frac,
           (unsigned)p_whole, (unsigned)p_frac);

    display_cycles++;
    if (display_cycles >= DISPLAY_ITERATIONS) {
        /* Stop from the timer service task so we're off the display's
         * own timer callback before deleting the timer itself. */
        xTimerStop(display_timer, 0);
        xTaskNotifyGive(display_handle);
    }
}

/* When the display reaches its iteration count, this idle task wakes
 * and performs the orderly shutdown. Keeping the shutdown out of the
 * timer callback avoids deleting the timer from its own context. */
static void display_shutdown_task(void *arg)
{
    (void)arg;
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

    /* Print a tiny task / heap summary before exiting — gives the user
     * something to inspect and also exercises the trace facility. */
    static char buf[512];
    vTaskGetRunTimeStats(buf);
    puts("---- run-time stats ----");
    fputs(buf, stdout);
    printf("---- heap free: %u / %u bytes\n",
           (unsigned)xPortGetFreeHeapSize(),
           (unsigned)configTOTAL_HEAP_SIZE);

    litex_done();
}

/* ----- watchdog ---------------------------------------------------- */

static void watchdog_task(void *arg)
{
    (void)arg;
    for (;;) {
        uint32_t got = ulTaskNotifyTake(pdTRUE,
                                        pdMS_TO_TICKS(WATCHDOG_TIMEOUT_MS));
        if (got == 0) {
            puts("[watchdog] fusion starvation — no notification in window");
            /* In a real system we'd reset here; in the demo we just log
             * and keep watching so the run still terminates cleanly. */
        }
    }
}

/* ----- blinky ------------------------------------------------------ */

static void blinky_task(void *arg)
{
    (void)arg;
    uint32_t pattern = 0x1;
    for (;;) {
#if defined(CSR_LEDS_BASE)
        leds_out_write(pattern);
#endif
        pattern = ((pattern << 1) | (pattern >> 7)) & 0xff;
        vTaskDelay(pdMS_TO_TICKS(BLINKY_PERIOD_MS));
    }
}

/* ----- shell ------------------------------------------------------- */

static void shell_handle_line(const char *line)
{
    if (strcmp(line, "help") == 0 || strcmp(line, "?") == 0) {
        puts("commands: help tasks heap uptime stop reboot");
    } else if (strcmp(line, "tasks") == 0) {
        static char buf[512];
        vTaskList(buf);
        fputs("Name              State  Prio  Stack  Num\n", stdout);
        fputs(buf, stdout);
    } else if (strcmp(line, "heap") == 0) {
        printf("free=%u / %u bytes  (lowest watermark=%u)\n",
               (unsigned)xPortGetFreeHeapSize(),
               (unsigned)configTOTAL_HEAP_SIZE,
               (unsigned)xPortGetMinimumEverFreeHeapSize());
    } else if (strcmp(line, "uptime") == 0) {
        uint32_t ticks = (uint32_t)xTaskGetTickCount();
        uint32_t ms    = ticks * (1000u / configTICK_RATE_HZ);
        printf("uptime=%u.%03us  (%u ticks)\n",
               (unsigned)(ms / 1000), (unsigned)(ms % 1000),
               (unsigned)ticks);
    } else if (strcmp(line, "stop") == 0) {
        puts("[shell] shutting down");
        litex_done();
    } else if (strcmp(line, "reboot") == 0) {
        puts("[shell] rebooting");
        litex_reboot();
    } else if (line[0] != '\0') {
        printf("unknown command: %s\n", line);
    }
}

static void shell_task(void *arg)
{
    (void)arg;
    litex_uart_rx_enable();
    puts("[shell] ready. type 'help' for commands.");

    char line[64];
    size_t len = 0;

    for (;;) {
        char c = litex_uart_getc();
        if (c == '\r' || c == '\n') {
            fputc('\n', stdout);
            line[len] = 0;
            shell_handle_line(line);
            len = 0;
            fputs("> ", stdout);
        } else if (c == 0x7f || c == 0x08) {
            if (len > 0) {
                len--;
                fputs("\b \b", stdout);
            }
        } else if (c >= 0x20 && c < 0x7f && len < sizeof(line) - 1) {
            line[len++] = c;
            fputc(c, stdout);
        }
    }
}

/* ----- entry ------------------------------------------------------- */

void app_main(void)
{
    q_temp  = xQueueCreate(4, sizeof(int));
    q_press = xQueueCreate(4, sizeof(uint32_t));
    fusion_mutex = xSemaphoreCreateMutex();
    configASSERT(q_temp && q_press && fusion_mutex);

    xTaskCreate(sensor_temp_task,  "sensor_t", configMINIMAL_STACK_SIZE * 2,
                NULL, tskIDLE_PRIORITY + 2, NULL);
    xTaskCreate(sensor_press_task, "sensor_p", configMINIMAL_STACK_SIZE * 2,
                NULL, tskIDLE_PRIORITY + 2, NULL);
    xTaskCreate(fusion_task,       "fusion",   configMINIMAL_STACK_SIZE * 2,
                NULL, tskIDLE_PRIORITY + 3, NULL);
    xTaskCreate(blinky_task,       "blinky",   configMINIMAL_STACK_SIZE * 2,
                NULL, tskIDLE_PRIORITY + 1, NULL);
    xTaskCreate(watchdog_task,     "watchdog", configMINIMAL_STACK_SIZE * 2,
                NULL, tskIDLE_PRIORITY + 4, &watchdog_handle);
    xTaskCreate(shell_task,        "shell",    configMINIMAL_STACK_SIZE * 3,
                NULL, tskIDLE_PRIORITY + 2, NULL);
    xTaskCreate(display_shutdown_task, "dispctl", configMINIMAL_STACK_SIZE * 2,
                NULL, tskIDLE_PRIORITY + 1, &display_handle);

    display_timer = xTimerCreate(
        "display",
        pdMS_TO_TICKS(DISPLAY_PERIOD_MS),
        pdTRUE,      /* auto-reload */
        NULL,
        display_cb);
    configASSERT(display_timer != NULL);
    xTimerStart(display_timer, 0);
}
