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
// \file format.c
// \brief This file defines the functions for performing some format unit operations

#include "format.h"
#include "logs.h"

bool is_Format_Unit_Supported(tDevice *device, bool *fastFormatSupported)
{
    uint8_t formatSupportData[10] = { 0 };
    if (fastFormatSupported)
    {
        *fastFormatSupported = false;//make sure this defaults to false
    }
    if (device->drive_info.scsiVersion >= SCSI_VERSION_SPC_3 && SUCCESS == scsi_Report_Supported_Operation_Codes(device, false, 1, SCSI_FORMAT_UNIT_CMD, 0, 10, formatSupportData))
    {
        //uint16_t cdbSize = M_BytesTo2ByteValue(formatSupportData[2], formatSupportData[3]);
        uint8_t supportField = formatSupportData[1] & 0x07;//only need bits 2:0
        switch (supportField)
        {
        case 0://information not available...try again?
        case 1://not supported by device server
            return false;
        case 3://supported in conformance with a SCSI standard
        case 5://supported in a vendor specific manor
            break;
        case 4://reserved
        case 2://reserved
        default:
            return false;
        }
        //if we made it here, then it's at least supported...not check the bit field for fast format support
        if (fastFormatSupported)//make sure the pointer is valid
        {
            if (!(formatSupportData[7] == 0xFF && formatSupportData[8] == 0xFF))//if both these bytes are FFh, then the drive conforms to SCSI2 where this was the "interleave" field
            {
                if (formatSupportData[8] & 0x03)//checks that fast format bits are available for use.
                {
                    *fastFormatSupported = true;
                }
            }
        }
        return true;
    }
    else if (device->drive_info.drive_type == SCSI_DRIVE && device->drive_info.interface_type == SCSI_INTERFACE)
    {
        //hack for old scsi drives that don't support the report supported op codes command. - TJE
        return true;
    }
    else
    {
        return false;
    }
    return false;
}

int get_Format_Progress(tDevice *device, double *percentComplete)
{
    uint8_t senseData[SPC3_SENSE_LEN] = { 0 };
    *percentComplete = 0.0;
    if (SUCCESS == scsi_Request_Sense_Cmd(device, false, senseData, SPC3_SENSE_LEN))
    {
        uint8_t senseKey = 0, asc = 0, ascq = 0, fru = 0;
        get_Sense_Key_ASC_ASCQ_FRU(senseData, SPC3_SENSE_LEN, &senseKey, &asc, &ascq, &fru);
        if (senseKey == SENSE_KEY_NOT_READY)
        {
            if (asc == 0x04 && ascq == 0x04)
            {
                //get progress
                *percentComplete = (M_BytesTo2ByteValue(senseData[16], senseData[17]) * 100.0) / 65536.0;
                return IN_PROGRESS;
            }
            else
            {
                return UNKNOWN;
            }
        }
        else if (senseKey == SENSE_KEY_NO_ERROR)
        {
            return SUCCESS;
        }
        else
        {
            return FAILURE;
        }
    }
    else
    {
        return FAILURE;
    }
}

int show_Format_Unit_Progress(tDevice *device)
{
    int ret = UNKNOWN;
    double percentComplete = 0;

    ret = get_Format_Progress(device, &percentComplete);

    if (ret == IN_PROGRESS)
    {
        printf("\tFormat Unit Progress = %3.2f%% \n", percentComplete);
    }
    else if (ret == SUCCESS)
    {
        printf("\tFormat Unit command is not currently in progress. It is either complete or has not been run.\n");
    }
    else
    {
        printf("\tError occurred while retrieving format unit progress!\n");
    }
    return ret;
}

int run_Format_Unit(tDevice *device, runFormatUnitParameters formatParameters, bool pollForProgress)
{
    int ret = SUCCESS;
    uint8_t *dataBuf = NULL;
    uint32_t dataSize = 4;//assume short list for now
    bool longList = false;
    bool fastFormatSupported = false;//not used yet - TJE
    bool formatSupported = is_Format_Unit_Supported(device, &fastFormatSupported);
    uint8_t fmtpInfo = 0;//set protection information to zero
    uint8_t defectListFormat = 0;
    bool initializationPattern = false;
    uint32_t offset = 2;//for filling in parameter data
    uint8_t patternType = 0;
    //validate the input parameters first
    //start with flags in the cdb
    if (!formatParameters.currentBlockSize && formatParameters.newBlockSize == 0)
    {
        return BAD_PARAMETER;
    }
    if (formatParameters.gList && formatParameters.glistSize == 0)
    {
        return BAD_PARAMETER;
    }
    if (formatParameters.gList)
    {
        defectListFormat = formatParameters.gList[1] & 0x03;
        if (formatParameters.glistSize > 0xFFFF && !longList)
        {
            dataSize += 4;
            longList = true;
            offset = 4;
        }
        dataSize += formatParameters.glistSize;
    }
    if (!formatParameters.defaultFormat && (formatParameters.pattern || formatParameters.securityInitialize))
    {
        initializationPattern = true;
        dataSize += 4;
        if (formatParameters.pattern)
        {
            patternType = 1;
            dataSize += formatParameters.patternLength;
        }
        else
        {
            formatParameters.patternLength = 0;
        }
    }
    //check if format is supported
    if (!formatSupported || formatParameters.protectionType > 3)
    {
        return NOT_SUPPORTED;
    }
    if ((formatParameters.protectionType == 2 || formatParameters.protectionType == 3) && formatParameters.protectionIntervalExponent != 0)
    {
        dataSize += 4;
        longList = true;
        offset = 4;
    }
    //dataSize += 1;//adding 1 to make sure we don't go over the end of out memory
    //allocate memory
    dataBuf = (uint8_t*)calloc(dataSize *sizeof(uint8_t), sizeof(uint8_t));
    if (!dataBuf)
    {
        return MEMORY_FAILURE;
    }
    //now flags that go in parameter data
    //bytes 0 & 1 are the same between short and long parameter headers
    dataBuf[0] = 0;//no protection field stuff to set for now
    if (!formatParameters.disableImmediate)
    {
        dataBuf[1] = BIT1;//immediate bit
    }
    if (!formatParameters.defaultFormat)
    {
        dataBuf[1] |= BIT7;//set FOV bit
        if (formatParameters.disableCertification)
        {
            dataBuf[1] |= BIT5;//dcrt bit
        }
        if (formatParameters.disablePrimaryList)
        {
            dataBuf[1] |= BIT6;//dpry bit
        }
        if (formatParameters.stopOnListError)
        {
            dataBuf[1] |= BIT4;//stpf bit
        }
        if (initializationPattern)
        {
            dataBuf[1] |= BIT3;//ip bit
        }
    }
    if (!formatParameters.changeProtectionType)
    {
        formatParameters.protectionType = device->drive_info.currentProtectionType;
        formatParameters.protectionIntervalExponent = device->drive_info.piExponent;
    }
    if (!(formatParameters.defaultFormat && formatParameters.disableImmediate))//Only set the fmtpInfo bit if we are sending data to the drive via parameter list...otherwise this is an illegal combination
    {
        //set up protection fields
        switch (formatParameters.protectionType)
        {
        case 1://type 1
            fmtpInfo = 2;//01b
            //protection field usage should be left zero
            break;
        case 2://type 2
            fmtpInfo = 3;//11b
            //protection field usage should be left zero
            break;
        case 3://type 3
            fmtpInfo = 3;//11b
            dataBuf[1] |= BIT0;//001b
            break;
        case 0://no protection 
        default://don't set protection. There is a condition before this that should catch this error.
            fmtpInfo = 0;
            break;
        }
    }
    if (longList)
    {
        if ((formatParameters.protectionType == 2 || formatParameters.protectionType == 3) && formatParameters.protectionIntervalExponent != 0)
        {
            //P_I_Information should be left as zero always! the exponent is in the lower nibble of this same byte
            dataBuf[3] = M_Nibble0(formatParameters.protectionIntervalExponent);
        }
        //set defect list length
        dataBuf[offset] = M_Byte3(formatParameters.glistSize);
        dataBuf[offset + 1] = M_Byte2(formatParameters.glistSize);
        dataBuf[offset + 2] = M_Byte1(formatParameters.glistSize);
        dataBuf[offset + 3] = M_Byte0(formatParameters.glistSize);
        offset += 4;
    }
    else
    {
        dataBuf[offset] = M_Byte1(formatParameters.glistSize);
        dataBuf[offset + 1] = M_Byte0(formatParameters.glistSize);
        offset += 2;
    }
    if (initializationPattern)
    {
        if (formatParameters.securityInitialize)
        {
            dataBuf[offset] |= BIT5;
        }
        dataBuf[offset + 1] = patternType;
        dataBuf[offset + 2] = M_Byte1(formatParameters.patternLength);
        dataBuf[offset + 3] = M_Byte0(formatParameters.patternLength);
        offset += 4;
        if (formatParameters.pattern && formatParameters.patternLength > 0)
        {
            //copy pattern into buffer
            memcpy(&dataBuf[offset], formatParameters.pattern, formatParameters.patternLength);
            offset += formatParameters.patternLength;
        }
    }
    if (formatParameters.gList)
    {
        memcpy(&dataBuf[offset], formatParameters.gList, formatParameters.glistSize);
        offset += formatParameters.glistSize;
    }
    dataSize = offset;
    //if they want to change the sector size, we need to do a mode select command
    if (!formatParameters.currentBlockSize || formatParameters.newMaxLBA)
    {
        bool modeSelect10 = true;
        uint8_t modeParameterData[24] = { 0 };
        //try mode sense 10 with LongLBA bit set...if that fails, try mode sense 6
        if (SUCCESS != scsi_Mode_Sense_10(device, 0, 24, 0, false, true, MPC_CURRENT_VALUES, modeParameterData))
        {
            //try mode sense 10 without the longLBA bit now
            if (SUCCESS != scsi_Mode_Sense_10(device, 0, 24, 0, false, false, MPC_CURRENT_VALUES, modeParameterData))
            {
                modeSelect10 = false;
                //all else fails, try mode sense 6
                if (SUCCESS != scsi_Mode_Sense_6(device, 0, 12, 0, false, MPC_CURRENT_VALUES, modeParameterData))
                {
                    safe_Free(dataBuf);
                    return NOT_SUPPORTED;
                }
            }
        }
        //modify the BD
        uint16_t blockDescriptorLength = 0;
        uint8_t blockDescriptorOffset = 0;
        //set the block size to the new block size and the max lba to all F's...they can change the sector size later with another command if they want to
        if (modeSelect10)
        {
            blockDescriptorOffset = MODE_PARAMETER_HEADER_10_LEN;
            blockDescriptorLength = M_BytesTo2ByteValue(modeParameterData[6], modeParameterData[7]);
        }
        else //mode sense 6
        {
            blockDescriptorOffset = MODE_PARAMETER_HEADER_6_LEN;
            blockDescriptorLength = modeParameterData[3];
        }
        if (blockDescriptorLength == 8)
        {
            //short block descriptor
            //set the LBA to all Fs to reset to maximum LBA of the drive
            if (formatParameters.newMaxLBA)
            {
                modeParameterData[blockDescriptorOffset + 0] = M_Byte3(formatParameters.newMaxLBA);
                modeParameterData[blockDescriptorOffset + 1] = M_Byte2(formatParameters.newMaxLBA);
                modeParameterData[blockDescriptorOffset + 2] = M_Byte1(formatParameters.newMaxLBA);
                modeParameterData[blockDescriptorOffset + 3] = M_Byte0(formatParameters.newMaxLBA);
            }
            else
            {
                modeParameterData[blockDescriptorOffset + 0] = 0xFF;
                modeParameterData[blockDescriptorOffset + 1] = 0xFF;
                modeParameterData[blockDescriptorOffset + 2] = 0xFF;
                modeParameterData[blockDescriptorOffset + 3] = 0xFF;
            }
            //1 reserved byte (don't touch it)
            //set logical block length in bytes 5 to 7
            if (!formatParameters.currentBlockSize)
            {
                modeParameterData[blockDescriptorOffset + 5] = M_Byte2(formatParameters.newBlockSize);
                modeParameterData[blockDescriptorOffset + 6] = M_Byte1(formatParameters.newBlockSize);
                modeParameterData[blockDescriptorOffset + 7] = M_Byte0(formatParameters.newBlockSize);
            }
        }
        else if (blockDescriptorLength == 16)
        {
            //long block descriptor
            //set the LBA to all Fs to reset to maximum LBA of the drive
            if (formatParameters.newMaxLBA)
            {
                modeParameterData[blockDescriptorOffset + 0] = M_Byte7(formatParameters.newMaxLBA);
                modeParameterData[blockDescriptorOffset + 1] = M_Byte6(formatParameters.newMaxLBA);
                modeParameterData[blockDescriptorOffset + 2] = M_Byte5(formatParameters.newMaxLBA);
                modeParameterData[blockDescriptorOffset + 3] = M_Byte4(formatParameters.newMaxLBA);
                modeParameterData[blockDescriptorOffset + 4] = M_Byte3(formatParameters.newMaxLBA);
                modeParameterData[blockDescriptorOffset + 5] = M_Byte2(formatParameters.newMaxLBA);
                modeParameterData[blockDescriptorOffset + 6] = M_Byte1(formatParameters.newMaxLBA);
                modeParameterData[blockDescriptorOffset + 7] = M_Byte0(formatParameters.newMaxLBA);
            }
            else
            {
                modeParameterData[blockDescriptorOffset + 0] = 0xFF;
                modeParameterData[blockDescriptorOffset + 1] = 0xFF;
                modeParameterData[blockDescriptorOffset + 2] = 0xFF;
                modeParameterData[blockDescriptorOffset + 3] = 0xFF;
                modeParameterData[blockDescriptorOffset + 4] = 0xFF;
                modeParameterData[blockDescriptorOffset + 5] = 0xFF;
                modeParameterData[blockDescriptorOffset + 6] = 0xFF;
                modeParameterData[blockDescriptorOffset + 7] = 0xFF;
            }
            //8 reserved bytes (don't touch them)
            //set logical block length in bytes 12 to 15
            if (!formatParameters.currentBlockSize)
            {
                modeParameterData[blockDescriptorOffset + 12] = M_Byte3(formatParameters.newBlockSize);
                modeParameterData[blockDescriptorOffset + 13] = M_Byte2(formatParameters.newBlockSize);
                modeParameterData[blockDescriptorOffset + 14] = M_Byte1(formatParameters.newBlockSize);
                modeParameterData[blockDescriptorOffset + 15] = M_Byte0(formatParameters.newBlockSize);
            }
        }
        else
        {
            //invalid block descriptor length
            safe_Free(dataBuf);
            return NOT_SUPPORTED;
        }
        //now send a mode select command
        if (modeSelect10)
        {
            ret = scsi_Mode_Select_10(device, 24, false, true, false, modeParameterData, 24); //turning off page format bit due to reading page 0 above
        }
        else
        {
            ret = scsi_Mode_Select_6(device, 12, false, true, false, modeParameterData, 12); //turning off page format bit due to reading page 0 above
        }
    }
    if (ret == SUCCESS)
    {
        uint32_t formatCommandTimeout = 15;
        if (formatParameters.disableImmediate)
        {
            if (formatParameters.formatType != FORMAT_STD_FORMAT)
            {
                formatCommandTimeout = 3600;//fast format should complete in a few minutes, but setting a 1 hour timeout leaves plenty of room for error.
            }
            else
            {
                formatCommandTimeout = 86400;//setting to 1 day worth of time...nothing should take this long...yet. Doing this because Windows doesn't like setting a max time like we were. UINT32_MAX;
            }
        }
        //send the format command
        if (formatParameters.defaultFormat && formatParameters.disableImmediate)
        {
            ret = scsi_Format_Unit(device, fmtpInfo, longList, false, formatParameters.completeList, defectListFormat, 0, NULL, 0, formatParameters.formatType, formatCommandTimeout);
        }
        else
        {
            ret = scsi_Format_Unit(device, fmtpInfo, longList, true, formatParameters.completeList, defectListFormat, 0, dataBuf, dataSize, formatParameters.formatType, formatCommandTimeout);
        }

        //poll for progress
        if (pollForProgress && ret == SUCCESS)
        {
            double progress = 0;
            uint32_t delayTimeSeconds = 300;
            if (is_SSD(device))
            {
                delayTimeSeconds = 5;
            }
            switch (formatParameters.formatType)
            {
            case FORMAT_FAST_WRITE_NOT_REQUIRED:
            case FORMAT_FAST_WRITE_REQUIRED:
                delayTimeSeconds = 5;
                break;
            default:
                break;
            }
            delay_Seconds(2); //2 second delay to make sure it starts (and on SSD this may be enough for it to finish immediately)
            if (VERBOSITY_QUIET < device->deviceVerbosity)
            {
                uint8_t seconds = 0, minutes = 0, hours = 0;
                convert_Seconds_To_Displayable_Time(delayTimeSeconds, NULL, NULL, &hours, &minutes, &seconds);
                printf("Progress will be updated every ");
                print_Time_To_Screen(NULL, NULL, &hours, &minutes, &seconds);
                printf("\n");
            }
            while (IN_PROGRESS == get_Format_Progress(device, &progress))
            {
                if (VERBOSITY_QUIET < device->deviceVerbosity)
                {
                    printf("\r\tPercent Complete: %0.02f%%", progress);
                    fflush(stdout);
                }
                delay_Seconds(delayTimeSeconds); //time set above
            }
            ret = get_Format_Progress(device, &progress);
            if (ret == SUCCESS && progress < 100.00)
            {
                if (VERBOSITY_QUIET < device->deviceVerbosity)
                {
                    printf("\r\tPercent Complete: 100.00%%\n");
                    fflush(stdout);
                }
            }
            else if (VERBOSITY_QUIET < device->deviceVerbosity)
            {
                printf("\n");
            }
        }
        else
        {
            //check if there was an invalid parameter field specifying the security initialize bit...if so, print a message and return not supported - TJE
        }
    }
    safe_Free(dataBuf);
    return ret;
}

int get_Format_Status(tDevice *device, ptrFormatStatus formatStatus)
{
    int ret = SUCCESS;
    if (!device || !formatStatus)
    {
        return BAD_PARAMETER;
    }
    //Need to allocate enough memory to read all parameters (0 - 5)
    //4 for header
    //4 + 255 for param 0
    //4 + 8 for param 1
    //4 + 8 for param 2
    //4 + 8 for param 3
    //4 + 4 for param 4
    uint8_t *formatStatusPage = (uint8_t*)calloc(307, sizeof(uint8_t));
    if (!formatStatusPage)
    {
        return MEMORY_FAILURE;
    }
    if (SUCCESS == scsi_Log_Sense_Cmd(device, false, LPC_CUMULATIVE_VALUES, LP_FORMAT_STATUS_LOG_PAGE, 0, 0, formatStatusPage, 307))
    {
        //TODO: Parameters will be all F's when the data is not available or new or the last format failed
        if (M_GETBITRANGE(formatStatusPage[0], 5, 0) == LP_FORMAT_STATUS_LOG_PAGE && !(formatStatusPage[0] & BIT6) && formatStatusPage[0] & BIT7 && formatStatusPage[1] == 0)//make sure we got the right page!
        {
            //got the data, so let's loop through it.
            uint16_t pageLength = M_BytesTo2ByteValue(formatStatusPage[2], formatStatusPage[3]);
            uint32_t offset = 4;//start here to begin looking at the parameters
            uint8_t parameterLength = 0;
            bool lastFormatUnitAllFs = false, grownDefectsDuringCertificationAllFs = false, totalBlockReassignsDuringFormatAllFs = false, totalNewBlocksReassignedAllFs = false, powerOnMinutesSinceLastFormatAllFs = false;
            for (; offset < (pageLength + 4U) && offset < 307U; offset += parameterLength + 4)
            {
                uint16_t parameterCode = M_BytesTo2ByteValue(formatStatusPage[offset + 0], formatStatusPage[offset + 1]);
                parameterLength = formatStatusPage[offset + 3];
                if (parameterLength == 0)
                {
                    //stop the infinite loop before it would start
                    break;
                }
                switch (parameterCode)
                {
                case 0://format data out
                    formatStatus->lastFormatParametersValid = true;
                    {
                        uint8_t *allFs = (uint8_t*)calloc(formatStatusPage[offset + 3], sizeof(uint8_t*));
                        if (allFs)
                        {
                            if (memcmp(allFs, &formatStatusPage[offset + 4], formatStatusPage[offset + 3]) == 0)
                            {
                                lastFormatUnitAllFs = true;
                            }
                            safe_Free(allFs);
                        }
                        else
                        {
                            if (formatStatusPage[offset + 3] >= 4)
                            {
                                formatStatus->lastFormatData.protectionFieldUsage = M_GETBITRANGE(formatStatusPage[offset + 4], 2, 0);
                                formatStatus->lastFormatData.formatOptionsValid = formatStatusPage[offset + 5] & BIT7;
                                formatStatus->lastFormatData.disablePrimaryList = formatStatusPage[offset + 5] & BIT6;
                                formatStatus->lastFormatData.disableCertify = formatStatusPage[offset + 5] & BIT5;
                                formatStatus->lastFormatData.stopFormat = formatStatusPage[offset + 5] & BIT4;
                                formatStatus->lastFormatData.initializationPattern = formatStatusPage[offset + 5] & BIT3;
                                formatStatus->lastFormatData.obsoleteDisableSaveParameters = formatStatusPage[offset + 5] & BIT2;
                                formatStatus->lastFormatData.immediateResponse = formatStatusPage[offset + 5] & BIT1;
                                formatStatus->lastFormatData.vendorSpecific = formatStatusPage[offset + 5] & BIT0;
                            }
                            switch (formatStatusPage[offset + 3])//based on the length of the data, it may be a short or long list...
                            {
                            case 4://short list
                                formatStatus->lastFormatData.defectListLength = M_BytesTo2ByteValue(formatStatusPage[offset + 6], formatStatusPage[offset + 7]);
                                break;
                            case 8://long list
                                formatStatus->lastFormatData.isLongList = true;
                                formatStatus->lastFormatData.defectListLength = M_BytesTo4ByteValue(formatStatusPage[offset + 8], formatStatusPage[offset + 9], formatStatusPage[offset + 10], formatStatusPage[offset + 11]);
                                formatStatus->lastFormatData.p_i_information = M_Nibble1(formatStatusPage[offset + 7]);
                                formatStatus->lastFormatData.protectionIntervalExponent = M_Nibble0(formatStatusPage[offset + 7]);
                                break;
                            default://unknown
                                break;
                            }
                        }
                    }
                    break;
                case 1://grown defects during certification
                    formatStatus->grownDefectsDuringCertificationValid = true;
                    formatStatus->grownDefectsDuringCertification = M_BytesTo8ByteValue(formatStatusPage[offset + 4], formatStatusPage[offset + 5], formatStatusPage[offset + 6], formatStatusPage[offset + 7], formatStatusPage[offset + 8], formatStatusPage[offset + 9], formatStatusPage[offset + 10], formatStatusPage[offset + 11]);
                    if (formatStatus->grownDefectsDuringCertification == UINT64_MAX)
                    {
                        grownDefectsDuringCertificationAllFs = true;
                        formatStatus->grownDefectsDuringCertificationValid = false;
                    }
                    break;
                case 2://total blocks reassigned during format
                    formatStatus->totalBlockReassignsDuringFormatValid = true;
                    formatStatus->totalBlockReassignsDuringFormat = M_BytesTo8ByteValue(formatStatusPage[offset + 4], formatStatusPage[offset + 5], formatStatusPage[offset + 6], formatStatusPage[offset + 7], formatStatusPage[offset + 8], formatStatusPage[offset + 9], formatStatusPage[offset + 10], formatStatusPage[offset + 11]);
                    if (formatStatus->totalBlockReassignsDuringFormat == UINT64_MAX)
                    {
                        totalBlockReassignsDuringFormatAllFs = true;
                        formatStatus->totalBlockReassignsDuringFormatValid = false;
                    }
                    break;
                case 3://total new blocks reassigned
                    formatStatus->totalNewBlocksReassignedValid = true;
                    formatStatus->totalNewBlocksReassigned = M_BytesTo8ByteValue(formatStatusPage[offset + 4], formatStatusPage[offset + 5], formatStatusPage[offset + 6], formatStatusPage[offset + 7], formatStatusPage[offset + 8], formatStatusPage[offset + 9], formatStatusPage[offset + 10], formatStatusPage[offset + 11]);
                    if (formatStatus->totalNewBlocksReassigned == UINT64_MAX)
                    {
                        totalNewBlocksReassignedAllFs = true;
                        formatStatus->totalNewBlocksReassignedValid = false;
                    }
                    break;
                case 4://power on minutes since format
                    formatStatus->powerOnMinutesSinceFormatValid = true;
                    formatStatus->powerOnMinutesSinceFormat = M_BytesTo4ByteValue(formatStatusPage[offset + 4], formatStatusPage[offset + 5], formatStatusPage[offset + 6], formatStatusPage[offset + 7]);
                    if (formatStatus->powerOnMinutesSinceFormat == UINT32_MAX)
                    {
                        powerOnMinutesSinceLastFormatAllFs = true;
                        formatStatus->powerOnMinutesSinceFormatValid = false;
                    }
                    break;
                default:
                    break;
                }
            }
            formatStatus->formatParametersAllFs = false;
            //TODO: should we handle setting the flag below if we didn't get all the parameters? They are marked mandatory, but that doesn't mean much since there are many times that mandatory support is missing on various deviecs.
            if (lastFormatUnitAllFs && grownDefectsDuringCertificationAllFs && totalBlockReassignsDuringFormatAllFs && totalNewBlocksReassignedAllFs && powerOnMinutesSinceLastFormatAllFs)
            {
                formatStatus->formatParametersAllFs = true;
            }
        }
        else
        {
            formatStatus->grownDefectsDuringCertificationValid = false;
            formatStatus->lastFormatParametersValid = false;
            formatStatus->powerOnMinutesSinceFormatValid = false;
            formatStatus->totalBlockReassignsDuringFormatValid = false;
            formatStatus->totalNewBlocksReassignedValid = false;
            ret = NOT_SUPPORTED;
        }
        safe_Free(formatStatusPage);
    }
    else
    {
        formatStatus->grownDefectsDuringCertificationValid = false;
        formatStatus->lastFormatParametersValid = false;
        formatStatus->powerOnMinutesSinceFormatValid = false;
        formatStatus->totalBlockReassignsDuringFormatValid = false;
        formatStatus->totalNewBlocksReassignedValid = false;
        ret = NOT_SUPPORTED;
    }
    return ret;
}

void show_Format_Status_Log(ptrFormatStatus formatStatus)
{
    if (formatStatus)
    {
        printf("Format Status:\n");
        if (!formatStatus->formatParametersAllFs)
        {
            if (formatStatus->lastFormatParametersValid)
            {
                printf("The last format unit was performed with the following parameters:\n");
                printf("\tProtection Field Usage: %" PRIX8"h\n", formatStatus->lastFormatData.protectionFieldUsage);
                if (formatStatus->lastFormatData.formatOptionsValid)
                {
                    printf("\tFormat Options Valid\n");
                    if (formatStatus->lastFormatData.disablePrimaryList)
                    {
                        printf("\tPrimary List Disabled\n");
                    }
                    if (formatStatus->lastFormatData.disableCertify)
                    {
                        printf("\tCertification Disabled\n");
                    }
                    if (formatStatus->lastFormatData.stopFormat)
                    {
                        printf("\tStop format on list error\n");
                    }
                    if (formatStatus->lastFormatData.initializationPattern)
                    {
                        printf("\tInitialization Pattern provided\n");
                    }
                }
                else
                {
                    printf("\tDefault format\n");
                }
                if (formatStatus->lastFormatData.obsoleteDisableSaveParameters)
                {
                    printf("\tObsolete disable save parameters bit set\n");
                }
                if (formatStatus->lastFormatData.immediateResponse)
                {
                    printf("\tImmediate Response Bit set\n");
                }
                if (formatStatus->lastFormatData.vendorSpecific)
                {
                    printf("\tVendor Specific Bit set\n");
                }
                if (formatStatus->lastFormatData.isLongList)
                {
                    printf("\tP_I_Information: %" PRIX8"h", formatStatus->lastFormatData.p_i_information);
                    printf("\tProtection Interval Exponent: %" PRIu8"\n", formatStatus->lastFormatData.protectionIntervalExponent);
                }
                printf("\tDefect List Length: %" PRIu32"\n", formatStatus->lastFormatData.defectListLength);
            }
            if (formatStatus->grownDefectsDuringCertificationValid)
            {
                printf("Grown Defects During Certification: %" PRIu64"\n", formatStatus->grownDefectsDuringCertification);
            }
            if (formatStatus->totalBlockReassignsDuringFormatValid)
            {
                printf("Total Block Reassigns During Format: %" PRIu64"\n", formatStatus->totalBlockReassignsDuringFormat);
            }
            if (formatStatus->totalNewBlocksReassignedValid)
            {
                printf("Total New Blocks Reassigned: %" PRIu64"\n", formatStatus->totalNewBlocksReassigned);
            }
            if (formatStatus->powerOnMinutesSinceFormatValid)
            {
                printf("Power On Minutes Since Last Format: %" PRIu32"\n", formatStatus->powerOnMinutesSinceFormat);
                //convert the time to seconds, then print it in a displayable format
                printf("Power On Time Since Last Format: ");
                uint8_t years, days = 0, hours = 0, minutes = 0, seconds = 0;
                convert_Seconds_To_Displayable_Time(formatStatus->powerOnMinutesSinceFormat * UINT32_C(60), &years, &days, &hours, &minutes, &seconds);
                print_Time_To_Screen(&years, &days, &hours, &minutes, &seconds);
                printf("\n");
            }
        }
        else
        {
            printf("Format unit currently in progress or the last format command failed!\n");
        }
    }
    return;
}

bool is_Set_Sector_Configuration_Supported(tDevice *device)
{
    if (device->drive_info.drive_type == ATA_DRIVE)
    {
        uint8_t idDataLogSupportedCapabilities[LEGACY_DRIVE_SEC_SIZE] = { 0 };
        if (SUCCESS == send_ATA_Read_Log_Ext_Cmd(device, ATA_LOG_IDENTIFY_DEVICE_DATA, ATA_ID_DATA_LOG_SUPPORTED_CAPABILITIES, idDataLogSupportedCapabilities, LEGACY_DRIVE_SEC_SIZE, 0))
        {
            uint64_t qword0 = M_BytesTo8ByteValue(idDataLogSupportedCapabilities[7], idDataLogSupportedCapabilities[6], idDataLogSupportedCapabilities[5], idDataLogSupportedCapabilities[4], idDataLogSupportedCapabilities[3], idDataLogSupportedCapabilities[2], idDataLogSupportedCapabilities[1], idDataLogSupportedCapabilities[0]);
            if (qword0 & BIT63 && M_Byte2(qword0) == ATA_ID_DATA_LOG_SUPPORTED_CAPABILITIES && M_Word0(qword0) >= 0x0001)
            {
                uint64_t supportedCapabilitiesQWord = M_BytesTo8ByteValue(idDataLogSupportedCapabilities[15], idDataLogSupportedCapabilities[14], idDataLogSupportedCapabilities[13], idDataLogSupportedCapabilities[12], idDataLogSupportedCapabilities[11], idDataLogSupportedCapabilities[10], idDataLogSupportedCapabilities[9], idDataLogSupportedCapabilities[8]);
                if (supportedCapabilitiesQWord & BIT63 && supportedCapabilitiesQWord & BIT49)//check bit63 since it should always be 1, then bit 49 for set sector configuration
                {
                    return true;
                }
                else
                {
                    return false;
                }
            }
        }
        else
        {
            return false;
        }
    }
    else if (device->drive_info.drive_type == SCSI_DRIVE)
    {
        bool fastFormatSupported = false;
        if (is_Format_Unit_Supported(device, &fastFormatSupported))
        {
            return fastFormatSupported;
        }
        else
        {
            return false;
        }
    }
    else
    {
        return false;
    }
    return false;
}
#define MAX_NUMBER_SUPPORTED_SECTOR_SIZES UINT32_C(32)
uint32_t get_Number_Of_Supported_Sector_Sizes(tDevice *device)
{
    if (device->drive_info.drive_type == ATA_DRIVE)
    {
        return MAX_NUMBER_SUPPORTED_SECTOR_SIZES;//This should be ok on ATA...we would have to pull the log and count to know for sure, but this is the max available in the log
    }
    else if (device->drive_info.drive_type == SCSI_DRIVE)
    {
        //pull the VPD page and determine how many are supported based on descriptor length and the VPD page length
        uint32_t scsiSectorSizesSupported = 0;
        uint8_t supportedBlockLengthsData[4] = { 0 };
        if (SUCCESS == get_SCSI_VPD(device, SUPPORTED_BLOCK_LENGTHS_AND_PROTECTION_TYPES, NULL, NULL, true, supportedBlockLengthsData, 4, NULL))
        {
            uint16_t pageLength = M_BytesTo2ByteValue(supportedBlockLengthsData[2], supportedBlockLengthsData[3]);
            scsiSectorSizesSupported = pageLength / 8;//each descriptor is 8 bytes in size
        }
        else
        {
            bool fastFormatSup = false;
            //This device either doesn't support any other sector sizes, or supports legacy sector sizes...
            if (is_Format_Unit_Supported(device, &fastFormatSup))
            {
                if (fastFormatSup)
                {
                    scsiSectorSizesSupported = 6;//guessing
                }
                else
                {
                    scsiSectorSizesSupported = 3;//guessing
                }
            }
            else
            {
                //leave at zero for now
                scsiSectorSizesSupported = 0;
            }
        }
        return scsiSectorSizesSupported;
    }
#if !defined (DISABLE_NVME_PASSTHROUGH)
    else if (device->drive_info.drive_type == NVME_DRIVE)
    {
        return device->drive_info.IdentifyData.nvme.ns.nlbaf + 1;//zeros based value so add 1
    }
#endif
    else
    {
        return 0;
    }
}

int ata_Get_Supported_Formats(tDevice *device, ptrSupportedFormats formats)
{
    int ret = NOT_SUPPORTED;
    if (is_Set_Sector_Configuration_Supported(device))
    {
        uint8_t sectorConfigurationLog[LEGACY_DRIVE_SEC_SIZE] = { 0 };
        if (SUCCESS == send_ATA_Read_Log_Ext_Cmd(device, ATA_LOG_SECTOR_CONFIGURATION_LOG, 0, sectorConfigurationLog, LEGACY_DRIVE_SEC_SIZE, 0))
        {
            formats->deviceSupportsOtherFormats = true;
            formats->protectionInformationSupported.deviceSupportsProtection = false;
            uint32_t numberOfSizes = formats->numberOfSectorSizes;
            formats->numberOfSectorSizes = 0;
            for (uint16_t iter = 0, sectorSizeCounter = 0; iter < LEGACY_DRIVE_SEC_SIZE && sectorSizeCounter < numberOfSizes; iter += 16, ++sectorSizeCounter)
            {
                formats->sectorSizes[sectorSizeCounter].logicalBlockLength = M_BytesTo4ByteValue(sectorConfigurationLog[7 + iter], sectorConfigurationLog[6 + iter], sectorConfigurationLog[5 + iter], sectorConfigurationLog[4 + iter]) * 2;
                formats->sectorSizes[sectorSizeCounter].ataSetSectorFields.descriptorCheck = M_BytesTo2ByteValue(sectorConfigurationLog[3 + iter], sectorConfigurationLog[2 + iter]);
                if (formats->sectorSizes[sectorSizeCounter].logicalBlockLength > 0 && formats->sectorSizes[sectorSizeCounter].ataSetSectorFields.descriptorCheck != 0)
                {
                    formats->sectorSizes[sectorSizeCounter].valid = true;
                    formats->sectorSizes[sectorSizeCounter].additionalInformationType = SECTOR_SIZE_ADDITIONAL_INFO_ATA;
                    if (formats->sectorSizes[sectorSizeCounter].logicalBlockLength == device->drive_info.deviceBlockSize)
                    {
                        formats->sectorSizes[sectorSizeCounter].currentFormat = true;
                    }
                    ++(formats->numberOfSectorSizes);
                }
                formats->sectorSizes[sectorSizeCounter].ataSetSectorFields.descriptorIndex = (uint8_t)(iter / 16);
            }
            ret = SUCCESS;
        }
        else
        {
            ret = FAILURE;
        }
    }
    else
    {
        formats->deviceSupportsOtherFormats = false;
        formats->numberOfSectorSizes = 1;
        formats->protectionInformationSupported.deviceSupportsProtection = false;
        formats->sectorSizes[0].valid = true;
        formats->sectorSizes[0].currentFormat = true;
        formats->sectorSizes[0].logicalBlockLength = device->drive_info.deviceBlockSize;
        formats->sectorSizes[0].additionalInformationType = SECTOR_SIZE_ADDITIONAL_INFO_NONE;
        ret = SUCCESS;
    }
    return ret;
}

int scsi_Get_Supported_Formats(tDevice *device, ptrSupportedFormats formats)
{
    int ret = NOT_SUPPORTED;
    uint8_t *inquiryData = (uint8_t*)calloc(INQ_RETURN_DATA_LENGTH, sizeof(uint8_t));
    if (!inquiryData)
    {
        return MEMORY_FAILURE;
    }
    if (SUCCESS == scsi_Inquiry(device, inquiryData, INQ_RETURN_DATA_LENGTH, 0, false, false))
    {
        ret = SUCCESS;
        if (inquiryData[5] & BIT0)
        {
            formats->protectionInformationSupported.deviceSupportsProtection = true;
            //now read the extended inquiry data VPD page
            if (SUCCESS == scsi_Inquiry(device, inquiryData, INQ_RETURN_DATA_LENGTH, EXTENDED_INQUIRY_DATA, true, false))
            {
                switch (inquiryData[0] & 0x1F)
                {
                case 0://direct access block device
                    switch ((inquiryData[4] >> 3) & 0x07)//spt field
                    {
                    case 0:
                        formats->protectionInformationSupported.protectionType1Supported = true;
                        formats->protectionInformationSupported.protectionType2Supported = false;
                        formats->protectionInformationSupported.protectionType3Supported = false;
                        break;
                    case 1:
                        formats->protectionInformationSupported.protectionType1Supported = true;
                        formats->protectionInformationSupported.protectionType2Supported = true;
                        formats->protectionInformationSupported.protectionType3Supported = false;
                        break;
                    case 2:
                        formats->protectionInformationSupported.protectionType1Supported = false;
                        formats->protectionInformationSupported.protectionType2Supported = true;
                        formats->protectionInformationSupported.protectionType3Supported = false;
                        break;
                    case 3:
                        formats->protectionInformationSupported.protectionType1Supported = true;
                        formats->protectionInformationSupported.protectionType2Supported = false;
                        formats->protectionInformationSupported.protectionType3Supported = true;
                        break;
                    case 4:
                        formats->protectionInformationSupported.protectionType1Supported = false;
                        formats->protectionInformationSupported.protectionType2Supported = false;
                        formats->protectionInformationSupported.protectionType3Supported = true;
                        break;
                    case 5:
                        formats->protectionInformationSupported.protectionType1Supported = false;
                        formats->protectionInformationSupported.protectionType2Supported = true;
                        formats->protectionInformationSupported.protectionType3Supported = true;
                        break;
                    case 6:
                        formats->protectionInformationSupported.protectionReportedPerSectorSize = true;
                        break;
                    case 7:
                        formats->protectionInformationSupported.protectionType1Supported = true;
                        formats->protectionInformationSupported.protectionType2Supported = true;
                        formats->protectionInformationSupported.protectionType3Supported = true;
                        break;
                    }
                    break;
                case 1://sequential access block device (we don't care...it's a tape drive...)
                default:
                    break;
                }
            }
        }
    }
    safe_Free(inquiryData);
    bool dummyUpCommonSizes = true;
    uint32_t supportedSectorSizesDataLength = 0;
    get_SCSI_VPD_Page_Size(device, SUPPORTED_BLOCK_LENGTHS_AND_PROTECTION_TYPES, &supportedSectorSizesDataLength);
    if (formats->protectionInformationSupported.protectionReportedPerSectorSize || supportedSectorSizesDataLength)
    {
        uint32_t supportedSectorSizesDataLength = 0;
        if (SUCCESS == get_SCSI_VPD_Page_Size(device, SUPPORTED_BLOCK_LENGTHS_AND_PROTECTION_TYPES, &supportedSectorSizesDataLength))
        {
            uint8_t *supportedBlockLengthsData = (uint8_t*)calloc(supportedSectorSizesDataLength, sizeof(uint8_t));
            if (!supportedBlockLengthsData)
            {
                return MEMORY_FAILURE;
            }
            if (SUCCESS == get_SCSI_VPD(device, SUPPORTED_BLOCK_LENGTHS_AND_PROTECTION_TYPES, NULL, NULL, true, supportedBlockLengthsData, supportedSectorSizesDataLength, NULL))
            {
                dummyUpCommonSizes = false;
                uint32_t numberOfSizes = formats->numberOfSectorSizes;
                formats->numberOfSectorSizes = 0;
                for (uint32_t iter = 4, sectorSizeCounter = 0; (iter + 8) < supportedSectorSizesDataLength && sectorSizeCounter < numberOfSizes; iter += 8, ++sectorSizeCounter, ++formats->numberOfSectorSizes)
                {
                    formats->sectorSizes[sectorSizeCounter].valid = true;
                    formats->sectorSizes[sectorSizeCounter].logicalBlockLength = M_BytesTo4ByteValue(supportedBlockLengthsData[iter + 0], supportedBlockLengthsData[iter + 1], supportedBlockLengthsData[iter + 2], supportedBlockLengthsData[iter + 3]);
                    formats->sectorSizes[sectorSizeCounter].additionalInformationType = SECTOR_SIZE_ADDITIONAL_INFO_SCSI;
                    formats->sectorSizes[sectorSizeCounter].scsiSectorBits.piSupportBitsValid = true;
                    formats->sectorSizes[sectorSizeCounter].scsiSectorBits.p_i_i_sup = (bool)(supportedBlockLengthsData[iter + 4] & BIT6);
                    formats->sectorSizes[sectorSizeCounter].scsiSectorBits.no_pi_chk = (bool)(supportedBlockLengthsData[iter + 4] & BIT3);
                    formats->sectorSizes[sectorSizeCounter].scsiSectorBits.grd_chk = (bool)(supportedBlockLengthsData[iter + 4] & BIT2);
                    formats->sectorSizes[sectorSizeCounter].scsiSectorBits.app_chk = (bool)(supportedBlockLengthsData[iter + 4] & BIT1);
                    formats->sectorSizes[sectorSizeCounter].scsiSectorBits.ref_chk = (bool)(supportedBlockLengthsData[iter + 5] & BIT0);
                    formats->sectorSizes[sectorSizeCounter].scsiSectorBits.t0ps = (bool)(supportedBlockLengthsData[iter + 5] & BIT0);
                    if (supportedBlockLengthsData[iter + 5] & BIT1)
                    {
                        formats->sectorSizes[sectorSizeCounter].scsiSectorBits.t1ps = true;
                        formats->protectionInformationSupported.protectionType1Supported = true;
                    }
                    if (supportedBlockLengthsData[iter + 5] & BIT2)
                    {
                        formats->sectorSizes[sectorSizeCounter].scsiSectorBits.t2ps = true;
                        formats->protectionInformationSupported.protectionType2Supported = true;
                    }
                    if (supportedBlockLengthsData[iter + 5] & BIT3)
                    {
                        formats->sectorSizes[sectorSizeCounter].scsiSectorBits.t3ps = true;
                        formats->protectionInformationSupported.protectionType3Supported = true;
                    }
                    if (device->drive_info.deviceBlockSize == formats->sectorSizes[sectorSizeCounter].logicalBlockLength)
                    {
                        //check PI to see if this is the current format
                        switch (device->drive_info.currentProtectionType)
                        {
                        case 0:
                            if (formats->sectorSizes[sectorSizeCounter].scsiSectorBits.t0ps)
                            {
                                formats->sectorSizes[sectorSizeCounter].currentFormat = true;
                            }
                            break;
                        case 1:
                            if (formats->sectorSizes[sectorSizeCounter].scsiSectorBits.t1ps)
                            {
                                formats->sectorSizes[sectorSizeCounter].currentFormat = true;
                            }
                            break;
                        case 2:
                            if (formats->sectorSizes[sectorSizeCounter].scsiSectorBits.t2ps)
                            {
                                formats->sectorSizes[sectorSizeCounter].currentFormat = true;
                            }
                            break;
                        case 3:
                            if (formats->sectorSizes[sectorSizeCounter].scsiSectorBits.t3ps)
                            {
                                formats->sectorSizes[sectorSizeCounter].currentFormat = true;
                            }
                            break;
                        default:
                            break;
                        }
                    }
                }
                if (formats->protectionInformationSupported.protectionType1Supported || formats->protectionInformationSupported.protectionType2Supported || formats->protectionInformationSupported.protectionType3Supported)
                {
                    formats->protectionInformationSupported.deviceSupportsProtection = true;
                }
                ret = SUCCESS;
            }
            safe_Free(supportedBlockLengthsData);
        }
    }
    if (is_Format_Unit_Supported(device, &formats->scsiFastFormatSupported))
    {
        if (dummyUpCommonSizes)
        {
            formats->scsiInformationNotReported = true;
            ret = SUCCESS;
            //dummy up the support based on what is known from traditional formatting support - don't include any PI stuff here for now. Need more refactoring
            if (formats->scsiFastFormatSupported)
            {
                formats->numberOfSectorSizes = 6;
                formats->sectorSizes[0].valid = true;
                formats->sectorSizes[0].logicalBlockLength = 512;
                formats->sectorSizes[0].additionalInformationType = SECTOR_SIZE_ADDITIONAL_INFO_NONE;

                formats->sectorSizes[1].valid = true;
                formats->sectorSizes[1].logicalBlockLength = 520;
                formats->sectorSizes[1].additionalInformationType = SECTOR_SIZE_ADDITIONAL_INFO_NONE;

                formats->sectorSizes[2].valid = true;
                formats->sectorSizes[2].logicalBlockLength = 528;
                formats->sectorSizes[2].additionalInformationType = SECTOR_SIZE_ADDITIONAL_INFO_NONE;

                formats->sectorSizes[3].valid = true;
                formats->sectorSizes[3].logicalBlockLength = 4096;
                formats->sectorSizes[3].additionalInformationType = SECTOR_SIZE_ADDITIONAL_INFO_NONE;

                formats->sectorSizes[4].valid = true;
                formats->sectorSizes[4].logicalBlockLength = 4112;
                formats->sectorSizes[4].additionalInformationType = SECTOR_SIZE_ADDITIONAL_INFO_NONE;

                formats->sectorSizes[5].valid = true;
                formats->sectorSizes[5].logicalBlockLength = 4160;
                formats->sectorSizes[5].additionalInformationType = SECTOR_SIZE_ADDITIONAL_INFO_NONE;

                switch (device->drive_info.deviceBlockSize)
                {
                case 512:
                    formats->sectorSizes[0].currentFormat = true;
                    break;
                case 520:
                    formats->sectorSizes[1].currentFormat = true;
                    break;
                case 528:
                    formats->sectorSizes[2].currentFormat = true;
                    break;
                case 4096:
                    formats->sectorSizes[3].currentFormat = true;
                    break;
                case 4112:
                    formats->sectorSizes[4].currentFormat = true;
                    break;
                case 4160:
                    formats->sectorSizes[5].currentFormat = true;
                    break;
                default:
                    break;
                }
            }
            else
            {
                formats->numberOfSectorSizes = 3;
                //dummy up based on current sector size
                if (device->drive_info.deviceBlockSize < 4096)
                {
                    //512, 520, 528
                    formats->sectorSizes[0].valid = true;
                    formats->sectorSizes[0].logicalBlockLength = 512;
                    formats->sectorSizes[0].additionalInformationType = SECTOR_SIZE_ADDITIONAL_INFO_NONE;

                    formats->sectorSizes[1].valid = true;
                    formats->sectorSizes[1].logicalBlockLength = 520;
                    formats->sectorSizes[1].additionalInformationType = SECTOR_SIZE_ADDITIONAL_INFO_NONE;

                    formats->sectorSizes[2].valid = true;
                    formats->sectorSizes[2].logicalBlockLength = 528;
                    formats->sectorSizes[2].additionalInformationType = SECTOR_SIZE_ADDITIONAL_INFO_NONE;
                    switch (device->drive_info.deviceBlockSize)
                    {
                    case 512:
                        formats->sectorSizes[0].currentFormat = true;
                        break;
                    case 520:
                        formats->sectorSizes[1].currentFormat = true;
                        break;
                    case 528:
                        formats->sectorSizes[2].currentFormat = true;
                        break;
                    default:
                        break;
                    }
                }
                else
                {
                    //4096, 4112, 4160
                    formats->sectorSizes[0].valid = true;
                    formats->sectorSizes[0].logicalBlockLength = 4096;
                    formats->sectorSizes[0].additionalInformationType = SECTOR_SIZE_ADDITIONAL_INFO_NONE;

                    formats->sectorSizes[1].valid = true;
                    formats->sectorSizes[1].logicalBlockLength = 4112;
                    formats->sectorSizes[1].additionalInformationType = SECTOR_SIZE_ADDITIONAL_INFO_NONE;

                    formats->sectorSizes[2].valid = true;
                    formats->sectorSizes[2].logicalBlockLength = 4160;
                    formats->sectorSizes[2].additionalInformationType = SECTOR_SIZE_ADDITIONAL_INFO_NONE;
                    switch (device->drive_info.deviceBlockSize)
                    {
                    case 4096:
                        formats->sectorSizes[0].currentFormat = true;
                        break;
                    case 4112:
                        formats->sectorSizes[1].currentFormat = true;
                        break;
                    case 4160:
                        formats->sectorSizes[2].currentFormat = true;
                        break;
                    default:
                        break;
                    }
                }
            }
        }
    }
    else
    {
        formats->deviceSupportsOtherFormats = false;
        formats->numberOfSectorSizes = 1;
        formats->sectorSizes[0].valid = true;
        formats->sectorSizes[0].currentFormat = true;
        formats->sectorSizes[0].logicalBlockLength = device->drive_info.deviceBlockSize;
        formats->sectorSizes[0].additionalInformationType = SECTOR_SIZE_ADDITIONAL_INFO_NONE;
    }
    
    return ret;
}

#if !defined (DISABLE_NVME_PASSTHROUGH)
int nvme_Get_Supported_Formats(tDevice *device, ptrSupportedFormats formats)
{
    //read the PI support from identify namespace structure
    if (device->drive_info.IdentifyData.nvme.ns.dpc > 0)
    {
        if (device->drive_info.IdentifyData.nvme.ns.dpc & BIT0)
        {
            formats->protectionInformationSupported.protectionType1Supported = true;
        }
        if (device->drive_info.IdentifyData.nvme.ns.dpc & BIT1)
        {
            formats->protectionInformationSupported.protectionType2Supported = true;
        }
        if (device->drive_info.IdentifyData.nvme.ns.dpc & BIT2)
        {
            formats->protectionInformationSupported.protectionType3Supported = true;
        }
        if (formats->protectionInformationSupported.protectionType1Supported || formats->protectionInformationSupported.protectionType2Supported || formats->protectionInformationSupported.protectionType3Supported)
        {
            formats->protectionInformationSupported.deviceSupportsProtection = true;
        }
        formats->protectionInformationSupported.nvmSpecificPI.nvmSpecificValid = true;
        formats->protectionInformationSupported.nvmSpecificPI.piFirst8 = device->drive_info.IdentifyData.nvme.ns.dpc & BIT3;
        formats->protectionInformationSupported.nvmSpecificPI.piLast8 = device->drive_info.IdentifyData.nvme.ns.dpc & BIT4;
    }
    formats->nvmeMetadataSupport.nvmSpecificValid = true;
    formats->nvmeMetadataSupport.metadataXLBASup = device->drive_info.IdentifyData.nvme.ns.mc & BIT0;
    formats->nvmeMetadataSupport.metadataSeparateSup = device->drive_info.IdentifyData.nvme.ns.mc & BIT1;

    formats->deviceSupportsOtherFormats = true;

    //set metadata and PI location bits first
    for (uint8_t iter = 0; iter < (device->drive_info.IdentifyData.nvme.ns.nlbaf + 1); ++iter)
    {
        if (device->drive_info.IdentifyData.nvme.ns.lbaf[iter].lbaDS > 0)
        {
            formats->sectorSizes[iter].valid = true;
            formats->sectorSizes[iter].logicalBlockLength = (uint32_t)power_Of_Two(device->drive_info.IdentifyData.nvme.ns.lbaf[iter].lbaDS);
            formats->sectorSizes[iter].additionalInformationType = SECTOR_SIZE_ADDITIONAL_INFO_NVME;
            formats->sectorSizes[iter].nvmeSectorBits.relativePerformance = M_GETBITRANGE(device->drive_info.IdentifyData.nvme.ns.lbaf[iter].rp, 1, 0);
            formats->sectorSizes[iter].nvmeSectorBits.metadataSize = device->drive_info.IdentifyData.nvme.ns.lbaf[iter].ms;
            ++formats->numberOfSectorSizes;
        }
    }

    //set current format
    formats->sectorSizes[M_Nibble0(device->drive_info.IdentifyData.nvme.ns.flbas)].currentFormat = true;
    return SUCCESS;
}
#endif

int get_Supported_Formats(tDevice *device, ptrSupportedFormats formats)
{
    int ret = NOT_SUPPORTED;
    if (!formats)
    {
        return BAD_PARAMETER;
    }
    switch (device->drive_info.drive_type)
    {
    case ATA_DRIVE:
        return ata_Get_Supported_Formats(device, formats);
        break;
    case NVME_DRIVE:
#if !defined (DISABLE_NVME_PASSTHROUGH)
        return nvme_Get_Supported_Formats(device, formats);
        break;
#endif
    case SCSI_DRIVE:
        return scsi_Get_Supported_Formats(device, formats);
        break;
    default:
        break;
    }
    return ret;
}


void show_Supported_Formats(ptrSupportedFormats formats)
{
    printf("\nSupported Logical Block Sizes and Protection Types:\n");
    printf("---------------------------------------------------\n");
    printf("  * - current device format\n");
    printf("PI Key:\n");
    printf("  Y - protection type supported at specified block size\n");
    printf("  N - protection type not supported at specified block size\n");
    printf("  ? - unable to determine support for protection type at specified block size\n");
    printf("Relative performance key:\n");
    printf("  N/A - relative performance not available.\n");
    printf("  Best    \n");
    printf("  Better  \n");
    printf("  Good    \n");
    printf("  Degraded\n");
    //now print out the supported block sizes
    printf("--------------------------------------------------------------------------------\n");
    printf(" %18s  %4s  %4s  %4s  %4s  %20s  %13s\n", "Logical Block Size", "PI-0", "PI-1", "PI-2", "PI-3", "Relative Performance", "Metadata Size");
    printf("--------------------------------------------------------------------------------\n");
    for (uint32_t iter = 0; iter < formats->numberOfSectorSizes; ++iter)
    {
        if (formats->sectorSizes[iter].valid)
        {
            char current = ' ';
            char pi0 = 0;
            char pi1 = 0;
            char pi2 = 0;
            char pi3 = 0;
            char perf[10] = { 0 };
            char metaSize[10] = { 0 };
            sprintf(perf, "N/A");
            sprintf(metaSize, "N/A");
            if (formats->protectionInformationSupported.deviceSupportsProtection)
            {
                pi0 = 'Y';
                if (formats->protectionInformationSupported.protectionType1Supported)
                {
                    pi1 = '?';
                }
                else
                {
                    pi1 = 'N';
                }
                if (formats->protectionInformationSupported.protectionType2Supported)
                {
                    pi2 = '?';
                }
                else
                {
                    pi2 = 'N';
                }
                if (formats->protectionInformationSupported.protectionType3Supported)
                {
                    pi3 = '?';
                }
                else
                {
                    pi3 = 'N';
                }
            }
            else if (formats->protectionInformationSupported.protectionReportedPerSectorSize)
            {
                switch (formats->sectorSizes[iter].additionalInformationType)
                {
                case SECTOR_SIZE_ADDITIONAL_INFO_SCSI:
                    if (formats->sectorSizes[iter].scsiSectorBits.piSupportBitsValid)
                    {
                        if (formats->sectorSizes[iter].scsiSectorBits.t0ps)
                        {
                            pi0 = 'Y';
                        }
                        else
                        {
                            pi0 = 'N';
                        }
                        if (formats->sectorSizes[iter].scsiSectorBits.t1ps)
                        {
                            pi1 = 'Y';
                        }
                        else
                        {
                            pi1 = 'N';
                        }
                        if (formats->sectorSizes[iter].scsiSectorBits.t2ps)
                        {
                            pi2 = 'Y';
                        }
                        else
                        {
                            pi2 = 'N';
                        }
                        if (formats->sectorSizes[iter].scsiSectorBits.t3ps)
                        {
                            pi3 = 'Y';
                        }
                        else
                        {
                            pi3 = 'N';
                        }
                    }
                    break;
                default:
                    break;
                }
            }
            else
            {
                pi0 = 'Y';
                pi1 = 'N';
                pi2 = 'N';
                pi3 = 'N';
            }
            //set relative performance for NVMe
            switch (formats->sectorSizes[iter].additionalInformationType)
            {
            case SECTOR_SIZE_ADDITIONAL_INFO_NVME:
                memset(perf, 0, 10);
                switch (formats->sectorSizes[iter].nvmeSectorBits.relativePerformance)
                {
                case 0:
                    sprintf(perf, "Best");
                    break;
                case 1:
                    sprintf(perf, "Better");
                    break;
                case 2:
                    sprintf(perf, "Good");
                    break;
                case 3:
                    sprintf(perf, "Degraded");
                    break;
                default:
                    sprintf(perf, "N/A");
                    break;
                }
                memset(metaSize, 0, 10);
                sprintf(metaSize, "%" PRIu16, formats->sectorSizes[iter].nvmeSectorBits.metadataSize);
                break;
            default:
                break;
            }
            if (formats->sectorSizes[iter].currentFormat)
            {
                current = '*';
            }
            printf("%c%18" PRIu32 "  %4c  %4c  %4c  %4c  %20s  %13s\n", current, formats->sectorSizes[iter].logicalBlockLength, pi0, pi1, pi2, pi3, perf, metaSize);
        }
    }
    printf("--------------------------------------------------------------------------------\n");
    if (formats->scsiInformationNotReported)
    {
        printf("NOTE: Device is not capable of showing all sizes it supports. Only common\n");
        printf("      sizes are listed. Please consult the product manual for all supported\n");
        printf("      combinations.\n");
    }
    if (formats->protectionInformationSupported.deviceSupportsProtection)
    {
        printf("NOTE: This device supports protection information (PI) (a.k.a. End to End protection).\n");
        printf("\tType 0 - No protection beyond transport protocol\n");//this is always supported
        if (formats->protectionInformationSupported.protectionType1Supported)
        {
            printf("\tType 1 - Logical Block Guard and Logical Block Reference Tag\n");
        }
        if (formats->protectionInformationSupported.protectionType2Supported)
        {
            printf("\tType 2 - Logical Block Guard and Logical Block Reference Tag (except first block)\n\t\t 32byte read/write CDBs allowed\n");
        }
        if (formats->protectionInformationSupported.protectionType3Supported)
        {
            printf("\tType 3 - Logical Block Guard\n");
        }

        if (!formats->protectionInformationSupported.protectionReportedPerSectorSize)
        {
            printf("      Not all forms of PI are supported on all sector sizes unless otherwise indicated\n");
            printf("      in the device product manual.\n");
        }
    }
    if (formats->scsiFastFormatSupported)
    {
        printf("NOTE: This device supports Fast Format. Fast format is not instantaneous and is used for\n");
        printf("      switching between 5xx and 4xxx sector sizes. A fast format may take a few minutes or longer\n");
        printf("      but may take longer depending on the size of the drive. Fast format support does not necessarily\n");
        printf("      mean switching sector sizes AND changing PI at the same time is supported. In most cases, a\n");
        printf("      switch of PI type will require a full device format.\n");
        printf("      Fast format mode 1 is typically used to switch from 512 to 4096 block sizes with the current\n");
        printf("          PI scheme.\n");
    }
    //TODO: NVMe Metadata and PI location information.
    return;
}

//this function takes a sector size and maps it to the descriptor check code to use in the set sector configuration command
int ata_Map_Sector_Size_To_Descriptor_Check(tDevice *device, uint32_t logicalBlockLength, uint16_t *descriptorCheckCode, uint8_t *descriptorIndex)
{
    int ret = SUCCESS;
    if (!descriptorCheckCode || !descriptorIndex)
    {
        return BAD_PARAMETER;
    }
    else
    {
        *descriptorCheckCode = 0;
    }
    if (device->drive_info.drive_type == ATA_DRIVE)
    {
        uint32_t numberOfSupportedFormats = get_Number_Of_Supported_Sector_Sizes(device);
        uint32_t formatsDataSize = sizeof(supportedFormats) + (sizeof(sectorSize) * numberOfSupportedFormats);
        ptrSupportedFormats formats = (ptrSupportedFormats)malloc(formatsDataSize);
        if (!formats)
        {
            return MEMORY_FAILURE;
        }
        memset(formats, 0, formatsDataSize);
        formats->numberOfSectorSizes = numberOfSupportedFormats;
        ret = get_Supported_Formats(device, formats);
        if (SUCCESS == ret)
        {
            for (uint8_t sectorSizeIter = 0; sectorSizeIter < formats->numberOfSectorSizes && sectorSizeIter < numberOfSupportedFormats; ++sectorSizeIter)
            {
                if (!formats->sectorSizes[sectorSizeIter].valid)
                {
                    break;
                }
                if (formats->sectorSizes[sectorSizeIter].logicalBlockLength == logicalBlockLength)
                {
                    *descriptorCheckCode = formats->sectorSizes[sectorSizeIter].ataSetSectorFields.descriptorCheck;
                    *descriptorIndex = formats->sectorSizes[sectorSizeIter].ataSetSectorFields.descriptorIndex;
                    break;
                }
            }
            if (*descriptorCheckCode == 0)//TODO: Verify this is safe to do for a check that we got something valid
            {
                ret = NOT_SUPPORTED;
            }
        }
    }
    return ret;
}

//this is used to determine which fast format mode to use.
bool is_Requested_Sector_Size_Multiple(tDevice *device, uint32_t sectorSize)
{
    uint32_t larger = device->drive_info.deviceBlockSize > sectorSize ? device->drive_info.deviceBlockSize : sectorSize;
    uint32_t smaller = device->drive_info.deviceBlockSize < sectorSize ? device->drive_info.deviceBlockSize : sectorSize;
    if (larger == 0 || smaller == 0)
    {
        return false;
    }
    //if there is a remainder in this division then this is not a multiple of
    if (larger % smaller)
    {
        return false;
    }
    else
    {
        return true;
    }
}

int set_Sector_Configuration(tDevice *device, uint32_t sectorSize)
{
    int ret = NOT_SUPPORTED;
    if (is_Set_Sector_Configuration_Supported(device))
    {
        if (device->deviceVerbosity >= VERBOSITY_DEFAULT)
        {
            printf("Setting the drive sector size quickly.\n");
            printf("Please wait a few minutes for this command to complete.\n");
            printf("It should complete in under 5 minutes, but interrupting it may make\n");
            printf("the drive unusable or require performing this command again!!\n");
        }
        if (device->drive_info.drive_type == ATA_DRIVE)
        {
            uint16_t descriptorCheck = 0;
            uint8_t descriptorIndex = 0;
            ret = ata_Map_Sector_Size_To_Descriptor_Check(device, sectorSize, &descriptorCheck, &descriptorIndex);
            if (SUCCESS == ret)
            {
                ret = ata_Set_Sector_Configuration_Ext(device, descriptorCheck, descriptorIndex);
            }
        }
        else //Assume SCSI
        {
            runFormatUnitParameters formatUnitParameters;
            memset(&formatUnitParameters, 0, sizeof(runFormatUnitParameters));
            formatUnitParameters.formatType = FORMAT_FAST_WRITE_NOT_REQUIRED;
            formatUnitParameters.currentBlockSize = false;
            formatUnitParameters.newBlockSize = (uint16_t)sectorSize;
            formatUnitParameters.gList = NULL;
            formatUnitParameters.glistSize = 0;
            formatUnitParameters.completeList = false;
            formatUnitParameters.disablePrimaryList = false;
            formatUnitParameters.disableCertification = false;
            formatUnitParameters.pattern = NULL;
            formatUnitParameters.patternLength = 0;
            formatUnitParameters.securityInitialize = false;
            formatUnitParameters.defaultFormat = true;//Don't need any option bits! In fact, this could cause an error if not set!
            formatUnitParameters.protectionType = device->drive_info.currentProtectionType;
            formatUnitParameters.protectionIntervalExponent = device->drive_info.piExponent;
            formatUnitParameters.disableImmediate = true;
            //make this smarter to know which type of fast format to use! FAST_FORMAT_WRITE_NOT_REQUIRED is a power of 2 change (512 to 4096), FAST_FORMAT_WRITE_REQUIRED is any other size change
            if (!is_Requested_Sector_Size_Multiple(device, sectorSize))
            {
                formatUnitParameters.formatType = FORMAT_FAST_WRITE_REQUIRED;
            }
            ret = run_Format_Unit(device, formatUnitParameters, false);
        }
    }
    return ret;
}

#if !defined (DISABLE_NVME_PASSTHROUGH)
int get_NVM_Format_Progress(tDevice *device, uint8_t *percentComplete)
{
    int ret = SUCCESS;
    if (!percentComplete)
    {
        return BAD_PARAMETER;
    }
    *percentComplete = 0;
    if (device->drive_info.drive_type == NVME_DRIVE)
    {
        ret = nvme_Identify(device, (uint8_t*)&device->drive_info.IdentifyData.nvme.ns, NVME_ALL_NAMESPACES, NVME_IDENTIFY_NS);
        if (ret == SUCCESS)
        {
            if (device->drive_info.IdentifyData.nvme.ns.fpi & BIT7)
            {
                *percentComplete = 100 - M_GETBITRANGE(device->drive_info.IdentifyData.nvme.ns.fpi, 6, 0);
                ret = IN_PROGRESS;
            }
        }
    }
    else
    {
        ret = NOT_SUPPORTED;
    }
    return ret;
}

int show_NVM_Format_Progress(tDevice *device)
{
    int ret = UNKNOWN;
    uint8_t percentComplete = 0;

    ret = get_NVM_Format_Progress(device, &percentComplete);

    if (ret == IN_PROGRESS)
    {
        printf("\tFormat Progress = %" PRIu8 "%% \n", percentComplete);
    }
    else if (ret == SUCCESS)
    {
        printf("\tA format is not detected as running. Either it is complete or the device does not report its progress\n");
    }
    else
    {
        printf("\tError occurred while retrieving format progress!\n");
    }
    return ret;
}

int map_NVM_Format_To_Format_Number(tDevice * device, uint32_t lbaSize, uint16_t metadataSize)
{
    int fmtNum = 16;//invalid value to catch errors!
    for (uint8_t fmtIter = 0; fmtIter < device->drive_info.IdentifyData.nvme.ns.nlbaf; ++fmtIter)
    {
        if (lbaSize == power_Of_Two(device->drive_info.IdentifyData.nvme.ns.lbaf[fmtIter].lbaDS))
        {
            //lba size matches, now check the metadata!
            if (metadataSize == device->drive_info.IdentifyData.nvme.ns.lbaf[fmtIter].ms)
            {
                fmtNum = fmtIter;
                break;
            }
        }
    }
    return fmtNum;
}

int run_NVMe_Format(tDevice * device, runNVMFormatParameters nvmParams, bool pollForProgress)
{
    int ret = SUCCESS;
    nvmeFormatCmdOpts formatCmdOptions;
    memset(&formatCmdOptions, 0, sizeof(nvmeFormatCmdOpts));
    //Set metadata, PI, PIL settings to current device settings to start
    formatCmdOptions.ms = device->drive_info.IdentifyData.nvme.ns.mc & BIT0 ? 1 : 0;
    formatCmdOptions.pil = device->drive_info.IdentifyData.nvme.ns.dps & BIT3 ? 1 : 0;
    formatCmdOptions.pi = M_GETBITRANGE(device->drive_info.IdentifyData.nvme.ns.dps, 2, 0);

    if (nvmParams.metadataSettings.valid)
    {
        formatCmdOptions.ms = nvmParams.metadataSettings.metadataAsExtendedLBA ? 1 : 0;
    }

    if (nvmParams.protectionLocation.valid)
    {
        formatCmdOptions.pil = nvmParams.protectionLocation.first8Bytes ? 1 : 0;
    }

    if (nvmParams.changeProtectionType)
    {
        formatCmdOptions.pi = nvmParams.protectionType;
    }
    
    if (nvmParams.formatNumberProvided)
    {
        formatCmdOptions.lbaf = nvmParams.formatNumber;
    }
    else
    {
        //need to figure out what format we want to run!
        uint32_t fmtBlockSize = device->drive_info.deviceBlockSize;
        uint32_t fmtMetaDataSize = device->drive_info.IdentifyData.nvme.ns.lbaf[M_GETBITRANGE(device->drive_info.IdentifyData.nvme.ns.flbas, 3, 0)].ms;

        if (!nvmParams.newSize.currentBlockSize)
        {
            fmtBlockSize = nvmParams.newSize.newBlockSize;
        }

        if (!nvmParams.newSize.changeMetadataSize)
        {
            fmtMetaDataSize = nvmParams.newSize.metadataSize;
        }

        formatCmdOptions.lbaf = map_NVM_Format_To_Format_Number(device, fmtBlockSize, fmtMetaDataSize);
    }
    //invalid format requested.
    if (formatCmdOptions.lbaf > 15)
    {
        if (device->deviceVerbosity > VERBOSITY_QUIET)
        {
            printf("ERROR: Invalid format requested\n");
        }
        return NOT_SUPPORTED;
    }

    if (nvmParams.secureEraseSettings == NVM_FMT_SE_CRYPTO && (!(device->drive_info.IdentifyData.nvme.ctrl.fna & BIT2)))
    {
        if (device->deviceVerbosity > VERBOSITY_QUIET)
        {
            printf("ERROR: Crypto Erase not supported by the device\n");
        }
        return NOT_SUPPORTED;
    }

    switch (nvmParams.secureEraseSettings)
    {
    case NVM_FMT_SE_USER_DATA:
        formatCmdOptions.ses = FORMAT_NVME_ERASE_USER_DATA;
        break;
    case NVM_FMT_SE_CRYPTO:
        formatCmdOptions.ses = FORMAT_NVME_CRYPTO_ERASE;
        break;
    case NVM_FMT_SE_NO_SECURE_ERASE_REQUESTED:
    default:
        formatCmdOptions.ses = FORMAT_NVME_NO_SECURE_ERASE;
        break;
    }
    if (nvmParams.currentNamespace)
    {
        formatCmdOptions.nsid = device->drive_info.namespaceID;
    }
    else
    {
        formatCmdOptions.nsid = NVME_ALL_NAMESPACES;
    }
    ret = nvme_Format(device, &formatCmdOptions);
    if (pollForProgress && ret == SUCCESS)
    {
        uint32_t delayTimeSeconds = 5;
        uint8_t progress = 0;
        delay_Seconds(2); //2 second delay to make sure it starts (and on SSD this may be enough for it to finish immediately)
        if (VERBOSITY_QUIET < device->deviceVerbosity)
        {
            uint8_t seconds = 0, minutes = 0, hours = 0;
            convert_Seconds_To_Displayable_Time(delayTimeSeconds, NULL, NULL, &hours, &minutes, &seconds);
            printf("Progress will be updated every ");
            print_Time_To_Screen(NULL, NULL, &hours, &minutes, &seconds);
            printf("\n");
        }
        while (IN_PROGRESS == get_NVM_Format_Progress(device, &progress) && progress < 100.0)
        {
            if (VERBOSITY_QUIET < device->deviceVerbosity)
            {
                printf("\r\tPercent Complete: %" PRIu8 "%%", progress);
                fflush(stdout);
            }
            delay_Seconds(delayTimeSeconds); //time set above
        }
        if (VERBOSITY_QUIET < device->deviceVerbosity)
        {
            printf("\n");
        }
    }
    return ret;
}
#endif