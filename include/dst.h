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
// \file dst.h
// \brief This file defines the function calls for dst and dst related operations

#pragma once

#include "operations_Common.h"
#include "sector_repair.h"

#if defined (__cplusplus)
extern "C"
{
#endif

    //offline may also be called background in some specs
    //captive may also be called foreground in some specs
    typedef enum _eDSTType
    {
        DST_TYPE_SHORT = 1,
        DST_TYPE_LONG = 2,
        DST_TYPE_CONVEYENCE = 3,
    }eDSTType;

    //-----------------------------------------------------------------------------
    //
    //  run_DST()
    //
    //! \brief   Description:  Function to send a ATA Spec DST or SCSI spec DST to a device and poll it for updates. Recommended for utility usage since this will also poll or wait
    //
    //  Entry:
    //!   \param[in] device = file descriptor
    //!   \param[in] DSTType = see enum above
    //!   \param[in] pollForProgress = 0 = don't poll, just start the test. 1 = poll for progress and display the progress on the screen.
    //!   \param[in] captiveForeground = when set to true, the self test is run in captive/foreground mode. This is only for ATA or SCSI. When set, this will wait for the entire test to complete before returning. This is ignored on NVMe
    //!   \param[in] ignoreMaxTime = when this is set to true, the timeout for the maximum time to wait for DST before aborting it will be ignored and will wait indefinitely to complete the DST. This is useful if a system is having high disc usage and DST is unable to progress
    //!
    //  Exit:
    //!   \return SUCCESS on successful completion, FAILURE = fail
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API int run_DST(tDevice *device, eDSTType DSTType, bool pollForProgress, bool captiveForeground, bool ignoreMaxTime);

    //-----------------------------------------------------------------------------
    //
    //  send_DST(tDevice *device, eDSTType DSTType, bool captiveForeground, uint32_t commandTimeout)
    //
    //! \brief   Description:  Function to send a ATA Spec DST or SCSI spec DST to a device
    //
    //  Entry:
    //!   \param[in] device = file descriptor
    //!   \param[in] DSTType = see enum above
    //!   \param[in] pollForProgress = 0 = don't poll, just start the test. 1 = poll for progress and display the progress on the screen.
    //!   \param[in] captiveForeground = when set to true, the self test is run in captive/foreground mode. This is only for ATA or SCSI. When set, this will wait for the entire test to complete before returning. This is ignored on NVMe
    //!
    //  Exit:
    //!   \return SUCCESS on successful completion, FAILURE = fail
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API int send_DST(tDevice *device, eDSTType DSTType, bool captiveForeground, uint32_t commandTimeout);

    //-----------------------------------------------------------------------------
    //
    //  abort_DST()
    //
    //! \brief   Description:  Function to send a DST abort to a device
    //
    //  Entry:
    //!   \param[in] device = file descriptor
    //!
    //  Exit:
    //!   \return SUCCESS on successful completion, FAILURE = fail
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API int abort_DST(tDevice *device);

    //-----------------------------------------------------------------------------
    //
    //  get_DST_Progress()
    //
    //! \brief   Description:  Function to get the progress of an active DST test
    //
    //  Entry:
    //!   \param[in] device = file descriptor
    //!   \param[out] percentComplete = pointer to a uint32 that will hold the DST %complete
    //!   \param[out] status = pointer to a value to hold the current DST status
    //!
    //  Exit:
    //!   \return SUCCESS = pass, FAILURE = fail
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API int get_DST_Progress(tDevice *device, uint32_t *percentComplete, uint8_t *status);

    //-----------------------------------------------------------------------------
    //
    //  print_DST_Progress()
    //
    //! \brief   Description:  Function to get and print the progress of an active DST test
    //
    //  Entry:
    //!   \param[in] device = file descriptor
    //!
    //  Exit:
    //!   \return SUCCESS = pass, FAILURE = fail
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API int print_DST_Progress(tDevice *device);
    
    #define MAX_DST_STATUS_STRING_LENGTH 160
    OPENSEA_OPERATIONS_API void translate_DST_Status_To_String(uint8_t status, char *translatedString, bool justRanDST, bool isNVMeDrive);

    //-----------------------------------------------------------------------------
    //
    //  get_Long_DST_Time( tDevice * device )
    //
    //! \brief   Get the long DST timeout in hours and minutes. This function fills in the data referenced by the passed in pointers so it's up to the called to use the data as they wish
    //
    //  Entry:
    //!   \param device - file descriptor
    //!   \param hours - pointer to a uint8_t to hold the number of hours
    //!   \param minutes - pointer to a uint8_t to hold the number of minutes
    //!
    //  Exit:
    //!   \return SUCCESS = good, !SUCCESS something went wrong see error codes
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API int get_Long_DST_Time(tDevice *device, uint8_t *hours, uint8_t *minutes);

    //-----------------------------------------------------------------------------
    //
    //  ata_Abort_DST()
    //
    //! \brief   Description:  Function to send a DST abort to a device
    //
    //  Entry:
    //!   \param[in] device = file descriptor
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API int ata_Abort_DST(tDevice *device);

    //-----------------------------------------------------------------------------
    //
    //  ata_Get_DST_Progress()
    //
    //! \brief   Description:  Function to get the DST progress
    //
    //  Entry:
    //!   \param[in] device = file descriptor
    //!   \param[out] percentComplete = pointer to the variable that will be filled in with the percent Complete
    //!   \param[out] status = pointer to the variable that will be filled in with the DST status/result
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API int ata_Get_DST_Progress(tDevice *device, uint32_t *percentComplete, uint8_t *status);

    //-----------------------------------------------------------------------------
    //
    //  scsi_Get_DST_Progress()
    //
    //! \brief   Description:  Function to get the current DST progress and Status/result
    //
    //  Entry:
    //!   \param[in] device - pointer to the device structure
    //!   \param[out] percentComplete - pointer to a variable to hold the percentage completed
    //!   \param[out] status - pointer to a variable to hold the status
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API int scsi_Get_DST_Progress(tDevice *device, uint32_t *percentComplete, uint8_t *status);

    //-----------------------------------------------------------------------------
    //
    //  scsi_Abort_DST()
    //
    //! \brief   Description:  Function to Send a SCSI DST abort
    //
    //  Entry:
    //!   \param[in] device - pointer to the device structure
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API int scsi_Abort_DST(tDevice *device);

    //-----------------------------------------------------------------------------
    //
    //  get_Error_LBA_From_DST_Log()
    //
    //! \brief   Description:  Function to get the error LBA from the attached device from the DST log. Will auto detect ATA vs SCSI
    //
    //  Entry:
    //!   \param[in] device - pointer to the device structure
    //!   \param[out] lba - pointer to a uint64_t that will hold the LBA of the last error
    //!
    //  Exit:
    //!   \return true = valid LBA returned, false = invalid LBA. Could not read log, or status indicates failure other than read failure
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API bool get_Error_LBA_From_DST_Log(tDevice *device, uint64_t *lba);

    //-----------------------------------------------------------------------------
    //
    //  get_Error_LBA_From_ATA_DST_Log()
    //
    //! \brief   Description:  Function to get the error LBA from the attached device from the appropriate ATA DST log
    //
    //  Entry:
    //!   \param[in] device - pointer to the device structure
    //!   \param[out] lba - pointer to a uint64_t that will hold the LBA of the last error
    //!
    //  Exit:
    //!   \return true = valid LBA returned, false = invalid LBA. Could not read log, or status indicates failure other than read failure
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API bool get_Error_LBA_From_ATA_DST_Log(tDevice *device, uint64_t *lba);

    //-----------------------------------------------------------------------------
    //
    //  get_Error_LBA_From_SCSI_DST_Log()
    //
    //! \brief   Description:  Function to get the error LBA from the attached device from the SCSI DST log
    //
    //  Entry:
    //!   \param[in] device - pointer to the device structure
    //!   \param[out] lba - pointer to a uint64_t that will hold the LBA of the last error
    //!
    //  Exit:
    //!   \return true = valid LBA returned, false = invlaid LBA. Could not read log, or status indicates failure other than read failure
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API bool get_Error_LBA_From_SCSI_DST_Log(tDevice *device, uint64_t *lba);


    typedef struct _dstAndCleanErrorList
    {
        ptrErrorLBA ptrToErrorList;//pointer to the list so there is no need to copy memory all over the place
        uint64_t *errorIndex;//pointer to the current index value so that DST and clean can update this and give it back to the caller
    }dstAndCleanErrorList, *ptrDSTAndCleanErrorList;

    //-----------------------------------------------------------------------------
    //
    //  run_DST_And_Clean()
    //
    //! \brief   Description:  This function performs a DST and clean. It starts DST, gets the error, fixes it, then repeats until all errors are fixed or the error limit is reached.
    //!                        The error limit to this function should NOT be adjusted for number of logical sectors per physical sector like in the generic test functions.
    //
    //  Entry:
    //!   \param[in] device - pointer to the device structure
    //!   \param[in] errorLimit - value representing number of errors to fix. This must be 1 or higher.
    //!   \param[in] updateFunction - 
    //!   \param[in] updateData - 
    //!   \param[in] externalErrorList - optional. Only use if you intend to do other things before or after DST & Clean. With this parameter, the ending result error list will not print.
    //!   \param[in] repaired - flag for Tattoo log for when the drive has been repaired.
    //!
    //  Exit:
    //!   \return SUCCESS = completed DST and clean successfully, !SUCCESS = error limit reached, or unrepairable DST condition
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API int run_DST_And_Clean(tDevice *device, uint16_t errorLimit, custom_Update updateFunction, void *updateData, ptrDSTAndCleanErrorList externalErrorList, bool *repaired);

    typedef struct _dstDescriptor
    {
        bool descriptorValid;
        uint8_t selfTestRun;//"Content of LBA 0:7" in ATA spec, or from self test status in NVMe
        uint8_t selfTestExecutionStatus;
        union
        {
            uint32_t lifetimeTimestamp;
            uint32_t powerOnHours;
        };//union isn't necessary but might help make someone's life easier if they understand this data one way or another
        union
        {
            uint8_t checkPointByte;
            uint8_t segmentNumber;
        };//union isn't necessary but might help make someone's life easier if they understand this data one way or another
        union
        {
            uint8_t ataVendorSpecificData[15];
            uint8_t scsiVendorSpecificByte;
            uint16_t nvmeVendorSpecificWord;
        };
        uint64_t lbaOfFailure;//Invalid if set to all F's
        bool nsidValid;//when set to true, the namespace ID is valid
        uint32_t namespaceID;//NVMe only.
        union
        {
            //sense info below is translated according to SAT for ATA. SCSI will set this right from the log
            struct
            {
                uint8_t senseKey;
                uint8_t additionalSenseCode;
                uint8_t additionalSenseCodeQualifier;
            }scsiSenseCode;
            struct
            {
                bool statusCodeValid;
                bool statusCodeTypeValid;
                uint8_t statusCode;
                uint8_t statusCodeType;
            }nvmeStatus;
        };
    }dstDescriptor, *ptrDescriptor;

    //max of 21 allowed by ATA spec for SMART log. 19/page for GPL, but I've never seen more than 21 entries supported...-TJE
    //Max of 20 in NVMe specification
    #define MAX_DST_ENTRIES 21 

    typedef enum _dstLogType
    {
        DST_LOG_TYPE_UNKNOWN,
        DST_LOG_TYPE_ATA,
        DST_LOG_TYPE_SCSI,
        DST_LOG_TYPE_NVME,
    }dstLogType;

    typedef struct _dstLogEntries
    {
        uint8_t numberOfEntries;//count of how many valid entries are placed into this struct
        dstLogType logType;//can be used by things printing this data to parse things correctly
        dstDescriptor dstEntry[MAX_DST_ENTRIES];
    }dstLogEntries, *ptrDstLogEntries;

    OPENSEA_OPERATIONS_API int get_DST_Log_Entries(tDevice *device, ptrDstLogEntries entries);

    OPENSEA_OPERATIONS_API int print_DST_Log_Entries(ptrDstLogEntries entries);

    OPENSEA_OPERATIONS_API bool is_Self_Test_Supported(tDevice *device);

    OPENSEA_OPERATIONS_API bool is_Conveyence_Self_Test_Supported(tDevice *device);

#if defined (__cplusplus)
}
#endif
