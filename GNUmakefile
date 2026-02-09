# SPDX-License-Identifier: MPL-2.0
#
# Do NOT modify or remove this copyright and license
#
# Copyright (c) 2012-2025 Seagate Technology LLC and/or its Affiliates, All Rights Reserved
#
# This software is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
#
# ******************************************************************************************
#
# \file GNUmakefile
# \brief Standalone GNU makefile for opensea-operations library
#
# This makefile can build opensea-operations independently of the main project.
# It requires opensea-common and opensea-transport to be built first.
# Requires GNU Make - not compatible with BSD make or other make implementations.

# Directories
OPERATIONS_DIR := $(CURDIR)
COMMON_DIR ?= $(OPERATIONS_DIR)/../opensea-common
TRANSPORT_DIR ?= $(OPERATIONS_DIR)/../opensea-transport
SRC_DIR := $(OPERATIONS_DIR)/src
INCLUDE_DIR := $(OPERATIONS_DIR)/include

# Include main build system modules (reuse existing infrastructure)
MAKE_DIR := $(OPERATIONS_DIR)/../../Make
include $(MAKE_DIR)/config.mk

# Override BUILD_DIR for standalone builds (use local build directory)
BUILD_DIR := $(OPERATIONS_DIR)/build
OBJ_DIR := $(BUILD_DIR)/obj
LIB_DIR := $(BUILD_DIR)/lib
include $(MAKE_DIR)/compiler-detection.mk
include $(MAKE_DIR)/compiler-flags.mk
include $(MAKE_DIR)/security-hardening.mk
include $(MAKE_DIR)/platforms/$(PLATFORM).mk

# Include source list from this subproject
include $(OPERATIONS_DIR)/sources.mk

# Set default goal (must be after all includes)
.DEFAULT_GOAL := all

# Compiler flags
CFLAGS := $(OPERATIONS_CFLAGS) $(WARNING_FLAGS) $(SECURITY_CFLAGS) \
          -I$(INCLUDE_DIR) -I$(TRANSPORT_DIR)/include -I$(COMMON_DIR)/include \
          $(PLATFORM_DEFINES)

# Object files
OBJS := $(addprefix $(OBJ_DIR)/,$(OPERATIONS_SOURCES:.c=.o))
DEPS := $(OBJS:.o=.d)

# Library target
LIBOPERATIONS_STATIC := $(LIB_DIR)/libopensea-operations.a

# vpath for source discovery
vpath %.c $(SRC_DIR)

#===============================================================================
# Targets
#===============================================================================

.PHONY: all clean distclean help

all: $(LIBOPERATIONS_STATIC)

$(LIBOPERATIONS_STATIC): $(OBJS) | $(LIB_DIR)
	@echo "  AR      $@"
	@$(AR) rcs $@ $^

$(OBJ_DIR)/%.o: %.c | $(OBJ_DIR)
	@echo "  CC      $<"
	@$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

$(OBJ_DIR) $(LIB_DIR):
	@mkdir -p $@

clean:
	@echo "Cleaning opensea-operations build artifacts..."
	@rm -rf $(BUILD_DIR)

distclean: clean
	@echo "Distclean complete for opensea-operations"

help:
	@echo "opensea-operations standalone build targets:"
	@echo "  all       - Build libopensea-operations.a (default)"
	@echo "  clean     - Remove build artifacts"
	@echo "  distclean - Remove all generated files"
	@echo "  help      - Show this help message"
	@echo ""
	@echo "Variables:"
	@echo "  BUILD_DIR     - Build output directory (default: ./build)"
	@echo "  COMMON_DIR    - Path to opensea-common (default: ../opensea-common)"
	@echo "  TRANSPORT_DIR - Path to opensea-transport (default: ../opensea-transport)"
	@echo "  CC            - C compiler (default: auto-detected)"
	@echo "  PLATFORM      - Target platform (default: auto-detected)"
	@echo ""
	@echo "Note: opensea-common and opensea-transport must be built first"

# Include dependency files
-include $(DEPS)
