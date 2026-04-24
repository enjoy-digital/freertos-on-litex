/*
 * FreeRTOSConfig.h for LiteX/VexRiscv.
 *
 * Copyright (c) 2026 EnjoyDigital <florent@enjoy-digital.fr>
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

#ifndef __ASSEMBLER__
#include <generated/soc.h>     /* CONFIG_CLOCK_FREQUENCY */
#include <stdint.h>
extern void vAssertCalled(const char *file, int line);
#endif

/* Core --------------------------------------------------------------- */

#define configUSE_PREEMPTION                    1
#define configUSE_TIME_SLICING                  1
#define configUSE_IDLE_HOOK                     0
#define configUSE_TICK_HOOK                     0
#define configUSE_MALLOC_FAILED_HOOK            1
#define configUSE_DAEMON_TASK_STARTUP_HOOK      0
#define configCPU_CLOCK_HZ                      ((unsigned long)CONFIG_CLOCK_FREQUENCY)
#define configTICK_RATE_HZ                      ((TickType_t)1000)
#define configMAX_PRIORITIES                    7
#define configMINIMAL_STACK_SIZE                ((unsigned short)256)
#define configTOTAL_HEAP_SIZE                   ((size_t)(64 * 1024))
#define configMAX_TASK_NAME_LEN                 16
#define configUSE_16_BIT_TICKS                  0
#define configIDLE_SHOULD_YIELD                 1

/* Features ----------------------------------------------------------- */

#define configUSE_MUTEXES                       1
#define configUSE_RECURSIVE_MUTEXES             1
#define configUSE_COUNTING_SEMAPHORES           1
#define configUSE_TASK_NOTIFICATIONS            1
#define configUSE_QUEUE_SETS                    1
#define configQUEUE_REGISTRY_SIZE               8
#define configSUPPORT_STATIC_ALLOCATION         0
#define configSUPPORT_DYNAMIC_ALLOCATION        1
#define configSTACK_DEPTH_TYPE                  uint32_t

/* Software timers ---------------------------------------------------- */

#define configUSE_TIMERS                        1
#define configTIMER_TASK_PRIORITY               (configMAX_PRIORITIES - 2)
#define configTIMER_QUEUE_LENGTH                8
#define configTIMER_TASK_STACK_DEPTH            (configMINIMAL_STACK_SIZE * 2)

/* Hook functions / safety checks ------------------------------------- */

#define configCHECK_FOR_STACK_OVERFLOW          2
#define configRECORD_STACK_HIGH_ADDRESS         1

/* Run-time stats / tracing ------------------------------------------- */

#define configGENERATE_RUN_TIME_STATS           1
#define configUSE_TRACE_FACILITY                1
#define configUSE_STATS_FORMATTING_FUNCTIONS    1

#ifndef __ASSEMBLER__
extern uint32_t ulPortGetRunTimeCounterValue(void);
extern void     vPortSetupTimerInterrupt(void);
#endif
#define portCONFIGURE_TIMER_FOR_RUN_TIME_STATS()    /* shared with tick timer */
#define portGET_RUN_TIME_COUNTER_VALUE()            ulPortGetRunTimeCounterValue()

/* Optional API inclusion --------------------------------------------- */

#define INCLUDE_vTaskPrioritySet                1
#define INCLUDE_uxTaskPriorityGet               1
#define INCLUDE_vTaskDelete                     1
#define INCLUDE_vTaskSuspend                    1
#define INCLUDE_vTaskDelayUntil                 1
#define INCLUDE_vTaskDelay                      1
#define INCLUDE_xTaskGetSchedulerState          1
#define INCLUDE_xTaskGetCurrentTaskHandle       1
#define INCLUDE_uxTaskGetStackHighWaterMark     1
#define INCLUDE_xTaskGetHandle                  1
#define INCLUDE_eTaskGetState                   1

/* RISC-V port specifics ---------------------------------------------- */

/* VexRiscv + LiteX does not expose a CLINT/mtime. The tick interrupt
 * comes through the LiteX IRQ controller (machine external interrupt).
 * Setting these to zero tells the upstream port.c to skip its CLINT-
 * driven tick setup and defer to our weak vPortSetupTimerInterrupt(). */
#define configMTIME_BASE_ADDRESS                0
#define configMTIMECMP_BASE_ADDRESS             0

/* ISR stack sized at 2 KiB — comfortable for nested context-save plus
 * a handful of C frames in application_interrupt_handler. */
#define configISR_STACK_SIZE_WORDS              (512)

/* Asserts ------------------------------------------------------------ */

#define configASSERT(x) \
    do { if (!(x)) vAssertCalled(__FILE__, __LINE__); } while (0)

#endif /* FREERTOS_CONFIG_H */
