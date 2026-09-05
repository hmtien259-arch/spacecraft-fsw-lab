/* labs/02-rtos/freertos_hooks.c
 *
 * Application-provided FreeRTOS hooks required by FreeRTOSConfig.h
 * (configCHECK_FOR_STACK_OVERFLOW=2, configUSE_MALLOC_FAILED_HOOK=1).
 * Shared by both the Session 5 and Session 6 executables.
 */
#include "FreeRTOS.h"
#include "task.h"

#include <stdio.h>
#include <stdlib.h>

void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName) {
    (void) xTask;
    fprintf(stderr, "FATAL: stack overflow in task \"%s\"\n", pcTaskName);
    fflush(stderr);
    abort();
}

void vApplicationMallocFailedHook(void) {
    fprintf(stderr, "FATAL: FreeRTOS heap allocation failed\n");
    fflush(stderr);
    abort();
}
