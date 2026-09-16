#include <cstdint>
#include <cstring>

#include "tensorflow/lite/c/common.h"
#include "tensorflow/lite/micro/all_ops_resolver.h"
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/schema/schema_generated.h"

#include "tflm_result.h"

#define TFLM_TENSOR_ARENA_SIZE (128U * 1024U)
#define TFLM_SCHEMA_VERSION    (3U)

alignas(16) static uint8_t tensor_arena[TFLM_TENSOR_ARENA_SIZE]
    __attribute__((section(".cy_socmem_data")));

__attribute__((section(".cy_sharedmem")))
tflm_demo_result_t g_tflm_result;

static tflite::MicroInterpreter *interpreter = nullptr;

static void set_status(uint32_t status)
{
    g_tflm_result.magic = TFLM_RESULT_MAGIC;
    g_tflm_result.status = status;
}

static int32_t round_to_int(float value)
{
    return static_cast<int32_t>(value + (value >= 0.0f ? 0.5f : -0.5f));
}

static bool set_sample_input(TfLiteTensor *input, float value)
{
    if (input == nullptr || input->bytes == 0U) {
        return false;
    }

    switch (input->type) {
        case kTfLiteFloat32:
            input->data.f[0] = value;
            return true;
        case kTfLiteInt8:
            if (input->params.scale == 0.0f) {
                return false;
            }
            input->data.int8[0] = static_cast<int8_t>(round_to_int(
                value / input->params.scale + input->params.zero_point));
            return true;
        case kTfLiteUInt8:
            if (input->params.scale == 0.0f) {
                return false;
            }
            input->data.uint8[0] = static_cast<uint8_t>(round_to_int(
                value / input->params.scale + input->params.zero_point));
            return true;
        default:
            return false;
    }
}

static bool read_sample_output(const TfLiteTensor *output)
{
    if (output == nullptr || output->bytes == 0U) {
        return false;
    }

    g_tflm_result.output_type = static_cast<uint32_t>(output->type);
    g_tflm_result.output_zero_point = output->params.zero_point;
    g_tflm_result.output_scale = output->params.scale;

    switch (output->type) {
        case kTfLiteFloat32:
            g_tflm_result.y = output->data.f[0];
            g_tflm_result.raw_y = 0;
            return true;
        case kTfLiteInt8:
            g_tflm_result.raw_y = output->data.int8[0];
            g_tflm_result.y =
                (output->data.int8[0] - output->params.zero_point) * output->params.scale;
            return true;
        case kTfLiteUInt8:
            g_tflm_result.raw_y = output->data.uint8[0];
            g_tflm_result.y =
                (output->data.uint8[0] - output->params.zero_point) * output->params.scale;
            return true;
        default:
            return false;
    }
}

extern "C" bool tflm_init(const uint8_t *model_data)
{
    std::memset(&g_tflm_result, 0, sizeof(g_tflm_result));
    set_status(1U);

    const tflite::Model *model = tflite::GetModel(model_data);
    if (model->version() != TFLM_SCHEMA_VERSION) {
        set_status(2U);
        return false;
    }

    static tflite::AllOpsResolver resolver;
    static tflite::MicroInterpreter static_interpreter(
        model, resolver, tensor_arena, TFLM_TENSOR_ARENA_SIZE);
    interpreter = &static_interpreter;

    if (interpreter->AllocateTensors() != kTfLiteOk) {
        set_status(3U);
        return false;
    }

    set_status(0U);
    return true;
}

extern "C" bool tflm_step(float x)
{
    if (interpreter == nullptr) {
        return false;
    }

    TfLiteTensor *input = interpreter->input(0);
    if (!set_sample_input(input, x)) {
        set_status(4U);
        return false;
    }

    if (interpreter->Invoke() != kTfLiteOk) {
        set_status(5U);
        return false;
    }

    if (!read_sample_output(interpreter->output(0))) {
        set_status(6U);
        return false;
    }

    g_tflm_result.x = x;
    g_tflm_result.status = 0U;
    g_tflm_result.generation += 1U;
    return true;
}
