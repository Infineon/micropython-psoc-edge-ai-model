/*
 * tflm_engine.c — TFLM model state machine over the transport_interface_t.
 *
 * Implements the wire protocol and state machine declared in
 * tflm_engine.h against the transport_interface_t vtable (see
 * includes/transport.h) -- no transport-specific header is referenced here, and
 * models are loaded/run through adapters/tflm/adapter.h.
 * Follows the same producer/consumer split as framework/tests/main.c: the
 * transport's receive callback only queues events (commands) and wakes the
 * owning task; all engine logic runs from that task's context via
 * tflm_engine_process().
 *
 * Copyright (c) 2026 Infineon Technologies AG
 * SPDX-License-Identifier: MIT
 */

#include <string.h>

#include "FreeRTOS.h"
#include "task.h"

#include "tflm_engine.h"
#include "adapter.h"

#define TFLM_CMD_QUEUE_LEN (8U)

/* Max input payload buffered between TFLM_CMD_RUN_INFERENCE commands;
 * matches the bulk ring capacity of typical transports since a single
 * transfer cannot exceed it anyway. */
#define TFLM_MAX_INPUT_BYTES (65536U)

/* Output scratch for tflm_adapter_invoke(); sized for small on-device models
 * (classifiers, KWS, sensor models). */
#define TFLM_MAX_OUTPUT_BYTES (4096U)

/* The MODEL_INFO reply is two of these back to back; the host decodes them. */
_Static_assert(sizeof(tflm_adapter_tensor_info_t) == 32, "MODEL_INFO wire layout");

typedef struct {
    uint8_t  cmd;
    uint32_t value;
} tflm_cmd_evt_t;

typedef enum {
    TFLM_STATE_UNLOADED = 0, /* no model loaded */
    TFLM_STATE_LOADED   = 1, /* tensors allocated, paused (not accepting inference) */
    TFLM_STATE_RUNNING  = 2, /* accepting TFLM_CMD_RUN_INFERENCE */
} tflm_state_t;

/* Singleton: the transport callbacks carry no user pointer and the adapter
 * holds a single interpreter, so all engine state is file-scope. */
static const transport_interface_t *s_transport;
static uintptr_t s_model_base;
static tflm_state_t s_state;
static TaskHandle_t s_task;

/* SPSC command queue: producer = transport's receive callback (usually ISR
 * context), consumer = the registered task (tflm_engine_process()). */
static volatile tflm_cmd_evt_t s_cmd_queue[TFLM_CMD_QUEUE_LEN];
static volatile uint32_t s_cmd_head;
static volatile uint32_t s_cmd_tail;

/* Input accumulator, filled by the transport's data_cb (task context) ahead
 * of the next TFLM_CMD_RUN_INFERENCE. */
static uint8_t s_input_buf[TFLM_MAX_INPUT_BYTES];
static size_t s_input_len;
static bool s_input_overflow;
static uint8_t s_output_buf[TFLM_MAX_OUTPUT_BYTES];

static void notify_task_from_isr(void)
{
    if (s_task != NULL) {
        BaseType_t higher_priority_task_woken = pdFALSE;
        vTaskNotifyGiveFromISR(s_task, &higher_priority_task_woken);
        portYIELD_FROM_ISR(higher_priority_task_woken);
    }
}

/* Transport receive callback: may run in interrupt context, so just queue
 * the command and defer handling to the process task. */
static void on_transport_command(uint8_t cmd, uint32_t value)
{
    uint32_t head = s_cmd_head;
    uint32_t next = (head + 1U) % TFLM_CMD_QUEUE_LEN;
    if (next != s_cmd_tail) {
        s_cmd_queue[head].cmd   = cmd;
        s_cmd_queue[head].value = value;
        s_cmd_head = next;
    }
    notify_task_from_isr();
}

/* Transport data sink: guaranteed task context (see transport_interface_t). */
static void on_transport_data(const uint8_t *data, size_t len)
{
    if (len > sizeof(s_input_buf) - s_input_len) {
        s_input_overflow = true;
        return;
    }
    memcpy(&s_input_buf[s_input_len], data, len);
    s_input_len += len;
}

static void send_error(tflm_error_t err)
{
    s_transport->send(TFLM_EVT_ERROR, (uint32_t)err);
}

static void handle_run_inference(void)
{
    size_t len = s_input_len;
    bool overflow = s_input_overflow;
    s_input_len = 0U;
    s_input_overflow = false;

    if (s_state != TFLM_STATE_RUNNING) {
        send_error(TFLM_ERR_BAD_STATE);
        return;
    }
    if (overflow) {
        send_error(TFLM_ERR_INVOKE_FAILED);
        return;
    }
    size_t out_len = 0U;
    if (!tflm_adapter_invoke(s_input_buf, len, s_output_buf, sizeof(s_output_buf), &out_len)) {
        send_error(TFLM_ERR_INVOKE_FAILED);
        return;
    }
    s_transport->send_data(s_output_buf, out_len);
}

void tflm_engine_init(const transport_interface_t *transport, uintptr_t model_base)
{
    s_state      = TFLM_STATE_UNLOADED;
    s_transport  = transport;
    s_model_base = model_base;

    s_cmd_head = 0U;
    s_cmd_tail = 0U;
    s_input_len = 0U;
    s_input_overflow = false;

    transport->init();
    transport->set_data_cb(on_transport_data);
    transport->register_receive_cb(on_transport_command);
}

void tflm_engine_set_process_task(void *task_handle)
{
    s_task = (TaskHandle_t)task_handle;
    s_transport->set_process_task(task_handle);
}

/* Not reentrant: runs only from the task that owns the engine. */
static void handle_command(uint8_t cmd, uint32_t value)
{
    switch (cmd) {
        case TFLM_CMD_MODEL_LOAD:
            if (s_state != TFLM_STATE_UNLOADED) {
                send_error(TFLM_ERR_BAD_STATE);
                break;
            }
            if (!tflm_adapter_load((const uint8_t *)(s_model_base + value))) {
                send_error(TFLM_ERR_MODEL_LOAD_FAILED);
                break;
            }
            s_state = TFLM_STATE_LOADED;
            s_transport->send(TFLM_EVT_MODEL_LOADED, 0U);
            break;

        case TFLM_CMD_MODEL_UNLOAD:
            if (s_state == TFLM_STATE_UNLOADED) {
                send_error(TFLM_ERR_BAD_STATE);
                break;
            }
            tflm_adapter_unload();
            s_state = TFLM_STATE_UNLOADED;
            s_input_len = 0U;
            s_input_overflow = false;
            s_transport->send(TFLM_EVT_MODEL_UNLOADED, 0U);
            break;

        case TFLM_CMD_MODEL_RUN:
            if (s_state != TFLM_STATE_LOADED) {
                send_error(TFLM_ERR_BAD_STATE);
                break;
            }
            s_state = TFLM_STATE_RUNNING;
            s_transport->send(TFLM_EVT_MODEL_RUNNING, 0U);
            break;

        case TFLM_CMD_MODEL_PAUSE:
            if (s_state != TFLM_STATE_RUNNING) {
                send_error(TFLM_ERR_BAD_STATE);
                break;
            }
            s_state = TFLM_STATE_LOADED;
            s_transport->send(TFLM_EVT_MODEL_PAUSED, 0U);
            break;

        case TFLM_CMD_RUN_INFERENCE:
            handle_run_inference();
            break;

        case TFLM_CMD_MODEL_INFO: {
            tflm_adapter_tensor_info_t info[2];
            if (s_state == TFLM_STATE_UNLOADED) {
                send_error(TFLM_ERR_BAD_STATE);
                break;
            }
            if (!tflm_adapter_get_info(&info[0], &info[1])) {
                send_error(TFLM_ERR_INVOKE_FAILED);
                break;
            }
            s_transport->send_data((const uint8_t *)info, sizeof(info));
            break;
        }

        default:
            send_error(TFLM_ERR_BAD_STATE);
            break;
    }
}

void tflm_engine_process(void)
{
    s_transport->process(); /* drains bulk data into on_transport_data() */

    while (s_cmd_tail != s_cmd_head) {
        uint32_t tail = s_cmd_tail;
        uint8_t cmd = s_cmd_queue[tail].cmd;
        uint32_t value = s_cmd_queue[tail].value;
        s_cmd_tail = (tail + 1U) % TFLM_CMD_QUEUE_LEN;
        handle_command(cmd, value);
    }
}

void tflm_engine_notify_ready(void)
{
    s_transport->send(TFLM_EVT_READY, 0U);
}
