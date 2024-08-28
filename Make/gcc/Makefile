# SPDX-License-Identifier: MPL-2.0
#
# Do NOT modify or remove this copyright and license
#
# Copyright (c) 2012-2024 Seagate Technology LLC and/or its Affiliates, All Rights Reserved
#
# This software is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
#
# ******************************************************************************************

# Hand Written Makefile (Edit with caution) -Muhammad
# 

NAME=opensea-operations
FILE_OUTPUT_DIR=lib
#Change the Major version when major interface changes are made. E.g. tDevice changes
MAJOR=8
#Change the Minor version when new features are added. 
MINOR=0
#Change the patch version when only bug fixes are made.
PATCH=1
VERSION=$(MAJOR).$(MINOR).$(PATCH)
SRC_DIR=../../src/
INC_DIR=-I../../include -I../../../opensea-transport/include -I../../../opensea-transport/include/vendor -I../../../opensea-common/include
CC ?= gcc
AR ?= ar
#override CFLAGS = -Wall -c -fPIC -I.
CFLAGS ?= -Wall -Wextra 
CFLAGS += -c -fPIC -I.

#NOTE -Wsign-conversion can be useful but warns way too much by default. Only enable it if debugging specific problems
COMPILER_VERSION := $(shell $(CC) --version)
ifneq (,$(findstring clang,$(COMPILER_VERSION)))
	#setup clang specific warnings/flags (centos 7's old version supports -Wno-unknown-warning-option so enabling all of them)
	CFLAGS += -Wno-unknown-warning-option -Wcast-align=strict -Wvla -Wfloat-equal -Wnull-dereference -Wunused-const-variable \
	-Wduplicated-cond -Wjump-misses-init -Wstringop-overflow -Wlogical-op -Wshift-overflow=2 -Wdouble-promotion -Wformat-security \
	-Wold-style-definition -Wstrict-prototypes -Wmissing-declarations -Wmissing-prototypes
else 
	ifneq (,$(findstring GCC,$(COMPILER_VERSION)))
		#setup gcc specific warnings/flags
		GCC_VERSION_STRING = $(shell $(CC) -dumpversion)
		GCC_VER = $(subst ., ,$(GCC_VERSION_STRING))
		GCC_MAJOR = $(word 1,$(GCC_VER))
		GCC_MINOR = $(word 2,$(GCC_VER))
		GCC_SUBMINOR = $(word 3,$(GCC_VER))
		ifeq ($(GCC_MINOR),)
			GCC_MINOR = 0
		endif 
		ifeq ($(GCC_SUBMINOR),)
			GCC_SUBMINOR = 0
		endif
		#version 8.5 and higher
		ifeq ($(shell test $(GCC_MAJOR) -gt 7; echo $$?),0)
			ifeq ($(shell test $(GCC_MINOR) -gt 4; echo $$?),0)
				CFLAGS += -Wcast-align=strict
			else 
				CFLAGS += -Wcast-align
			endif
		else
			CFLAGS += -Wcast-align
		endif
		#version 7.5 and higher
		ifeq ($(shell test $(GCC_MAJOR) -gt 6; echo $$?),0)
			ifeq ($(shell test $(GCC_MINOR) -gt 4; echo $$?),0)
				CFLAGS += -Wshadow=compatible-local -Wstringop-overflow
			else 
				CFLAGS +=
			endif
		else
			CFLAGS +=
		endif
		#version 6.5 and higher
		ifeq ($(shell test $(GCC_MAJOR) -gt 5; echo $$?),0)
			ifeq ($(shell test $(GCC_MINOR) -gt 4; echo $$?),0)
				CFLAGS += -Wnull-dereference -Wunused-const-variable -Wduplicated-cond -Wshift-overflow=2
			else 
				CFLAGS +=
			endif
		else
			CFLAGS +=
		endif
		#version 5.5 and higher
		ifeq ($(shell test $(GCC_MAJOR) -gt 4; echo $$?),0)
			ifeq ($(shell test $(GCC_MINOR) -gt 4; echo $$?),0)
				CFLAGS += -Wlogical-not-parentheses
			endif
		else
			#GCC less that v 5.x.x needs to set gnu99 standard
			#as of 5.x.x, gnu11 is default
			CFLAGS += -std=gnu99
		endif
		
		CFLAGS += -Wvla -Wfloat-equal -Wjump-misses-init -Wlogical-op -Wdouble-promotion -Wformat-security \
			-Wold-style-definition -Wstrict-prototypes -Wmissing-declarations -Wmissing-prototypes
	else
		CFLAGS += -std=gnu99
		CFLAGS += -Wvla -Wfloat-equal -Wjump-misses-init -Wlogical-op -Wdouble-promotion -Wformat-security \
				-Wold-style-definition -Wstrict-prototypes -Wmissing-declarations -Wmissing-prototypes
	endif
endif
UNAME := $(shell uname -s)
#removed this linker option for now: -Wl,-Map,output.map
#This was incompatible with AIX's linker and seems more related to debugging a memory problem.
LFLAGS ?= -Wall ../../../opensea-common/Make/gcc/$(FILE_OUTPUT_DIR)/libopensea-common.a ../../../opensea-transport/Make/gcc/$(FILE_OUTPUT_DIR)/libopensea-transport.a -lm
#AIX wants all linker libraries for the .so. Need libodm and libcfg in addition to the above to resolve all symbols
ifeq ($(UNAME),AIX)
	LFLAGS += -lodm -lcfg
endif
LIB_SRC_FILES = \
	$(SRC_DIR)ata_Security.c \
	$(SRC_DIR)dst.c \
	$(SRC_DIR)firmware_download.c \
	$(SRC_DIR)host_erase.c \
	$(SRC_DIR)logs.c \
	$(SRC_DIR)farm_log.c \
	$(SRC_DIR)operations.c \
	$(SRC_DIR)power_control.c \
	$(SRC_DIR)sanitize.c \
	$(SRC_DIR)seagate_operations.c \
	$(SRC_DIR)set_max_lba.c \
	$(SRC_DIR)smart.c \
	$(SRC_DIR)writesame.c \
	$(SRC_DIR)generic_tests.c \
	$(SRC_DIR)sector_repair.c \
	$(SRC_DIR)trim_unmap.c\
	$(SRC_DIR)drive_info.c\
	$(SRC_DIR)format.c\
	$(SRC_DIR)device_statistics.c\
	$(SRC_DIR)sas_phy.c\
	$(SRC_DIR)depopulate.c\
	$(SRC_DIR)zoned_operations.c\
	$(SRC_DIR)buffer_test.c\
	$(SRC_DIR)defect.c\
	$(SRC_DIR)nvme_operations.c\
	$(SRC_DIR)reservations.c\
	$(SRC_DIR)partition_info.c\
	$(SRC_DIR)ata_device_config_overlay.c\
	$(SRC_DIR)sata_phy.c

UNAME := $(shell uname)

PROJECT_DEFINES += #-D_DEBUG

#All of the source files have associated object files
LIB_OBJ_FILES = $(LIB_SRC_FILES:.c=.o)
LIBS = lib$(NAME).a

#DEPFILES = $(LIB_SRC_FILES:.c=.d)

#-include $(DEPFILES)

.PHONY: all 

all: clean mkoutputdir $(LIBS)

opensea-libs:
	$(MAKE) -C ../../../opensea-common/Make/gcc
	$(MAKE) -C ../../../opensea-transport/Make/gcc

%.o: %.c
	$(CC) $(CFLAGS) $(PROJECT_DEFINES) $(INC_DIR) $< -o $@

$(LIBS): $(LIB_OBJ_FILES) opensea-libs
	rm -f $(FILE_OUTPUT_DIR)/$@
	$(AR) cq $(FILE_OUTPUT_DIR)/$@ $(LIB_OBJ_FILES)
	$(CC) -shared $(LIB_OBJ_FILES) $(LFLAGS) -o $(FILE_OUTPUT_DIR)/lib$(NAME).so.$(VERSION)
	cd $(FILE_OUTPUT_DIR) && ln -sf lib$(NAME).so.$(VERSION) lib$(NAME).so

clean:
	rm -f $(FILE_OUTPUT_DIR)/lib$(NAME).a $(FILE_OUTPUT_DIR)/lib$(NAME).so.$(VERSION) lib$(NAME).so *.o ../../src/*.o
	rm -rf $(FILE_OUTPUT_DIR)

mkoutputdir:
	mkdir -p $(FILE_OUTPUT_DIR)

