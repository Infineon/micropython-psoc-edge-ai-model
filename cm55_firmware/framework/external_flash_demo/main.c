#include <stdint.h>
#include <string.h>

#include "cybsp.h"
#include "cycfg_peripherals.h"
#include "cycfg_qspi_memslot.h"
#include "ipc.h"

#define DEMO_FLASH_ADDRESS (0x60880000UL)
#define DEMO_FLASH_OFFSET  (0x00880000UL)
#define DEMO_FLASH_LENGTH  (16U)
#define DEMO_CMD_PASS      (0x90U)
#define DEMO_CMD_FAIL      (0x91U)
#define DEMO_CMD_STARTED   (0x92U)

static ipc_interface_t ipc_interface;

__attribute__((section(".cy_dtcm")))
static const uint8_t demo_data[DEMO_FLASH_LENGTH] = {
    0x00U, 0x11U, 0x22U, 0x33U,
    0x44U, 0x55U, 0x66U, 0x77U,
    0x88U, 0x99U, 0xAAU, 0xBBU,
    0xCCU, 0xDDU, 0xEEU, 0xFFU,
};

__attribute__((section(".cy_ramfunc")))
static cy_rslt_t erase_sector(void)
{
    return (cy_rslt_t)Cy_SMIF_MemEraseSector(
        SMIF0_CORE, &S25HS512T_SMIF0_SlaveSlot_1,
        DEMO_FLASH_OFFSET, 0x40000U,
        (cy_stc_smif_context_t *)&CYBSP_SMIF_CORE_0_XSPI_FLASH_hal_config.context);
}

__attribute__((section(".cy_ramfunc")))
static cy_rslt_t program_data(void)
{
    return (cy_rslt_t)Cy_SMIF_MemWrite(
        SMIF0_CORE, &S25HS512T_SMIF0_SlaveSlot_1,
        DEMO_FLASH_OFFSET, demo_data, sizeof(demo_data),
        (cy_stc_smif_context_t *)&CYBSP_SMIF_CORE_0_XSPI_FLASH_hal_config.context);
}

__attribute__((section(".cy_ramfunc")))
static cy_rslt_t read_data(uint8_t *buffer, size_t length)
{
    return (cy_rslt_t)Cy_SMIF_MemRead(
        SMIF0_CORE, &S25HS512T_SMIF0_SlaveSlot_1,
        DEMO_FLASH_OFFSET, buffer, length,
        (cy_stc_smif_context_t *)&CYBSP_SMIF_CORE_0_XSPI_FLASH_hal_config.context);
}

int main(void)
{
    cy_rslt_t result;
    uint8_t readback[DEMO_FLASH_LENGTH] = {0U};
    bool demo_passed;

    result = cybsp_init();
    CY_ASSERT(result == CY_RSLT_SUCCESS);
    __enable_irq();
    ipc_interface_init(&ipc_interface);

    /* Allow CM33 to construct its callback before sending the result. */
    Cy_SysLib_Delay(1000U);
    ipc_interface.base.send(&ipc_interface.base, DEMO_CMD_STARTED, 0U);

    result = erase_sector();
    if (result != CY_RSLT_SUCCESS) {
        ipc_interface.base.send(&ipc_interface.base, DEMO_CMD_FAIL, (uint32_t)result);
        for (;;) {
        }
    }

    result = program_data();
    if (result != CY_RSLT_SUCCESS) {
        ipc_interface.base.send(&ipc_interface.base, DEMO_CMD_FAIL, (uint32_t)result);
        for (;;) {
        }
    }

    result = read_data(readback, sizeof(readback));
    if (result != CY_RSLT_SUCCESS) {
        ipc_interface.base.send(&ipc_interface.base, DEMO_CMD_FAIL, (uint32_t)result);
        for (;;) {
        }
    }

    demo_passed = memcmp(demo_data, readback, sizeof(demo_data)) == 0;

    ipc_interface.base.send(&ipc_interface.base,
                            demo_passed ? DEMO_CMD_PASS : DEMO_CMD_FAIL,
                            DEMO_FLASH_ADDRESS);

    for (;;) {
    }
}
