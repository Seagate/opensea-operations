#
# Do NOT modify or remove this copyright and license
#
# Copyright (c) 2012 - 2019 Seagate Technology LLC and/or its Affiliates, All Rights Reserved
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
MAJOR=1
#Change the Minor version when new features are added. 
MINOR=20
#Change the patch version when only bug fixes are made.
PATCH=20
VERSION=$(MAJOR).$(MINOR).$(PATCH)
SRC_DIR=../../src/
INC_DIR=-I../../include -I../../../opensea-transport/include -I../../../opensea-transport/include/vendor -I../../../opensea-common/include
CC ?= gcc
AR ?= ar
#override CFLAGS = -Wall -c -fPIC -I. -std=gnu99
CFLAGS ?= -Wall 
CFLAGS += -c -fPIC -I. -std=gnu99

LFLAGS ?= -Wall ../../../opensea-common/Make/gcc/$(FILE_OUTPUT_DIR)/libopensea-common.a ../../../opensea-transport/Make/gcc/$(FILE_OUTPUT_DIR)/libopensea-transport.a
LIB_SRC_FILES = \
	$(SRC_DIR)ata_Security.c \
	$(SRC_DIR)dst.c \
	$(SRC_DIR)firmware_download.c \
	$(SRC_DIR)host_erase.c \
	$(SRC_DIR)logs.c \
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
	$(SRC_DIR)nvme_operations.c

#Only define public stuff 
PROJECT_DEFINES += #-DDISABLE_NVME_PASSTHROUGH  #-D_DEBUG

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
	$(CC) -shared $(LIB_OBJ_FILES) -o $(FILE_OUTPUT_DIR)/lib$(NAME).so.$(VERSION)
	cd $(FILE_OUTPUT_DIR) 
	ln -s lib$(NAME).so* lib$(NAME).so

clean:
	rm -f $(FILE_OUTPUT_DIR)/lib$(NAME).a $(FILE_OUTPUT_DIR)/lib$(NAME).so.$(VERSION) lib$(NAME).so *.o ../../src/*.o
	rm -rf $(FILE_OUTPUT_DIR)

mkoutputdir:
	mkdir -p $(FILE_OUTPUT_DIR)

