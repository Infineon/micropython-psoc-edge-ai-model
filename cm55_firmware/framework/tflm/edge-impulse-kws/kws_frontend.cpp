/*
 * kws_frontend.cpp — "hey edge" KWS front-end (Edge Impulse MFE + decode).
 *
 * The model's input tensor is 7920 int8 MFE features, not raw audio, so this
 * runs Edge Impulse's own MFE DSP (byte-exact with how the model was trained)
 * on the incoming PCM, quantizes to the input tensor's int8 domain, invokes
 * the generic tflm_runner, and thresholds the 2-class output.
 *
 * Only the Edge Impulse DSP subset is compiled in (see tflm.mk) -- the EI
 * TensorFlow/run_classifier engine is deliberately NOT used; inference stays
 * on the project's existing tflm-core.a via tflm_runner.
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

#include "tflm_runner.h"
#include "kws_frontend.h"

/* Model I/O geometry, tracked from the exported metadata. */
#define KWS_FEATURE_COUNT   (EI_CLASSIFIER_NN_INPUT_FRAME_SIZE)
#define KWS_SAMPLE_RATE_HZ  (16000.0f)

/* Output tensor is int8 softmax over {hey_edge, noise} with zero_point=-128,
 * scale=1/256, so p(hey_edge) = (q + 128) / 256. The 0.6 detection threshold
 * is therefore q >= ceil(0.6*256 - 128) = 26. */
#define KWS_OUTPUT_INT8_THRESHOLD (26)

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

/* Front-end runs on the single tflm-task, so a file-static signal context is
 * safe (no reentrancy). EI feeds audio as raw int16 cast to float, no scaling
 * (numpy::int16_to_float). */
static const int16_t *s_pcm = nullptr;

static int pcm_get_data(size_t offset, size_t length, float *out_ptr)
{
    for (size_t i = 0U; i < length; i++) {
        out_ptr[i] = static_cast<float>(s_pcm[offset + i]);
    }
    return EIDSP_OK;
}

static float s_features[KWS_FEATURE_COUNT];
static int8_t s_features_q[KWS_FEATURE_COUNT];

extern "C" bool kws_frontend_process(const uint8_t *pcm_bytes, size_t len,
    uint8_t *out_code, int8_t *out_scores)
{
    if (pcm_bytes == nullptr || out_code == nullptr) {
        return false;
    }

    s_pcm = reinterpret_cast<const int16_t *>(pcm_bytes);

    signal_t signal;
    signal.total_length = len / sizeof(int16_t);
    signal.get_data = &pcm_get_data;

    matrix_t features(1, KWS_FEATURE_COUNT, s_features);
    if (extract_mfe_features(&signal, &features, (void *)&s_mfe_config,
            KWS_SAMPLE_RATE_HZ) != EIDSP_OK) {
        return false;
    }

    float scale = 0.0f;
    int32_t zero_point = 0;
    if (!tflm_runner_input_quant(&scale, &zero_point) || scale == 0.0f) {
        return false;
    }

    for (size_t i = 0U; i < KWS_FEATURE_COUNT; i++) {
        int32_t q = static_cast<int32_t>(lroundf(s_features[i] / scale)) + zero_point;
        if (q < -128) { q = -128; }
        if (q > 127)  { q = 127; }
        s_features_q[i] = static_cast<int8_t>(q);
    }

    uint8_t raw_out[8];
    size_t out_len = 0U;
    if (!tflm_runner_invoke(reinterpret_cast<const uint8_t *>(s_features_q),
            KWS_FEATURE_COUNT, raw_out, sizeof(raw_out), &out_len)) {
        return false;
    }
    if (out_len < 1U) {
        return false;
    }

    const int8_t q_hey_edge = static_cast<int8_t>(raw_out[0]);
    *out_code = (q_hey_edge >= KWS_OUTPUT_INT8_THRESHOLD)
        ? KWS_CODE_DETECTED : KWS_CODE_NOT_DETECTED;
    if (out_scores != nullptr) {
        out_scores[0] = q_hey_edge;
        out_scores[1] = (out_len > 1U) ? static_cast<int8_t>(raw_out[1]) : 0;
    }
    return true;
}
