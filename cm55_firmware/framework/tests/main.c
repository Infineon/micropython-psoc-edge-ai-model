#include "cy_pdl.h"
#include "cybsp.h"
#include "FreeRTOS.h"
#include "task.h"
#include "ipc.h"
#include "ipc_communication.h"
#include <string.h>

#define IPC_TEST_TASK_NAME       ("ipc-test")
#define IPC_TEST_TASK_STACK_SIZE (2048U)
#define IPC_TEST_TASK_PRIORITY   (CY_RTOS_PRIORITY_NORMAL)

/* Two independent command-echo services share the CM55 endpoint (see ipc.py):
 *   svc1: CM55 client 5 -> echo back to CM33 client 3
 *   svc2: CM55 client 6 -> echo back to CM33 client 4
 * Both are handled uniformly via the transport's multi-client API; the CM33
 * reply client is derived from the CM55 client id by the same offset. */
#define IPC_SVC2_CM55_CLIENT_ID  (CM55_IPC_PIPE_CLIENT_ID + 1U)
#define IPC_CMD_QUEUE_LEN        (8U)

static ipc_interface_t g_ipc_interface;
static TaskHandle_t g_ipc_task_hdl = NULL;
static uint8_t g_echo_buffer[65536U];
static volatile size_t g_echo_length;

/* SPSC queue of received commands: producer = pipe ISR, consumer = task. */
typedef struct {
    uint8_t  client_id;
    uint8_t  cmd;
    uint32_t value;
} ipc_cmd_evt_t;
static volatile ipc_cmd_evt_t g_cmd_queue[IPC_CMD_QUEUE_LEN];
static volatile uint32_t g_cmd_head;
static volatile uint32_t g_cmd_tail;

static void notify_ipc_task_from_isr(void)
{
    if (g_ipc_task_hdl != NULL) {
        BaseType_t higher_priority_task_woken = pdFALSE;
        vTaskNotifyGiveFromISR(g_ipc_task_hdl, &higher_priority_task_woken);
        portYIELD_FROM_ISR(higher_priority_task_woken);
    }
}

/* Shared command handler for every registered service (svc1, svc2, ...). */
static void ipc_command_cb(uint8_t client_id, uint8_t cmd, uint32_t value)
{
    uint32_t head = g_cmd_head;
    uint32_t next = (head + 1U) % IPC_CMD_QUEUE_LEN;
    if (next != g_cmd_tail) {
        g_cmd_queue[head].client_id = client_id;
        g_cmd_queue[head].cmd       = cmd;
        g_cmd_queue[head].value     = value;
        g_cmd_head = next;
    }
    notify_ipc_task_from_isr();
}

static void ipc_echo_cb(const uint8_t *data, size_t len)
{
    if (len <= sizeof(g_echo_buffer)) {
        memcpy(g_echo_buffer, data, len);
        __DMB();
        g_echo_length = len;
    }
}

static void ipc_test_task(void *argument)
{
    (void)argument;
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        ipc_interface_process();
        if (g_echo_length != 0U) {
            size_t len = g_echo_length;
            __DMB();
            ipc_interface_send_data(g_echo_buffer, len);
            g_echo_length = 0U;
        }
        while (g_cmd_tail != g_cmd_head) {
            uint32_t tail = g_cmd_tail;
            uint8_t client_id = g_cmd_queue[tail].client_id;
            uint8_t cmd = g_cmd_queue[tail].cmd;
            uint32_t value = g_cmd_queue[tail].value;
            g_cmd_tail = (tail + 1U) % IPC_CMD_QUEUE_LEN;
            /* Echo back to the paired CM33 client (same offset as the CM55 id). */
            uint8_t reply_client = (uint8_t)(client_id - CM55_IPC_PIPE_CLIENT_ID
                + CM33_IPC_PIPE_CLIENT_ID);
            ipc_interface_send_command(reply_client, cmd, value);
        }
        vTaskDelay(pdMS_TO_TICKS(1U));
    }
}

int main(void)
{
    cy_rslt_t result = cybsp_init();
    CY_ASSERT(result == CY_RSLT_SUCCESS);
    __enable_irq();

    ipc_interface_init(&g_ipc_interface);
    ipc_interface_set_data_cb(ipc_echo_cb);

    /* Register both command services on the shared CM55 endpoint. Client 5 also
     * carries the bulk-data doorbell, handled inside the transport. */
    CY_ASSERT(ipc_interface_register_client(CM55_IPC_PIPE_CLIENT_ID, ipc_command_cb));
    CY_ASSERT(ipc_interface_register_client(IPC_SVC2_CM55_CLIENT_ID, ipc_command_cb));

    BaseType_t task_result = xTaskCreate(ipc_test_task,
        IPC_TEST_TASK_NAME, IPC_TEST_TASK_STACK_SIZE,
        NULL, IPC_TEST_TASK_PRIORITY, &g_ipc_task_hdl);
    CY_ASSERT(task_result == pdPASS);
    ipc_interface_set_process_task(g_ipc_task_hdl);
    vTaskStartScheduler();

    CY_ASSERT(false);
    return 0;
}
