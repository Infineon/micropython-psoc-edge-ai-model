#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "cybsp.h"
#include "cy_pdl.h"
#include "cycfg_pins.h"

#include "FreeRTOS.h"
#include "task.h"

#include "tflm_engine.h"
#include "tflm_runner.h"
#include "ipc.h"

/*
 * Generic TFLM inference server: the model itself is not known at build
 * time. This file only (a) owns the concrete IPC transport instance (fully
 * implemented by ipc_interface_init() -- see sources/transport/ipc.c, no
 * transport glue belongs here), and (b) implements tflm_callbacks_t on top
 * of the model runner (tflm_runner.h). Which model is loaded, when, and
 * what data flows through it is entirely driven by TFLM_CMD_* commands
 * from the host (CM33) -- see engine/tflm/tflm_engine.c.
 */

static ipc_interface_t s_ipc;

/* ── Model callbacks: implement tflm_callbacks_t on top of tflm_runner.h ──── */

/* Raw AHB/XIP alias for the shared-data flash partition holding runtime
 * .tflite models (CM33's EXT_FLASH_SHARED_DATA_BASE = 0x02000000, mapped at
 * 0x60000000 + that offset). Works as a plain pointer now that the MPC
 * region for this range is registered in the MicroPython port's secboot
 * image -- see shared_flash_read for the full history of why this used to
 * fault. TFLM_CMD_MODEL_LOAD's value selects which model in this partition
 * to load, as a byte offset from this base. */
#define MODEL_XIP_BASE (0x62000000UL)

/* Output scratch buffer for tflm_runner_invoke(); sized generously for
 * typical small on-device models (classifiers, KWS, sensor models). */
#define TFLM_OUTPUT_BUF_SIZE (4096U)

static tflm_engine_t g_engine;
static uint8_t g_output_buf[TFLM_OUTPUT_BUF_SIZE];

static bool on_model_load(uint32_t flash_offset)
{
    const uint8_t *model_data = (const uint8_t *)(MODEL_XIP_BASE + flash_offset);
    return tflm_runner_load(model_data);
}

static bool on_model_unload(void)
{
    tflm_runner_unload();
    return true;
}

static bool on_run_inference(const uint8_t *data, size_t len)
{
    size_t out_len = 0U;
    if (!tflm_runner_invoke(data, len, g_output_buf, sizeof(g_output_buf), &out_len)) {
        return false;
    }
    tflm_engine_send_result(&g_engine, g_output_buf, out_len);
    return true;
}

static const tflm_callbacks_t g_callbacks = {
    .on_model_load    = on_model_load,
    .on_model_unload  = on_model_unload,
    .on_run_inference = on_run_inference,
};

/* ── Task + entrypoint ────────────────────────────────────────────────────── */

#define TFLM_TASK_NAME       ("tflm-task")
#define TFLM_TASK_STACK_SIZE (4096U)
#define TFLM_TASK_PRIORITY   (CY_RTOS_PRIORITY_NORMAL)

static TaskHandle_t g_tflm_task_hdl = NULL;

static void tflm_task(void *arg)
{
    (void)arg;
    tflm_engine_notify_ready(&g_engine);
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        tflm_engine_process(&g_engine);
    }
}

int main(void)
{
    cy_rslt_t result = cybsp_init();
    CY_ASSERT(result == CY_RSLT_SUCCESS);
    __enable_irq();

    Cy_GPIO_Write(CYBSP_LED_RGB_GREEN_PORT, CYBSP_LED_RGB_GREEN_PIN,
                  CYBSP_LED_STATE_OFF);
    Cy_GPIO_Write(CYBSP_LED_RGB_BLUE_PORT, CYBSP_LED_RGB_BLUE_PIN,
                  CYBSP_LED_STATE_OFF);

    /* ipc_interface_init() fully populates s_ipc.base (all six
     * transport_interface_t slots) -- same call framework/deepcraft/main.c
     * makes; no transport glue belongs in this file. */
    ipc_interface_init(&s_ipc);
    tflm_engine_init(&g_engine, &s_ipc.base, &g_callbacks);

    BaseType_t task_result = xTaskCreate(tflm_task, TFLM_TASK_NAME,
        TFLM_TASK_STACK_SIZE, NULL, TFLM_TASK_PRIORITY, &g_tflm_task_hdl);
    CY_ASSERT(task_result == pdPASS);
    tflm_engine_set_process_task(&g_engine, g_tflm_task_hdl);

    vTaskStartScheduler();

    CY_ASSERT(false);
    return 0;
}

