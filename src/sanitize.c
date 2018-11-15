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
// \file sanitize.c
// \brief This file defines the functions for sanitize operations on SCSI and ATA drives

#include "operations_Common.h"
#include "sanitize.h"

int get_Sanitize_Progress(tDevice *device, double *percentComplete, bool *sanitizeInProgress)
{
    int result = UNKNOWN;
    *sanitizeInProgress = false;
    switch (device->drive_info.drive_type)
    {
    case ATA_DRIVE:
        result = ata_Sanitize_Status(device, false);
        if (result == SUCCESS)
        {
            *percentComplete = device->drive_info.lastCommandRTFRs.lbaLow | ((uint16_t)device->drive_info.lastCommandRTFRs.lbaMid << 8);
            if (!(device->drive_info.lastCommandRTFRs.lbaLow == 0xFF && device->drive_info.lastCommandRTFRs.lbaMid == 0xFF) || device->drive_info.lastCommandRTFRs.secCntExt & BIT6)
            {
                *sanitizeInProgress = true;
            }
            else if (device->drive_info.lastCommandRTFRs.secCntExt & BIT7)//check this second in case there was a warning about not getting all the rtfrs
            {
                *sanitizeInProgress = false;//this means it is complete
            }
        }
        break;
    case NVME_DRIVE:
#if !defined (DISABLE_NVME_PASSTHROUGH)
    {
        //read the sanitize status log
        uint8_t sanitizeStatusLog[512] = { 0 };
        nvmeGetLogPageCmdOpts getLogOpts;
        memset(&getLogOpts, 0, sizeof(nvmeGetLogPageCmdOpts));
        getLogOpts.dataLen = 512;
        getLogOpts.lid = 0x81;
        getLogOpts.addr = sanitizeStatusLog;
        //TODO: Set namespace ID?
        if (SUCCESS == nvme_Get_Log_Page(device, &getLogOpts))
        {
            uint16_t sprog = M_BytesTo2ByteValue(sanitizeStatusLog[1], sanitizeStatusLog[0]);
            uint16_t sstat = M_BytesTo2ByteValue(sanitizeStatusLog[3], sanitizeStatusLog[2]);
            if (sstat == 2)
            {
                *sanitizeInProgress = true;
                *percentComplete = sprog;
            }
        }
    }
        break;
#endif
    case SCSI_DRIVE:
    {
        uint8_t req_sense_buf[SPC3_SENSE_LEN] = { 0 };
        uint8_t acq = 0, ascq = 0, senseKey = 0, fru = 0;
        result = scsi_Request_Sense_Cmd(device, false, req_sense_buf, SPC3_SENSE_LEN);//get fixed format sense data to make this easier to parse the progress from.
        get_Sense_Key_ASC_ASCQ_FRU(&req_sense_buf[0], SPC3_SENSE_LEN, &senseKey, &acq, &ascq, &fru);
        result = check_Sense_Key_ASC_ASCQ_And_FRU(device, senseKey, acq, ascq, fru);
        if (VERBOSITY_BUFFERS <= device->deviceVerbosity)
        {
            printf("\n\tSense Data:\n");
            print_Data_Buffer(&req_sense_buf[0], SPC3_SENSE_LEN, false);
        }
        if (result == SUCCESS || result == IN_PROGRESS)
        {
            if (acq == 0x04 && ascq == 0x1B) //this is making sure that a sanitize command is in progress
            {
                *sanitizeInProgress = true;
                *percentComplete = ((uint16_t)req_sense_buf[16] << 8) | req_sense_buf[17];//sense key specific information
            }
        }
    }
    break;
    default:
        if (VERBOSITY_QUIET < device->deviceVerbosity)
        {
            printf("Not supported on this device type at this time");
        }
        return NOT_SUPPORTED;
        break;
    }
    *percentComplete *= 100.0;
    *percentComplete /= 65536.0;
    return result;
}

int show_Sanitize_Progress(tDevice *device)
{
    int ret = UNKNOWN;
    double percentComplete = 0;
    bool sanitizeInProgress = false;

    ret = get_Sanitize_Progress(device, &percentComplete, &sanitizeInProgress);

    if ((ret == SUCCESS || ret == IN_PROGRESS) && sanitizeInProgress == true)
    {
        printf("\tSanitize Progress = %3.2f%% \n", percentComplete);
    }
    else if (ret == SUCCESS && sanitizeInProgress == false)
    {
        printf("\tSanitize command is not currently in progress. It is either complete or has not been run.\n");
    }
    else
    {
        printf("\tError occurred while retrieving sanitize progress!\n");
    }
    return ret;
}

int get_ATA_Sanitize_Device_Features(tDevice *device, sanitizeFeaturesSupported *sanitizeOptions)
{
    int ret = FAILURE;
    uint16_t *word_ptr = (uint16_t *)&device->drive_info.IdentifyData.ata.Word000;
    if (device->drive_info.IdentifyData.ata.Word255 == 0)
    {
        ret = ata_Identify(device, (uint8_t *)&device->drive_info.IdentifyData.ata.Word000, LEGACY_DRIVE_SEC_SIZE);
    }
    else
    {
        ret = SUCCESS;
    }
    if (ret == SUCCESS)
    {
        if (word_ptr[ATA_IDENTIFY_SANITIZE_INDEX] & ATA_IDENTIFY_SANITIZE_SUPPORTED)
        {
            sanitizeOptions->sanitizeCmdEnabled = true;
            sanitizeOptions->exitFailMode = true;
            if (word_ptr[ATA_IDENTIFY_SANITIZE_INDEX] & ATA_IDENTIFY_CRYPTO_SUPPORTED)
            {
                sanitizeOptions->crypto = true;
            }
            if (word_ptr[ATA_IDENTIFY_SANITIZE_INDEX] & ATA_IDENTIFY_OVERWRITE_SUPPORTED)
            {
                sanitizeOptions->overwrite = true;
            }
            if (word_ptr[ATA_IDENTIFY_SANITIZE_INDEX] & ATA_IDENTIFY_BLOCK_ERASE_SUPPORTED)
            {
                sanitizeOptions->blockErase = true;
            }
        }
    }
    else
    {
        #if defined (_DEBUG)
        printf("ATA IDENTIFY Failed %d\n", ret);
        #endif
    }

    return ret;
}

int get_SCSI_Sanitize_Supported_Features(tDevice *device, sanitizeFeaturesSupported *sanitizeOpts)
{
    int                         ret = NOT_SUPPORTED;
    if (device->drive_info.scsiVersion >= 5)//check for this version of SPC first since the report supported Operation codes and Sanitize command should only be on drives with this version or highter.
    {
        uint8_t supportedCommands[14] = { 0 };
        ret = FAILURE;
        if (SUCCESS == scsi_Report_Supported_Operation_Codes(device, false, REPORT_OPERATION_CODE_AND_SERVICE_ACTION, SANITIZE_CMD, SCSI_SANITIZE_OVERWRITE, 14, supportedCommands))
        {
            ret = SUCCESS;
            switch (supportedCommands[1] & 0x07)
            {
            case 0: //not available right now...so not supported
            case 1://not supported
                break;
            case 3://supported according to spec
            case 5://supported in vendor specific mannor in same format as case 3
                sanitizeOpts->sanitizeCmdEnabled = true;
                sanitizeOpts->overwrite = true;
                break;
            default:
                break;
            }
        }
        else if (device->drive_info.interface_type != SCSI_INTERFACE && device->drive_info.interface_type != IDE_INTERFACE) //TODO: add other interfaces here to filter out when we send a TUR
        {
            //Send a test unit ready to clear the error from failure to read this page. This is done mostly for USB interfaces that don't handle errors from commands well.
            scsi_Test_Unit_Ready(device, NULL);
        }
        if (SUCCESS == scsi_Report_Supported_Operation_Codes(device, false, REPORT_OPERATION_CODE_AND_SERVICE_ACTION, SANITIZE_CMD, SCSI_SANITIZE_BLOCK_ERASE, 14, supportedCommands))
        {
            ret = SUCCESS;
            switch (supportedCommands[1] & 0x07)
            {
            case 0: //not available right now...so not supported
            case 1://not supported
                break;
            case 3://supported according to spec
            case 5://supported in vendor specific mannor in same format as case 3
                sanitizeOpts->sanitizeCmdEnabled = true;
                sanitizeOpts->blockErase = true;
                break;
            default:
                break;
            }
        }
        else if (device->drive_info.interface_type != SCSI_INTERFACE && device->drive_info.interface_type != IDE_INTERFACE) //TODO: add other interfaces here to filter out when we send a TUR
        {
            //Send a test unit ready to clear the error from failure to read this page. This is done mostly for USB interfaces that don't handle errors from commands well.
            scsi_Test_Unit_Ready(device, NULL);
        }
        if (SUCCESS == scsi_Report_Supported_Operation_Codes(device, false, REPORT_OPERATION_CODE_AND_SERVICE_ACTION, SANITIZE_CMD, SCSI_SANITIZE_CRYPTOGRAPHIC_ERASE, 14, supportedCommands))
        {
            ret = SUCCESS;
            switch (supportedCommands[1] & 0x07)
            {
            case 0: //not available right now...so not supported
            case 1://not supported
                break;
            case 3://supported according to spec
            case 5://supported in vendor specific mannor in same format as case 3
                sanitizeOpts->sanitizeCmdEnabled = true;
                sanitizeOpts->crypto = true;
                break;
            default:
                break;
            }
        }
        else if (device->drive_info.interface_type != SCSI_INTERFACE && device->drive_info.interface_type != IDE_INTERFACE) //TODO: add other interfaces here to filter out when we send a TUR
        {
            //Send a test unit ready to clear the error from failure to read this page. This is done mostly for USB interfaces that don't handle errors from commands well.
            scsi_Test_Unit_Ready(device, NULL);
        }
        if (SUCCESS == scsi_Report_Supported_Operation_Codes(device, false, REPORT_OPERATION_CODE_AND_SERVICE_ACTION, SANITIZE_CMD, SCSI_SANITIZE_EXIT_FAILURE_MODE, 14, supportedCommands))
        {
            ret = SUCCESS;
            switch (supportedCommands[1] & 0x07)
            {
            case 0: //not available right now...so not supported
            case 1://not supported
                break;
            case 3://supported according to spec
            case 5://supported in vendor specific mannor in same format as case 3
                sanitizeOpts->sanitizeCmdEnabled = true;
                sanitizeOpts->exitFailMode = true;
                break;
            default:
                break;
            }
        }
        else if (device->drive_info.interface_type != SCSI_INTERFACE && device->drive_info.interface_type != IDE_INTERFACE) //TODO: add other interfaces here to filter out when we send a TUR
        {
            //Send a test unit ready to clear the error from failure to read this page. This is done mostly for USB interfaces that don't handle errors from commands well.
            scsi_Test_Unit_Ready(device, NULL);
        }
    }
    return ret;
}
#if !defined (DISABLE_NVME_PASSTHROUGH)
int get_NVMe_Sanitize_Supported_Features(tDevice *device, sanitizeFeaturesSupported *sanitizeOpts)
{
    int ret = NOT_SUPPORTED;
    if (device->drive_info.IdentifyData.nvme.ctrl.sanicap > 0)
    {
        sanitizeOpts->sanitizeCmdEnabled = true;
        sanitizeOpts->exitFailMode = true;
        //Cntl identify bytes 331:328
        //BIT0 = crypto
        //bit1 = block erase
        //bit 2 = overwrite
        if (device->drive_info.IdentifyData.nvme.ctrl.sanicap & BIT0)
        {
            sanitizeOpts->crypto = true;
        }
        if (device->drive_info.IdentifyData.nvme.ctrl.sanicap & BIT1)
        {
            sanitizeOpts->blockErase = true;
        }
        if (device->drive_info.IdentifyData.nvme.ctrl.sanicap & BIT2)
        {
            sanitizeOpts->overwrite = true;
        }
    }
    return ret;
}
#endif

int get_Sanitize_Device_Features(tDevice *device, sanitizeFeaturesSupported *opts)
{
    int ret = UNKNOWN;
    switch (device->drive_info.drive_type)
    {
    case NVME_DRIVE:
#if !defined (DISABLE_NVME_PASSTHROUGH)
        ret = get_NVMe_Sanitize_Supported_Features(device, opts);
        break;
#endif
    case SCSI_DRIVE:
        ret = get_SCSI_Sanitize_Supported_Features(device, opts);
        break;
    case ATA_DRIVE:
        ret = get_ATA_Sanitize_Device_Features(device, opts);
        break;
    default:
        ret = NOT_SUPPORTED;
        break;
    }
    return ret;
}

int run_Sanitize_Operation(tDevice *device, eSanitizeOperations sanitizeOperation, bool pollForProgress, uint8_t *pattern, uint32_t patternLength)
{
    int ret = UNKNOWN;
    uint32_t delayTime = 1;
    double percentComplete = 0;
    bool sanitizeInProgress = false;
    //first check if a sanitize test is in progress (and that the drive isn't frozen or in a failure state)
    ret = get_Sanitize_Progress(device, &percentComplete, &sanitizeInProgress);
    if (sanitizeInProgress == true || ret == IN_PROGRESS)
    {
        return IN_PROGRESS;
    }
    if (ret != SUCCESS)
    {
        bool sendExitFailureMode = false;
        //need to check if the drive has a sanitize failure bit set (and frozen bit set on ATA drives)
        switch (device->drive_info.drive_type)
        {
        case ATA_DRIVE:
            if (device->drive_info.lastCommandRTFRs.lbaLow == 0x01)
            {
                //failure mode need to be cleared
                sendExitFailureMode = true;
            }
            else if (device->drive_info.lastCommandRTFRs.lbaLow == 0x03)
            {
                //device is frozen.
                return FROZEN;
            }
            break;
        case NVME_DRIVE:
#if !defined (DISABLE_NVME_PASSTHROUGH)
        {
            //read the sanitize status log
            uint8_t sanitizeStatusLog[512] = { 0 };
            nvmeGetLogPageCmdOpts getLogOpts;
            memset(&getLogOpts, 0, sizeof(nvmeGetLogPageCmdOpts));
            getLogOpts.dataLen = 512;
            getLogOpts.lid = 0x81;
            getLogOpts.addr = sanitizeStatusLog;
            //TODO: Set namespace ID?
            if (SUCCESS == nvme_Get_Log_Page(device, &getLogOpts))
            {
                uint16_t sstat = M_BytesTo2ByteValue(sanitizeStatusLog[3], sanitizeStatusLog[2]);
                if (sstat == 3)
                {
                    sendExitFailureMode = true;
                }
            }
        }
        break;
#endif
        case SCSI_DRIVE:
        {
            uint8_t asc = 0, ascq = 0, senseKey = 0, fru = 0;
            get_Sense_Key_ASC_ASCQ_FRU(device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, &senseKey, &asc, &ascq, &fru);
            if (asc == 0x31 && ascq == 0x03)
            {
                //failure mode needs to be cleared
                sendExitFailureMode = true;
            }
            else//don't care what the error was, but return an error here anyways
            {
                return ret;
            }
        }
        break;
        default:
            return NOT_SUPPORTED;
            break;
        }
        if (sendExitFailureMode)
        {
            ret = send_Sanitize_Exit_Failure_Mode(device);
            if (ret != SUCCESS)
            {
                return ret;
            }
        }
    }

    //start the sanitize operation requested
    switch (sanitizeOperation)
    {
    case SANITIZE_BLOCK_ERASE:
        ret = send_Sanitize_Block_Erase(device, false, false);
        delayTime = 1;
        break;
    case SANITIZE_CRYPTO_ERASE:
        ret = send_Sanitize_Crypto_Erase(device, false, false);
        delayTime = 1;
        break;
    case SANITIZE_OVERWRITE_ERASE:
        ret = send_Sanitize_Overwrite_Erase(device, false, false, 1, pattern, patternLength, false);
        delayTime = 600;//this is 10 minute delay between progress updates
        break;
    case SANTIZIE_FREEZE_LOCK:
        if (device->drive_info.drive_type == ATA_DRIVE)
        {
            ret = ata_Sanitize_Freeze_Lock(device);
            if (ret != SUCCESS)
            {
                if (device->drive_info.lastCommandRTFRs.lbaLow == 0x02)
                {
                    ret = NOT_SUPPORTED;
                }
            }
        }
        else
        {
            ret = NOT_SUPPORTED;
        }
        return ret;
    case SANITIZE_ANTI_FREEZE_LOCK:
        if (device->drive_info.drive_type == ATA_DRIVE)
        {
            ret = ata_Sanitize_Anti_Freeze_Lock(device);
            if (ret != SUCCESS)
            {
                if (device->drive_info.lastCommandRTFRs.lbaLow == 0x02)
                {
                    ret = NOT_SUPPORTED;
                }
            }
        }
        else
        {
            ret = NOT_SUPPORTED;
        }
        return ret;
    default:
        return NOT_SUPPORTED;
    }

    if (pollForProgress && ret == SUCCESS)
    {
        uint8_t minutes = 0, seconds = 0;
        bool progressUpdateChanged = false;
        convert_Seconds_To_Displayable_Time(delayTime, NULL, NULL, NULL, &minutes, &seconds);
        printf("Sanitize progress will be updated every");
        print_Time_To_Screen(NULL, NULL, NULL, &minutes, &seconds);
        printf("\n");
        sanitizeInProgress = true;
        while (sanitizeInProgress)
        {
            delay_Seconds(delayTime);
            ret = get_Sanitize_Progress(device, &percentComplete, &sanitizeInProgress);
            if (VERBOSITY_QUIET < device->deviceVerbosity)
            {
                if ((ret == SUCCESS || ret == IN_PROGRESS))
                {
                    if (sanitizeInProgress == false && percentComplete < 100.0)//if we get to the end, percent complete may not say 100%, so we need this condition to correct it
                    {
                        printf("\tSanitize Progress = 100.0%% \n");
                    }
                    else
                    {
                        printf("\tSanitize Progress = %3.2f%% \n", percentComplete);
                    }
                    if (sanitizeOperation == SANITIZE_OVERWRITE_ERASE && percentComplete >= 95 && sanitizeInProgress == true && progressUpdateChanged == false)
                    {
                        progressUpdateChanged = true;
                        delayTime = 60;//change the update time on sanitize overwrite to be once every minute when we are near the end
                        convert_Seconds_To_Displayable_Time(delayTime, NULL, NULL, NULL, &minutes, &seconds);
                        printf("Changing polling to every");
                        print_Time_To_Screen(NULL, NULL, NULL, &minutes, &seconds);
                        printf("\n");
                    }
                }
                else
                {
                    printf("\tError occurred while retrieving sanitize progress!\n");
                    break;
                }
            }
        }
    }
    return ret;
}