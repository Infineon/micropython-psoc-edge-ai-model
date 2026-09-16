#include <stdbool.h>
#include <stdint.h>

#include "cybsp.h"
#include "cy_pdl.h"
#include "cycfg_pins.h"

#include "tflm_result.h"

/* Raw AHB/XIP alias for the shared-data flash partition (CM33's
 * EXT_FLASH_SHARED_DATA_BASE = 0x02000000, mapped at 0x60000000 + that
 * offset). Works as a plain pointer now that the MPC region for this range
 * is registered in the MicroPython port's secboot image -- see
 * shared_flash_read for the full history of why this used to fault. */
#define MODEL_XIP_BASE (0x62000000UL)
#define MODEL_OFFSET   (0x00000000UL)

static const uint8_t *const model_data =
    (const uint8_t *)(MODEL_XIP_BASE + MODEL_OFFSET);

static void blink_led(GPIO_PRT_Type *port, uint32_t pin)
{
    for (uint32_t count = 0U; count < 3U; ++count) {
        Cy_GPIO_Write(port, pin, CYBSP_LED_STATE_ON);
        Cy_SysLib_Delay(200U);
        Cy_GPIO_Write(port, pin, CYBSP_LED_STATE_OFF);
        Cy_SysLib_Delay(200U);
    }
}

int main(void)
{
    cy_rslt_t result = cybsp_init();
    CY_ASSERT(result == CY_RSLT_SUCCESS);
    __enable_irq();

    Cy_GPIO_Write(CYBSP_LED_RGB_GREEN_PORT, CYBSP_LED_RGB_GREEN_PIN,
                  CYBSP_LED_STATE_OFF);
    Cy_GPIO_Write(CYBSP_LED_RGB_BLUE_PORT, CYBSP_LED_RGB_BLUE_PIN,
                  CYBSP_LED_STATE_OFF);

    /* Zero-copy: tflite::GetModel() reads directly from external flash. */
    if (!tflm_init(model_data)) {
        for (;;) {
            blink_led(CYBSP_LED_RGB_BLUE_PORT, CYBSP_LED_RGB_BLUE_PIN);
        }
    }

    /* Sweep x continuously; MicroPython polls g_tflm_result.generation. */
    float x = 0.0f;
    const float step = 0.05f;
    const float two_pi = 6.28318548f;

    for (;;) {
        if (!tflm_step(x)) {
            for (;;) {
                blink_led(CYBSP_LED_RGB_BLUE_PORT, CYBSP_LED_RGB_BLUE_PIN);
            }
        }

        Cy_GPIO_Write(CYBSP_LED_RGB_GREEN_PORT, CYBSP_LED_RGB_GREEN_PIN, CYBSP_LED_STATE_ON);
        Cy_SysLib_Delay(20U);
        Cy_GPIO_Write(CYBSP_LED_RGB_GREEN_PORT, CYBSP_LED_RGB_GREEN_PIN, CYBSP_LED_STATE_OFF);

        x += step;
        if (x >= two_pi) {
            x -= two_pi;
        }

        Cy_SysLib_Delay(200U);
    }
}
