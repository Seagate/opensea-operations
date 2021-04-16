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
// \file writesame.c
// \brief This file defines the functions related to the writesame command on a drive

#include "writesame.h"

bool is_Write_Same_Supported(tDevice *device, uint64_t startingLBA, uint64_t requesedNumberOfLogicalBlocks, uint64_t *maxNumberOfLogicalBlocksPerCommand)
{
    bool supported = false;
    if (device->drive_info.drive_type == ATA_DRIVE)
    {
        //for ata check identifying info
        if (device->drive_info.IdentifyData.ata.Word206 & BIT2)
        {
            supported = true;
            //as far as I know, ATA drives don't have a limit so just return the range from the requested start and the maxLBA of the device - TJE
            if (maxNumberOfLogicalBlocksPerCommand)
            {
                *maxNumberOfLogicalBlocksPerCommand = device->drive_info.deviceMaxLba + 1;//adding plus 1 because the range is zero indexed!
            }
        }
    }
    else if (device->drive_info.drive_type == SCSI_DRIVE)
    {
        //SCSI 2 added write same 10.
        //SBC3 added max write same length to the block limits VPD page.
        //Can use report supported opcodes & MAYBE older inquiry cmdDT to try to figure out if the device definitely supports the command or not.
        //  NOTE: SPC added cmdDT. SPC3 obsoletes this for report supported operation codes
        //Can use these to get a good idea if the command is supported or not for the request.

        //If SCSI 2, just say supported and we'll check sense data for an error later.
        //Otherwise try getting the report about if it is supported from cmdDT/report supported op.
        //  If these complete and show definitive support or definitive no support, we can easily decide how to proceed.
        //  If these don't work (not supported), then try checking the block limits page to see what it says.
        //If block limits shows zeros, assume support and report support/lack of support based on trying the command.
        if (device->drive_info.scsiVersion >= SCSI_VERSION_SCSI2)//SCSI 2 or higher
        {
            bool driveReportsSupport = false;//This tracks if the report supported opcodes or cmdDT reports with absolute certainty that the drive supports the command.
            if (device->drive_info.scsiVersion >= SCSI_VERSION_SPC_3)
            {
                //for scsi ask for supported op code and look for write same 16....we don't care about the 10 byte or 32byte commands right now
                uint32_t supportLengthCheck = 20;//enough space to read all support data bytes for 16B version
                uint8_t *writeSameSupported = (uint8_t*)calloc_aligned(supportLengthCheck, sizeof(uint8_t), device->os_info.minimumAlignment);
                if (!writeSameSupported)
                {
                    perror("Error allocating memory to check write same support");
                    return false;
                }
                bool gotData = false;
                if (SUCCESS == scsi_Report_Supported_Operation_Codes(device, false, REPORT_OPERATION_CODE, WRITE_SAME_16_CMD, 0, supportLengthCheck, writeSameSupported))
                {
                    gotData = true;
                }
                if (!driveReportsSupport && SUCCESS == scsi_Report_Supported_Operation_Codes(device, false, REPORT_OPERATION_CODE, WRITE_SAME_10_CMD, 0, supportLengthCheck, writeSameSupported))
                {
                    gotData = true;
                }
                if (gotData)
                {
                    uint8_t support = M_GETBITRANGE(writeSameSupported[1], 2, 0);
                    switch (support)
                    {
                    case 3:
                        driveReportsSupport = true;
                        supported = true;
                        break;
                    default:
                        break;
                    }
                }
                safe_Free_aligned(writeSameSupported);
            }
            else if (device->drive_info.scsiVersion >= SCSI_VERSION_SPC && device->drive_info.scsiVersion < SCSI_VERSION_SPC_3)
            {
                //for scsi ask for supported op code and look for write same 16....we don't care about the 10 byte or 32byte commands right now
                uint32_t supportLengthCheck = 22;//enough space to read all support data bytes for 16B version with CmdDT
                uint8_t *writeSameSupported = (uint8_t*)calloc_aligned(supportLengthCheck, sizeof(uint8_t), device->os_info.minimumAlignment);
                if (!writeSameSupported)
                {
                    perror("Error allocating memory to check write same support");
                    return false;
                }
                bool gotData = false;
                if (SUCCESS == scsi_Inquiry(device, writeSameSupported, supportLengthCheck, WRITE_SAME_16_CMD, false, true))
                {
                    gotData = true;
                }
                if (!driveReportsSupport && SUCCESS == scsi_Inquiry(device, writeSameSupported, supportLengthCheck, WRITE_SAME_10_CMD, false, true))
                {
                    gotData = true;
                }
                if (gotData)
                {
                    uint8_t support = M_GETBITRANGE(writeSameSupported[1], 2, 0);
                    switch (support)
                    {
                    case 3:
                        driveReportsSupport = true;
                        supported = true;
                        break;
                    default:
                        break;
                    }
                }
                safe_Free_aligned(writeSameSupported);
            }
            else
            {
                //Assume supported for SCSI 2
                supported = true;
            }

            //SPC4 will have full block limits. Check for SPC2 and up since it's ambiguous about when exactly the fields we want to check may have been supported by a given drive
            if (device->drive_info.scsiVersion >= SCSI_VERSION_SPC_2 && maxNumberOfLogicalBlocksPerCommand)
            {
                //also check the block limits vpd page to see what the maximum number of logical blocks is so that we don't get in a trouble spot...(we may need chunk the write same command...ugh).
                uint8_t *blockLimits = (uint8_t*)calloc_aligned(VPD_BLOCK_LIMITS_LEN, sizeof(uint8_t), device->os_info.minimumAlignment);
                if (!blockLimits)
                {
                    perror("Error allocating memory to check block limits VPD page");
                    return false;
                }
                if (SUCCESS == scsi_Inquiry(device, blockLimits, VPD_BLOCK_LIMITS_LEN, BLOCK_LIMITS, true, false))
                {
                    uint16_t pageLength = M_BytesTo2ByteValue(blockLimits[2], blockLimits[3]);
                    if (pageLength >= 0x3C)//earlier specs, this page was shorter
                    {
                        *maxNumberOfLogicalBlocksPerCommand = M_BytesTo8ByteValue(blockLimits[36], blockLimits[37], blockLimits[38], blockLimits[39], blockLimits[40], blockLimits[41], blockLimits[42], blockLimits[43]);
                        if (*maxNumberOfLogicalBlocksPerCommand >= requesedNumberOfLogicalBlocks)
                        {
                            supported = true;
                            //TODO: Should WSNZ be checked??? removed this for now since it was not making much sense, but it may need to be added back in.
                        }
                        else if (*maxNumberOfLogicalBlocksPerCommand == 0)
                        {
                            //Device does not report a limit. This can be a backwards-compatible thing, or it could mean the device supports any length.
                            //Because of this, call it supported since we don't have any reason to otherwise think write same is not supported.
                            supported = true;
                            //TODO: also check if WSNZ is set? Because that MAY mean there is a limit, but the device doesn't say....not checking for now - TJE
                        }
                        else
                        {
                            //This case should only be hit when the requested range is larger than the device supports
                            supported = false;
                        }
                    }
                    else
                    {
                        maxNumberOfLogicalBlocksPerCommand = 0;//We don't know so set zero
                    }
                }
                safe_Free_aligned(blockLimits);
            }
            else if (maxNumberOfLogicalBlocksPerCommand)
            {
                *maxNumberOfLogicalBlocksPerCommand = 0;//setting this since we just don't know - TJE
            }
        }
    }
    return supported;
}

//we need to know where we started at and the range in order to properly calculate progress
int get_Writesame_Progress(tDevice *device, double *progress, bool *writeSameInProgress, uint64_t startingLBA, uint64_t range)
{
    int ret = SUCCESS;
    *writeSameInProgress = false;
    if (device->drive_info.drive_type == ATA_DRIVE)
    {
        //need to get status from SCT status command data
        uint8_t *sctStatusBuf = (uint8_t*)calloc_aligned(LEGACY_DRIVE_SEC_SIZE, sizeof(uint8_t), device->os_info.minimumAlignment);
        if (!sctStatusBuf)
        {
            return MEMORY_FAILURE;
        }
        ret = send_ATA_SCT_Status(device, sctStatusBuf, LEGACY_DRIVE_SEC_SIZE);
        if (ret == SUCCESS)
        {
            uint16_t sctStatus = M_BytesTo2ByteValue(sctStatusBuf[15], sctStatusBuf[14]);
            uint8_t sctState = sctStatusBuf[10];
            if (sctState == SCT_STATE_SCT_COMMAND_PROCESSING_IN_BACKGROUND || sctStatus == SCT_EXT_STATUS_SCT_COMMAND_PROCESSING_IN_BACKGROUND)//sct commmand processing in background
            {
                //check the action code says that it's a write same
                if (SCT_WRITE_SAME == M_BytesTo2ByteValue(sctStatusBuf[17], sctStatusBuf[16]))
                {
                    uint64_t currentLBA = M_BytesTo8ByteValue(sctStatusBuf[47], sctStatusBuf[46], sctStatusBuf[45], sctStatusBuf[44], sctStatusBuf[43], sctStatusBuf[42], sctStatusBuf[41], sctStatusBuf[40]);
                    *writeSameInProgress = true;
                    currentLBA -= startingLBA;
                    if (range != 0)
                    {
                        *progress = (double)((double)currentLBA / (double)range) * 100.0;
                    }
                    else
                    {
                        *progress = (double)currentLBA;
                    }
                }
            }
            else if (sctStatus == SCT_EXT_STATUS_OPERATION_WAS_TERMINATED_DUE_TO_DEVICE_SECURITY_BEING_LOCKED || sctStatus == SCT_EXT_STATUS_BACKGROUND_SCT_OPERATION_WAS_TERMINATED_BECAUSE_OF_AN_INTERRUPTING_HOST_COMMAND)
            {
                *writeSameInProgress = false;
                ret = ABORTED;
            }            
            else
            {
                *writeSameInProgress = false;
            }
        }
        safe_Free_aligned(sctStatusBuf);
    }
    /*
    else if (device->drive_info.drive_type == SCSI_DRIVE)
    {
        //request sense...hopefully we get something here....we don't, FYI. The drive doesn't report the write same progress at all. I'll leave this code in place anyways in case someone ever is interested in it, or progress reporting is added. - TJE
        //Not sure what we'll get for sense data, so we'll have to do some testing to make sure we get at least one of these
        //asc = 0x04, ascq = 0x07 - Logical Unit Not Ready, Operation In Progress
        //asc = 0x00, ascq = 0x16 - Operation In Progress
        //if the progress is reported, then we just have to return it... i think...
        uint8_t *senseData = (uint8_t*)calloc_aligned(SPC3_SENSE_LEN, sizeof(uint8_t), device->os_info.minimumAlignment);
        if (!senseData)
        {
            return MEMORY_FAILURE;
        }
        uint8_t asc = 0, ascq = 0, senseKey = 0, fru = 0;
        ret = scsi_Request_Sense_Cmd(device, false, senseData, SPC3_SENSE_LEN);//get fixed format sense data to make this easier to parse the progress from.
        get_Sense_Key_ASC_ASCQ_FRU(&senseData[0], SPC3_SENSE_LEN, &senseKey, &asc, &ascq, &fru);
        if (VERBOSITY_BUFFERS <= device->deviceVerbosity)
        {
            printf("\n\tSense Data:\n");
            print_Data_Buffer(&senseData[0], SPC3_SENSE_LEN, false);
        }
        if (ret == SUCCESS || ret == IN_PROGRESS)
        {
            *progress = ((uint16_t)senseData[16] << 8) | senseData[17];//sense key specific information
            if (asc == 0x04 && ascq == 0x07) //this is making sure that something is in progress
            {
                *writeSameInProgress = true;
            }
            else if (asc == 0x00 && ascq == 0x16)
            {
                *writeSameInProgress = true;
            }
            *progress *= 100.0;
            *progress /= 65536.0;
        }
        safe_Free(senseData);
    }
    */
    else
    {
        ret = NOT_SUPPORTED;
    }
    return ret;
}

int show_Write_Same_Current_LBA(tDevice *device)
{
    int ret = SUCCESS;
    uint64_t currentLBA = 0;
    uint16_t sctStatus = SCT_STATE_ACTIVE_WAITING_FOR_COMMAND;
    if (device->drive_info.drive_type == ATA_DRIVE)
    {
        //need to get status from SCT status command data
        uint8_t *sctStatusBuf = (uint8_t*)calloc_aligned(LEGACY_DRIVE_SEC_SIZE, sizeof(uint8_t), device->os_info.minimumAlignment);
        if (!sctStatusBuf)
        {
            return MEMORY_FAILURE;
        }
        ret = send_ATA_SCT_Status(device, sctStatusBuf, LEGACY_DRIVE_SEC_SIZE);
        if (ret == SUCCESS)
        {
            sctStatus = M_BytesTo2ByteValue(sctStatusBuf[15], sctStatusBuf[14]);
            uint8_t sctState = sctStatusBuf[10];
            if (sctState == SCT_STATE_SCT_COMMAND_PROCESSING_IN_BACKGROUND || sctStatus == SCT_EXT_STATUS_SCT_COMMAND_PROCESSING_IN_BACKGROUND)//sct commmand processing in background
            {
                //check the action code says that it's a write same
                if (SCT_WRITE_SAME == M_BytesTo2ByteValue(sctStatusBuf[17], sctStatusBuf[16]))
                {
                    currentLBA = M_BytesTo8ByteValue(sctStatusBuf[47], sctStatusBuf[46], sctStatusBuf[45], sctStatusBuf[44], sctStatusBuf[43], sctStatusBuf[42], sctStatusBuf[41], sctStatusBuf[40]);
                    ret = IN_PROGRESS;
                }
            }
            else if (sctStatus == SCT_EXT_STATUS_OPERATION_WAS_TERMINATED_DUE_TO_DEVICE_SECURITY_BEING_LOCKED || sctStatus == SCT_EXT_STATUS_BACKGROUND_SCT_OPERATION_WAS_TERMINATED_BECAUSE_OF_AN_INTERRUPTING_HOST_COMMAND)
            {
                ret = ABORTED;
            }
        }
        else
        {
            if (!is_Write_Same_Supported(device, 0, 0, NULL))
            {
                ret = NOT_SUPPORTED;
            }
        }
        safe_Free_aligned(sctStatusBuf);
    }
    else //SCSI Drive doesn't tell us
    {
        ret = NOT_SUPPORTED;
    }

    switch (ret)
    {
    case SUCCESS:
        //not in progress or completed successfully
        printf("\tA Write same is not currently in progress or has completed successfully\n");
        break;
    case IN_PROGRESS:
        //currently running. Current LBA = %llu, calculate progress with this formula:
        printf("\tA Write same is currently processing LBA %"PRIu64"\n", currentLBA);
        printf("\tTo calculate write same progress, use the following formula:\n");
        printf("\t\t( %"PRIu64" - startLBA ) / range\n", currentLBA);
        break;
    case ABORTED:
        //Write same was aborted by host or due to ata security being locked
        printf("\tA write same was aborted due to ");
        if (sctStatus == SCT_EXT_STATUS_OPERATION_WAS_TERMINATED_DUE_TO_DEVICE_SECURITY_BEING_LOCKED)
        {
            printf("device being security locked\n");
        }
        else if (sctStatus == SCT_EXT_STATUS_BACKGROUND_SCT_OPERATION_WAS_TERMINATED_BECAUSE_OF_AN_INTERRUPTING_HOST_COMMAND)
        {
            printf("interupting host command\n");
        }
        else
        {
            printf("unknown reason\n");
        }
        break;
    case NOT_SUPPORTED:
        //getting progress is not supported
        printf("\tWrite same progress not available on this device\n");
        break;
    default:
        //failed to get progress
        printf("\tAn error occured while trying to retrieve write same progress\n");
        break;
    }
    return ret;
}

int writesame(tDevice *device, uint64_t startingLba, uint64_t numberOfLogicalBlocks, bool pollForProgress, uint8_t *pattern, uint32_t patternLength)
{
    int ret = UNKNOWN;
    uint64_t maxWriteSameRange = 0;
    //first check if the device supports the write same command
    if (is_Write_Same_Supported(device, startingLba, numberOfLogicalBlocks, &maxWriteSameRange) && (maxWriteSameRange >= numberOfLogicalBlocks || maxWriteSameRange == 0))
    {
        uint32_t zeroPatternBufLen = 0;
        uint8_t *zeroPatternBuf = NULL;
        if (device->drive_info.drive_type != ATA_DRIVE && !pattern && patternLength != device->drive_info.deviceBlockSize)
        {
            //only allocate this memory for SCSI drives because they need a sector telling what to use as a pattern, whereas ATA has a feature that does not require this, and why bother sending an extra command/data transfer when it isn't neded for our application
            zeroPatternBufLen = device->drive_info.deviceBlockSize;
            zeroPatternBuf = (uint8_t*)calloc_aligned(zeroPatternBufLen, sizeof(uint8_t), device->os_info.minimumAlignment);
            if (!zeroPatternBuf)
            {
                perror("Error allocating logical sector sized buffer for zero pattern\n");
            }
            if (maxWriteSameRange == 0)
            {
                numberOfLogicalBlocks = 0;//if we are here and is_Write_Same_Supported returned true, then this value means we will write same to the whole drive
            }
        }
        //start the write same for the requested range
        if (pattern && patternLength == device->drive_info.deviceBlockSize)
        {
            ret = write_Same(device, startingLba, numberOfLogicalBlocks, pattern);//null for the pattern means we'll write a bunch of zeros
        }
        else
        {
            ret = write_Same(device, startingLba, numberOfLogicalBlocks, zeroPatternBuf);//null for the pattern means we'll write a bunch of zeros
        }
        //if the user wants us to poll for progress, then start polling
        if (pollForProgress && device->drive_info.drive_type == ATA_DRIVE)
        {
            double percentComplete = 0.0;
            bool writeSameInProgress = true;
            uint32_t delayTime = 1;
            uint64_t numberOfMebibytes = (numberOfLogicalBlocks * device->drive_info.deviceBlockSize) / 1048576;
            if (numberOfMebibytes > 180)
            {
                if (numberOfMebibytes > 180 && numberOfMebibytes < 10800)
                {
                    delayTime = 5;//once every 5 seconds
                }
                else if (numberOfMebibytes >= 10800 && numberOfMebibytes < 108000)
                {
                    delayTime = 30;//once every 30 seconds
                }
                else//change to every 10 minutes
                {
                    delayTime = 600;//once every ten minutes
                }
            }
            if (device->deviceVerbosity > VERBOSITY_QUIET)
            {
                uint8_t minutes = 0, seconds = 0;
                printf("Write same progress will be updated every");
                convert_Seconds_To_Displayable_Time(delayTime, NULL, NULL, NULL, &minutes, &seconds);
                print_Time_To_Screen(NULL, NULL, NULL, &minutes, &seconds);
                printf("\n");
            }
            while (writeSameInProgress)
            {
                double lastPercentComplete = percentComplete;
                delay_Seconds(delayTime);
                ret = get_Writesame_Progress(device, &percentComplete, &writeSameInProgress, startingLba, numberOfLogicalBlocks);
                if (SUCCESS == ret)
                {
                    if (device->deviceVerbosity > VERBOSITY_QUIET)
                    {
                        if (lastPercentComplete > 0 && writeSameInProgress == false)
                        {
                            printf("\tWrite Same progress: 100.00%%\n");
                        }
                        else
                        {
                            printf("\tWrite Same progress: %3.2f%%\n", percentComplete);
                        }
                    }
                }
                else
                {
                    break;
                }
            }
        }
        safe_Free_aligned(zeroPatternBuf);
    }
    else
    {
        ret = NOT_SUPPORTED;
    }
    return ret;
}
