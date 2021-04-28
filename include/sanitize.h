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
// \file sanitize.h
// \brief This file defines the functions for sanitize operations on SCSI and ATA drives

#pragma once

#include "operations_Common.h"

#if defined (__cplusplus)
extern "C"
{
#endif
    
    // \struct typedef struct _sanitizeFeaturesSupported
    typedef struct _sanitizeFeaturesSupported
    {
        bool sanitizeCmdEnabled;
        bool blockErase;
        bool overwrite;
        bool crypto;
        bool exitFailMode;
    } sanitizeFeaturesSupported;

    //-----------------------------------------------------------------------------
    //
    //  get_SCSI_Sanitize_Supported_Features()
    //
    //! \brief   Description:  Gets the SCSI Sanitize Device Feature set support
    //
    //  Entry:
    //!   \param[in] device - pointer to the device structure
    //!   \param[out] sanitizeOptions - pointer to the sanitizeFeaturesSupported structure to populate some information
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API int get_SCSI_Sanitize_Supported_Features(tDevice *device, sanitizeFeaturesSupported *sanitizeOptions);

    //-----------------------------------------------------------------------------
    //
    //  get_ATA_Sanitize_Device_Features()
    //
    //! \brief   Description:  Function to get the SANITIZE Device Features from an ATA drive
    //
    //  Entry:
    //!   \param[in] device = file descriptor
    //!   \param[out] sanitizeOptions = pointer to struct that will have which operations are supported set
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API int get_ATA_Sanitize_Device_Features(tDevice *device, sanitizeFeaturesSupported *sanitizeOptions);

    //-----------------------------------------------------------------------------
    //
    //  get_Sanitize_Device_Features()
    //
    //! \brief   Description:  Function to find out which of the sanitize feature options are supported, if any
    //
    //  Entry:
    //!   \param[in] device = file descriptor
    //!   \param[in] opts = sanitize options for the device
    //!
    //  Exit:
    //!   \return SUCCESS on successful completion, !SUCCESS if problems encountered
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API int get_Sanitize_Device_Features(tDevice *device, sanitizeFeaturesSupported *opts);

    typedef enum _eSanitizeStatus
    {
        SANITIZE_STATUS_SUCCESS = 0,//device reports that the last sanitize completed without error.
        SANITIZE_STATUS_NOT_IN_PROGRESS,//SCSI/SAS - May be the same thing as success, which is why they share the same value.
        SANITIZE_STATUS_IN_PROGRESS,
        SANITIZE_STATUS_NEVER_SANITIZED,//Only useful on a fresh drive that's never been sanitized. Some amount of support to detect this on ATA is also present.
        SANITIZE_STATUS_FAILED,//generic failure
        SANITIZE_STATUS_FAILED_PHYSICAL_SECTORS_REMAIN,//ATA - Completed with physical sectors that are available to be allocated for user data that were not successfully sanitized
        SANITIZE_STATUS_UNSUPPORTED_FEATURE,//ATA - the specified sanitize value in the feature register is not supported
        SANITIZE_STATUS_FROZEN,//ATA specific. In sanitize frozen state
        SANITIZE_STATUS_FREEZELOCK_FAILED_DUE_TO_ANTI_FREEZE_LOCK,
        SANITIZE_STATUS_UNKNOWN, //Will likely be considered a failure.
    }eSanitizeStatus;

    //-----------------------------------------------------------------------------
    //
    //  get_Sanitize_Progress()
    //
    //! \brief   Description:  Function to get the progress of an active Sanitize test
    //
    //  Entry:
    //!   \param[in] device = file descriptor
    //!   \param[out] percentComplete = pointer to a double type that will hold the test progress
    //!   \param[out] sanitizeStatus = pointer to a enum that holds the current sanitize status
    //!
    //  Exit:
    //!   \return SUCCESS = pass, FAILURE = fail
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API int get_Sanitize_Progress(tDevice *device, double *percentComplete, eSanitizeStatus *sanitizeStatus);

    //-----------------------------------------------------------------------------
    //
    //  show_Sanitize_Progress()
    //
    //! \brief   Description:  This function calls get_Sanitize_Progress and prints out the progress based on the result
    //
    //  Entry:
    //!   \param[in] device = file descriptor
    //!
    //  Exit:
    //!   \return SUCCESS = pass, FAILURE = fail
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API int show_Sanitize_Progress(tDevice *device);

    typedef enum _eSanitizeOperations{
        SANITIZE_BLOCK_ERASE,
        SANITIZE_CRYPTO_ERASE,
        SANITIZE_OVERWRITE_ERASE,
        SANTIZIE_FREEZE_LOCK,
        SANITIZE_ANTI_FREEZE_LOCK,
        SANITIZE_EXIT_FAILURE_MODE,
    }eSanitizeOperations;

    //-----------------------------------------------------------------------------
    //
    //  run_Sanitize_Operation()
    //
    //! \brief   Description: This function will start, and optionally poll for progress for the duration of a sanitize operation
    //
    //  Entry:
    //!   \param[in] device = pointer to device structure containing open device handle
    //!   \param[in] sanitizeOperation = enum type specifying which sanitize operation to perform
    //!   \param[in] pollForProgress = set to true to poll for progress until complete. This will output percent complete to stdout. For GUI's, it's recommended that this is set to false and progress is polled elsewhere.
    //!   \param[in] pattern = pointer to a data pattern to use in an overwrite. NOTE This param is only used for Sanitize overwrite. It will be ignored otherwise.
    //!   \param[in] patternLength = length of the pattern pointed to by pattern argument. Must be at least 4 for ATA (first 4 bytes are used). Pattern cannot be larger than 1 logical sector for SCSI
    //!
    //  Exit:
    //!   \return SUCCESS = pass, FAILURE = fail
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API int run_Sanitize_Operation(tDevice *device, eSanitizeOperations sanitizeOperation, bool pollForProgress, uint8_t *pattern, uint32_t patternLength);

#if defined (__cplusplus)
}
#endif
