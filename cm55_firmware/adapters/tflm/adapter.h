/*
 * adapter.h — Adapts tflite::MicroInterpreter to a flat C, bytes-in/
 * bytes-out API.
 *
 * Model-agnostic: the caller hands it a flash-mapped (XIP) buffer holding
 * an arbitrary .tflite model plus raw, model-owner-formatted tensor bytes,
 * and gets raw tensor bytes back. No assumptions about tensor shape/dtype
 * beyond what the flashed model itself declares. Only single-input/
 * single-output models are supported (tensor index 0 on each side) --
 * sufficient for the small on-device models this framework targets; models
 * with multiple I/O tensors need a richer framing than a flat byte buffer
 * and are out of scope here.
 *
 * Copyright (c) 2026 Infineon Technologies AG
 * SPDX-License-Identifier: MIT
 */

#ifndef TFLM_ADAPTER_H
#define TFLM_ADAPTER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Parse the model at `model_data` (flash-mapped/XIP, zero-copy) and allocate
 * its tensors. Safe to call again after tflm_adapter_unload() to load a
 * different model. Returns false on a bad schema version or if tensor
 * allocation fails; the adapter remains unloaded in that case. */
bool tflm_adapter_load(const uint8_t *model_data);

/* Release the current interpreter so a new model can be loaded. No-op if
 * nothing is loaded. */
void tflm_adapter_unload(void);

/* Copy `len` bytes into input tensor 0 (rejected if it doesn't match the
 * tensor's byte size), run inference, then copy output tensor 0's raw bytes
 * into `out` (up to `out_capacity`). `*out_len` receives the number of bytes
 * written. Returns false if no model is loaded, the input size doesn't
 * match, invocation fails, or the output doesn't fit in `out_capacity`. */
bool tflm_adapter_invoke(const uint8_t *data, size_t len,
    uint8_t *out, size_t out_capacity, size_t *out_len);

/* Description of one tensor, little-endian, 32 bytes (sent to the host as is). */
typedef struct {
    uint8_t  type;        /* TfLiteType: 1 float32, 3 uint8, 9 int8, ... */
    uint8_t  rank;        /* valid entries in dims (at most 4) */
    uint8_t  reserved[2];
    int32_t  dims[4];
    uint32_t bytes;       /* total tensor size in bytes */
    float    scale;       /* 0 when not quantized */
    int32_t  zero_point;
} tflm_adapter_tensor_info_t;

/* Describe input tensor 0 and output tensor 0 of the loaded model. Returns
 * false if no model is loaded. */
bool tflm_adapter_get_info(tflm_adapter_tensor_info_t *in, tflm_adapter_tensor_info_t *out);

#ifdef __cplusplus
}
#endif

#endif /* TFLM_ADAPTER_H */

