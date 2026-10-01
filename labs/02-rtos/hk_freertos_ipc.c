/* labs/02-rtos/hk_freertos_ipc.c
 *
 * Session 6 practical task - RTOS Tasks, IPC, and Shared Resource
 * Protection.
 *
 * Grows the Session 5 two-task pipeline into a four-task system:
 *   - vSamplerTask    : samples state, applies the SAFE-mode limit check,
 *                       sends to the HK queue, updates the shared stats
 *                       block (mutex protected), and signals a binary
 *                       semaphore whenever the vehicle enters SAFE mode.
 *   - vTelemetryTask  : unchanged from Session 5 - drains the queue,
 *                       emits HK frames.
 *   - vReporterTask   : new. Low rate. Takes the SAME mutex as the
 *                       sampler, reads the shared stats block, prints a
 *                       summary line.
 *   - vSafeModeTask   : new. Blocked on a binary semaphore; wakes only
 *                       when the sampler signals a SAFE-mode entry.
 *
 * Primitive choice, per Session 6 Section 5:
 *   - queue           : moving HK samples from sampler to telemetry.
 *   - mutex            : guarding g_stats, shared by sampler + reporter.
 *                        A mutex (not a binary semaphore) is required here
 *                        because it carries *priority inheritance*: without
 *                        it, the high-priority safe-mode task could be
 *                        blocked behind a low-priority holder of the stats
 *                        lock with no bound on the wait (the Mars
 *                        Pathfinder failure mode from Session 5). A plain
 *                        binary semaphore has no ownership concept and
 *                        cannot donate priority.
 *   - binary semaphore : signaling the sampler -> safe-mode-handler event.
 *                        No data is transferred and there is no mutual
 *                        exclusion involved, so a queue or mutex would be
 *                        the wrong tool.
 */
#define HK_UNIT_TEST
#include "../01-foundation/hk_telemetry.c"

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"

#include <stdio.h>

#define SAMPLER_PERIOD_MS       200   /* 5 Hz */
#define REPORTER_PERIOD_MS      1000  /* 1 Hz - slow, non-critical */
#define HK_QUEUE_LENGTH         4
#define TOTAL_FRAMES_TO_RUN     HK_NUM_CYCLES

typedef struct {
    spacecraft_state state;
    uint32_t frame;
    uint32_t tick_ms;
} hk_sample_t;

/* Shared, protected by xStatsMutex. Touched by vSamplerTask (writer) and
 * vReporterTask (reader) - never touched outside the mutex. */
typedef struct {
    uint32_t nominal_frames;
    uint32_t safe_frames;
    float    min_voltage;
    float    max_voltage;
} hk_stats;

static hk_stats g_stats = { .min_voltage = 1e9f, .max_voltage = -1e9f };

static QueueHandle_t     xHkQueue;
static SemaphoreHandle_t xStatsMutex;
static SemaphoreHandle_t xSafeModeSem;

/* Rate monotonic assignment for the two periodic tasks (sampler 5 Hz >
 * reporter 1 Hz -> sampler outranks reporter). The safe-mode handler is
 * aperiodic but safety-relevant, so it is placed above the periodic
 * telemetry/reporter work: a real FDIR response should preempt routine
 * telemetry formatting. */
#define SAMPLER_TASK_PRIORITY     ( tskIDLE_PRIORITY + 4 )
#define SAFE_MODE_TASK_PRIORITY   ( tskIDLE_PRIORITY + 3 )
#define TELEMETRY_TASK_PRIORITY   ( tskIDLE_PRIORITY + 2 )
#define REPORTER_TASK_PRIORITY    ( tskIDLE_PRIORITY + 1 )

static void update_stats(const spacecraft_state *s) {
    if (xSemaphoreTake(xStatsMutex, portMAX_DELAY) == pdTRUE) {
        if (s->mode == MODE_SAFE) {
            g_stats.safe_frames++;
        } else {
            g_stats.nominal_frames++;
        }
        if (s->bus_voltage < g_stats.min_voltage) g_stats.min_voltage = s->bus_voltage;
        if (s->bus_voltage > g_stats.max_voltage) g_stats.max_voltage = s->bus_voltage;
        xSemaphoreGive(xStatsMutex);
    }
}

static void vSamplerTask(void *pv) {
    (void) pv;
    TickType_t last = xTaskGetTickCount();

    for (uint32_t frame = 0; frame < TOTAL_FRAMES_TO_RUN; ++frame) {
        spacecraft_state s;
        sample_state(&s);
        apply_limits(&s);          /* HK-REQ-003 */
        update_stats(&s);          /* mutex-protected shared stats */

        hk_sample_t sample = {
            .state = s, .frame = frame,
            .tick_ms = (uint32_t) (xTaskGetTickCount() * portTICK_PERIOD_MS)
        };
        if (xQueueSend(xHkQueue, &sample, 0) != pdPASS) {
            fprintf(stderr, "sampler: queue full, dropped frame %u\n", frame);
        }

        if (s.mode == MODE_SAFE) {
            xSemaphoreGive(xSafeModeSem); /* signal the event, no data */
        }

        vTaskDelayUntil(&last, pdMS_TO_TICKS(SAMPLER_PERIOD_MS));
    }

    vTaskDelete(NULL);
}

static void vTelemetryTask(void *pv) {
    (void) pv;
    hk_sample_t sample;
    uint32_t frames_emitted = 0;

    while (frames_emitted < TOTAL_FRAMES_TO_RUN) {
        if (xQueueReceive(xHkQueue, &sample, portMAX_DELAY) == pdPASS) {
            g_sim_tick_ms = sample.tick_ms;
            emit_hk_frame(&sample.state, sample.frame); /* HK-REQ-002 */
            frames_emitted++;
        }
    }

    printf("telemetry: all %u frames emitted, stopping scheduler\n", TOTAL_FRAMES_TO_RUN);
    vTaskEndScheduler();
}

static void vReporterTask(void *pv) {
    (void) pv;
    TickType_t last = xTaskGetTickCount();

    for (;;) {
        vTaskDelayUntil(&last, pdMS_TO_TICKS(REPORTER_PERIOD_MS));

        if (xSemaphoreTake(xStatsMutex, portMAX_DELAY) == pdTRUE) {
            printf("reporter: nominal=%u safe=%u bus=[%.2fV .. %.2fV]\n",
                   g_stats.nominal_frames, g_stats.safe_frames,
                   (double) g_stats.min_voltage, (double) g_stats.max_voltage);
            xSemaphoreGive(xStatsMutex);
        }
    }
}

static void vSafeModeTask(void *pv) {
    (void) pv;
    for (;;) {
        if (xSemaphoreTake(xSafeModeSem, portMAX_DELAY) == pdTRUE) {
            printf("SAFE MODE entered - handler responding\n");
            /* Later this becomes real FDIR action (Session 9+). */
        }
    }
}

int main(void) {
    xHkQueue     = xQueueCreate(HK_QUEUE_LENGTH, sizeof(hk_sample_t));
    xStatsMutex  = xSemaphoreCreateMutex();
    xSafeModeSem = xSemaphoreCreateBinary();

    if (xHkQueue == NULL || xStatsMutex == NULL || xSafeModeSem == NULL) {
        fprintf(stderr, "failed to create queue/mutex/semaphore\n");
        return 1;
    }

    xTaskCreate(vSamplerTask,   "sampler",   configMINIMAL_STACK_SIZE, NULL, SAMPLER_TASK_PRIORITY,   NULL);
    xTaskCreate(vSafeModeTask,  "safemode",  configMINIMAL_STACK_SIZE, NULL, SAFE_MODE_TASK_PRIORITY, NULL);
    xTaskCreate(vTelemetryTask, "telemetry", configMINIMAL_STACK_SIZE, NULL, TELEMETRY_TASK_PRIORITY, NULL);
    xTaskCreate(vReporterTask,  "reporter",  configMINIMAL_STACK_SIZE, NULL, REPORTER_TASK_PRIORITY,  NULL);

    vTaskStartScheduler();

    return 0;
}
