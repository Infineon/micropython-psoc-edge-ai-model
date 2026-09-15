/*
 * ipc.c — IPC transport implementation of deepcraft_interface_t
 *          (target / C-application side, PSoC Edge IPC pipe).
 *
 * Implements the two vtable function pointers (send, register_receive_cb) and
 * the notify_* API so deepcraft_target.c contains zero raw IPC or PDL calls.
 *
 * To use a different transport, create a new transport file that fills in a
 * deepcraft_interface_t with its own send / register_receive_cb and exposes
 * the same init + notify_* signatures.  deepcraft_target.c is unchanged.
 *
 * Copyright (c) 2026 Infineon Technologies AG
 * SPDX-License-Identifier: MIT
 */

#include "ipc.h"
#include "ipc_communication.h"
#include "ipc_ring.h"

#include "cybsp.h"
#include "cy_pdl.h"
#include "cy_ipc_pipe.h"
#include "FreeRTOS.h"
#include "task.h"

/* ── Tunable send parameters ─────────────────────────────────────────────── */
#define IPC_SEND_MAX_RETRIES     (100U)
#define IPC_SEND_RETRY_DELAY_US  (1000U)

/* Back-pressure while streaming the target -> host ring: when the ring is full
 * we wait for the host to drain before writing the remainder, bounded by a
 * timeout (MAX_SPINS * DELAY_US). */
#define IPC_TX_BACKPRESSURE_MAX_SPINS  (1000U)
#define IPC_TX_BACKPRESSURE_DELAY_US   (100U)

/* Host -> target drain chunk = size of the s_rx_scratch staging buffer.
 * Configurable and decoupled from the ring: the ring is drained IPC_H2T_CHUNK
 * bytes at a time and each chunk handed to the data sink, so a payload larger
 * than this chunk (or than the ring) is received losslessly in pieces.
 * Currently 64 KB (matches the ring); reduce if CM55 SRAM gets tight. */
#define IPC_H2T_CHUNK  (65536u)

/* Back-pressure while draining the host -> target ring: when the announced
 * total is not all in the ring yet, wait (bounded) for CM33 to write more. */
#define IPC_RX_BACKPRESSURE_MAX_SPINS  (1000U)
#define IPC_RX_BACKPRESSURE_DELAY_US   (100U)

/* ── Shared TX buffer ────────────────────────────────────────────────────── */
/* Must reside in IPC-visible SRAM so both processors can access it.         */
CY_SECTION_SHAREDMEM static ipc_msg_t s_tx_msg;

/* Singleton — static ISR trampoline needs to reach the instance             */
static ipc_interface_t *s_iface = NULL;
static volatile size_t s_pending_rx_length;
static TaskHandle_t s_process_task = NULL;

/* Bulk-data receive sink (host -> target ring). NULL = drain and discard.   */
static void (*s_on_data)(const uint8_t *data, size_t len) = NULL;

/* Scratch buffer for draining the inbound (host -> target) ring inside the
 * pipe ISR; sized to the configurable drain chunk (IPC_H2T_CHUNK).          */
static uint8_t s_rx_scratch[IPC_H2T_CHUNK];

#define IPC_MAX_CLIENTS  (CY_IPC_CYPIPE_CLIENT_CNT)
typedef struct {
    uint8_t          client_id;
    ipc_client_cb_t  cb;
} ipc_client_entry_t;
static ipc_client_entry_t s_clients[IPC_MAX_CLIENTS];
static size_t s_num_clients;

/* Dedicated shared TX buffer for command replies.  */
CY_SECTION_SHAREDMEM static ipc_msg_t s_cmd_tx;

/* Drain `total` bytes from the host -> target ring, handing them to the data
 * sink in <= IPC_H2T_CHUNK pieces. `total` may exceed the ring: CM33 streams
 * with back-pressure, so we consume what is present, deliver it, and wait
 * (bounded) for more. This runs from ipc_interface_process(), outside the
 * pipe ISR, so a large transfer cannot hold the IPC channel busy.             */
static void ipc_drain_rx_ring(size_t total)
{
    size_t consumed = 0U;
    uint32_t spins = 0U;
    while (consumed < total) {
        size_t want = total - consumed;
        if (want > sizeof(s_rx_scratch)) {
            want = sizeof(s_rx_scratch);
        }
        size_t n = ipc_ring_read(IPC_RING_HOST_TO_TARGET, s_rx_scratch, want);
        if (n > 0U) {
            if (s_on_data != NULL) {
                s_on_data(s_rx_scratch, n);
            }
            consumed += n;
            spins = 0U;
            continue;
        }
        if (++spins > IPC_RX_BACKPRESSURE_MAX_SPINS) {
            break;                       /* producer stalled: bail (partial) */
        }
        Cy_SysLib_DelayUs(IPC_RX_BACKPRESSURE_DELAY_US);
    }
}

/* Application-level start / stop callbacks, set during init                 */
/* IPC pipe ISR trampoline — called by the PDL pipe driver */
static void ipc_rx_callback(uint32_t *msg_data)
{
    if (msg_data == NULL || s_iface == NULL) {
        return;
    }
    const ipc_msg_t *msg = (const ipc_msg_t *)msg_data;    
    // Handle incoming IPC bulk data.
    if (msg->cmd == IPC_CMD_DATA_AVAIL) {
        s_pending_rx_length = msg->value;
        if (s_process_task != NULL) {
            BaseType_t higher_priority_task_woken = pdFALSE;
            xTaskNotifyFromISR(s_process_task, 0U, eNoAction,
                &higher_priority_task_woken);
            portYIELD_FROM_ISR(higher_priority_task_woken);
        }
        return;
    }

    // Look for a registered client that matches the incoming message's client ID.
    for (size_t i = 0U; i < s_num_clients; i++) {
        if (s_clients[i].client_id == msg->client_id) {
            if (s_clients[i].cb != NULL) {
                s_clients[i].cb(msg->client_id, msg->cmd, msg->value);
            }
            return;
        }
    }

    // If no registered client handled the message, call the generic receive callback.
    if (s_iface->on_receive != NULL) {
        s_iface->on_receive(msg->cmd, msg->value);
    }
}

/* ── vtable: send ────────────────────────────────────────────────────────── */
static bool ipc_send(deepcraft_interface_t *self, uint8_t cmd, uint32_t value)
{
    (void)self;
    cy_en_ipc_pipe_status_t status;
    uint32_t retries = 0;

    s_tx_msg.client_id = CM33_IPC_PIPE_CLIENT_ID;
    s_tx_msg.intr_mask = 0;
    s_tx_msg.cmd       = cmd;
    s_tx_msg.value     = value;

    Cy_SysLib_DelayUs(2000U);

    while (retries < IPC_SEND_MAX_RETRIES) {
        status = Cy_IPC_Pipe_SendMessage(CM33_IPC_PIPE_EP_ADDR,
            CM55_IPC_PIPE_EP_ADDR,
            (void *)&s_tx_msg, NULL);
        if (status == CY_IPC_PIPE_SUCCESS) {
            return true;
        }
        retries++;
        Cy_SysLib_DelayUs(IPC_SEND_RETRY_DELAY_US);
    }
    return false;
}

/* ── vtable: register_receive_cb for deepcraft_interface ─────────────────────────── */
static void ipc_register_receive_cb(deepcraft_interface_t *self,
    void (*cb)(uint8_t cmd, uint32_t value))
{
    ipc_interface_t *iface = (ipc_interface_t *)self;
    iface->on_receive = cb;
    Cy_IPC_Pipe_RegisterCallback(CM55_IPC_PIPE_EP_ADDR,
        &ipc_rx_callback,
        (uint32_t)CM55_IPC_PIPE_CLIENT_ID);
}

/* ── Multi-client command channel for generic CM55 clients ───────────────────────── */
bool ipc_interface_register_client(uint8_t cm55_client_id, ipc_client_cb_t cb)
{
    /* Reject ids the pipe cannot route and guard the fixed dispatch table. */
    if (cm55_client_id >= CY_IPC_CYPIPE_CLIENT_CNT
            || s_num_clients >= IPC_MAX_CLIENTS) {
        return false;
    }
    s_clients[s_num_clients].client_id = cm55_client_id;
    s_clients[s_num_clients].cb        = cb;
    s_num_clients++;
    return Cy_IPC_Pipe_RegisterCallback(CM55_IPC_PIPE_EP_ADDR,
        &ipc_rx_callback, (uint32_t)cm55_client_id) == CY_IPC_PIPE_SUCCESS;
}

bool ipc_interface_send_command(uint8_t cm33_client_id, uint8_t cmd, uint32_t value)
{
    cy_en_ipc_pipe_status_t status;
    uint32_t retries = 0U;

    s_cmd_tx.client_id = cm33_client_id;
    s_cmd_tx.intr_mask = CY_IPC_CYPIPE_INTR_MASK;
    s_cmd_tx.cmd       = cmd;
    s_cmd_tx.value     = value;

    while (retries < IPC_SEND_MAX_RETRIES) {
        status = Cy_IPC_Pipe_SendMessage(CM33_IPC_PIPE_EP_ADDR,
            CM55_IPC_PIPE_EP_ADDR, (void *)&s_cmd_tx, NULL);
        if (status == CY_IPC_PIPE_SUCCESS) {
            return true;
        }
        retries++;
        Cy_SysLib_DelayUs(IPC_SEND_RETRY_DELAY_US);
    }
    return false;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Public init
 * ═══════════════════════════════════════════════════════════════════════════ */
void ipc_interface_init(ipc_interface_t *self)
{
    self->base.send               = ipc_send;
    self->base.register_receive_cb = ipc_register_receive_cb;
    self->on_receive               = NULL;

    s_iface = self;

    /* CM55 owns the target -> host ring (m33_m55_shared SOCMEM region).     */
    ipc_ring_init(IPC_RING_TARGET_TO_HOST, IPC_RING_T2H_CAPACITY);

    /* Platform-specific IPC pipe setup (defined in shared/source) */
    cm55_ipc_communication_setup();

}

void ipc_interface_set_process_task(void *task_handle)
{
    s_process_task = (TaskHandle_t)task_handle;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Bulk data transfer — target -> host byte stream
 * ═══════════════════════════════════════════════════════════════════════════ */
void ipc_interface_set_data_cb(void (*cb)(const uint8_t *data, size_t len))
{
    s_on_data = cb;
}

void ipc_interface_process(void)
{
    size_t total = s_pending_rx_length;
    if (total == 0U) {
        return;
    }
    s_pending_rx_length = 0U;
    ipc_drain_rx_ring(total);
}

size_t ipc_interface_send_data(const uint8_t *data, size_t len)
{
    if (data == NULL || len == 0U || s_iface == NULL) {
        return 0U;
    }

    /* Announce the total length up front so the host starts draining while we
     * stream; this is what lets back-pressure work when len exceeds the ring. */
    s_iface->base.send(&s_iface->base, IPC_CMD_DATA_AVAIL, (uint32_t)len);

    /* Write losslessly: append whatever fits, then block (bounded) for the
     * host to free space, until every byte is in the ring.                 */
    size_t sent = 0U;
    uint32_t spins = 0U;
    while (sent < len) {
        size_t n = ipc_ring_write(IPC_RING_TARGET_TO_HOST, data + sent, len - sent);
        if (n > 0U) {
            sent += n;
            spins = 0U;
            continue;
        }
        if (++spins > IPC_TX_BACKPRESSURE_MAX_SPINS) {
            break;                       /* host stalled: give up (partial) */
        }
        Cy_SysLib_DelayUs(IPC_TX_BACKPRESSURE_DELAY_US);
    }
    return sent;
}
