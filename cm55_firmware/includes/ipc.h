/*
 * ipc.h — Generic IPC transport (target / C-application side, PSoC Edge IPC
 *         pipe).
 *
 * Used by adapters/deepcraft/wrapper.c and any other adapter needing the
 * IPC pipe; not tied to any particular adapter's protocol.
 *
 * Provides:
 *   ipc_interface_init()  — sets up IPC pipe and registers callbacks
 *   ipc_notify_*()        — send VA events to the host
 *
 * To swap transports, only adapters/deepcraft/wrapper.c needs to change:
 * replace the include of this header and the init call. The vtable
 * contract (ipc_transport_vtable_t) is unchanged.
 *
 * Copyright (c) 2026 Infineon Technologies AG
 * SPDX-License-Identifier: MIT
 */

#ifndef IPC_H
#define IPC_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

/*
 * Minimal two-function transport vtable. Deliberately not deepcraft_interface_t:
 * this header has no DeepCraft dependency, even though deepcraft_interface_t
 * has the same shape and adapters/deepcraft/main.c relies on that to reinterpret
 * ipc_interface_t::base as one (see the cast there).
 */
typedef struct ipc_transport_vtable_s ipc_transport_vtable_t;
struct ipc_transport_vtable_s {
    bool (*send)(ipc_transport_vtable_t *self, uint8_t cmd, uint32_t value);
    void (*register_receive_cb)(ipc_transport_vtable_t *self,
        void (*cb)(uint8_t cmd, uint32_t value));
};

/*
 * IPC transport instance.
 * `base` MUST be first — allows cast to ipc_transport_vtable_t *.
 */
typedef struct {
    ipc_transport_vtable_t base;       /* vtable — MUST be first      */
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
