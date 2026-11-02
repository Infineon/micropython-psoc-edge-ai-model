/*
 * transport.h — Generic inter-core transport interface.
 *
 * Plug-in point between an engine (e.g. tflm_engine) and a concrete link
 * (e.g. the IPC pipe in ipc.h). The engine depends only on this header; the
 * application wires a concrete transport into an instance of the struct.
 *
 * Copyright (c) 2026 Infineon Technologies AG
 * SPDX-License-Identifier: MIT
 */

#ifndef TRANSPORT_H
#define TRANSPORT_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/*
 * Covers link bring-up (init), small command/event messages (send,
 * register_receive_cb) and raw byte-stream transfer (send_data, set_data_cb, set_process_task,
 * process) so a single interface serves every use case.
 */
typedef struct {
    void (*init)(void); /* Bring the link up. Called once, before any other slot. */
    bool (*send)(uint8_t cmd, uint32_t value);
    void (*register_receive_cb)(void (*cb)(uint8_t cmd, uint32_t value));
    size_t (*send_data)(const uint8_t *data, size_t len);  /* Send raw bytes to the host. Returns the number of bytes accepted. */
    void (*set_data_cb)(void (*cb)(const uint8_t *data, size_t len)); /* Register the sink for raw bytes received from the host. Invoked from process.*/
    void (*set_process_task)(void *task_handle); /* Register the task that should be woken when there is pending bulk data to hand to the data_cb. */
    void (*process)(void); /* Drain/poll any pending bulk data. Call once per wake from the task registered above. */
} transport_interface_t;

#endif /* TRANSPORT_H */
