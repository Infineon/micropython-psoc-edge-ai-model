/*
 * DeepCraft framework entrypoint.
 * This file owns the model-specific VA lifecycle and uses the generic
 * top-level main.c for common board initialization only.
 */

#include "cybsp.h"
#include "FreeRTOS.h"
#include "task.h"
#include "pdm_mic.h"
#include "retarget_io_init.h"
#include "voice_assistant.h"
#include "profiler.h"
#include "ipc.h"

#ifdef USE_AUDIO_ENHANCEMENT
#include "audio_enhancement.h"
#endif

#include MTB_WWD_NLU_APP_HEADER(PROJECT_PREFIX)
#include MTB_WWD_NLU_CONFIG_HEADER(PROJECT_PREFIX)

#include "wrapper.h"

#define VA_TASK_NAME         ("va-task")
#define VA_TASK_STACK_SIZE   (10 * 1024)
#define VA_TASK_PRIORITY     (CY_RTOS_PRIORITY_NORMAL)
#define IPC_TASK_NAME        ("ipc-task")
#define IPC_TASK_STACK_SIZE  (2048U)
#define IPC_TASK_PRIORITY    (CY_RTOS_PRIORITY_NORMAL)
#define COMMAND_STRING_SIZE  (250U)
#define CMD_TIMEOUT_MS       (5000U)

static volatile bool g_va_enabled  = false;
static TaskHandle_t  g_va_task_hdl = NULL;
static TaskHandle_t  g_ipc_task_hdl = NULL;
static ipc_interface_t g_ipc_interface;

static void ipc_task(void *arg)
{
    (void)arg;
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        ipc_interface_process();
    }
}

uint8_t  bf_coeffs[1];
uint32_t bf_coeffs_total_len;

static void on_va_start(void)
{
    g_va_enabled = true;
    if (g_va_task_hdl != NULL) {
        xTaskResumeFromISR(g_va_task_hdl);
    }
}

static void on_va_stop(void)
{
    g_va_enabled = false;
    if (g_va_task_hdl != NULL) {
        vTaskSuspend(g_va_task_hdl);
    }
    deepcraft_wrapper_notify_stopped();
}

static void run_va_process(int16_t *audio_frame)
{
    va_rslt_t  va_result;
    va_data_t  va_data;
    va_event_t va_event;
    char       cmd_text[COMMAND_STRING_SIZE];

    va_result = voice_assistant_process(audio_frame, &va_event, &va_data);

    if (va_result == VA_RSLT_LICENSE_ERROR) {
        deepcraft_wrapper_notify_error();
        handle_error();
    }
    if (va_result != VA_RSLT_SUCCESS) {
        return;
    }

    switch (va_event) {
        case VA_EVENT_WW_DETECTED:
            deepcraft_wrapper_notify_wakeword_detected();
            break;

        case VA_EVENT_CMD_DETECTED:
            if (CY_RSLT_SUCCESS == voice_assistant_get_command(cmd_text)) {
                deepcraft_wrapper_notify_intent((uint8_t)va_data.intent_index);
            }
            break;

        case VA_EVENT_CMD_TIMEOUT:
            deepcraft_wrapper_notify_timeout();
            break;

        default:
            break;
    }
}

static void voice_assistant_task(void *arg)
{
    (void)arg;
    int16_t   *audio_frame;
    va_rslt_t  va_result;
#ifdef USE_AUDIO_ENHANCEMENT
    ae_rslt_t  ae_result;
#endif

    pdm_mic_init();

    va_result = voice_assistant_init(VA_MODE_WW_SINGLE_CMD);
    if (va_result != VA_RSLT_SUCCESS) {
        deepcraft_wrapper_notify_error();
        handle_error();
    }

    va_result = voice_assistant_set_command_timeout(CMD_TIMEOUT_MS);
    if (va_result != VA_RSLT_SUCCESS) {
        deepcraft_wrapper_notify_error();
        handle_error();
    }

    deepcraft_wrapper_notify_ready();

    for (;;) {
        if (!g_va_enabled) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        pdm_mic_get_data(&audio_frame);

#ifdef USE_AUDIO_ENHANCEMENT
        ae_result = audio_enhancement_feed_input(audio_frame, NULL);
        if (ae_result == AE_RSLT_LICENSE_ERROR) {
            deepcraft_wrapper_notify_error();
            handle_error();
        }
#else
        run_va_process(audio_frame);
#endif
    }
}

#ifdef USE_AUDIO_ENHANCEMENT
void audio_enhancement_process_output(ae_buffer_info_t *output_buffer)
{
    run_va_process(output_buffer->output_buf);
}
#endif

int main(void)
{
    cy_rslt_t result = cybsp_init();
    CY_ASSERT(result == CY_RSLT_SUCCESS);
    __enable_irq();

    /* Initialise the DeepCraft model interface (transport configured inside) */
    ipc_interface_init(&g_ipc_interface);
    deepcraft_wrapper_init(&g_ipc_interface.base, on_va_start, on_va_stop);

    result = xTaskCreate(ipc_task, IPC_TASK_NAME, IPC_TASK_STACK_SIZE,
        NULL, IPC_TASK_PRIORITY, &g_ipc_task_hdl);
    CY_ASSERT(result == pdPASS);
    // Set the IPC task as the task responsible for processing IPC events
    ipc_interface_set_process_task(g_ipc_task_hdl);

#ifdef USE_AUDIO_ENHANCEMENT
    ae_rslt_t ae_result = audio_enhancement_init(1U);
    if (ae_result != AE_RSLT_SUCCESS) {
        handle_error();
    }
#endif

    result = xTaskCreate(voice_assistant_task,
        VA_TASK_NAME, VA_TASK_STACK_SIZE,
        NULL, VA_TASK_PRIORITY,
        &g_va_task_hdl);
    CY_ASSERT(result == pdPASS);
    vTaskSuspend(g_va_task_hdl);

    vTaskStartScheduler();

    CY_ASSERT(false);
    return 0;
}
