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
// \file firmware_download.c
// \brief This file defines the function for performing a firmware download to a drive

#include "common_types.h"
#include "precision_timer.h"
#include "memory_safety.h"
#include "type_conversion.h"
#include "string_utils.h"
#include "bit_manip.h"
#include "code_attributes.h"
#include "math_utils.h"
#include "error_translation.h"
#include "io_utils.h"
#include "sleep.h"
#if defined (_WIN32)
#include "windows_version_detect.h" //for Windows API checks
#endif

#include "operations_Common.h"
#include "firmware_download.h"
#include "logs.h"
#include "platform_helper.h"
#include "power_control.h"

//In order to be able to validate the data on any CPU, we don't want to hardcode any lengths in case things get packed or aligned differently.
//So define each struct version here internally so we can do sizeof(v1), etc to check it.
//Each time the structure changes, add a new one that we can use to verify size, version, length, etc

#define FIRMWARE_UPDATE_DATA_VERSION_V1 1

typedef struct _firmwareUpdateDataV1 {
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
} firmwareUpdateDataV1;

#define FIRMWARE_UPDATE_DATA_VERSION_V2 2

typedef struct _firmwareUpdateDataV2 {
    size_t size; //set to sizeof(firmwareUpdateData)
    uint32_t version; //set to FIRMWARE_UPDATE_DATA_VERSION
    eDownloadMode   dlMode; //how to do the download. Full, Segmented, Deferred, etc
    uint16_t        segmentSize; //size of segments to use when doing segmented. If 0, will use 64.
    uint8_t* firmwareFileMem; //pointer to the firmware file read into memory to send to the drive.
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
    bool forceCommitActionValid;
    uint8_t forceCommitAction;//NVMe only. forceCommitActionValid must be true to use this.
    bool disableResetAfterCommit;//NVMe only
} firmwareUpdateDataV2;

#define FIRMWARE_UPDATE_DATA_VERSION_V3 3

typedef struct _firmwareUpdateDataV3 {
    size_t size; //set to sizeof(firmwareUpdateData)
    uint32_t version; //set to FIRMWARE_UPDATE_DATA_VERSION
    eFirmwareUpdateMode   dlMode; //how to do the download. Full, Segmented, Deferred, etc
    uint16_t        segmentSize; //size of segments to use when doing segmented. If 0, will use 64.
    uint8_t* firmwareFileMem; //pointer to the firmware file read into memory to send to the drive.
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
    bool forceCommitActionValid;
    uint8_t forceCommitAction;//NVMe only. forceCommitActionValid must be true to use this.
    bool disableResetAfterCommit;//NVMe only
} firmwareUpdateDataV3;

static eReturnValues check_For_Power_Cycle_Required(eReturnValues ret, tDevice *device)
{
#if defined (_WIN32) && WINVER >= SEA_WIN32_WINNT_WIN10
    //Check if the device needs a power cycle to complete the update...This has been necessary in Windows with the Intel NVMe driver in some cases
    if (ret == SUCCESS && device->drive_info.drive_type == NVME_DRIVE)
    {
        //we do not already have a "Power cycle is requied" return code, so need to check the firmware log for the NVMe device to see if the "next active" slot value is non-zero.
        //If it is non-zero then the low-level driver did not issue the necessary reset for activation.
        //read the firmware log for more information
        DECLARE_ZERO_INIT_ARRAY(uint8_t, firmwareLog, 512);
        nvmeGetLogPageCmdOpts firmwareLogOpts;
        firmwareLogOpts.addr = firmwareLog;
        firmwareLogOpts.dataLen = 512;
        firmwareLogOpts.lid = 3;
        firmwareLogOpts.nsid = 0;
        if (SUCCESS == nvme_Get_Log_Page(device, &firmwareLogOpts))
        {
            if (is_Empty(firmwareLog, 512))
            {
                //if the driver is saying this command passed, but the data is empty, it is a driver bug but this is a driver with a known need for the power cycle to happen.
                //For some reason, this driver will do the update, but attempting to read this log gives back zeros until you close all software (closing all handles is not enough) and return empty data for this log.
                ret = POWER_CYCLE_REQUIRED;
            }
            else
            {
                //uint8_t activeSlot = M_GETBITRANGE(firmwareLog[0], 2, 0);
                uint8_t nextSlotToBeActivated = M_GETBITRANGE(firmwareLog[0], 6, 4);
                if (nextSlotToBeActivated != 0)
                {
                    ret = POWER_CYCLE_REQUIRED;
                }
                //this is a workaround where this log is reported empty, which is NOT correct, but seems to match when the power cycle is actually needed to complete activation -TJE
                //This is most likely an Intel Driver caching problem from what I can tell when debugging this issue.
                if (is_Empty(firmwareLog, 512))
                {
                    ret = POWER_CYCLE_REQUIRED;
                }
                //set the firmware revision in each slot
                //for (uint32_t slotIter = 0, offset = 8; slotIter <= M_GETBITRANGE(device->drive_info.IdentifyData.nvme.ctrl.frmw, 3, 1) && slotIter <= 7 /*max of 7 slots in spec and structure*/ && offset < 512; ++slotIter, offset += 8)
                //{
                //    DECLARE_ZERO_INIT_ARRAY(char, rev, 9);
                //    memcpy(rev, &firmwareLog[offset], 8);
                //    rev[8] = '\0';
                //    printf("slot %u: %s\n", slotIter, rev);
                //}
            }
        }
    }
    return ret;
#else
    //this is a windows only issue, so just return
    M_USE_UNUSED(device);
    return ret;
#endif //_WIN32 and WINVER >= WIN10
}

eReturnValues firmware_Download(tDevice *device, firmwareUpdateData * options)
{
    eReturnValues ret = SUCCESS;
#ifdef _DEBUG
    printf("--> %s\n", __FUNCTION__);
#endif

    //first verify the provided structure info to make sure it is compatible.
    if (options && options->version >= FIRMWARE_UPDATE_DATA_VERSION_V1 && options->size >= sizeof(firmwareUpdateDataV1))
    {
        bool nvmeForceCA = false;
        bool nvmeforceDisableReset = false;
        uint8_t nvmeForceCommitAction = 0;//since bool is zero, this will not matter
        if (options->version >= FIRMWARE_UPDATE_DATA_VERSION_V2 && options->size >= sizeof(firmwareUpdateDataV2))
        {
            nvmeForceCA = options->forceCommitActionValid;
            nvmeForceCommitAction = options->forceCommitAction;
            nvmeforceDisableReset = options->disableResetAfterCommit;
        }
        if (options->version < FIRMWARE_UPDATE_DATA_VERSION_V3)
        {
            //older structures reused the eDownloadMode enum in cmds.h
            //While v3 should be backwards compatible, do a translation here before entering the remaining code
            switch (C_CAST(eDownloadMode, options->dlMode))
            {
            case DL_FW_ACTIVATE:
                options->dlMode = FWDL_UPDATE_MODE_ACTIVATE;
                break;
            case DL_FW_FULL:
                options->dlMode = FWDL_UPDATE_MODE_FULL;
                break;
            case DL_FW_TEMP:
                options->dlMode = FWDL_UPDATE_MODE_TEMP;
                break;
            case DL_FW_SEGMENTED:
                options->dlMode = FWDL_UPDATE_MODE_SEGMENTED;
                break;
            case DL_FW_DEFERRED:
                options->dlMode = FWDL_UPDATE_MODE_DEFERRED;
                break;
            case DL_FW_DEFERRED_SELECT_ACTIVATE:
                options->dlMode = FWDL_UPDATE_MODE_DEFERRED_SELECT_ACTIVATE;
                break;
#if defined (_WIN32) && defined (_MSC_VER) && !defined (__clang__)
            //visual studio complains about this NOT being here and GCC does the opposite...so only add this case for visual studio.
            case FWDL_UPDATE_MODE_AUTOMATIC:
#endif //_MSC_VER
            case DL_FW_UNKNOWN: //no direct translation, but call it automatic mode
                options->dlMode = FWDL_UPDATE_MODE_AUTOMATIC;
                break;
            }
        }

        bool automaticModeDetection = false;
        supportedDLModes fwdlSupport;
        memset(&fwdlSupport, 0, sizeof(supportedDLModes));
        fwdlSupport.version = SUPPORTED_FWDL_MODES_VERSION;
        fwdlSupport.size = sizeof(supportedDLModes);
        get_Supported_FWDL_Modes(device, &fwdlSupport);

        if (options->dlMode == FWDL_UPDATE_MODE_AUTOMATIC)
        {
            automaticModeDetection = true;
            options->dlMode = fwdlSupport.recommendedDownloadMode;
            //special case. Some SAS controllers (PMC 8070) filter the SCSI write buffer command mode and will not allow deferred download
            //So change this to segmented for this case.
            if (device->drive_info.passThroughHacks.scsiHacks.writeBufferNoDeferredDownload)
            {
                options->dlMode = FWDL_UPDATE_MODE_SEGMENTED;
            }
        }

        //send test unit ready to determine if spinup command is required. If it is, send the SCSI start-stop unit command to do the spinup.
        scsiStatus turStatus;
        memset(&turStatus, 0, sizeof(scsiStatus));
        scsi_Test_Unit_Ready(device, &turStatus);//Note: Not checking for success because we want to evaluate the received sense data ourselves in this case-TJE
        if (turStatus.senseKey == SENSE_KEY_NOT_READY)
        {
            //check for "initilizing command required"
            if (turStatus.asc == 0x04 && turStatus.ascq == 0x02)
            {
                //send the start-stop unit command with the "start" bit set to one.
                //We should only hit this on anything scsi encapsulated either by a SATL or RAID controller or a native SCSI drive that needs
                //a spin up before receiving any other commands.
                scsi_Start_Stop_Unit(device, false, 0, 0, false, false, true);
            }
        }

        //Adding a flush cache here because it is occasionally needed on some devices. It is just a precaution to avoid potential issues with firmware not flushing when it activates new code.
        flush_Cache(device);

        if (options->dlMode == FWDL_UPDATE_MODE_ACTIVATE)
        {
            if (device->drive_info.drive_type == NVME_DRIVE && options->existingFirmwareImage && options->firmwareSlot == 0)
            {
                //cannot activate slot 0 for an existing image. Value should be between 1 and 7
                //Activating with slot 0 is only allowed for letting the controller choose an image for replacing after sending it to the drive. Not applicable for switching slots
                return NOT_SUPPORTED;
            }
            os_Lock_Device(device);
            ret = firmware_Download_Command(device, DL_FW_ACTIVATE, 0, 0, options->firmwareFileMem, options->firmwareSlot, options->existingFirmwareImage, false, false, 60, nvmeForceCA, nvmeForceCommitAction, nvmeforceDisableReset);//giving 60 seconds to activate the firmware
            options->activateFWTime = options->avgSegmentDlTime = device->drive_info.lastCommandTimeNanoSeconds;
#if defined (_WIN32) && WINVER >= SEA_WIN32_WINNT_WIN10
            if (device->drive_info.drive_type != NVME_DRIVE && ret == OS_PASSTHROUGH_FAILURE && device->os_info.fwdlIOsupport.fwdlIOSupported && device->os_info.last_error == ERROR_INVALID_FUNCTION)
            {
                //This means that we encountered a driver that is not allowing us to issue the Win10 API Firmware activate call for some unknown reason. 
                //This doesn't happen with Microsoft's AHCI driver though...
                //Instead, we should disable the use of the API and retry with passthrough to perform the activation. This is not preferred at all. 
                //We want to use the Win10 API whenever possible so the system is ready for the changes to the bus and drive information so that it is less likely to BSOD like we used to see in older versions of Windows.
                device->os_info.fwdlIOsupport.fwdlIOSupported = false;
                ret = firmware_Download_Command(device, DL_FW_ACTIVATE, 0, 0, options->firmwareFileMem, options->firmwareSlot, options->existingFirmwareImage, false, false, 60, nvmeForceCA, nvmeForceCommitAction, nvmeforceDisableReset);
                options->activateFWTime = options->avgSegmentDlTime = device->drive_info.lastCommandTimeNanoSeconds;
                device->os_info.fwdlIOsupport.fwdlIOSupported = true;
            }
#endif //_WIN32 and WINVER >= WIN10
            os_Unlock_Device(device);
            ret = check_For_Power_Cycle_Required(ret, device);
            return ret;
        }
        if (options->firmwareMemoryLength == 0)
        {
            if (device->deviceVerbosity > VERBOSITY_QUIET)
            {
                printf("Error: empty file\n");
            }
            return FAILURE;
        }
        if (options->dlMode == FWDL_UPDATE_MODE_FULL || options->dlMode == FWDL_UPDATE_MODE_TEMP)
        {
            eDownloadMode mode = DL_FW_FULL;
            if (options->dlMode == FWDL_UPDATE_MODE_TEMP)
            {
                mode = DL_FW_TEMP;
            }
            os_Lock_Device(device);
            //single command to do the whole download
            ret = firmware_Download_Command(device, mode, 0, options->firmwareMemoryLength, options->firmwareFileMem, options->bufferID, false, true, true, 60, nvmeForceCA, nvmeForceCommitAction, nvmeforceDisableReset);
            options->activateFWTime = options->avgSegmentDlTime = device->drive_info.lastCommandTimeNanoSeconds;
            os_Unlock_Device(device);
        }
        else
        {
            eDownloadMode downloadMode = DL_FW_SEGMENTED;
            uint32_t downloadSize = 0;
            uint32_t downloadBlocks = 0;
            uint32_t downloadRemainder = 0;
            uint32_t downloadOffset = 0;
            uint32_t currentDownloadBlock = 0;

            if (device->drive_info.drive_type == NVME_DRIVE && options->dlMode == FWDL_UPDATE_MODE_SEGMENTED)
            {
                options->dlMode = FWDL_UPDATE_MODE_DEFERRED_PLUS_ACTIVATE;
            }

            switch (options->dlMode)
            {
            case FWDL_UPDATE_MODE_ACTIVATE:
            case FWDL_UPDATE_MODE_FULL:
            case FWDL_UPDATE_MODE_TEMP:
            case FWDL_UPDATE_MODE_AUTOMATIC:
                //these will not happen as they are already handled above
                return BAD_PARAMETER;
                break;
            case FWDL_UPDATE_MODE_SEGMENTED:
                downloadMode = DL_FW_SEGMENTED;
                break;
            case FWDL_UPDATE_MODE_DEFERRED_PLUS_ACTIVATE:
            case FWDL_UPDATE_MODE_DEFERRED:
                downloadMode = DL_FW_DEFERRED;
                if (device->drive_info.passThroughHacks.scsiHacks.writeBufferNoDeferredDownload)
                {
                    if (device->deviceVerbosity > VERBOSITY_QUIET)
                    {
                        printf("\nWARNING: This controller is known to filter the SCSI write-buffer command and block deferred download.\n");
                        printf("         If the firmware update fails, try using segmented download instead.\n\n");
                    }
                }
                break;
            case FWDL_UPDATE_MODE_DEFERRED_SELECT_ACTIVATE:
                downloadMode = DL_FW_DEFERRED_SELECT_ACTIVATE;
                if (device->drive_info.passThroughHacks.scsiHacks.writeBufferNoDeferredDownload)
                {
                    if (device->deviceVerbosity > VERBOSITY_QUIET)
                    {
                        printf("\nWARNING: This controller is known to filter the SCSI write-buffer command and block deferred download.\n");
                        printf("         If the firmware update fails, try using segmented download instead.\n\n");
                    }
                }
                break;
            }

            //multiple commands needed to do the download (segmented)
            if (options->segmentSize == 0)
            {
                //If segment size is not specified, set to compatible defaults for now. This is more complicated on Windows due to old workarounds - TJE
#if defined (_WIN32) && defined (WINVER)
                if (device->os_info.ioType == WIN_IOCTL_ATA_PASSTHROUGH)
                {
#if WINVER >= SEA_WIN32_WINNT_WIN10
                    if (device->os_info.fwdlIOsupport.fwdlIOSupported)
                    {
                        options->segmentSize = 64;
                        //this driver supports the FWDL ioctl, so it likely does not have a problem with multi-sector transfers - TJE
                    }
                    else
                    {
                        //changing the transfer size to single blocks as a workaround for old drivers. This is more generic than I would like, but do not currently have a 
                        //better solution for this old issue.
                        //This issue goes back to Windows XP ATA passthrough days and only single sector transfers work properly on these old, strange drivers.
                        //Ideally this check is more enhanced for specific drivers that are known to have this issue, but there is not currently enough information
                        //to setup this more complicated check. -TJE
                        options->segmentSize = 1;
                    }
#else //winver >=win10
                    //not enough information, so assume old XP workaround listed above. - TJE
                    options->segmentSize = 1;
#endif //winver >= win10
                }
                else
                {
                    //not ATA passthrough, so do not worry about working around strange driver issues
                    options->segmentSize = 64;
                }
#else
                //Not Windows, so no strange driver workarounds necessary at this time - TJE
                options->segmentSize = 64;
#endif
            }
            downloadSize = options->segmentSize * LEGACY_DRIVE_SEC_SIZE;
            downloadBlocks = options->firmwareMemoryLength / downloadSize;
            downloadRemainder = options->firmwareMemoryLength % downloadSize;

#if defined (_WIN32) && defined(WINVER)
#if WINVER >= SEA_WIN32_WINNT_WIN10
            //saving this for later since we may need to turn it off...
            bool deviceSupportsWinAPI = device->os_info.fwdlIOsupport.fwdlIOSupported;
            if (device->drive_info.drive_type != NVME_DRIVE && deviceSupportsWinAPI && downloadMode == DL_FW_DEFERRED)
            {
                //check if alignment requirements will be met
                if (options->firmwareMemoryLength % device->os_info.fwdlIOsupport.payloadAlignment)
                {
                    //turn off use of the windows API since this file cannot meet the alignment requirements for the whole download
                    device->os_info.fwdlIOsupport.fwdlIOSupported = false;
                }
                //check if transfer size requirements will be met
                else if (downloadSize > device->os_info.fwdlIOsupport.maxXferSize || downloadRemainder > device->os_info.fwdlIOsupport.maxXferSize)
                {
                    //transfer size for download or remainder are greater than the API can handle, so just turn it off... (Unlikely to happen)
                    device->os_info.fwdlIOsupport.fwdlIOSupported = false;
                }
            }
#endif //WINVER >= WIN10
#endif //_WIN32 && WINVER
            os_Lock_Device(device);
            //start the download
            currentDownloadBlock = 0;
            while (currentDownloadBlock < downloadBlocks)
            {
                bool lastSegment = false;
                bool firstSegment = false;
                uint32_t fwdlTimeout = 30;
                if (currentDownloadBlock + 1 == downloadBlocks && downloadRemainder == 0)
                {
                    lastSegment = true;
                    fwdlTimeout = 60;
                }
                else if (currentDownloadBlock == 0)
                {
                    firstSegment = true;
                }
                ret = firmware_Download_Command(device, downloadMode, downloadOffset, downloadSize, &options->firmwareFileMem[downloadOffset], options->bufferID, false, firstSegment, lastSegment, fwdlTimeout, nvmeForceCA, nvmeForceCommitAction, nvmeforceDisableReset);
                options->avgSegmentDlTime += device->drive_info.lastCommandTimeNanoSeconds;

                if (currentDownloadBlock % 20 == 0)
                {
                    if (device->deviceVerbosity > VERBOSITY_QUIET)
                    {
                        printf(".");
                        fflush(stdout);
                    }
                }
                if (ret != SUCCESS)
                {
                    if (automaticModeDetection && (downloadMode == DL_FW_DEFERRED || downloadMode == DL_FW_DEFERRED_SELECT_ACTIVATE) && downloadOffset == 0 && device->drive_info.drive_type != NVME_DRIVE)
                    {
                        //possible drive bug where it reported an unsupported mode
                        //try changing to a segmented mode
                        //This retry is only when not forcing a specific mode and when the very first segment fails.
                        //2 more conditions for this. 
                        // 1. For SCSI/SAS, need to look for invalid field in CDB. An invalid firmware returns invalid field in parameter list, so if you get invalid field in parameter, do NOT retry.
                        // 2. For ATA/SATA, need to look for a command abort in the rtfrs. There is no other way for SATA to specify things, so it is a less robust check
                        if (device->drive_info.drive_type == ATA_DRIVE && device->drive_info.lastCommandRTFRs.status & ATA_STATUS_BIT_ERROR && device->drive_info.lastCommandRTFRs.error & ATA_ERROR_BIT_ABORT)
                        {
                            options->dlMode = FWDL_UPDATE_MODE_SEGMENTED;
                            downloadMode = DL_FW_SEGMENTED;
                            if (device->deviceVerbosity > VERBOSITY_QUIET)
                            {
                                printf("\nAutomatic deferred download failed. Either the drive does not support this mode\n");
                                printf("or this is an invalid firmware image for this device.\n");
                                printf("Retrying the download with segmented download mode to verify.\n");
                                fflush(stdout);
                            }
                            continue;
                        }
                        else if (device->drive_info.drive_type == SCSI_DRIVE)
                        {
                            uint8_t senseKey = 0;
                            uint8_t asc = 0;
                            uint8_t ascq = 0;
                            uint8_t fru = 0;
                            get_Sense_Key_ASC_ASCQ_FRU(device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, &senseKey, &asc, &ascq, &fru);
                            if (senseKey == SENSE_KEY_ILLEGAL_REQUEST && asc == 0x24 && ascq == 0x00)
                            {
                                options->dlMode = FWDL_UPDATE_MODE_SEGMENTED;
                                downloadMode = DL_FW_SEGMENTED;
                                if (device->deviceVerbosity > VERBOSITY_QUIET)
                                {
                                    printf("\nAutomatic deferred download failed. Either the drive does not support this mode\n");
                                    printf("or this is an invalid firmware image for this device.\n");
                                    printf("Retrying the download with segmented download mode to verify.\n");
                                    fflush(stdout);
                                }
                                continue;
                            }
                            else
                            {
                                //this is not a case to issue a retry as it looks like the drive rejected the command for another reason
                                break;
                            }
                        }
                        else
                        {
                            //this is not a case to issue a retry as it looks like the drive rejected the command for another reason
                            break;
                        }
                    }
                    else
                    {
                        //error occurred for a specific mode rather than automatic mode for deferred download, so exit and do not continue trying the download.
                        break;
                    }
                }
                currentDownloadBlock++;
                downloadOffset += downloadSize;
            }

            if (!downloadRemainder)
            {
                options->activateFWTime = device->drive_info.lastCommandTimeNanoSeconds;
            }

            //check to make sure we haven't had a failure yet
            if (!fwdlSupport.seagateDeferredPowerCycleActivate && options->ignoreStatusOfFinalSegment && ret != SUCCESS)
            {
                if (downloadMode == DL_FW_SEGMENTED && downloadRemainder == 0 && (currentDownloadBlock + 1) == downloadBlocks)
                {
                    //this means that we had an error on the last sector, which is a drive bug in old products.
                    //Check that we don't have RTFRs from the last command and that the sense data does not say "unaligned write command"
                    //We may need to expand this check if we encounter this problem in other OS's or on other kinds of controllers (currently this is from a motherboard)
                    if (device->drive_info.drive_type == ATA_DRIVE && device->drive_info.lastCommandRTFRs.status == 0 && device->drive_info.lastCommandRTFRs.error == 0)
                    {
                        uint8_t senseKey = 0;
                        uint8_t asc = 0;
                        uint8_t ascq = 0;
                        uint8_t fru = 0;
                        get_Sense_Key_ASC_ASCQ_FRU(device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, &senseKey, &asc, &ascq, &fru);
                        if (senseKey == SENSE_KEY_ILLEGAL_REQUEST && asc == 0x21 && ascq == 0x04)//Check fru?
                        {
                            ret = SUCCESS;
                        }
                    }
                }
            }

            //download remaining data
            if (downloadRemainder > 0 && ret == SUCCESS)
            {
                if (device->drive_info.drive_type == ATA_DRIVE)
                {
                    //round the remainder to the nearest sector since ATA talks in terms of sectors
                    downloadRemainder = ((downloadRemainder + LEGACY_DRIVE_SEC_SIZE) - 1) / LEGACY_DRIVE_SEC_SIZE;
                    //convert number of sectors back to bytes
                    downloadRemainder *= LEGACY_DRIVE_SEC_SIZE;
                }
#if defined (_WIN32) && defined(WINVER)
#if WINVER >= SEA_WIN32_WINNT_WIN10
                //If we are here, then this is windows and the Windows 10 API may be being used below.
                //Because of this, there are additional allignment requirements for the segments that we must meet.
                //So now we are going to check if this meets those requirements...if not, we need to allocate a different buffer that meets the requirements, copy the data to it, then send the command. - TJE
                if (device->drive_info.drive_type != NVME_DRIVE && device->os_info.fwdlIOsupport.fwdlIOSupported && downloadMode == DL_FW_DEFERRED)//checking to see if Windows says the FWDL API is supported
                {
                    if (downloadRemainder < device->os_info.fwdlIOsupport.maxXferSize && (downloadRemainder % device->os_info.fwdlIOsupport.payloadAlignment == 0))
                    {
                        //we're fine, just issue the command
                        ret = firmware_Download_Command(device, downloadMode, downloadOffset, downloadRemainder, &options->firmwareFileMem[downloadOffset], options->bufferID, false, false, true, 60, nvmeForceCA, nvmeForceCommitAction, nvmeforceDisableReset);
                    }
                    else if (!(downloadRemainder < device->os_info.fwdlIOsupport.maxXferSize))
                    {
                        //This shouldn't happen. We should catch that the individual chunks or remainder are too big and shouldn't fall into here...
                        ret = OS_PASSTHROUGH_FAILURE;
                    }
                    else if (downloadRemainder % device->os_info.fwdlIOsupport.payloadAlignment)
                    {
                        //This SHOULDN'T happen. We should catch that this won't work earlier when we check if the file meets alignment requirements or not.
                        ret = OS_PASSTHROUGH_FAILURE;
                    }
                }
                else //not supported, so nothing else needs to be done other than issue the command
                {
                    ret = firmware_Download_Command(device, downloadMode, downloadOffset, downloadRemainder, &options->firmwareFileMem[downloadOffset], options->bufferID, false, false, true, 60, nvmeForceCA, nvmeForceCommitAction, nvmeforceDisableReset);
                }
#else
                //not windows 10 API, so just issue the command
                ret = firmware_Download_Command(device, downloadMode, downloadOffset, downloadRemainder, &options->firmwareFileMem[downloadOffset], options->bufferID, false, false, true, 60, nvmeForceCA, nvmeForceCommitAction, nvmeforceDisableReset);
#endif
#else
                //not windows 10 API, so just issue the command
                ret = firmware_Download_Command(device, downloadMode, downloadOffset, downloadRemainder, &options->firmwareFileMem[downloadOffset], options->bufferID, false, false, true, 60, nvmeForceCA, nvmeForceCommitAction, nvmeforceDisableReset);
#endif
                if (device->deviceVerbosity > VERBOSITY_QUIET)
                {
                    printf(".");
                    fflush(stdout);
                }
                if (!fwdlSupport.seagateDeferredPowerCycleActivate && options->ignoreStatusOfFinalSegment && ret != SUCCESS)
                {
                    if (downloadMode == DL_FW_SEGMENTED)
                    {
                        //this means that we had an error on the last sector, which is a drive bug in old products.
                        //Check that we don't have RTFRs from the last command and that the sense data does not say "unaligned write command"
                        //We may need to expand this check if we encounter this problem in other OS's or on other kinds of controllers (currently this is from a motherboard)
                        if (device->drive_info.drive_type == ATA_DRIVE && device->drive_info.lastCommandRTFRs.status == 0 && device->drive_info.lastCommandRTFRs.error == 0)
                        {
                            uint8_t senseKey = 0;
                            uint8_t asc = 0;
                            uint8_t ascq = 0;
                            uint8_t fru = 0;
                            get_Sense_Key_ASC_ASCQ_FRU(device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, &senseKey, &asc, &ascq, &fru);
                            if (senseKey == SENSE_KEY_ILLEGAL_REQUEST && asc == 0x21 && ascq == 0x04)//Check fru?
                            {
                                ret = SUCCESS;
                            }
                        }
                    }
                }
                options->activateFWTime = device->drive_info.lastCommandTimeNanoSeconds;
                options->avgSegmentDlTime += device->drive_info.lastCommandTimeNanoSeconds;
            }
            if (options->dlMode == FWDL_UPDATE_MODE_DEFERRED_PLUS_ACTIVATE && ret == SUCCESS)
            {
                delay_Milliseconds(100);//This is here because there seems to be a need for a delay. If activating too quickly after the Firmware is downloaded, it seems to fail. This works - TJE
                //send an activate command (not an existing slot, this is a new image activation)
                ret = firmware_Download_Command(device, DL_FW_ACTIVATE, 0, 0, options->firmwareFileMem, options->firmwareSlot, false, false, false, 60, nvmeForceCA, nvmeForceCommitAction, nvmeforceDisableReset);
                options->activateFWTime = options->avgSegmentDlTime = device->drive_info.lastCommandTimeNanoSeconds;
                ret = check_For_Power_Cycle_Required(ret, device);
            }
            os_Unlock_Device(device);
            if (device->deviceVerbosity > VERBOSITY_QUIET)
            {
                printf("\n");
            }
            options->avgSegmentDlTime /= C_CAST(uint64_t, currentDownloadBlock) + UINT64_C(1);
#if defined (_WIN32) && defined(WINVER)
#if WINVER >= SEA_WIN32_WINNT_WIN10
            //restore this value back to what it was (if it was ever even changed)
            device->os_info.fwdlIOsupport.fwdlIOSupported = deviceSupportsWinAPI;

            if (downloadMode == DL_FW_SEGMENTED && fwdlSupport.seagateDeferredPowerCycleActivate && ret == SUCCESS)
            {
                ret = POWER_CYCLE_REQUIRED;
            }
#endif //WINVER >= WIN10
#endif //_WIN32 && WINVER
        }
    }
    else //invalid or incompatible update structure.
    {
        ret = BAD_PARAMETER;
    }

#ifdef _DEBUG
    printf("<-- %s (%d)\n", __FUNCTION__, ret);
#endif
    return ret;
}

#define SUPPORTED_FWDL_MODES_VERSION_V1 1

typedef struct _supportedDLModesV1
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
}supportedDLModesV1, *ptrSupportedDLModesV1;

#define SUPPORTED_FWDL_MODES_VERSION_V2 2

typedef struct _supportedDLModesV2
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
    eFirmwareUpdateMode recommendedDownloadMode;
    SCSIMicrocodeActivation codeActivation;//SAS Only
    eMLU multipleLogicalUnitsAffected;//This will only be set for multi-lun devices. NVMe will set this since firmware affects all namespaces on the controller
    firmwareSlotInfo firmwareSlotInfo;//Basically NVMe only at this point since such a concept doesn't exist for ATA or SCSI at this time - TJE
}supportedDLModesV2, *ptrSupportedDLModesV2;

eReturnValues get_Supported_FWDL_Modes(tDevice *device, ptrSupportedDLModes supportedModes)
{
    eReturnValues ret = SUCCESS;
    if (supportedModes && supportedModes->version >= SUPPORTED_FWDL_MODES_VERSION_V1 && supportedModes->size >= sizeof(supportedDLModesV1))
    {
        switch (device->drive_info.drive_type)
        {
        case ATA_DRIVE:
        {
            //first check the bits in the identify data
            if ((is_ATA_Identify_Word_Valid(device->drive_info.IdentifyData.ata.Word053) && device->drive_info.IdentifyData.ata.Word053 & BIT1) /* this is a validity bit for field 69 */
                && (is_ATA_Identify_Word_Valid(device->drive_info.IdentifyData.ata.Word069) && device->drive_info.IdentifyData.ata.Word069 & BIT8))
            {
                supportedModes->downloadMicrocodeSupported = true;
                supportedModes->firmwareDownloadDMACommandSupported = true;
                supportedModes->fullBuffer = true;
                supportedModes->driveOffsetBoundaryInBytes = LEGACY_DRIVE_SEC_SIZE;
                supportedModes->driveOffsetBoundary = 9;
            }
            if ((is_ATA_Identify_Word_Valid_With_Bits_14_And_15(device->drive_info.IdentifyData.ata.Word083) && device->drive_info.IdentifyData.ata.Word083 & BIT0)
                || (is_ATA_Identify_Word_Valid(device->drive_info.IdentifyData.ata.Word086) && device->drive_info.IdentifyData.ata.Word086 & BIT0))
            {
                supportedModes->downloadMicrocodeSupported = true;
                supportedModes->fullBuffer = true;
            }
            if (is_ATA_Identify_Word_Valid(device->drive_info.IdentifyData.ata.Word086) && device->drive_info.IdentifyData.ata.Word086 & BIT15)/*words 119, 120 valid*/
            {
                if ((is_ATA_Identify_Word_Valid(device->drive_info.IdentifyData.ata.Word119) && device->drive_info.IdentifyData.ata.Word119 & BIT4)
                    || (is_ATA_Identify_Word_Valid(device->drive_info.IdentifyData.ata.Word120) && device->drive_info.IdentifyData.ata.Word120 & BIT4))
                {
                    supportedModes->segmented = true;
                }
            }
            if (is_ATA_Identify_Word_Valid(device->drive_info.IdentifyData.ata.Word234))
            {
                supportedModes->minSegmentSize = M_BytesTo2ByteValue(M_Byte1(device->drive_info.IdentifyData.ata.Word234), M_Byte0(device->drive_info.IdentifyData.ata.Word234));
            }
            else
            {
                supportedModes->minSegmentSize = 0;
            }
            if (is_ATA_Identify_Word_Valid(device->drive_info.IdentifyData.ata.Word235))
            {
                supportedModes->maxSegmentSize = M_BytesTo2ByteValue(M_Byte1(device->drive_info.IdentifyData.ata.Word235), M_Byte0(device->drive_info.IdentifyData.ata.Word235));
            }
            else
            {
                supportedModes->maxSegmentSize = UINT32_MAX;
            }
            if (is_Seagate_Family(device) == SEAGATE && is_ATA_Identify_Word_Valid(device->drive_info.IdentifyData.ata.Word243) && device->drive_info.IdentifyData.ata.Word243 & BIT12)
            {
                supportedModes->seagateDeferredPowerCycleActivate = true;
                supportedModes->deferredPowerCycleActivationSupported = true;
            }
            //now try reading the supportd capabilities page of the identify device data log for the remaining info (deferred download)
            DECLARE_ZERO_INIT_ARRAY(uint8_t, supportedCapabilities, LEGACY_DRIVE_SEC_SIZE);
            if (SUCCESS == send_ATA_Read_Log_Ext_Cmd(device, ATA_LOG_IDENTIFY_DEVICE_DATA, ATA_ID_DATA_LOG_SUPPORTED_CAPABILITIES, supportedCapabilities, ATA_LOG_PAGE_LEN_BYTES, 0))
            {
                uint64_t supportedCapabilitiesQword = M_BytesTo8ByteValue(supportedCapabilities[15], supportedCapabilities[14], supportedCapabilities[13], supportedCapabilities[12], supportedCapabilities[11], supportedCapabilities[10], supportedCapabilities[9], supportedCapabilities[8]);
                uint64_t dlMicrocodeBits = M_BytesTo8ByteValue(supportedCapabilities[23], supportedCapabilities[22], supportedCapabilities[21], supportedCapabilities[20], supportedCapabilities[19], supportedCapabilities[18], supportedCapabilities[17], supportedCapabilities[16]);
                //this bit should always be set to 1, but doesn't hurt to check
                if (supportedCapabilitiesQword & ATA_ID_DATA_QWORD_VALID_BIT)
                {
                    if (supportedCapabilitiesQword & BIT33)
                    {
                        supportedModes->downloadMicrocodeSupported = true;
                        supportedModes->firmwareDownloadDMACommandSupported = true;
                        supportedModes->fullBuffer = true;
                    }
                    if (supportedCapabilitiesQword & BIT14)
                    {
                        supportedModes->downloadMicrocodeSupported = true;
                        supportedModes->fullBuffer = true;
                    }
                    if (supportedCapabilitiesQword & BIT3)
                    {
                        supportedModes->segmented = true;
                    }
                }
                if (dlMicrocodeBits & ATA_ID_DATA_QWORD_VALID_BIT)
                {
                    if (dlMicrocodeBits & BIT34)
                    {
                        supportedModes->deferred = true;
                        supportedModes->deferredPowerCycleActivationSupported = true;
                    }
                    if (dlMicrocodeBits & BIT33)
                    {
                        supportedModes->fullBuffer = true;
                    }
                    if (dlMicrocodeBits & BIT32)
                    {
                        supportedModes->segmented = true;
                    }
                    supportedModes->maxSegmentSize = M_BytesTo2ByteValue(M_Byte3(dlMicrocodeBits), M_Byte2(dlMicrocodeBits));
                    if (supportedModes->downloadMicrocodeSupported && !(supportedModes->maxSegmentSize > 0 && supportedModes->maxSegmentSize < 0xFFFF))
                    {
                        supportedModes->maxSegmentSize = UINT32_MAX;
                    }
                    supportedModes->minSegmentSize = M_BytesTo2ByteValue(M_Byte1(dlMicrocodeBits), M_Byte0(dlMicrocodeBits));
                    if (supportedModes->downloadMicrocodeSupported && !(supportedModes->minSegmentSize > 0 && supportedModes->minSegmentSize < 0xFFFF))
                    {
                        supportedModes->minSegmentSize = 0;
                    }
                }
            }
            supportedModes->recommendedSegmentSize = 64;
        }
        break;
        case NVME_DRIVE:
            if (device->drive_info.IdentifyData.nvme.ctrl.oacs & BIT2)
            {
                supportedModes->downloadMicrocodeSupported = true;
                supportedModes->deferred = true;
                supportedModes->multipleLogicalUnitsAffected = MLU_AFFECTS_ALL_LU;//Firmware affects all namespaces attached to the controller
                //byte 260
                //BIT0 = firmware slot 1 is read only
                //BITS 3:1 = number of supported firmware slots
                //BIT4 = firmware activation without a reset
                //byte 319 - update granularity
                uint32_t updateGranularity = device->drive_info.IdentifyData.nvme.ctrl.fwug;
                supportedModes->driveOffsetBoundaryInBytes = updateGranularity * 4096;
                supportedModes->minSegmentSize = 0;
                supportedModes->maxSegmentSize = UINT32_MAX;
                if (updateGranularity == 0xFF)
                {
                    //no granularity limits
                    supportedModes->driveOffsetBoundary = 0xFF;
                    supportedModes->driveOffsetBoundaryInBytes = 1;
                }
                else if (updateGranularity > 0)
                {
                    //get the power of 2 that represents this byte value
                    uint8_t counter = 0;
                    uint32_t updateGranularityD = updateGranularity;
                    while (updateGranularityD != 0)
                    {
                        updateGranularityD = updateGranularityD >> 1;
                        ++counter;
                    }
                    supportedModes->driveOffsetBoundary = counter - 1;
                    supportedModes->minSegmentSize = C_CAST(uint16_t, supportedModes->driveOffsetBoundaryInBytes / LEGACY_DRIVE_SEC_SIZE);
                    supportedModes->maxSegmentSize = UINT32_MAX;
                }
                else
                {
                    //granularity not provided
                    supportedModes->driveOffsetBoundary = 0;
                    supportedModes->driveOffsetBoundaryInBytes = 0;
#if defined (_WIN32) && WINVER >= SEA_WIN32_WINNT_WIN10
                    if (device->os_info.fwdlIOsupport.fwdlIOSupported)
                    {
                        //If we got here in Windows, then we need to make sure we follow any default rules MS sets for using their API
                        supportedModes->driveOffsetBoundaryInBytes = device->os_info.fwdlIOsupport.payloadAlignment;
                        uint32_t byteAlignment = device->os_info.fwdlIOsupport.payloadAlignment;
                        uint16_t counter = 0;
                        while (byteAlignment != 0)
                        {
                            byteAlignment = byteAlignment >> 1;
                            ++counter;
                        }
                        supportedModes->driveOffsetBoundary = C_CAST(uint8_t, counter - UINT8_C(1));
                    }
#endif
                }
#if defined (_WIN32) && WINVER >= SEA_WIN32_WINNT_WIN10
                if (device->os_info.fwdlIOsupport.fwdlIOSupported)
                {
                    //If Windows, then we need to make sure we don't break their rules.-TJE
                    if ((supportedModes->maxSegmentSize / 512) > device->os_info.fwdlIOsupport.maxXferSize)
                    {
                        supportedModes->maxSegmentSize = device->os_info.fwdlIOsupport.maxXferSize / 512;//segments are in 512B blocks for
                    }
                }
#endif
                supportedModes->firmwareSlotInfo.activateWithoutAResetSupported = device->drive_info.IdentifyData.nvme.ctrl.frmw & BIT4;
                supportedModes->firmwareSlotInfo.numberOfSlots = M_GETBITRANGE(device->drive_info.IdentifyData.nvme.ctrl.frmw, 3, 1);
                supportedModes->firmwareSlotInfo.slot1ReadOnly = device->drive_info.IdentifyData.nvme.ctrl.frmw & BIT0;
                //read the firmware log for more information
                DECLARE_ZERO_INIT_ARRAY(uint8_t, firmwareLog, 512);
                nvmeGetLogPageCmdOpts firmwareLogOpts;
                firmwareLogOpts.addr = firmwareLog;
                firmwareLogOpts.dataLen = 512;
                firmwareLogOpts.lid = 3;
                firmwareLogOpts.nsid = 0;
                if (SUCCESS == nvme_Get_Log_Page(device, &firmwareLogOpts))
                {
                    supportedModes->firmwareSlotInfo.firmwareSlotInfoValid = true;
                    supportedModes->firmwareSlotInfo.activeSlot = M_GETBITRANGE(firmwareLog[0], 2, 0);
                    supportedModes->firmwareSlotInfo.nextSlotToBeActivated = M_GETBITRANGE(firmwareLog[0], 6, 4);
                    //set the firmware revision in each slot
                    for (uint32_t slotIter = 0, offset = 8; slotIter <= supportedModes->firmwareSlotInfo.numberOfSlots && slotIter <= 7 /*max of 7 slots in spec and structure*/ && offset < 512; ++slotIter, offset += 8)
                    {
                        memcpy(supportedModes->firmwareSlotInfo.slotRevisionInfo[slotIter].revision, &firmwareLog[offset], 8);
                        supportedModes->firmwareSlotInfo.slotRevisionInfo[slotIter].revision[8] = '\0';
                    }
                }
            }
            /*
            //NOTE: This is the code that was previously for when running in SCSI translation, but showed up as an NVMe drive. It probably wasn't used and falling into the next case will be ok. Keeping it as a comment for now - TJE
            //running in SCSI translation mode, so only set full & deferred download modes
            supportedModes->downloadMicrocodeSupported = true;
            supportedModes->fullBuffer = true;
            supportedModes->deferred = true;
            supportedModes->minSegmentSize = 0;
            supportedModes->maxSegmentSize = UINT32_MAX;
            //need to set the offset requirement...for now I'm setting the minimum the NVMe spec says can be reported...should be OK...-TJE
            supportedModes->driveOffsetBoundaryInBytes = 4096;//4Kb is the minimum specified in the NVMe specification that the drive may conform to..this should be good enough for the translation.
            supportedModes->driveOffsetBoundary = 12;//power of 2
            */
            break;
        case SCSI_DRIVE:
        {
            //before trying all the code below, look at the extended inquiry data page so see if the download modes are supported or not.
            uint8_t *extendedInq = C_CAST(uint8_t*, safe_calloc_aligned(VPD_EXTENDED_INQUIRY_LEN, sizeof(uint8_t), device->os_info.minimumAlignment));
            if (extendedInq)
            {
                if (SUCCESS == scsi_Inquiry(device, extendedInq, VPD_EXTENDED_INQUIRY_LEN, EXTENDED_INQUIRY_DATA, true, false))
                {
                    if (extendedInq[12] & BIT7)
                    {
                        supportedModes->deferredPowerCycleActivationSupported = true;
                    }
                    if (extendedInq[12] & BIT6)
                    {
                        supportedModes->deferredHardResetActivationSupported = true;
                    }
                    if (extendedInq[12] & BIT5)
                    {
                        supportedModes->deferredVendorSpecificActivationSupported = true;
                    }
                    supportedModes->codeActivation = C_CAST(SCSIMicrocodeActivation, M_GETBITRANGE(extendedInq[4], 7, 6));
                    if (extendedInq[12] & BIT4)//dms valid
                    {
                        supportedModes->downloadMicrocodeSupported = true;
                        //bit 7 = dm_md_4 - temporary...not saving at this time since it's rarely used.
                        //bit 6 = dm_md_5
                        supportedModes->fullBuffer = M_ToBool(extendedInq[19] & BIT6);
                        //bit 5 = dm_md_6 - segmented temporary...not saving at this time since it's rarely used
                        //bit 4 = dm_md_7
                        supportedModes->segmented = M_ToBool(extendedInq[19] & BIT4);
                        //bit 3 = dm_md_d - deferred, select activation
                        supportedModes->deferredSelectActivation = M_ToBool(extendedInq[19] & BIT3);
                        //bit 2 = dm_md_e - deferred
                        supportedModes->deferred = M_ToBool(extendedInq[19] & BIT2);
                        //bit 1 = dm_md_f - activate deferred code (part of mode e. If mode e is supported, so should f - TJE
                    }
                }
                safe_Free_aligned(C_CAST(void**, &extendedInq)) ;// PRH valgrind check
            }

            //PMC 8070 fails this command for some unknown reason even if a drive supports it, so skip these requests when this hack is set.-TJE
            if (!device->drive_info.passThroughHacks.scsiHacks.noReportSupportedOperations)
            {
                uint8_t* writeBufferSupportData = C_CAST(uint8_t*, safe_calloc_aligned(14, sizeof(uint8_t), device->os_info.minimumAlignment));
                //first try asking for supported operation code for Full Buffer download
                if (SUCCESS == scsi_Report_Supported_Operation_Codes(device, false, REPORT_OPERATION_CODE_AND_SERVICE_ACTION, WRITE_BUFFER_CMD, SCSI_WB_DL_MICROCODE_SAVE_ACTIVATE, 14, writeBufferSupportData))
                {
                    switch (writeBufferSupportData[1] & 0x07)
                    {
                    case 3://supported according to spec
                        supportedModes->downloadMicrocodeSupported = true;
                        supportedModes->fullBuffer = true;
                        supportedModes->multipleLogicalUnitsAffected = C_CAST(eMLU, M_GETBITRANGE(writeBufferSupportData[1], 6, 5));
                        break;
                    default:
                        break;
                    }
                    //if this worked, then we know we can ask about other supported operation codes.
                    if (SUCCESS == scsi_Report_Supported_Operation_Codes(device, false, REPORT_OPERATION_CODE_AND_SERVICE_ACTION, WRITE_BUFFER_CMD, SCSI_WB_DL_MICROCODE_OFFSETS_SAVE_ACTIVATE, 14, writeBufferSupportData))
                    {
                        switch (writeBufferSupportData[1] & 0x07)
                        {
                        case 3://supported according to spec
                        {
                            supportedModes->segmented = true;
                            supportedModes->recommendedSegmentSize = 64;
                            //set the min/max segment size from the cmd information bitfield
                            uint32_t length = M_BytesTo4ByteValue(0, writeBufferSupportData[10], writeBufferSupportData[11], writeBufferSupportData[12]);
                            if (length == UINT32_C(0xFFFFFF) || length == 0)
                            {
                                supportedModes->maxSegmentSize = UINT32_MAX;
                                supportedModes->minSegmentSize = 0;
                            }
                            else
                            {
                                supportedModes->maxSegmentSize = length;
                                //the minimum is the lowest non-zero bit
                                uint32_t counter = 0;
                                while ((length & BIT0) == 0 && counter < UINT32_C(0xFFFFFF))
                                {
                                    length = length >> 1;
                                    ++counter;
                                }
                                supportedModes->minSegmentSize = 1 << counter;
                            }
                        }
                        break;
                        default:
                            break;
                        }
                    }
                    if (SUCCESS == scsi_Report_Supported_Operation_Codes(device, false, REPORT_OPERATION_CODE_AND_SERVICE_ACTION, WRITE_BUFFER_CMD, SCSI_WB_DL_MICROCODE_OFFSETS_SAVE_DEFER, 14, writeBufferSupportData))
                    {
                        switch (writeBufferSupportData[1] & 0x07)
                        {
                        case 3://supported according to spec
                        {
                            supportedModes->deferred = true;
                            supportedModes->recommendedSegmentSize = 64;
                            //set the min/max segment size from the cmd information bitfield
                            uint32_t length = M_BytesTo4ByteValue(0, writeBufferSupportData[10], writeBufferSupportData[11], writeBufferSupportData[12]);
                            if (length == UINT32_C(0xFFFFFF) || length == 0)
                            {
                                supportedModes->maxSegmentSize = UINT32_MAX;
                                supportedModes->minSegmentSize = 0;
                            }
                            else
                            {
                                supportedModes->maxSegmentSize = length;
                                //the minimum is the lowest non-zero bit
                                uint32_t counter = 0;
                                while ((length & BIT0) == 0 && counter < UINT32_C(0xFFFFFF))
                                {
                                    length = length >> 1;
                                    ++counter;
                                }
                                supportedModes->minSegmentSize = 1 << counter;
                            }
                        }
                        break;
                        default:
                            break;
                        }
                    }
                    if (SUCCESS == scsi_Report_Supported_Operation_Codes(device, false, REPORT_OPERATION_CODE_AND_SERVICE_ACTION, WRITE_BUFFER_CMD, SCSI_WB_DL_MICROCODE_OFFSETS_SAVE_SELECT_ACTIVATE_DEFER, 14, writeBufferSupportData))
                    {
                        switch (writeBufferSupportData[1] & 0x07)
                        {
                        case 0: //not available right now...so not supported
                        case 1://not supported
                            break;
                        case 3://supported according to spec
                        case 5://supported in vendor specific manor in same format as case 3
                        {
                            supportedModes->deferredSelectActivation = true;
                            supportedModes->recommendedSegmentSize = 64;
                            //set the min/max segment size from the cmd information bitfield
                            uint32_t length = M_BytesTo4ByteValue(0, writeBufferSupportData[10], writeBufferSupportData[11], writeBufferSupportData[12]);
                            if (length == UINT32_C(0xFFFFFF) || length == 0)
                            {
                                supportedModes->maxSegmentSize = UINT32_MAX;
                                supportedModes->minSegmentSize = 0;
                            }
                            else
                            {
                                supportedModes->maxSegmentSize = length;
                                //the minimum is the lowest non-zero bit
                                uint32_t counter = 0;
                                while ((length & BIT0) == 0 && counter < UINT32_C(0xFFFFFF))
                                {
                                    length = length >> 1;
                                    ++counter;
                                }
                                supportedModes->minSegmentSize = 1 << counter;
                            }
                        }
                        break;
                        default:
                            break;
                        }
                    }
                }
                else
                {
                    //check if unsupported op code versus invalid field in CDB
                    uint8_t senseKey = 0;
                    uint8_t asc = 0;
                    uint8_t ascq = 0;
                    uint8_t fru = 0;
                    get_Sense_Key_ASC_ASCQ_FRU(device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, &senseKey, &asc, &ascq, &fru);
                    if (senseKey == SENSE_KEY_ILLEGAL_REQUEST && asc == 0x20 && ascq == 0x00)
                    {
                        //report supported operation codes isn't supported, so try legacy inquiry cmdDT method
                        uint8_t* temp = C_CAST(uint8_t*, safe_reallocf_aligned(C_CAST(void**, &writeBufferSupportData), 14, 16, device->os_info.minimumAlignment));
                        if (!temp)
                        {
                            return MEMORY_FAILURE;
                        }
                        writeBufferSupportData = temp;
                        memset(writeBufferSupportData, 0, 16);
                        //if that still doesn't work, we can try the obsolete method using the inquiry command
                        if (SUCCESS == scsi_Inquiry(device, writeBufferSupportData, 16, WRITE_BUFFER_CMD, false, true))
                        {
                            supportedModes->scsiInfoPossiblyIncomplete = true;
                            switch (writeBufferSupportData[1] & 0x07)
                            {
                            case 3://supported according to spec
                            {
                                //try to look at the mode bit field and determine which modes are supported...
                                uint8_t mode = writeBufferSupportData[7] & 0x1F;//byte 1 of the write buffer cdb itself
                                if ((mode & 0x07) == 0x07)
                                {
                                    //full and segmented supported
                                    supportedModes->downloadMicrocodeSupported = true;
                                    supportedModes->fullBuffer = true;
                                    supportedModes->segmented = true;
                                    supportedModes->recommendedSegmentSize = 64;
                                }
                                else if (mode & BIT2) //we'll just say full only...no really good way to do this honestly
                                {
                                    supportedModes->downloadMicrocodeSupported = true;
                                    supportedModes->fullBuffer = true;
                                }
                                uint32_t length = M_BytesTo4ByteValue(0, writeBufferSupportData[12], writeBufferSupportData[13], writeBufferSupportData[14]);
                                if (length == UINT32_C(0xFFFFFF) || length == 0)
                                {
                                    supportedModes->maxSegmentSize = UINT32_MAX;
                                    supportedModes->minSegmentSize = 0;
                                }
                                else
                                {
                                    supportedModes->maxSegmentSize = length;
                                    //the minimum is the lowest non-zero bit
                                    uint32_t counter = 0;
                                    while ((length & BIT0) == 0 && counter < UINT32_C(0xFFFFFF))
                                    {
                                        length = length >> 1;
                                        ++counter;
                                    }
                                    supportedModes->minSegmentSize = 1 << counter;
                                }
                            }
                            break;
                            default:
                                //vendor specific or reserved formats
                                break;
                            }
                        }
                        else
                        {
                            //NVMe needs some special case here
                            if (strncmp(device->drive_info.T10_vendor_ident, "NVMe", 4) == 0)
                            {
                                supportedModes->deferred = true;
                                supportedModes->downloadMicrocodeSupported = true;
                                supportedModes->fullBuffer = true;
                                supportedModes->scsiInfoPossiblyIncomplete = false;//setting this since we know it's NVMe, so we're pretty sure that this is what we'll have from SCSI translation
                                supportedModes->recommendedSegmentSize = 64;
                                supportedModes->maxSegmentSize = UINT32_MAX;
                                supportedModes->minSegmentSize = 0;
                                supportedModes->multipleLogicalUnitsAffected = MLU_AFFECTS_ALL_LU;
                            }
                            else
                            {
                                supportedModes->downloadMicrocodeSupported = true;//set this to on so we return good status...
                                supportedModes->scsiInfoPossiblyIncomplete = true;
                                //Setting supported stuff below even though we don't know for sure...should be safe enough.
                                supportedModes->fullBuffer = true;
                                supportedModes->segmented = true;
                                supportedModes->recommendedSegmentSize = 64;
                                supportedModes->maxSegmentSize = UINT32_MAX;
                                supportedModes->minSegmentSize = 0;
                            }
                        }
                    }
                    else
                    {
                        //try asking without a service action
                        if (SUCCESS == scsi_Report_Supported_Operation_Codes(device, false, REPORT_OPERATION_CODE, WRITE_BUFFER_CMD, 0, 14, writeBufferSupportData))
                        {
                            supportedModes->scsiInfoPossiblyIncomplete = true;
                            bool writeBufferCmdSupported = false;
                            switch (writeBufferSupportData[1] & 0x07)
                            {
                            case 3://supported according to spec
                                writeBufferCmdSupported = true;
                                break;
                            default:
                                break;
                            }
                            if (writeBufferCmdSupported)
                            {
                                //try to look at the mode bit field and determine which modes are supported...
                                uint8_t mode = writeBufferSupportData[5] & 0x1F;//byte 1 of the write buffer cdb itself
                                if ((mode & 0x07) == 0x07)
                                {
                                    //full and segmented supported
                                    supportedModes->downloadMicrocodeSupported = true;
                                    supportedModes->fullBuffer = true;
                                    supportedModes->segmented = true;
                                    supportedModes->recommendedSegmentSize = 64;
                                }
                                else if (mode & BIT2) //we'll just say full only...no really good way to do this honestly
                                {
                                    supportedModes->downloadMicrocodeSupported = true;
                                    supportedModes->fullBuffer = true;
                                }
                                uint32_t length = M_BytesTo4ByteValue(0, writeBufferSupportData[10], writeBufferSupportData[11], writeBufferSupportData[12]);
                                if (length == UINT32_C(0xFFFFFF) || length == 0)
                                {
                                    supportedModes->maxSegmentSize = UINT32_MAX;
                                    supportedModes->minSegmentSize = 0;
                                }
                                else
                                {
                                    supportedModes->maxSegmentSize = length;
                                    //the minimum is the lowest non-zero bit
                                    uint32_t counter = 0;
                                    while ((length & BIT0) == 0 && counter < UINT32_C(0xFFFFFF))
                                    {
                                        length = length >> 1;
                                        ++counter;
                                    }
                                    supportedModes->minSegmentSize = 1 << counter;
                                }
                            }
                        }
                        //else try requesting all supported OPs and parse that information??? It could be report all is supported, but other modes are not
                        else
                        {
                            safe_free_aligned(&writeBufferSupportData);
                                uint32_t reportAllOPsLength = 4;
                            uint8_t* reportAllOPs = C_CAST(uint8_t*, safe_calloc_aligned(reportAllOPsLength, sizeof(uint8_t), device->os_info.minimumAlignment));
                            if (reportAllOPs)
                            {
                                if (SUCCESS == scsi_Report_Supported_Operation_Codes(device, false, REPORT_ALL, 0, 0, reportAllOPsLength, reportAllOPs))
                                {
                                    //get the full length, then reallocate and reread
                                    reportAllOPsLength = M_BytesTo4ByteValue(reportAllOPs[0], reportAllOPs[1], reportAllOPs[2], reportAllOPs[3]) + 4;
                                    safe_free_aligned(&reportAllOPs);
                                        reportAllOPs = C_CAST(uint8_t*, safe_calloc_aligned(reportAllOPsLength, sizeof(uint8_t), device->os_info.minimumAlignment));
                                    if (reportAllOPs)
                                    {
                                        if (SUCCESS == scsi_Report_Supported_Operation_Codes(device, false, REPORT_ALL, 0, 0, reportAllOPsLength, reportAllOPs))
                                        {
                                            //loop through the data and check for the commands and service actions we are interested in.
                                            uint32_t supportedCmdsIter = 4;
                                            uint16_t cmdDescriptorLength = 8;
                                            uint32_t supportedCmdsLength = M_BytesTo4ByteValue(reportAllOPs[0], reportAllOPs[1], reportAllOPs[2], reportAllOPs[3]) + 4;
                                            for (; supportedCmdsIter < supportedCmdsLength; supportedCmdsIter += cmdDescriptorLength)
                                            {
                                                uint8_t operationCode = reportAllOPs[supportedCmdsIter];
                                                uint16_t serviceAction = M_BytesTo2ByteValue(reportAllOPs[supportedCmdsIter + 2], reportAllOPs[supportedCmdsIter + 3]);
                                                bool serviceActionValid = M_ToBool(reportAllOPs[supportedCmdsIter + 5] & BIT0);
                                                eMLU mlu = C_CAST(eMLU, M_GETBITRANGE(reportAllOPs[supportedCmdsIter + 5], 5, 4));
                                                cmdDescriptorLength = (reportAllOPs[supportedCmdsIter + 5] & BIT1) ? 20 : 8;
                                                switch (operationCode)
                                                {
                                                case WRITE_BUFFER_CMD:
                                                    if (serviceActionValid)
                                                    {
                                                        switch (serviceAction)
                                                        {
                                                            //case SCSI_WB_DL_MICROCODE_TEMP_ACTIVATE:
                                                        case SCSI_WB_DL_MICROCODE_SAVE_ACTIVATE:
                                                            supportedModes->downloadMicrocodeSupported = true;
                                                            supportedModes->fullBuffer = true;
                                                            supportedModes->multipleLogicalUnitsAffected = mlu;
                                                            break;
                                                            //case SCSI_WB_DL_MICROCODE_OFFSETS_ACTIVATE:
                                                        case SCSI_WB_DL_MICROCODE_OFFSETS_SAVE_ACTIVATE:
                                                            supportedModes->downloadMicrocodeSupported = true;
                                                            supportedModes->segmented = true;
                                                            //In this format, we cannot determine minimum or maximum transfer sizes. so set to max
                                                            supportedModes->maxSegmentSize = 0xFFFF;
                                                            supportedModes->minSegmentSize = 0;
                                                            supportedModes->multipleLogicalUnitsAffected = mlu;
                                                            break;
                                                        case SCSI_WB_DL_MICROCODE_OFFSETS_SAVE_SELECT_ACTIVATE_DEFER:
                                                            supportedModes->downloadMicrocodeSupported = true;
                                                            supportedModes->deferredSelectActivation = true;
                                                            supportedModes->multipleLogicalUnitsAffected = mlu;
                                                            break;
                                                        case SCSI_WB_DL_MICROCODE_OFFSETS_SAVE_DEFER:
                                                            supportedModes->downloadMicrocodeSupported = true;
                                                            supportedModes->deferred = true;
                                                            supportedModes->multipleLogicalUnitsAffected = mlu;
                                                            break;
                                                        case SCSI_WB_ACTIVATE_DEFERRED_MICROCODE: //not currently handled since it is assumed that this will be present if the deferred modes are supported
                                                        default:
                                                            break;
                                                        }
                                                    }
                                                    else
                                                    {
                                                        supportedModes->downloadMicrocodeSupported = true;
                                                        supportedModes->scsiInfoPossiblyIncomplete = true;
                                                        //setting segmented and full buffer download modes in here because they SHOULD work on the products we care about supporting.
                                                        supportedModes->fullBuffer = true;
                                                        if (device->drive_info.scsiVersion > 2)//SPC added segmented. Earlier products only supported full buffer
                                                        {
                                                            supportedModes->segmented = true;
                                                        }
                                                    }
                                                    break;
                                                default:
                                                    break;
                                                }
                                            }
                                            supportedModes->recommendedSegmentSize = 64;
                                        }
                                        else
                                        {
                                            supportedModes->scsiInfoPossiblyIncomplete = true;
                                        }
                                    }
                                }
                                else
                                {
                                    supportedModes->scsiInfoPossiblyIncomplete = true;
                                }
                                safe_free_aligned(&reportAllOPs);
                            }
                        }
                    }
                }
                safe_free_aligned(&writeBufferSupportData);
            }
            DECLARE_ZERO_INIT_ARRAY(uint8_t, offsetReq, 4);
            if (SUCCESS == scsi_Read_Buffer(device, 0x03, 0, 0, 4, offsetReq))
            {
                supportedModes->driveOffsetBoundary = offsetReq[0];
                supportedModes->driveOffsetBoundaryInBytes = 1;//start with this
                if (supportedModes->driveOffsetBoundary > 0 && supportedModes->driveOffsetBoundary != 0xFF)
                {
                    uint16_t counter = 0;
                    while (counter < supportedModes->driveOffsetBoundary)
                    {
                        supportedModes->driveOffsetBoundaryInBytes = supportedModes->driveOffsetBoundaryInBytes << 1;
                        ++counter;
                    }
                }
                else
                {
                    supportedModes->driveOffsetBoundaryInBytes = 1;
                }
            }
            else
            {
                //assume 512B boundaries unless vendor ID is NVMe, in which case assume 4k
                if (strncmp(device->drive_info.T10_vendor_ident, "NVMe", 4) == 0)
                {
                    supportedModes->driveOffsetBoundaryInBytes = UINT32_C(4096);
                    supportedModes->driveOffsetBoundary = 12;
                }
                else
                {
                    supportedModes->driveOffsetBoundaryInBytes = LEGACY_DRIVE_SEC_SIZE;
                    supportedModes->driveOffsetBoundary = 9;
                }
            }

            //The code below is Seagate specific...should this be in Seagate Operations? - TJE
            eSeagateFamily family = is_Seagate_Family(device);
            if ((family == SEAGATE || family == SEAGATE_VENDOR_A) && supportedModes->scsiInfoPossiblyIncomplete)
            {
                uint8_t * c3VPD = C_CAST(uint8_t*, safe_calloc_aligned(255, sizeof(uint8_t), device->os_info.minimumAlignment));
                if (c3VPD)
                {
                    //If the drive is a Seagate SCSI drive, then try reading the C3 mode page which is Seagate specific for the supported features
                    if (SUCCESS == scsi_Inquiry(device, c3VPD, 255, 0xC3, true, false))
                    {
                        supportedModes->downloadMicrocodeSupported = true;
                        supportedModes->scsiInfoPossiblyIncomplete = false;//turning this off because if we read this page we SHOULD know it's capabilities
                        supportedModes->fullBuffer = true;
                        //byte 63  bit7 = QNR
                        if (c3VPD[63] & BIT6)
                        {
                            supportedModes->segmented = true;
                        }
                        //DO NOT turn the flag to false. It should already be false. If it was set to true, then the drive has already reported it supports this mode some other way.
                        if (c3VPD[82] & BIT6)
                        {
                            supportedModes->deferred = true;
                        }
                        //DO NOT turn the flag to false. It should already be false. If it was set to true, then the drive has already reported it supports this mode some other way.
                    }
                }
                safe_free_aligned(&c3VPD);
            }
        }
        break;
        default:
            ret = NOT_SUPPORTED;
            break;
        }
        //set the recommended download mode
        if (supportedModes->downloadMicrocodeSupported)
        {
            //if version < 2 use these old lookup methods
            if (supportedModes->version < SUPPORTED_FWDL_MODES_VERSION_V2)
            {
                //start low and work up to most recommended
                supportedModes->recommendedDownloadMode = C_CAST(int, DL_FW_FULL);
                if (supportedModes->segmented)
                {
                    supportedModes->recommendedDownloadMode = C_CAST(int, DL_FW_SEGMENTED);
                }
                if (supportedModes->deferred && !device->drive_info.passThroughHacks.scsiHacks.writeBufferNoDeferredDownload)
                {
                    supportedModes->recommendedDownloadMode = C_CAST(int, DL_FW_DEFERRED);
                }
            }
            else
            {
                supportedModes->recommendedDownloadMode = FWDL_UPDATE_MODE_DEFERRED_PLUS_ACTIVATE;
                if (!supportedModes->deferred || device->drive_info.passThroughHacks.scsiHacks.writeBufferNoDeferredDownload)
                {
                    //even older ATA drives have no choice, so set these modes when needed
                    supportedModes->recommendedDownloadMode = FWDL_UPDATE_MODE_SEGMENTED;
                    if (!supportedModes->segmented)
                    {
                        supportedModes->recommendedDownloadMode = FWDL_UPDATE_MODE_FULL;
                    }
                }
            }
        }
        else
        {
            ret = NOT_SUPPORTED;
        }
    }
    else
    {
        ret = BAD_PARAMETER;
    }
    return ret;
}

void show_Supported_FWDL_Modes(tDevice *device, ptrSupportedDLModes supportedModes)
{
    if (supportedModes && device && supportedModes->version >= SUPPORTED_FWDL_MODES_VERSION_V1 && supportedModes->size >= sizeof(supportedDLModesV1))
    {
        printf("===Download Support information===\n");
        if (device->drive_info.interface_type == USB_INTERFACE || device->drive_info.interface_type == IEEE_1394_INTERFACE)
        {
            printf("Model Number: %s (%s)\n", device->drive_info.bridge_info.childDriveMN, device->drive_info.product_identification);
            printf("Firmware Revision: %s (%s)\n", device->drive_info.bridge_info.childDriveFW, device->drive_info.product_revision);
        }
        else
        {
            printf("Model Number: %s\n", device->drive_info.product_identification);
            printf("Firmware Revision: %s\n", device->drive_info.product_revision);
        }
        printf("Modes Supported:\n");
        if (supportedModes->downloadMicrocodeSupported)
        {
            if (supportedModes->fullBuffer)
            {
                printf("\tFull\n");
            }
            if (supportedModes->segmented)
            {
                printf("\tSegmented");
                if (supportedModes->seagateDeferredPowerCycleActivate)
                {
                    printf(" (requires power cycle to activate code)");
                }
                printf("\n");
            }
            if (supportedModes->deferred)
            {
                if (device->drive_info.drive_type == NVME_DRIVE)
                {
                    printf("\tDeferred (Requires activation command to activate)\n");
                }
                else
                {
                    printf("\tDeferred (Requires power cycle or activation command to activate)\n");
                }
            }
            if (supportedModes->deferredSelectActivation)//SAS Only
            {
                printf("\tDeferred - Select activation events\n");
                if (supportedModes->deferredPowerCycleActivationSupported || supportedModes->deferredHardResetActivationSupported || supportedModes->deferredVendorSpecificActivationSupported)
                {
                    printf("\t    Supported Activation events:\n");
                    if (supportedModes->deferredPowerCycleActivationSupported)
                    {
                        printf("\t\tPower Cycle\n");
                    }
                    if (supportedModes->deferredHardResetActivationSupported)
                    {
                        printf("\t\tHard Reset\n");
                    }
                    if (supportedModes->deferredVendorSpecificActivationSupported)
                    {
                        printf("\t\tVendor Specific\n");
                    }
                }
            }
            if (supportedModes->maxSegmentSize == UINT32_MAX)
            {
                printf("Maximum Segment Size (512B Blocks): No maximum\n");
            }
            else
            {
                printf("Maximum Segment Size (512B Blocks): %" PRIu32 "\n", supportedModes->maxSegmentSize);
            }
            if (supportedModes->minSegmentSize == 0)
            {
                printf("Minimum Segment Size (512B Blocks): No minimum\n");
            }
            else
            {
                printf("Minimum Segment Size (512B Blocks): %" PRIu32 "\n", supportedModes->minSegmentSize);
            }
            printf("Recommended Segment Size (512B Blocks): %" PRIu16 "\n", supportedModes->recommendedSegmentSize);
            //additional drive requirements
            printf("Buffer Offset required (Bytes): %" PRIu32 "\n", supportedModes->driveOffsetBoundaryInBytes);
            switch (supportedModes->codeActivation)
            {
            case SCSI_MICROCODE_ACTIVATE_BEFORE_COMMAND_COMPLETION:
                printf("Microcode Activation: Activated before completion of final command in write buffer sequence\n");
                break;
            case SCSI_MICROCODE_ACTIVATE_AFTER_EVENT:
                printf("Microcode Activation: Activated after vendor specific event, power cycle, or hard reset\n");
                break;
            case SCSI_MICROCODE_ACTIVATE_RESERVED:
                printf("Microcode Activation: Reserved\n");
                break;
            case SCSI_MICROCODE_ACTIVATE_NOT_INDICATED:
            default:
                //don't print anything...
                break;
            }
            /*printf("Affect on multiple logical units/namespaces: ");
            switch (supportedModes->multipleLogicalUnitsAffected)
            {
            case MLU_NOT_REPORTED:
                printf("Not reported. Device may not support multiple logical units.\n");
                break;
            case MLU_AFFECTS_ONLY_THIS_UNIT:
                printf("FW Updates affect only this unit.\n");
                break;
            case MLU_AFFECTS_MULTIPLE_LU:
                printf("FW Updates affect multiple logical units.\n");
                break;
            case MLU_AFFECTS_ALL_LU:
                printf("FW Updates affect all logical units.\n");
                break;
            }*/
            if (supportedModes->firmwareSlotInfo.firmwareSlotInfoValid)
            {
                printf("Firmware Slot Info:\n");
                for (uint8_t counter = 0; counter < supportedModes->firmwareSlotInfo.numberOfSlots; ++counter)
                {
                    //slot number, read only?, active slot?, next active slot?, firmware revision in that slot
                    DECLARE_ZERO_INIT_ARRAY(char, slotRevision, 14);
                    printf("\tSlot %" PRIu8, counter + 1);
                    if ((counter + 1) == 1 && supportedModes->firmwareSlotInfo.slot1ReadOnly)
                    {
                        printf(" (Read Only)");
                    }
                    if ((counter + 1) == supportedModes->firmwareSlotInfo.activeSlot)
                    {
                        printf(" (Active)");
                    }
                    if (supportedModes->firmwareSlotInfo.nextSlotToBeActivated != 0 && (counter + 1) == supportedModes->firmwareSlotInfo.nextSlotToBeActivated)
                    {
                        printf(" (Next Active)");
                    }
                    if (safe_strlen(supportedModes->firmwareSlotInfo.slotRevisionInfo[counter].revision))
                    {
                        snprintf(slotRevision, 14, "%s", supportedModes->firmwareSlotInfo.slotRevisionInfo[counter].revision);
                    }
                    else
                    {
                        snprintf(slotRevision, 14, "Not Available");
                    }
                    printf(": %s\n", slotRevision);
                }
            }
        }
        if (supportedModes->scsiInfoPossiblyIncomplete)
        {
            printf("\nWARNING: FWDL Support information may be incomplete.\n");
            printf("This can happen on old SCSI drives that don't allow\n");
            printf("write buffer \"service actions\" to be reported in the\n");
            printf("\"report supported op codes\" command.\n");
            printf("Refer to the product documentation for support information.\n\n");
        }
        printf("\n");
    }
    return;
}
