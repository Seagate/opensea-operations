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
// \file firmware_download.h
// \brief This file defines the function for performing a firmware download to a drive

#pragma once

#include "operations.h"
#include "operations_Common.h"

#if defined(__cplusplus)
extern "C"
{
#endif

#define FIRMWARE_UPDATE_DATA_VERSION 1

    typedef struct _firmwareUpdateData {
        size_t size; //set to sizeof(firmwareUpdateData)
        uint32_t version; //set to FIRMWARE_UPDATE_DATA_VERSION
        eDownloadMode   dlMode; //how to do the download. Full, Segmented, Deferred, etc
        uint16_t        segmentSize; //size of segments to use when doing segmented. If 0, will use 64.
        uint8_t         *firmwareFileMem; //pointer to the firmware file read into memory to send to the drive.
        uint32_t        firmwareMemoryLength; //length of the memory the firmware file was read into. This should be a multiple of 512B sizes...
        uint64_t        avgSegmentDlTime; //stores the average segment time for the download
        uint64_t        activateFWTime; //stores the amount of time it took to issue the last segment and activate the new code (on segmented). On deferred this is only the time to activate.
        union
        {
            uint8_t firmwareSlot;//NVMe
            uint8_t bufferID;//SCSI
        };
        bool existingFirmwareImage;//set to true means you are activiting an existing firmware image in the specified slot. - NVMe only
        bool ignoreStatusOfFinalSegment;//This is a legacy compatibility option. Some old drives do not return status on the last segment, but the download is successful and this ignores the failing status from the OS and reports SUCCESS when set to true.
    } firmwareUpdateData;
    //-----------------------------------------------------------------------------
    //
    //  firmware_Download()
    //
    //! \brief   Description:  This function takes a device handle, a download mode, and a file pointer and download the file to the device specified
    //
    //  Entry:
    //!   \param[in] device = file descriptor
    //!   \param[in] options = pointer to struct with all the needed options for sending a firmware download
    //!
    //  Exit:
    //!   \return SUCCESS on successful completion, FAILURE = fail
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API int firmware_Download(tDevice *device, firmwareUpdateData * options);

    //See extended inquiry VPD page in SPC spec for details
    typedef enum _eSCSIMicrocodeActivation
    {
        SCSI_MICROCODE_ACTIVATE_NOT_INDICATED = 0,
        SCSI_MICROCODE_ACTIVATE_BEFORE_COMMAND_COMPLETION = 1,
        SCSI_MICROCODE_ACTIVATE_AFTER_EVENT = 2,
        SCSI_MICROCODE_ACTIVATE_RESERVED = 3
    }SCSIMicrocodeActivation;

    typedef struct _firmwareSlotRevision
    {
        char revision[9];//8 characters and a null terminator - TJE
    }firmwareSlotRevision;

    typedef struct _firmwareSlotInfo 
    {
        bool firmwareSlotInfoValid;//must be true to be valid
        bool slot1ReadOnly;
        bool activateWithoutAResetSupported;
        uint8_t numberOfSlots;//from identify
        uint8_t activeSlot;//from firmware log page
        uint8_t nextSlotToBeActivated;//only valid if non-zero
        firmwareSlotRevision slotRevisionInfo[7];//up to 7 slots supported in NVMe spec. (I figured this would be more readable than a 2 dimensional array - TJE)
    }firmwareSlotInfo;
    //NOTE: If firmware slot info changes, the supported modes data structure below must change version and size since this is only used in there right now-TJE

    #define SUPPORTED_FWDL_MODES_VERSION 1

    typedef struct _supportedDLModes
    {
        size_t size;//set to sizeof(supportedDLModes)
        uint32_t version;// set to SUPPORTED_FWDL_MODES_VERSION
        bool downloadMicrocodeSupported;//should always be true unless it's a super old drive that doesn't support a download command
        bool fullBuffer;
        bool segmented;
        bool deferred;//includes activate command (mode Eh only!)
        bool deferredSelectActivation;//SAS Only! (mode Dh)
        bool seagateDeferredPowerCycleActivate;
        bool firmwareDownloadDMACommandSupported;
        bool scsiInfoPossiblyIncomplete;
        bool deferredPowerCycleActivationSupported;//SATA will always set this to true!
        bool deferredHardResetActivationSupported;//SAS only
        bool deferredVendorSpecificActivationSupported;//SAS only
        uint32_t minSegmentSize;//in 512B blocks...May not be accurate for SAS Value of 0 means there is no minumum
        uint32_t maxSegmentSize;//in 512B blocks...May not be accurate for SAS. Value of all F's means there is no maximum
        uint16_t recommendedSegmentSize;//in 512B blocks...check SAS
        uint8_t driveOffsetBoundary;//this is 2^<value>
        uint32_t driveOffsetBoundaryInBytes;//This is the bytes value from the PO2 calculation in the comment above.
        eDownloadMode recommendedDownloadMode;
        SCSIMicrocodeActivation codeActivation;//SAS Only
        eMLU multipleLogicalUnitsAffected;//This will only be set for multi-lun devices. NVMe will set this since firmware affects all namespaces on the controller
        firmwareSlotInfo firmwareSlotInfo;//Basically NVMe only at this point since such a concept doesn't exist for ATA or SCSI at this time - TJE
    }supportedDLModes, *ptrSupportedDLModes;

    //-----------------------------------------------------------------------------
    //
    //  get_Supported_FWDL_Modes(tDevice *device, ptrSupportedDLModes supportedModes)
    //
    //! \brief   Description:  This function will print out the supported firmware information reported by the drive.
    //!          Note: For SAS, this may not be accurate on older products that don't support the "report supported operations" command 
    //!          or do not return very good information on the supported "write buffer" modes as allowed in newer specifications.
    //
    //  Entry:
    //!   \param[in] device = file descriptor
    //!   \param[in] supportedModes = pointer to a supported DL modes structure that will be filled in with valid information
    //!
    //  Exit:
    //!   \return SUCCESS on successful completion, FAILURE = fail
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API int get_Supported_FWDL_Modes(tDevice *device, ptrSupportedDLModes supportedModes);

    //-----------------------------------------------------------------------------
    //
    //  show_Supported_FWDL_Modes(tDevice *device, ptrSupportedDLModes supportedModes)
    //
    //! \brief   Description:  This function will print out the supported firmware information reported by the drive.
    //
    //  Entry:
    //!   \param[in] device = file descriptor
    //!   \param[in] supportedModes = pointer to a supported DL modes structure that has been filled in with valid information
    //!
    //  Exit:
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API void show_Supported_FWDL_Modes(tDevice *device, ptrSupportedDLModes supportedModes);

#if defined(__cplusplus)
}
#endif
