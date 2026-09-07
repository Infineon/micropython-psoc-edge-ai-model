#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "cybsp.h"
#include "cy_pdl.h"
#include "cycfg_peripherals.h"
#include "cycfg_qspi_memslot.h"
#include "cycfg_pins.h"
#include "cy_smif_memslot.h"

#define DEMO_FLASH_OFFSET (0x00880000UL)
#define DEMO_FLASH_LENGTH (16U)
#define DEMO_ERASE_SIZE   (0x40000U)

__attribute__((section(".cy_dtcm")))
static uint8_t known_data[DEMO_FLASH_LENGTH] = {
    0x00U, 0x11U, 0x22U, 0x33U,
    0x44U, 0x55U, 0x66U, 0x77U,
    0x88U, 0x99U, 0xAAU, 0xBBU,
    0xCCU, 0xDDU, 0xEEU, 0xFFU,
};

__attribute__((section(".cy_dtcm")))
static cy_stc_smif_context_t smif_context;

__attribute__((section(".cy_ramfunc")))
static cy_rslt_t erase_sector(void)
{
    return (cy_rslt_t)Cy_SMIF_MemEraseSector(
        SMIF0_CORE, &S25HS512T_SMIF0_SlaveSlot_1,
        DEMO_FLASH_OFFSET, DEMO_ERASE_SIZE,
        &smif_context);
}

__attribute__((section(".cy_ramfunc")))
static cy_rslt_t program_data(const uint8_t *data)
{
    return (cy_rslt_t)Cy_SMIF_MemWrite(
        SMIF0_CORE, &S25HS512T_SMIF0_SlaveSlot_1,
    DEMO_FLASH_OFFSET, data, DEMO_FLASH_LENGTH,
        &smif_context);
}

__attribute__((section(".cy_ramfunc")))
static cy_rslt_t read_data(uint8_t *buffer)
{
    return (cy_rslt_t)Cy_SMIF_MemRead(
        SMIF0_CORE, &S25HS512T_SMIF0_SlaveSlot_1,
        DEMO_FLASH_OFFSET, buffer, DEMO_FLASH_LENGTH,
        &smif_context);
}

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

static bool validate_read(const uint8_t *expected, const uint8_t *actual)
{
    return memcmp(expected, actual, DEMO_FLASH_LENGTH) == 0;
}

int main(void)
{
    cy_rslt_t result;
    uint8_t readback[DEMO_FLASH_LENGTH] = {0U};
    uint8_t modified_data[DEMO_FLASH_LENGTH];

    result = cybsp_init();
    CY_ASSERT(result == CY_RSLT_SUCCESS);
    __enable_irq();
    /* Keep both result LEDs off while the flash test is running. */
    Cy_GPIO_Write(CYBSP_LED_RGB_GREEN_PORT, CYBSP_LED_RGB_GREEN_PIN,
                  CYBSP_LED_STATE_OFF);
    Cy_GPIO_Write(CYBSP_LED_RGB_BLUE_PORT, CYBSP_LED_RGB_BLUE_PIN,
                  CYBSP_LED_STATE_OFF);

    result = (cy_rslt_t)Cy_SMIF_MemInit(SMIF0_CORE, &smif0BlockConfig,
                                        &smif_context);
    if (result != CY_RSLT_SUCCESS) {
        signal_blue();
        return 0;
    }

    /* One green blink confirms CM55 reached main before flash access. */
    Cy_GPIO_Write(CYBSP_LED_RGB_GREEN_PORT, CYBSP_LED_RGB_GREEN_PIN,
                  CYBSP_LED_STATE_ON);
    Cy_SysLib_Delay(200U);
    Cy_GPIO_Write(CYBSP_LED_RGB_GREEN_PORT, CYBSP_LED_RGB_GREEN_PIN,
                  CYBSP_LED_STATE_OFF);

    result = erase_sector();
    if (result != CY_RSLT_SUCCESS) {
        signal_blue();
        return 0;
    }

    result = program_data(known_data);
    if (result != CY_RSLT_SUCCESS) {
        signal_blue();
        return 0;
    }

    result = read_data(readback);
    if (result != CY_RSLT_SUCCESS ||
        !validate_read(known_data, readback)) {
        signal_blue();
        return 0;
    }
    signal_green();

    memcpy(modified_data, known_data, DEMO_FLASH_LENGTH);
    modified_data[DEMO_FLASH_LENGTH - 1U] = 0xEEU;
    result = program_data(modified_data);
    if (result != CY_RSLT_SUCCESS) {
        signal_blue();
        return 0;
    }

    result = read_data(readback);
    if (result != CY_RSLT_SUCCESS ||
        !validate_read(modified_data, readback)) {
        signal_blue();
        return 0;
    }
    signal_green();
    return 0;
}
