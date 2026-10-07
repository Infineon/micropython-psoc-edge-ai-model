/*
 * tflm_engine.h — Public header for the TFLM engine (tflm_engine.c): wire
 * protocol, state machine, and engine API.
 *
 * Copyright (c) 2026 Infineon Technologies AG
 * SPDX-License-Identifier: MIT
 */

#ifndef TFLM_ENGINE_H
#define TFLM_ENGINE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "transport.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ═══════════════════════════════════════════════════════════════════════
 * Wire protocol
 * ═══════════════════════════════════════════════════════════════════════ */

/* ── Commands: host (CM33) -> target (CM55) ──────────────────────────── */
#define TFLM_CMD_MODEL_LOAD     (0x90U) /* value = byte offset of the .tflite model in the external-flash model partition */
#define TFLM_CMD_MODEL_UNLOAD   (0x91U)
#define TFLM_CMD_MODEL_RUN      (0x92U) /* start accepting TFLM_CMD_RUN_INFERENCE */
#define TFLM_CMD_MODEL_PAUSE    (0x93U) /* stop accepting TFLM_CMD_RUN_INFERENCE; model stays loaded */
#define TFLM_CMD_RUN_INFERENCE  (0x94U) /* invoke on input bytes already staged via the bulk data ring */
#define TFLM_CMD_MODEL_INFO     (0x95U) /* reply (bulk ring): input then output tflm_adapter_tensor_info_t, 64 bytes; needs a loaded model */

/* ── Events: target (CM55) -> host (CM33) ────────────────────────────── */
#define TFLM_EVT_READY           (0xB0U) /* engine initialised, no model loaded yet */
#define TFLM_EVT_MODEL_LOADED    (0xB1U)
#define TFLM_EVT_MODEL_UNLOADED  (0xB2U)
#define TFLM_EVT_MODEL_RUNNING   (0xB3U) /* ack of TFLM_CMD_MODEL_RUN */
#define TFLM_EVT_MODEL_PAUSED    (0xB4U) /* ack of TFLM_CMD_MODEL_PAUSE */
/* No separate "result ready" event: inference output is streamed over the
 * same bulk data ring used for input, whose doorbell already tells the host
 * the byte count is available. */
#define TFLM_EVT_ERROR           (0xE0U) /* value = a tflm_error_t */

/* ── Error codes: reported alongside TFLM_EVT_ERROR ───────────────────── */
typedef enum {
    TFLM_ERR_NONE               = 0,
    TFLM_ERR_BAD_STATE          = 1, /* command not valid in the current state */
    TFLM_ERR_MODEL_LOAD_FAILED  = 2,
    TFLM_ERR_INVOKE_FAILED      = 3,
} tflm_error_t;

/* ═══════════════════════════════════════════════════════════════════════
 * Engine — singleton state machine over the wire protocol, driving tflm_adapter
 * (engine/tflm/tflm_engine.c); driven entirely by host (CM33) commands
 * ═══════════════════════════════════════════════════════════════════════ */

/* ── Lifecycle ─────────────────────────────────────────────────────────── */

/* Call once at boot, before starting the scheduler. Initialises `transport`
 * and registers the engine's receive callback and data sink with it.
 * `model_base` is the address (XIP-mapped) that TFLM_CMD_MODEL_LOAD's offset
 * is added to. */
void tflm_engine_init(const transport_interface_t *transport, uintptr_t model_base);

/* Register the FreeRTOS task that IPC events (commands, bulk input bytes)
 * should wake; that task must call tflm_engine_process() when notified. */
void tflm_engine_set_process_task(void *task_handle);

/* ── Event loop: call only from the task registered above ───────────────── */

/*
 * Drain any pending bulk input bytes and dispatch any queued commands.
 * Call only from the task registered above.
 */
void tflm_engine_process(void);

/* ── Notifications ────────────────────────────────────────────────────── */

/* Send TFLM_EVT_READY to the host. Call once boot-time setup is complete. */
void tflm_engine_notify_ready(void);

#ifdef __cplusplus
}
#endif

#endif /* TFLM_ENGINE_H */

