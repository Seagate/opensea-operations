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
// \file sanitize.c
// \brief This file defines the functions for sanitize operations on SCSI and ATA drives

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
#include "time_utils.h"
#include "sleep.h"

#include "operations_Common.h"
#include "sanitize.h"
#include "platform_helper.h"

static eReturnValues get_ATA_Sanitize_Progress(tDevice *device, double *percentComplete, eSanitizeStatus *sanitizeStatus)
{
    eReturnValues result = ata_Sanitize_Status(device, false);
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

static eReturnValues get_NVMe_Sanitize_Progress(tDevice *device, double *percentComplete, eSanitizeStatus *sanitizeStatus)
{
    eReturnValues result = UNKNOWN;
    //read the sanitize status log
    DECLARE_ZERO_INIT_ARRAY(uint8_t, sanitizeStatusLog, 512);
    nvmeGetLogPageCmdOpts getLogOpts;
    memset(&getLogOpts, 0, sizeof(nvmeGetLogPageCmdOpts));
    getLogOpts.dataLen = 512;
    getLogOpts.lid = 0x81;
    getLogOpts.addr = sanitizeStatusLog;
    if (SUCCESS == nvme_Get_Log_Page(device, &getLogOpts))
    {
        result = SUCCESS;
        uint16_t sprog = M_BytesTo2ByteValue(sanitizeStatusLog[1], sanitizeStatusLog[0]);
        uint16_t sstat = M_BytesTo2ByteValue(sanitizeStatusLog[3], sanitizeStatusLog[2]);
        *percentComplete = sprog;

        switch (M_GETBITRANGE(sstat, 2, 0))
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

static eReturnValues get_SCSI_Sanitize_Progress(tDevice *device, double *percentComplete, eSanitizeStatus *sanitizeStatus)
{
    DECLARE_ZERO_INIT_ARRAY(uint8_t, req_sense_buf, SPC3_SENSE_LEN);
    uint8_t acq = 0;
    uint8_t ascq = 0;
    uint8_t senseKey = 0;
    uint8_t fru = 0;
    eReturnValues result = scsi_Request_Sense_Cmd(device, false, req_sense_buf, SPC3_SENSE_LEN);//get fixed format sense data to make this easier to parse the progress from.
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

eReturnValues get_Sanitize_Progress(tDevice *device, double *percentComplete, eSanitizeStatus *sanitizeStatus)
{
    eReturnValues result = UNKNOWN;
    *sanitizeStatus = 0;
    switch (device->drive_info.drive_type)
    {
    case ATA_DRIVE:
        result = get_ATA_Sanitize_Progress(device, percentComplete, sanitizeStatus);
        break;
    case NVME_DRIVE:
        result = get_NVMe_Sanitize_Progress(device, percentComplete, sanitizeStatus);
        break;
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

eReturnValues show_Sanitize_Progress(tDevice *device)
{
    eReturnValues ret = UNKNOWN;
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

eReturnValues get_ATA_Sanitize_Device_Features(tDevice *device, sanitizeFeaturesSupported *sanitizeOptions)
{
    eReturnValues ret = FAILURE;
    if (device->drive_info.IdentifyData.ata.Word255 == 0)
    {
        ret = ata_Identify(device, C_CAST(uint8_t *, &device->drive_info.IdentifyData.ata.Word000), LEGACY_DRIVE_SEC_SIZE);
    }
    else
    {
        ret = SUCCESS;
    }
    if (ret == SUCCESS)
    {
        if (is_ATA_Identify_Word_Valid(device->drive_info.IdentifyData.ata.Word059) && device->drive_info.IdentifyData.ata.Word059 & ATA_IDENTIFY_SANITIZE_SUPPORTED)
        {
            sanitizeOptions->sanitizeCmdEnabled = true;
            sanitizeOptions->exitFailMode = true;
            sanitizeOptions->freezelock = true;
            if (device->drive_info.IdentifyData.ata.Word059 & ATA_IDENTIFY_CRYPTO_SUPPORTED)
            {
                sanitizeOptions->crypto = true;
            }
            if (device->drive_info.IdentifyData.ata.Word059 & ATA_IDENTIFY_OVERWRITE_SUPPORTED)
            {
                sanitizeOptions->overwrite = true;
                sanitizeOptions->maximumOverwritePasses = 16;
            }
            if (device->drive_info.IdentifyData.ata.Word059 & ATA_IDENTIFY_BLOCK_ERASE_SUPPORTED)
            {
                sanitizeOptions->blockErase = true;
            }
            //bit 10 = antifreeze-lock command supported
            //ata identify device data log also contains "Definitive ending pattern supported" bit on capabilities page
            //ata identify device data log sets copies of word 59 on security page.
            //another bit describes commands allowed matching ACS-2 or not. Modern drives should set this to one to match current standards. ACS-2 may set this to zero
            if (device->drive_info.IdentifyData.ata.Word059 & BIT10)
            {
                sanitizeOptions->antiFreezeLock = true;
            }
            sanitizeOptions->writeAfterCryptoErase = WAEREQ_NOT_SPECIFIED;//or WAEREQ_READ_COMPLETES_GOOD_STATUS???
            sanitizeOptions->writeAfterBlockErase = WAEREQ_NOT_SPECIFIED;//or WAEREQ_READ_COMPLETES_GOOD_STATUS???

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

eReturnValues get_SCSI_Sanitize_Supported_Features(tDevice *device, sanitizeFeaturesSupported *sanitizeOpts)
{
    eReturnValues ret = NOT_SUPPORTED;
    if (device->drive_info.scsiVersion >= SCSI_VERSION_SPC_3)//check for this version of SPC first since the report supported Operation codes and Sanitize command should only be on drives with this version or highter.
    {
        if (device->drive_info.passThroughHacks.scsiHacks.noReportSupportedOperations)
        {
            return NOT_SUPPORTED;
        }
        DECLARE_ZERO_INIT_ARRAY(uint8_t, supportedCommands, 14);
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
                sanitizeOpts->definitiveEndingPattern = true;//only setting this in this case since the "vendor specific mannor" may be that definitive ending pattern is not possible.-TJE
                M_FALLTHROUGH;
            case 5://supported in vendor specific mannor in same format as case 3
                sanitizeOpts->sanitizeCmdEnabled = true;
                sanitizeOpts->overwrite = true;
                if (strstr("ATA", device->drive_info.T10_vendor_ident) || strstr("NVMe", device->drive_info.T10_vendor_ident))
                {
                    //Assuming that only a compliant translator will support this, so screening for ATA and NVMe which are limited to 16 passes
                    sanitizeOpts->maximumOverwritePasses = 16;
                }
                else
                {
                    sanitizeOpts->maximumOverwritePasses = 31;
                }
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
        writeAfterErase writeAfterEraseRequirements;
        memset(&writeAfterEraseRequirements, 0, sizeof(writeAfterErase));
        if (SUCCESS == is_Write_After_Erase_Required(device, &writeAfterEraseRequirements))
        {
            sanitizeOpts->writeAfterBlockErase = writeAfterEraseRequirements.blockErase;
            sanitizeOpts->writeAfterCryptoErase = writeAfterEraseRequirements.cryptoErase;
        }
        if (sanitizeOpts->sanitizeCmdEnabled)
        {
            sanitizeOpts->antiFreezeLock = false;
            sanitizeOpts->freezelock = false;
        }
    }
    return ret;
}

eReturnValues get_NVMe_Sanitize_Supported_Features(tDevice *device, sanitizeFeaturesSupported *sanitizeOpts)
{
    eReturnValues ret = NOT_SUPPORTED;
    if (device->drive_info.IdentifyData.nvme.ctrl.sanicap > 0)
    {
        ret = SUCCESS;
        sanitizeOpts->sanitizeCmdEnabled = true;
        sanitizeOpts->exitFailMode = true;
        sanitizeOpts->antiFreezeLock = false;
        sanitizeOpts->freezelock = false;
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
            sanitizeOpts->definitiveEndingPattern = true;
        }
        if (device->drive_info.IdentifyData.nvme.ctrl.sanicap & BIT29)
        {
            sanitizeOpts->noDeallocateInhibited = true;

        }
        sanitizeOpts->nodmmas = C_CAST(noDeallocateModifiesAfterSanitize, M_GETBITRANGE(device->drive_info.IdentifyData.nvme.ctrl.sanicap, 31, 30));
        sanitizeOpts->writeAfterCryptoErase = WAEREQ_NOT_SPECIFIED;//or WAEREQ_READ_COMPLETES_GOOD_STATUS???
        sanitizeOpts->writeAfterBlockErase = WAEREQ_NOT_SPECIFIED;//or WAEREQ_READ_COMPLETES_GOOD_STATUS???
        if (sanitizeOpts->noDeallocateInhibited)
        {
            //get the sanitize config feature status to know which mode it is operating in.
            nvmeFeaturesCmdOpt feat;
            memset(&feat, 0, sizeof(nvmeFeaturesCmdOpt));
            feat.fid = NVME_FEAT_SANITIZE_CONFIG_;
            feat.nsid = NVME_ALL_NAMESPACES;
            feat.sel = NVME_CURRENT_FEAT_SEL;
            feat.dataPtr = M_NULLPTR;
            feat.dataLength = 0;
            //reported in completion dword 0
            if (SUCCESS == nvme_Get_Features(device, &feat))
            {
                if (device->drive_info.lastNVMeResult.lastNVMeCommandSpecific & BIT0)
                {
                    sanitizeOpts->responseMode = NO_DEALLOC_RESPONSE_WARNING;
                }
                else
                {
                    sanitizeOpts->responseMode = NO_DEALLOC_RESPONSE_ERROR;
                }
            }
            else 
            {
                sanitizeOpts->responseMode = NO_DEALLOC_RESPONSE_INV;
            }
        }
    }
    return ret;
}

eReturnValues get_Sanitize_Device_Features(tDevice *device, sanitizeFeaturesSupported *opts)
{
    eReturnValues ret = UNKNOWN;
    switch (device->drive_info.drive_type)
    {
    case NVME_DRIVE:
        ret = get_NVMe_Sanitize_Supported_Features(device, opts);
        break;
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

eReturnValues sanitize_Freezelock(tDevice* device)
{
    eReturnValues ret = NOT_SUPPORTED;
    if (device->drive_info.drive_type == ATA_DRIVE)
    {
        ret = ata_Sanitize_Freeze_Lock(device);
        if (ret != SUCCESS)
        {
            if (device->drive_info.lastCommandRTFRs.lbaLow == 0x00)
            {
                ret = FAILURE;
            }
            else if (device->drive_info.lastCommandRTFRs.lbaLow == 0x02)
            {
                ret = NOT_SUPPORTED;
            }
            else if (device->drive_info.lastCommandRTFRs.lbaLow == 0x03)
            {
                ret = FROZEN;
            }
            else if (device->drive_info.lastCommandRTFRs.lbaLow == 0x04)
            {
                ret = ABORTED;
            }
            else
            {
                ret = FAILURE;
            }
        }
    }
    return ret;
}

eReturnValues sanitize_Anti_Freezelock(tDevice* device)
{
    eReturnValues ret = NOT_SUPPORTED;
    if (device->drive_info.drive_type == ATA_DRIVE)
    {
        ret = ata_Sanitize_Anti_Freeze_Lock(device);
        if (ret != SUCCESS)
        {
            if (device->drive_info.lastCommandRTFRs.lbaLow == 0x00)
            {
                ret = FAILURE;
            }
            else if (device->drive_info.lastCommandRTFRs.lbaLow == 0x02)
            {
                ret = NOT_SUPPORTED;
            }
            else if (device->drive_info.lastCommandRTFRs.lbaLow == 0x03)
            {
                ret = FROZEN;
            }
            else if (device->drive_info.lastCommandRTFRs.lbaLow == 0x04)
            {
                ret = ABORTED;
            }
            else
            {
                ret = FAILURE;
            }
        }
    }
    return ret;
}

eReturnValues run_Sanitize_Operation(tDevice* device, eSanitizeOperations sanitizeOperation, bool pollForProgress, uint8_t* pattern, uint32_t patternLength)
{
    //convert to calling new functions since this one is obsolete.
    sanitizeOperationOptions sanitizeOptions;
    memset(&sanitizeOptions, 0, sizeof(sanitizeOperationOptions));
    sanitizeOptions.version = SANITIZE_OPERATION_OPTIONS_VERSION;
    sanitizeOptions.size = sizeof(sanitizeOperationOptions);
    sanitizeOptions.commonOptions.allowUnrestrictedSanitizeExit = false;
    sanitizeOptions.commonOptions.zoneNoReset = false;
    sanitizeOptions.commonOptions.noDeallocate = false;
    sanitizeOptions.pollForProgress = pollForProgress;
    switch (sanitizeOperation)
    {
    case SANITIZE_BLOCK_ERASE:
        sanitizeOptions.sanitizeEraseOperation = BLOCK_ERASE;
        break;
    case SANITIZE_CRYPTO_ERASE:
        sanitizeOptions.sanitizeEraseOperation = CRYPTO_ERASE;
        break;
    case SANITIZE_OVERWRITE_ERASE:
        sanitizeOptions.sanitizeEraseOperation = OVERWRITE_ERASE;
        sanitizeOptions.overwriteOptions.invertPatternBetweenPasses = false;
        sanitizeOptions.overwriteOptions.numberOfPasses = UINT8_C(1);
        if (pattern)
        {
            memcpy(&sanitizeOptions.overwriteOptions.pattern, pattern, M_Min(patternLength, sizeof(uint32_t)));
        }
        else
        {
            sanitizeOptions.overwriteOptions.pattern = 0;
        }
        break;
    case SANTIZIE_FREEZE_LOCK:
        return sanitize_Freezelock(device);
    case SANITIZE_ANTI_FREEZE_LOCK:
        return sanitize_Anti_Freezelock(device);
    default:
        return NOT_SUPPORTED;
    }
    return run_Sanitize_Operation2(device, sanitizeOptions);
}

static eReturnValues sanitize_Poll_For_Progress(tDevice* device, uint32_t delayTime)
{
    eReturnValues ret = IN_PROGRESS;
    uint8_t minutes = 0;
    uint8_t seconds = 0;
    double percentComplete = 0;
    convert_Seconds_To_Displayable_Time(delayTime, M_NULLPTR, M_NULLPTR, M_NULLPTR, &minutes, &seconds);
    printf("Sanitize progress will be updated every");
    print_Time_To_Screen(M_NULLPTR, M_NULLPTR, M_NULLPTR, &minutes, &seconds);
    printf("\n");
    eSanitizeStatus sanitizeInProgress = SANITIZE_STATUS_IN_PROGRESS;
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
            }
        }
        if (ret != SUCCESS && ret != IN_PROGRESS)
        {
            if (VERBOSITY_QUIET < device->deviceVerbosity)
            {
                printf("\n\tError occurred while retrieving sanitize progress!");
            }
            break;
        }
    }
    if (VERBOSITY_QUIET < device->deviceVerbosity)
    {
        printf("\n");
    }
    os_Update_File_System_Cache(device);
    return ret;
}

//Sanitize API V1 structs/enums/defs
//These must be preserved to offer backwards compatibility if this changes in the future -TJE

#define SANITIZE_OPERATION_OPTIONS_VERSION_V1 (1)
typedef struct _sanitizeOperationOptions_V1
{
    size_t size;//sizeof(sanitizeOperationOptions)
    uint32_t version;//SANITIZE_OPERATION_OPTIONS_VERSION
    eSanitizeErase sanitizeEraseOperation;
    bool pollForProgress;//crypto, block, and overwrite erases
    struct
    {
        bool allowUnrestrictedSanitizeExit;
        bool zoneNoReset;//zoned devices only.
        bool noDeallocate;//NVMe only today. May not be supported by a controller.
    }commonOptions; //options that apply to all Sanitize erase's
    struct
    {
        bool invertPatternBetweenPasses;//SATA note: Some drives may or may not set a definitive ending pattern upon completion. By default, this function will set the definitive ending pattern bit whenever possible-TJE
        uint8_t numberOfPasses;//0 = BAD_PARAMETER, 1 = 1, 2 = 2, etc. NVMe and SATA max at 16. SCSI maxes at 32
        uint32_t pattern;
    }overwriteOptions; //overwrite unique options
}sanitizeOperationOptions_V1;

eReturnValues run_Sanitize_Operation2(tDevice* device, sanitizeOperationOptions sanitizeOptions)
{
    eReturnValues ret = UNKNOWN;
    if (sanitizeOptions.version >= SANITIZE_OPERATION_OPTIONS_VERSION_V1 && sanitizeOptions.size >= sizeof(sanitizeOperationOptions_V1))
    {
        //NOTE: If sanitizeoptions changes version, need to adjust code to offer backwards compatibility when possible.-TJE
        //      If new options are added, set to backwards compatible defaults.
        uint32_t delayTime = 1;
        double percentComplete = 0.0;
        eSanitizeStatus sanitizeInProgress = 0;
        bool sendExitFailureMode = false;
        //first check if a sanitize test is in progress (and that the drive isn't frozen or in a failure state)
        ret = get_Sanitize_Progress(device, &percentComplete, &sanitizeInProgress);
        if (sanitizeInProgress == SANITIZE_STATUS_IN_PROGRESS || ret == IN_PROGRESS)
        {
            if (sanitizeOptions.pollForProgress)
            {
                return sanitize_Poll_For_Progress(device, delayTime);
            }
            else
            {
                return IN_PROGRESS;
            }
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
                ret = get_Sanitize_Progress(device, &percentComplete, &sanitizeInProgress);
                if (sanitizeInProgress == SANITIZE_STATUS_FAILED || sanitizeInProgress == SANITIZE_STATUS_FAILED_PHYSICAL_SECTORS_REMAIN)
                {
                    //need to run the sanitize command in restricted exit mode, so you can only get it to run by reissuing the command in restricted mode again.
                    return DEVICE_ACCESS_DENIED;//use this to inform the user that you cannot use AUSE and need to retry sanitize without that field.
                }
            }
        }

        os_Lock_Device(device);
        os_Unmount_File_Systems_On_Device(device);
        //start the sanitize operation requested
        switch (sanitizeOptions.sanitizeEraseOperation)
        {
        case BLOCK_ERASE:
            ret = send_Sanitize_Block_Erase(device, sanitizeOptions.commonOptions.allowUnrestrictedSanitizeExit, sanitizeOptions.commonOptions.zoneNoReset);
            delayTime = 1;
            break;
        case CRYPTO_ERASE:
            ret = send_Sanitize_Crypto_Erase(device, sanitizeOptions.commonOptions.allowUnrestrictedSanitizeExit, sanitizeOptions.commonOptions.zoneNoReset);
            delayTime = 1;
            break;
        case OVERWRITE_ERASE:
            if (sanitizeOptions.overwriteOptions.numberOfPasses == 0)
            {
                return BAD_PARAMETER;
            }
            if (sanitizeOptions.overwriteOptions.numberOfPasses >= 16)//16 is max number of passes for NVMe/SATA
            {
                if (device->drive_info.drive_type != SCSI_DRIVE)
                {
                    if (sanitizeOptions.overwriteOptions.numberOfPasses > 16)
                    {
                        return BAD_PARAMETER;
                    }
                    else
                    {
                        //change to zero to do all 16 passes in NVMe and SATA
                        sanitizeOptions.overwriteOptions.numberOfPasses = 0;
                    }
                }
                else
                {
                    if (sanitizeOptions.overwriteOptions.numberOfPasses > 31)//31 passes is the maximum in SCSI
                    {
                        return BAD_PARAMETER;
                    }
                    //In SCSI, a number of passes == 0 is reserved, so do not use this-TJE
                }
            }
            ret = send_Sanitize_Overwrite_Erase(device, sanitizeOptions.commonOptions.allowUnrestrictedSanitizeExit, sanitizeOptions.overwriteOptions.invertPatternBetweenPasses, sanitizeOptions.overwriteOptions.numberOfPasses, C_CAST(uint8_t*, &sanitizeOptions.overwriteOptions.pattern), sizeof(uint32_t), sanitizeOptions.commonOptions.zoneNoReset);
            delayTime = 600;//this is 10 minute delay between progress updates
            break;
        }

        if (sanitizeOptions.pollForProgress && ret == SUCCESS)
        {
            ret = sanitize_Poll_For_Progress(device, delayTime);
        }
        os_Unlock_Device(device);
    }
    else
    {
        ret = BAD_PARAMETER;
    }
    return ret;
}
