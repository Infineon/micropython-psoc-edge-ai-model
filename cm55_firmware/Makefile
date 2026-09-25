################################################################################
# cm55_firmware/Makefile — project entry point
#
# Selects a framework implementation and delegates the build to it.
# All hardware/BSP/toolchain config lives in board.mk.
#
# Usage:
#   make [CONFIG=Debug|Release] [DEEPCRAFT_PROJECT=name]
#   make clean
#   make deploy
################################################################################

APPNAME          ?= cm55_firmware
CONFIG           ?= Debug
BOARD            ?= KIT_PSE84_AI
FRAMEWORK        ?= deepcraft

APP_DIR       := $(abspath .)
BOARD_DIR     := $(APP_DIR)
BUILD_DIR     := $(BOARD_DIR)/build/$(CONFIG)
FRAMEWORK_DIR := $(APP_DIR)/framework/$(FRAMEWORK)

# Frameworks normally use framework/<name>/Makefile; tflm uses
# framework/tflm/tflm.mk instead (see there for why).
FRAMEWORK_MAKEFILE := $(if $(wildcard $(FRAMEWORK_DIR)/$(FRAMEWORK).mk),$(FRAMEWORK).mk,Makefile)

# All hardware/BSP config in one place
include $(BOARD_DIR)/board.mk

.PHONY: all clean deploy flash build framework help submodules

all: framework
build: framework

framework:
	$(if $(wildcard $(FRAMEWORK_DIR)),,$(error Unknown framework: $(FRAMEWORK). Available frameworks: $(notdir $(wildcard $(APP_DIR)/framework/*))))
	$(MAKE) -C $(FRAMEWORK_DIR) -f $(FRAMEWORK_MAKEFILE) \
		APPNAME=$(APPNAME) \
		CONFIG=$(CONFIG) \
		BOARD=$(BOARD) \
		BOARD_DIR=$(BOARD_DIR) \
		BUILD_DIR=$(BUILD_DIR) \
		FRAMEWORK=$(FRAMEWORK)

clean:
	$(if $(wildcard $(FRAMEWORK_DIR)),,$(error Unknown framework: $(FRAMEWORK). Available frameworks: $(notdir $(wildcard $(APP_DIR)/framework/*))))
	$(MAKE) -C $(FRAMEWORK_DIR) -f $(FRAMEWORK_MAKEFILE) clean \
		APPNAME=$(APPNAME) \
		CONFIG=$(CONFIG) \
		BOARD=$(BOARD) \
		BOARD_DIR=$(BOARD_DIR) \
		BUILD_DIR=$(BUILD_DIR) \
		FRAMEWORK=$(FRAMEWORK)

flash deploy:
	$(if $(wildcard $(FRAMEWORK_DIR)),,$(error Unknown framework: $(FRAMEWORK). Available frameworks: $(notdir $(wildcard $(APP_DIR)/framework/*))))
	$(MAKE) -C $(FRAMEWORK_DIR) -f $(FRAMEWORK_MAKEFILE) deploy \
		APPNAME=$(APPNAME) \
		CONFIG=$(CONFIG) \
		BOARD=$(BOARD) \
		BOARD_DIR=$(BOARD_DIR) \
		BUILD_DIR=$(BUILD_DIR) \
		FRAMEWORK=$(FRAMEWORK)

submodules:
	git -C $(REPO_ROOT) submodule update --init --jobs 8 $(REPO_ROOT)/deps/psoc-edge

help:
	@echo "Usage:"
	@echo "  make [CONFIG=Debug|Release] [DEEPCRAFT_PROJECT=name]"
	@echo "  make clean"
	@echo "  make deploy"
	@echo "Available frameworks:"
	@find $(APP_DIR)/framework -mindepth 1 -maxdepth 1 -type d -printf '  %f\n' | sort
