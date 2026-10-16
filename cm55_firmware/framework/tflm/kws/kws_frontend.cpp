/*
 * kws_frontend.cpp — "hey edge" KWS front-end (Edge Impulse MFE preprocess).
 *
 * The model's input tensor is 7920 int8 MFE features, not raw audio, so this
 * runs Edge Impulse's own MFE DSP (byte-exact with how the model was trained)
 * on the incoming PCM and quantizes to the input tensor's int8 domain.
 *
 * It plugs into the generic TFLM adapter by overriding the weak
 * tflm_adapter_preprocess hook.
 *
 * Copyright (c) 2026 Infineon Technologies AG
 * SPDX-License-Identifier: MIT
 */

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "adapter.h"

/* Model input geometry (7920 int8 MFE features). */
#define KWS_FEATURE_COUNT   (7920U)

/* MFE DSP; lives in deps/libedge-impulse-sdk.a. */
extern "C" bool kws_mfe_extract(const int16_t *pcm, size_t n_samples,
    float *features, size_t n_features);

static float s_features[KWS_FEATURE_COUNT];

/* preprocess hook: raw int16 PCM -> MFE features -> int8, written straight into
 * the model's input tensor. */
extern "C" bool tflm_adapter_preprocess(const uint8_t *in, size_t in_len,
    uint8_t *tensor, size_t tensor_capacity, size_t *tensor_len)
{
    if (in == nullptr || tensor == nullptr) {
        return false;
    }

    /* Not the KWS model: pass bytes through like the adapter's weak default. */
    if (tensor_capacity != KWS_FEATURE_COUNT) {
        if (in_len != tensor_capacity) {
            return false;
        }
        std::memcpy(tensor, in, in_len);
        *tensor_len = in_len;
        return true;
    }

    if (!kws_mfe_extract(reinterpret_cast<const int16_t *>(in),
            in_len / sizeof(int16_t), s_features, KWS_FEATURE_COUNT)) {
        return false;
    }

    tflm_adapter_tensor_info_t in_info;
    tflm_adapter_tensor_info_t out_info;
    if (!tflm_adapter_get_info(&in_info, &out_info) || in_info.scale == 0.0f) {
        return false;
    }
    const float scale = in_info.scale;
    const int32_t zero_point = in_info.zero_point;

    int8_t *q = reinterpret_cast<int8_t *>(tensor);
    for (size_t i = 0U; i < KWS_FEATURE_COUNT; i++) {
        int32_t v = static_cast<int32_t>(lroundf(s_features[i] / scale)) + zero_point;
        if (v < -128) { v = -128; }
        if (v > 127)  { v = 127; }
        q[i] = static_cast<int8_t>(v);
    }
    *tensor_len = KWS_FEATURE_COUNT;
    return true;
}
