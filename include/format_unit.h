//
// Do NOT modify or remove this copyright and license
//
// Copyright (c) 2012 - 2017 Seagate Technology LLC and/or its Affiliates, All Rights Reserved
//
// This software is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
//
// ******************************************************************************************
// 
// \file format_unit.h
// \brief This file defines the function calls for performing some format unit operations

#pragma once

#include "operations_Common.h"

#if defined (__cplusplus)
extern "C"
{
#endif

    //-----------------------------------------------------------------------------
    //
    //  is_Format_Unit_Supported(tDevice *device, bool *fastFormatSupported)
    //
    //! \brief   Description:  Checks if format unit is supported and optionally if fast format is supported. (No guarantee on fast format check accuracy at this time)
    //
    //  Entry:
    //!   \param[in] device = file descriptor
    //!   \param[out] fastFormatSupported = bool that will hold if fast format is supported. May be NULL if you don't care about checking for fast format support.
    //!
    //  Exit:
    //!   \return true = format unit supported, false = format unit not supported.
    //
    //-----------------------------------------------------------------------------
    bool is_Format_Unit_Supported(tDevice *device, bool *fastFormatSupported);

    //-----------------------------------------------------------------------------
    //
    //  get_Format_Progress(tDevice *device, double *percentComplete)
    //
    //! \brief   Description:  Gets the current progress of a format unit operation
    //
    //  Entry:
    //!   \param[in] device = file descriptor
    //!   \param[out] percentComplete = pointer to double to hold the current format unit progress.
    //!
    //  Exit:
    //!   \return SUCCESS = format unit not in progress, IN_PROGRESS = format unit in progress, !SUCCESS = something when wrong trying to get progress
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API int get_Format_Progress(tDevice *device, double *percentComplete);

    typedef enum _eFormatType
    {
        FORMAT_STD_FORMAT = 0,
        FORMAT_FAST_WRITE_NOT_REQUIRED = 1,
        FORMAT_FAST_WRITE_REQUIRED = 2,//not supported on Seagate Drives at this time.
        FORMAT_RESERVED = 3
    }eFormatType;

    typedef enum _eFormatPattern
    {
        FORMAT_PATTERN_DEFAULT = 0,
        FORMAT_PATTERN_REPEAT = 1,
        //other values are reserved or vendor specific
    }eFormatPattern;

    //-----------------------------------------------------------------------------
    //
    //  int run_Format_Unit(tDevice *device, eFormatType formatType, bool currentBlockSize, uint16_t newBlockSize, uint8_t *gList, uint32_t glistSize, bool completeList, bool disablePrimaryList, bool disableCertification, uint8_t *pattern, uint32_t patternLength, bool securityInitialize, bool pollForProgress)
    //
    //! \brief   Description:  runs or starts a format unit operation on a specified device. All formats through this function are started with immed bit set to 1.
    //
    //  Entry:
    //!   \param[in] device = file descriptor
    //!   \param[in] formatType = use this to specify a standard format or fast format
    //!   \param[in] currentBlockSize = use the current logical block size of the device for format (next parameter ignored)
    //!   \param[in] newBlockSize = if currentBlockSize is false, this is the new logical block size to format the drive with.
    //!   \param[in] gList = pointer to the glist to use during format. When NULL, the device will use the current glist unless the completeList bool is set to true.
    //!   \param[in] glistSize = size of the glist pointed to by gList
    //!   \param[in] completeList = set to true to say the provided glist is the complete glist for the device. If this is true and gList is NULL, then this will clear the glist.
    //!   \param[in] disablePrimaryList = set to true to disable using the primary list during a format.
    //!   \param[in] disableCertification = set to true to disable certification
    //!   \param[in] pattern = pointer to a pattern to use during the format. If NULL, the device's default patter is used.
    //!   \param[in] patternLength = length of the data pointed to by pattern
    //!   \param[in] securityInitialize = set to true to set the security initialize bit which requests previously reallocated areas to be overwritten. (Seagate drive's don't currently support this) SBC spec recommends using Sanitize instead of this bit to overwrite previously reallocated sectors.
    //!   \param[in] pollForProgress = set to true for this function to poll for progress until complete. Set to false to use this function to kick off a format unit operation for you.
    //!
    //  Exit:
    //!   \return SUCCESS = format unit successfull or successfully started, !SUCCESS = check error code.
    //
    //-----------------------------------------------------------------------------
    typedef struct _runFormatUnitParameters
    {
        eFormatType formatType;
        bool defaultFormat;//default device format. FOV = 0. If combined with disableImmediat, then no data sent to the device. AKA fmtdata bit is zero. Only defect list format, cmplst, and format type will be used.
        bool currentBlockSize;
        uint16_t newBlockSize;
        uint8_t *gList;
        uint32_t glistSize;
        bool completeList;
        uint8_t defectListFormat;//set to 0 if you don't know or are not sending a list
        bool disablePrimaryList;
        bool disableCertification;
        uint8_t *pattern;
        uint32_t patternLength;
        bool securityInitialize;//Not supported on Seagate products. Recommended to use sanitize instead. This ignores a lot of other fields to perform a secure overwrite of all sectors including reallocated sectors
        bool stopOnListError;//Only used if cmplst is zero and dpry is zero. If the previous condition is met and this is true, the device will stop the format if it cannot access a list, otherwise it will continue processing the command. If unsure, leave false
        bool disableImmediate;//Only set this is you want to wait for the device to completely format itself before returning status! You cannot poll while this is happening. It is recommended that this is left false!
        uint8_t protectionType;//if unsure, use 0. This will set the proper bit combinations for each protection type
        uint8_t protectionIntervalExponent;//Only used on protection types 2 or 3. Ignored and unused otherwise since other types require this to be zero. If unsure, leave as zero
    }runFormatUnitParameters;

    OPENSEA_OPERATIONS_API int run_Format_Unit(tDevice *device, runFormatUnitParameters formatParameters, bool pollForProgress);

    //-----------------------------------------------------------------------------
    //
    //  int show_Format_Unit_Progress(tDevice *device)
    //
    //! \brief   Description:  shows the current progress of a format unit operation if one is in progress.
    //
    //  Entry:
    //!   \param[in] device = file descriptor
    //!
    //  Exit:
    //!   \return SUCCESS = format unit was successful, IN_PROGRESS = format unit in progress, !SUCCESS = check error code.
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API int show_Format_Unit_Progress(tDevice *device);

    typedef struct protectionSupport
    {
        //from inquiry
        bool deviceSupportsProtection;//Must be true for remaining data to be valid. If false, all below this are to be considered false
        //from extended inquiry VPD page
        bool protectionType1Supported;
        bool protectionType2Supported;
        bool protectionType3Supported;
        bool seeSupportedBlockLengthsAndProtectionTypesVPDPage;//This means we should tell the user to use the option to show this page instead. (SBC4)
    }protectionSupport, *ptrProtectionSupport;

    OPENSEA_OPERATIONS_API int get_Supported_Protection_Types(tDevice *device, ptrProtectionSupport protectionSupportInfo);

    OPENSEA_OPERATIONS_API void show_Supported_Protection_Types(ptrProtectionSupport protectionSupportInfo);

    typedef struct _formatStatus
    {
        bool formatParametersAllFs;//This means that the last format failed, or the drive is new, or the data is not available right now
        bool lastFormatParametersValid;
        struct 
        {
            bool isLongList;
            uint8_t protectionFieldUsage;
            bool formatOptionsValid;
            bool disablePrimaryList;
            bool disableCertify;
            bool stopFormat;
            bool initializationPattern;
            bool obsoleteDisableSaveParameters;
            bool immediateResponse;
            bool vendorSpecific;
            uint32_t defectListLength;
            //all options after this are only in the long list
            uint8_t p_i_information;
            uint8_t protectionIntervalExponent;
        }lastFormatData;
        bool grownDefectsDuringCertificationValid;
        uint64_t grownDefectsDuringCertification;//param code 1
        bool totalBlockReassignsDuringFormatValid;
        uint64_t totalBlockReassignsDuringFormat;//param code 2
        bool totalNewBlocksReassignedValid;
        uint64_t totalNewBlocksReassigned;//param code 3
        bool powerOnMinutesSinceFormatValid;
        uint32_t powerOnMinutesSinceFormat;//param code 4
    }formatStatus, *ptrFormatStatus;

    OPENSEA_OPERATIONS_API int get_Format_Status(tDevice *device, ptrFormatStatus formatStatus);

    OPENSEA_OPERATIONS_API void show_Format_Status_Log(ptrFormatStatus formatStatus);

#if defined (__cplusplus)
}
#endif