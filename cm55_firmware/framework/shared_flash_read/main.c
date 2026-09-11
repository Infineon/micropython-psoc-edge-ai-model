#include <stdint.h>
#include <string.h>

#include "cybsp.h"
#include "cy_pdl.h"
#include "cycfg_peripherals.h"
#include "cycfg_pins.h"

/* Must match the base + offset the CM33 MicroPython flash module writes to.
 * "flash.allocate(size)" on the CM33 side is expected to return 0 for the
 * first allocation; if it doesn't, update DATA_OFFSET to match. */
#define SHARED_DATA_XIP_BASE (0x62000000UL)
#define DATA_OFFSET          (0x00000000UL)
#define EXPECTED_LEN          (16U)

static const uint8_t *shared_data =
    (const uint8_t *)(SHARED_DATA_XIP_BASE + DATA_OFFSET);

/* Write exactly these 16 bytes from CM33 MicroPython before enabling CM55. */
static const uint8_t expected_text[EXPECTED_LEN] = {
    'C', 'M', '3', '3', '_', 'T', 'O', '_',
    'C', 'M', '5', '5', '_', 'O', 'K', '!'
};

static void blink_led(GPIO_PRT_Type *port, uint32_t pin)
{
    for (uint32_t count = 0U; count < 3U; ++count) {
        Cy_GPIO_Write(port, pin, CYBSP_LED_STATE_ON);
        Cy_SysLib_Delay(200U);
        Cy_GPIO_Write(port, pin, CYBSP_LED_STATE_OFF);
        Cy_SysLib_Delay(200U);
    }
}

static void signal_green(void)
{
    blink_led(CYBSP_LED_RGB_GREEN_PORT, CYBSP_LED_RGB_GREEN_PIN);
}

static void signal_blue(void)
{
    blink_led(CYBSP_LED_RGB_BLUE_PORT, CYBSP_LED_RGB_BLUE_PIN);
}

int main(void)
{
    cy_rslt_t result = cybsp_init();
    CY_ASSERT(result == CY_RSLT_SUCCESS);
    __enable_irq();

    Cy_GPIO_Write(CYBSP_LED_RGB_GREEN_PORT, CYBSP_LED_RGB_GREEN_PIN, CYBSP_LED_STATE_OFF);
    Cy_GPIO_Write(CYBSP_LED_RGB_BLUE_PORT, CYBSP_LED_RGB_BLUE_PIN, CYBSP_LED_STATE_OFF);

    /* CM33 must have already written expected_text at this address before
     * enabling CM55. This is a pure XIP read; no SMIF calls needed here. */
    if (memcmp(shared_data, expected_text, EXPECTED_LEN) == 0) {
        signal_green();
    } else {
        signal_blue();
    }

    for (;;) {
    }
}
