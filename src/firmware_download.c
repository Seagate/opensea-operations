//
// Do NOT modify or remove this copyright and license
//
// Copyright (c) 2012 - 2018 Seagate Technology LLC and/or its Affiliates, All Rights Reserved
//
// This software is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
//
// ******************************************************************************************
// 
// \file firmware_download.c
// \brief This file defines the function for performing a firmware download to a drive

#include "operations_Common.h"
#include "firmware_download.h"
#include "logs.h"
#include "common_platform.h"

//int firmware_Download(tDevice *device, bool useDMA, eDownloadMode dlMode, uint16_t segmentSize, uint8_t *firmwareFileMem, uint32_t firmwareMemoryLength)

int firmware_Download(tDevice *device, firmwareUpdateData * options)
{
    int ret = SUCCESS;
#ifdef _DEBUG
    printf("--> %s\n",__FUNCTION__);
#endif
    if (options->dlMode == DL_FW_ACTIVATE)
    {
        if (device->drive_info.drive_type == NVME_DRIVE && options->existingFirmwareImage && options->firmwareSlot == 0)
        {
            //cannot activate slot 0 for an existing image. Value should be between 1 and 7
            //Activating with slot 0 is only allowed for letting the controller choose an image for replacing after sending it to the drive. Not applicable for switching slots
            return NOT_SUPPORTED;
        }
        ret = firmware_Download_Command(device, DL_FW_ACTIVATE, 0, 0, options->firmwareFileMem, options->firmwareSlot, options->existingFirmwareImage);
        options->activateFWTime = options->avgSegmentDlTime = device->drive_info.lastCommandTimeNanoSeconds;
#if defined (_WIN32) && WINVER >= SEA_WIN32_WINNT_WIN10
        if (device->drive_info.drive_type != NVME_DRIVE && ret == OS_PASSTHROUGH_FAILURE && device->os_info.fwdlIOsupport.fwdlIOSupported && device->os_info.last_error == ERROR_INVALID_FUNCTION)
        {
            //This means that we encountered a driver that is not allowing us to issue the Win10 API Firmware activate call for some unknown reason. 
            //This doesn't happen with Microsoft's AHCI driver though...
            //Instead, we should disable the use of the API and retry with passthrough to perform the activation. This is not preferred at all. 
            //We want to use the Win10 API whenever possible so the system is ready for the changes to the bus and drive information so that it is less likely to BSOD like we used to see in older versions of Windows.
            device->os_info.fwdlIOsupport.fwdlIOSupported = false;
            ret = firmware_Download_Command(device, DL_FW_ACTIVATE, 0, 0, options->firmwareFileMem, options->firmwareSlot, options->existingFirmwareImage);
            options->activateFWTime = options->avgSegmentDlTime = device->drive_info.lastCommandTimeNanoSeconds;
            device->os_info.fwdlIOsupport.fwdlIOSupported = true;
        }
#endif
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
    if (options->dlMode == DL_FW_FULL || options->dlMode == DL_FW_TEMP)
    {
        //single command to do the whole download
        ret = firmware_Download_Command(device, options->dlMode, 0, options->firmwareMemoryLength, options->firmwareFileMem, options->bufferID, false);
        options->activateFWTime = options->avgSegmentDlTime = device->drive_info.lastCommandTimeNanoSeconds;
    }
    else
    {
        eDownloadMode specifiedDLMode = options->dlMode;
        if (device->drive_info.drive_type == NVME_DRIVE)
        {
            //switch to deferred and we'll send the activate at the end
            options->dlMode = DL_FW_DEFERRED;
        }
        //multiple commands needed to do the download (segmented)
        if (options->segmentSize == 0)
        {
            options->segmentSize = 64;
        }
        uint32_t downloadSize = options->segmentSize * LEGACY_DRIVE_SEC_SIZE;
        uint32_t downloadBlocks = options->firmwareMemoryLength / downloadSize;
        uint32_t downloadRemainder = options->firmwareMemoryLength % downloadSize;
        uint32_t downloadOffset = 0;
        uint32_t currentDownloadBlock = 0;

#if defined (_WIN32) && defined(WINVER)
#if WINVER >= SEA_WIN32_WINNT_WIN10
        //saving this for later since we may need to turn it off...
        bool deviceSupportsWinAPI = device->os_info.fwdlIOsupport.fwdlIOSupported;
        if (device->drive_info.drive_type != NVME_DRIVE && deviceSupportsWinAPI && options->dlMode == DL_FW_DEFERRED)
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
#endif
#endif

        //start the download
        for (currentDownloadBlock = 0; currentDownloadBlock < downloadBlocks; currentDownloadBlock++, downloadOffset += downloadSize)
        {
#if defined (_WIN32) && defined(WINVER)
#if WINVER >= SEA_WIN32_WINNT_WIN10
            if (currentDownloadBlock + 1 == downloadBlocks && downloadRemainder == 0)
            {
                device->os_info.fwdlIOsupport.isLastSegmentOfDownload = true;
            }
            else
            {
                device->os_info.fwdlIOsupport.isLastSegmentOfDownload = false;
            }
            if (currentDownloadBlock == 0)
            {
                device->os_info.fwdlIOsupport.isFirstSegmentOfDownload = true;
            }
            else
            {
                device->os_info.fwdlIOsupport.isFirstSegmentOfDownload = false;
            }
#endif
#endif
            ret = firmware_Download_Command(device, options->dlMode, downloadOffset, downloadSize, &options->firmwareFileMem[downloadOffset], options->bufferID, false);
            options->avgSegmentDlTime += device->drive_info.lastCommandTimeNanoSeconds;

#if defined(DISABLE_NVME_PASSTHROUGH)//Remove it later if someone wants to. -X
            if (currentDownloadBlock % 20 == 0)
#endif
            {
                if (device->deviceVerbosity > VERBOSITY_QUIET)
                {
                    printf(".");
                    fflush(stdout);
                }
            }
            if (ret != SUCCESS)
            {
                break;
            }
        }

        if (!downloadRemainder)
        {
            options->activateFWTime = device->drive_info.lastCommandTimeNanoSeconds;
        }

        //check to make sure we haven't had a failure yet
        if (ret != SUCCESS)
        {
            if (options->dlMode == DL_FW_SEGMENTED && downloadRemainder == 0 && (currentDownloadBlock + 1) == downloadBlocks)
            {
                //this means that we had an error on the last sector, which is a drive bug in old products.
                //Check that we don't have RTFRs from the last command and that the sense data does not say "unaligned write command"
                //We may need to expand this check if we encounter this problem in other OS's or on other kinds of controllers (currently this is from a motherboard)
                if (device->drive_info.drive_type == ATA_DRIVE && device->drive_info.lastCommandRTFRs.status == 0 && device->drive_info.lastCommandRTFRs.error == 0)
                {
                    uint8_t senseKey = 0, asc = 0, ascq = 0, fru = 0;
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
            if (device->drive_info.drive_type != NVME_DRIVE && device->os_info.fwdlIOsupport.fwdlIOSupported && options->dlMode == DL_FW_DEFERRED)//checking to see if Windows says the FWDL API is supported
            {
                //device->os_info.fwdlIOsupport.isFirstSegmentOfDownload = false;
                device->os_info.fwdlIOsupport.isLastSegmentOfDownload = true;
                //ret = firmware_Download_Command(device, options->dlMode, options->useDMA, downloadOffset, downloadRemainder, &options->firmwareFileMem[downloadOffset]);
                if (downloadRemainder < device->os_info.fwdlIOsupport.maxXferSize && (downloadRemainder % device->os_info.fwdlIOsupport.payloadAlignment == 0))
                {
                    //we're fine, just issue the command
                    ret = firmware_Download_Command(device, options->dlMode, downloadOffset, downloadRemainder, &options->firmwareFileMem[downloadOffset], options->bufferID, false);
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
                device->os_info.fwdlIOsupport.isLastSegmentOfDownload = true;//set anyways, just in case.
                ret = firmware_Download_Command(device, options->dlMode, downloadOffset, downloadRemainder, &options->firmwareFileMem[downloadOffset], options->bufferID, false);
            }
            //device->os_info.fwdlIOsupport.isFirstSegmentOfDownload = false;
            device->os_info.fwdlIOsupport.isLastSegmentOfDownload = false;
#else
            //not windows 10 API, so just issue the command
            ret = firmware_Download_Command(device, options->dlMode, downloadOffset, downloadRemainder, &options->firmwareFileMem[downloadOffset], options->bufferID, false);
#endif
#else
            //not windows 10 API, so just issue the command
            ret = firmware_Download_Command(device, options->dlMode, downloadOffset, downloadRemainder, &options->firmwareFileMem[downloadOffset], options->bufferID, false);
#endif
            if (device->deviceVerbosity > VERBOSITY_QUIET)
            {
                printf(".");
                fflush(stdout);
            }
            if (ret != SUCCESS)
            {
                if (options->dlMode == DL_FW_SEGMENTED)
                {
                    //this means that we had an error on the last sector, which is a drive bug in old products.
                    //Check that we don't have RTFRs from the last command and that the sense data does not say "unaligned write command"
                    //We may need to expand this check if we encounter this problem in other OS's or on other kinds of controllers (currently this is from a motherboard)
                    if (device->drive_info.drive_type == ATA_DRIVE && device->drive_info.lastCommandRTFRs.status == 0 && device->drive_info.lastCommandRTFRs.error == 0)
                    {
                        uint8_t senseKey = 0, asc = 0, ascq = 0, fru = 0;
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
        if (specifiedDLMode != options->dlMode && specifiedDLMode == DL_FW_SEGMENTED && device->drive_info.drive_type == NVME_DRIVE)
        {
            //send an activate command (not an existing slot, this is a new image activation)
            ret = firmware_Download_Command(device, DL_FW_ACTIVATE, 0, 0, options->firmwareFileMem, options->firmwareSlot, false);
            options->activateFWTime = options->avgSegmentDlTime = device->drive_info.lastCommandTimeNanoSeconds;
        }

        if (device->deviceVerbosity > VERBOSITY_QUIET)
        {
            printf("\n");
        }
        options->avgSegmentDlTime /= (currentDownloadBlock + 1);
#if defined (_WIN32) && defined(WINVER)
#if WINVER >= SEA_WIN32_WINNT_WIN10
        //restore this value back to what it was (if it was ever even changed)
        device->os_info.fwdlIOsupport.fwdlIOSupported = deviceSupportsWinAPI;
#endif
#endif
    }

#ifdef _DEBUG
    printf("<-- %s (%d)\n",__FUNCTION__, ret);
#endif
    return ret;
}

int get_Supported_FWDL_Modes(tDevice *device, ptrSupportedDLModes supportedModes)
{
    int ret = SUCCESS;
    if (supportedModes)
    {
        switch (device->drive_info.drive_type)
        {
        case ATA_DRIVE:
        {
            //first check the bits in the identify data
            if (device->drive_info.IdentifyData.ata.Word069 & BIT8)
            {
                supportedModes->downloadMicrocodeSupported = true;
                supportedModes->firmwareDownloadDMACommandSupported = true;
                supportedModes->fullBuffer = true;
                supportedModes->driveOffsetBoundaryInBytes = LEGACY_DRIVE_SEC_SIZE;
                supportedModes->driveOffsetBoundary = 9;
            }
            if (device->drive_info.IdentifyData.ata.Word083 & BIT0 || device->drive_info.IdentifyData.ata.Word086 & BIT0)
            {
                supportedModes->downloadMicrocodeSupported = true;
                supportedModes->fullBuffer = true;
            }
            if (device->drive_info.IdentifyData.ata.Word119 & BIT4 || device->drive_info.IdentifyData.ata.Word120 & BIT4)
            {
                supportedModes->segmented = true;
            }
            supportedModes->maxSegmentSize = M_BytesTo2ByteValue(M_Byte1(device->drive_info.IdentifyData.ata.Word235), M_Byte0(device->drive_info.IdentifyData.ata.Word235));
            if (supportedModes->downloadMicrocodeSupported && !(supportedModes->maxSegmentSize > 0 && supportedModes->maxSegmentSize < 0xFFFF))
            {
                supportedModes->maxSegmentSize = UINT32_MAX;
            }
            supportedModes->minSegmentSize = M_BytesTo2ByteValue(M_Byte1(device->drive_info.IdentifyData.ata.Word234), M_Byte0(device->drive_info.IdentifyData.ata.Word234));
            if (supportedModes->downloadMicrocodeSupported && !(supportedModes->minSegmentSize > 0 && supportedModes->minSegmentSize < 0xFFFF))
            {
                supportedModes->minSegmentSize = 0;
            }
            if (is_Seagate_Family(device) == SEAGATE && device->drive_info.IdentifyData.ata.Word243 & BIT12)
            {
                supportedModes->seagateDeferredPowerCycleActivate = true;
                supportedModes->deferredPowerCycleActivationSupported = true;
            }
            //now try reading the supportd capabilities page of the identify device data log for the remaining info (deferred download)
            uint8_t supportedCapabilities[LEGACY_DRIVE_SEC_SIZE] = { 0 };
            if (SUCCESS == send_ATA_Read_Log_Ext_Cmd(device, ATA_LOG_IDENTIFY_DEVICE_DATA, ATA_ID_DATA_LOG_SUPPORTED_CAPABILITIES, supportedCapabilities, LEGACY_DRIVE_SEC_SIZE, 0))
            {
                uint64_t supportedCapabilitiesQword = M_BytesTo8ByteValue(supportedCapabilities[15], supportedCapabilities[14], supportedCapabilities[13], supportedCapabilities[12], supportedCapabilities[11], supportedCapabilities[10], supportedCapabilities[9], supportedCapabilities[8]);
                uint64_t dlMicrocodeBits = M_BytesTo8ByteValue(supportedCapabilities[23], supportedCapabilities[22], supportedCapabilities[21], supportedCapabilities[20], supportedCapabilities[19], supportedCapabilities[18], supportedCapabilities[17], supportedCapabilities[16]);
                //this bit should always be set to 1, but doesn't hurt to check
                if (supportedCapabilitiesQword & BIT63)
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
                if (dlMicrocodeBits & BIT63)
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
#if !defined(DISABLE_NVME_PASSTHROUGH)
            if (device->drive_info.IdentifyData.nvme.ctrl.oacs & BIT2)
            {
                supportedModes->downloadMicrocodeSupported = true;
                supportedModes->deferred = true;
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
                    uint32_t counter = 0;
                    uint32_t updateGranularityD = updateGranularity;
                    while (updateGranularityD != 0)
                    {
                        updateGranularityD = updateGranularityD >> 1;
                        ++counter;
                    }
                    supportedModes->driveOffsetBoundary = counter - 1;
                    supportedModes->minSegmentSize = supportedModes->driveOffsetBoundaryInBytes / LEGACY_DRIVE_SEC_SIZE;
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
                        supportedModes->driveOffsetBoundary = counter - 1;
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
                uint8_t firmwareLog[512] = { 0 };
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
#else
            //running in SCSI translation mode, so only set full & deferred download modes
            supportedModes->downloadMicrocodeSupported = true;
            supportedModes->fullBuffer = true;
            supportedModes->deferred = true;
            supportedModes->minSegmentSize = 0;
            supportedModes->maxSegmentSize = UINT32_MAX;
            //need to set the offset requirement...for now I'm setting the minimum the NVMe spec says can be reported...should be OK...-TJE
            supportedModes->driveOffsetBoundaryInBytes = 4096;//4Kb is the minimum specified in the NVMe specification that the drive may conform to..this should be good enough for the translation.
            supportedModes->driveOffsetBoundary = 12;//power of 2
#endif
            break;
        case SCSI_DRIVE:
        {
            uint8_t *writeBufferSupportData = (uint8_t*)calloc(14, sizeof(uint8_t));
            //first try asking for supported operation code for Full Buffer download
            if (SUCCESS == scsi_Report_Supported_Operation_Codes(device, false, REPORT_OPERATION_CODE_AND_SERVICE_ACTION, WRITE_BUFFER_CMD, SCSI_WB_DL_MICROCODE_SAVE_ACTIVATE, 14, writeBufferSupportData))
            {
                switch (writeBufferSupportData[1] & 0x07)
                {
                case 0: //not available right now...so not supported
                case 1://not supported
                    break;
                case 3://supported according to spec
                case 5://supported in vendor specific mannor in same format as case 3
                    supportedModes->downloadMicrocodeSupported = true;
                    supportedModes->fullBuffer = true;
                    break;
                default:
                    break;
                }
                //if this worked, then we know we can ask about other supported operation codes.
                if (SUCCESS == scsi_Report_Supported_Operation_Codes(device, false, REPORT_OPERATION_CODE_AND_SERVICE_ACTION, WRITE_BUFFER_CMD, SCSI_WB_DL_MICROCODE_OFFSETS_SAVE_ACTIVATE, 14, writeBufferSupportData))
                {
                    switch (writeBufferSupportData[1] & 0x07)
                    {
                    case 0: //not available right now...so not supported
                    case 1://not supported
                        break;
                    case 3://supported according to spec
                    case 5://supported in vendor specific mannor in same format as case 3
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
                    case 0: //not available right now...so not supported
                    case 1://not supported
                        break;
                    case 3://supported according to spec
                    case 5://supported in vendor specific mannor in same format as case 3
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
                    case 5://supported in vendor specific mannor in same format as case 3
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
                //read the ext inquiry data for supported deferred activation events
                uint8_t extInquiryData[VPD_EXTENDED_INQUIRY_LEN] = { 0 };
                if (SUCCESS == scsi_Inquiry(device, extInquiryData, VPD_EXTENDED_INQUIRY_LEN, EXTENDED_INQUIRY_DATA, true, false))
                {
                    if (extInquiryData[12] & BIT7)
                    {
                        supportedModes->deferredPowerCycleActivationSupported = true;
                    }
                    if (extInquiryData[12] & BIT6)
                    {
                        supportedModes->deferredHardResetActivationSupported = true;
                    }
                    if (extInquiryData[12] & BIT5)
                    {
                        supportedModes->deferredVendorSpecificActivationSupported = true;
                    }
                    supportedModes->codeActivation = M_GETBITRANGE(extInquiryData[4], 7, 6);
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
                    case 0: //not available right now...so not supported
                    case 1://not supported
                        break;
                    case 3://supported according to spec
                    case 5://supported in vendor specific mannor in same format as case 3
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
                else
                {
                    uint8_t *temp = (uint8_t*)realloc(writeBufferSupportData, 16);
                    if (!temp)
                    {
                        return MEMORY_FAILURE;
                    }
                    writeBufferSupportData = temp;
                    //if that still doesn't work, we can try the obsolete method using the inquiry command
                    if (SUCCESS == scsi_Inquiry(device, writeBufferSupportData, 16, WRITE_BUFFER_CMD, false, true))
                    {
                        supportedModes->scsiInfoPossiblyIncomplete = true;
                        switch (writeBufferSupportData[1] & 0x07)
                        {
                        case 0: //not available right now...so not supported
                        case 1://not supported
                            break;
                        case 3://supported according to spec
                        case 5://supported in vendor specific mannor in same format as case 3
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
            }
            safe_Free(writeBufferSupportData);

            uint8_t offsetReq[4] = { 0 };
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
            if (family == SEAGATE || family == SEAGATE_VENDOR_A)
            {
                uint8_t * c3VPD = calloc(255 * sizeof(uint8_t), sizeof(uint8_t));
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
                safe_Free(c3VPD);
            }
            //Old method to check support is below...new method above should be as good if not better.
            /*
            //need to try setting this based on the response from the request supported operations command...this may not be perfect and may not work on old drives.
            uint8_t supportedCommands[1024] = { 0 };
            if (SUCCESS == scsi_Report_Supported_Operation_Codes(device, false, REPORT_ALL, 0, 0, 1024, supportedCommands))
            {
                //loop through the data and check for the commands and service actions we are interested in.
                uint16_t supportedCmdsIter = 4;
                uint16_t cmdDescriptorLength = 8;
                uint32_t supportedCmdsLength = M_BytesTo4ByteValue(supportedCommands[0], supportedCommands[1], supportedCommands[2], supportedCommands[3]);
                for (; supportedCmdsIter < M_Min(1024, supportedCmdsLength); supportedCmdsIter += cmdDescriptorLength)
                {
                    uint8_t operationCode = supportedCommands[supportedCmdsIter];
                    uint16_t serviceAction = M_BytesTo2ByteValue(supportedCommands[supportedCmdsIter + 2], supportedCommands[supportedCmdsIter + 3]);
                    bool serviceActionValid = supportedCommands[supportedCmdsIter + 5] & BIT0 ? true : false;
                    cmdDescriptorLength = supportedCommands[supportedCmdsIter + 5] & BIT1 ? 20 : 8;
                    switch (operationCode)
                    {
                    case WRITE_BUFFER_CMD:
                        if (serviceActionValid)
                        {
                            switch (serviceAction)
                            {
                            case SCSI_WB_DL_MICROCODE_TEMP_ACTIVATE:
                            case SCSI_WB_DL_MICROCODE_SAVE_ACTIVATE:
                                supportedModes->downloadMicrocodeSupported = true;
                                supportedModes->fullBuffer = true;
                                break;
                            case SCSI_WB_DL_MICROCODE_OFFSETS_ACTIVATE:
                            case SCSI_WB_DL_MICROCODE_OFFSETS_SAVE_ACTIVATE:
                                supportedModes->downloadMicrocodeSupported = true;
                                supportedModes->segmented = true;
                                //TODO: Check the bitfield of the command to see if it says whether or there is a minumum or maximum transfer size.
                                supportedModes->maxSegmentSize = 0xFFFF;
                                supportedModes->minSegmentSize = 0;
                                break;
                            case SCSI_WB_DL_MICROCODE_OFFSETS_SAVE_SELECT_ACTIVATE_DEFER:
                            case SCSI_WB_DL_MICROCODE_OFFSETS_SAVE_DEFER:
                                supportedModes->downloadMicrocodeSupported = true;
                                supportedModes->deferred = true;
                                break;
                            case SCSI_WB_ACTIVATE_DEFERRED_MICROCODE:
                            default:
                                break;
                            }
                        }
                        else
                        {
                            supportedModes->downloadMicrocodeSupported = true;
                            supportedModes->scsiInfoPossiblyIncomplete = true;
                            //TODO: check the bitfield after requesting information about the write buffer command specifically?
                            //setting segmented and full buffer download modes in here because they SHOULD work on the products we care about supporting.
                            supportedModes->fullBuffer = true;
                            supportedModes->segmented = true;
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
            */
        }
        break;
        default:
            ret = NOT_SUPPORTED;
            break;
        }
        //set the recommended download mode
        if (supportedModes->downloadMicrocodeSupported)
        {
            //start low and work up to most recommended
            supportedModes->recommendedDownloadMode = DL_FW_FULL;
            if (supportedModes->segmented)
            {
                supportedModes->recommendedDownloadMode = DL_FW_SEGMENTED;
            }
            if (supportedModes->deferred)
            {
                supportedModes->recommendedDownloadMode = DL_FW_DEFERRED;
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
    if (supportedModes && device)
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
                printf("Maximum Segment Size (512B Blocks): %" PRIu16 "\n", supportedModes->maxSegmentSize);
            }
            if (supportedModes->minSegmentSize == 0)
            {
                printf("Minimum Segment Size (512B Blocks): No minimum\n");
            }
            else
            {
                printf("Minimum Segment Size (512B Blocks): %" PRIu16 "\n", supportedModes->minSegmentSize);
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
            if (supportedModes->firmwareSlotInfo.firmwareSlotInfoValid)
            {
                printf("Firmware Slot Info:\n");
                for (uint8_t counter = 0; counter < supportedModes->firmwareSlotInfo.numberOfSlots; ++counter)
                {
                    //slot number, read only?, active slot?, next active slot?, firmware revision in that slot
                    char slotRevision[14] = { 0 };
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
                    if (strlen(supportedModes->firmwareSlotInfo.slotRevisionInfo[counter].revision))
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