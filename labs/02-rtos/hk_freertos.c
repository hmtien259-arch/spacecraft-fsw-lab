/* labs/02-rtos/hk_freertos.c
 *
 * Session 5 practical task - Housekeeping on FreeRTOS.
 *
 * Turns the single-loop housekeeping program from Sessions 3-4 into a
 * concurrent two-task pipeline on FreeRTOS (POSIX simulator port):
 *   - vSamplerTask   : higher rate (5 Hz). Samples state, applies the
 *                      SAFE-mode limit check, sends the state to a queue.
 *   - vTelemetryTask : lower rate, event-driven. Blocks on the queue and
 *                      emits the HK frame whenever a sample arrives.
 *
 * Priorities follow rate-monotonic assignment (Session 5, Section 3): the
 * faster task gets the higher priority.
 *
 * Reuses spacecraft_state / sample_state() / apply_limits() / emit_hk_frame()
 * from labs/01-foundation/hk_telemetry.c unchanged, compiled in with
 * HK_UNIT_TEST defined so that file's own main() is suppressed (same trick
 * test_hk.c uses in Session 4) - one implementation, three consumers.
 */
#define HK_UNIT_TEST
#include "../01-foundation/hk_telemetry.c"

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

#include <stdio.h>

#define SAMPLER_PERIOD_MS      200   /* 5 Hz  - matches HK_CYCLE_MS in the HK program */
#define HK_QUEUE_LENGTH        4
#define TOTAL_FRAMES_TO_RUN    HK_NUM_CYCLES /* keep the demo bounded, like Session 3/4 */

/* One sample = the HK state plus the frame index and RTOS tick it was
 * taken at, so the telemetry task can label/timestamp without keeping its
 * own counter or clock - HK-REQ-004 stays a single source of truth. */
typedef struct {
    spacecraft_state state;
    uint32_t frame;
    uint32_t tick_ms;
} hk_sample_t;

static QueueHandle_t xHkQueue;

/* Rate-monotonic priority assignment: the sampler runs at a fixed 5 Hz and
 * is the only periodic deadline in this system, so it gets the higher
 * priority; the telemetry task is purely event-driven off the queue and
 * gets the lower one. */
#define SAMPLER_TASK_PRIORITY     ( tskIDLE_PRIORITY + 2 )
#define TELEMETRY_TASK_PRIORITY   ( tskIDLE_PRIORITY + 1 )

static void vSamplerTask(void *pv) {
    (void) pv;
    TickType_t last = xTaskGetTickCount();

    for (uint32_t frame = 0; frame < TOTAL_FRAMES_TO_RUN; ++frame) {
        spacecraft_state s;
        sample_state(&s);
        apply_limits(&s); /* HK-REQ-003 */

        hk_sample_t sample = {
            .state = s, .frame = frame,
            .tick_ms = (uint32_t) (xTaskGetTickCount() * portTICK_PERIOD_MS)
        };

        if (xQueueSend(xHkQueue, &sample, 0) != pdPASS) {
            fprintf(stderr, "sampler: queue full, dropped frame %u\n", frame);
        }

        vTaskDelayUntil(&last, pdMS_TO_TICKS(SAMPLER_PERIOD_MS));
    }

    /* Session 3/4's program runs a bounded ten cycles and exits; this task
     * does the same, then lets the telemetry task drain the last item and
     * stop the scheduler. */
    vTaskDelete(NULL);
}

static void vTelemetryTask(void *pv) {
    (void) pv;
    hk_sample_t sample;
    uint32_t frames_emitted = 0;

    while (frames_emitted < TOTAL_FRAMES_TO_RUN) {
        if (xQueueReceive(xHkQueue, &sample, portMAX_DELAY) == pdPASS) {
            g_sim_tick_ms = sample.tick_ms;             /* real RTOS timestamp */
            emit_hk_frame(&sample.state, sample.frame); /* HK-REQ-002 */
            frames_emitted++;
        }
    }

    printf("telemetry: all %u frames emitted, stopping scheduler\n", TOTAL_FRAMES_TO_RUN);
    vTaskEndScheduler();
}

int main(void) {
    xHkQueue = xQueueCreate(HK_QUEUE_LENGTH, sizeof(hk_sample_t));
    if (xHkQueue == NULL) {
        fprintf(stderr, "failed to create HK queue\n");
        return 1;
    }

    xTaskCreate(vSamplerTask, "sampler", configMINIMAL_STACK_SIZE,
                NULL, SAMPLER_TASK_PRIORITY, NULL);
    xTaskCreate(vTelemetryTask, "telemetry", configMINIMAL_STACK_SIZE,
                NULL, TELEMETRY_TASK_PRIORITY, NULL);

    vTaskStartScheduler();

    /* Only reached after vTaskEndScheduler() on the POSIX port. */
    return 0;
}
