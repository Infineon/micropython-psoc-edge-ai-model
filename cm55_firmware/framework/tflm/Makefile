APPNAME          ?= tflm
CONFIG           ?= Debug
BOARD            ?= KIT_PSE84_AI
FRAMEWORK        ?= tflm

BOARD_DIR ?= $(abspath ../..)
BUILD_DIR ?= $(BOARD_DIR)/build/$(CONFIG)

# TensorFlow Lite for Microcontrollers upstream builds/tests with GCC, not
# LLVM/clang -- this framework only supports TOOLCHAIN=GCC_ARM. GCC_ARM_DIR
# can point at a local install; the toolchain image also provides it on
# PATH (via GCC_ARM_DIR/PATH env vars), so it works unset in Docker too.
ifdef TOOLCHAIN
    ifneq ($(TOOLCHAIN),GCC_ARM)
        $(error tflm framework only supports TOOLCHAIN=GCC_ARM (got '$(TOOLCHAIN)'); TensorFlow Lite for Microcontrollers is built/tested upstream with GCC)
    endif
endif
override TOOLCHAIN := GCC_ARM

include $(BOARD_DIR)/board.mk
include $(REPO_ROOT)/deps/tflm/tflm_deps.mk

FRAMEWORK_DIR := $(BOARD_DIR)/framework/tflm
FRAMEWORK_C_SRCS := $(FRAMEWORK_DIR)/main.c
FRAMEWORK_CXX_SRCS := $(FRAMEWORK_DIR)/tflm_runner.cpp

INCLUDES := $(TFLM_INCLUDES) \
    -I$(FRAMEWORK_DIR)

APP_C_SRCS := $(TFLM_APP_C_SRCS)
LIB_C_SRCS := $(TFLM_LIB_C_SRCS)
LIB_CXX_SRCS := $(TFLM_LIB_CXX_SRCS)
LDLIBS := $(TFLM_LDLIBS)

ALL_C_SRCS   := $(FRAMEWORK_C_SRCS) $(APP_C_SRCS) $(BSP_C_SRCS) $(LIB_C_SRCS)
ALL_CXX_SRCS := $(FRAMEWORK_CXX_SRCS) $(LIB_CXX_SRCS)
ALL_S_SRCS   := $(LIB_S_SRCS)

# TFLM runtime core (tensorflow/, signal/, third_party/ from tflm_tree) is
# compiled into its own tflm-core.a instead of linked as loose objects.
# Pass TFLM_PREBUILT_CORE_LIB=/path/to/tflm-core.a (defaults to the
# committed deps/tflm/prebuilt/ archive) to use a prebuilt archive and
# skip compiling these sources entirely.
CORE_C_SRCS   := $(TFLM_CORE_C_SRCS)
CORE_CXX_SRCS := $(TFLM_CORE_CXX_SRCS)

_src_to_obj = $(BUILD_DIR)/obj/$(subst :,_,$(subst /,_,$(subst $(REPO_ROOT)/,,$(abspath $1)))).o

C_OBJS   := $(foreach s,$(ALL_C_SRCS),$(call _src_to_obj,$(s)))
CXX_OBJS := $(foreach s,$(ALL_CXX_SRCS),$(call _src_to_obj,$(s)))
S_OBJS   := $(foreach s,$(ALL_S_SRCS),$(call _src_to_obj,$(s)))
ALL_OBJS := $(C_OBJS) $(CXX_OBJS) $(S_OBJS)
DEPS     := $(ALL_OBJS:.o=.d)

ifdef TFLM_PREBUILT_CORE_LIB
TFLM_CORE_LIB := $(TFLM_PREBUILT_CORE_LIB)
else
TFLM_CORE_LIB := $(BUILD_DIR)/tflm-core.a
CORE_OBJS     := $(foreach s,$(CORE_C_SRCS) $(CORE_CXX_SRCS),$(call _src_to_obj,$(s)))
DEPS          += $(CORE_OBJS:.o=.d)
endif

ELF := $(BUILD_DIR)/$(APPNAME).elf
HEX := $(BUILD_DIR)/$(APPNAME).hex
BIN := $(BUILD_DIR)/$(APPNAME).bin

.PHONY: all clean deploy size tflm-core build-tflm-core

all: $(HEX) $(BIN) size

tflm-core: $(TFLM_CORE_LIB)

$(BUILD_DIR)/obj:
	$(if $(filter $(OS),Windows_NT),if not exist "$(subst /,\\,$@)" mkdir "$(subst /,\\,$@)",mkdir -p $@)

define C_RULE
$(call _src_to_obj,$(1)): $(1) | $(BUILD_DIR)/obj
	@echo   CC  $(notdir $(1))
	@$(CC) $(CFLAGS) $(DEFINES) $(INCLUDES) -MF $$(@:.o=.d) -MT $$@ -o $$@ $$<
endef
$(foreach s,$(ALL_C_SRCS),$(eval $(call C_RULE,$(s))))

define CXX_RULE
$(call _src_to_obj,$(1)): $(1) | $(BUILD_DIR)/obj
	@echo   CXX $(notdir $(1))
	@$(CXX) $(CXXFLAGS) $(DEFINES) $(INCLUDES) -MF $$(@:.o=.d) -MT $$@ -o $$@ $$<
endef
$(foreach s,$(ALL_CXX_SRCS),$(eval $(call CXX_RULE,$(s))))

define S_RULE
$(call _src_to_obj,$(1)): $(1) | $(BUILD_DIR)/obj
	@echo   AS  $(notdir $(1))
	@$(AS) $(ASFLAGS) $(INCLUDES) -MF $$(@:.o=.d) -MT $$@ -o $$@ $$<
endef
$(foreach s,$(ALL_S_SRCS),$(eval $(call S_RULE,$(s))))

ifndef TFLM_PREBUILT_CORE_LIB
$(foreach s,$(CORE_C_SRCS),$(eval $(call C_RULE,$(s))))
$(foreach s,$(CORE_CXX_SRCS),$(eval $(call CXX_RULE,$(s))))

$(TFLM_CORE_LIB): $(CORE_OBJS) | $(BUILD_DIR)/obj
	@echo   AR  $(notdir $@)
	@rm -f $@
	@$(AR) rcs $@ $(CORE_OBJS)
endif

OBJS_RSP := $(BUILD_DIR)/objects.rsp
$(ELF): $(ALL_OBJS) $(TFLM_CORE_LIB) $(LDLIBS) | $(BUILD_DIR)/obj
	@echo   LD  $@
	$(file >$(OBJS_RSP),$(ALL_OBJS))
	@$(LD) $(LDFLAGS) -o $@ -Wl,--start-group @$(OBJS_RSP) $(TFLM_CORE_LIB) $(LDLIBS) -Wl,--end-group $(LD_EXTRA_LIBS)

$(HEX): $(ELF)
	@echo   HEX $@
	@$(OBJCOPY) -O ihex $< $@

$(BIN): $(ELF)
	@echo   BIN $@
	@$(OBJCOPY) -O binary $< $@

size: $(ELF)
	@$(SIZE) $<

clean:
	$(RM_RF) $(BUILD_DIR)

# deps/tflm/prebuilt/tflm-core-GCC_ARM.a is a committed, prebuilt archive
# (auto-detected by deps/tflm/tflm_deps.mk), so local builds skip
# recompiling ~300 TFLM/CMSIS-NN sources by default. Regenerate and commit
# it via this target whenever deps/tflm/build-tflm-core.sh's pinned TFLM_REF
# is bumped. Requires arm-none-eabi-gcc (and git/python3/patch/unzip/wget)
# on PATH. MAKEFLAGS/MFLAGS/MAKELEVEL must be stripped: the script's
# create_tflm_tree.py runs its own nested `make` internally, and since
# we're already inside a make recipe (MAKELEVEL>0), GNU Make auto-enables
# --print-directory for that nested make, corrupting the file lists the
# script parses from its stdout with "Entering/Leaving directory" banners.
TFLM_PREBUILT_DIR := $(REPO_ROOT)/deps/tflm/prebuilt

build-tflm-core:
	env -u MAKEFLAGS -u MFLAGS -u MAKELEVEL bash $(REPO_ROOT)/deps/tflm/build-tflm-core.sh $(TFLM_PREBUILT_DIR) tflm-core-GCC_ARM.a

BUILD ?= $(BUILD_DIR)
deploy: $(ELF)
	$(call flash_target,$<)

-include $(DEPS)
