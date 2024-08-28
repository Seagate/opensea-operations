// SPDX-License-Identifier: MPL-2.0
//
// Do NOT modify or remove this copyright and license
//
// Copyright (c) 2012-2024 Seagate Technology LLC and/or its Affiliates, All Rights Reserved
//
// This software is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
//
// ******************************************************************************************
// 
// \file farm_log.c
// \brief This file defines the functions related to FARM Log

#pragma once
#include "operations_Common.h"

#if defined (__cplusplus)
extern "C"
{
#endif
    typedef enum _eSataFarmCopyType
    {
        SATA_FARM_COPY_TYPE_UNKNOWN,
        SATA_FARM_COPY_TYPE_DISC,
        SATA_FARM_COPY_TYPE_FLASH,
    } eSataFarmCopyType;

    //-----------------------------------------------------------------------------
    //
    //  pull_FARM_Combined_Log(tDevice *device, const char * const filePath);
    //
    //! \brief   Description: This function pulls the Seagate Combined FARM log. This Log is a combination of all
    //!						  FARM Log Subpages.
    //
    //  Entry:
    //!   \param[in] device = poiner to a valid device structure with a device handle
    //!   \param[in] filePath = pointer to the path where this log should be generated. Use M_NULLPTR for current working dir.
    //!   \param[in] transferSizeBytes = OPTIONAL. If set to zero, this is ignored. 
    //  Exit:
    //!   \return SUCCESS = everything worked, !SUCCESS means something went wrong
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API eReturnValues pull_FARM_Combined_Log(tDevice *device, const char * const filePath, uint32_t transferSizeBytes, int sataFarmCopyType);

#if defined (__cplusplus)
}
#endif
