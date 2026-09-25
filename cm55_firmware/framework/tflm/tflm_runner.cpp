#include <cstdint>
#include <cstring>

#include "tensorflow/lite/c/common.h"
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/schema/schema_generated.h"

#include "tflm_result.h"

#define TFLM_TENSOR_ARENA_SIZE (128U * 1024U)
#define TFLM_SCHEMA_VERSION    (3U)

alignas(16) static uint8_t tensor_arena[TFLM_TENSOR_ARENA_SIZE]
    __attribute__((section(".cy_socmem_data")));

// Upstream tflite-micro dropped AllOpsResolver (it forces callers to opt into
// exactly the ops a model needs, for binary size). This runner has no
// compile-time knowledge of which model gets flashed, so register every
// available builtin op here to keep the old AllOpsResolver-style behavior.
using TflmOpResolver = tflite::MicroMutableOpResolver<128>;

static TflmOpResolver &GetOpResolver()
{
    static TflmOpResolver resolver;
    static bool initialized = false;
    if (!initialized) {
        resolver.AddAbs();
        resolver.AddAdd();
        resolver.AddAddN();
        resolver.AddArgMax();
        resolver.AddArgMin();
        resolver.AddAssignVariable();
        resolver.AddAveragePool2D();
        resolver.AddBasicClassifier();
        resolver.AddBatchMatMul();
        resolver.AddBatchToSpaceNd();
        resolver.AddBroadcastArgs();
        resolver.AddBroadcastTo();
        resolver.AddCallOnce();
        resolver.AddCast();
        resolver.AddCeil();
        resolver.AddCircularBuffer();
        resolver.AddConcatenation();
        resolver.AddConv2D();
        resolver.AddCos();
        resolver.AddCumSum();
        resolver.AddDecode();
        resolver.AddDelay();
        resolver.AddDepthToSpace();
        resolver.AddDepthwiseConv2D();
        resolver.AddDequantize();
        resolver.AddDetectionPostprocess();
        resolver.AddDiv();
        resolver.AddDynamicUpdateSlice();
        resolver.AddEmbeddingLookup();
        resolver.AddEnergy();
        resolver.AddElu();
        resolver.AddEqual();
        resolver.AddEthosU();
        resolver.AddExp();
        resolver.AddExpandDims();
        resolver.AddFftAutoScale();
        resolver.AddFill();
        resolver.AddFilterBank();
        resolver.AddFilterBankLog();
        resolver.AddFilterBankSquareRoot();
        resolver.AddFilterBankSpectralSubtraction();
        resolver.AddFloor();
        resolver.AddFloorDiv();
        resolver.AddFloorMod();
        resolver.AddFramer();
        resolver.AddFullyConnected();
        resolver.AddGather();
        resolver.AddGatherNd();
        resolver.AddGreater();
        resolver.AddGreaterEqual();
        resolver.AddHardSwish();
        resolver.AddIf();
        resolver.AddIrfft();
        resolver.AddL2Normalization();
        resolver.AddL2Pool2D();
        resolver.AddLeakyRelu();
        resolver.AddLess();
        resolver.AddLessEqual();
        resolver.AddLog();
        resolver.AddLogicalAnd();
        resolver.AddLogicalNot();
        resolver.AddLogicalOr();
        resolver.AddLogistic();
        resolver.AddLogSoftmax();
        resolver.AddMaximum();
        resolver.AddMaxPool2D();
        resolver.AddMirrorPad();
        resolver.AddMean();
        resolver.AddMinimum();
        resolver.AddMul();
        resolver.AddNeg();
        resolver.AddNotEqual();
        resolver.AddOverlapAdd();
        resolver.AddPack();
        resolver.AddPad();
        resolver.AddPadV2();
        resolver.AddPCAN();
        resolver.AddPrelu();
        resolver.AddQuantize();
        resolver.AddReadVariable();
        resolver.AddReduceAll();
        resolver.AddReduceMax();
        resolver.AddReduceMin();
        resolver.AddRelu();
        resolver.AddRelu6();
        resolver.AddReshape();
        resolver.AddResizeBilinear();
        resolver.AddResizeNearestNeighbor();
        resolver.AddReverseV2();
        resolver.AddRfft();
        resolver.AddRound();
        resolver.AddRsqrt();
        resolver.AddSelectV2();
        resolver.AddShape();
        resolver.AddSin();
        resolver.AddSlice();
        resolver.AddSoftmax();
        resolver.AddSpaceToBatchNd();
        resolver.AddSpaceToDepth();
        resolver.AddSplit();
        resolver.AddSplitV();
        resolver.AddSqueeze();
        resolver.AddSqrt();
        resolver.AddSquare();
        resolver.AddSquaredDifference();
        resolver.AddStridedSlice();
        resolver.AddStacker();
        resolver.AddSub();
        resolver.AddSum();
        resolver.AddSvdf();
        resolver.AddTanh();
        resolver.AddTransposeConv();
        resolver.AddTranspose();
        resolver.AddUnpack();
        resolver.AddUnidirectionalSequenceLSTM();
        resolver.AddVarHandle();
        resolver.AddWhile();
        resolver.AddWindow();
        resolver.AddZerosLike();
        initialized = true;
    }
    return resolver;
}

__attribute__((section(".cy_sharedmem")))
tflm_demo_result_t g_tflm_result;

static tflite::MicroInterpreter *interpreter = nullptr;

static void set_status(uint32_t status)
{
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
    g_tflm_result.magic = TFLM_RESULT_MAGIC;
    set_status(1U);

    const tflite::Model *model = tflite::GetModel(model_data);
    if (model->version() != TFLM_SCHEMA_VERSION) {
        set_status(2U);
        return false;
    }

    TflmOpResolver &resolver = GetOpResolver();
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
