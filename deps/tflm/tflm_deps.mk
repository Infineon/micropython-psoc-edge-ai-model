################################################################################
# Generic TFLM dependency boundary for CM55 demos.
#
# Demo-owned code lives under cm55_firmware/framework/tflm.
# MTB assets are reused only for board bring-up, PDL/HAL, ML middleware support,
# and the prebuilt TensorFlow Lite Micro static library shipped with PSOC Edge.
################################################################################

# Board/project-owned sources required for CM55 boot and generated BSP config.
TFLM_APP_C_SRCS := \
    $(APP_DIR)/sources/platform/profiler.c \
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

# Remove framework-specific audio/voice/Bluetooth payloads from board.mk.
TFLM_LIB_C_SRCS := $(filter-out \
    $(LIB_AFE)/% \
    $(LIB_AVC)/% \
    $(LIB_BTFW)/% \
    $(LIB_SOD)/% \
    $(LIB_VA)/%, \
    $(LIB_C_SRCS))

TFLM_LIB_CXX_SRCS := $(filter-out \
    $(LIB_AVC)/%, \
    $(LIB_CXX_SRCS))

# Keep only the generic TFLM runtime archive from MTB ml-tflite-micro.
TFLM_LDLIBS := \
    $(LIB_TFLM)/COMPONENT_ML_TFLM/COMPONENT_U55/TOOLCHAIN_LLVM_ARM/libtensorflow-microlite.a

# Drop include paths that belong only to the removed audio/voice framework assets.
TFLM_INCLUDES := $(filter-out \
    -I$(APP_DIR)/adapters/deepcraft \
    -I$(LIB_AFE)% \
    -I$(LIB_AVC)% \
    -I$(LIB_SOD)% \
    -I$(LIB_VA)%, \
    $(INCLUDES))
