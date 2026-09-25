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

################################################################################
# TFLM dependency wiring
#
# ML runtime built from source out of the upstream tensorflow/tflite-micro
# submodule (deps/tflm/tflite-micro), pruned into a standalone tree with
# tflite-micro's own project-generation tool (see regen-tflm-tree below).
# tflm_tree/ is regenerated (not committed -- see .gitignore) since it's
# fully reproducible from the pinned submodule commit; run
# `make -f tflm.mk regen-tflm-tree` once after cloning.
# deps/psoc-edge/ml-tflite-micro (Infineon's prebuilt libtensorflow-microlite.a)
# and deps/psoc-edge/ml-middleware are NOT used by this framework.
################################################################################

TFLM_TREE := $(REPO_ROOT)/deps/tflm/tflm_tree

# Board/BSP-config sources (cycfg*.c, cy_afe_configurator_settings.c) and
# generic SDK libraries (RTOS, HAL, PDL, ...) come straight from board.mk's
# BSP_C_SRCS/LIB_C_SRCS/LIB_CXX_SRCS -- DeepCraft-only libraries (audio,
# voice, Bluetooth, ML-middleware) live in framework/deepcraft/Makefile
# instead, so no filtering is needed here.

# Upstream tflite-micro runtime (core interpreter, kernels, third-party deps).
# Kept separate from LIB_C/CXX_SRCS -- compiled into its own tflm-core.a
# archive instead of linked as loose objects, so the committed prebuilt
# archive (deps/tflm/prebuilt/) can be substituted via TFLM_PREBUILT_CORE_LIB
# to skip recompiling ~300 files.
CORE_C_SRCS   := $(shell find $(TFLM_TREE)/third_party -name '*.c' 2>/dev/null)
CORE_CXX_SRCS := $(shell find $(TFLM_TREE)/tensorflow $(TFLM_TREE)/signal -name '*.cc' 2>/dev/null)

# board.mk's default LDLIBS is already empty -- nothing to override here.

# deps/tflm/prebuilt/tflm-core.a is a committed, prebuilt archive (see
# deps/tflm/build-tflm-core.sh) -- use it automatically instead of
# rebuilding ~300 tflm_tree files on every clean build. Override entirely
# with TFLM_PREBUILT_CORE_LIB=/other/path, or unset it to force a from-source
# build regardless of what's committed here. Always GCC_ARM (the only
# toolchain this framework supports), so no toolchain suffix needed.
TFLM_PREBUILT_CORE_LIB_DEFAULT := $(REPO_ROOT)/deps/tflm/prebuilt/tflm-core.a
ifneq ($(wildcard $(TFLM_PREBUILT_CORE_LIB_DEFAULT)),)
    TFLM_PREBUILT_CORE_LIB ?= $(TFLM_PREBUILT_CORE_LIB_DEFAULT)
endif

# Tell the reference kernel headers to defer INT8/INT4/INT16 registration to
# the cmsis_nn/*.cc optimized implementations instead of their own inline
# fallbacks (see e.g. tensorflow/lite/micro/kernels/conv.h).
DEFINES += -DCMSIS_NN

# tensorflow/lite/micro/cortex_m_generic/micro_time.cc has a dedicated stub
# mode for exactly this integration scenario (no DWT/CMSIS-device coupling
# needed) -- ticks_per_second()/GetCurrentTimeTicks() just return 0, which is
# fine since this demo doesn't rely on TFLM's built-in cycle-count profiling.
DEFINES += -DPROJECT_GENERATION

# LIB_TFLM (Infineon's prebuilt ml-tflite-micro) ships its own copies of
# tensorflow/, flatbuffers/, gemmlowp/, signal/ headers under the same
# relative paths as our TFLM_TREE -- if left on the search path, the
# compiler could silently pick Infineon's (different-version) headers ahead
# of the ones matching our pinned tflite-micro commit. The DeepCraft
# adapter/AFE/AVC/SOD/VA/ML-middleware include paths don't share any header
# names with tflm_tree, so they're harmless to leave in and aren't filtered.
INCLUDES := $(filter-out -I$(LIB_TFLM)%, $(INCLUDES)) \
    -I$(TFLM_TREE) \
    -I$(TFLM_TREE)/third_party/flatbuffers/include \
    -I$(TFLM_TREE)/third_party/gemmlowp \
    -I$(TFLM_TREE)/third_party/ruy \
    -I$(TFLM_TREE)/third_party/kissfft \
    -I$(TFLM_TREE)/third_party/cmsis_nn \
    -I$(TFLM_TREE)/third_party/cmsis_nn/Include

FRAMEWORK_DIR := $(BOARD_DIR)/framework/tflm
FRAMEWORK_C_SRCS := $(FRAMEWORK_DIR)/main.c
FRAMEWORK_CXX_SRCS := $(FRAMEWORK_DIR)/tflm_runner.cpp

INCLUDES += \
    -I$(FRAMEWORK_DIR)

ALL_C_SRCS   := $(FRAMEWORK_C_SRCS) $(BSP_C_SRCS) $(LIB_C_SRCS)
ALL_CXX_SRCS := $(FRAMEWORK_CXX_SRCS) $(LIB_CXX_SRCS)
ALL_S_SRCS   := $(LIB_S_SRCS)

# CORE_C_SRCS/CORE_CXX_SRCS (tflm_tree third_party/tensorflow/signal) are
# compiled into their own tflm-core.a instead of linked as loose objects.
# Pass TFLM_PREBUILT_CORE_LIB=/path/to/tflm-core.a (defaults to the
# committed deps/tflm/prebuilt/ archive) to use a prebuilt archive and
# skip compiling these sources entirely.
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

.PHONY: all build clean deploy tflm-core help \
        from-source from-prebuilt \
        submodule-init regen-tflm-tree require-tflm-tree require-prebuilt-core gen-tflm-core

all: require-tflm-tree $(HEX) $(BIN)
build: all

help:
	@echo "Usage: make -f tflm.mk [target] [VAR=value ...]"
	@echo ""
	@echo "Everyday targets:"
	@echo "  all, build      Build hex+bin (default). Uses the committed prebuilt"
	@echo "                  tflm-core.a if present, else compiles from tflm_tree/."
	@echo "  from-prebuilt   Same as above but fails loudly if no prebuilt archive"
	@echo "                  is available, instead of silently compiling from source."
	@echo "  from-source     Regenerate tflm_tree/ from the tflite-micro submodule"
	@echo "                  and force a full from-source build (ignores any prebuilt)."
	@echo "  deploy          Build and flash via OpenOCD."
	@echo "  clean           Remove the build directory."
	@echo ""
	@echo "One-time / maintenance targets:"
	@echo "  regen-tflm-tree Regenerate deps/tflm/tflm_tree/ (run once after cloning,"
	@echo "                  or after bumping the tflite-micro submodule)."
	@echo "  gen-tflm-core   Rebuild deps/tflm/prebuilt/tflm-core.a from a pinned"
	@echo "                  upstream ref (own network fetch); commit the result."
	@echo "  tflm-core       Build just the tflm-core.a archive, not the full app."
	@echo ""
	@echo "Useful variables:"
	@echo "  BUILD_DIR=/path       Override the build output directory."
	@echo "  CONFIG=Debug|Release  Build configuration (default: Debug)."
	@echo "  TFLM_PREBUILT_CORE_LIB=/path/to/tflm-core.a   Use a specific archive."
	@echo "  TFLM_PREBUILT_CORE_LIB=  (empty)  Force from-source core compilation."

# tflm_tree/ is gitignored (regenerated, not committed) -- error clearly
# instead of letting header lookups fail deep inside a compiler invocation.
require-tflm-tree:
	@test -d "$(TFLM_TREE)" || { \
		echo "error: $(TFLM_TREE) not found." >&2; \
		echo "Run 'make -f tflm.mk regen-tflm-tree' once after cloning (needs network access)." >&2; \
		exit 1; \
	}

# --- (a) Build from the deps/tflm/tflite-micro submodule -------------------
# Ensures the submodule is checked out at the commit pinned in this repo's
# git index, regenerates deps/tflm/tflm_tree from that local checkout (no
# network fetch beyond the submodule clone), then re-invokes make for a
# fresh from-source build (TFLM_PREBUILT_CORE_LIB= forces the prebuilt
# archive off; a fresh `make` process is required so this file's own
# `$(shell find $(TFLM_TREE) ...)` source lists are recomputed against the
# just-regenerated tree instead of the one seen when this invocation started).
TFLM_SUBMODULE_DIR := $(REPO_ROOT)/deps/tflm/tflite-micro

submodule-init:
	git -C $(REPO_ROOT) submodule update --init $(TFLM_SUBMODULE_DIR)

regen-tflm-tree: submodule-init
	$(RM_RF) $(TFLM_TREE)
	cd $(TFLM_SUBMODULE_DIR) && env -u MAKEFLAGS -u MFLAGS -u MAKELEVEL \
		python3 tensorflow/lite/micro/tools/project_generation/create_tflm_tree.py \
			$(TFLM_TREE) \
			--makefile_options="TARGET=cortex_m_generic TARGET_ARCH=cortex-m55 OPTIMIZED_KERNEL_DIR=cmsis_nn TOOLCHAIN=armclang TENSORFLOW_ROOT="

from-source: regen-tflm-tree
	$(MAKE) -f tflm.mk all \
		BOARD_DIR=$(BOARD_DIR) BUILD_DIR=$(BUILD_DIR) CONFIG=$(CONFIG) \
		TFLM_PREBUILT_CORE_LIB=

# --- (b) Build using the committed prebuilt deps/tflm/prebuilt/tflm-core.a -
# Never compiles tflm_tree/ -- just links FRAMEWORK/APP/LIB objects plus the
# existing archive and produces the hex. Fails loudly if no prebuilt archive
# is available (run `make gen-tflm-core` to create one first).
require-prebuilt-core:
	@test -n "$(TFLM_PREBUILT_CORE_LIB)" -a -f "$(TFLM_CORE_LIB)" || { \
		echo "error: no prebuilt tflm-core.a found (looked for $(TFLM_PREBUILT_CORE_LIB_DEFAULT))." >&2; \
		echo "Run 'make gen-tflm-core' to build and commit one, or pass TFLM_PREBUILT_CORE_LIB=/path/to/tflm-core.a" >&2; \
		exit 1; \
	}

from-prebuilt: require-prebuilt-core all

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

clean:
	$(RM_RF) $(BUILD_DIR)

# --- Maintenance: (re)generate deps/tflm/prebuilt/tflm-core.a --------------
# Standalone (no submodule/tflm_tree dependency; fetches its own pinned ref
# over the network) -- re-run and commit the result whenever
# deps/tflm/build-tflm-core.sh's pinned TFLM_REF is bumped. Requires
# arm-none-eabi-gcc (and git/python3/patch/unzip/wget) on PATH.
TFLM_PREBUILT_DIR := $(REPO_ROOT)/deps/tflm/prebuilt

gen-tflm-core:
	env -u MAKEFLAGS -u MFLAGS -u MAKELEVEL bash $(REPO_ROOT)/deps/tflm/build-tflm-core.sh $(TFLM_PREBUILT_DIR) tflm-core.a

BUILD ?= $(BUILD_DIR)
deploy: $(ELF)
	$(call flash_target,$<)

-include $(DEPS)
