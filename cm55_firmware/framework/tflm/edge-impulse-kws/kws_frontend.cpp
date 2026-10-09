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
#include <cstdint>

/* model_metadata.h first so the EI headers see the model's macro context
 * (inferencing engine, datatypes). It only pulls ei_constants.h -- not the
 * colliding inferencing_engines/ engine headers that model_variables.h does. */
#include "model_metadata.h"
#include "edge-impulse-sdk/classifier/ei_run_dsp.h"

#include "adapter.h"

/* Model I/O geometry, tracked from the exported metadata. */
#define KWS_FEATURE_COUNT   (EI_CLASSIFIER_NN_INPUT_FRAME_SIZE)
#define KWS_SAMPLE_RATE_HZ  (16000.0f)

/* MFE DSP block 7 config, copied verbatim from the exported model_variables.h
 * (positional order matches ei_dsp_config_mfe_t). named_axes is unused by
 * extract_mfe_features, so it is left null. */
static const ei_dsp_config_mfe_t s_mfe_config = {
    7,        /* block_id               */
    4,        /* implementation_version */
    1,        /* axes                   */
    nullptr,  /* named_axes             */
    0,        /* named_axes_size        */
    0.02f,    /* frame_length           */
    0.01f,    /* frame_stride           */
    40,       /* num_filters            */
    256,      /* fft_length             */
    0,        /* low_frequency          */
    0,        /* high_frequency         */
    101,      /* win_size               */
    -100,     /* noise_floor_db         */
};

/* Front-end runs on the single tflm-task. EI feeds audio as raw 
 * int16 cast to float. */
static const int16_t *s_pcm = nullptr;

static int pcm_get_data(size_t offset, size_t length, float *out_ptr)
{
    for (size_t i = 0U; i < length; i++) {
        out_ptr[i] = static_cast<float>(s_pcm[offset + i]);
    }
    return EIDSP_OK;
}

static float s_features[KWS_FEATURE_COUNT];

/* preprocess hook: raw int16 PCM -> MFE features -> int8, written straight into
 * the model's input tensor. */
extern "C" bool tflm_adapter_preprocess(const uint8_t *in, size_t in_len,
    uint8_t *tensor, size_t tensor_capacity, size_t *tensor_len)
{
    if (in == nullptr || tensor == nullptr || tensor_capacity < KWS_FEATURE_COUNT) {
        return false;
    }

    s_pcm = reinterpret_cast<const int16_t *>(in);

    signal_t signal;
    signal.total_length = in_len / sizeof(int16_t);
    signal.get_data = &pcm_get_data;

    matrix_t features(1, KWS_FEATURE_COUNT, s_features);
    if (extract_mfe_features(&signal, &features, (void *)&s_mfe_config,
            KWS_SAMPLE_RATE_HZ) != EIDSP_OK) {
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
