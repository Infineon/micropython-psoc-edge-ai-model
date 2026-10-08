/*
 * kws_frontend.h — "hey edge" keyword-spotting front-end.
 *
 * Bridges raw int16 PCM (as received over IPC from the CM33 host) to the
 * generic tflm_runner: it runs the Edge Impulse MFE DSP to produce the
 * model's input features, quantizes them, invokes inference, and decodes the
 * 2-class output into a single detection byte. This is the only model-aware
 * piece; tflm_runner stays model-agnostic.
 *
 * Copyright (c) 2026 Infineon Technologies AG
 * SPDX-License-Identifier: MIT
 */

#ifndef KWS_FRONTEND_H
#define KWS_FRONTEND_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Byte emitted to the CM33 on a positive / negative "hey edge" detection. */
#define KWS_CODE_DETECTED    (0x35U)
#define KWS_CODE_NOT_DETECTED (0x00U)

/* Run the full pipeline on `len` bytes of little-endian int16 PCM @16kHz:
 * MFE features -> quantize -> tflm_runner_invoke -> threshold decode.
 * On success sets `*out_code` to KWS_CODE_DETECTED/KWS_CODE_NOT_DETECTED.
 * If `out_scores` is non-NULL, the two raw int8 class logits (0=hey_edge,
 * 1=noise) are written there so the host can read model confidence.
 * Returns false if a model isn't loaded or any stage fails. */
bool kws_frontend_process(const uint8_t *pcm_bytes, size_t len,
    uint8_t *out_code, int8_t *out_scores);

#ifdef __cplusplus
}
#endif

#endif /* KWS_FRONTEND_H */
