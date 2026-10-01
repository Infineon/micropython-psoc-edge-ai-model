/*
 * tflm_interface.h — Shared TFLM model interface layer.
 *
 * Copyright (c) 2026 Infineon Technologies AG
 * SPDX-License-Identifier: MIT
 */

#ifndef TFLM_INTERFACE_H
#define TFLM_INTERFACE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Wire protocol: host (CM33) -> target (CM55) commands ───────────────── */
#define TFLM_CMD_MODEL_LOAD     (0x90U) /* value = byte offset of the .tflite model in the external-flash model partition */
#define TFLM_CMD_MODEL_UNLOAD   (0x91U)
#define TFLM_CMD_MODEL_RUN      (0x92U) /* start accepting TFLM_CMD_RUN_INFERENCE */
#define TFLM_CMD_MODEL_PAUSE    (0x93U) /* stop accepting TFLM_CMD_RUN_INFERENCE; model stays loaded */
#define TFLM_CMD_RUN_INFERENCE  (0x94U) /* invoke on input bytes already staged via the bulk data ring */

/* ── Wire protocol: target (CM55) -> host (CM33) events ──────────────────── */
#define TFLM_EVT_READY           (0xB0U) /* engine initialised, no model loaded yet */
#define TFLM_EVT_MODEL_LOADED    (0xB1U)
#define TFLM_EVT_MODEL_UNLOADED  (0xB2U)
#define TFLM_EVT_MODEL_RUNNING   (0xB3U) /* ack of TFLM_CMD_MODEL_RUN */
#define TFLM_EVT_MODEL_PAUSED    (0xB4U) /* ack of TFLM_CMD_MODEL_PAUSE */
/* No separate "result ready" event: tflm_engine_send_result() streams the
 * output bytes over the same bulk data ring used for input, whose doorbell
 * already tells the host the byte count is available. */
#define TFLM_EVT_ERROR           (0xE0U) /* value = a tflm_error_t */

/* Reasons reported alongside TFLM_EVT_ERROR. */
typedef enum {
    TFLM_ERR_NONE               = 0,
    TFLM_ERR_BAD_STATE          = 1, /* command not valid in the current state */
    TFLM_ERR_MODEL_LOAD_FAILED  = 2,
    TFLM_ERR_INVOKE_FAILED      = 3,
} tflm_error_t;

/* Engine-owned state machine, driven entirely by host commands. */
typedef enum {
    TFLM_STATE_UNLOADED = 0, /* no model loaded */
    TFLM_STATE_LOADED   = 1, /* tensors allocated, paused (not accepting inference) */
    TFLM_STATE_RUNNING  = 2, /* accepting TFLM_CMD_RUN_INFERENCE */
} tflm_state_t;

/*
 * Transport vtable -- pluggable point for any inter-core link (IPC pipe,
 * UART, OpenAMP, ...). Richer than a plain command channel because TFLM
 * needs to move raw tensor bytes, not just 4-byte command messages.
 * Implement this struct for your transport and embed it as the first
 * member of your transport-specific struct so a pointer to it can be
 * safely cast:
 * @code{.c}
 *   typedef struct {
 *       tflm_transport_t base;   // MUST be first
 *       // transport-specific fields ...
 *   } my_transport_t;
 * @endcode
 */
typedef struct tflm_transport_s tflm_transport_t;
struct tflm_transport_s {
    /* Send a (cmd, value) message to the host. */
    bool (*send_command)(tflm_transport_t *self, uint8_t cmd, uint32_t value);
    /*
     * Register the callback invoked on every received (cmd, value)
     * message. May be called from interrupt/transport context -- keep the
     * implementation minimal (queue + notify, no blocking work).
     */
    void (*register_receive_cb)(tflm_transport_t *self,
        void (*cb)(uint8_t cmd, uint32_t value));
    /* Send raw bytes to the host (e.g. inference output). Returns the
     * number of bytes actually accepted. */
    size_t (*send_data)(tflm_transport_t *self, const uint8_t *data, size_t len);
    /*
     * Register the sink for raw bytes received from the host (e.g.
     * inference input). Invoked from process(), never from an ISR.
     */
    void (*set_data_cb)(tflm_transport_t *self,
        void (*cb)(const uint8_t *data, size_t len));
    /* Register the task that should be woken when there is pending bulk
     * data to hand to the data_cb. */
    void (*set_process_task)(tflm_transport_t *self, void *task_handle);
    /* Drain/poll any pending bulk data. Call once per wake from the task
     * registered above. */
    void (*process)(tflm_transport_t *self);
};

/*
 * Application/runner callbacks, implemented by framework/tflm/main.c on top
 * of the model runner (tflm_runner.h). The engine knows nothing about
 * TFLM/tflite itself -- only the wire protocol and state machine above.
 * Each callback returns true on success; the engine sends the matching
 * TFLM_EVT_* reply (or TFLM_EVT_ERROR) and advances the state machine
 * accordingly. Any callback pointer may be NULL if the model/runner has no
 * extra work to do for that command beyond the state transition itself.
 */
typedef struct {
    /* Load the model at `flash_offset` bytes into the external-flash model
     * partition. */
    bool (*on_model_load)(uint32_t flash_offset);
    /* Release the currently loaded model/interpreter. */
    bool (*on_model_unload)(void);
    bool (*on_model_run)(void);
    bool (*on_model_pause)(void);
    /*
     * Run one inference over `len` freshly-staged input bytes. On success,
     * must call tflm_engine_send_result() with the output bytes before
     * returning true.
     */
    bool (*on_run_inference)(const uint8_t *data, size_t len);
} tflm_callbacks_t;

/* Engine instance: current state, the callbacks above, and the transport
 * it talks to the host through. One per boot. */
typedef struct {
    tflm_state_t       state;
    tflm_callbacks_t   callbacks;
    tflm_transport_t  *transport;
} tflm_engine_t;

/* Call once at boot, before starting the scheduler. Registers the engine's
 * receive callback and data sink with `transport`. */
void tflm_engine_init(tflm_engine_t *engine, tflm_transport_t *transport,
    const tflm_callbacks_t *callbacks);

/* Register the FreeRTOS task that IPC events (commands, bulk input bytes)
 * should wake; that task must call tflm_engine_process() when notified. */
void tflm_engine_set_process_task(tflm_engine_t *engine, void *task_handle);

/*
 * Drain any pending bulk input bytes and dispatch any queued commands (via
 * tflm_engine_on_receive()). Call only from the task registered above.
 */
void tflm_engine_process(tflm_engine_t *engine);

/*
 * Dispatch entry point: call with every (cmd, value) the transport routes to
 * this engine's client id. Not reentrant -- call only from the single task
 * that owns this engine (see framework/tflm/main.c).
 */
void tflm_engine_on_receive(tflm_engine_t *engine, uint8_t cmd, uint32_t value);

tflm_state_t tflm_engine_get_state(const tflm_engine_t *engine);

/* Send TFLM_EVT_READY to the host. Call once boot-time setup is complete. */
void tflm_engine_notify_ready(tflm_engine_t *engine);

/*
 * Send inference output bytes to the host over the bulk data ring. Call
 * from inside on_run_inference() once inference succeeds. Returns the number
 * of bytes actually accepted (may be < len if the transport stalls).
 */
size_t tflm_engine_send_result(tflm_engine_t *engine, const uint8_t *data, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* TFLM_INTERFACE_H */
