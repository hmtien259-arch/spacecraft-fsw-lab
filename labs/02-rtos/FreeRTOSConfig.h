/* labs/02-rtos/FreeRTOSConfig.h
 *
 * Application configuration for the FreeRTOS kernel, POSIX simulator port.
 * Trimmed to what the Posix_GCC port and this lab's HK pipeline actually
 * need (no MPU / TrustZone / SMP options, which do not apply here).
 */
#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

#include <stdint.h>

/* --- Scheduling --- */
#define configUSE_PREEMPTION                       1
#define configUSE_TIME_SLICING                     1
#define configUSE_PORT_OPTIMISED_TASK_SELECTION     0
#define configUSE_TICKLESS_IDLE                     0
#define configCPU_CLOCK_HZ                          ( 100000000UL )
#define configTICK_RATE_HZ                          ( 1000 )
#define configMAX_PRIORITIES                        ( 7 )
#define configMINIMAL_STACK_SIZE                    ( ( unsigned short ) 1000 )
#define configMAX_TASK_NAME_LEN                     ( 16 )
#define configTICK_TYPE_WIDTH_IN_BITS               TICK_TYPE_WIDTH_32_BITS
#define configIDLE_SHOULD_YIELD                     1

/* --- Synchronization primitives (the whole point of Session 6) --- */
#define configUSE_MUTEXES                           1
#define configUSE_RECURSIVE_MUTEXES                 1
#define configUSE_COUNTING_SEMAPHORES                1
#define configUSE_QUEUE_SETS                         0
#define configQUEUE_REGISTRY_SIZE                    10
#define configUSE_TASK_NOTIFICATIONS                 1
#define configTASK_NOTIFICATION_ARRAY_ENTRIES         1

/* --- Software timers (not used by this lab, kept off) --- */
#define configUSE_TIMERS                             0
#define configTIMER_TASK_PRIORITY                    ( configMAX_PRIORITIES - 1 )
#define configTIMER_QUEUE_LENGTH                      10
#define configTIMER_TASK_STACK_DEPTH                 configMINIMAL_STACK_SIZE

/* --- Event groups / stream buffers (unused, kept off to shrink the build) --- */
#define configUSE_EVENT_GROUPS                       0
#define configUSE_STREAM_BUFFERS                     0
#define configUSE_CO_ROUTINES                        0

/* --- Memory allocation: heap_3 wraps the C library malloc/free, which is
 * the simplest correct choice under the POSIX simulator (real threads,
 * real address space - no need for FreeRTOS's own heap_4 allocator). --- */
#define configSUPPORT_STATIC_ALLOCATION               0
#define configSUPPORT_DYNAMIC_ALLOCATION               1
#define configTOTAL_HEAP_SIZE                          ( ( size_t ) ( 64 * 1024 ) )
#define configAPPLICATION_ALLOCATED_HEAP               0

/* --- Hooks --- */
#define configUSE_IDLE_HOOK                            0
#define configUSE_TICK_HOOK                            0
#define configUSE_MALLOC_FAILED_HOOK                   1
#define configCHECK_FOR_STACK_OVERFLOW                 2

/* --- Stats (off; not needed for these labs) --- */
#define configGENERATE_RUN_TIME_STATS                  0
#define configUSE_TRACE_FACILITY                       0
#define configUSE_STATS_FORMATTING_FUNCTIONS           0

/* --- API inclusion --- */
#define INCLUDE_vTaskPrioritySet                       1
#define INCLUDE_uxTaskPriorityGet                      1
#define INCLUDE_vTaskDelete                            1
#define INCLUDE_vTaskSuspend                           1
#define INCLUDE_xTaskDelayUntil                        1
#define INCLUDE_vTaskDelay                             1
#define INCLUDE_xTaskGetSchedulerState                 1
#define INCLUDE_xTaskGetCurrentTaskHandle               1
#define INCLUDE_uxTaskGetStackHighWaterMark             1
#define INCLUDE_xTaskGetIdleTaskHandle                  0
#define INCLUDE_eTaskGetState                           1
#define INCLUDE_xTimerPendFunctionCall                  0
#define INCLUDE_xTaskAbortDelay                         0
#define INCLUDE_xTaskGetHandle                          0
#define INCLUDE_xTaskResumeFromISR                      1

/* configASSERT(): stop and print, rather than silently continuing - flight
 * software philosophy (Session 4/5): a violated invariant must be loud. */
#include <stdio.h>
#include <stdlib.h>
#define configASSERT( x )                                                    \
    if( ( x ) == 0 )                                                         \
    {                                                                        \
        fprintf( stderr, "configASSERT failed: %s:%d\n", __FILE__, __LINE__ );\
        fflush( stderr );                                                    \
        abort();                                                             \
    }

#endif /* FREERTOS_CONFIG_H */
