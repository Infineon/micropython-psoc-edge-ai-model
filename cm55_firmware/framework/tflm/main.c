#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "cybsp.h"
#include "cy_pdl.h"
#include "cycfg_pins.h"

#include "FreeRTOS.h"
#include "task.h"

#include "ipc.h"
#include "ipc_communication.h"
#include "tflm_result.h"

/* Raw AHB/XIP alias for the shared-data flash partition (CM33's
 * EXT_FLASH_SHARED_DATA_BASE = 0x02000000, mapped at 0x60000000 + that
 * offset). Works as a plain pointer now that the MPC region for this range
 * is registered in the MicroPython port's secboot image -- see
 * shared_flash_read for the full history of why this used to fault. */
#define MODEL_XIP_BASE (0x62000000UL)
#define MODEL_OFFSET   (0x00000000UL)

#define IPC_TASK_NAME        ("tflm-ipc")
#define IPC_TASK_STACK_SIZE  (4096U)   /* inference runs in this task's context */
#define IPC_TASK_PRIORITY    (CY_RTOS_PRIORITY_NORMAL)

static const uint8_t *const model_data =
    (const uint8_t *)(MODEL_XIP_BASE + MODEL_OFFSET);

static ipc_interface_t g_ipc_interface;
static TaskHandle_t g_ipc_task_hdl = NULL;

static void blink_led(GPIO_PRT_Type *port, uint32_t pin)
{
    for (uint32_t count = 0U; count < 3U; ++count) {
        Cy_GPIO_Write(port, pin, CYBSP_LED_STATE_ON);
        Cy_SysLib_Delay(200U);
        Cy_GPIO_Write(port, pin, CYBSP_LED_STATE_OFF);
        Cy_SysLib_Delay(200U);
    }
}

/* Bulk-data sink (runs in the IPC task context for each drained H2T chunk).
 * The stream is scalar float32 records -- one inference and one
 * float32 y reply per x received from CM33. */
static void on_x_data(const uint8_t *data, size_t len)
{
    for (size_t i = 0U; i + sizeof(float) <= len; i += sizeof(float)) {
        float x;
        memcpy(&x, &data[i], sizeof x);

        if (!tflm_step(x)) {
            for (;;) {
                blink_led(CYBSP_LED_RGB_BLUE_PORT, CYBSP_LED_RGB_BLUE_PIN);
            }
        }

        Cy_GPIO_Write(CYBSP_LED_RGB_GREEN_PORT, CYBSP_LED_RGB_GREEN_PIN, CYBSP_LED_STATE_ON);
        float y = g_tflm_result.y;
        ipc_interface_send_data((const uint8_t *)&y, sizeof y);
        Cy_GPIO_Write(CYBSP_LED_RGB_GREEN_PORT, CYBSP_LED_RGB_GREEN_PIN, CYBSP_LED_STATE_OFF);
    }
}

/* Doorbell-driven: the pipe ISR wakes this task, which drains the H2T ring
 * (each chunk handed to on_x_data) outside interrupt context. */
static void tflm_ipc_task(void *arg)
{
    (void)arg;
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        ipc_interface_process();
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

    /* Zero-copy: tflite::GetModel() reads directly from external flash. */
    if (!tflm_init(model_data)) {
        for (;;) {
            blink_led(CYBSP_LED_RGB_BLUE_PORT, CYBSP_LED_RGB_BLUE_PIN);
        }
    }

    /* IPC transport: sets up the pipe, inits the T2H (CM55->CM33) ring, and
     * registers the pipe ISR on the CM55 endpoint. x arrives via the H2T ring;
     * y is streamed back via ipc_interface_send_data() from on_x_data(). */
    ipc_interface_init(&g_ipc_interface);
    ipc_interface_set_data_cb(on_x_data);
    CY_ASSERT(ipc_interface_register_client(CM55_IPC_PIPE_CLIENT_ID, NULL));

    BaseType_t task_result = xTaskCreate(tflm_ipc_task, IPC_TASK_NAME,
        IPC_TASK_STACK_SIZE, NULL, IPC_TASK_PRIORITY, &g_ipc_task_hdl);
    CY_ASSERT(task_result == pdPASS);
    ipc_interface_set_process_task(g_ipc_task_hdl);

    vTaskStartScheduler();

    CY_ASSERT(false);
    return 0;
}
