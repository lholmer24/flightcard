/*
 * Flightcard - FreeRTOS SMP configuration for RP2350.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

#ifndef FC_SMP
#define FC_SMP 1
#endif

#if FC_SMP
#define configNUMBER_OF_CORES                2
#define configNUM_CORES                      configNUMBER_OF_CORES
#define configUSE_CORE_AFFINITY               1
#define configTICK_CORE                       0
#define configRUN_MULTIPLE_PRIORITIES         1
#else
#define configNUMBER_OF_CORES                1
#define configNUM_CORES                      1
#define configUSE_CORE_AFFINITY               0
#endif

#define configUSE_PREEMPTION                  1
#define configUSE_TIME_SLICING                1
#define configUSE_PORT_OPTIMISED_TASK_SELECTION 0
#define configUSE_TICKLESS_IDLE               0
#define configTICK_RATE_HZ                    1000
#define configMAX_PRIORITIES                  16
#define configMINIMAL_STACK_SIZE              512

#define configSUPPORT_DYNAMIC_ALLOCATION      1
#define configSUPPORT_STATIC_ALLOCATION       0
#define configTOTAL_HEAP_SIZE                 (128 * 1024)
#define configAPPLICATION_ALLOCATED_HEAP      0
#define configUSE_MALLOC_FAILED_HOOK          1

#define configUSE_IDLE_HOOK                   0
#define configIDLE_SHOULD_YIELD               1
#define configUSE_DAEMON_TASK_STARTUP_HOOK    0
#define configUSE_APPLICATION_TASK_TAG        0
#define configUSE_PASSIVE_IDLE_HOOK           0
#define configUSE_TICK_HOOK                   0
#define configCHECK_FOR_STACK_OVERFLOW        2
#define configUSE_RECURSIVE_MUTEXES           1
#define configUSE_MUTEXES                     1
#define configUSE_COUNTING_SEMAPHORES         1
#define configUSE_TASK_NOTIFICATIONS          1
#define configTASK_NOTIFICATION_ARRAY_ENTRIES 1
#define configQUEUE_REGISTRY_SIZE             8
#define configUSE_QUEUE_SETS                  1
#define configUSE_NEWLIB_REENTRANT            0
#define configENABLE_BACKWARD_COMPATIBILITY   1
#define configNUM_THREAD_LOCAL_STORAGE_POINTERS 0

#define configUSE_TIMERS                      1
#define configTIMER_TASK_PRIORITY             (configMAX_PRIORITIES - 1)
#define configTIMER_QUEUE_LENGTH              8
#define configTIMER_TASK_STACK_DEPTH          1024

#define configUSE_TRACE_FACILITY              1
#define INCLUDE_uxTaskGetStackHighWaterMark   1
#define INCLUDE_vTaskDelete                   1
#define INCLUDE_vTaskDelay                    1
#define INCLUDE_vTaskSuspend                  1
#define INCLUDE_xSemaphoreGetMutexHolder      1
#define INCLUDE_xTaskGetCurrentTaskHandle     1
#define INCLUDE_vTaskPrioritySet              1
#define INCLUDE_xEventGroupSetBitsFromISR     1
#define INCLUDE_xTimerPendFunctionCall        1
#define INCLUDE_vTaskDelayUntil               1
#define configUSE_STATS_FORMATTING_FUNCTIONS  1
#define configGENERATE_RUN_TIME_STATS         0
#define configUSE_16_BIT_TICKS                0
#define configMAX_TASK_NAME_LEN               16

#define configUSE_EVENT_GROUPS                1
#define configUSE_STREAM_BUFFERS              1
#define configMESSAGE_BUFFER_LENGTH_TYPE      size_t
#define configSTACK_DEPTH_TYPE                uint32_t
#define configUSE_POSIX_ERRNO                 0

#define configENABLE_FPU                      1
#define configENABLE_MPU                      0
#define configENABLE_TRUSTZONE                0
#define configRUN_FREERTOS_SECURE_ONLY        1
#define configMAX_SYSCALL_INTERRUPT_PRIORITY  16

#define configASSERT(x) do { if (!(x)) { \
    __asm volatile ("bkpt #0"); for (;;) {} } } while (0)

#endif /* FREERTOS_CONFIG_H */
