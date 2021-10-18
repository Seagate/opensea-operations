//
// Do NOT modify or remove this copyright and license
//
// Copyright (c) 2012-2021 Seagate Technology LLC and/or its Affiliates, All Rights Reserved
//
// This software is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
//
// ******************************************************************************************
// 
// \file writesame.h
// \brief This file defines the functions related to the writesame command on a drive

#pragma once

#include "operations_Common.h"

#if defined (__cplusplus)
extern "C"
{
#endif

    //-----------------------------------------------------------------------------
    //
    // is_Write_Same_Supported
    //
    //! \brief   This function checks if the device supports write same. On SCSI, it returns the max number of logical blocks per command, which is reported in an inquiry page. On ATA, this will return MaxLBA - startLBA and whether SCT write same is supported or not
    //
    //  Entry:
    //!   \param[in] device = file descriptor
    //!   \param[in] startingLBA = The LBA that you want to start write same at
    //!   \param[in] requesedNumberOfLogicalBlocks = the number of logical blocks you want to erase starting at startLBA (also known as the range)
    //!   \param[out] maxNumberOfLogicalBlocksPerCommand = this is the range the device supports in a single write same command (0 means that there is no limit)
    //!
    //  Exit:
    //!   \return SUCCESS = good, !SUCCESS something went wrong see error codes
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API bool is_Write_Same_Supported(tDevice *device, uint64_t startingLBA, uint64_t requesedNumberOfLogicalBlocks, uint64_t *maxNumberOfLogicalBlocksPerCommand);

    //-----------------------------------------------------------------------------
    //
    // get_Writesame_Progress
    //
    //! \brief   This function will get the write same progress for you. This only works on ATA drives (and it is calculated progress, not drive reported) since SCSI does not report and progress on write same
    //
    //  Entry:
    //!   \param[in] device = file descriptor
    //!   \param[out] progress = pointer to a double that will hold the calculated progress percentage
    //!   \param[out] writeSameInProgress = pointer to a bool telling whether a write same is in progress or not
    //!   \param[in] startingLBA = This is the LBA that the write same was started at (used to calculate progress)
    //!   \param[in] range = this is the range that the write same is being run on (used to calculate progress)
    //!
    //  Exit:
    //!   \return SUCCESS = good, !SUCCESS something went wrong see error codes
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API int get_Writesame_Progress(tDevice *device, double *progress, bool *writeSameInProgress, uint64_t startingLBA, uint64_t range);

    //-----------------------------------------------------------------------------
    //
    // writesame
    //
    //! \brief   This function will get start a write same, and on ATA drives, it can also poll for progress
    //
    //  Entry:
    //!   \param[in] device = file descriptor
    //!   \param[in] startingLba = This is the LBA that the write same will be started at
    //!   \param[in] numberOfLogicalBlocks = this is the range that the write same is being run on
    //!   \param[in] pollForProgress = boolean flag specifying whether or not to poll for progress
    //!   \param[in] pattern = pointer to buffer to use for pattern. Should be 1 logical sector in size. May be NULL to use default zero pattern
    //!   \param[in] patternLength = lenght of the pattern memory
    //!
    //  Exit:
    //!   \return SUCCESS = good, !SUCCESS something went wrong see error codes
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API int writesame(tDevice *device, uint64_t startingLba, uint64_t numberOfLogicalBlocks, bool pollForProgress, uint8_t *pattern, uint32_t patternLength);

    //-----------------------------------------------------------------------------
    //
    // show_Write_Same_Current_LBA
    //
    //! \brief   This function will show the current LBA being processed by write same (cannot calculate percentage without start and range)
    //
    //  Entry:
    //!   \param[in] device = file descriptor
    //!
    //  Exit:
    //!   \return SUCCESS = good, !SUCCESS something went wrong see error codes
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API int show_Write_Same_Current_LBA(tDevice *device);


#if defined (__cplusplus)
}
#endif
