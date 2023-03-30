//
// Do NOT modify or remove this copyright and license
//
// Copyright (c) 2012-2023 Seagate Technology LLC and/or its Affiliates, All Rights Reserved
//
// This software is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
//
// ******************************************************************************************
// 
// \file dst.c
// \brief This file defines the function calls for dst and dst related operations

#include "operations_Common.h"
#include "dst.h"
#include "sector_repair.h"
#include "smart.h"
#include "logs.h"
#include "cmds.h"
#include <stdlib.h>
#include "platform_helper.h"

int ata_Abort_DST(tDevice *device)
{
    int result = UNKNOWN;
    result = ata_SMART_Offline(device, 0x7F, 15);
    return result;
}
int scsi_Abort_DST(tDevice *device)
{
    int     result = UNKNOWN;
    result = scsi_Send_Diagnostic(device, 4, 0, 0, 0, 0, 0, NULL, 0, 15);
    return result;
}
int nvme_Abort_DST(tDevice *device, uint32_t nsid)
{
    int result = UNKNOWN;
    result = nvme_Device_Self_Test(device, nsid, 0x0F);
    return result;
}
int abort_DST(tDevice *device)
{
    int result = UNKNOWN;
    switch (device->drive_info.drive_type)
    {
    case NVME_DRIVE:
        result = nvme_Abort_DST(device, UINT32_MAX);//TODO: Need to handle whether we are testing all namespaces or a specific namespace ID!
        break;
    case SCSI_DRIVE:
        result = scsi_Abort_DST(device);
        break;
    case ATA_DRIVE:
        result = ata_Abort_DST(device);
        break;
    default:
        result = NOT_SUPPORTED;
        break;
    }
    return result;
}
int ata_Get_DST_Progress(tDevice *device, uint32_t *percentComplete, uint8_t *status)
{
    int     result = UNKNOWN;
    uint8_t temp_buf[512] = { 0 };
    result = ata_SMART_Read_Data(device, temp_buf, sizeof(temp_buf));
    if (result == SUCCESS)
    {
        //get the progress
        *status = temp_buf[363];
        *percentComplete = M_Nibble0(*status) * 10;
        *status = M_Nibble1(*status);
    }
    return result;
}
int scsi_Get_DST_Progress(tDevice *device, uint32_t *percentComplete, uint8_t *status)
{
    //04h 09h LOGICAL UNIT NOT READY, SELF-TEST IN PROGRESS
    int     result = UNKNOWN;
    uint8_t *temp_buf = C_CAST(uint8_t*, calloc_aligned(LEGACY_DRIVE_SEC_SIZE, sizeof(uint8_t), device->os_info.minimumAlignment));
    if (temp_buf == NULL)
    {
        perror("Calloc Failure!\n");
        return MEMORY_FAILURE;
    }
    result = scsi_Log_Sense_Cmd(device, false, LPC_CUMULATIVE_VALUES, LP_SELF_TEST_RESULTS, 0, 0, temp_buf, LP_SELF_TEST_RESULTS_LEN);
    if (result == SUCCESS)
    {
        *status = temp_buf[8];
        *status &= 0x0F;
        //check the progress since the test is still running
        memset(temp_buf, 0, LEGACY_DRIVE_SEC_SIZE);
        *(volatile uint8_t*)temp_buf = *(volatile uint8_t*)temp_buf;//force GCC not to optimize this out...it makes the output confusing.
        scsi_Request_Sense_Cmd(device, false, temp_buf, LEGACY_DRIVE_SEC_SIZE);
        *percentComplete = M_BytesTo2ByteValue(temp_buf[16], temp_buf[17]);
        *percentComplete *= 100;
        *percentComplete /= 65536;
    }
    safe_Free_aligned(temp_buf)
    return result;
}
int nvme_Get_DST_Progress(tDevice *device, uint32_t *percentComplete, uint8_t *status)
{
    int result = UNKNOWN;
    uint8_t nvmeSelfTestLog[564] = { 0 };//strange size for the log, but it's what I see in the spec - TJE
    nvmeGetLogPageCmdOpts getDSTLog;
    memset(&getDSTLog, 0, sizeof(nvmeGetLogPageCmdOpts));
    getDSTLog.addr = nvmeSelfTestLog;
    getDSTLog.dataLen = 564;
    getDSTLog.lid = 0x06;
    if (SUCCESS == nvme_Get_Log_Page(device, &getDSTLog))
    {
        result = SUCCESS;
        if (nvmeSelfTestLog[0] == 0)
        {
            //no self test in progress
            *percentComplete = 0;
            //need to set a status value based on the most recent result data
            uint32_t newestResultOffset = 4;
            *status = M_Nibble0(nvmeSelfTestLog[newestResultOffset + 0]);//This should be fine for the rest of the running DST code.
            //According to spec, if status bit is 0x0F, that means entry is not valid(doesn't cantain valid test results.
            //and in that case, we have to ignore this bit, and consider this as completed/success.
            //I have seen this issue on the drive, where DST was never run. - Nidhi
            if (*status == 0x0F)
            {
                *status = 0;
            }
        }
        else
        {
            //NVMe made this simple...if this is 25, then it's 25% complete. no silly business
            *percentComplete = nvmeSelfTestLog[1];
            //Setting the status to Fh to work with existing SCSI/ATA code in the run_DST_Function - TJE
            *status = 0x0F;
        }
    }
    else
    {
        result = FAILURE;
    }
    return result;
}

int get_DST_Progress(tDevice *device, uint32_t *percentComplete, uint8_t *status)
{
    int      result = UNKNOWN;
    *percentComplete = 0;
    *status = 0xFF;
    switch (device->drive_info.drive_type)
    {
    case ATA_DRIVE:
        result = ata_Get_DST_Progress(device, percentComplete, status);
        *percentComplete = 100 - *percentComplete; //make this match SCSI
        break;
    case NVME_DRIVE:
        result = nvme_Get_DST_Progress(device, percentComplete, status);
        break;
    case SCSI_DRIVE:
        result = scsi_Get_DST_Progress(device, percentComplete, status);
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

//TODO: Status codes in NVMe are slightly different! Need a bool to pass in to say when it's from NVMe)
void translate_DST_Status_To_String(uint8_t status, char *translatedString, bool justRanDST, bool isNVMeDrive)
{
    if (!translatedString)
    {
        return;
    }
    if (isNVMeDrive)
    {
        switch (status)
        {
        case 0x00:
            snprintf(translatedString, MAX_DST_STATUS_STRING_LENGTH, "Operation completed without error.");
            break;
        case 0x01:
            snprintf(translatedString, MAX_DST_STATUS_STRING_LENGTH, "Operation was aborted by a Device Self-test command.");
            break;
        case 0x02:
            snprintf(translatedString, MAX_DST_STATUS_STRING_LENGTH, "Operation was aborted by a Controller Level Reset.");
            break;
        case 0x03:
            snprintf(translatedString, MAX_DST_STATUS_STRING_LENGTH, "Operation was aborted due to a removal of a namespace from the namespace inventory.");
            break;
        case 0x04:
            snprintf(translatedString, MAX_DST_STATUS_STRING_LENGTH, "Operation was aborted due to the processing of a Format NVM command.");
            break;
        case 0x05:
            snprintf(translatedString, MAX_DST_STATUS_STRING_LENGTH, "A fatal error or unknown test error occurred while the controller was executing the device self-test operation and the operation did not complete.");
            break;
        case 0x06:
            snprintf(translatedString, MAX_DST_STATUS_STRING_LENGTH, "Operation completed with a segment that failed and the segment that failed is not known.");
            break;
        case 0x07:
            snprintf(translatedString, MAX_DST_STATUS_STRING_LENGTH, "Operation completed with one or more failed segments and the first segment that failed is indicated in the Segment Number field.");
            break;
        case 0x08:
            snprintf(translatedString, MAX_DST_STATUS_STRING_LENGTH, "Operation was aborted for unknown reason.");
            break;
        case 0x0F://NOTE: The spec says that this is NOT used. We are dummying this up to work with existing SAS/SATA code which is why this is here - TJE
            snprintf(translatedString, MAX_DST_STATUS_STRING_LENGTH, "Operation in progress.");
            break;
        default:
            snprintf(translatedString, MAX_DST_STATUS_STRING_LENGTH, "Error, unknown status: %" PRIX8 "h.", status);
            break;
        }
    }
    else
    {
        switch (status)
        {
        case 0x00:
            if (justRanDST)
            {
                snprintf(translatedString, MAX_DST_STATUS_STRING_LENGTH, "The self-test routine completed without error.");
            }
            else
            {
                snprintf(translatedString, MAX_DST_STATUS_STRING_LENGTH, "The previous self-test routine completed without error or no self-test has ever been run.");
            }
            break;
        case 0x01:

            snprintf(translatedString, MAX_DST_STATUS_STRING_LENGTH, "The self-test routine was aborted by the host.");
            break;
        case 0x02:
            snprintf(translatedString, MAX_DST_STATUS_STRING_LENGTH, "The self-test routine was interrupted by the host with a hardware or software reset.");
            break;
        case 0x03:
            snprintf(translatedString, MAX_DST_STATUS_STRING_LENGTH, "A fatal error or unknown test error occurred while the device was executing its self-test routine and the device was unable to complete the self-test routine.");
            break;
        case 0x04:
            if (justRanDST)
            {
                snprintf(translatedString, MAX_DST_STATUS_STRING_LENGTH, "The self-test completed having a test element that failed and the test element that failed is not known.");
            }
            else
            {
                snprintf(translatedString, MAX_DST_STATUS_STRING_LENGTH, "The previous self-test completed having a test element that failed and the test element that failed is not known.");
            }
            break;
        case 0x05:
            if (justRanDST)
            {
                snprintf(translatedString, MAX_DST_STATUS_STRING_LENGTH, "The self-test completed having the electrical element of the test failed.");
            }
            else
            {
                snprintf(translatedString, MAX_DST_STATUS_STRING_LENGTH, "The previous self-test completed having the electrical element of the test failed.");
            }
            break;
        case 0x06:
            if (justRanDST)
            {
                snprintf(translatedString, MAX_DST_STATUS_STRING_LENGTH, "The self-test completed having the servo (and/or seek) test element of the test failed.");
            }
            else
            {
                snprintf(translatedString, MAX_DST_STATUS_STRING_LENGTH, "The previous self-test completed having the servo (and/or seek) test element of the test failed.");
            }
            break;
        case 0x07:
            if (justRanDST)
            {
                snprintf(translatedString, MAX_DST_STATUS_STRING_LENGTH, "The self-test completed having the read element of the test failed.");
            }
            else
            {
                snprintf(translatedString, MAX_DST_STATUS_STRING_LENGTH, "The previous self-test completed having the read element of the test failed.");
            }
            break;
        case 0x08:
            if (justRanDST)
            {
                snprintf(translatedString, MAX_DST_STATUS_STRING_LENGTH, "The self-test completed having a test element that failed and the device is suspected of having handling damage.");
            }
            else
            {
                snprintf(translatedString, MAX_DST_STATUS_STRING_LENGTH, "The previous self-test completed having a test element that failed and the device is suspected of having handling damage.");
            }
            break;
        case 0x09:
        case 0x0A:
        case 0x0B:
        case 0x0C:
        case 0x0D:
        case 0x0E:
            snprintf(translatedString, MAX_DST_STATUS_STRING_LENGTH, "Reserved Status.");
            break;
        case 0x0F:
            snprintf(translatedString, MAX_DST_STATUS_STRING_LENGTH, "Self-test in progress.");
            break;
        default:
            snprintf(translatedString, MAX_DST_STATUS_STRING_LENGTH, "Error, unknown status: %" PRIX8 "h.", status);
        }
    }
}

int print_DST_Progress(tDevice *device)
{
    int result = UNKNOWN;
    uint32_t percentComplete = 0;
    uint8_t status = 0xFF;
    result = get_DST_Progress(device, &percentComplete, &status);
    if (result == NOT_SUPPORTED)
    {
        return result;
    }
    else if (result != SUCCESS)
    {
        if (VERBOSITY_QUIET < device->deviceVerbosity)
        {
            printf("An error occured while trying to retrieve DST Progress\n");
        }
    }
    else
    {
        bool isNVMeDrive = false;
        char statusTranslation[MAX_DST_STATUS_STRING_LENGTH] = { 0 };
        if (device->drive_info.drive_type == NVME_DRIVE)
        {
            isNVMeDrive = true;
        }
        printf("\tTest Progress = %"PRIu32"%%\n", percentComplete);
        translate_DST_Status_To_String(status, statusTranslation, false, isNVMeDrive);
        printf("%s\n", statusTranslation);
        switch (status)
        {
        case 0x00:
            result = SUCCESS;
            break;
        case 0x01:
            result = ABORTED;
            break;
        case 0x02:
            result = ABORTED;
            break;
        case 0x03:
            result = FAILURE;
            break;
        case 0x04:
            result = FAILURE;
            break;
        case 0x05:
            result = FAILURE;
            break;
        case 0x06:
            result = FAILURE;
            break;
        case 0x07:
            result = FAILURE;
            break;
        case 0x08:
            result = FAILURE;
            break;
        case 0x09:
        case 0x0A:
        case 0x0B:
        case 0x0C:
        case 0x0D:
        case 0x0E:
            break;
        case 0x0F:
            result = IN_PROGRESS;
            break;
        default:
            result = FAILURE;
        }
    }
    return result;
}

bool is_Self_Test_Supported(tDevice *device)
{
    bool supported = false;
    switch (device->drive_info.drive_type)
    {
    case NVME_DRIVE:
        //set based on controller reported capabilities first
        if (device->drive_info.IdentifyData.nvme.ctrl.oacs & BIT4)
        {
            supported = true;
        }
        //check if there is a passthrough limitation next
        if (device->drive_info.passThroughHacks.nvmePTHacks.limitedPassthroughCapabilities && !device->drive_info.passThroughHacks.nvmePTHacks.limitedCommandsSupported.deviceSelfTest)
        {
            supported = false;
        }
        break;
    case SCSI_DRIVE:
    {
        uint8_t selfTestResultsLog[LP_SELF_TEST_RESULTS_LEN] = { 0 };
        if (SUCCESS == scsi_Log_Sense_Cmd(device, false, LPC_CUMULATIVE_VALUES, LP_SELF_TEST_RESULTS, 0, 0, selfTestResultsLog, LP_SELF_TEST_RESULTS_LEN))
        {
            supported = true;
        }
    }
        break;
    case ATA_DRIVE:
        if (is_SMART_Enabled(device))
        {
            //also check that self test is supported by the drive
            if ((is_ATA_Identify_Word_Valid(device->drive_info.IdentifyData.ata.Word084) && device->drive_info.IdentifyData.ata.Word084 & BIT1)
                || (is_ATA_Identify_Word_Valid(device->drive_info.IdentifyData.ata.Word087) && device->drive_info.IdentifyData.ata.Word087 & BIT1))
            {
                //NOTE: Also need to check the SMART read data as it also contains a bit to indicate if DST is supported or not!
                //      That field also indicates whether Short, extended, or conveyance tests are supported!
                //      Since the SAMART read data has been made obsolete on newer standards, we may need a version check or something to keep proper behavior
                //      as new devices show up without support for this information.
                //SMART read data is listed as optional in ata/atapi-7
                uint8_t smartData[512] = { 0 };
                if (SUCCESS == ata_SMART_Read_Data(device, smartData, 512))
                {
                    //check the pff-line data collection capability field
                    //assume this is more accurate since this seems to be the case with some older products
                    if ((smartData[367] & BIT0) && (smartData[367] & BIT4))//bit0 = the subcommand, bit4 = self-test routine implemented (short and extended)
                    {
                        supported = true;
                    }
                }
                else
                {
                    //assume that the identify bits were accurate for this command.
                    supported = true;
                }
            }
        }
        break;
    default:
        break;
    }
    return supported;
}

bool is_Conveyence_Self_Test_Supported(tDevice *device)
{
    bool supported = false;
    if (device->drive_info.drive_type == ATA_DRIVE)
    {
        uint8_t smartReadData[LEGACY_DRIVE_SEC_SIZE] = { 0 };
        if (SUCCESS == ata_SMART_Read_Data(device, smartReadData, LEGACY_DRIVE_SEC_SIZE))
        {
            if ((smartReadData[367] & BIT0) && (smartReadData[367] & BIT5))
            {
                supported = true;
            }
        }
    }
    return supported;
}

bool is_Selective_Self_Test_Supported(tDevice* device)
{
    bool supported = false;
    if (device->drive_info.drive_type == ATA_DRIVE)
    {
        uint8_t smartReadData[LEGACY_DRIVE_SEC_SIZE] = { 0 };
        if (SUCCESS == ata_SMART_Read_Data(device, smartReadData, LEGACY_DRIVE_SEC_SIZE))
        {
            if ((smartReadData[367] & BIT0) && (smartReadData[367] & BIT6))
            {
                supported = true;
            }
        }
    }
    return supported;
}

int send_DST(tDevice *device, eDSTType DSTType, bool captiveForeground, uint32_t commandTimeout)
{
    int ret = NOT_SUPPORTED;
    if (commandTimeout == 0)
    {
        if (os_Is_Infinite_Timeout_Supported())
        {
            commandTimeout = INFINITE_TIMEOUT_VALUE;
        }
        else
        {
            commandTimeout = MAX_CMD_TIMEOUT_SECONDS;
        }
    }
    os_Lock_Device(device);
    switch (device->drive_info.drive_type)
    {
    case NVME_DRIVE:
        //TODO: Handle individual namespaces! currently just running it on all of them!
        switch (DSTType)
        {
        case DST_TYPE_SHORT:
            ret = nvme_Device_Self_Test(device, UINT32_MAX, 1);
            break;
        case DST_TYPE_LONG:
            ret = nvme_Device_Self_Test(device, UINT32_MAX, 2);
            break;
        case DST_TYPE_CONVEYENCE://not available on NVMe
        default:
            break;
        }
        break;
    case SCSI_DRIVE:
        switch (DSTType)
        {
        case DST_TYPE_SHORT:
            if (captiveForeground)
            {
                ret = scsi_Send_Diagnostic(device, 0x05, 0, 0, 0, 0, 0, NULL, 0, commandTimeout);
            }
            else
            {
                ret = scsi_Send_Diagnostic(device, 0x01, 0, 0, 0, 0, 0, NULL, 0, commandTimeout);
            }
            break;
        case DST_TYPE_LONG:
            if (captiveForeground)
            {
                ret = scsi_Send_Diagnostic(device, 0x06, 0, 0, 0, 0, 0, NULL, 0, commandTimeout);
            }
            else
            {
                ret = scsi_Send_Diagnostic(device, 0x02, 0, 0, 0, 0, 0, NULL, 0, commandTimeout);
            }
            break;
        case DST_TYPE_CONVEYENCE://not available on SCSI
        default:
            break;
        }
        break;
    case ATA_DRIVE:
        switch (DSTType)
        {
        case DST_TYPE_SHORT:
            if (captiveForeground)
            {
                ret = ata_SMART_Offline(device, 0x81, commandTimeout);
            }
            else
            {
                ret = ata_SMART_Offline(device, 0x01, commandTimeout);
            }
            break;
        case DST_TYPE_LONG:
            if (captiveForeground)
            {
                ret = ata_SMART_Offline(device, 0x82, commandTimeout);
            }
            else
            {
                ret = ata_SMART_Offline(device, 0x02, commandTimeout);
            }
            break;
        case DST_TYPE_CONVEYENCE:
            if (is_Conveyence_Self_Test_Supported(device))
            {
                if (captiveForeground)
                {
                    ret = ata_SMART_Offline(device, 0x83, commandTimeout);
                }
                else
                {
                    ret = ata_SMART_Offline(device, 0x03, commandTimeout);
                }
            }
            else
            {
                ret = NOT_SUPPORTED;
            }
            break;
        default:
            break;
        }
        break;
    default:
        ret = NOT_SUPPORTED;
        break;
    }
    os_Unlock_Device(device);
    return ret;
}

int run_DST(tDevice *device, eDSTType DSTType, bool pollForProgress, bool captiveForeground, bool ignoreMaxTime)
{
    int ret = NOT_SUPPORTED;
    if (is_Self_Test_Supported(device))
    {
        uint8_t status = 0xF0;
        uint32_t percentComplete = 0;
        uint32_t delayTime = 5;//assume 5 second delay between progress checks
        uint32_t commandTimeout = 15;//start with this default timeout value - TJE
        uint32_t timeDiff = 30;
        uint32_t maxTimeIncreases = 2;
        uint32_t timeIncreaseWarningCount = 1;
        uint32_t totalDSTTimeSeconds = 120;
        uint32_t maxDSTWaitTimeSeconds = totalDSTTimeSeconds * 5;//TODO: add some kind of multiplier or something to this
        //check if DST is already running
        ret = get_DST_Progress(device, &percentComplete, &status);
        if (status == 0x0F)
        {
            ret = IN_PROGRESS;
        }
        if (ret == SUCCESS || ret == IN_PROGRESS)
        {
            //time to start the DST
            switch (DSTType)
            {
            case DST_TYPE_SHORT: //short
                delayTime = 5;
                if (captiveForeground)
                {
                    commandTimeout = 120;//two minutes as per ATA and SCSI specifications
                }
                break;
            case DST_TYPE_LONG: //extended
                //Since long DST is slower in terms of when progress is achieved, we need to change some things such as how often we poll, how often we expect progress increments,
                //and the maximum number of times to allow for increasing the polling interval.
                //For now, I'm just multiplying everything by 3. Not sure if there is a better way to do this or not.
                delayTime *= 3;
                timeDiff *= 3;
                maxTimeIncreases *= 3;
                timeIncreaseWarningCount *= 3;
                {
                    uint8_t hours = 0, minutes = 0;
                    if (SUCCESS == get_Long_DST_Time(device, &hours, &minutes))
                    {
                        if (captiveForeground)
                        {
                            commandTimeout = C_CAST(uint32_t, hours) * UINT32_C(3600) + C_CAST(uint32_t, minutes) * UINT32_C(60);//this is a value in seconds
                        }
                        totalDSTTimeSeconds = hours * UINT32_C(3600) + minutes * UINT32_C(60);
                        maxDSTWaitTimeSeconds = totalDSTTimeSeconds * UINT32_C(5);//TODO: add some kind of multiplier or something to this
                    }
                    else
                    {
                        if (captiveForeground)
                        {
                            //set a maximum time out value
                            commandTimeout = UINT32_MAX;
                        }
                        totalDSTTimeSeconds = 14400;//a fallback for drives not reporting a time, so using 4 hours for now. This likely will not ever happen
                        maxDSTWaitTimeSeconds = totalDSTTimeSeconds * 5;//TODO: add some kind of multiplier or something to this
                    }
                }
                break;
            case DST_TYPE_CONVEYENCE: //conveyence
                delayTime = 5;
                if (captiveForeground)
                {
                    commandTimeout = 120;//two minutes as per ATA spec
                }
                break;
            default:
                ret = BAD_PARAMETER;
                return ret;
            }
            if (ret == SUCCESS)
            {
                ret = send_DST(device, DSTType, captiveForeground, commandTimeout);
            }
            //now poll for progress if it was requested
            if ((ret == SUCCESS || ret == IN_PROGRESS) && pollForProgress && !captiveForeground)
            {
                delay_Seconds(1);//delay for a second before starting to poll for progress to give it time to start
                //set status to 0x08 before the loop or it will not get entered
                status = 0x0F;
                time_t dstProgressTimer = time(NULL);
                time_t startTime = time(NULL);
                uint32_t lastProgressIndication = 0;
                uint8_t timeExtensionCount = 0;
                char *overTimeWarningMessage = "WARNING: DST is taking longer than expected.";
                bool showTimeWarning = false;
                bool abortForTooLong = false;
                while (status == 0x0F && (ret == SUCCESS || ret == IN_PROGRESS))
                {
                    lastProgressIndication = percentComplete;
                    ret = get_DST_Progress(device, &percentComplete, &status);
                    if (VERBOSITY_QUIET < device->deviceVerbosity)
                    {
                        if (showTimeWarning)
                        {
                            printf("\r    Test progress: %" PRIu32 "%% complete. %s", percentComplete, overTimeWarningMessage);
                        }
                        else
                        {
                            printf("\r    Test progress: %" PRIu32 "%% complete.  ", percentComplete);
                        }
                        if (status != 0x00)
                        {
                            fflush(stdout);
                        }
                    }
                    if (status < 0x08)//known errors are less than this value
                    {
                        break;
                    }
                    //make the time between progress polls bigger if it isn't changing quickly to allow the drive time to finish any recovery it's doing.
                    if (difftime(time(NULL), dstProgressTimer) > timeDiff && lastProgressIndication == percentComplete)
                    {
                        //We are likely pinging the drive too quickly during the read test and error recovery isn't finishing...extend the delay time
                        if (timeExtensionCount <= maxTimeIncreases)
                        {
                            delayTime *= 2;
                            timeDiff *= 2;
                            ++timeExtensionCount;
                        }
                        dstProgressTimer = time(NULL);//reset this beginning timer since we changed the polling time
                        if (!ignoreMaxTime && timeExtensionCount > maxTimeIncreases && difftime(dstProgressTimer, startTime) > maxDSTWaitTimeSeconds)
                        {
                            //only abort if we are past the total DST time and have already increased the delays multiple times.
                            //we've extended the polling time too much. Something else is wrong in the drive. Just abort it and exit.
                            ret = abort_DST(device);
                            ret = ABORTED;
                            abortForTooLong = true;
                            break;
                        }
                        else if(timeExtensionCount > timeIncreaseWarningCount && difftime(dstProgressTimer, startTime) > totalDSTTimeSeconds)
                        {
                            showTimeWarning = true;
                        }
                    }
                    delay_Seconds(delayTime);
                }
                if (status == 0 && ret == SUCCESS)
                {
                    //printf 35 characters + width of warning message to clear the line before printing this final status update
                    printf("\r                                    %.*s", C_CAST(int, strlen(overTimeWarningMessage)), "                                                                        ");
                    printf("\r    Test progress: 100%% complete   ");
                    fflush(stdout);
                    ret = SUCCESS; //we passed.
                }
                else if (status == 0x01 || status == 0x02 || ret == ABORTED)
                {
                    //DST was aborted by the host with either a reset or a abort command
                    ret = ABORTED;
                }
                else if (ret != ABORTED)
                {
                    ret = FAILURE; //failed the test
                }
                if (VERBOSITY_QUIET < device->deviceVerbosity)
                {
                    if (abortForTooLong)
                    {
                        printf("\nDST was aborted for taking too long. This may happen if other disc activity\n");
                        printf("is too high! Please check to make sure no other disk IO is occurring so that DST\n");
                        printf("can complete as expected!\n");
                    }
                    else
                    {
                        bool isNVMeDrive = false;
                        char statusTranslation[MAX_DST_STATUS_STRING_LENGTH] = { 0 };
                        if (device->drive_info.drive_type == NVME_DRIVE)
                        {
                            isNVMeDrive = true;
                        }
                        translate_DST_Status_To_String(status, statusTranslation, true, isNVMeDrive);
                        printf("\n%s\n", statusTranslation);
                    }
                }
            }
            else if (ret == SUCCESS && captiveForeground)
            {
                //need to check the result! Probably best to do this from DST log in case ATA RTFRs are not reported back correctly. - TJE
#if !defined (DISABLE_NVME_PASSTHORUGH)
                if (device->drive_info.drive_type == NVME_DRIVE)
                {
                    //simulate a "captive" test on NVMe by polling and waiting to get status.
                    while (status == 0x0F && ret == SUCCESS)
                    {
                        ret = get_DST_Progress(device, &percentComplete, &status);
                    }
                    if (status == 0)
                    {
                        ret = SUCCESS;
                    }
                    else
                    {
                        ret = FAILURE;
                    }
                }
                else
                {
#endif
                    //if the LBA registers have C2-4F or 2C-F4, then we have pass vs fail results.
                    if (device->drive_info.drive_type == ATA_DRIVE && device->drive_info.lastCommandRTFRs.lbaMid == ATA_SMART_SIG_MID && device->drive_info.lastCommandRTFRs.lbaHi == ATA_SMART_SIG_HI)
            		{
                		ret = SUCCESS;
            		}
            		else if (device->drive_info.drive_type == ATA_DRIVE && device->drive_info.lastCommandRTFRs.lbaMid == ATA_SMART_BAD_SIG_MID && device->drive_info.lastCommandRTFRs.lbaHi == ATA_SMART_BAD_SIG_HI)
            		{
                		//SMART is tripped
                		ret = FAILURE;
            		}
            		else //SCSI drive, or ATA rtfrs not available
            		{
                    	//otherwise, read the DST log to figure out the results
                    	dstLogEntries logEntries;
                    	memset(&logEntries, 0, sizeof(dstLogEntries));
                    	//read the DST log for the result to avoid any SATL issues...
                    	if (SUCCESS == get_DST_Log_Entries(device, &logEntries))
                    	{
                        	if (0 == M_Nibble1(logEntries.dstEntry[0].selfTestExecutionStatus))
                        	{
                            	ret = SUCCESS;
                        	}
                        	else if (0xF == M_Nibble1(logEntries.dstEntry[0].selfTestExecutionStatus))
                        	{
                            	//NOTE: this shouldn't ever happen, but I've seen weird things before...-TJE
                            	ret = IN_PROGRESS;
                        	}
                            else if (0x1 == M_Nibble1(logEntries.dstEntry[0].selfTestExecutionStatus) || 0x2 == M_Nibble1(logEntries.dstEntry[0].selfTestExecutionStatus))
                            {
                                //DST was aborted by the host somehow.
                                ret = ABORTED;
                            }
                        	else
                        	{
                            	ret = FAILURE;
                        	}
                    	}
                    	else
                    	{
                        	ret = UNKNOWN;
                    	}
                    }
                }
                if (VERBOSITY_QUIET < device->deviceVerbosity)
                {
                    bool isNVMeDrive = false;
                    char statusTranslation[MAX_DST_STATUS_STRING_LENGTH] = { 0 };
                    if (device->drive_info.drive_type == NVME_DRIVE)
                    {
                        isNVMeDrive = true;
                    }
                    translate_DST_Status_To_String(status, statusTranslation, true, isNVMeDrive);
                    printf("\n%s\n", statusTranslation);
                }

            }
            else if (!pollForProgress && SUCCESS == ret)
            {
                ret = SUCCESS;
            }
        }
    }
    else
    {
        ret = NOT_SUPPORTED;
    }
    return ret;
}

int get_Long_DST_Time(tDevice *device, uint8_t *hours, uint8_t *minutes)
{
    int ret = UNKNOWN;
    if (hours == NULL || minutes == NULL)
    {
        return BAD_PARAMETER;
    }
    switch (device->drive_info.drive_type)
    {
    case ATA_DRIVE:
        if (is_Self_Test_Supported(device))
        {
            uint16_t longDSTTime = 0;
            uint8_t *smartData = C_CAST(uint8_t*, calloc_aligned(LEGACY_DRIVE_SEC_SIZE, sizeof(uint8_t), device->os_info.minimumAlignment));
            if (smartData == NULL)
            {
                perror("calloc failure\n");
                return MEMORY_FAILURE;
            }
            if (SUCCESS == ata_SMART_Read_Data(device, smartData, LEGACY_DRIVE_SEC_SIZE))
            {
                longDSTTime = smartData[373];
                if (longDSTTime == UINT8_MAX)
                {
                    longDSTTime = M_BytesTo2ByteValue(smartData[376], smartData[375]);
                }
                //convert the time to hours and minutes
                *hours = C_CAST(uint8_t, longDSTTime / 60);
                *minutes = C_CAST(uint8_t, longDSTTime % 60);
                ret = SUCCESS;
            }
            safe_Free_aligned(smartData)
        }
        break;
    case NVME_DRIVE:
    {
        uint16_t longTestTime = device->drive_info.IdentifyData.nvme.ctrl.edstt;
        *hours = C_CAST(uint8_t, longTestTime / 60);
        *minutes = C_CAST(uint8_t, longTestTime % 60);
        ret = SUCCESS;
    }
        break;
    case SCSI_DRIVE:
    {
        uint16_t longDSTTime = 0;
        bool getTimeFromExtendedInquiryData = false;
        uint8_t *controlMP = C_CAST(uint8_t*, calloc_aligned(MP_CONTROL_LEN + MODE_PARAMETER_HEADER_10_LEN, sizeof(uint8_t), device->os_info.minimumAlignment));
        if (controlMP == NULL)
        {
            perror("calloc failure!");
            return MEMORY_FAILURE;
        }
        //read the control MP to get the long DST time, but it is reported in SECONDS here
        if (SUCCESS == scsi_Mode_Sense_10(device, MP_CONTROL, MP_CONTROL_LEN + MODE_PARAMETER_HEADER_10_LEN, 0, true, false, MPC_DEFAULT_VALUES, controlMP))
        {
            longDSTTime = M_BytesTo2ByteValue(controlMP[MODE_PARAMETER_HEADER_10_LEN + 10], controlMP[MODE_PARAMETER_HEADER_10_LEN + 11]);
            if (longDSTTime == UINT16_MAX)
            {
                getTimeFromExtendedInquiryData = true;
            }
            else
            {
                //convert from the time in SECONDS to hours and minutes
                *hours = C_CAST(uint8_t, longDSTTime / 3600);
                *minutes = C_CAST(uint8_t, (longDSTTime % 3600) / 60);
                ret = SUCCESS;
            }
        }
        else
        {
            if (SUCCESS == scsi_Mode_Sense_6(device, MP_CONTROL, MP_CONTROL_LEN + MODE_PARAMETER_HEADER_6_LEN, 0, true, MPC_DEFAULT_VALUES, controlMP))
            {
                longDSTTime = M_BytesTo2ByteValue(controlMP[MODE_PARAMETER_HEADER_6_LEN + 10], controlMP[MODE_PARAMETER_HEADER_6_LEN + 11]);
                if (longDSTTime == UINT16_MAX)
                {
                    getTimeFromExtendedInquiryData = true;
                }
                else
                {
                    //convert from the time in SECONDS to hours and minutes
                    *hours = C_CAST(uint8_t, longDSTTime / 3600);
                    *minutes = C_CAST(uint8_t, (longDSTTime % 3600) / 60);
                    ret = SUCCESS;
                }
            }
            else
            {
                ret = NOT_SUPPORTED;
                getTimeFromExtendedInquiryData = true;//some crappy USB bridges may not support the mode page, but will support the VPD page, so attempt to read the VPD page anyways
            }
        }
        safe_Free_aligned(controlMP)
        if (getTimeFromExtendedInquiryData)
        {
            uint8_t *extendedInqyData = C_CAST(uint8_t*, calloc_aligned(VPD_EXTENDED_INQUIRY_LEN, sizeof(uint8_t), device->os_info.minimumAlignment));
            if (extendedInqyData == NULL)
            {
                perror("calloc failure!\n");
                return MEMORY_FAILURE;
            }
            if (SUCCESS == scsi_Inquiry(device, extendedInqyData, VPD_EXTENDED_INQUIRY_LEN, EXTENDED_INQUIRY_DATA, true, false))
            {
                //time is reported in MINUTES here
                longDSTTime = M_BytesTo2ByteValue(extendedInqyData[10], extendedInqyData[11]);
                //convert the time to hours and minutes
                *hours = C_CAST(uint8_t, longDSTTime / 60);
                *minutes = C_CAST(uint8_t, longDSTTime % 60);
                ret = SUCCESS;
            }
            safe_Free_aligned(extendedInqyData)
        }
    }
    break;
    default:
        ret = NOT_SUPPORTED;
        break;
    }
    return ret;
}

bool get_Error_LBA_From_DST_Log(tDevice *device, uint64_t *lba)
{
    bool isValidLBA = false;
    *lba = UINT64_MAX;//set to something crazy in case caller ignores return type
    dstLogEntries dstEntries;
    memset(&dstEntries, 0, sizeof(dstLogEntries));
    if (get_DST_Log_Entries(device, &dstEntries) == SUCCESS)
    {
        if (dstEntries.numberOfEntries > 0
            && dstEntries.dstEntry[0].descriptorValid
            && M_Nibble1(dstEntries.dstEntry[0].selfTestExecutionStatus) == 0x07)
        {
            isValidLBA = true;
            *lba = dstEntries.dstEntry[0].lbaOfFailure;
        }
    }
    return isValidLBA;
}

int run_DST_And_Clean(tDevice *device, uint16_t errorLimit, custom_Update updateFunction, void *updateData, ptrDSTAndCleanErrorList externalErrorList, bool *repaired)
{
    int ret = SUCCESS;//assume this works successfully
    errorLBA *errorList = NULL;
    uint64_t *errorIndex = NULL;
    uint64_t localErrorIndex = 0;
    uint64_t totalErrors = 0;
    bool unableToRepair = false;
    bool passthroughWrite = false;
    M_USE_UNUSED(updateFunction);
    M_USE_UNUSED(updateData);
    if (is_Sector_Size_Emulation_Active(device))
    {
        passthroughWrite = true;//in this case, since sector size emulation is active, we need to issue a passthrough command for the repair instead of a standard interface command. - TJE
    }
    if (!externalErrorList)
    {
        size_t errorListAllocation = 0; 
        if (errorLimit < 1)
        {
            errorListAllocation = sizeof(errorLBA);//allocate a list length of 1..that way we have something if the rest of the function does try and access this memory.
        }
        else
        {
            errorListAllocation = errorLimit * sizeof(errorLBA);
        }
        errorList = C_CAST(errorLBA*, calloc_aligned(errorListAllocation, sizeof(errorLBA), device->os_info.minimumAlignment));
        if (!errorList)
        {
            perror("calloc failure\n");
            return MEMORY_FAILURE;
        }
        errorIndex = &localErrorIndex;
    }
    else
    {
        errorList = externalErrorList->ptrToErrorList;
        errorIndex = externalErrorList->errorIndex;
    }

    bool autoReadReassign = false;
    bool autoWriteReassign = false;
    if (SUCCESS != get_Automatic_Reallocation_Support(device, &autoWriteReassign, &autoReadReassign))
    {
        autoWriteReassign = true;//just in case this fails, default to previous behavior
    }
    //this is escentially a loop over the sequential read function
    while (totalErrors <= errorLimit)
    {
        //start DST
        if (device->deviceVerbosity >= VERBOSITY_DEFAULT)
        {
            printf("Running DST.\n");
        }

        if (SUCCESS == run_DST(device, DST_TYPE_SHORT, false, false, true))
        {
            //poll until it finished
            delay_Seconds(1);
            uint8_t status = 0x0F;
            uint32_t percentComplete = 0;
            uint32_t lastProgressIndication = 0;
            uint8_t delayTime = 5;//Start with 5. If after 30 seconds, progress has not changed, increase the delay to 15seconds. If it still fails to update after the next minute, break out and abort the DST.
            time_t dstProgressTimer = time(NULL);
            uint8_t timeExtensionCount = 0;
            bool dstAborted = false;
            while (status == 0x0F && ret == SUCCESS)
            {
                lastProgressIndication = percentComplete;
                delay_Seconds(delayTime);
                ret = get_DST_Progress(device, &percentComplete, &status);
                if (difftime(time(NULL), dstProgressTimer) > 30 && lastProgressIndication == percentComplete)
                {
                    //We are likely pinging the drive too quickly during the read test and error recovery isn't finishing...extend the delay time
                    delayTime *= 2;
                    ++timeExtensionCount;
                    dstProgressTimer = time(NULL);//reset this beginning timer since we changed the polling time
                    if (timeExtensionCount > 3)
                    {
                        //we've extended the polling time too much. Something else is wrong in the drive. Just abort it and exit.
                        ret = abort_DST(device);
                        dstAborted = true;
                        ret = ABORTED;
                        break;//break out of the loop so we don't hang forever
                    }
                }
            }
            if (dstAborted)
            {
                break;
            }
            if (status == 0)
            {
                //printf("DST Passed - exiting.\n");
                //DST passed, time to exit
                break;
            }
            else
            {
                if (get_Error_LBA_From_DST_Log(device, &errorList[*errorIndex].errorAddress))
                {
                    totalErrors++; // Increment the number of errors we have seen
                    if (totalErrors > errorLimit) 
                    {
                        break;
                    }
                    if (device->deviceVerbosity > VERBOSITY_QUIET)
                    {
                        printf("Reparing LBA %"PRIu64"\n", errorList[*errorIndex].errorAddress);
                    }
                    //we got a valid LBA, so time to fix it
                    int repairRet = repair_LBA(device, &errorList[*errorIndex], passthroughWrite, autoWriteReassign, autoReadReassign);
                    if (repaired)
                    {
                        *repaired = true;
                    }
                    (*errorIndex)++;
                    if (FAILURE == repairRet)
                    {
                        ret = FAILURE;
                        break;
                    }
                    else if (OS_PASSTHROUGH_FAILURE == repairRet)
                    {
                        ret = FAILURE;
                        unableToRepair = true;
                        break;
                    }
                    else if (PERMISSION_DENIED == repairRet)
                    {
                        ret = PERMISSION_DENIED;
                        break;
                    }
                    else if (SUCCESS != repairRet)
                    {
                        ret = repairRet;
                        unableToRepair = true;
                        break;
                    }
                    //Now we need to read around the LBA we repaired to make sure there aren't others around
                    uint64_t readAroundStart = 0;
                    uint64_t readAroundRange = 10000;//10000 LBAs total (as long as we don't go over the end of the drive)
                    if (errorList[*errorIndex].errorAddress > 5000)
                    {
                        readAroundStart = errorList[*errorIndex].errorAddress - 5000;
                    }
                    if (passthroughWrite)
                    {
                        if (device->drive_info.bridge_info.childDeviceMaxLba - errorList[*errorIndex].errorAddress < 5000)
                        {
                            readAroundRange = device->drive_info.bridge_info.childDeviceMaxLba - readAroundStart;
                        }
                    }
                    else
                    {
                        if (device->drive_info.deviceMaxLba - errorList[*errorIndex].errorAddress < 5000)
                        {
                            readAroundRange = device->drive_info.deviceMaxLba - readAroundStart;
                        }
                    }
                    //not using generic_tests.h since we don't have a way to force ATA vs SCSI passthrough command for this, and we have times where we must do a passthrough write (USB emulation nonsense)
                    //first try verifying the whole thing at once so we can skip the loop below if it is good
                    int verify = SUCCESS;
                    if (passthroughWrite)
                    {
                        verify = ata_Read_Verify(device, readAroundStart, C_CAST(uint32_t, readAroundRange));
                    }
                    else
                    {
                        verify = verify_LBA(device, readAroundStart, C_CAST(uint32_t, readAroundRange));
                    }
                    if (SUCCESS != verify)
                    {
                        //there is another bad sector we need to find and fix...
                        uint8_t logicalPerPhysical = C_CAST(uint8_t, device->drive_info.devicePhyBlockSize / device->drive_info.deviceBlockSize);
                        if (passthroughWrite)
                        {
                            logicalPerPhysical = C_CAST(uint8_t, device->drive_info.bridge_info.childDevicePhyBlockSize / device->drive_info.bridge_info.childDeviceBlockSize);
                        }
                        for (uint64_t iter = readAroundStart; iter < (readAroundStart + readAroundRange); iter += logicalPerPhysical)
                        {
                            if (totalErrors > errorLimit)
                            {
                                break;
                            }
                            if (passthroughWrite)
                            {
                                verify = ata_Read_Verify(device, iter, logicalPerPhysical);
                            }
                            else
                            {
                                verify = verify_LBA(device, iter, logicalPerPhysical);
                            }
                            if (verify != SUCCESS)
                            {
                                if (device->deviceVerbosity > VERBOSITY_QUIET)
                                {
                                    printf("Reparing LBA %"PRIu64"\n", iter);
                                }
                                //add the LBA to the error list we have going, then repair it
                                errorList[*errorIndex].repairStatus = NOT_REPAIRED;
                                errorList[*errorIndex].errorAddress = iter;
                                repairRet = repair_LBA(device, &errorList[*errorIndex], passthroughWrite, autoWriteReassign, autoReadReassign);
                                ++totalErrors;
                                ++(*errorIndex);
                                if (FAILURE == repairRet)
                                {
                                    ret = FAILURE;
                                    break;
                                }
                                else if (OS_PASSTHROUGH_FAILURE == repairRet)
                                {
                                    ret = FAILURE;
                                    unableToRepair = true;
                                    break;
                                }
                                else if (PERMISSION_DENIED == repairRet)
                                {
                                    ret = PERMISSION_DENIED;
                                    break;
                                }
                            }
                        }
                    }
                }
                else
                {
//                  printf("\n\n\n*** DST and Clean - Unable to repair ***\n\n\n");
                    unableToRepair = true;
                    ret = FAILURE;
                    break;
                }
            }
        }
        else
        {
            //printf("\n\n\n*** Couldn't start a DST. ***\n\n\n");
            //couldn't start a DST...so just break out of the loop
            break;
        }
    }
    //printf("totalErrors:  %"PRIu64"\n", totalErrors);
    //printf("errorIndex:  %"PRIu64"\n", errorIndex);
    //printf("errorLimit:  %"PRIu16"\n", errorLimit);
    if (totalErrors > errorLimit)
    {
        ret = FAILURE;
    }
    if (!externalErrorList)
    {
        if (device->deviceVerbosity > VERBOSITY_QUIET)
        {
            if (totalErrors > 0)
            {
                print_LBA_Error_List(errorList, C_CAST(uint16_t, *errorIndex));
                if (unableToRepair)
                {
                    printf("Other errors were found during DST, but were unable to be repaired.\n");
                }
            }
            else if (unableToRepair)
            {
                printf("An error was detected during DST but it is unable to be repaired.\n");
            }
            else
            {
                printf("No bad LBAs detected during DST and Clean.\n");
            }
        }
        safe_Free_aligned(errorList)
    }
    return ret;
}
#define ENABLE_DST_LOG_DEBUG 0 //set to non zero to enable this debug.
//TODO: This should grab the entries in order from most recent to oldest...current sort via timestamp won't fix getting the most recent one first.
static int get_ATA_DST_Log_Entries(tDevice *device, ptrDstLogEntries entries)
{
    int ret = NOT_SUPPORTED;
    uint8_t *selfTestResults = NULL;
    uint32_t logSize = 0;//used for compatibility purposes with drives that may have GPL, but not support the ext log...
    //device->drive_info.ata_Options.generalPurposeLoggingSupported = false;//for debugging SMART log version
    if (device->drive_info.ata_Options.generalPurposeLoggingSupported && SUCCESS == get_ATA_Log_Size(device, ATA_LOG_EXTENDED_SMART_SELF_TEST_LOG, &logSize, true, false) && logSize > 0)
    {
        uint32_t extLogSize = LEGACY_DRIVE_SEC_SIZE;
        if(SUCCESS != get_ATA_Log_Size(device, ATA_LOG_EXTENDED_SMART_SELF_TEST_LOG, &extLogSize, true, false))
        {
            return NOT_SUPPORTED;
        }
        selfTestResults = C_CAST(uint8_t*, calloc_aligned(extLogSize, sizeof(uint8_t), device->os_info.minimumAlignment));
        uint16_t lastPage = C_CAST(uint16_t, (extLogSize / LEGACY_DRIVE_SEC_SIZE) - 1);//zero indexed
        if (!selfTestResults)
        {
            return MEMORY_FAILURE;
        }
        //read the extended self test results log with read log ext
        if (SUCCESS == get_ATA_Log(device, ATA_LOG_EXTENDED_SMART_SELF_TEST_LOG, NULL, NULL, true, false, true, selfTestResults, extLogSize, NULL, 0, 0))
        //if (SUCCESS == ata_Read_Log_Ext(device, ATA_LOG_EXTENDED_SMART_SELF_TEST_LOG, 0, selfTestResults, LEGACY_DRIVE_SEC_SIZE, device->drive_info.ata_Options.readLogWriteLogDMASupported, 0))
        {
            ret = SUCCESS;
            entries->logType = DST_LOG_TYPE_ATA;
            uint16_t selfTestIndex = M_BytesTo2ByteValue(selfTestResults[3], selfTestResults[2]);
            if (selfTestIndex > 0)//we know DST has been run at least once...
            {
                uint8_t descriptorLength = 26;
                uint8_t zeroCompare[26] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
                //To calculate page number:
                //   There are 19 descriptors in 512 bytes.
                //   There are 4 reserved bytes in each sector + 18 at the end
                //   26 * 19 + 18 = 512;

                uint16_t zeroBasedIndex = selfTestIndex - 1;
                uint16_t pageNumber = (zeroBasedIndex) / 19;
                uint16_t entryWithinPage = (zeroBasedIndex) % 19;
                uint16_t descriptorOffset = (entryWithinPage * descriptorLength) + 4;//offset withing the page
                #if ENABLE_DST_LOG_DEBUG
                printf("starting at page number = %u\n", pageNumber);
                printf("lastPage = %u\n", lastPage);
                #endif
                uint32_t offset = (pageNumber > 0) ? descriptorOffset * pageNumber : descriptorOffset;
                uint32_t firstOffset = offset;
                uint16_t counter = 0;//when this get's larger than our max, we need to break out of the loop. This is always incremented. - TJE
                uint16_t maxEntries = (lastPage + 1) * 19;//this should get us some multiple of 19 based on the number of pages we read.
                #if ENABLE_DST_LOG_DEBUG
                printf("maxEntries = %u\n", maxEntries);
                #endif
                while(counter <= maxEntries)
                {
                    #if ENABLE_DST_LOG_DEBUG
                    printf("offset = %u\n", offset);
                    #endif
                    if(counter > 0 && offset == firstOffset)
                    {
                        //we're back at the beginning and need to exit the loop.
                        break;
                    }
                    if (memcmp(&selfTestResults[offset], zeroCompare, descriptorLength))//invalid entires will be all zeros-TJE
                    {
                        entries->dstEntry[entries->numberOfEntries].descriptorValid = true;
                        entries->dstEntry[entries->numberOfEntries].selfTestRun = selfTestResults[offset];
                        entries->dstEntry[entries->numberOfEntries].selfTestExecutionStatus = selfTestResults[offset + 1];
                        entries->dstEntry[entries->numberOfEntries].lifetimeTimestamp = M_BytesTo2ByteValue(selfTestResults[offset + 3], selfTestResults[offset + 2]);
                        entries->dstEntry[entries->numberOfEntries].checkPointByte = selfTestResults[offset + 4];
                        entries->dstEntry[entries->numberOfEntries].lbaOfFailure = M_BytesTo8ByteValue(0, 0, selfTestResults[offset + 10], selfTestResults[offset + 9], selfTestResults[offset + 8], selfTestResults[offset + 7], selfTestResults[offset + 6], selfTestResults[offset + 5]);
                        //if LBA field is all F's, this is meant to signify an invalid value like T10 specs say to do - TJE
                        if (entries->dstEntry[entries->numberOfEntries].lbaOfFailure == MAX_48_BIT_LBA)
                        {
                            entries->dstEntry[entries->numberOfEntries].lbaOfFailure = UINT64_MAX;
                        }

                        memcpy(&entries->dstEntry[entries->numberOfEntries].ataVendorSpecificData[0], &selfTestResults[offset + 11], 15);
                        //dummy up sense data...
                        switch (M_Nibble1(entries->dstEntry[entries->numberOfEntries].selfTestExecutionStatus))
                        {
                        case 0:
                        case 15:
                            entries->dstEntry[entries->numberOfEntries].scsiSenseCode.senseKey = SENSE_KEY_NO_ERROR;
                            entries->dstEntry[entries->numberOfEntries].scsiSenseCode.additionalSenseCode = 0;
                            entries->dstEntry[entries->numberOfEntries].scsiSenseCode.additionalSenseCodeQualifier = 0;
                            //NOTE: This is a workaround to clear out the LBA on success:
                            entries->dstEntry[entries->numberOfEntries].lbaOfFailure = UINT64_MAX;
                            break;
                        case 1:
                            entries->dstEntry[entries->numberOfEntries].scsiSenseCode.senseKey = SENSE_KEY_ABORTED_COMMAND;
                            entries->dstEntry[entries->numberOfEntries].scsiSenseCode.additionalSenseCode = 0x40;
                            entries->dstEntry[entries->numberOfEntries].scsiSenseCode.additionalSenseCodeQualifier = 0x81;
                            break;
                        case 2:
                            entries->dstEntry[entries->numberOfEntries].scsiSenseCode.senseKey = SENSE_KEY_ABORTED_COMMAND;
                            entries->dstEntry[entries->numberOfEntries].scsiSenseCode.additionalSenseCode = 0x40;
                            entries->dstEntry[entries->numberOfEntries].scsiSenseCode.additionalSenseCodeQualifier = 0x82;
                            break;
                        case 3:
                            entries->dstEntry[entries->numberOfEntries].scsiSenseCode.senseKey = SENSE_KEY_ABORTED_COMMAND;
                            entries->dstEntry[entries->numberOfEntries].scsiSenseCode.additionalSenseCode = 0x40;
                            entries->dstEntry[entries->numberOfEntries].scsiSenseCode.additionalSenseCodeQualifier = 0x83;
                            break;
                        case 4:
                            entries->dstEntry[entries->numberOfEntries].scsiSenseCode.senseKey = SENSE_KEY_HARDWARE_ERROR;
                            entries->dstEntry[entries->numberOfEntries].scsiSenseCode.additionalSenseCode = 0x40;
                            entries->dstEntry[entries->numberOfEntries].scsiSenseCode.additionalSenseCodeQualifier = 0x84;
                            break;
                        case 5:
                            entries->dstEntry[entries->numberOfEntries].scsiSenseCode.senseKey = SENSE_KEY_HARDWARE_ERROR;
                            entries->dstEntry[entries->numberOfEntries].scsiSenseCode.additionalSenseCode = 0x40;
                            entries->dstEntry[entries->numberOfEntries].scsiSenseCode.additionalSenseCodeQualifier = 0x85;
                            break;
                        case 6:
                            entries->dstEntry[entries->numberOfEntries].scsiSenseCode.senseKey = SENSE_KEY_HARDWARE_ERROR;
                            entries->dstEntry[entries->numberOfEntries].scsiSenseCode.additionalSenseCode = 0x40;
                            entries->dstEntry[entries->numberOfEntries].scsiSenseCode.additionalSenseCodeQualifier = 0x86;
                            break;
                        case 7:
                            entries->dstEntry[entries->numberOfEntries].scsiSenseCode.senseKey = SENSE_KEY_MEDIUM_ERROR;
                            entries->dstEntry[entries->numberOfEntries].scsiSenseCode.additionalSenseCode = 0x40;
                            entries->dstEntry[entries->numberOfEntries].scsiSenseCode.additionalSenseCodeQualifier = 0x87;
                            break;
                        case 8:
                            entries->dstEntry[entries->numberOfEntries].scsiSenseCode.senseKey = SENSE_KEY_HARDWARE_ERROR;
                            entries->dstEntry[entries->numberOfEntries].scsiSenseCode.additionalSenseCode = 0x40;
                            entries->dstEntry[entries->numberOfEntries].scsiSenseCode.additionalSenseCodeQualifier = 0x88;
                            break;
                        default://unspecified
                            break;
                        }
                        ++entries->numberOfEntries;
                    }
                    if(offset > descriptorLength)
                    {
                        uint32_t offsetCheck = 0;
                        //offset needs to subtract since we work backwards from the end (until rollover)
                        offset -= descriptorLength;
                        offsetCheck = offset;
                        #if ENABLE_DST_LOG_DEBUG
                        printf("\toffsetCheck = %u ", offsetCheck);
                        #endif
                        if(pageNumber > 0 && offsetCheck > C_CAST(uint32_t, (LEGACY_DRIVE_SEC_SIZE * pageNumber)))//this cast is extremely stupid. Apparently MSFT's UINT16_C doesn't actually force it to unsigned properly, so this generates an unneccessary warning
                        {   
                            offsetCheck -= (LEGACY_DRIVE_SEC_SIZE * pageNumber);
                            #if ENABLE_DST_LOG_DEBUG
                            printf("\tsubtracting %u", (LEGACY_DRIVE_SEC_SIZE * pageNumber));
                            #endif
                        }
                        #if ENABLE_DST_LOG_DEBUG
                        printf("\n");
                        #endif
                        if(offsetCheck < 4 || offsetCheck > UINT32_C(472))
                        {
                            if (pageNumber > 0)
                            {
                                --pageNumber;
                            }
                            else
                            {
                                pageNumber = lastPage;
                            }
                            offset = UINT32_C(472) + (pageNumber * LEGACY_DRIVE_SEC_SIZE);
                        }
                    }
                    else
                    {
                        #if ENABLE_DST_LOG_DEBUG
                        printf("\tsetting offset to 472 on last page read\n");
                        #endif
                        offset = UINT32_C(472) + (lastPage * LEGACY_DRIVE_SEC_SIZE);
                    }
                    ++counter;
                    if(entries->numberOfEntries >= MAX_DST_ENTRIES)
                    {
                        break;
                    }
                }
            }
        }
    }
    else if (is_SMART_Enabled(device) && is_SMART_Error_Logging_Supported(device) && SUCCESS == get_ATA_Log_Size(device, ATA_LOG_SMART_SELF_TEST_LOG, &logSize, false, true) && logSize > 0)
    {
        selfTestResults = C_CAST(uint8_t*, calloc_aligned(LEGACY_DRIVE_SEC_SIZE, sizeof(uint8_t), device->os_info.minimumAlignment));
        if (!selfTestResults)
        {
            return MEMORY_FAILURE;
        }
        //read the self tests results log with SMART read log
        if (SUCCESS == ata_SMART_Read_Log(device, ATA_LOG_SMART_SELF_TEST_LOG, selfTestResults, LEGACY_DRIVE_SEC_SIZE))
        {
            ret = SUCCESS;
            entries->logType = DST_LOG_TYPE_ATA;
            uint8_t selfTestIndex = selfTestResults[508];
            if (selfTestIndex > 0)
            {
                uint8_t descriptorLength = 24;
                uint8_t descriptorOffset = ((selfTestIndex * descriptorLength) - descriptorLength) + 2;
                uint8_t zeroCompare[24] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, };
                uint16_t offset = descriptorOffset;
                uint16_t counter = 0;//when this get's larger than our max, we need to break out of the loop. This is always incremented. - TJE
                while(counter < MAX_DST_ENTRIES /*&& counter < 21*/)//max of 21 dst entries in this log
                {
                    if (memcmp(&selfTestResults[offset], zeroCompare, descriptorLength))//invalid entires will be all zeros-TJE
                    {
                        entries->dstEntry[entries->numberOfEntries].descriptorValid = true;
                        entries->dstEntry[entries->numberOfEntries].selfTestRun = selfTestResults[offset];
                        entries->dstEntry[entries->numberOfEntries].selfTestExecutionStatus = selfTestResults[offset + 1];
                        entries->dstEntry[entries->numberOfEntries].lifetimeTimestamp = M_BytesTo2ByteValue(selfTestResults[offset + 3], selfTestResults[offset + 2]);
                        entries->dstEntry[entries->numberOfEntries].checkPointByte = selfTestResults[offset + 4];
                        entries->dstEntry[entries->numberOfEntries].lbaOfFailure = M_BytesTo4ByteValue(selfTestResults[offset + 8], selfTestResults[offset + 7], selfTestResults[offset + 6], selfTestResults[offset + 5]);
                        memcpy(&entries->dstEntry[entries->numberOfEntries].ataVendorSpecificData[0], &selfTestResults[offset + 9], 15);
                        //if LBA field is all F's, this is meant to signify an invalid value like T10 specs say to do - TJE
                        //filtering 28bit all F's and 32bit all F's since it is not clear exactly how many drives will report this invalid value -TJE
                        if (entries->dstEntry[entries->numberOfEntries].lbaOfFailure == MAX_28_BIT_LBA || entries->dstEntry[entries->numberOfEntries].lbaOfFailure == UINT32_MAX)
                        {
                            entries->dstEntry[entries->numberOfEntries].lbaOfFailure = UINT64_MAX;
                        }

                        //dummy up sense data...
                        switch (M_Nibble1(entries->dstEntry[entries->numberOfEntries].selfTestExecutionStatus))
                        {
                        case 0:
                        case 15:
                            entries->dstEntry[entries->numberOfEntries].scsiSenseCode.senseKey = SENSE_KEY_NO_ERROR;
                            entries->dstEntry[entries->numberOfEntries].scsiSenseCode.additionalSenseCode = 0;
                            entries->dstEntry[entries->numberOfEntries].scsiSenseCode.additionalSenseCodeQualifier = 0;
                            //NOTE: This is a workaround to clear out the LBA on success:
                            entries->dstEntry[entries->numberOfEntries].lbaOfFailure = UINT64_MAX;
                            break;
                        case 1:
                            entries->dstEntry[entries->numberOfEntries].scsiSenseCode.senseKey = SENSE_KEY_ABORTED_COMMAND;
                            entries->dstEntry[entries->numberOfEntries].scsiSenseCode.additionalSenseCode = 0x40;
                            entries->dstEntry[entries->numberOfEntries].scsiSenseCode.additionalSenseCodeQualifier = 0x81;
                            break;
                        case 2:
                            entries->dstEntry[entries->numberOfEntries].scsiSenseCode.senseKey = SENSE_KEY_ABORTED_COMMAND;
                            entries->dstEntry[entries->numberOfEntries].scsiSenseCode.additionalSenseCode = 0x40;
                            entries->dstEntry[entries->numberOfEntries].scsiSenseCode.additionalSenseCodeQualifier = 0x82;
                            break;
                        case 3:
                            entries->dstEntry[entries->numberOfEntries].scsiSenseCode.senseKey = SENSE_KEY_ABORTED_COMMAND;
                            entries->dstEntry[entries->numberOfEntries].scsiSenseCode.additionalSenseCode = 0x40;
                            entries->dstEntry[entries->numberOfEntries].scsiSenseCode.additionalSenseCodeQualifier = 0x83;
                            break;
                        case 4:
                            entries->dstEntry[entries->numberOfEntries].scsiSenseCode.senseKey = SENSE_KEY_HARDWARE_ERROR;
                            entries->dstEntry[entries->numberOfEntries].scsiSenseCode.additionalSenseCode = 0x40;
                            entries->dstEntry[entries->numberOfEntries].scsiSenseCode.additionalSenseCodeQualifier = 0x84;
                            break;
                        case 5:
                            entries->dstEntry[entries->numberOfEntries].scsiSenseCode.senseKey = SENSE_KEY_HARDWARE_ERROR;
                            entries->dstEntry[entries->numberOfEntries].scsiSenseCode.additionalSenseCode = 0x40;
                            entries->dstEntry[entries->numberOfEntries].scsiSenseCode.additionalSenseCodeQualifier = 0x85;
                            break;
                        case 6:
                            entries->dstEntry[entries->numberOfEntries].scsiSenseCode.senseKey = SENSE_KEY_HARDWARE_ERROR;
                            entries->dstEntry[entries->numberOfEntries].scsiSenseCode.additionalSenseCode = 0x40;
                            entries->dstEntry[entries->numberOfEntries].scsiSenseCode.additionalSenseCodeQualifier = 0x86;
                            break;
                        case 7:
                            entries->dstEntry[entries->numberOfEntries].scsiSenseCode.senseKey = SENSE_KEY_MEDIUM_ERROR;
                            entries->dstEntry[entries->numberOfEntries].scsiSenseCode.additionalSenseCode = 0x40;
                            entries->dstEntry[entries->numberOfEntries].scsiSenseCode.additionalSenseCodeQualifier = 0x87;
                            break;
                        case 8:
                            entries->dstEntry[entries->numberOfEntries].scsiSenseCode.senseKey = SENSE_KEY_HARDWARE_ERROR;
                            entries->dstEntry[entries->numberOfEntries].scsiSenseCode.additionalSenseCode = 0x40;
                            entries->dstEntry[entries->numberOfEntries].scsiSenseCode.additionalSenseCodeQualifier = 0x88;
                            break;
                        default://unspecified
                            break;
                        }
                        ++entries->numberOfEntries;
                    }                
                    if(offset > descriptorLength)
                    {
                        uint16_t offsetCheck = 0;
                        //offset needs to subtract since we work backwards from the end (until rollover)
                        offset -= descriptorLength;
                        offsetCheck = offset;
                        if(offsetCheck > LEGACY_DRIVE_SEC_SIZE)
                        {
                            offsetCheck -= LEGACY_DRIVE_SEC_SIZE;
                        }
                        if(offsetCheck < 2 || offsetCheck > 482)
                        {
                            //need to adjust offset to 482 (since we are rolling over back to beginning)
                            offset = 482;
                        }
                    }
                    else
                    {
                        //go to last descriptor
                        offset = 482;
                    }
                    ++counter;
                }
            }
        }
    }
    safe_Free_aligned(selfTestResults)
    return ret;
}

static int get_SCSI_DST_Log_Entries(tDevice *device, ptrDstLogEntries entries)
{
    int ret = NOT_SUPPORTED;
    uint8_t dstLog[LP_SELF_TEST_RESULTS_LEN] = { 0 };
    if (!entries)
    {
        return BAD_PARAMETER;
    }
    if (SUCCESS == scsi_Log_Sense_Cmd(device, false, LPC_CUMULATIVE_VALUES, LP_SELF_TEST_RESULTS, 0, 1, dstLog, LP_SELF_TEST_RESULTS_LEN))
    {
        uint8_t zeroCompare[16] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
        uint16_t pageLength = M_BytesTo2ByteValue(dstLog[2], dstLog[3]);
        ret = SUCCESS;
        entries->logType = DST_LOG_TYPE_SCSI;
        entries->numberOfEntries = 0;
        for (uint32_t offset = 4; offset < C_CAST(uint32_t, pageLength + UINT16_C(4)) && offset < LP_SELF_TEST_RESULTS_LEN && entries->numberOfEntries < MAX_DST_ENTRIES; offset += 20)
        {
            if (memcmp(&dstLog[offset + 4], zeroCompare, 16))//if this doesn't match, we have an entry...-TJE
            {
                entries->dstEntry[entries->numberOfEntries].descriptorValid = true;
                entries->dstEntry[entries->numberOfEntries].selfTestExecutionStatus = M_Nibble0(dstLog[offset + 4]) << 4;
                entries->dstEntry[entries->numberOfEntries].selfTestRun = M_Nibble1(dstLog[offset + 4]) >> 1;
                entries->dstEntry[entries->numberOfEntries].checkPointByte = dstLog[offset + 5];
                entries->dstEntry[entries->numberOfEntries].lifetimeTimestamp = M_BytesTo2ByteValue(dstLog[offset + 6], dstLog[offset + 7]);
                entries->dstEntry[entries->numberOfEntries].lbaOfFailure = M_BytesTo8ByteValue(dstLog[offset + 8], dstLog[offset + 9], dstLog[offset + 10], dstLog[offset + 11], dstLog[offset + 12], dstLog[offset + 13], dstLog[offset + 14], dstLog[offset + 15]);
                entries->dstEntry[entries->numberOfEntries].scsiSenseCode.senseKey = M_Nibble0(dstLog[offset + 16]);
                entries->dstEntry[entries->numberOfEntries].scsiSenseCode.additionalSenseCode = dstLog[offset + 17];
                entries->dstEntry[entries->numberOfEntries].scsiSenseCode.additionalSenseCodeQualifier = dstLog[offset + 18];
                entries->dstEntry[entries->numberOfEntries].scsiVendorSpecificByte = dstLog[offset + 19];
                ++entries->numberOfEntries;
            }
        }
    }
    return ret;
}

static int get_NVMe_DST_Log_Entries(tDevice *device, ptrDstLogEntries entries)
{
    int ret = NOT_SUPPORTED;
    if (!entries)
    {
        return BAD_PARAMETER;
    }
    if (is_Self_Test_Supported(device))
    {
        nvmeGetLogPageCmdOpts dstLogParms;
        memset(&dstLogParms, 0, sizeof(nvmeGetLogPageCmdOpts));
        uint8_t nvmeDSTLog[564] = { 0 };
        dstLogParms.addr = nvmeDSTLog;
        dstLogParms.dataLen = 564;
        dstLogParms.lid = 0x06;
        dstLogParms.nsid = UINT32_MAX;
        if (SUCCESS == nvme_Get_Log_Page(device, &dstLogParms))
        {
            ret = SUCCESS;
            entries->logType = DST_LOG_TYPE_NVME;
            entries->numberOfEntries = 0;
            for (uint32_t offset = 4; offset < 564 && entries->numberOfEntries < 20; offset += 28)//maximum of 20 NVMe DST log entires
            {
                //check if the entry is valid by checking for zeros
                uint8_t zeros[28] = { 0 };
                if (memcmp(zeros, &nvmeDSTLog[offset], 28) && M_Nibble0(nvmeDSTLog[offset + 0]) != 0x0F)//0F in NVMe is an unused entry.
                {
                    entries->dstEntry[entries->numberOfEntries].selfTestRun = M_Nibble1(nvmeDSTLog[offset + 0]);
                    entries->dstEntry[entries->numberOfEntries].selfTestExecutionStatus = M_Nibble0(nvmeDSTLog[offset + 0]);
                    entries->dstEntry[entries->numberOfEntries].segmentNumber = nvmeDSTLog[offset + 1];
                    entries->dstEntry[entries->numberOfEntries].powerOnHours = M_BytesTo8ByteValue(nvmeDSTLog[offset + 11], nvmeDSTLog[offset + 10], nvmeDSTLog[offset + 9], nvmeDSTLog[offset + 8], nvmeDSTLog[offset + 7], nvmeDSTLog[offset + 6], nvmeDSTLog[offset + 5], nvmeDSTLog[offset + 4]);
                    if (nvmeDSTLog[offset + 2] & BIT0)
                    {
                        entries->dstEntry[entries->numberOfEntries].nsidValid = true;
                        entries->dstEntry[entries->numberOfEntries].namespaceID = M_BytesTo4ByteValue(nvmeDSTLog[offset + 15], nvmeDSTLog[offset + 14], nvmeDSTLog[offset + 13], nvmeDSTLog[offset + 12]);
                    }
                    if (nvmeDSTLog[offset + 2] & BIT1)//check if flba is set
                    {
                        entries->dstEntry[entries->numberOfEntries].lbaOfFailure = M_BytesTo8ByteValue(nvmeDSTLog[offset + 23], nvmeDSTLog[offset + 22], nvmeDSTLog[offset + 21], nvmeDSTLog[offset + 20], nvmeDSTLog[offset + 19], nvmeDSTLog[offset + 18], nvmeDSTLog[offset + 17], nvmeDSTLog[offset + 16]);
                    }
                    else
                    {
                        //this is an invalid LBA value, so it can be filtered out with existing SCSI/ATA code
                        entries->dstEntry[entries->numberOfEntries].lbaOfFailure = UINT64_MAX;
                    }
                    if (nvmeDSTLog[offset + 2] & BIT2)
                    {
                        entries->dstEntry[entries->numberOfEntries].nvmeStatus.statusCodeTypeValid = true;
                        entries->dstEntry[entries->numberOfEntries].nvmeStatus.statusCodeType = nvmeDSTLog[offset + 24];
                    }
                    if (nvmeDSTLog[offset + 2] & BIT3)
                    {
                        entries->dstEntry[entries->numberOfEntries].nvmeStatus.statusCodeValid = true;
                        entries->dstEntry[entries->numberOfEntries].nvmeStatus.statusCode = nvmeDSTLog[offset + 25];
                    }
                    entries->dstEntry[entries->numberOfEntries].nvmeVendorSpecificWord = M_BytesTo2ByteValue(nvmeDSTLog[offset + 27], nvmeDSTLog[offset + 26]);
                    //increment the number of entries since we found another good one!
                    entries->dstEntry[entries->numberOfEntries].descriptorValid = true;
                    ++(entries->numberOfEntries);
                }
            }
        }
        else
        {
            ret = FAILURE;
        }
    }
    return ret;
}

int get_DST_Log_Entries(tDevice *device, ptrDstLogEntries entries)
{
    switch (device->drive_info.drive_type)
    {
    case ATA_DRIVE:
        return get_ATA_DST_Log_Entries(device, entries);
    case NVME_DRIVE:
        return get_NVMe_DST_Log_Entries(device, entries);
    case SCSI_DRIVE:
        return get_SCSI_DST_Log_Entries(device, entries);
    default:
        return NOT_SUPPORTED;
    }
}

int print_DST_Log_Entries(ptrDstLogEntries entries)
{
    if (!entries)
    {
        return BAD_PARAMETER;
    }
    printf("\n===DST Log===\n");
    if (entries->numberOfEntries == 0)
    {
        printf("DST Has never been run/no DST entries found.\n");
    }
    else
    {
        //test types ATA
        //short (offline)
        //short (captive)
        //extended (offline)
        //extended (captive)
        //conveyance (offline)
        //conveyance (captive)
        //offline data collect
        //test types SCSI
        //short (background)
        //short (foreground)
        //extended (background)
        //extended (foreground)
        //test types NVMe
        //short
        //extended
        
        //TODO: Need to change some wording or screen output for ATA & SCSI vs NVMe
        if (entries->logType == DST_LOG_TYPE_NVME)
        {
            //checkpoint = segment?
            printf(" # %-21s  %-9s  %-26s  %-14s  %-10s  %-9s\n", "Test", "Timestamp", "Execution Status", "Error LBA", "Segment#", "Status Info");
        }
        else
        {
            printf(" # %-21s  %-9s  %-26s  %-14s  %-10s  %-9s\n", "Test", "Timestamp", "Execution Status", "Error LBA", "Checkpoint", "Sense Info");
        }
        for (uint8_t iter = 0, counter = 0; iter < entries->numberOfEntries; ++iter)
        {
            if (!entries->dstEntry[iter].descriptorValid)
            {
                continue;
            }
            //#
            printf("%2" PRIu8 " ", iter + 1);
            //Test
#define SELF_TEST_RUN_STRING_MAX_LENGTH 22
            char selfTestRunString[SELF_TEST_RUN_STRING_MAX_LENGTH] = { 0 };
            if (entries->logType == DST_LOG_TYPE_ATA)
            {
                switch (entries->dstEntry[iter].selfTestRun)
                {
                case 0:
                    snprintf(selfTestRunString, SELF_TEST_RUN_STRING_MAX_LENGTH,  "Offline Data Collect");
                    break;
                case 1://short
                    snprintf(selfTestRunString, SELF_TEST_RUN_STRING_MAX_LENGTH,  "Short (offline)");
                    break;
                case 2://extended
                    snprintf(selfTestRunString, SELF_TEST_RUN_STRING_MAX_LENGTH,  "Extended (offline)");
                    break;
                case 3://conveyance
                    snprintf(selfTestRunString, SELF_TEST_RUN_STRING_MAX_LENGTH,  "Conveyance (offline)");
                    break;
                case 4://selective
                    snprintf(selfTestRunString, SELF_TEST_RUN_STRING_MAX_LENGTH,  "Selective (offline)");
                    break;
                case 0x81://short
                    snprintf(selfTestRunString, SELF_TEST_RUN_STRING_MAX_LENGTH,  "Short (captive)");
                    break;
                case 0x82://extended
                    snprintf(selfTestRunString, SELF_TEST_RUN_STRING_MAX_LENGTH,  "Extended (captive)");
                    break;
                case 0x83://conveyance
                    snprintf(selfTestRunString, SELF_TEST_RUN_STRING_MAX_LENGTH,  "Conveyance (captive)");
                    break;
                case 0x84://selective
                    snprintf(selfTestRunString, SELF_TEST_RUN_STRING_MAX_LENGTH,  "Selective (captive)");
                    break;
                default:
                    if ((entries->dstEntry[iter].selfTestRun >= 0x40 && entries->dstEntry[iter].selfTestRun <= 0x7E) || (entries->dstEntry[iter].selfTestRun >= 0x90 /*&& entries->dstEntry[iter].selfTestRun <= 0xFF*/))
                    {
                        snprintf(selfTestRunString, SELF_TEST_RUN_STRING_MAX_LENGTH,  "Vendor Specific - %"PRIX8"h", entries->dstEntry[iter].selfTestRun);
                    }
                    else
                    {
                        snprintf(selfTestRunString, SELF_TEST_RUN_STRING_MAX_LENGTH,  "Unknown - %"PRIX8"h", entries->dstEntry[iter].selfTestRun);
                    }
                    break;
                }
            }
            else if (entries->logType == DST_LOG_TYPE_SCSI)
            {
                switch (entries->dstEntry[iter].selfTestRun)
                {
                case 0:
                    snprintf(selfTestRunString, SELF_TEST_RUN_STRING_MAX_LENGTH,  "Unknown (Not in spec)");
                    break;
                case 1://short
                    snprintf(selfTestRunString, SELF_TEST_RUN_STRING_MAX_LENGTH,  "Short (background)");
                    break;
                case 2://extended
                    snprintf(selfTestRunString, SELF_TEST_RUN_STRING_MAX_LENGTH,  "Extended (background)");
                    break;
                case 5://short
                    snprintf(selfTestRunString, SELF_TEST_RUN_STRING_MAX_LENGTH,  "Short (foreground)");
                    break;
                case 6://extended
                    snprintf(selfTestRunString, SELF_TEST_RUN_STRING_MAX_LENGTH,  "Extended (foreground)");
                    break;
                default:
                    snprintf(selfTestRunString, SELF_TEST_RUN_STRING_MAX_LENGTH,  "Unknown - %"PRIX8"h", entries->dstEntry[iter].selfTestRun);
                    break;
                }
            }
            else if (entries->logType == DST_LOG_TYPE_NVME)
            {
                switch (entries->dstEntry[iter].selfTestRun)
                {
                case 0:
                    snprintf(selfTestRunString, SELF_TEST_RUN_STRING_MAX_LENGTH,  "Reserved");
                    break;
                case 1://short
                    snprintf(selfTestRunString, SELF_TEST_RUN_STRING_MAX_LENGTH,  "Short");
                    break;
                case 2://extended
                    snprintf(selfTestRunString, SELF_TEST_RUN_STRING_MAX_LENGTH,  "Extended");
                    break;
                case 0x0E://vendor specific
                    snprintf(selfTestRunString, SELF_TEST_RUN_STRING_MAX_LENGTH,  "Vendor Specific");
                    break;
                default:
                    snprintf(selfTestRunString, SELF_TEST_RUN_STRING_MAX_LENGTH,  "Unknown - %"PRIX8"h", entries->dstEntry[iter].selfTestRun);
                    break;
                }
            }
            else //print the number
            {
                snprintf(selfTestRunString, SELF_TEST_RUN_STRING_MAX_LENGTH,  "Unknown - %"PRIX8"h", entries->dstEntry[iter].selfTestRun);
            }
            printf("%-21s  ", selfTestRunString);
            //Timestamp
            printf("%-9"PRIu32"  ", entries->dstEntry[iter].lifetimeTimestamp);
            //Execution Status
#define SELF_TEST_EXECUTION_STATUS_MAX_LENGTH 30
            char status[SELF_TEST_EXECUTION_STATUS_MAX_LENGTH] = { 0 };
            uint8_t percentRemaining = 0;
            if (entries->logType == DST_LOG_TYPE_ATA)
            {
                percentRemaining = M_Nibble0(entries->dstEntry[iter].selfTestExecutionStatus) * 10;
            }
            if (entries->logType == DST_LOG_TYPE_NVME)
            {
                switch (M_Nibble1(entries->dstEntry[iter].selfTestExecutionStatus))
                {
                case 0:
                    snprintf(status, SELF_TEST_EXECUTION_STATUS_MAX_LENGTH, "No Error");
                    break;
                case 1:
                    snprintf(status, SELF_TEST_EXECUTION_STATUS_MAX_LENGTH, "Aborted by command");
                    break;
                case 2:
                    snprintf(status, SELF_TEST_EXECUTION_STATUS_MAX_LENGTH, "Aborted by controller reset");
                    break;
                case 3:
                    snprintf(status, SELF_TEST_EXECUTION_STATUS_MAX_LENGTH, "Aborted by namespace removal");
                    break;
                case 4:
                    snprintf(status, SELF_TEST_EXECUTION_STATUS_MAX_LENGTH, "Aborted by NVM format");
                    break;
                case 5:
                    snprintf(status, SELF_TEST_EXECUTION_STATUS_MAX_LENGTH, "Unknown/Fatal Error");
                    break;
                case 6:
                    snprintf(status, SELF_TEST_EXECUTION_STATUS_MAX_LENGTH, "Unknown Segment Failure");
                    break;
                case 7:
                    snprintf(status, SELF_TEST_EXECUTION_STATUS_MAX_LENGTH, "Failed on segment %" PRIu8 "", entries->dstEntry[iter].segmentNumber);
                    break;
                case 8:
                    snprintf(status, SELF_TEST_EXECUTION_STATUS_MAX_LENGTH, "Aborted for Unknown Reason");
                    break;
                default:
                    snprintf(status, SELF_TEST_EXECUTION_STATUS_MAX_LENGTH, "Reserved");
                    break;
                }
            }
            else
            {
                switch (M_Nibble1(entries->dstEntry[iter].selfTestExecutionStatus))
                {
                case 0:
                    snprintf(status, SELF_TEST_EXECUTION_STATUS_MAX_LENGTH, "Success");
                    break;
                case 1:
                    snprintf(status, SELF_TEST_EXECUTION_STATUS_MAX_LENGTH, "Aborted by host");
                    break;
                case 2:
                    snprintf(status, SELF_TEST_EXECUTION_STATUS_MAX_LENGTH, "Interrupted by reset");
                    break;
                case 3:
                    snprintf(status, SELF_TEST_EXECUTION_STATUS_MAX_LENGTH, "Fatal Error - Unknown");
                    break;
                case 4:
                    snprintf(status, SELF_TEST_EXECUTION_STATUS_MAX_LENGTH, "Unknown Failure Type");
                    break;
                case 5:
                    snprintf(status, SELF_TEST_EXECUTION_STATUS_MAX_LENGTH, "Electrical Failure");
                    break;
                case 6:
                    snprintf(status, SELF_TEST_EXECUTION_STATUS_MAX_LENGTH, "Servo/Seek Failure");
                    break;
                case 7:
                    snprintf(status, SELF_TEST_EXECUTION_STATUS_MAX_LENGTH, "Read Failure");
                    break;
                case 8:
                    snprintf(status, SELF_TEST_EXECUTION_STATUS_MAX_LENGTH, "Handling Damage");
                    break;
                case 0xF:
                    snprintf(status, SELF_TEST_EXECUTION_STATUS_MAX_LENGTH, "In progress");
                    break;
                default:
                    snprintf(status, SELF_TEST_EXECUTION_STATUS_MAX_LENGTH, "Reserved");
                    break;
                }
            }
            if (percentRemaining > 0)
            {
                char percentRemainingString[8] = { 0 };
                snprintf(percentRemainingString, 8, " (%" PRIu8 "%%)", percentRemaining);
                common_String_Concat(status, SELF_TEST_EXECUTION_STATUS_MAX_LENGTH, percentRemainingString);
            }
            printf("%-26s  ", status);
            //Error LBA
#define SELF_TEST_ERROR_LBA_STRING_MAX_LENGTH 21
            char errorLBAString[SELF_TEST_ERROR_LBA_STRING_MAX_LENGTH] = { 0 };
            if (entries->dstEntry[iter].lbaOfFailure == UINT64_MAX)
            {
                snprintf(errorLBAString, SELF_TEST_ERROR_LBA_STRING_MAX_LENGTH, "None");
            }
            else
            {
                snprintf(errorLBAString, SELF_TEST_ERROR_LBA_STRING_MAX_LENGTH, "%" PRIu64, entries->dstEntry[iter].lbaOfFailure);
            }
            printf("%-14s  ", errorLBAString);
            //Checkpoint/Segment number
            printf("%-10" PRIX8 "  ", entries->dstEntry[iter].checkPointByte);
            //Sense Info
#define SELF_TEST_SENSE_INFO_STRING_MAX_LENGTH 21
            char senseInfoString[SELF_TEST_SENSE_INFO_STRING_MAX_LENGTH] = { 0 };
            if (entries->logType == DST_LOG_TYPE_NVME)
            {
                //SCT - SC
                char sctVal[10] = { 0 };
                char scVal[10] = { 0 };
                if (entries->dstEntry[iter].nvmeStatus.statusCodeTypeValid)
                {
                    snprintf(sctVal, 10, "%02" PRIX8 "", entries->dstEntry[iter].nvmeStatus.statusCodeType);
                }
                else
                {
                    snprintf(sctVal, 10, "NA");
                }
                if (entries->dstEntry[iter].nvmeStatus.statusCodeValid)
                {
                    snprintf(sctVal, 10, "%02" PRIX8 "", entries->dstEntry[iter].nvmeStatus.statusCode);
                }
                else
                {
                    snprintf(scVal, 10, "NA");
                }
                snprintf(senseInfoString, SELF_TEST_SENSE_INFO_STRING_MAX_LENGTH, "%s/%s", sctVal, scVal);
            }
            else
            {
                snprintf(senseInfoString, SELF_TEST_SENSE_INFO_STRING_MAX_LENGTH, "%02"PRIX8"/%02"PRIX8"/%02"PRIX8, entries->dstEntry[iter].scsiSenseCode.senseKey, entries->dstEntry[iter].scsiSenseCode.additionalSenseCode, entries->dstEntry[iter].scsiSenseCode.additionalSenseCodeQualifier);
            }
            printf("%-9s\n", senseInfoString);
            ++counter;
        }
        printf("NOTE: DST Log entries are printed out in order from newest to oldest based ATA/SCSI/NVMe\n");
        printf("      specifications on where to find the latest entry.\n\n");
    }
    return SUCCESS;
}
