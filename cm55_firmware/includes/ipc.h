/*
 * ipc.h — IPC transport over the PSoC Edge IPC pipe (target / C-application
 *          side).
 *
 * Included directly by application code that drives the IPC transport. An
 * application that needs the generic transport_interface_t (transport.h)
 * wires these functions into one (see framework/tflm/main.c).
 *
 * Provides:
 *   ipc_interface_init()         — sets up the IPC pipe and the T2H ring
 *   ipc_interface_send()         — send a command/event to the host
 *   ipc_interface_send_data()    — stream bytes target -> host
 *   ipc_interface_set_data_cb()  — register a host -> target bulk-data sink
 *   ipc_interface_register_client() / ipc_interface_send_command()
 *                                — per-client command channel
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
 * ipc_interface_init
 *
 * Initialises the target -> host ring and the IPC pipe. Call once at boot,
 * before starting the RTOS scheduler.
 */
void ipc_interface_init(void);

/* Send a command/event to the host. Returns false if the pipe stayed busy. */
bool ipc_interface_send(uint8_t cmd, uint32_t value);

/* Register the callback for messages addressed to CM55_IPC_PIPE_CLIENT_ID. */
void ipc_interface_register_receive_cb(void (*cb)(uint8_t cmd, uint32_t value));

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

#endif /* IPC_H */
