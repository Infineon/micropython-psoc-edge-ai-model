/*
 * tflm_engine.c — Transport-agnostic TFLM model state machine.
 *
 * Implements the wire protocol and state machine declared in
 * tflm_engine.h purely against the transport_interface_t vtable (see
 * includes/ipc.h) -- no transport-specific header is referenced here.
 * Follows the same producer/consumer split as framework/tests/main.c: the
 * transport's receive callback only queues events (commands) and wakes the
 * owning task; all engine/callback logic runs from that task's context via
 * tflm_engine_process().
 *
 * Copyright (c) 2026 Infineon Technologies AG
 * SPDX-License-Identifier: MIT
 */

#include <string.h>

#include "FreeRTOS.h"
#include "task.h"

#include "tflm_engine.h"

#define TFLM_CMD_QUEUE_LEN (8U)

/* Max input payload buffered between TFLM_CMD_RUN_INFERENCE commands;
 * matches the bulk ring capacity of typical transports since a single
 * transfer cannot exceed it anyway. */
#define TFLM_MAX_INPUT_BYTES (65536U)

typedef struct {
    uint8_t  cmd;
    uint32_t value;
} tflm_cmd_evt_t;

/* Only one task can own an engine's IPC events at a time -- stored here
 * since the transport's callbacks carry no user-data pointer. */
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

static void send_error(tflm_engine_t *engine, tflm_error_t err)
{
    engine->transport->send(engine->transport, TFLM_EVT_ERROR, (uint32_t)err);
}

static void handle_run_inference(tflm_engine_t *engine)
{
    size_t len = s_input_len;
    bool overflow = s_input_overflow;
    s_input_len = 0U;
    s_input_overflow = false;

    if (engine->state != TFLM_STATE_RUNNING) {
        send_error(engine, TFLM_ERR_BAD_STATE);
        return;
    }
    if (overflow) {
        send_error(engine, TFLM_ERR_INVOKE_FAILED);
        return;
    }
    if (engine->callbacks.on_run_inference == NULL
            || !engine->callbacks.on_run_inference(s_input_buf, len)) {
        send_error(engine, TFLM_ERR_INVOKE_FAILED);
    }
}

void tflm_engine_init(tflm_engine_t *engine, transport_interface_t *transport,
    const tflm_callbacks_t *callbacks)
{
    engine->state     = TFLM_STATE_UNLOADED;
    engine->callbacks = *callbacks;
    engine->transport = transport;

    s_cmd_head = 0U;
    s_cmd_tail = 0U;
    s_input_len = 0U;
    s_input_overflow = false;

    transport->set_data_cb(on_transport_data);
    transport->register_receive_cb(transport, on_transport_command);
}

void tflm_engine_set_process_task(tflm_engine_t *engine, void *task_handle)
{
    s_task = (TaskHandle_t)task_handle;
    engine->transport->set_process_task(task_handle);
}

void tflm_engine_on_receive(tflm_engine_t *engine, uint8_t cmd, uint32_t value)
{
    switch (cmd) {
        case TFLM_CMD_MODEL_LOAD:
            if (engine->state != TFLM_STATE_UNLOADED) {
                send_error(engine, TFLM_ERR_BAD_STATE);
                break;
            }
            if (engine->callbacks.on_model_load == NULL
                    || !engine->callbacks.on_model_load(value)) {
                send_error(engine, TFLM_ERR_MODEL_LOAD_FAILED);
                break;
            }
            engine->state = TFLM_STATE_LOADED;
            engine->transport->send(engine->transport, TFLM_EVT_MODEL_LOADED, 0U);
            break;

        case TFLM_CMD_MODEL_UNLOAD:
            if (engine->state == TFLM_STATE_UNLOADED) {
                send_error(engine, TFLM_ERR_BAD_STATE);
                break;
            }
            if (engine->callbacks.on_model_unload != NULL) {
                engine->callbacks.on_model_unload();
            }
            engine->state = TFLM_STATE_UNLOADED;
            s_input_len = 0U;
            s_input_overflow = false;
            engine->transport->send(engine->transport, TFLM_EVT_MODEL_UNLOADED, 0U);
            break;

        case TFLM_CMD_MODEL_RUN:
            if (engine->state != TFLM_STATE_LOADED) {
                send_error(engine, TFLM_ERR_BAD_STATE);
                break;
            }
            if (engine->callbacks.on_model_run != NULL && !engine->callbacks.on_model_run()) {
                send_error(engine, TFLM_ERR_BAD_STATE);
                break;
            }
            engine->state = TFLM_STATE_RUNNING;
            engine->transport->send(engine->transport, TFLM_EVT_MODEL_RUNNING, 0U);
            break;

        case TFLM_CMD_MODEL_PAUSE:
            if (engine->state != TFLM_STATE_RUNNING) {
                send_error(engine, TFLM_ERR_BAD_STATE);
                break;
            }
            if (engine->callbacks.on_model_pause != NULL) {
                engine->callbacks.on_model_pause();
            }
            engine->state = TFLM_STATE_LOADED;
            engine->transport->send(engine->transport, TFLM_EVT_MODEL_PAUSED, 0U);
            break;

        case TFLM_CMD_RUN_INFERENCE:
            handle_run_inference(engine);
            break;

        default:
            send_error(engine, TFLM_ERR_BAD_STATE);
            break;
    }
}

void tflm_engine_process(tflm_engine_t *engine)
{
    engine->transport->process(); /* drains bulk data into on_transport_data() */

    while (s_cmd_tail != s_cmd_head) {
        uint32_t tail = s_cmd_tail;
        uint8_t cmd = s_cmd_queue[tail].cmd;
        uint32_t value = s_cmd_queue[tail].value;
        s_cmd_tail = (tail + 1U) % TFLM_CMD_QUEUE_LEN;
        tflm_engine_on_receive(engine, cmd, value);
    }
}

tflm_state_t tflm_engine_get_state(const tflm_engine_t *engine)
{
    return engine->state;
}

void tflm_engine_notify_ready(tflm_engine_t *engine)
{
    engine->transport->send(engine->transport, TFLM_EVT_READY, 0U);
}

size_t tflm_engine_send_result(tflm_engine_t *engine, const uint8_t *data, size_t len)
{
    return engine->transport->send_data(data, len);
}
