/*
 * ipc.h — IPC transport over the PSoC Edge IPC pipe (target / C-application
 *          side), exposing the generic transport_interface_t vtable.
 *
 * Included directly by application code that drives the IPC transport itself
 * (e.g. framework/tflm/main.c for bulk tensor streaming, framework/deepcraft
 * via adapters/deepcraft/wrapper.c casting to its own deepcraft_interface_t --
 * layout-compatible for the shared send/register_receive_cb prefix; see the
 * _Static_asserts in framework/deepcraft/main.c).
 *
 * Provides:
 *   ipc_interface_init()         — sets up the IPC pipe and the T2H ring
 *   ipc_interface_send_data()    — stream bytes target -> host
 *   ipc_interface_set_data_cb()  — register a host -> target bulk-data sink
 *   ipc_interface_register_client() / ipc_interface_send_command()
 *                                — per-client command channel
 *
 * To swap transports, implement a new transport_interface_t in a separate
 * file; callers are otherwise unchanged.
 *
 * Copyright (c) 2026 Infineon Technologies AG
 * SPDX-License-Identifier: MIT
 */

#ifndef IPC_H
#define IPC_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/*
 * Generic transport vtable — the plug-in point for inter-core link
 * implementations. Covers both small command/event messages (send,
 * register_receive_cb) and raw byte-stream transfer (send_data, set_data_cb,
 * set_process_task, process) so a single vtable serves every use case
 */
typedef struct transport_interface_s transport_interface_t;
struct transport_interface_s {
    bool (*send)(transport_interface_t *self, uint8_t cmd, uint32_t value);
    void (*register_receive_cb)(transport_interface_t *self, void (*cb)(uint8_t cmd, uint32_t value));
    size_t (*send_data)(const uint8_t *data, size_t len);  /* Send raw bytes to the host. Returns the number of bytes accepted. */
    void (*set_data_cb)(void (*cb)(const uint8_t *data, size_t len)); /* Register the sink for raw bytes received from the host. Invoked from process.*/
    void (*set_process_task)(void *task_handle); /* Register the task that should be woken when there is pending bulk data to hand to the data_cb. */
    void (*process)(void); /* Drain/poll any pending bulk data. Call once per wake from the task registered above. */
};

/*
 * IPC transport instance.
 * `base` MUST be first — allows cast to transport_interface_t *.
 */
typedef struct {
    transport_interface_t  base;       /* vtable — MUST be first      */
    void (*on_receive)(uint8_t cmd, uint32_t value);  /* ISR relay    */
} ipc_interface_t;

/*
 * ipc_interface_init
 *
 * Populates the transport vtable, initialises the target -> host ring, and
 * sets up the IPC pipe. Call once at boot, before starting the RTOS scheduler.
 */
void ipc_interface_init(ipc_interface_t *self);

/* Register the FreeRTOS task that processes deferred bulk-data notifications. */
void ipc_interface_set_process_task(void *task_handle);

/* Process any pending IPC events. Must be called from the task registered via ipc_interface_set_process_task(). */
void ipc_interface_process(void);

/* ── Bulk data transfer (target -> host byte stream) ─────────────────────── */
/*
 * ipc_interface_send_data — append bytes to the target->host ring and ring the
 * doorbell. Returns the number of bytes accepted (may be < len if the ring is
 * full).
 */
size_t ipc_interface_send_data(const uint8_t *data, size_t len);

/*
 * ipc_interface_set_data_cb — register a sink for host->target bulk data.
 * Invoked from ipc_interface_process() task context. Pass NULL to
 * drain-and-discard.
 */
void ipc_interface_set_data_cb(void (*cb)(const uint8_t *data, size_t len));

/*
 * Register an application callback for an arbitrary CM55 pipe client id. The
 * transport routes every non-bulk message to the callback whose client id
 * matches the message, so multiple independent command services can share the
 * single CM55 endpoint uniformly. Call before starting the scheduler. Returns
 * false if the client id is out of range or the dispatch table is full.
 */
typedef void (*ipc_client_cb_t)(uint8_t client_id, uint8_t cmd, uint32_t value);
bool ipc_interface_register_client(uint8_t cm55_client_id, ipc_client_cb_t cb);

/*
 * Send a command message to an arbitrary CM33 pipe client. Serialize calls
 * returns false if the pipe stayed busy.
 */
bool ipc_interface_send_command(uint8_t cm33_client_id, uint8_t cmd, uint32_t value);

/* ── Notify helpers: send VA model events to the host ───────────────────── */

#endif /* IPC_H */
