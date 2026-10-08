#include <cstdint>
#include <cstring>
#include <new>

#include "tensorflow/lite/c/common.h"
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/schema/schema_generated.h"

#include "tflm_runner.h"

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

/* Interpreter lives in placement-new'd static storage (no heap) so it can be
 * destroyed and reconstructed for a different model on unload/reload, unlike
 * a plain function-local `static` (constructed once, ever). */
alignas(alignof(tflite::MicroInterpreter))
static uint8_t interpreter_storage[sizeof(tflite::MicroInterpreter)];
static tflite::MicroInterpreter *interpreter = nullptr;

extern "C" bool tflm_runner_load(const uint8_t *model_data)
{
    if (interpreter != nullptr) {
        tflm_runner_unload();
    }

    const tflite::Model *model = tflite::GetModel(model_data);
    if (model == nullptr || model->version() != TFLM_SCHEMA_VERSION) {
        return false;
    }

    TflmOpResolver &resolver = GetOpResolver();
    interpreter = new (interpreter_storage) tflite::MicroInterpreter(
        model, resolver, tensor_arena, TFLM_TENSOR_ARENA_SIZE);

    if (interpreter->AllocateTensors() != kTfLiteOk) {
        tflm_runner_unload();
        return false;
    }
    return true;
}

extern "C" void tflm_runner_unload(void)
{
    if (interpreter != nullptr) {
        interpreter->~MicroInterpreter();
        interpreter = nullptr;
    }
}

extern "C" bool tflm_runner_is_loaded(void)
{
    return interpreter != nullptr;
}

extern "C" bool tflm_runner_invoke(const uint8_t *data, size_t len,
    uint8_t *out, size_t out_capacity, size_t *out_len)
{
    if (interpreter == nullptr) {
        return false;
    }

    TfLiteTensor *input = interpreter->input(0);
    if (input == nullptr || input->data.raw == nullptr || len != input->bytes) {
        return false;
    }
    std::memcpy(input->data.raw, data, len);

    if (interpreter->Invoke() != kTfLiteOk) {
        return false;
    }

    const TfLiteTensor *output = interpreter->output(0);
    if (output == nullptr || output->data.raw == nullptr || output->bytes > out_capacity) {
        return false;
    }
    std::memcpy(out, output->data.raw, output->bytes);
    if (out_len != nullptr) {
        *out_len = output->bytes;
    }
    return true;
}

extern "C" bool tflm_runner_input_quant(float *scale, int32_t *zero_point)
{
    if (interpreter == nullptr) {
        return false;
    }

    const TfLiteTensor *input = interpreter->input(0);
    if (input == nullptr) {
        return false;
    }
    if (scale != nullptr) {
        *scale = input->params.scale;
    }
    if (zero_point != nullptr) {
        *zero_point = input->params.zero_point;
    }
    return true;
}
