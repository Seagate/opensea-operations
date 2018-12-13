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
// \file format_unit.c
// \brief This file defines the functions for performing some format unit operations

#include "format_unit.h"
#include "logs.h"

bool is_Format_Unit_Supported(tDevice *device, bool *fastFormatSupported)
{
    uint8_t formatSupportData[10] = { 0 };
    if (fastFormatSupported)
    {
        *fastFormatSupported = false;//make sure this defaults to false
    }
    if (device->drive_info.scsiVersion >= 5 && SUCCESS == scsi_Report_Supported_Operation_Codes(device, false, 1, SCSI_FORMAT_UNIT_CMD, 0, 10, formatSupportData))
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
            ret = scsi_Mode_Select_10(device, 24, false, true, modeParameterData, 24); //turning off page format bit due to reading page 0 above
        }
        else
        {
            ret = scsi_Mode_Select_6(device, 12, false, true, modeParameterData, 12); //turning off page format bit due to reading page 0 above
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

int get_Supported_Protection_Types(tDevice *device, ptrProtectionSupport protectionSupportInfo)
{
    int ret = SUCCESS;
    if (!device || !protectionSupportInfo)
    {
        return BAD_PARAMETER;
    }
    uint8_t *inquiryData = (uint8_t*)calloc(INQ_RETURN_DATA_LENGTH, sizeof(uint8_t));
    if (!inquiryData)
    {
        return MEMORY_FAILURE;
    }
    if (SUCCESS == scsi_Inquiry(device, inquiryData, INQ_RETURN_DATA_LENGTH, 0, false, false))
    {
        if (inquiryData[5] & BIT0)
        {
            protectionSupportInfo->deviceSupportsProtection = true;
            //now read the extended inquiry data VPD page
            if (SUCCESS == scsi_Inquiry(device, inquiryData, INQ_RETURN_DATA_LENGTH, EXTENDED_INQUIRY_DATA, true, false))
            {
                switch (inquiryData[0] & 0x1F)
                {
                case 0://direct access block device
                    switch ((inquiryData[4] >> 3) & 0x07)//spt field
                    {
                    case 0:
                        protectionSupportInfo->protectionType1Supported = true;
                        protectionSupportInfo->protectionType2Supported = false;
                        protectionSupportInfo->protectionType3Supported = false;
                        break;
                    case 1:
                        protectionSupportInfo->protectionType1Supported = true;
                        protectionSupportInfo->protectionType2Supported = true;
                        protectionSupportInfo->protectionType3Supported = false;
                        break;
                    case 2:
                        protectionSupportInfo->protectionType1Supported = false;
                        protectionSupportInfo->protectionType2Supported = true;
                        protectionSupportInfo->protectionType3Supported = false;
                        break;
                    case 3:
                        protectionSupportInfo->protectionType1Supported = true;
                        protectionSupportInfo->protectionType2Supported = false;
                        protectionSupportInfo->protectionType3Supported = true;
                        break;
                    case 4:
                        protectionSupportInfo->protectionType1Supported = false;
                        protectionSupportInfo->protectionType2Supported = false;
                        protectionSupportInfo->protectionType3Supported = true;
                        break;
                    case 5:
                        protectionSupportInfo->protectionType1Supported = false;
                        protectionSupportInfo->protectionType2Supported = true;
                        protectionSupportInfo->protectionType3Supported = true;
                        break;
                    case 6:
                        protectionSupportInfo->seeSupportedBlockLengthsAndProtectionTypesVPDPage = true;
                        break;
                    case 7:
                        protectionSupportInfo->protectionType1Supported = true;
                        protectionSupportInfo->protectionType2Supported = true;
                        protectionSupportInfo->protectionType3Supported = true;
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
    else
    {
        protectionSupportInfo->deviceSupportsProtection = false;
        protectionSupportInfo->protectionType1Supported = false;
        protectionSupportInfo->protectionType2Supported = false;
        protectionSupportInfo->protectionType3Supported = false;
        protectionSupportInfo->seeSupportedBlockLengthsAndProtectionTypesVPDPage = false;
    }
    safe_Free(inquiryData);
    return ret;
}

void show_Supported_Protection_Types(ptrProtectionSupport protectionSupportInfo)
{
    if (protectionSupportInfo)
    {
        printf("Supported Protection Types:\n");
        printf("---------------------------\n");
        if (protectionSupportInfo->deviceSupportsProtection)
        {
            if (protectionSupportInfo->seeSupportedBlockLengthsAndProtectionTypesVPDPage)
            {
                printf("Please see the supported block lengths and protection types VPD page.\n");
                printf("This page shows which protection types are supported with each logical block size.\n");
            }
            else
            {
                printf("Type 0 - No protection beyond transport protocol\n");//this is always supported
                if (protectionSupportInfo->protectionType1Supported)
                {
                    printf("Type 1 - Logical Block Guard and Logical Block Reference Tag\n");
                }
                if (protectionSupportInfo->protectionType2Supported)
                {
                    printf("Type 2 - Logical Block Guard and Logical Block Reference Tag (except first block)\n\t32byte read/write CDBs allowed\n");
                }
                if (protectionSupportInfo->protectionType3Supported)
                {
                    printf("Type 3 - Logical Block Guard\n");
                }
            }
        }
        else
        {
            printf("Type 0 - No protection beyond transport protocol\n");
        }
    }
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
