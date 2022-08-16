//
// Do NOT modify or remove this copyright and license
//
// Copyright (c) 2012-2022 Seagate Technology LLC and/or its Affiliates, All Rights Reserved
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

    //-----------------------------------------------------------------------------
    //
    //  pull_FARM_Combined_Log(tDevice *device, const char * const filePath);
    //
    //! \brief   Description: This function pulls the Seagate Combined FARM log. This Log is a combination of all
    //!						  FARM Log Subpages.
    //
    //  Entry:
    //!   \param[in] device = poiner to a valid device structure with a device handle
    //!   \param[in] filePath = pointer to the path where this log should be generated. Use NULL for current working dir.
    //!   \param[in] transferSizeBytes = OPTIONAL. If set to zero, this is ignored. 
    //!   \param[in] delayTime = delay time between commands to control performance.
    //  Exit:
    //!   \return SUCCESS = everything worked, !SUCCESS means something went wrong
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API int pull_FARM_Combined_Log(tDevice *device, const char * const filePath, uint32_t transferSizeBytes, uint32_t delayTime);

#if defined (__cplusplus)
}
#endif