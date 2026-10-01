#ifndef TFLM_RESULT_H
#define TFLM_RESULT_H

#include <stdbool.h>
#include <stdint.h>

#define TFLM_RESULT_MAGIC        (0x54464C4DUL)

/* status codes:
 *   1 = initializing
 *   2 = bad TFLite schema
 *   3 = AllocateTensors failed
 *   4 = input setup failed
 *   5 = Invoke failed
 *   6 = output read failed
 *   0 = running normally; check `generation` for a new sample
 */
typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint32_t status;
    uint32_t generation;
    float x;
    float y;
    int32_t raw_y;
    uint32_t output_type;
    int32_t output_zero_point;
    float output_scale;
} tflm_demo_result_t;

#ifdef __cplusplus
extern "C" {
#endif

extern tflm_demo_result_t g_tflm_result;
bool tflm_init(const uint8_t *model_data);
bool tflm_step(float x);

#ifdef __cplusplus
}
#endif

#endif
