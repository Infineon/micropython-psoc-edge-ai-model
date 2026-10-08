################################################################################
# framework/tests/Makefile -- standalone HIL test build
#
# Builds the test entrypoint without adding any other framework sources.
# Board startup, generated configuration, RTOS, drivers, and board-owned
# transport support are inherited from board.mk.
#
# Usage:
#   make BOARD_DIR=/path/to/cm55_firmware [CONFIG=Debug|Release]
#   make clean
#   make deploy
################################################################################

APPNAME          ?= cm55_test
CONFIG           ?= Debug
BOARD            ?= KIT_PSE84_AI
FRAMEWORK        ?= tests

BOARD_DIR ?= $(abspath ../..)
BUILD_DIR ?= $(BOARD_DIR)/build/$(CONFIG)/tests

include $(BOARD_DIR)/board.mk

TEST_FRAMEWORK_DIR := $(BOARD_DIR)/framework/tests

# Entrypoint plus the IPC transport under test (board.mk no longer carries them).
FRAMEWORK_C_SRCS := \
	$(TEST_FRAMEWORK_DIR)/main.c \
	$(APP_DIR)/sources/transport/ipc.c \
	$(SHARED_DIR)/source/COMPONENT_CM55/cm55_ipc_communication.c

INCLUDES += -I$(TEST_FRAMEWORK_DIR)

ALL_C_SRCS   := $(FRAMEWORK_C_SRCS) $(BSP_C_SRCS) $(LIB_C_SRCS)
ALL_CXX_SRCS := $(LIB_CXX_SRCS)
ALL_S_SRCS   := $(LIB_S_SRCS)

_src_to_obj = $(BUILD_DIR)/obj/$(subst :,_,$(subst /,_,$(subst $(REPO_ROOT)/,,$(abspath $1)))).o

C_OBJS   := $(foreach s,$(ALL_C_SRCS),$(call _src_to_obj,$(s)))
CXX_OBJS := $(foreach s,$(ALL_CXX_SRCS),$(call _src_to_obj,$(s)))
S_OBJS   := $(foreach s,$(ALL_S_SRCS),$(call _src_to_obj,$(s)))

ALL_OBJS := $(C_OBJS) $(CXX_OBJS) $(S_OBJS)
DEPS     := $(ALL_OBJS:.o=.d)

ELF := $(BUILD_DIR)/$(APPNAME).elf
HEX := $(BUILD_DIR)/$(APPNAME).hex
BIN := $(BUILD_DIR)/$(APPNAME).bin

.PHONY: all clean deploy size

all: $(HEX) $(BIN) size

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
	@$(AS) $(ASFLAGS) $(DEFINES) $(INCLUDES) -MF $$(@:.o=.d) -MT $$@ -o $$@ $$<
endef
$(foreach s,$(ALL_S_SRCS),$(eval $(call S_RULE,$(s))))

OBJS_RSP := $(BUILD_DIR)/objects.rsp
$(ELF): $(ALL_OBJS) $(LDLIBS) | $(BUILD_DIR)/obj
	@echo   LD  $@
	$(file >$(OBJS_RSP),$(ALL_OBJS))
	@$(LD) $(LDFLAGS) -o $@ \
	    -Wl,--start-group @$(OBJS_RSP) $(LDLIBS) -Wl,--end-group $(LD_EXTRA_LIBS)

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

BUILD ?= $(BUILD_DIR)
deploy: $(BUILD)/$(APPNAME).elf
	$(call flash_target,$<)

-include $(DEPS)
