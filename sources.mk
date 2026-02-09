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
# \file sources.mk
# \brief opensea-operations source file list
#
# This file defines all source files needed to build libopensea-operations.
# No platform-specific sources currently needed.

#===============================================================================
# Operations Sources (all platforms)
#===============================================================================

OPERATIONS_SOURCES := \
    ata_Security.c \
    dst.c \
    firmware_download.c \
    host_erase.c \
    logs.c \
    farm_log.c \
    operations.c \
    power_control.c \
    sanitize.c \
    seagate_operations.c \
    set_max_lba.c \
    smart.c \
    writesame.c \
    generic_tests.c \
    sector_repair.c \
    trim_unmap.c \
    drive_info.c \
    format.c \
    device_statistics.c \
    cdl.c \
    sas_phy.c \
    depopulate.c \
    zoned_operations.c \
    buffer_test.c \
    defect.c \
    nvme_operations.c \
    reservations.c \
    partition_info.c \
    ata_device_config_overlay.c \
    sata_phy.c
