################################################################################
# board.mk — PSoC Edge KIT_PSE84_AI hardware configuration
#
# Included by cm55_firmware/Makefile and framework/deepcraft/Makefile.
# All board-specific paths, toolchain, flags, defines, includes, BSP and
# SDK library sources live here; nothing hardware-specific should appear
# inside a framework Makefile.
#
# Required inputs (must be set before including this file):
#   APPNAME, CONFIG, DEEPCRAFT_PROJECT
#   BOARD_DIR   — absolute path to cm55_firmware/
#   BUILD_DIR   — absolute path to the build output directory
################################################################################

################################################################################
# Derived root paths
################################################################################

REPO_ROOT  := $(abspath $(BOARD_DIR)/..)
PSE_DIR    := $(REPO_ROOT)/deps/psoc-edge
SHARED_DIR := $(REPO_ROOT)/shared
APP_DIR    := $(BOARD_DIR)

BSP_DIR    := $(PSE_DIR)/TARGET_KIT_PSE84_AI

LIB_ABS_RTOS    := $(PSE_DIR)/abstraction-rtos
LIB_ASYNC       := $(PSE_DIR)/async-transfer
LIB_AFE         := $(PSE_DIR)/audio-front-end
LIB_AVC         := $(PSE_DIR)/audio-voice-core
LIB_BTFW        := $(PSE_DIR)/bt-fw-mur-cyw55513
LIB_CLIB        := $(PSE_DIR)/clib-support
LIB_CMSIS       := $(PSE_DIR)/cmsis
LIB_CONN_UTILS  := $(PSE_DIR)/connectivity-utilities
LIB_CORELIB     := $(PSE_DIR)/core-lib
LIB_FREERTOS    := $(PSE_DIR)/freertos
LIB_ML_MW       := $(PSE_DIR)/ml-middleware
LIB_TFLM        := $(PSE_DIR)/ml-tflite-micro
LIB_PSE         := $(PSE_DIR)/mtb-dsl-pse8xxgp
LIB_IPC         := $(PSE_DIR)/mtb-ipc
LIB_SRF         := $(PSE_DIR)/mtb-srf
LIB_RETARGET    := $(PSE_DIR)/retarget-io
LIB_SE_RT       := $(PSE_DIR)/se-rt-services-utils
LIB_SOD         := $(PSE_DIR)/speech-onset-detection
LIB_VA          := $(PSE_DIR)/voice-assistant

################################################################################
# Cross-platform shell / clean helpers
################################################################################

RM_RF := $(if $(filter $(OS),Windows_NT),cmd /c rd /s /q,rm -rf)

ifeq ($(OS),Windows_NT)
    _IS_WSL := $(wildcard /proc/version)
    ifneq ($(_IS_WSL),)
        SHELL := /bin/sh
        ifneq (,$(findstring :,$(LLVM_DIR)))
            override undefine LLVM_DIR
        endif
    endif
else
    SHELL := /bin/sh
endif

################################################################################
# Toolchain
################################################################################

# TOOLCHAIN=LLVM_ARM (default) or GCC_ARM. NOTE: the deepcraft framework's
# prebuilt audio-voice-core archives (ifx_va.a/sp_enh.a) only ship
# TOOLCHAIN_LLVM_ARM/TOOLCHAIN_ARM variants -- GCC_ARM only works for
# frameworks that don't link those (e.g. tflm, shared_flash_read). The tflm
# framework Makefile forces TOOLCHAIN=GCC_ARM unconditionally (see there).
TOOLCHAIN     ?= LLVM_ARM
TOOLCHAIN_DIR := TOOLCHAIN_$(TOOLCHAIN)
OPENOCD       := openocd

ifeq ($(TOOLCHAIN),GCC_ARM)
    CC      := arm-none-eabi-gcc
    CXX     := arm-none-eabi-g++
    AS      := arm-none-eabi-gcc
    LD      := arm-none-eabi-g++
    AR      := arm-none-eabi-ar
    OBJCOPY := arm-none-eabi-objcopy
    SIZE    := arm-none-eabi-size

    ifdef GCC_ARM_DIR
        CC      := $(GCC_ARM_DIR)/bin/arm-none-eabi-gcc
        CXX     := $(GCC_ARM_DIR)/bin/arm-none-eabi-g++
        AS      := $(GCC_ARM_DIR)/bin/arm-none-eabi-gcc
        LD      := $(GCC_ARM_DIR)/bin/arm-none-eabi-g++
        AR      := $(GCC_ARM_DIR)/bin/arm-none-eabi-ar
        OBJCOPY := $(GCC_ARM_DIR)/bin/arm-none-eabi-objcopy
        SIZE    := $(GCC_ARM_DIR)/bin/arm-none-eabi-size
        export PATH := $(subst \,/,$(GCC_ARM_DIR))/bin:$(PATH)
    endif
else
    CC      := clang
    CXX     := clang++
    AS      := clang
    LD      := clang
    AR      := llvm-ar
    OBJCOPY := llvm-objcopy
    SIZE    := llvm-size

    ifdef LLVM_DIR
        CC      := $(LLVM_DIR)/bin/clang
        CXX     := $(LLVM_DIR)/bin/clang++
        AS      := $(LLVM_DIR)/bin/clang
        LD      := $(LLVM_DIR)/bin/clang
        AR      := $(LLVM_DIR)/bin/llvm-ar
        OBJCOPY := $(LLVM_DIR)/bin/llvm-objcopy
        SIZE    := $(LLVM_DIR)/bin/llvm-size
        export PATH := $(subst \,/,$(LLVM_DIR))/bin:$(PATH)
    endif
endif

################################################################################
# Compiler / linker flags
################################################################################

# GCC infers the Cortex-M55 FPU/MVE config from -mcpu alone; clang needs the
# FPU spelled out explicitly (its -mfpu= naming differs from GCC's).
ifeq ($(TOOLCHAIN),GCC_ARM)
    FPU_FLAGS :=
else
    FPU_FLAGS := -mfpu=fp-armv8-fullfp16-d16
endif

CPU_FLAGS := \
    -mcpu=cortex-m55 \
    -mthumb \
    -mfloat-abi=hard \
    $(FPU_FLAGS)

ifeq ($(CONFIG),Debug)
    # CMSIS-NN's hand-written MVE inline asm can hit "impossible constraints"
    # under GCC at -O0 (not enough spare registers without any optimization);
    # -Og keeps debuggability while giving GCC enough room to allocate them.
    ifeq ($(TOOLCHAIN),GCC_ARM)
        OPT_FLAGS := -Og -g
    else
        OPT_FLAGS := -O0 -g
    endif
    DEFINES_CFG := -DCOMPONENT_Debug -DDEBUG
else ifeq ($(CONFIG),Release)
    OPT_FLAGS   := -Os
    DEFINES_CFG := -DCOMPONENT_Release -DNDEBUG
else
    OPT_FLAGS   := -O0 -g
    DEFINES_CFG := -DCOMPONENT_Debug -DDEBUG
endif

ifeq ($(TOOLCHAIN),GCC_ARM)
    # CMSIS-DSP's MVE (Helium) intrinsics rely on lax vector-to-vector
    # conversions that clang allows by default but GCC does not.
    TOOLCHAIN_FLAGS  := -flax-vector-conversions
    TOOLCHAIN_LDLIBS :=
else
    TOOLCHAIN_FLAGS  := --target=arm-none-eabi -fintegrated-cc1 -fintegrated-objemitter -DMTB_LLVM_EMBEDDED_ARM
    TOOLCHAIN_LDLIBS := -lc++ -lc++abi
endif

COMMON_FLAGS := \
    $(CPU_FLAGS) \
    $(OPT_FLAGS) \
    -ffunction-sections \
    -fdata-sections \
    -fshort-enums \
    -Wall \
    $(TOOLCHAIN_FLAGS)

CFLAGS   := $(COMMON_FLAGS) -c -MMD -MP
CXXFLAGS := $(COMMON_FLAGS) -c -MMD -MP -std=c++17 -fno-exceptions -fno-rtti

ASFLAGS := \
    $(CPU_FLAGS) \
    $(OPT_FLAGS) \
    -c -MMD -MP \
    $(TOOLCHAIN_FLAGS)

LDFLAGS := \
    $(CPU_FLAGS) \
    $(OPT_FLAGS) \
    -ffunction-sections \
    -fdata-sections \
    -Wall \
    $(TOOLCHAIN_FLAGS) \
    -Wl,--gc-sections \
    -Xlinker -L$(BSP_DIR)/COMPONENT_CM55/$(TOOLCHAIN_DIR) \
    -Xlinker -L$(APP_DIR)/sources/bsp-cfg \
    -T$(BSP_DIR)/COMPONENT_CM55/$(TOOLCHAIN_DIR)/pse84_ns_cm55.ld \
    -Wl,-Map,$(BUILD_DIR)/$(APPNAME).map

################################################################################
# Preprocessor defines
################################################################################

DEFINES := \
    $(DEFINES_CFG) \
    -DBLHS_SUPPORT \
    -DCOMPONENT_55500 \
    -DCOMPONENT_55500A1 \
    -DCOMPONENT_APP_KIT_PSE84_AI \
    -DCOMPONENT_AVC_DEMO \
    -DCOMPONENT_CM55 \
    -DCOMPONENT_CM55_0 \
    -DCOMPONENT_CMSIS_DSP \
    -DCOMPONENT_FREERTOS \
    -DCOMPONENT_HARDFP \
    -DCOMPONENT_LLVM_ARM \
    -DCOMPONENT_ML_INT8x8 \
    -DCOMPONENT_ML_TFLM \
    -DCOMPONENT_MTB_DEVICE_SUPPORT \
    -DCOMPONENT_MTB_HAL \
    -DCOMPONENT_MW_ASYNC_TRANSFER \
    -DCOMPONENT_MW_DSNS_MODEL \
    -DCOMPONENT_MW_MDL_DSES \
    -DCOMPONENT_MW_MDL_DSNS \
    -DCOMPONENT_MW_MDL_VA \
    -DCOMPONENT_MW_MTB_DSL_PSE8XXGP \
    -DCOMPONENT_MW_MTB_IPC \
    -DCOMPONENT_MW_MTB_SRF \
    -DCOMPONENT_MW_SE_RT_SERVICES_UTILS \
    -DCOMPONENT_NON_SECURE_DEVICE \
    -DCOMPONENT_PSE84 \
    -DCOMPONENT_RTOS_AWARE \
    -DCOMPONENT_U55 \
    -DCORE_NAME_CM55_0=1 \
    -DCYBSP_MCUBOOT_HEADER_SIZE=0x400 \
    -DCY_APPNAME_$(APPNAME) \
    -DCY_ML_MODEL_MEM=.cy_socmem_data \
    -DCY_RETARGET_IO_CONVERT_LF_TO_CRLF \
    -DCY_RTOS_AWARE \
    -DCY_SUPPORTS_DEVICE_VALIDATION \
    -DCY_TARGET_BOARD=APP_KIT_PSE84_AI \
    -DDSNS_ENABLE_MTB_ML \
    -DENABLE_AFE_MW_SUPPORT \
    -DENABLE_IFX_PRE_PROCESS_HPF \
    -DENABLE_IFX_SOD \
    -DENABLE_IFX_VA_CMD \
    -DENABLE_IFX_VA_WWD \
    -DPROJECT_PREFIX=$(DEEPCRAFT_PROJECT) \
    -DPSE846GPS2DBZC4A \
    -DTARGET_KIT_PSE84_AI \
    -DTF_LITE_STATIC_MEMORY \
    -DTRXV5 \
    -DUSE_AUDIO_ENHANCEMENT \
    -DUSE_KIT_PSE84_AI \
    -DUSE_MTB_ML \
    -DFLASH_BOOT \
    -DCY_PDL_FLASH_BOOT

################################################################################
# Include paths
################################################################################

INCLUDES := \
    -I$(APP_DIR) \
    -I$(APP_DIR)/includes \
    -I$(APP_DIR)/sources \
    -I$(APP_DIR)/sources/bsp-cfg \
    -I$(APP_DIR)/adapters/deepcraft \
    -I$(SHARED_DIR)/include \
    \
    -I$(BSP_DIR) \
    -I$(BSP_DIR)/bluetooth \
    \
    -I$(LIB_ABS_RTOS)/include \
    -I$(LIB_ABS_RTOS)/include/COMPONENT_FREERTOS \
    \
    -I$(LIB_ASYNC)/include \
    \
    -I$(LIB_AFE)/include \
    -I$(LIB_AFE)/source/include \
    \
    -I$(LIB_AVC)/lib/SP_ENH/COMPONENT_CM55/inc \
    -I$(LIB_AVC)/lib/SP_ENH/COMPONENT_CM55/src/inc \
    -I$(LIB_AVC)/lib/SP_ENH/COMPONENT_CM55/MODEL/COMPONENT_ML_TFLM \
    \
    -I$(LIB_CLIB)/include \
    -I$(LIB_CLIB)/source/$(TOOLCHAIN_DIR) \
    \
    -I$(LIB_CMSIS)/Core/Include \
    -I$(LIB_CMSIS)/Core/Include/a-profile \
    -I$(LIB_CMSIS)/Core/Include/m-profile \
    -I$(LIB_CMSIS)/Core/Include/r-profile \
    -I$(LIB_CMSIS)/COMPONENT_CMSIS_DSP/Include \
    -I$(LIB_CMSIS)/COMPONENT_CMSIS_DSP/PrivateInclude \
    \
    -I$(LIB_CORELIB)/include \
    \
    -I$(LIB_CONN_UTILS) \
    -I$(LIB_CONN_UTILS)/JSON_parser \
    -I$(LIB_CONN_UTILS)/cy_log \
    -I$(LIB_CONN_UTILS)/cy_string \
    -I$(LIB_CONN_UTILS)/linked_list \
    -I$(LIB_CONN_UTILS)/network \
    \
    -I$(LIB_FREERTOS)/Source/include \
    -I$(LIB_FREERTOS)/Source/portable/COMPONENT_CM55/$(TOOLCHAIN_DIR) \
    \
    -I$(LIB_ML_MW)/include \
    -I$(LIB_ML_MW)/source/COMPONENT_ML_TFLM \
    \
    -I$(LIB_TFLM)/COMPONENT_ML_TFLM/include \
    -I$(LIB_TFLM)/COMPONENT_ML_TFLM/include/flatbuffers/include \
    -I$(LIB_TFLM)/COMPONENT_ML_TFLM/include/gemmlowp \
    -I$(LIB_TFLM)/COMPONENT_ML_TFLM/include/ethos-u-core-driver \
    \
    -I$(LIB_PSE)/device-utils/syspm/hobto/pdm_pcm/audioss_pdm \
    -I$(LIB_PSE)/device-utils/syspm/hobto/pdm_pcm/mxpdm \
    -I$(LIB_PSE)/device-utils/syspm/include \
    -I$(LIB_PSE)/hal/include \
    -I$(LIB_PSE)/nnkernel/COMPONENT_CM33/include \
    -I$(LIB_PSE)/pdl/devices/include \
    -I$(LIB_PSE)/pdl/devices/include/ip \
    -I$(LIB_PSE)/pdl/drivers/include \
    -I$(LIB_PSE)/pdl/drivers/third_party/ethernet/include \
    -I$(LIB_PSE)/pdl/drivers/third_party/COMPONENT_U55/include \
    \
    -I$(LIB_IPC)/include \
    \
    -I$(LIB_SRF)/include \
    -I$(LIB_SRF)/include/COMPONENT_NON_SECURE_DEVICE \
    -I$(LIB_SRF)/include/COMPONENT_NON_SECURE_DEVICE/COMPONENT_MW_MTB_IPC \
    \
    -I$(LIB_RETARGET)/include \
    \
    -I$(LIB_SE_RT) \
    \
    -I$(LIB_SOD)/include \
    -I$(LIB_SOD)/source/inc \
    \
    -I$(LIB_VA)/include \
    -I$(LIB_VA)/source/va_core

################################################################################
# BSP source files
################################################################################

BSP_C_SRCS := \
    $(BSP_DIR)/cybsp.c \
    $(BSP_DIR)/system_edge.c \
    $(BSP_DIR)/bluetooth/cybsp_bt_config.c \
    $(BSP_DIR)/COMPONENT_CM55/COMPONENT_NON_SECURE_DEVICE/ns_start_pse84.c \
    $(BSP_DIR)/COMPONENT_CM55/COMPONENT_NON_SECURE_DEVICE/ns_system_pse84.c \
    $(APP_DIR)/sources/bsp-cfg/cy_afe_configurator_settings.c \
    $(APP_DIR)/sources/bsp-cfg/cycfg.c \
    $(APP_DIR)/sources/bsp-cfg/cycfg_clocks.c \
    $(APP_DIR)/sources/bsp-cfg/cycfg_dmas.c \
    $(APP_DIR)/sources/bsp-cfg/cycfg_peripheral_clocks.c \
    $(APP_DIR)/sources/bsp-cfg/cycfg_peripherals.c \
    $(APP_DIR)/sources/bsp-cfg/cycfg_pins.c \
    $(APP_DIR)/sources/bsp-cfg/cycfg_protection.c \
    $(APP_DIR)/sources/bsp-cfg/cycfg_qspi_memslot.c \
    $(APP_DIR)/sources/bsp-cfg/cycfg_routing.c \
    $(APP_DIR)/sources/bsp-cfg/cycfg_system.c

# NOTE: BSP config/GeneratedSource/cycfg*.c are excluded here; the project's
# bsp-cfg/cycfg*.c files above override them to avoid duplicate symbols.

################################################################################
# SDK library source files
################################################################################

# abstraction-rtos (FreeRTOS backend)
LIB_C_SRCS := \
    $(LIB_ABS_RTOS)/source/cy_worker_thread.c \
    $(LIB_ABS_RTOS)/source/COMPONENT_FREERTOS/cyabs_freertos_common.c \
    $(LIB_ABS_RTOS)/source/COMPONENT_FREERTOS/cyabs_freertos_helpers.c \
    $(LIB_ABS_RTOS)/source/COMPONENT_FREERTOS/cyabs_rtos_dsram.c \
    $(LIB_ABS_RTOS)/source/COMPONENT_FREERTOS/cyabs_rtos_freertos.c

# async-transfer
LIB_C_SRCS += \
    $(LIB_ASYNC)/source/mtb_async_transfer.c

# clib-support (GCC_ARM/newlib handles FreeRTOS reentrancy in one file;
# LLVM_ARM/picolibc needs a separate COMPONENT_FREERTOS glue file)
LIB_C_SRCS += \
    $(LIB_CLIB)/source/cy_time.c \
    $(LIB_CLIB)/source/COMPONENT_FREERTOS/cy_mutex_pool.c

ifeq ($(TOOLCHAIN),GCC_ARM)
LIB_C_SRCS += \
    $(LIB_CLIB)/source/TOOLCHAIN_GCC_ARM/cy_clib_support_newlib.c
else
LIB_C_SRCS += \
    $(LIB_CLIB)/source/TOOLCHAIN_LLVM_ARM/cy_clib_support_llvm_arm_picolibc.c \
    $(LIB_CLIB)/source/TOOLCHAIN_LLVM_ARM/COMPONENT_FREERTOS/cy_clib_support_llvm_arm_freertos.c
endif

# cmsis-dsp
CMSIS_DSP_SRCS := $(wildcard $(LIB_CMSIS)/COMPONENT_CMSIS_DSP/Source/*.c) \
                  $(wildcard $(LIB_CMSIS)/COMPONENT_CMSIS_DSP/Source/*/*.c)
LIB_C_SRCS += $(CMSIS_DSP_SRCS)

# connectivity-utilities
LIB_C_SRCS += \
    $(LIB_CONN_UTILS)/JSON_parser/cy_json_parser.c \
    $(LIB_CONN_UTILS)/cy_log/cy_log.c \
    $(LIB_CONN_UTILS)/cy_string/cy_string_utils.c \
    $(LIB_CONN_UTILS)/linked_list/cy_linked_list.c \
    $(LIB_CONN_UTILS)/network/cy_nw_helper_common.c

# FreeRTOS kernel
LIB_C_SRCS += \
    $(LIB_FREERTOS)/Source/croutine.c \
    $(LIB_FREERTOS)/Source/event_groups.c \
    $(LIB_FREERTOS)/Source/list.c \
    $(LIB_FREERTOS)/Source/queue.c \
    $(LIB_FREERTOS)/Source/stream_buffer.c \
    $(LIB_FREERTOS)/Source/tasks.c \
    $(LIB_FREERTOS)/Source/timers.c \
    $(LIB_FREERTOS)/Source/portable/MemMang/heap_3.c \
    $(LIB_FREERTOS)/Source/portable/COMPONENT_CM55/$(TOOLCHAIN_DIR)/port.c \
    $(LIB_FREERTOS)/Source/portable/COMPONENT_CM55/$(TOOLCHAIN_DIR)/portasm.c \
    $(LIB_FREERTOS)/Source/portable/COMPONENT_CM55/$(TOOLCHAIN_DIR)/mpu_wrappers_v2_asm.c

# ml-middleware -- moved to framework/deepcraft/Makefile (DEEPCRAFT_LIB_*).

# PSoC Edge PDL drivers (79 of 91; 12 v1-crypto + cy_pdl_srf GC'd in firmware.map)
LIB_C_SRCS += \
    $(LIB_PSE)/pdl/drivers/source/cy_autanalog.c \
    $(LIB_PSE)/pdl/drivers/source/cy_autanalog_ac.c \
    $(LIB_PSE)/pdl/drivers/source/cy_autanalog_ctb.c \
    $(LIB_PSE)/pdl/drivers/source/cy_autanalog_dac.c \
    $(LIB_PSE)/pdl/drivers/source/cy_autanalog_ptc.c \
    $(LIB_PSE)/pdl/drivers/source/cy_autanalog_sar.c \
    $(LIB_PSE)/pdl/drivers/source/cy_axidmac.c \
    $(LIB_PSE)/pdl/drivers/source/cy_canfd.c \
    $(LIB_PSE)/pdl/drivers/source/cy_crypto.c \
    $(LIB_PSE)/pdl/drivers/source/cy_crypto_core_aes_v2.c \
    $(LIB_PSE)/pdl/drivers/source/cy_crypto_core_cmac_v2.c \
    $(LIB_PSE)/pdl/drivers/source/cy_crypto_core_crc_v2.c \
    $(LIB_PSE)/pdl/drivers/source/cy_crypto_core_des_v2.c \
    $(LIB_PSE)/pdl/drivers/source/cy_crypto_core_ecc_domain_params.c \
    $(LIB_PSE)/pdl/drivers/source/cy_crypto_core_ecc_ec25519.c \
    $(LIB_PSE)/pdl/drivers/source/cy_crypto_core_ecc_ecdsa.c \
    $(LIB_PSE)/pdl/drivers/source/cy_crypto_core_ecc_eddsa.c \
    $(LIB_PSE)/pdl/drivers/source/cy_crypto_core_ecc_key_gen.c \
    $(LIB_PSE)/pdl/drivers/source/cy_crypto_core_ecc_nist_p.c \
    $(LIB_PSE)/pdl/drivers/source/cy_crypto_core_hkdf_v2.c \
    $(LIB_PSE)/pdl/drivers/source/cy_crypto_core_hmac_v2.c \
    $(LIB_PSE)/pdl/drivers/source/cy_crypto_core_hw.c \
    $(LIB_PSE)/pdl/drivers/source/cy_crypto_core_mem_v2.c \
    $(LIB_PSE)/pdl/drivers/source/cy_crypto_core_prng_v2.c \
    $(LIB_PSE)/pdl/drivers/source/cy_crypto_core_rsa.c \
    $(LIB_PSE)/pdl/drivers/source/cy_crypto_core_sha_v2.c \
    $(LIB_PSE)/pdl/drivers/source/cy_crypto_core_trng.c \
    $(LIB_PSE)/pdl/drivers/source/cy_crypto_core_vu.c \
    $(LIB_PSE)/pdl/drivers/source/cy_crypto_server.c \
    $(LIB_PSE)/pdl/drivers/source/cy_crypto_utils.c \
    $(LIB_PSE)/pdl/drivers/source/cy_dma.c \
    $(LIB_PSE)/pdl/drivers/source/cy_ephy.c \
    $(LIB_PSE)/pdl/drivers/source/cy_ethif.c \
    $(LIB_PSE)/pdl/drivers/source/cy_gpio.c \
    $(LIB_PSE)/pdl/drivers/source/cy_i3c.c \
    $(LIB_PSE)/pdl/drivers/source/cy_ipc_drv.c \
    $(LIB_PSE)/pdl/drivers/source/cy_ipc_pipe.c \
    $(LIB_PSE)/pdl/drivers/source/cy_ipc_sema.c \
    $(LIB_PSE)/pdl/drivers/source/cy_lpcomp.c \
    $(LIB_PSE)/pdl/drivers/source/cy_lvd.c \
    $(LIB_PSE)/pdl/drivers/source/cy_mcwdt.c \
    $(LIB_PSE)/pdl/drivers/source/cy_mipidsi.c \
    $(LIB_PSE)/pdl/drivers/source/cy_mpc.c \
    $(LIB_PSE)/pdl/drivers/source/cy_ms_ctl.c \
    $(LIB_PSE)/pdl/drivers/source/cy_nnlite.c \
    $(LIB_PSE)/pdl/drivers/source/cy_pdl_srf_common.c \
    $(LIB_PSE)/pdl/drivers/source/cy_pdm_pcm_v2.c \
    $(LIB_PSE)/pdl/drivers/source/cy_ppc.c \
    $(LIB_PSE)/pdl/drivers/source/cy_rram.c \
    $(LIB_PSE)/pdl/drivers/source/cy_rtc.c \
    $(LIB_PSE)/pdl/drivers/source/cy_scb_common.c \
    $(LIB_PSE)/pdl/drivers/source/cy_scb_ezi2c.c \
    $(LIB_PSE)/pdl/drivers/source/cy_scb_i2c.c \
    $(LIB_PSE)/pdl/drivers/source/cy_scb_spi.c \
    $(LIB_PSE)/pdl/drivers/source/cy_scb_uart.c \
    $(LIB_PSE)/pdl/drivers/source/cy_sd_host.c \
    $(LIB_PSE)/pdl/drivers/source/cy_smartio.c \
    $(LIB_PSE)/pdl/drivers/source/cy_smif.c \
    $(LIB_PSE)/pdl/drivers/source/cy_smif_hb_flash.c \
    $(LIB_PSE)/pdl/drivers/source/cy_smif_memnum.c \
    $(LIB_PSE)/pdl/drivers/source/cy_smif_memslot.c \
    $(LIB_PSE)/pdl/drivers/source/cy_smif_sfdp.c \
    $(LIB_PSE)/pdl/drivers/source/cy_sysclk_v2.c \
    $(LIB_PSE)/pdl/drivers/source/cy_sysfault.c \
    $(LIB_PSE)/pdl/drivers/source/cy_sysint_v2.c \
    $(LIB_PSE)/pdl/drivers/source/cy_syslib.c \
    $(LIB_PSE)/pdl/drivers/source/cy_syspm_pdcm.c \
    $(LIB_PSE)/pdl/drivers/source/cy_syspm_ppu.c \
    $(LIB_PSE)/pdl/drivers/source/cy_syspm_v4.c \
    $(LIB_PSE)/pdl/drivers/source/cy_systick_v2.c \
    $(LIB_PSE)/pdl/drivers/source/cy_systrimm.c \
    $(LIB_PSE)/pdl/drivers/source/cy_tcpwm_counter.c \
    $(LIB_PSE)/pdl/drivers/source/cy_tcpwm_pwm.c \
    $(LIB_PSE)/pdl/drivers/source/cy_tcpwm_quaddec.c \
    $(LIB_PSE)/pdl/drivers/source/cy_tcpwm_shiftreg.c \
    $(LIB_PSE)/pdl/drivers/source/cy_tdm.c \
    $(LIB_PSE)/pdl/drivers/source/cy_trigmux.c \
    $(LIB_PSE)/pdl/drivers/source/cy_wdt.c \
    $(LIB_PSE)/pdl/drivers/source/ppu_v1.c

# PSoC Edge PDL LLVM assembly
LIB_S_SRCS := \
    $(LIB_PSE)/pdl/drivers/source/$(TOOLCHAIN_DIR)/cy_syslib_ext.S

# PSoC Edge PDL device
LIB_C_SRCS += \
    $(LIB_PSE)/pdl/devices/source/cy_device.c

# PSoC Edge HAL (23 sources confirmed live in firmware.map)
LIB_C_SRCS += \
    $(LIB_PSE)/hal/source/mtb_hal_adc_sar.c \
    $(LIB_PSE)/hal/source/mtb_hal_clock.c \
    $(LIB_PSE)/hal/source/mtb_hal_comp.c \
    $(LIB_PSE)/hal/source/mtb_hal_dma.c \
    $(LIB_PSE)/hal/source/mtb_hal_dma_dmac.c \
    $(LIB_PSE)/hal/source/mtb_hal_dma_dw.c \
    $(LIB_PSE)/hal/source/mtb_hal_gpio.c \
    $(LIB_PSE)/hal/source/mtb_hal_i2c.c \
    $(LIB_PSE)/hal/source/mtb_hal_lptimer.c \
    $(LIB_PSE)/hal/source/mtb_hal_lptimer_common.c \
    $(LIB_PSE)/hal/source/mtb_hal_lptimer_mcwdt.c \
    $(LIB_PSE)/hal/source/mtb_hal_memoryspi.c \
    $(LIB_PSE)/hal/source/mtb_hal_nvm.c \
    $(LIB_PSE)/hal/source/mtb_hal_nvm_mxs22rramc.c \
    $(LIB_PSE)/hal/source/mtb_hal_pwm.c \
    $(LIB_PSE)/hal/source/mtb_hal_rtc.c \
    $(LIB_PSE)/hal/source/mtb_hal_sdhc.c \
    $(LIB_PSE)/hal/source/mtb_hal_spi.c \
    $(LIB_PSE)/hal/source/mtb_hal_syspm.c \
    $(LIB_PSE)/hal/source/mtb_hal_system.c \
    $(LIB_PSE)/hal/source/mtb_hal_timer.c \
    $(LIB_PSE)/hal/source/mtb_hal_uart.c \
    $(LIB_PSE)/hal/source/mtb_hal_utils_impl.c

# PSoC Edge device-utils syspm callbacks (7 sources confirmed live)
LIB_C_SRCS += \
    $(LIB_PSE)/device-utils/syspm/source/mtb_syspm_callbacks_dma.c \
    $(LIB_PSE)/device-utils/syspm/source/mtb_syspm_callbacks_i2s_tdm.c \
    $(LIB_PSE)/device-utils/syspm/source/mtb_syspm_callbacks_nvm.c \
    $(LIB_PSE)/device-utils/syspm/source/mtb_syspm_callbacks_pdm_pcm.c \
    $(LIB_PSE)/device-utils/syspm/source/mtb_syspm_callbacks_scb_uart.c \
    $(LIB_PSE)/device-utils/syspm/source/mtb_syspm_callbacks_smif.c \
    $(LIB_PSE)/device-utils/syspm/source/mtb_syspm_callbacks_tcpwm.c

# PSoC Edge stdlib stubs
LIB_C_SRCS += \
    $(LIB_PSE)/support/source/mtb_stdlib_stubs.c

# PSoC Edge Ethos-U55 driver
LIB_C_SRCS += \
    $(LIB_PSE)/pdl/drivers/third_party/COMPONENT_U55/src/ethosu_device_u55_u65.c \
    $(LIB_PSE)/pdl/drivers/third_party/COMPONENT_U55/src/ethosu_device_u85.c \
    $(LIB_PSE)/pdl/drivers/third_party/COMPONENT_U55/src/ethosu_driver.c \
    $(LIB_PSE)/pdl/drivers/third_party/COMPONENT_U55/src/ethosu_pmu.c

# Ethernet third-party (edd.c, edd_rx.c, edd_tx.c) fully GC'd in firmware.map — excluded.

# mtb-ipc
LIB_C_SRCS += \
    $(LIB_IPC)/source/mtb_ipc.c

# mtb-srf
LIB_C_SRCS += \
    $(LIB_SRF)/source/mtb_srf.c \
    $(LIB_SRF)/source/COMPONENT_NON_SECURE_DEVICE/mtb_srf_pool.c \
    $(LIB_SRF)/source/COMPONENT_NON_SECURE_DEVICE/COMPONENT_MW_MTB_IPC/mtb_srf_ipc.c

# retarget-io
LIB_C_SRCS += \
    $(LIB_RETARGET)/source/cy_retarget_io.c

# se-rt-services-utils
LIB_C_SRCS += \
    $(LIB_SE_RT)/ifx_se_crc32.c \
    $(LIB_SE_RT)/ifx_se_fih.c \
    $(LIB_SE_RT)/ifx_se_platform.c \
    $(LIB_SE_RT)/ifx_se_psacrypto.c \
    $(LIB_SE_RT)/ifx_se_ss_wifi.c \
    $(LIB_SE_RT)/ifx_se_syscall.c \
    $(LIB_SE_RT)/ifx_se_syscall_builtin.c

# speech-onset-detection and voice-assistant -- moved to
# framework/deepcraft/Makefile (DEEPCRAFT_LIB_C_SRCS).

################################################################################
# Pre-built static libraries
################################################################################

# ifx_va.a/sp_enh.a (audio-voice-core) and libtensorflow-microlite.a
# (Infineon's prebuilt TFLM, used by DeepCraft's audio pipeline) are
# DeepCraft-only and TOOLCHAIN_LLVM_ARM-only -- owned by
# framework/deepcraft/Makefile (DEEPCRAFT_LDLIBS), not board.mk.
LDLIBS :=

LD_EXTRA_LIBS := $(TOOLCHAIN_LDLIBS)

################################################################################
# OpenOCD / flash programming
################################################################################

ifndef DEV_SERIAL_NUMBER
SERIAL_ADAPTER_CMD =
else
SERIAL_ADAPTER_CMD = -c "adapter serial $(DEV_SERIAL_NUMBER)"
endif

OPENOCD_OPEN_CMDS = \
    -s $(BOARD_DIR)/sources/bsp-cfg \
    -c "set QSPI_FLASHLOADER $(BOARD_DIR)/sources/bsp-cfg/PSE84_SMIF.FLM" \
    -c "source [find interface/kitprog3.cfg]" \
    $(SERIAL_ADAPTER_CMD) \
    -c "transport select swd" \
    -c "source [find target/infineon/pse84xgxs2.cfg]" \
    -c "init" \
    -c "reset init" \
    -c "adapter speed 12000"

OPENOCD_CLOSE_CMDS = \
    -c "reset run; shutdown"

define flash_target
    $(Q)$(OPENOCD) \
        $(OPENOCD_OPEN_CMDS) \
        -c "flash write_image erase $(1)" \
        $(OPENOCD_CLOSE_CMDS)
endef
