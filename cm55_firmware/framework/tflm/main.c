#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "cybsp.h"
#include "cy_pdl.h"

#include "FreeRTOS.h"
#include "task.h"

#include "tflm_engine.h"
#include "ipc.h"

/*
* Mandatorily create a transport interface (here IPC) for the TFLM engine
*/

static const transport_interface_t s_transport = {
    .init                = ipc_interface_init,
    .send                = ipc_interface_send,
    .register_receive_cb = ipc_interface_register_receive_cb,
    .send_data           = ipc_interface_send_data,
    .set_data_cb         = ipc_interface_set_data_cb,
    .set_process_task    = ipc_interface_set_process_task,
    .process             = ipc_interface_process,
};

/* Raw AHB/XIP alias for the shared-data flash partition holding runtime
 * .tflite models (CM33's EXT_FLASH_SHARED_DATA_BASE = 0x02000000, mapped at
 * 0x60000000 + that offset). Works as a plain pointer now that the MPC
 * region for this range is registered in the MicroPython port's secboot
 * image -- see shared_flash_read for the full history of why this used to
 * fault. TFLM_CMD_MODEL_LOAD's value selects which model in this partition
 * to load, as a byte offset from this base. */
#define MODEL_XIP_BASE (0x62000000UL)

/* ── Task + entrypoint ────────────────────────────────────────────────────── */

#define TFLM_TASK_NAME       ("tflm-task")
#define TFLM_TASK_STACK_SIZE (4096U)
#define TFLM_TASK_PRIORITY   (CY_RTOS_PRIORITY_NORMAL)

static TaskHandle_t g_tflm_task_hdl = NULL;

static void tflm_task(void *arg)
{
    (void)arg;
    tflm_engine_notify_ready();
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        tflm_engine_process();
    }
}

int main(void)
{
    cy_rslt_t result = cybsp_init();
    CY_ASSERT(result == CY_RSLT_SUCCESS);
    __enable_irq();

    tflm_engine_init(&s_transport, MODEL_XIP_BASE);

    BaseType_t task_result = xTaskCreate(tflm_task, TFLM_TASK_NAME,
        TFLM_TASK_STACK_SIZE, NULL, TFLM_TASK_PRIORITY, &g_tflm_task_hdl);
    CY_ASSERT(task_result == pdPASS);
    tflm_engine_set_process_task(g_tflm_task_hdl);

    vTaskStartScheduler();

    CY_ASSERT(false);
    return 0;
}

