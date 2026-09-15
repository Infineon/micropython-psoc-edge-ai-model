/*
 * ipc.h — IPC transport implementation of deepcraft_interface_t
 *          (target / C-application side, PSoC Edge IPC pipe).
 *
 * Included only by adapters/deepcraft/wrapper.c — application code (main.c)
 * must NOT include this file directly; use wrapper.h instead.
 *
 * Provides:
 *   ipc_interface_init()  — sets up IPC pipe and registers callbacks
 *   ipc_notify_*()        — send VA events to the host
 *
 * To swap transports, only adapters/deepcraft/wrapper.c needs to change: replace the
 * include of this header and the init call.  The vtable contract
 * (deepcraft_interface_t) is unchanged.
 *
 * Copyright (c) 2026 Infineon Technologies AG
 * SPDX-License-Identifier: MIT
 */

#ifndef IPC_H
#define IPC_H

#include <stdint.h>
#include <stddef.h>
#include "deepcraft_interface.h"   /* from deepcraft-model-interface/src/    */

/*
 * IPC transport instance.
 * `base` MUST be first — allows cast to deepcraft_interface_t *.
 */
typedef struct {
    deepcraft_interface_t  base;       /* vtable — MUST be first      */
    void (*on_receive)(uint8_t cmd, uint32_t value);  /* ISR relay    */
} ipc_interface_t;

/*
 * ipc_interface_init
 *
 * Sets up the IPC pipe, registers the ISR receive callback, and stores the
 * on_start / on_stop application callbacks.
 * Call once at boot, before starting the RTOS scheduler.
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
