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
// \file sanitize.c
// \brief This file defines the functions for sanitize operations on SCSI and ATA drives

#include "operations_Common.h"
#include "sanitize.h"
#include "platform_helper.h"

int get_ATA_Sanitize_Progress(tDevice *device, double *percentComplete, eSanitizeStatus *sanitizeStatus)
{
    int result = ata_Sanitize_Status(device, false);
    if (result == SUCCESS)
    {
        *percentComplete = M_BytesTo2ByteValue(device->drive_info.lastCommandRTFRs.lbaMid, device->drive_info.lastCommandRTFRs.lbaLow);
        if (device->drive_info.lastCommandRTFRs.secCntExt & BIT7)
        {
            *sanitizeStatus = SANITIZE_STATUS_SUCCESS;
        }
        else if (device->drive_info.lastCommandRTFRs.secCntExt & BIT6)
        {
            *sanitizeStatus = SANITIZE_STATUS_IN_PROGRESS;
        }
        else if (device->drive_info.lastCommandRTFRs.secCntExt & BIT5)
        {
            *sanitizeStatus = SANITIZE_STATUS_FROZEN;
        }
        else if (device->drive_info.lastCommandRTFRs.secCntExt & BIT4)
        {
            *sanitizeStatus = SANITIZE_STATUS_FREEZELOCK_FAILED_DUE_TO_ANTI_FREEZE_LOCK;
        }
        //NOTE: If more status bits are added to the spec, then we will need to add support for detecting them here.
        else if (M_GETBITRANGE(device->drive_info.lastCommandRTFRs.secCntExt, 4, 0) != 0)
        {
            //unknown status
            *sanitizeStatus = SANITIZE_STATUS_UNKNOWN;
        }
        else
        {
            *sanitizeStatus = SANITIZE_STATUS_NEVER_SANITIZED;
        }
    }
    else
    {
        //need to check if there was a reason reported for failing this command.
        //first check that the abort bit was set because if that isn't there, then we won't be able to identify a reason due to a SATL issue
        if (device->drive_info.lastCommandRTFRs.error & ATA_ERROR_BIT_ABORT)
        {
            //LBA Low will have the failure reason, if any
            switch (device->drive_info.lastCommandRTFRs.lbaLow)
            {
            case 0:
                *sanitizeStatus = SANITIZE_STATUS_FAILED;
                break;
            case 1:
                *sanitizeStatus = SANITIZE_STATUS_FAILED_PHYSICAL_SECTORS_REMAIN;
                break;
            case 2:
                *sanitizeStatus = SANITIZE_STATUS_UNSUPPORTED_FEATURE;
                break;
            case 3:
                *sanitizeStatus = SANITIZE_STATUS_FROZEN;
                break;
            case 4:
                *sanitizeStatus = SANITIZE_STATUS_FREEZELOCK_FAILED_DUE_TO_ANTI_FREEZE_LOCK;
                break;
            default:
                *sanitizeStatus = SANITIZE_STATUS_UNKNOWN;
                break;
            }
        }
        else
        {
            *sanitizeStatus = SANITIZE_STATUS_UNKNOWN;
        }
    }
    *percentComplete *= 100.0;
    *percentComplete /= 65536.0;
    return result;
}
#if !defined (DISABLE_NVME_PASSTHROUGH)
int get_NVMe_Sanitize_Progress(tDevice *device, double *percentComplete, eSanitizeStatus *sanitizeStatus)
{
    int result = UNKNOWN;
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
        result = SUCCESS;
        uint16_t sprog = M_BytesTo2ByteValue(sanitizeStatusLog[1], sanitizeStatusLog[0]);
        uint16_t sstat = M_BytesTo2ByteValue(sanitizeStatusLog[3], sanitizeStatusLog[2]);
        *percentComplete = sprog;

        switch(M_GETBITRANGE(sstat, 2, 0))
        {
        case 0:
            *sanitizeStatus = SANITIZE_STATUS_NEVER_SANITIZED;
            break;
        case 1:
            *sanitizeStatus = SANITIZE_STATUS_SUCCESS;
            break;
        case 2:
            *sanitizeStatus = SANITIZE_STATUS_IN_PROGRESS;
            break;
        case 3:
            *sanitizeStatus = SANITIZE_STATUS_FAILED;
            break;
        default:
            *sanitizeStatus = SANITIZE_STATUS_UNKNOWN;
            break;
        }
    }
    else
    {
        result = NOT_SUPPORTED;
    }
    *percentComplete *= 100.0;
    *percentComplete /= 65536.0;
    return result;
}
#endif

int get_SCSI_Sanitize_Progress(tDevice *device, double *percentComplete, eSanitizeStatus *sanitizeStatus)
{
    uint8_t req_sense_buf[SPC3_SENSE_LEN] = { 0 };
    uint8_t acq = 0, ascq = 0, senseKey = 0, fru = 0;
    int result = scsi_Request_Sense_Cmd(device, false, req_sense_buf, SPC3_SENSE_LEN);//get fixed format sense data to make this easier to parse the progress from.
    get_Sense_Key_ASC_ASCQ_FRU(&req_sense_buf[0], SPC3_SENSE_LEN, &senseKey, &acq, &ascq, &fru);
    result = check_Sense_Key_ASC_ASCQ_And_FRU(device, senseKey, acq, ascq, fru);
    //set this for now. It will be changed below if necessary.
    *sanitizeStatus = SANITIZE_STATUS_NOT_IN_PROGRESS;
    *percentComplete = 0;
    if (result == SUCCESS || result == IN_PROGRESS)
    {
        if (acq == 0x04 && ascq == 0x1B) //this is making sure that a sanitize command is in progress
        {
            *sanitizeStatus = SANITIZE_STATUS_IN_PROGRESS;
            *percentComplete = M_BytesTo2ByteValue(req_sense_buf[16], req_sense_buf[17]);//sense key specific information
        }
    }
    else
    {
        if (acq == 0x31 && ascq == 0x03)
        {
            //sanitize command failed
            *sanitizeStatus = SANITIZE_STATUS_FAILED;
        }
    }
    *percentComplete *= 100.0;
    *percentComplete /= 65536.0;
    return result;
}

int get_Sanitize_Progress(tDevice *device, double *percentComplete, eSanitizeStatus *sanitizeStatus)
{
    int result = UNKNOWN;
    *sanitizeStatus = 0;
    switch (device->drive_info.drive_type)
    {
    case ATA_DRIVE:
        result = get_ATA_Sanitize_Progress(device, percentComplete, sanitizeStatus);
        break;
    case NVME_DRIVE:
#if !defined (DISABLE_NVME_PASSTHROUGH)
        result = get_NVMe_Sanitize_Progress(device, percentComplete, sanitizeStatus);
        break;
#endif
    case SCSI_DRIVE:
        result = get_SCSI_Sanitize_Progress(device, percentComplete, sanitizeStatus);
    break;
    default:
        if (VERBOSITY_QUIET < device->deviceVerbosity)
        {
            printf("Not supported on this device type at this time");
        }
        return NOT_SUPPORTED;
    }
    return result;
}

int show_Sanitize_Progress(tDevice *device)
{
    int ret = UNKNOWN;
    double percentComplete = 0;
    eSanitizeStatus sanitizeInProgress = 0;

    ret = get_Sanitize_Progress(device, &percentComplete, &sanitizeInProgress);

    if (sanitizeInProgress == SANITIZE_STATUS_IN_PROGRESS)
    {
        printf("\tSanitize Progress = %3.2f%% \n", percentComplete);
    }
    else if (sanitizeInProgress == SANITIZE_STATUS_NOT_IN_PROGRESS)
    {
        printf("\tSanitize command is not currently in progress. It is either complete or has not been run.\n");
    }
    else if (sanitizeInProgress == SANITIZE_STATUS_NEVER_SANITIZED)
    {
        printf("\tThis device has never been sanitized.\n");
    } 
    else if (sanitizeInProgress == SANITIZE_STATUS_SUCCESS)
    {
        printf("\tThe last sanitize operation completed successfully\n");
    }
    else if (sanitizeInProgress == SANITIZE_STATUS_FROZEN)
    {
        printf("\tSanitize is frozen on this device. It must be power cycled to clear the freeze lock.\n");
    }
    else if (sanitizeInProgress == SANITIZE_STATUS_UNSUPPORTED_FEATURE)
    {
        printf("\tThe last sanitize command specified an unsupported sanitize mode.\n");
    }
    else if (sanitizeInProgress == SANITIZE_STATUS_FREEZELOCK_FAILED_DUE_TO_ANTI_FREEZE_LOCK)
    {
        printf("\tSanitize freezelock command failed due to anti-freezelock.\n");
    }
    else if (sanitizeInProgress == SANITIZE_STATUS_FAILED)
    {
        printf("\tSanitize command failed!\n");
    }
    else if (sanitizeInProgress == SANITIZE_STATUS_FAILED_PHYSICAL_SECTORS_REMAIN)
    {
        //ATA- Completed with physical sectors that are available to be allocated for user data that were not successfully sanitized
        printf("\tSanitize command failed: completed with physical sectors that are available for user data and were not successfully sanitized!\n");
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
    if (device->drive_info.scsiVersion >= SCSI_VERSION_SPC_3)//check for this version of SPC first since the report supported Operation codes and Sanitize command should only be on drives with this version or highter.
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
    }
    return ret;
}
#if !defined (DISABLE_NVME_PASSTHROUGH)
int get_NVMe_Sanitize_Supported_Features(tDevice *device, sanitizeFeaturesSupported *sanitizeOpts)
{
    int ret = NOT_SUPPORTED;
    if (device->drive_info.IdentifyData.nvme.ctrl.sanicap > 0)
    {
        ret = SUCCESS;
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
    eSanitizeStatus sanitizeInProgress = 0;
    bool sendExitFailureMode = false;
    //first check if a sanitize test is in progress (and that the drive isn't frozen or in a failure state)
    ret = get_Sanitize_Progress(device, &percentComplete, &sanitizeInProgress);
    if (sanitizeInProgress == SANITIZE_STATUS_IN_PROGRESS || ret == IN_PROGRESS)
    {
        return IN_PROGRESS;
    }
    else if (sanitizeInProgress == SANITIZE_STATUS_FAILED || sanitizeInProgress == SANITIZE_STATUS_FAILED_PHYSICAL_SECTORS_REMAIN)
    {
        //failure mode need to be cleared
        sendExitFailureMode = true;
    }
    else if (sanitizeInProgress == SANITIZE_STATUS_FROZEN)
    {
        //device is frozen.
        return FROZEN;
    }
    if (sendExitFailureMode)
    {
        ret = send_Sanitize_Exit_Failure_Mode(device);
        if (ret != SUCCESS)
        {
            return ret;
        }
    }

    if (patternLength > UINT16_MAX)
    {
        return BAD_PARAMETER;
    }
    os_Lock_Device(device);
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
        ret = send_Sanitize_Overwrite_Erase(device, false, false, UINT8_C(1), pattern, C_CAST(uint16_t, patternLength), false);
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
        sanitizeInProgress = SANITIZE_STATUS_IN_PROGRESS;
        while (sanitizeInProgress == SANITIZE_STATUS_IN_PROGRESS)
        {
            delay_Seconds(delayTime);
            ret = get_Sanitize_Progress(device, &percentComplete, &sanitizeInProgress);
            if (VERBOSITY_QUIET < device->deviceVerbosity)
            {
                if ((ret == SUCCESS || ret == IN_PROGRESS))
                {
                    if (sanitizeInProgress != SANITIZE_STATUS_IN_PROGRESS && percentComplete < 100)//if we get to the end, percent complete may not say 100%, so we need this condition to correct it
                    {
                        printf("\r\tSanitize Progress = 100.00%%");
                        fflush(stdout);
                    }
                    else
                    {
                        printf("\r\tSanitize Progress = %3.2f%%", percentComplete);
                        fflush(stdout);
                    }
                    if (sanitizeOperation == SANITIZE_OVERWRITE_ERASE && percentComplete >= 95 && sanitizeInProgress == SANITIZE_STATUS_IN_PROGRESS && progressUpdateChanged == false)
                    {
                        progressUpdateChanged = true;
                        delayTime = 60;//change the update time on sanitize overwrite to be once every minute when we are near the end
//                      convert_Seconds_To_Displayable_Time(delayTime, NULL, NULL, NULL, &minutes, &seconds);
//                      printf("Changing polling to every");
//                      print_Time_To_Screen(NULL, NULL, NULL, &minutes, &seconds);
//                      printf("\n");
                    }
                }
                else
                {
                    printf("\n\tError occurred while retrieving sanitize progress!");
                    break;
                }
            }
        }
        if (VERBOSITY_QUIET < device->deviceVerbosity)
        {
            printf("\n");
        }
        //TODO: Now that we have more detail on the sanitize status, especially ATA failure, do we want to show it here???
        os_Update_File_System_Cache(device);
    }
    os_Unlock_Device(device);
    return ret;
}
