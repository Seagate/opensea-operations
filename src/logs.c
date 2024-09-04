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
#include "secure_file.h"

#include "logs.h"
#include "ata_helper_func.h"
#include "scsi_helper_func.h"
#include "nvme_helper.h"
#include "nvme_operations.h"
#include "smart.h"
#include "dst.h"
#include "operations_Common.h"
#include "vendor/seagate/seagate_ata_types.h"
#include "vendor/seagate/seagate_scsi_types.h"

//Idea: Try to recursively call this with identify commands to retry up to 5 times to get a valid SN or ID before returning unknown
const char* get_Drive_ID_For_Logfile_Name(tDevice *device)
{
    if (device)
    {
        //Try SN first
        if ((device->drive_info.interface_type == USB_INTERFACE || device->drive_info.interface_type == IEEE_1394_INTERFACE) && safe_strnlen(device->drive_info.bridge_info.childDriveSN, SERIAL_NUM_LEN) > 0)
        {
            return device->drive_info.bridge_info.childDriveSN;
        }
        else if (safe_strnlen(device->drive_info.serialNumber, SERIAL_NUM_LEN) > 0)
        {
            return device->drive_info.serialNumber;
        }
        else
        {
            return "Unknown";
        }
    }
    else
    {
        return M_NULLPTR;
    }
}

eReturnValues create_And_Open_Secure_Log_File_Dev_EZ(tDevice *device,
                                                secureFileInfo **file, /*required*/
                                                eLogFileNamingConvention logFileNamingConvention, /*required*/
                                                const char *logPath, //optional /*requested path to output to. Will be checked for security. If NULL, current directory will be used*/
                                                const char *logName, //optional /*name of the log file from the drive, FARM, DST, etc*/
                                                const char *logExt //optional /*extension for the log file. If NULL, set to .bin*/
    )
{
    return create_And_Open_Secure_Log_File(get_Drive_ID_For_Logfile_Name(device), safe_strlen(get_Drive_ID_For_Logfile_Name(device)), 
                file, logFileNamingConvention, logPath, safe_strnlen(logPath, OPENSEA_PATH_MAX), 
                logName, safe_strlen(logName), logExt, safe_strlen(logExt));
}

eReturnValues get_ATA_Log_Size(tDevice *device, uint8_t logAddress, uint32_t *logFileSize, bool gpl, bool smart)
{
    eReturnValues ret = NOT_SUPPORTED;//assume the log is not supported
    bool foundInGPL = false;

#ifdef _DEBUG
    printf("%s: logAddress %d, gpl=%s, smart=%s\n", __FUNCTION__, logAddress, gpl ? "true" : "false", smart ? "true" : "false");
#endif

    uint8_t *logBuffer = C_CAST(uint8_t*, safe_calloc_aligned(LEGACY_DRIVE_SEC_SIZE, sizeof(uint8_t), device->os_info.minimumAlignment));
    if (!logBuffer)
    {
        return MEMORY_FAILURE;
    }
    *logFileSize = 0;//make sure we set this to zero in case we don't find it.
    if (gpl && device->drive_info.ata_Options.generalPurposeLoggingSupported) //greater than one means check for it in the GPL directory
    {
        //first, check to see if the log is in the GPL directory.
        if (send_ATA_Read_Log_Ext_Cmd(device, ATA_LOG_DIRECTORY, 0, logBuffer, LEGACY_DRIVE_SEC_SIZE, 0) == SUCCESS)
        {
            *logFileSize = M_BytesTo2ByteValue(logBuffer[(logAddress * 2) + 1], logBuffer[(logAddress * 2)]) * LEGACY_DRIVE_SEC_SIZE;
            if (*logFileSize > 0)
            {
                ret = SUCCESS;
                foundInGPL = true;
            }
            else
            {
#ifdef _DEBUG
                printf("\t Didn't find it in GPL\n");
#endif
            }
        }
    }
    else
    {
#ifdef _DEBUG
        printf("\t generalPurposeLoggingSupported=%d\n", device->drive_info.ata_Options.generalPurposeLoggingSupported);
#endif
    }
    if (smart && !foundInGPL)
    {
        if (gpl)
        {
            //if we already tried the GPL buffer, make sure we clean it back up before we check again just to be safe.
            memset(logBuffer, 0, LEGACY_DRIVE_SEC_SIZE);
        }
        if (ata_SMART_Read_Log(device, ATA_LOG_DIRECTORY, logBuffer, LEGACY_DRIVE_SEC_SIZE) == SUCCESS)
        {
            *logFileSize = M_BytesTo2ByteValue(logBuffer[(logAddress * 2) + 1], logBuffer[(logAddress * 2)]) * LEGACY_DRIVE_SEC_SIZE;
            if (*logFileSize > 0)
            {
                ret = SUCCESS;
            }
        }
        else if (is_SMART_Enabled(device) && is_SMART_Error_Logging_Supported(device))
        {
            //The directory log is marked as optional and is supposed to return aborted IF the drive does not support multi-sector logs.
            //Only do this for summary error, comprehensive error, self-test log, and selective self-test log
            switch (logAddress)
            {
            case ATA_LOG_SUMMARY_SMART_ERROR_LOG:
                *logFileSize = UINT32_C(512);
                break;
            case ATA_LOG_SMART_SELF_TEST_LOG:
                if (is_Self_Test_Supported(device))
                {
                    *logFileSize = UINT32_C(512);
                }
                break;
            case ATA_LOG_SELECTIVE_SELF_TEST_LOG:
                if (is_Self_Test_Supported(device) && is_Selective_Self_Test_Supported(device))
                {
                    *logFileSize = UINT32_C(512);
                }
                break;
            case ATA_LOG_COMPREHENSIVE_SMART_ERROR_LOG:
                //first appears in ATA/ATAPI-6
                //If the drive does not report at least this version, do not return a size for it as it will not be supported
                if (is_ATA_Identify_Word_Valid(device->drive_info.IdentifyData.ata.Word080) && (device->drive_info.IdentifyData.ata.Word080 & 0xFFC0))
                {
                    *logFileSize = UINT32_C(512);
                }
                else
                {
                    *logFileSize = UINT32_C(0);
                }
                break;
            default:
                *logFileSize = UINT32_C(0);
                break;
            }
            if (*logFileSize > 0)
            {
                ret = SUCCESS;
            }
        }
    }
    safe_free_aligned(&logBuffer);
    return ret;
}

eReturnValues get_SCSI_Log_Size(tDevice *device, uint8_t logPage, uint8_t logSubPage, uint32_t *logFileSize)
{
    eReturnValues ret = NOT_SUPPORTED;//assume the log is not supported
    uint8_t *logBuffer = C_CAST(uint8_t*, safe_calloc_aligned(255, sizeof(uint8_t), device->os_info.minimumAlignment));
    if (!logBuffer)
    {
        return MEMORY_FAILURE;
    }
    *logFileSize = 0;
    //first check that the logpage is supported
    if (logSubPage != 0 && SUCCESS == scsi_Log_Sense_Cmd(device, false, LPC_CUMULATIVE_VALUES, LP_SUPPORTED_LOG_PAGES_AND_SUBPAGES, 0xFF, 0, logBuffer, 255))
    {
        //validate the page code and subpage code
        uint8_t pageCode = M_GETBITRANGE(logBuffer[0], 5, 0);
        uint8_t subpageCode = logBuffer[1];
        bool spf = M_ToBool(logBuffer[0] & BIT6);
        if (spf && pageCode == LP_SUPPORTED_LOG_PAGES_AND_SUBPAGES && subpageCode == 0xFF)
        {
            uint16_t pageSupportIter = SCSI_LOG_PARAMETER_HEADER_LENGTH;
            uint16_t pageLen = M_BytesTo2ByteValue(logBuffer[2], logBuffer[3]) + SCSI_LOG_PARAMETER_HEADER_LENGTH;
            //search the buffer for the page we want
            for (pageSupportIter = SCSI_LOG_PARAMETER_HEADER_LENGTH; pageSupportIter < pageLen; pageSupportIter += 2)
            {
                if (logBuffer[pageSupportIter] == logPage && logBuffer[pageSupportIter + 1] == logSubPage)
                {
                    ret = SUCCESS;
                    break;
                }
            }
        }
        else
        {
            //the device did not return the data that is was asked to, so it does not appear subpages are supported at all
            //since this is only looking on this page for subpages, call this page not supported since the returned list was not as expected
            ret = NOT_SUPPORTED;
            *logFileSize = 0;
        }
    }
    else if (logSubPage == 0 && SUCCESS == scsi_Log_Sense_Cmd(device, false, LPC_CUMULATIVE_VALUES, LP_SUPPORTED_LOG_PAGES, 0, 0, logBuffer, 255))
    {
        uint16_t pageSupportIter = SCSI_LOG_PARAMETER_HEADER_LENGTH;
        uint16_t pageLen = M_BytesTo2ByteValue(logBuffer[2], logBuffer[3]) + SCSI_LOG_PARAMETER_HEADER_LENGTH;
        //search the buffer for the page we want
        for (pageSupportIter = SCSI_LOG_PARAMETER_HEADER_LENGTH; pageSupportIter < pageLen; ++pageSupportIter)
        {
            if (logBuffer[pageSupportIter] == logPage)
            {
                ret = SUCCESS;
                break;
            }
        }
    }
    //we know the page is supported, but to get the size, we need to try reading it.
    if (ret == SUCCESS)
    {
        memset(logBuffer, 0, 255);
        //only requesting the header since this should get us the total length.
        //If this fails, we return success, but a size of zero. This shouldn't happen, but there are firmware bugs...
        if (scsi_Log_Sense_Cmd(device, false, LPC_CUMULATIVE_VALUES, logPage, logSubPage, 0, logBuffer, SCSI_LOG_PARAMETER_HEADER_LENGTH) == SUCCESS)
        {
            //validate the page code and subpage code
            uint8_t pageCode = M_GETBITRANGE(logBuffer[0], 5, 0);
            uint8_t subpageCode = logBuffer[1];
            bool spf = M_ToBool(logBuffer[0] & BIT6);
            if (logSubPage != 0 && spf && pageCode == logPage && subpageCode == logSubPage)
            {
                *logFileSize = C_CAST(uint32_t, M_BytesTo2ByteValue(logBuffer[2], logBuffer[3]) + SCSI_LOG_PARAMETER_HEADER_LENGTH);
            }
            else if (pageCode == logPage && !spf && subpageCode == 0)
            {
                *logFileSize = C_CAST(uint32_t, M_BytesTo2ByteValue(logBuffer[2], logBuffer[3]) + SCSI_LOG_PARAMETER_HEADER_LENGTH);
            }
            else
            {
                //not the page we requested, return NOT_SUPPORTED
                ret = NOT_SUPPORTED;
                *logFileSize = 0;
            }
        }
        else //trust that the page is supported, but this method of getting the size is broken on this firmware/device
        {
            *logFileSize = UINT16_MAX;//maximum transfer for a log page, so return this so that the page can at least be read....
        }
    }
    safe_free_aligned(&logBuffer);
    return ret;
}

eReturnValues get_SCSI_VPD_Page_Size(tDevice *device, uint8_t vpdPage, uint32_t *vpdPageSize)
{
    eReturnValues ret = NOT_SUPPORTED;//assume the page is not supported
    uint32_t vpdBufferLength = INQ_RETURN_DATA_LENGTH;
    uint8_t *vpdBuffer = C_CAST(uint8_t *, safe_calloc_aligned(vpdBufferLength, sizeof(uint8_t), device->os_info.minimumAlignment));
    if (!vpdBuffer)
    {
        if (VERBOSITY_QUIET < device->deviceVerbosity)
        {
            perror("Calloc failure!\n");
        }
        return MEMORY_FAILURE;
    }
    *vpdPageSize = 0;
    if (SUCCESS == scsi_Inquiry(device, vpdBuffer, vpdBufferLength, SUPPORTED_VPD_PAGES, true, false))
    {
        //now search the returned buffer for the requested page code
        uint32_t vpdIter = SCSI_VPD_PAGE_HEADER_LENGTH;
        uint16_t pageLength = M_BytesTo2ByteValue(vpdBuffer[2], vpdBuffer[3]);
        for (vpdIter = SCSI_VPD_PAGE_HEADER_LENGTH; vpdIter < UINT16_MAX && vpdIter <= C_CAST(uint32_t, pageLength + SCSI_VPD_PAGE_HEADER_LENGTH) && vpdIter < vpdBufferLength; vpdIter++)
        {
            if (vpdBuffer[vpdIter] == vpdPage)
            {
                ret = SUCCESS;
                break;
            }
        }
        if (ret == SUCCESS)
        {
            memset(vpdBuffer, 0, vpdBufferLength);
            //read the page so we can see how large it is.
            if (SUCCESS == scsi_Inquiry(device, vpdBuffer, vpdBufferLength, vpdPage, true, false))
            {
                *vpdPageSize = C_CAST(uint32_t, M_BytesTo2ByteValue(vpdBuffer[2], vpdBuffer[3]) + SCSI_VPD_PAGE_HEADER_LENGTH);
            }
        }
    }
    safe_free_aligned(&vpdBuffer);
    return ret;
}

//modePageSize includes any blockdescriptors that may be present
eReturnValues get_SCSI_Mode_Page_Size(tDevice *device, eScsiModePageControl mpc, uint8_t modePage, uint8_t subpage, uint32_t *modePageSize)
{
    eReturnValues ret = NOT_SUPPORTED;//assume the page is not supported
    uint32_t modeLength = MODE_PARAMETER_HEADER_10_LEN + SHORT_LBA_BLOCK_DESCRIPTOR_LEN;
    bool sixByte = false;
    bool retriedMP6 = false;//only for automatic hack
    if (device->drive_info.passThroughHacks.scsiHacks.noModePages)
    {
        return NOT_SUPPORTED;
    }
    else if (subpage > 0 && device->drive_info.passThroughHacks.scsiHacks.noModeSubPages)
    {
        return NOT_SUPPORTED;
    }
    //If device is older than SCSI2, DBD is not available and will be limited to 6 byte command
    //checking for this for old drives that may support mode pages, but not the dbd bit properly
    //Earlier than SCSI 2, RBC devices, and CCS compliant devices are assumed to only support mode sense 6 commands.
    if (device->drive_info.scsiVersion < SCSI_VERSION_SCSI2
        || M_GETBITRANGE(device->drive_info.scsiVpdData.inquiryData[0], 4, 0) == PERIPHERAL_SIMPLIFIED_DIRECT_ACCESS_DEVICE
        || M_GETBITRANGE(device->drive_info.scsiVpdData.inquiryData[3], 3, 0) == INQ_RESPONSE_FMT_CCS
        || device->drive_info.passThroughHacks.scsiHacks.mode6bytes
        || (device->drive_info.passThroughHacks.scsiHacks.useMode6BForSubpageZero && subpage == 0))
    {
        sixByte = true;
        modeLength = MODE_PARAMETER_HEADER_6_LEN + SHORT_LBA_BLOCK_DESCRIPTOR_LEN;
    }
    uint8_t *modeBuffer = C_CAST(uint8_t *, safe_calloc_aligned(modeLength, sizeof(uint8_t), device->os_info.minimumAlignment));
    if (!modeBuffer)
    {
        if (VERBOSITY_QUIET < device->deviceVerbosity)
        {
            perror("Calloc failure!\n");
        }
        return MEMORY_FAILURE;
    }
    *modePageSize = 0;
    if (!sixByte)
    {
        bool longlba = false;
        if (device->drive_info.deviceMaxLba > UINT32_MAX)
        {
            longlba = true;
        }
        if (SUCCESS == scsi_Mode_Sense_10(device, modePage, modeLength, subpage, false, longlba, mpc, modeBuffer))
        {
            //validate the correct page was returned!
            uint16_t blockDescLen = M_BytesTo2ByteValue(modeBuffer[6], modeBuffer[7]);
            if (modePage == M_GETBITRANGE(modeBuffer[MODE_PARAMETER_HEADER_10_LEN + blockDescLen], 5, 0))
            {
                if (subpage > 0)
                {
                    //validate we received a subpage correctly
                    if (modeBuffer[MODE_PARAMETER_HEADER_10_LEN + blockDescLen] & BIT6)
                    {
                        if (subpage != modeBuffer[MODE_PARAMETER_HEADER_10_LEN + blockDescLen + 1])
                        {
                            //subpage value does not match the request
                            ret = FAILURE;
                        }
                    }
                    else
                    {
                        ret = FAILURE;
                    }
                }
            }
            else
            {
                //page code already does not match!
                //consider this a failure!
                ret = FAILURE;
            }
            if (ret != FAILURE)
            {
                *modePageSize = M_BytesTo2ByteValue(modeBuffer[0], modeBuffer[1]) + 2;
                ret = SUCCESS;
            }
        }
        else
        {
            //if invalid operation code, then we should retry
            senseDataFields senseFields;
            memset(&senseFields, 0, sizeof(senseDataFields));
            get_Sense_Data_Fields(device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, &senseFields);
            if (senseFields.scsiStatusCodes.senseKey == SENSE_KEY_ILLEGAL_REQUEST && senseFields.scsiStatusCodes.asc == 0x20 && senseFields.scsiStatusCodes.ascq == 0x00)//checking for invalid operation code
            {
                sixByte = true;
                modeLength = MODE_PARAMETER_HEADER_6_LEN + SHORT_LBA_BLOCK_DESCRIPTOR_LEN;
                //reallocate memory!
                uint8_t* temp = C_CAST(uint8_t*, safe_reallocf_aligned(C_CAST(void**, &modeBuffer), 0, modeLength, device->os_info.minimumAlignment));
                if (!temp)
                {
                    return MEMORY_FAILURE;
                }
                modeBuffer = temp;
            }
            else if (senseFields.scsiStatusCodes.senseKey == SENSE_KEY_ILLEGAL_REQUEST && senseFields.scsiStatusCodes.asc == 0x24 && senseFields.scsiStatusCodes.ascq == 0x00)//invalid field in CDB
            {
                //could be a mode page is not supported, or MPC is not a correct value, among other errors
                //Try checking sense key speciic
                if (senseFields.senseKeySpecificInformation.type == SENSE_KEY_SPECIFIC_FIELD_POINTER)
                {
                    //if we are getting a sense key specific field pointer, this is a SAS drive and there is no need to retry anthing.
                    //set this hack to valid as there is no need to retry the 6 byte command
                    ret = NOT_SUPPORTED;
                }
                else if (subpage == 0 && !device->drive_info.passThroughHacks.scsiHacks.useMode6BForSubpageZero && device->drive_info.passThroughHacks.scsiHacks.mp6sp0Success == 0)
                {
                    //this hack has not already been tested for, so retry with mode sense 6
                    sixByte = true;
                    retriedMP6 = true;
                }
                else
                {
                    ret = NOT_SUPPORTED;
                }
            }
            else
            {
                ret = FAILURE;
            }
        }
    }
    if (sixByte)//not an else because in the above if, we can retry the command as 6 byte if it doesn't work.
    {
        if (SUCCESS == scsi_Mode_Sense_6(device, modePage, C_CAST(uint8_t, modeLength), subpage, false, mpc, modeBuffer))//don't disable block descriptors here since this is mostly to support old drives.
        {
            //validate the correct page was returned!
            uint8_t blockDescLen = modeBuffer[2];
            if (modePage == M_GETBITRANGE(modeBuffer[MODE_PARAMETER_HEADER_6_LEN + blockDescLen], 5, 0))
            {
                if (subpage > 0)
                {
                    //validate we received a subpage correctly
                    if (modeBuffer[MODE_PARAMETER_HEADER_10_LEN + blockDescLen] & BIT6)
                    {
                        if (subpage != modeBuffer[MODE_PARAMETER_HEADER_10_LEN + blockDescLen + 1])
                        {
                            //subpage value does not match the request
                            ret = FAILURE;
                        }
                    }
                    else
                    {
                        ret = FAILURE;
                    }
                }
            }
            else
            {
                //page code already does not match!
                //consider this a failure!
                ret = FAILURE;
            }
            if (ret != FAILURE)
            {
                *modePageSize = modeBuffer[0] + 1;
                ret = SUCCESS;
            }
        }
    }
    //if we are here and this hack has not already been validated, then validate it to skip future retries.
    if (retriedMP6 && ret == SUCCESS && device->drive_info.passThroughHacks.scsiHacks.mp6sp0Success > 0 && !is_Empty(modeBuffer, modeLength))
    {
        device->drive_info.passThroughHacks.scsiHacks.useMode6BForSubpageZero = true;
    }
    safe_free_aligned(&modeBuffer);
    return ret;
}

eReturnValues get_SCSI_Mode_Page(tDevice *device, eScsiModePageControl mpc, uint8_t modePage, uint8_t subpage, const char *logName, const char *fileExtension, bool toBuffer, uint8_t *myBuf, uint32_t bufSize, const char * const filePath, bool *used6ByteCmd)
{
    eReturnValues ret = NOT_SUPPORTED;//assume the page is not supported
    uint32_t modeLength = 0;
    bool retriedMP6 = false;//only for automatic hack
    if (device->drive_info.passThroughHacks.scsiHacks.noModePages)
    {
        return NOT_SUPPORTED;
    }
    else if (subpage > 0 && device->drive_info.passThroughHacks.scsiHacks.noModeSubPages)
    {
        return NOT_SUPPORTED;
    }
    if (toBuffer)
    {
        modeLength = bufSize;
        ret = SUCCESS;
    }
    else if (SUCCESS != get_SCSI_Mode_Page_Size(device, mpc, modePage, subpage, &modeLength))
    {
        return ret;
    }
    bool sixByte = false;
    //If device is older than SCSI2, DBD is not available and will be limited to 6 byte command
    //checking for this for old drives that may support mode pages, but not the dbd bit properly
    //Earlier than SCSI 2, RBC devices, and CCS compliant devices are assumed to only support mode sense 6 commands.
    if (device->drive_info.scsiVersion < SCSI_VERSION_SCSI2
        || M_GETBITRANGE(device->drive_info.scsiVpdData.inquiryData[0], 4, 0) == PERIPHERAL_SIMPLIFIED_DIRECT_ACCESS_DEVICE
        || M_GETBITRANGE(device->drive_info.scsiVpdData.inquiryData[3], 3, 0) == INQ_RESPONSE_FMT_CCS
        || device->drive_info.passThroughHacks.scsiHacks.mode6bytes
        || (device->drive_info.passThroughHacks.scsiHacks.useMode6BForSubpageZero && subpage == 0))
    {
        sixByte = true;
    }
    uint8_t *modeBuffer = C_CAST(uint8_t *, safe_calloc_aligned(modeLength, sizeof(uint8_t), device->os_info.minimumAlignment));
    if (!modeBuffer)
    {
        if (VERBOSITY_QUIET < device->deviceVerbosity)
        {
            perror("Calloc failure!\n");
        }
        return MEMORY_FAILURE;
    }
    if (!sixByte)
    {
        //do not disable block descriptor for consistency between mode sense 6 and 10.
        bool longlba = false;
        if (device->drive_info.deviceMaxLba > UINT32_MAX)
        {
            longlba = true;
        }
        if (SUCCESS == scsi_Mode_Sense_10(device, modePage, modeLength, subpage, false, longlba, mpc, modeBuffer))
        {
            secureFileInfo *fpmp = M_NULLPTR;
            bool fileOpened = false;
            ret = SUCCESS;
            if (used6ByteCmd)
            {
                *used6ByteCmd = false;
            }
            //validate the correct page was returned!
            uint16_t blockDescLen = M_BytesTo2ByteValue(modeBuffer[6], modeBuffer[7]);
            if (modePage == M_GETBITRANGE(modeBuffer[MODE_PARAMETER_HEADER_10_LEN + blockDescLen], 5, 0))
            {
                if (subpage > 0)
                {
                    //validate we received a subpage correctly
                    if (modeBuffer[MODE_PARAMETER_HEADER_10_LEN + blockDescLen] & BIT6)
                    {
                        if (subpage != modeBuffer[MODE_PARAMETER_HEADER_10_LEN + blockDescLen + 1])
                        {
                            //subpage value does not match the request
                            ret = FAILURE;
                        }
                    }
                    else
                    {
                        ret = FAILURE;
                    }
                }
            }
            else
            {
                //page code already does not match!
                //consider this a failure!
                ret = FAILURE;
            }
            if (!toBuffer && !fileOpened && ret != FAILURE)
            {
                if (SUCCESS == create_And_Open_Secure_Log_File_Dev_EZ(device, &fpmp, NAMING_SERIAL_NUMBER_DATE_TIME, filePath, logName, fileExtension))
                {
                    fileOpened = true;
                }
                else
                {
                    if (VERBOSITY_QUIET < device->deviceVerbosity)
                    {
                        printf("Failed to open file!\n");
                    }
                    ret = FAILURE;
                    safe_free_aligned(&modeBuffer);
                    free_Secure_File_Info(&fpmp);
                }
            }
            if (fileOpened && ret != FAILURE)
            {
                //write the mode page to a file
                if (SEC_FILE_SUCCESS != secure_Write_File(fpmp, modeBuffer, modeLength, sizeof(uint8_t), modeLength, M_NULLPTR))
                {
                    if (VERBOSITY_QUIET < device->deviceVerbosity)
                    {
                        perror("Error writing vpd data to a file!\n");
                    }
                    if (SEC_FILE_SUCCESS != secure_Close_File(fpmp))
                    {
                        printf("Error closing file!\n");
                    }
                    fileOpened = false;
                    safe_free_aligned(&modeBuffer);
                    free_Secure_File_Info(&fpmp);
                    return ERROR_WRITING_FILE;
                }

            }
            if (toBuffer && ret != FAILURE)
            {
                if (0 != safe_memcpy(myBuf, bufSize, modeBuffer, modeLength))
                {
                    return BAD_PARAMETER;
                }
            }
            if (fileOpened)
            {
                if (SEC_FILE_SUCCESS != secure_Flush_File(fpmp))
                {
                    if (VERBOSITY_QUIET < device->deviceVerbosity)
                    {
                        perror("Error flushing data!\n");
                    }
                    if (SEC_FILE_SUCCESS != secure_Close_File(fpmp))
                    {
                        printf("Error closing file!\n");
                    }
                    fileOpened = false;
                    safe_free_aligned(&modeBuffer);
                    free_Secure_File_Info(&fpmp);
                    return ERROR_WRITING_FILE;
                }

                if (SEC_FILE_SUCCESS != secure_Close_File(fpmp))
                {
                    printf("Error closing file!\n");
                }
                fileOpened = false;
                free_Secure_File_Info(&fpmp);
            }
        }
        else
        {
            //if invalid operation code, then we should retry
            senseDataFields senseFields;
            memset(&senseFields, 0, sizeof(senseDataFields));
            get_Sense_Data_Fields(device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, &senseFields);
            if (senseFields.scsiStatusCodes.senseKey == SENSE_KEY_ILLEGAL_REQUEST && senseFields.scsiStatusCodes.asc == 0x20 && senseFields.scsiStatusCodes.ascq == 0x00)//checking for invalid operation code
            {
                sixByte = true;
                modeLength = MODE_PARAMETER_HEADER_6_LEN + SHORT_LBA_BLOCK_DESCRIPTOR_LEN;
                //reallocate memory!
                uint8_t *temp = C_CAST(uint8_t*, safe_reallocf_aligned(C_CAST(void**, &modeBuffer), 0, modeLength, device->os_info.minimumAlignment));
                if (!temp)
                {
                    return MEMORY_FAILURE;
                }
                modeBuffer = temp;
            }
            else if (senseFields.scsiStatusCodes.senseKey == SENSE_KEY_ILLEGAL_REQUEST && senseFields.scsiStatusCodes.asc == 0x24 && senseFields.scsiStatusCodes.ascq == 0x00)//invalid field in CDB
            {
                //could be a mode page is not supported, or MPC is not a correct value, among other errors
                //Try checking sense key speciic
                if (senseFields.senseKeySpecificInformation.type == SENSE_KEY_SPECIFIC_FIELD_POINTER)
                {
                    //if we are getting a sense key specific field pointer, this is a SAS drive and there is no need to retry anthing.
                    //set this hack to valid as there is no need to retry the 6 byte command
                    ret = NOT_SUPPORTED;
                }
                else if (subpage == 0 && !device->drive_info.passThroughHacks.scsiHacks.useMode6BForSubpageZero && device->drive_info.passThroughHacks.scsiHacks.mp6sp0Success == 0)
                {
                    //this hack has not already been tested for, so retry with mode sense 6
                    sixByte = true;
                    retriedMP6 = true;
                }
                else
                {
                    ret = NOT_SUPPORTED;
                }
            }
            else
            {
                ret = FAILURE;
            }
        }
    }
    if (sixByte)//not an else because in the above if, we can retry the command as 6 byte if it doesn't work.
    {
        if (SUCCESS == scsi_Mode_Sense_6(device, modePage, C_CAST(uint8_t, modeLength), subpage, false, mpc, modeBuffer))//don't disable block descriptors here since this is mostly to support old drives.
        {
            secureFileInfo *fpmp = M_NULLPTR;
            bool fileOpened = false;
            ret = SUCCESS;
            if (used6ByteCmd)
            {
                *used6ByteCmd = true;
            }
            //validate the correct page was returned!
            uint8_t blockDescLen = modeBuffer[2];
            if (modePage == M_GETBITRANGE(modeBuffer[MODE_PARAMETER_HEADER_6_LEN + blockDescLen], 5, 0))
            {
                if (subpage > 0)
                {
                    //validate we received a subpage correctly
                    if (modeBuffer[MODE_PARAMETER_HEADER_10_LEN + blockDescLen] & BIT6)
                    {
                        if (subpage != modeBuffer[MODE_PARAMETER_HEADER_10_LEN + blockDescLen + 1])
                        {
                            //subpage value does not match the request
                            ret = FAILURE;
                        }
                    }
                    else
                    {
                        ret = FAILURE;
                    }
                }
            }
            else
            {
                //page code already does not match!
                //consider this a failure!
                ret = FAILURE;
            }
            if (!toBuffer && !fileOpened && ret != FAILURE)
            {
                if (SUCCESS == create_And_Open_Secure_Log_File_Dev_EZ(device, &fpmp, NAMING_SERIAL_NUMBER_DATE_TIME, filePath, logName, fileExtension))
                {
                    fileOpened = true;
                }
                else
                {
                    if (VERBOSITY_QUIET < device->deviceVerbosity)
                    {
                        printf("Failed to open file!\n");
                    }
                    ret = FAILURE;
                    safe_free_aligned(&modeBuffer);
                    free_Secure_File_Info(&fpmp);
                }
            }
            if (fileOpened && ret != FAILURE)
            {
                //write the vpd page to a file
                if (SEC_FILE_SUCCESS != secure_Write_File(fpmp, modeBuffer, modeLength, sizeof(uint8_t), modeLength, M_NULLPTR))
                {
                    if (VERBOSITY_QUIET < device->deviceVerbosity)
                    {
                        perror("Error writing vpd data to a file!\n");
                    }
                    if (SEC_FILE_SUCCESS != secure_Close_File(fpmp))
                    {
                        printf("Error closing file!\n");
                    }
                    fileOpened = false;
                    safe_free_aligned(&modeBuffer);
                    free_Secure_File_Info(&fpmp);
                    return ERROR_WRITING_FILE;
                }
            }
            if (toBuffer && ret != FAILURE)
            {
                if (0 != safe_memcpy(myBuf, bufSize, modeBuffer, modeLength))
                {
                    return BAD_PARAMETER;
                }
            }
            if (fileOpened)
            {
                if (SEC_FILE_SUCCESS != secure_Flush_File(fpmp))
                {
                    if (VERBOSITY_QUIET < device->deviceVerbosity)
                    {
                        perror("Error flushing data!\n");
                    }
                    if (SEC_FILE_SUCCESS != secure_Close_File(fpmp))
                    {
                        printf("Error closing file!\n");
                    }
                    fileOpened = false;
                    safe_free_aligned(&modeBuffer);
                    free_Secure_File_Info(&fpmp);
                    return ERROR_WRITING_FILE;
                }
                if (SEC_FILE_SUCCESS != secure_Close_File(fpmp))
                {
                    printf("Error closing file!\n");
                }
                fileOpened = false;
                free_Secure_File_Info(&fpmp);
            }
        }
        else
        {
            ret = FAILURE;
        }
    }
    //if we are here and this hack has not already been validated, then validate it to skip future retries.
    if (device->drive_info.passThroughHacks.scsiHacks.mp6sp0Success > 0 && retriedMP6 && ret == SUCCESS && !is_Empty(modeBuffer, modeLength))
    {
        device->drive_info.passThroughHacks.scsiHacks.useMode6BForSubpageZero = true;
    }
    safe_free_aligned(&modeBuffer);
    return ret;
}

bool is_SCSI_Read_Buffer_16_Supported(tDevice *device)
{
    bool supported = false;
    DECLARE_ZERO_INIT_ARRAY(uint8_t, reportSupportedOperationCode, 20);
    if (!device->drive_info.passThroughHacks.scsiHacks.noReportSupportedOperations && SUCCESS == scsi_Report_Supported_Operation_Codes(device, false, REPORT_OPERATION_CODE, READ_BUFFER_16_CMD, 0, 20, reportSupportedOperationCode))
    {
        if (M_GETBITRANGE(reportSupportedOperationCode[1], 2, 0) == 3)//matches the spec
        {
            supported = true;
        }
    }
    return supported;
}

#define SCSI_ERROR_HISTORY_DIRECTORY_LEN 2088
eReturnValues get_SCSI_Error_History_Size(tDevice *device, uint8_t bufferID, uint32_t *errorHistorySize, bool createNewSnapshot, bool useReadBuffer16)
{
    eReturnValues ret = NOT_SUPPORTED;
    if (!errorHistorySize)
    {
        return BAD_PARAMETER;
    }
    uint8_t *errorHistoryDirectory = C_CAST(uint8_t*, safe_calloc_aligned(SCSI_ERROR_HISTORY_DIRECTORY_LEN, sizeof(uint8_t), device->os_info.minimumAlignment));
    if (!errorHistoryDirectory)
    {
        return MEMORY_FAILURE;
    }
    uint8_t directoryID = 0;
    if (createNewSnapshot)
    {
        directoryID = 1;
    }
    bool gotData = false;
    if (useReadBuffer16)
    {
        if (SUCCESS == scsi_Read_Buffer_16(device, 0x1C, 0, directoryID, 0, SCSI_ERROR_HISTORY_DIRECTORY_LEN, errorHistoryDirectory))
        {
            gotData = true;
        }
    }
    else
    {
        if (SUCCESS == scsi_Read_Buffer(device, 0x1C, directoryID, 0, SCSI_ERROR_HISTORY_DIRECTORY_LEN, errorHistoryDirectory))
        {
            gotData = true;
        }
    }
    if (gotData)
    {
        uint16_t directoryLength = M_BytesTo2ByteValue(errorHistoryDirectory[30], errorHistoryDirectory[31]);
        #define SCSI_ERROR_HISTORY_DIRECTORY_DESC_START 32
        for (uint32_t directoryIter = SCSI_ERROR_HISTORY_DIRECTORY_DESC_START; directoryIter < C_CAST(uint32_t, directoryLength + SCSI_ERROR_HISTORY_DIRECTORY_DESC_START); directoryIter += 8)
        {
            if (errorHistoryDirectory[directoryIter + 0] == bufferID)
            {
                *errorHistorySize = M_BytesTo4ByteValue(errorHistoryDirectory[directoryIter + 4], errorHistoryDirectory[directoryIter + 5], errorHistoryDirectory[directoryIter + 6], errorHistoryDirectory[directoryIter + 7]);
                ret = SUCCESS;
                break;
            }
        }
    }
    safe_free_aligned(&errorHistoryDirectory);
    return ret;
}

eReturnValues get_SCSI_Error_History(tDevice *device, uint8_t bufferID, const char *logName, bool createNewSnapshot, bool useReadBuffer16, \
    const char *fileExtension, bool toBuffer, uint8_t *myBuf, uint32_t bufSize, \
    const char * const filePath, uint32_t transferSizeBytes, char *fileNameUsed)
{
    eReturnValues ret = UNKNOWN;
    uint32_t historyLen = 0;
    secureFileInfo *fp_History = M_NULLPTR;
    uint8_t *historyBuffer = M_NULLPTR;

    ret = get_SCSI_Error_History_Size(device, bufferID, &historyLen, createNewSnapshot, useReadBuffer16);

    if (ret == SUCCESS)
    {
        //If the user wants it in a buffer...just return. 
        if ((toBuffer) && (bufSize < historyLen))
        {
            return BAD_PARAMETER;
        }

        uint32_t increment = 65536;//pulling in 64K chunks...this should be ok, but we may need to change this later!
        if (transferSizeBytes != 0)
        {
            increment = transferSizeBytes;//use the user selected size (if it's non-zero)
        }
        if (increment > historyLen)
        {
            increment = historyLen;
        }
        historyBuffer = C_CAST(uint8_t *, safe_calloc_aligned(increment, sizeof(uint8_t), device->os_info.minimumAlignment));

        if (!historyBuffer)
        {
            if (VERBOSITY_QUIET < device->deviceVerbosity)
            {
                perror("Calloc failure!\n");
            }
            return MEMORY_FAILURE;
        }

        bool logFileOpened = false;
        uint64_t maxOffset = 0xFFFFFF;//24bits is maximum for standard readbuffer command
        if (useReadBuffer16)
        {
            maxOffset = UINT64_MAX;
        }
        for (uint64_t offset = 0; offset < historyLen && offset <= maxOffset; offset += increment)
        {
            bool dataRetrieved = false;
            if ((offset + increment) > historyLen)
            {
                //adjusting the pull size so we don't accidentally get an error from a drive that doesn't want to return more than the maximum it told is in this buffer ID.
                increment = C_CAST(uint32_t, historyLen - offset);
            }
            if (useReadBuffer16)
            {
                if (SUCCESS == scsi_Read_Buffer_16(device, 0x1C, 0, bufferID, offset, increment, historyBuffer))
                {
                    dataRetrieved = true;
                }
            }
            else
            {
                if (SUCCESS == scsi_Read_Buffer(device, 0x1C, bufferID, C_CAST(uint32_t, offset), increment, historyBuffer))
                {
                    dataRetrieved = true;
                }
            }
            if (dataRetrieved)
            {
                if (toBuffer)
                {
                    if (0 != safe_memcpy(&myBuf[offset], uint64_to_sizet(C_CAST(uint64_t, bufSize) - offset), historyBuffer, increment))
                    {
                        ret = BAD_PARAMETER;
                        break;
                    }
                }
                if (logName && fileExtension) //Because you can also get a log file & get it in buffer. 
                {
                    if (!logFileOpened)
                    {
                        if (SUCCESS == create_And_Open_Secure_Log_File_Dev_EZ(device, &fp_History, NAMING_SERIAL_NUMBER_DATE_TIME, filePath, logName, fileExtension))
                        {
                            logFileOpened = true;
                            if (fileNameUsed != M_NULLPTR)
                            {
                                safe_memcpy(fileNameUsed, OPENSEA_PATH_MAX, fp_History->fullpath, safe_strnlen(fp_History->fullpath, OPENSEA_PATH_MAX));
                            }
                        }
                        else
                        {
                            if (VERBOSITY_QUIET < device->deviceVerbosity)
                            {
                                printf("Failed to open file!\n");
                            }
                            ret = FAILURE;
                            safe_free_aligned(&historyBuffer);
                            free_Secure_File_Info(&fp_History);
                        }
                    }
                    if (logFileOpened)
                    {
                        //write the history data to a file
                        if (SEC_FILE_SUCCESS != secure_Write_File(fp_History, historyBuffer, increment, sizeof(uint8_t), increment, M_NULLPTR))
                        {
                            if (VERBOSITY_QUIET < device->deviceVerbosity)
                            {
                                perror("Error writing the history data to a file!\n");
                            }
                            if (SEC_FILE_SUCCESS != secure_Close_File(fp_History))
                            {
                                printf("Error closing file!\n");
                            }
                            logFileOpened = false;
                            safe_free_aligned(&historyBuffer);
                            free_Secure_File_Info(&fp_History);
                            return ERROR_WRITING_FILE;
                        }
                    }
                }
            }
            else
            {
                ret = FAILURE;
                break;
            }
        }
        if (historyLen > maxOffset)
        {
            //size of error history is greater than the maximum possible offset for the read buffer command, then need to return an error for truncated data.
            ret = TRUNCATED_FILE;
        }
        if (logFileOpened && fp_History)
        {
            if (SEC_FILE_SUCCESS != secure_Flush_File(fp_History))
            {
                if (VERBOSITY_QUIET < device->deviceVerbosity)
                {
                    perror("Error flushing data!\n");
                }
                if (SEC_FILE_SUCCESS != secure_Close_File(fp_History))
                {
                    printf("Error closing file!\n");
                }
                logFileOpened = false;
                safe_free_aligned(&historyBuffer);
                free_Secure_File_Info(&fp_History);
                return ERROR_WRITING_FILE;
            }
            if (SEC_FILE_SUCCESS != secure_Close_File(fp_History))
            {
                printf("Error closing file!\n");
            }
        }
        safe_free_aligned(&historyBuffer);
    }
    free_Secure_File_Info(&fp_History);
    return ret;
}

eReturnValues get_SMART_Extended_Comprehensive_Error_Log(tDevice *device, const char * const filePath)
{
    if (device->drive_info.drive_type == ATA_DRIVE)
    {
        return get_ATA_Log(device, ATA_LOG_EXTENDED_COMPREHENSIVE_SMART_ERROR_LOG, "SMART_Ext_Comp_Error_Log", "bin", true, false, false, M_NULLPTR, 0, filePath, 0, 0);
    }
    else
    {
        return NOT_SUPPORTED;
    }
}

eReturnValues get_ATA_DST_Log(tDevice *device, bool extLog, const char * const filePath)
{
    if (extLog)
    {
        //read from GPL
        return get_ATA_Log(device, ATA_LOG_EXTENDED_SMART_SELF_TEST_LOG, "Ext_SMART_Self_Test_Results", "bin", true, false, false, M_NULLPTR, 0, filePath, 0, 0);
    }
    else
    {
        //read from SMART
        return get_ATA_Log(device, ATA_LOG_SMART_SELF_TEST_LOG, "SMART_Self_Test_Results", "bin", false, true, false, M_NULLPTR, 0, filePath, 0, 0);
    }
}

eReturnValues get_DST_Log(tDevice *device, const char * const filePath)
{
    if (device->drive_info.drive_type == ATA_DRIVE)
    {
        return get_ATA_DST_Log(device, device->drive_info.ata_Options.generalPurposeLoggingSupported, filePath);
    }
    else if (device->drive_info.drive_type == SCSI_DRIVE)
    {
        return get_SCSI_Log(device, LP_SELF_TEST_RESULTS, 0, "Self_Test_Results", "bin", false, M_NULLPTR, 0, filePath);
    }
    else if (device->drive_info.drive_type == NVME_DRIVE)
    {
        return pull_Supported_NVMe_Logs(device, 6, PULL_LOG_BIN_FILE_MODE, 0);
    }
    else
    {
        return NOT_SUPPORTED;
    }
}

eReturnValues get_Pending_Defect_List(tDevice *device, const char * const filePath)
{
    if (device->drive_info.drive_type == ATA_DRIVE)
    {
        //new is ACS4. Can be read with standard read log command if the drive supports the log.
        return get_ATA_Log(device, ATA_LOG_PENDING_DEFECTS_LOG, "Pending_Defects", "plst", true, false, false, M_NULLPTR, 0, filePath, 0, 0);
    }
    else if (device->drive_info.drive_type == SCSI_DRIVE)
    {
        //this is new in SBC4. We can read this with a logsense command. (if the drive supports it)
        return get_SCSI_Log(device, LP_PENDING_DEFECTS, 0x01, "Pending_Defects", "plst", false, M_NULLPTR, 0, filePath);
    }
    else
    {
        return NOT_SUPPORTED;
    }
}

eReturnValues get_Identify_Device_Data_Log(tDevice *device, const char * const filePath)
{
    if (device->drive_info.drive_type == ATA_DRIVE)
    {
        return get_ATA_Log(device, ATA_LOG_IDENTIFY_DEVICE_DATA, "Identify_Device_Data_Log", "bin", true, true, false, M_NULLPTR, 0, filePath, 0, 0);
    }
    else
    {
        return NOT_SUPPORTED;
    }
}

eReturnValues get_SATA_Phy_Event_Counters_Log(tDevice *device, const char * const filePath)
{
    if (device->drive_info.drive_type == ATA_DRIVE)
    {
        return get_ATA_Log(device, ATA_LOG_SATA_PHY_EVENT_COUNTERS_LOG, "SATA_Phy_Event_Counters", "bin", true, false, false, M_NULLPTR, 0, filePath, 0, 0);
    }
    else
    {
        return NOT_SUPPORTED;
    }
}

eReturnValues get_Device_Statistics_Log(tDevice *device, const char * const filePath)
{
    if (device->drive_info.drive_type == ATA_DRIVE)
    {
        return get_ATA_Log(device, ATA_LOG_DEVICE_STATISTICS, "Device_Statistics", "bin", true, true, false, M_NULLPTR, 0, filePath, 0, 0);
    }
    else if (device->drive_info.drive_type == SCSI_DRIVE)
    {
        return get_SCSI_Log(device, LP_GENERAL_STATISTICS_AND_PERFORMANCE, 0, "Device_Statistics", "bin", false, M_NULLPTR, 0, filePath);
    }
    else
    {
        return NOT_SUPPORTED;
    }
}

//PowerCondition log
eReturnValues get_EPC_log(tDevice *device, const char * const filePath)
{
    eReturnValues ret = FAILURE;
    if (device->drive_info.drive_type == ATA_DRIVE)
    {
        //old code was reading address 0x12, however the ACS3 spec says 0x12 is the NCQ Queue Management log and 0x08 is the Power Conditions log
        ret = get_ATA_Log(device, ATA_LOG_POWER_CONDITIONS, "EPC", "EPC", true, false, false, M_NULLPTR, 0, filePath, LEGACY_DRIVE_SEC_SIZE * 2, 0);//sending in an override to read both pages in one command - TJE
    }
    else if (device->drive_info.drive_type == SCSI_DRIVE)
    {
        ret = get_SCSI_VPD(device, POWER_CONDITION, "EPC", "EPC", false, M_NULLPTR, 0, filePath);
    }
    else
    {
        ret = NOT_SUPPORTED;
    }
    return ret;
}

eReturnValues pull_SCSI_G_List(tDevice *device, const char * const filePath)
{
    eReturnValues ret = UNKNOWN;
    uint32_t addressDescriptorIndex = 0;
    uint32_t defectDataSize = 8;//set to size of defect data without any address descriptors so we know how much we will be pulling
    uint8_t *defectData = C_CAST(uint8_t*, safe_calloc_aligned(defectDataSize, sizeof(uint8_t), device->os_info.minimumAlignment));
    if (!defectData)
    {
        return MEMORY_FAILURE;
    }
    ret = scsi_Read_Defect_Data_12(device, false, true, AD_LONG_BLOCK_FORMAT_ADDRESS_DESCRIPTOR, addressDescriptorIndex, defectDataSize, defectData);
    if (ret == SUCCESS)
    {
        secureFileInfo *gListData = M_NULLPTR;
        bool fileOpened = false;
        uint32_t defectListLength = M_BytesTo4ByteValue(defectData[4], defectData[5], defectData[6], defectData[7]);
        //each address descriptor is 8 bytes in size
        defectDataSize = 4096;//pull 4096 at a time
        uint8_t *temp = C_CAST(uint8_t*, safe_reallocf_aligned(C_CAST(void**, &defectData), 0, defectDataSize * sizeof(uint8_t), device->os_info.minimumAlignment));
        if (!temp)
        {
            return MEMORY_FAILURE;
        }
        defectData = temp;
        memset(defectData, 0, defectDataSize);
        //now loop to get all the data
        for (addressDescriptorIndex = 0; ((addressDescriptorIndex + 511) * 8) < defectListLength; addressDescriptorIndex += 511)
        {
            ret = scsi_Read_Defect_Data_12(device, false, true, AD_LONG_BLOCK_FORMAT_ADDRESS_DESCRIPTOR, addressDescriptorIndex, defectDataSize, defectData);
            if (ret == SUCCESS)
            {
                //open file and save the data
                if (!fileOpened)
                {
                    if (SUCCESS == create_And_Open_Secure_Log_File_Dev_EZ(device, &gListData, NAMING_SERIAL_NUMBER_DATE_TIME, filePath, "GLIST", "bin"))
                    {
                        fileOpened = true;
                    }
                    else
                    {
                        if (VERBOSITY_QUIET < device->deviceVerbosity)
                        {
                            printf("Failed to open file!\n");
                        }
                        ret = FAILURE;
                        safe_free_aligned(&defectData);
                        free_Secure_File_Info(&gListData);
                    }
                }
                if (fileOpened)
                {
                    //write out to a file
                    if (SEC_FILE_SUCCESS != secure_Write_File(gListData, defectData, defectDataSize, sizeof(uint8_t), defectDataSize, M_NULLPTR))
                    {
                        if (VERBOSITY_QUIET < device->deviceVerbosity)
                        {
                            perror("Error writing the defect data to a file!\n");
                        }
                        if (SEC_FILE_SUCCESS != secure_Close_File(gListData))
                        {
                            printf("Error closing file!\n");
                        }
                        fileOpened = false;
                        safe_free_aligned(&defectData);
                        free_Secure_File_Info(&gListData);
                        return ERROR_WRITING_FILE;
                    }
                }
                if (fileOpened)
                {
                    if (SEC_FILE_SUCCESS != secure_Flush_File(gListData))
                    {
                        if (VERBOSITY_QUIET < device->deviceVerbosity)
                        {
                            perror("Error flushing data!\n");
                        }
                        if (SEC_FILE_SUCCESS != secure_Close_File(gListData))
                        {
                            printf("Error closing file!\n");
                        }
                        fileOpened = false;
                        safe_free_aligned(&defectData);
                        free_Secure_File_Info(&gListData);
                        return ERROR_WRITING_FILE;
                    }
                    if (SEC_FILE_SUCCESS != secure_Close_File(gListData))
                    {
                        printf("Error closing file!\n");
                    }
                }
            }
        }
        free_Secure_File_Info(&gListData);
    }
    safe_free_aligned(&defectData);
    return ret;
}

eReturnValues pull_SCSI_Informational_Exceptions_Log(tDevice *device, const char * const filePath)
{
    if (device->drive_info.drive_type == SCSI_DRIVE)
    {
        return get_SCSI_Log(device, LP_INFORMATION_EXCEPTIONS, 0, "Informational_Exceptions", "bin", false, M_NULLPTR, 0, filePath);
    }
    else
    {
        return NOT_SUPPORTED;
    }
}

eReturnValues get_ATA_Log(tDevice *device, uint8_t logAddress, const char *logName, const char *fileExtension, bool GPL, \
    bool SMART, bool toBuffer, uint8_t *myBuf, uint32_t bufSize, const char * const filePath, \
    uint32_t transferSizeBytes, uint16_t featureRegister)
{
    eReturnValues ret = UNKNOWN;
    uint32_t logSize = 0;

#ifdef _DEBUG
    printf("%s: -->\n", __FUNCTION__);
#endif

    if (transferSizeBytes % ATA_LOG_PAGE_LEN_BYTES)
    {
        return BAD_PARAMETER;
    }

    if ((logAddress == ATA_SCT_COMMAND_STATUS || logAddress == ATA_SCT_DATA_TRANSFER) && device->drive_info.passThroughHacks.ataPTHacks.smartCommandTransportWithSMARTLogCommandsOnly)
    {
        GPL = false;
    }

    if (toBuffer)
    {
        ret = SUCCESS;
        logSize = bufSize;
        if (bufSize % ATA_LOG_PAGE_LEN_BYTES)
        {
            return BAD_PARAMETER;
        }
    }
    else
    {
        ret = get_ATA_Log_Size(device, logAddress, &logSize, GPL, SMART);
    }
    if (ret == SUCCESS)
    {
        bool logFromGPL = false;
        bool fileOpened = false;
        secureFileInfo *fp_log = M_NULLPTR;
        uint8_t *logBuffer = C_CAST(uint8_t *, safe_calloc_aligned(logSize, sizeof(uint8_t), device->os_info.minimumAlignment));
        if (!logBuffer)
        {
            perror("Calloc Failure!\n");
            return MEMORY_FAILURE;
        }

        if (GPL)
        {
            //read each log 1 page at a time since some can get to be so large some controllers won't let you pull it.
            uint16_t pagesToReadAtATime = 1;
            uint16_t numberOfLogPages = C_CAST(uint16_t, logSize / LEGACY_DRIVE_SEC_SIZE);
            uint16_t pagesToReadNow = 1;
            uint16_t currentPage = 0;
            if (device->drive_info.interface_type != USB_INTERFACE && device->drive_info.interface_type != IEEE_1394_INTERFACE)
            {
                pagesToReadAtATime = 32;
            }
            else
            {
                //USB and IEEE 1394 should only ever be read 1 page at a time since these interfaces use cheap bridge chips that typically don't allow larger transfers.
                pagesToReadAtATime = 1;
            }
            if (transferSizeBytes)
            {
                //caller is telling us how much to read at a time...so let them.
                pagesToReadAtATime = C_CAST(uint16_t, (transferSizeBytes / LEGACY_DRIVE_SEC_SIZE));
            }
            logFromGPL = true;
            for (currentPage = 0; currentPage < numberOfLogPages; currentPage += pagesToReadAtATime)
            {
                ret = SUCCESS;//assume success
                pagesToReadNow = C_CAST(uint16_t, M_Min(numberOfLogPages - currentPage, pagesToReadAtATime));
                if (currentPage > 0 && (logAddress == ATA_LOG_IDENTIFY_DEVICE_DATA || logAddress == ATA_LOG_DEVICE_STATISTICS))
                {
                    //special case to allow skipping reading unavailable pages. Need to have already read page 0
                    //Both of these logs use the same structure in the first 512B to indicate a list of supported pages.
                    //If reading either of these logs, we can skip a drive request when the page will come back as zeroes anyways
                    //This can be especially helpful for USB devices reading single sectors at a time on logs like device statistics.
                    bool skipAhead = true;
                    bool doneChecking = false;
                    for (uint8_t pageIter = 0; !doneChecking && pageIter < logBuffer[ATA_DEV_STATS_SUP_PG_LIST_LEN_OFFSET]; ++pageIter)
                    {
                        //offset = deviceStatsLog[ATA_DEV_STATS_SUP_PG_LIST_OFFSET + pageIter] * LEGACY_DRIVE_SEC_SIZE;
                        //check if the current page is in the list PLUS additional transfer length if reading more than a single page at a time!
                        for (uint32_t pageCheck = currentPage; !doneChecking && pageCheck < C_CAST(uint32_t, (currentPage + pagesToReadAtATime)); pageCheck += 1)
                        {
                            if (pageCheck == logBuffer[ATA_DEV_STATS_SUP_PG_LIST_OFFSET + pageIter])
                            {
                                skipAhead = false;
                                doneChecking = true;
                            }
                        }
                    }
                    if (skipAhead)
                    {
                        if (fileOpened)
                        {
                            //write out to a file
                            if (SEC_FILE_SUCCESS != secure_Write_File(fp_log, &logBuffer[currentPage * LEGACY_DRIVE_SEC_SIZE], C_CAST(size_t, pagesToReadNow) * LEGACY_DRIVE_SEC_SIZE, sizeof(uint8_t), C_CAST(size_t, pagesToReadNow) * LEGACY_DRIVE_SEC_SIZE, M_NULLPTR))
                            {
                                if (VERBOSITY_QUIET < device->deviceVerbosity)
                                {
                                    perror("Error writing a file!\n");
                                }
                                if (SEC_FILE_SUCCESS != secure_Close_File(fp_log))
                                {
                                    printf("Error closing file!\n");
                                }
                                fileOpened = false;
                                safe_free_aligned(&logBuffer);
                                free_Secure_File_Info(&fp_log);
                                return ERROR_WRITING_FILE;
                            }
                            ret = SUCCESS;
                        }
                        if (toBuffer)
                        {
                            if (bufSize >= logSize)
                            {
                                memset(&myBuf[currentPage * LEGACY_DRIVE_SEC_SIZE], 0, C_CAST(size_t, pagesToReadNow) * LEGACY_DRIVE_SEC_SIZE);
                            }
                            else
                            {
                                return BAD_PARAMETER;
                            }
                        }
                        continue;
                    }
                }
                //loop and read each page or set of pages, then save to a file
                ret = send_ATA_Read_Log_Ext_Cmd(device, logAddress, currentPage, &logBuffer[currentPage * LEGACY_DRIVE_SEC_SIZE], pagesToReadNow * LEGACY_DRIVE_SEC_SIZE, featureRegister);
                if (ret == SUCCESS)
                {
                    if (device->deviceVerbosity > VERBOSITY_QUIET)
                    {
                        if (currentPage % 20 == 0)
                        {
                            if (!toBuffer)
                            {
                                printf(".");
                                fflush(stdout);
                            }
                        }
                    }
                    if (!toBuffer && !fileOpened)
                    {
                        if (SUCCESS == create_And_Open_Secure_Log_File_Dev_EZ(device, &fp_log, NAMING_SERIAL_NUMBER_DATE_TIME, filePath, logName, fileExtension))
                        {
                            fileOpened = true;
                        }
                        else
                        {
                            if (VERBOSITY_QUIET < device->deviceVerbosity)
                            {
                                printf("Failed to open file!\n");
                            }
                            ret = FAILURE;
                            safe_free_aligned(&logBuffer);
                            free_Secure_File_Info(&fp_log);
                            return FILE_OPEN_ERROR;
                        }
                    }
                    if (fileOpened)
                    {
                        //write out to a file
                        if (SEC_FILE_SUCCESS != secure_Write_File(fp_log, &logBuffer[currentPage * LEGACY_DRIVE_SEC_SIZE], C_CAST(size_t, pagesToReadNow) * LEGACY_DRIVE_SEC_SIZE, sizeof(uint8_t), C_CAST(size_t, pagesToReadNow) * LEGACY_DRIVE_SEC_SIZE, M_NULLPTR))
                        {
                            if (VERBOSITY_QUIET < device->deviceVerbosity)
                            {
                                perror("Error writing a file!\n");
                            }
                            if (SEC_FILE_SUCCESS != secure_Close_File(fp_log))
                            {
                                printf("Error closing file!\n");
                            }
                            fileOpened = false;
                            safe_free_aligned(&logBuffer);
                            free_Secure_File_Info(&fp_log);
                            return ERROR_WRITING_FILE;
                        }
                        ret = SUCCESS;
                    }
                    if (toBuffer)
                    {
                        if (0 != safe_memcpy(&myBuf[currentPage * LEGACY_DRIVE_SEC_SIZE], bufSize - currentPage * LEGACY_DRIVE_SEC_SIZE, &logBuffer[currentPage * LEGACY_DRIVE_SEC_SIZE], C_CAST(size_t, pagesToReadNow) * LEGACY_DRIVE_SEC_SIZE))
                        {
                            return BAD_PARAMETER;
                        }
                    }
                }
                else
                {
                    if (ret != NOT_SUPPORTED)
                    {
                        ret = FAILURE;
                    }
                    logSize = 0;
                    logFromGPL = true;
                    break;
                }
            }
            if (device->deviceVerbosity > VERBOSITY_QUIET)
            {
                if (!toBuffer && fp_log)
                {
                    printf("\nBinary log saved to: %s\n", fp_log->fullpath);
                }
            }
        }
        //if the log wasn't found in the GPL directory, then try reading from the SMART directory
        if (!logFromGPL && SMART)
        {
            ret = UNKNOWN; //start fresh again...
            //read the log from SMART
            if (ata_SMART_Read_Log(device, logAddress, logBuffer, logSize) == 0)
            {
                if (!toBuffer && !fileOpened)
                {
                    if (SUCCESS == create_And_Open_Secure_Log_File_Dev_EZ(device, &fp_log, NAMING_SERIAL_NUMBER_DATE_TIME, filePath, logName, fileExtension))
                    {
                        fileOpened = true;
                    }
                    else
                    {
                        if (VERBOSITY_QUIET < device->deviceVerbosity)
                        {
                            printf("Failed to open file!\n");
                        }
                        ret = FAILURE;
                        safe_free_aligned(&logBuffer);
                        free_Secure_File_Info(&fp_log);
                        return FILE_OPEN_ERROR;
                    }
                }
                if (fileOpened)
                {
                    //write out to a file
                    if (SEC_FILE_SUCCESS != secure_Write_File(fp_log, logBuffer, logSize, sizeof(uint8_t), logSize, M_NULLPTR))
                    {
                        if (VERBOSITY_QUIET < device->deviceVerbosity)
                        {
                            perror("Error writing a file!\n");
                        }
                        if (SEC_FILE_SUCCESS != secure_Close_File(fp_log))
                        {
                            printf("Error closing file!\n");
                        }
                        fileOpened = false;
                        safe_free_aligned(&logBuffer);
                        free_Secure_File_Info(&fp_log);
                        return ERROR_WRITING_FILE;
                    }
                    ret = SUCCESS;
                }
                if (toBuffer)
                {
                    if (0 != safe_memcpy(myBuf, bufSize, logBuffer, logSize))
                    {
                        return BAD_PARAMETER;
                    }
                }
                else
                {
                    if (device->deviceVerbosity > VERBOSITY_QUIET)
                    {
                        printf("\n");
                        printf("Binary log saved to: %s\n", fp_log->fullpath);
                    }
                }
            }
            else
            {
                //failed to read the log...
                ret = FAILURE;
            }
        }

        if (fileOpened)
        {
            if (SEC_FILE_SUCCESS != secure_Flush_File(fp_log))
            {
                if (VERBOSITY_QUIET < device->deviceVerbosity)
                {
                    perror("Error flushing data!\n");
                }
                if (SEC_FILE_SUCCESS != secure_Close_File(fp_log))
                {
                    printf("Error closing file!\n");
                }
                fileOpened = false;
                safe_free_aligned(&logBuffer);
                free_Secure_File_Info(&fp_log);
                return ERROR_WRITING_FILE;
            }
            if (SEC_FILE_SUCCESS != secure_Close_File(fp_log))
            {
                printf("Error closing file!\n");
            }
            fileOpened = false;
        }
        safe_free_aligned(&logBuffer);
        free_Secure_File_Info(&fp_log);
    }

#ifdef _DEBUG
    printf("%s: <--\n", __FUNCTION__);
#endif
    return ret;
}

//-----------------------------------------------------------------------------
//
//! get_SCSI_Log
//
//! \brief   generic function to pull an SCSI log and save it to a file
//
//  Entry:
//!   \param[in]  device - file descriptor
//!   \param[in]  logAddress - the address of the log you wish to pull
//!   \param[in]  subpage - set this to something other than zero if looking for a specific subpage to a log
//!   \param[in]  logName - a string that is the name of the log (NO SPACES please!) M_NULLPTR if no file output needed
//!   \param[in]  fileExtension - a string for the file extension. You do not need to include a dot character.
//!   \param[in]  toBuffer - boolean flag specifying if you want to return data in buffer 
//!   \param[in]  myBuf - buffer to return data in if toBuffer is true
//!   \param[in]  bufSize - size of the buffer to get data filled into it (use get_SCSI_Log_Size)
//!   \param[in]  filePath - string with path to output the file to. Can be M_NULLPTR for current directory.
//!
//  Exit:
//!   \return SUCCESS = good, !SUCCESS something went wrong see error codes
//
//-----------------------------------------------------------------------------

eReturnValues get_SCSI_Log(tDevice *device, uint8_t logAddress, uint8_t subpage, const char *logName, \
                 const char *fileExtension, bool toBuffer, uint8_t *myBuf, uint32_t bufSize,\
                 const char * const filePath)
{
    eReturnValues ret = UNKNOWN;
    uint32_t pageLen = 0;
    secureFileInfo *fp_log = M_NULLPTR;
    uint8_t *logBuffer = M_NULLPTR;

    if (toBuffer)
    {
        ret = SUCCESS;
        pageLen = bufSize;
    }
    else
    {
        ret = get_SCSI_Log_Size(device, logAddress, subpage, &pageLen);
    }
    if (ret == SUCCESS)
    {
        //If the user wants it in a buffer...just return. 
        if (toBuffer && ((bufSize < pageLen) || myBuf == M_NULLPTR))
        {
            return BAD_PARAMETER;
        }
        else if (toBuffer)
        {
            logBuffer = myBuf;
        }
        else
        {
            logBuffer = C_CAST(uint8_t *, safe_calloc_aligned(pageLen, sizeof(uint8_t), device->os_info.minimumAlignment));
        }
        if (logBuffer == M_NULLPTR)
        {
            if (VERBOSITY_QUIET < device->deviceVerbosity)
            {
                perror("Calloc failure!\n");
            }
            return MEMORY_FAILURE;
        }

        if (scsi_Log_Sense_Cmd(device, false, LPC_CUMULATIVE_VALUES, logAddress, subpage, 0, logBuffer, C_CAST(uint16_t, pageLen)) == SUCCESS)
        {
            uint16_t returnedPageLength = M_BytesTo2ByteValue(logBuffer[2], logBuffer[3]) + LOG_PAGE_HEADER_LENGTH;
            ret = SUCCESS;
            if (logName && fileExtension) //Because you can also get a log file & get it in buffer. 
            {
                if (SUCCESS == create_And_Open_Secure_Log_File_Dev_EZ(device, &fp_log, NAMING_SERIAL_NUMBER_DATE_TIME, filePath, logName, fileExtension))
                {
                    //write the log to a file
                    if (SEC_FILE_SUCCESS != secure_Write_File(fp_log, logBuffer, pageLen, sizeof(uint8_t), M_Min(pageLen, returnedPageLength), M_NULLPTR))
                    {
                        if (VERBOSITY_QUIET < device->deviceVerbosity)
                        {
                            perror("Error writing to a file!\n");
                        }
                        if (SEC_FILE_SUCCESS != secure_Close_File(fp_log))
                        {
                            printf("Error closing file!\n");
                        }
                        if (!toBuffer)
                        {
                            safe_free_aligned(&logBuffer);
                        }
                        free_Secure_File_Info(&fp_log);
                        return ERROR_WRITING_FILE;
                    }
                    if (SEC_FILE_SUCCESS != secure_Flush_File(fp_log))
                    {
                        if (VERBOSITY_QUIET < device->deviceVerbosity)
                        {
                            perror("Error flushing data!\n");
                        }
                        if (SEC_FILE_SUCCESS != secure_Close_File(fp_log))
                        {
                            printf("Error closing file!\n");
                        }
                        if (!toBuffer)
                        {
                            safe_free_aligned(&logBuffer);
                        }
                        free_Secure_File_Info(&fp_log);
                        return ERROR_WRITING_FILE;
                    }
                    if (SEC_FILE_SUCCESS != secure_Close_File(fp_log))
                    {
                        printf("Error closing file!\n");
                    }
                    if (device->deviceVerbosity > VERBOSITY_QUIET)
                    {
                        printf("\n");
                        printf("Binary log saved to: %s\n", fp_log->fullpath);
                    }
                }
                else
                {
                    if (VERBOSITY_QUIET < device->deviceVerbosity)
                    {
                        printf("Failed to open file!\n");
                    }
                    ret = FAILURE;
                    safe_free_aligned(&logBuffer);
                    free_Secure_File_Info(&fp_log);
                }
            }
        }
        else
        {
            senseDataFields senseFields;
            memset(&senseFields, 0, sizeof(senseDataFields));
            get_Sense_Data_Fields(device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, &senseFields);
            if (senseFields.scsiStatusCodes.senseKey == SENSE_KEY_ILLEGAL_REQUEST && senseFields.scsiStatusCodes.asc == 0x20 && senseFields.scsiStatusCodes.ascq == 0x00)
            {
                ret = FAILURE;
            }
            else if (senseFields.scsiStatusCodes.senseKey == SENSE_KEY_ILLEGAL_REQUEST && senseFields.scsiStatusCodes.asc == 0x24 && senseFields.scsiStatusCodes.ascq == 0x00)
            {
                ret = NOT_SUPPORTED;
            }
            else
            {
                ret = FAILURE;
            }
        }
        if (!toBuffer)
        {
            safe_free_aligned(&logBuffer);
        }
        free_Secure_File_Info(&fp_log);
    }
    return ret;
}

eReturnValues get_SCSI_VPD(tDevice *device, uint8_t pageCode, const char *logName, const char *fileExtension, bool toBuffer, uint8_t *myBuf, uint32_t bufSize, const char * const filePath)
{
    eReturnValues ret = UNKNOWN;
    uint32_t vpdBufferLength = 0;
    if (toBuffer)
    {
        ret = SUCCESS;
        vpdBufferLength = bufSize;
    }
    else
    {
        ret = get_SCSI_VPD_Page_Size(device, pageCode, &vpdBufferLength);
    }
    if (ret == SUCCESS)
    {
        secureFileInfo *fp_vpd = M_NULLPTR;
        uint8_t *vpdBuffer = C_CAST(uint8_t *, safe_calloc_aligned(vpdBufferLength, sizeof(uint8_t), device->os_info.minimumAlignment));
        bool fileOpened = false;
        if (vpdBuffer == M_NULLPTR)
        {
            if (VERBOSITY_QUIET < device->deviceVerbosity)
            {
                perror("Calloc failure!\n");
            }
            return MEMORY_FAILURE;
        }
        //read the requested VPD page
        if (SUCCESS == scsi_Inquiry(device, vpdBuffer, vpdBufferLength, pageCode, true, false))
        {
            if (!toBuffer && !fileOpened && ret != FAILURE)
            {
                if (SUCCESS == create_And_Open_Secure_Log_File_Dev_EZ(device, &fp_vpd, NAMING_SERIAL_NUMBER_DATE_TIME, filePath, logName, fileExtension))
                {
                    fileOpened = true;
                }
                else
                {
                    if (VERBOSITY_QUIET < device->deviceVerbosity)
                    {
                        printf("Failed to open file!\n");
                    }
                    ret = FAILURE;
                    safe_free_aligned(&vpdBuffer);
                    free_Secure_File_Info(&fp_vpd);
                }
            }
            if (fileOpened && ret != FAILURE)
            {
                //write the vpd page to a file
                if (SEC_FILE_SUCCESS != secure_Write_File(fp_vpd, vpdBuffer, vpdBufferLength, sizeof(uint8_t), vpdBufferLength, M_NULLPTR))
                {
                    if (VERBOSITY_QUIET < device->deviceVerbosity)
                    {
                        perror("Error writing vpd page to a file!\n");
                    }
                    if (SEC_FILE_SUCCESS != secure_Close_File(fp_vpd))
                    {
                        printf("Error closing file!\n");
                    }
                    fileOpened = false;
                    safe_free_aligned(&vpdBuffer);
                    free_Secure_File_Info(&fp_vpd);
                    return ERROR_WRITING_FILE;
                }
                if (device->deviceVerbosity > VERBOSITY_QUIET)
                {
                    printf("\n");
                    printf("Binary log saved to: %s\n", fp_vpd->fullpath);
                }
            }
            if (toBuffer && ret != FAILURE)
            {
                if (0 != safe_memcpy(myBuf, bufSize, vpdBuffer, vpdBufferLength))
                {
                    return BAD_PARAMETER;
                }
            }
        }
        if (fileOpened)
        {
            if (SEC_FILE_SUCCESS != secure_Flush_File(fp_vpd))
            {
                if (VERBOSITY_QUIET < device->deviceVerbosity)
                {
                    perror("Error flushing data!\n");
                }
                if (SEC_FILE_SUCCESS != secure_Close_File(fp_vpd))
                {
                    printf("Error closing file!\n");
                }
                fileOpened = false;
                safe_free_aligned(&vpdBuffer);
                free_Secure_File_Info(&fp_vpd);
                return ERROR_WRITING_FILE;
            }
            if (SEC_FILE_SUCCESS != secure_Close_File(fp_vpd))
            {
                printf("Error closing file!\n");
            }
            fileOpened = false;
        }
        safe_free_aligned(&vpdBuffer);
        free_Secure_File_Info(&fp_vpd);
    }
    return ret;
}

static eReturnValues ata_Pull_Telemetry_Log(tDevice *device, bool currentOrSaved, uint8_t islDataSet,\
                             bool saveToFile, uint8_t* ptrData, uint32_t dataSize,\
                            const char * const filePath, uint32_t transferSizeBytes)
{
    eReturnValues ret = SUCCESS;
    secureFileInfo *isl = M_NULLPTR;
    if (transferSizeBytes % LEGACY_DRIVE_SEC_SIZE)
    {
        return BAD_PARAMETER;
    }
    uint8_t *dataBuffer = C_CAST(uint8_t*, safe_calloc_aligned(LEGACY_DRIVE_SEC_SIZE, sizeof(uint8_t), device->os_info.minimumAlignment));
    if (dataBuffer == M_NULLPTR)
    {
        perror("calloc failure");
        return MEMORY_FAILURE;
    }
    //check the GPL directory to make sure that the internal status log is supported by the drive
    if (SUCCESS == send_ATA_Read_Log_Ext_Cmd(device, ATA_LOG_DIRECTORY, 0, dataBuffer, LEGACY_DRIVE_SEC_SIZE, 0))
    {
        uint8_t islLogToPull = 0;
        if (currentOrSaved == true)
        {
            //current
            islLogToPull = ATA_LOG_CURRENT_DEVICE_INTERNAL_STATUS_DATA_LOG;
        }
        else
        {
            //saved
            islLogToPull = ATA_LOG_SAVED_DEVICE_INTERNAL_STATUS_DATA_LOG;
        }
        if (M_BytesTo2ByteValue(dataBuffer[(islLogToPull * 2) + 1], dataBuffer[(islLogToPull * 2)]) > 0)
        {
            if (saveToFile)
            {
                if (SUCCESS == create_And_Open_Secure_Log_File_Dev_EZ(device, &isl, NAMING_SERIAL_NUMBER_DATE_TIME, filePath, "TELEMETRY", "bin"))
                {
                    if (VERBOSITY_QUIET < device->deviceVerbosity)
                    {
                        printf("Saving telemetry log to file: %s\n", isl->fullpath);
                    }
                }
                else
                {
                    ret = FILE_OPEN_ERROR;
                    safe_free_aligned(&dataBuffer);
                    free_Secure_File_Info(&isl);
                    return ret;
                }
            }
            //read the first sector of the log with the trigger bit set
            if (SUCCESS == send_ATA_Read_Log_Ext_Cmd(device, islLogToPull, 0, dataBuffer, LEGACY_DRIVE_SEC_SIZE, 0x0001))
            {
                //now we need to check the sizes reported for the log and what the user is requesting to pull (and save what we just read to a file)
                uint16_t reportedSmallSize = 0;
                uint16_t reportedMediumSize = 0;
                uint16_t reportedLargeSize = 0;
                uint16_t islPullingSize = 0;
                uint16_t pageNumber = 0;//keep track of the current page we are reading/saving
                uint32_t pullChunkSize = UINT32_C(8) * LEGACY_DRIVE_SEC_SIZE;//pull the remainder of the log in 4k chunks
                if (transferSizeBytes)
                {
                    pullChunkSize = transferSizeBytes;
                }
                uint8_t *temp = M_NULLPTR;
                //saving first page to file
                if (saveToFile)
                {
                    if (SEC_FILE_SUCCESS != secure_Write_File(isl, dataBuffer, LEGACY_DRIVE_SEC_SIZE, sizeof(uint8_t), LEGACY_DRIVE_SEC_SIZE, M_NULLPTR))
                    {
                        if (VERBOSITY_QUIET < device->deviceVerbosity)
                        {
                            perror("Error writing first page to a file!\n");
                        }
                        if (SEC_FILE_SUCCESS != secure_Close_File(isl))
                        {
                            printf("Error closing file!\n");
                        }
                        safe_free_aligned(&dataBuffer);
                        free_Secure_File_Info(&isl);
                        return ERROR_WRITING_FILE;
                    }
                    if (SEC_FILE_SUCCESS != secure_Flush_File(isl))
                    {
                        if (VERBOSITY_QUIET < device->deviceVerbosity)
                        {
                            perror("Error flushing data!\n");
                        }
                        if (SEC_FILE_SUCCESS != secure_Close_File(isl))
                        {
                            printf("Error closing file!\n");
                        }
                        safe_free_aligned(&dataBuffer);
                        free_Secure_File_Info(&isl);
                        return ERROR_WRITING_FILE;
                    }
                }
                else
                {
                    if (0 != safe_memcpy(ptrData, dataSize, dataBuffer, LEGACY_DRIVE_SEC_SIZE))
                    {
                        safe_free_aligned(&dataBuffer);
                        return BAD_PARAMETER;
                    }
                }
                //getting isl sizes (little endian)
                reportedSmallSize = M_BytesTo2ByteValue(dataBuffer[9], dataBuffer[8]);
                reportedMediumSize = M_BytesTo2ByteValue(dataBuffer[11], dataBuffer[10]);
                reportedLargeSize = M_BytesTo2ByteValue(dataBuffer[13], dataBuffer[12]);
                //check what the user requested us try and pull and set a size based off of what the drive reports supporting (ex, if they asked for large, but only small is available, return the small information set)
                switch (islDataSet)
                {
                case 3://large
                    islPullingSize = reportedLargeSize;
                    if (islPullingSize > 0)
                    {
                        break;
                    }
                    M_FALLTHROUGH;
                case 2://medium
                    islPullingSize = reportedMediumSize;
                    if (islPullingSize > 0)
                    {
                        break;
                    }
                    M_FALLTHROUGH;
                case 1://small
                    M_FALLTHROUGH;
                default:
                    islPullingSize = reportedSmallSize;
                    break;
                }
                //increment pageNumber to 1 and reallocate the local data buffer
                pageNumber += 1;
                temp = C_CAST(uint8_t*, safe_realloc_aligned(dataBuffer, 512, pullChunkSize, device->os_info.minimumAlignment));
                if (temp == M_NULLPTR)
                {
                    safe_free_aligned(&dataBuffer);
                    perror("realloc failure");
                    return MEMORY_FAILURE;
                }
                dataBuffer = temp;
                memset(dataBuffer, 0, pullChunkSize);
                //read the remaining data
                for (pageNumber = UINT16_C(1); pageNumber < islPullingSize; pageNumber += C_CAST(uint16_t, (pullChunkSize / LEGACY_DRIVE_SEC_SIZE)))
                {
                    if (VERBOSITY_QUIET < device->deviceVerbosity)
                    {
                        if ((pageNumber - 1) % 16 == 0)
                        {
                            printf(".");
                            fflush(stdout);
                        }
                    }
                    //adjust pullcheck size so we don't try and request anything that's not supported by the drive
                    if (pageNumber + (pullChunkSize / LEGACY_DRIVE_SEC_SIZE) > islPullingSize)
                    {
                        pullChunkSize = C_CAST(uint32_t, (islPullingSize - pageNumber)) * LEGACY_DRIVE_SEC_SIZE;
                    }
                    //read each remaining chunk with the trigger bit set to 0
                    if (SUCCESS == send_ATA_Read_Log_Ext_Cmd(device, islLogToPull, pageNumber, dataBuffer, pullChunkSize, 0))
                    {
                        //save to file, or copy to the ptr we were given
                        if (saveToFile)
                        {
                            if (SEC_FILE_SUCCESS != secure_Write_File(isl, dataBuffer, pullChunkSize, sizeof(uint8_t), pullChunkSize, M_NULLPTR))
                            {
                                if (VERBOSITY_QUIET < device->deviceVerbosity)
                                {
                                    perror("Error writing to a file!\n");
                                }
                                if (SEC_FILE_SUCCESS != secure_Close_File(isl))
                                {
                                    printf("Error closing file!\n");
                                }
                                safe_free_aligned(&dataBuffer);
                                free_Secure_File_Info(&isl);
                                return ERROR_WRITING_FILE;
                            }
                            if (SEC_FILE_SUCCESS != secure_Flush_File(isl))
                            {
                                if (VERBOSITY_QUIET < device->deviceVerbosity)
                                {
                                    perror("Error flushing data!\n");
                                }
                                if (SEC_FILE_SUCCESS != secure_Close_File(isl))
                                {
                                    printf("Error closing file!\n");
                                }
                                safe_free_aligned(&dataBuffer);
                                free_Secure_File_Info(&isl);
                                return ERROR_WRITING_FILE;
                            }
                        }
                        else
                        {
                            if (0 != safe_memcpy(&ptrData[pageNumber * LEGACY_DRIVE_SEC_SIZE], dataSize - (pageNumber * LEGACY_DRIVE_SEC_SIZE), dataBuffer, pullChunkSize))
                            {
                                safe_free_aligned(&dataBuffer);
                                return BAD_PARAMETER;
                            }
                        }
                    }
                    else
                    {
                        ret = FAILURE;
                        break;
                    }
                    memset(dataBuffer, 0, pullChunkSize);
                }
                if (VERBOSITY_QUIET < device->deviceVerbosity)
                {
                    printf("\n");
                }
                if (saveToFile)
                {
                    if (SEC_FILE_SUCCESS != secure_Flush_File(isl))
                    {
                        if (VERBOSITY_QUIET < device->deviceVerbosity)
                        {
                            perror("Error flushing data!\n");
                        }
                        if (SEC_FILE_SUCCESS != secure_Close_File(isl))
                        {
                            printf("Error closing file!\n");
                        }
                        free_Secure_File_Info(&isl);
                        return ERROR_WRITING_FILE;
                    }
                    if (SEC_FILE_SUCCESS != secure_Close_File(isl))
                    {
                        printf("Error closing file!\n");
                    }
                }
            }
            else
            {
                ret = FAILURE;
            }
        }
        else
        {
            ret = NOT_SUPPORTED;
        }
    }
    else
    {
        ret = FAILURE;
    }
    safe_free_aligned(&dataBuffer);
    free_Secure_File_Info(&isl);
    return ret;
}

static eReturnValues scsi_Pull_Telemetry_Log(tDevice *device, bool currentOrSaved, uint8_t islDataSet,\
                              bool saveToFile, uint8_t* ptrData, uint32_t dataSize,\
                              const char * const filePath, uint32_t transferSizeBytes)
{
    eReturnValues ret = SUCCESS;
    secureFileInfo *isl = M_NULLPTR;
    uint8_t islLogToPull = 0xFF;
    if (transferSizeBytes % LEGACY_DRIVE_SEC_SIZE)
    {
        //NOTE: We may be able to pull this in any size, but for now and for compatibility only allow 512B sizes.
        return BAD_PARAMETER;
    }
    uint8_t *dataBuffer = C_CAST(uint8_t*, safe_calloc_aligned(LEGACY_DRIVE_SEC_SIZE, sizeof(uint8_t), device->os_info.minimumAlignment));

#ifdef _DEBUG
    printf("--> %s\n", __FUNCTION__);
#endif

    if (!dataBuffer)
    {
        perror("calloc failure");
        return MEMORY_FAILURE;
    }
    if (SUCCESS == scsi_Read_Buffer(device, 0x3C, 0x01, 0, LEGACY_DRIVE_SEC_SIZE, dataBuffer))
    {
        bool islSupported = false;
        uint16_t errorHistoryIter = 32;//the entries start at offset 32 according to SPC4
        uint16_t errorHistoryLength = M_BytesTo2ByteValue(dataBuffer[30], dataBuffer[31]) + 32;
        //we just pulled the error history directory. We need to go through each of the error history parameters until we find the 
        //current/saved parameter for internal status log, otherwise the drive doesn't support internal status log.
        for (errorHistoryIter = 32; errorHistoryIter < M_Min(errorHistoryLength, LEGACY_DRIVE_SEC_SIZE); errorHistoryIter += 8)//each error history parameter is 8 bytes long
        {
            if (currentOrSaved && dataBuffer[errorHistoryIter + 1] == 0x01)
            {
                uint32_t length = M_BytesTo4ByteValue(dataBuffer[errorHistoryIter + 4], dataBuffer[errorHistoryIter + 5], dataBuffer[errorHistoryIter + 6], dataBuffer[errorHistoryIter + 7]);
                if (length != 0)
                {
                    islLogToPull = dataBuffer[errorHistoryIter];
                    islSupported = true;
                    break;
                }
                else
                {
                    if (VERBOSITY_QUIET < device->deviceVerbosity)
                    {
                        printf("Found telemetry log in error history but length is 0! Cannot pull the log!\n");
                    }
                    ret = NOT_SUPPORTED;
                }
            }
            else if (!currentOrSaved && dataBuffer[errorHistoryIter + 1] == 0x02)
            {
                uint32_t length = M_BytesTo4ByteValue(dataBuffer[errorHistoryIter + 4], dataBuffer[errorHistoryIter + 5], dataBuffer[errorHistoryIter + 6], dataBuffer[errorHistoryIter + 7]);
                if (length != 0)
                {
                    islLogToPull = dataBuffer[errorHistoryIter];
                    islSupported = true;
                    break;
                }
                else
                {
                    if (VERBOSITY_QUIET < device->deviceVerbosity)
                    {
                        printf("Found telemetry log in error history but length is 0! Cannot pull the log!\n");
                    }
                    ret = NOT_SUPPORTED;
                }
            }
        }
        if (islSupported)
        {
            uint32_t pageNumber = 0;
            //now we can pull the first page of internal status log and find whether the short/long pages are supported by the device
            if (SUCCESS == scsi_Read_Buffer(device, 0x1C, islLogToPull, pageNumber, LEGACY_DRIVE_SEC_SIZE, dataBuffer))
            {
                uint32_t pullChunkSize = 8 * LEGACY_DRIVE_SEC_SIZE;//pull the remainder of the log in 4k chunks
                if (transferSizeBytes)
                {
                    pullChunkSize = transferSizeBytes;
                }
                uint16_t reportedSmallSize = 0;
                uint16_t reportedMediumSize = 0;
                uint16_t reportedLargeSize = 0;
                uint32_t reportedXLargeSize = 0;
                uint32_t islPullingSize = 0;
                uint8_t *temp = M_NULLPTR;
                if (saveToFile)
                {
                    if (SUCCESS == create_And_Open_Secure_Log_File_Dev_EZ(device, &isl, NAMING_SERIAL_NUMBER_DATE_TIME, filePath, "TELEMETRY", "bin"))
                    {
                        if (VERBOSITY_QUIET < device->deviceVerbosity)
                        {
                            printf("Saving to file: %s\n", isl->fullpath);
                        }
                        if (SEC_FILE_SUCCESS != secure_Write_File(isl, dataBuffer, LEGACY_DRIVE_SEC_SIZE, sizeof(uint8_t), LEGACY_DRIVE_SEC_SIZE, M_NULLPTR))
                        {
                            if (VERBOSITY_QUIET < device->deviceVerbosity)
                            {
                                perror("Error writing to file!\n");
                            }
                            if (SEC_FILE_SUCCESS != secure_Close_File(isl))
                            {
                                printf("Error closing file!\n");
                            }
                            safe_free_aligned(&dataBuffer);
                            free_Secure_File_Info(&isl);
                            return ERROR_WRITING_FILE;
                        }
                        if (SEC_FILE_SUCCESS != secure_Flush_File(isl))
                        {
                            if (VERBOSITY_QUIET < device->deviceVerbosity)
                            {
                                perror("Error flushing data!\n");
                            }
                            if (SEC_FILE_SUCCESS != secure_Close_File(isl))
                            {
                                printf("Error closing file!\n");
                            }
                            safe_free_aligned(&dataBuffer);
                            free_Secure_File_Info(&isl);
                            return ERROR_WRITING_FILE;
                        }
                    }
                    else
                    {
                        ret = FILE_OPEN_ERROR;
                        safe_free_aligned(&dataBuffer);
                        free_Secure_File_Info(&isl);
                        return ret;
                    }
                }
                else
                {
                    if (0 != safe_memcpy(ptrData, dataSize, dataBuffer, LEGACY_DRIVE_SEC_SIZE))
                    {
                        safe_free_aligned(&dataBuffer);
                        return BAD_PARAMETER;
                    }
                }
                if (dataBuffer[0] == RESERVED)//SAS log
                {
                    reportedSmallSize = M_BytesTo2ByteValue(dataBuffer[8], dataBuffer[9]);
                    reportedMediumSize = M_BytesTo2ByteValue(dataBuffer[10], dataBuffer[11]);
                    reportedLargeSize = M_BytesTo2ByteValue(dataBuffer[12], dataBuffer[13]);
                    reportedXLargeSize = M_BytesTo4ByteValue(dataBuffer[14], dataBuffer[15], dataBuffer[16], dataBuffer[17]);
                }
                else //ATA log (SAT translation somewhere below)
                {
                    reportedSmallSize = M_BytesTo2ByteValue(dataBuffer[9], dataBuffer[8]);
                    reportedMediumSize = M_BytesTo2ByteValue(dataBuffer[11], dataBuffer[10]);
                    reportedLargeSize = M_BytesTo2ByteValue(dataBuffer[13], dataBuffer[12]);
                }
                //check what the user requested us try and pull and set a size based off of what the drive reports supporting (ex, if they asked for large, but only small is available, return the small information set)
                switch (islDataSet)
                {
                case 4://X-large
                    islPullingSize = reportedXLargeSize;
                    if (islPullingSize > 0)
                    {
                        break;
                    }
                    M_FALLTHROUGH;
                case 3://large
                    islPullingSize = reportedLargeSize;
                    if (islPullingSize > 0)
                    {
                        break;
                    }
                    M_FALLTHROUGH;
                case 2://medium
                    islPullingSize = reportedMediumSize;
                    if (islPullingSize > 0)
                    {
                        break;
                    }
                    M_FALLTHROUGH;
                case 1://small
                    M_FALLTHROUGH;
                default:
                    islPullingSize = reportedSmallSize;
                    break;
                }
                //increment pageNumber to 1 and reallocate the local data buffer
                pageNumber += 1;
                temp = C_CAST(uint8_t*, safe_realloc_aligned(dataBuffer, 512, pullChunkSize, device->os_info.minimumAlignment));
                if (temp == M_NULLPTR)
                {
                    safe_free_aligned(&dataBuffer);
                    perror("realloc failure");
                    return MEMORY_FAILURE;
                }
                dataBuffer = temp;
                memset(dataBuffer, 0, pullChunkSize);
                //read the remaining data
                for (pageNumber = 1; pageNumber < islPullingSize; pageNumber += (pullChunkSize / LEGACY_DRIVE_SEC_SIZE))
                {
                    if (VERBOSITY_QUIET < device->deviceVerbosity)
                    {
                        if ((pageNumber - 1) % 16 == 0)
                        {
                            printf(".");
                            fflush(stdout);
                        }
                    }
                    //adjust pullcheck size so we don't try and request anything that's not supported by the drive
                    if (pageNumber + (pullChunkSize / LEGACY_DRIVE_SEC_SIZE) > islPullingSize)
                    {
                        pullChunkSize = (islPullingSize - pageNumber) * LEGACY_DRIVE_SEC_SIZE;
                    }
                    //read each remaining chunk of the log
                    if (SUCCESS == scsi_Read_Buffer(device, 0x1C, islLogToPull, pageNumber * LEGACY_DRIVE_SEC_SIZE, pullChunkSize, dataBuffer))
                    {
                        //save to file, or copy to the ptr we were given
                        if (saveToFile)
                        {
                            if (SEC_FILE_SUCCESS != secure_Write_File(isl, dataBuffer, pullChunkSize, sizeof(uint8_t), pullChunkSize, M_NULLPTR))
                            {
                                if (VERBOSITY_QUIET < device->deviceVerbosity)
                                {
                                    perror("Error writing to file!\n");
                                }
                                if (SEC_FILE_SUCCESS != secure_Close_File(isl))
                                {
                                    printf("Error closing file!\n");
                                }
                                safe_free_aligned(&dataBuffer);
                                return ERROR_WRITING_FILE;
                            }
                            if (SEC_FILE_SUCCESS != secure_Flush_File(isl))
                            {
                                if (VERBOSITY_QUIET < device->deviceVerbosity)
                                {
                                    perror("Error flushing data!\n");
                                }
                                if (SEC_FILE_SUCCESS != secure_Close_File(isl))
                                {
                                    printf("Error closing file!\n");
                                }
                                safe_free_aligned(&dataBuffer);
                                return ERROR_WRITING_FILE;
                            }
                        }
                        else
                        {
                            if (0 != safe_memcpy(&ptrData[pageNumber * LEGACY_DRIVE_SEC_SIZE], dataSize - (pageNumber * LEGACY_DRIVE_SEC_SIZE), dataBuffer, pullChunkSize))
                            {
                                safe_free_aligned(&dataBuffer);
                                return BAD_PARAMETER;
                            }
                        }
                    }
                    else
                    {
                        ret = FAILURE;
                        break;
                    }
                    memset(dataBuffer, 0, pullChunkSize);
                }
                if (VERBOSITY_QUIET < device->deviceVerbosity)
                {
                    printf("\n");
                }
                if (saveToFile)
                {
                    secure_Flush_File(isl);
                    if (SEC_FILE_SUCCESS != secure_Close_File(isl))
                    {
                        printf("Error closing file!\n");
                    }
                }
            }
            else
            {
                ret = FAILURE;
            }
        }
        else
        {
            ret = NOT_SUPPORTED;
        }
    }
    else
    {
        //unable to read the error history directory from the device
        ret = FAILURE;
    }
    safe_free_aligned(&dataBuffer);
    free_Secure_File_Info(&isl);
    return ret;
}

static eReturnValues nvme_Pull_Telemetry_Log(tDevice *device, bool currentOrSaved, uint8_t islDataSet, \
    bool saveToFile, uint8_t* ptrData, uint32_t dataSize, \
    const char * const filePath, uint32_t transferSizeBytes)
{
    eReturnValues ret = SUCCESS;
    secureFileInfo *isl = M_NULLPTR;
    if (transferSizeBytes % LEGACY_DRIVE_SEC_SIZE)
    {
        return BAD_PARAMETER;
    }
    uint8_t *dataBuffer = C_CAST(uint8_t*, safe_calloc_aligned(LEGACY_DRIVE_SEC_SIZE, sizeof(uint8_t), device->os_info.minimumAlignment));
    if (dataBuffer == M_NULLPTR)
    {
        perror("calloc failure");
        return MEMORY_FAILURE;
    }
    //check if the nvme telemetry log is supported in the identify data
    if (device->drive_info.IdentifyData.nvme.ctrl.lpa & BIT3)//If this bit is set, then BOTH host and controller initiated are supported
    {
        uint8_t islLogToPull = 0;
        if (currentOrSaved)
        {
            //current/host
            islLogToPull = NVME_LOG_TELEMETRY_HOST_ID;
        }
        else
        {
            //saved/controller
            islLogToPull = NVME_LOG_TELEMETRY_CTRL_ID;
        }
        {
            if (saveToFile)
            {
                if (SUCCESS == create_And_Open_Secure_Log_File_Dev_EZ(device, &isl, NAMING_SERIAL_NUMBER_DATE_TIME, filePath, "TELEMETRY", "bin"))
                {
                    if (VERBOSITY_QUIET < device->deviceVerbosity)
                    {
                        printf("Saving Telemetry log to file: %s\n", isl->fullpath);
                    }
                }
                else
                {
                    ret = FILE_OPEN_ERROR;
                    safe_free_aligned(&dataBuffer);
                    free_Secure_File_Info(&isl);
                    return ret;
                }
            }
            //read the first sector of the log with the trigger bit set
            nvmeGetLogPageCmdOpts telemOpts;
            memset(&telemOpts, 0, sizeof(nvmeGetLogPageCmdOpts));
            telemOpts.dataLen = 512;
            telemOpts.addr = dataBuffer;
            telemOpts.nsid = NVME_ALL_NAMESPACES;
            telemOpts.lid = islLogToPull;
            telemOpts.lsp = 1;//This will be shifted into bit 8
            telemOpts.offset = 0;
            if (SUCCESS == nvme_Get_Log_Page(device, &telemOpts))
            {
                //now we need to check the sizes reported for the log and what the user is requesting to pull (and save what we just read to a file)
                uint16_t reportedSmallSize = 0;
                uint16_t reportedMediumSize = 0;
                uint16_t reportedLargeSize = 0;
                uint16_t islPullingSize = 0;
                uint16_t pageNumber = 0;//keep track of the offset we are reading/saving
                uint32_t pullChunkSize = 8 * LEGACY_DRIVE_SEC_SIZE;//pull the remainder of the log in 4k chunks
                if (transferSizeBytes)
                {
                    pullChunkSize = transferSizeBytes;
                }
                uint8_t *temp = M_NULLPTR;
                //saving first page to file
                if (saveToFile)
                {
                    if (SEC_FILE_SUCCESS != secure_Write_File(isl, dataBuffer, LEGACY_DRIVE_SEC_SIZE, sizeof(uint8_t), LEGACY_DRIVE_SEC_SIZE, M_NULLPTR))
                    {
                        if (VERBOSITY_QUIET < device->deviceVerbosity)
                        {
                            perror("Error writing data to a file!\n");
                        }
                        if (SEC_FILE_SUCCESS != secure_Close_File(isl))
                        {
                            printf("Error closing file!\n");
                        }
                        safe_free_aligned(&dataBuffer);
                        free_Secure_File_Info(&isl);
                        return ERROR_WRITING_FILE;
                    }
                    if (SEC_FILE_SUCCESS != secure_Flush_File(isl))
                    {
                        if (VERBOSITY_QUIET < device->deviceVerbosity)
                        {
                            perror("Error flushing data!\n");
                        }
                        if (SEC_FILE_SUCCESS != secure_Close_File(isl))
                        {
                            printf("Error closing file!\n");
                        }
                        safe_free_aligned(&dataBuffer);
                        free_Secure_File_Info(&isl);
                        return ERROR_WRITING_FILE;
                    }
                }
                else
                {
                    if (0 != safe_memcpy(ptrData, dataSize, dataBuffer, LEGACY_DRIVE_SEC_SIZE))
                    {
                        safe_free_aligned(&dataBuffer);
                        return BAD_PARAMETER;
                    }
                }
                //getting isl sizes (little endian)
                reportedSmallSize = M_BytesTo2ByteValue(dataBuffer[9], dataBuffer[8]);
                reportedMediumSize = M_BytesTo2ByteValue(dataBuffer[11], dataBuffer[10]);
                reportedLargeSize = M_BytesTo2ByteValue(dataBuffer[13], dataBuffer[12]);
                //check what the user requested us try and pull and set a size based off of what the drive reports supporting (ex, if they asked for large, but only small is available, return the small information set)
                switch (islDataSet)
                {
                case 3://large
                    islPullingSize = reportedLargeSize;
                    if (islPullingSize > 0)
                    {
                        break;
                    }
                    M_FALLTHROUGH;
                case 2://medium
                    islPullingSize = reportedMediumSize;
                    if (islPullingSize > 0)
                    {
                        break;
                    }
                    M_FALLTHROUGH;
                case 1://small
                    M_FALLTHROUGH;
                default:
                    islPullingSize = reportedSmallSize;
                    break;
                }
                //increment pageNumber to 1 and reallocate the local data buffer
                pageNumber += 1;
                temp = C_CAST(uint8_t*, safe_realloc_aligned(dataBuffer, 512, pullChunkSize, device->os_info.minimumAlignment));
                if (temp == M_NULLPTR)
                {
                    safe_free_aligned(&dataBuffer);
                    perror("realloc failure");
                    return MEMORY_FAILURE;
                }
                dataBuffer = temp;
                telemOpts.addr = dataBuffer;//update the data buffer after the reallocation - TJE
                memset(dataBuffer, 0, pullChunkSize);
                //read the remaining data
                for (pageNumber = UINT16_C(1); pageNumber < islPullingSize; pageNumber += C_CAST(uint16_t, (pullChunkSize / LEGACY_DRIVE_SEC_SIZE)))
                {
                    if (VERBOSITY_QUIET < device->deviceVerbosity)
                    {
                        if ((pageNumber - 1) % 16 == 0)
                        {
                            printf(".");
                            fflush(stdout);
                        }
                    }
                    //adjust pullcheck size so we don't try and request anything that's not supported by the drive
                    if (pageNumber + (pullChunkSize / LEGACY_DRIVE_SEC_SIZE) > islPullingSize)
                    {
                        pullChunkSize = C_CAST(uint32_t, (islPullingSize - pageNumber)) * LEGACY_DRIVE_SEC_SIZE;
                    }
                    //read each remaining chunk with the trigger bit set to 1 as thats what nvme-cli is doing - Deb
                    telemOpts.lsp = 1;
                    telemOpts.offset = C_CAST(uint64_t, pageNumber) * UINT64_C(512);
                    telemOpts.dataLen = pullChunkSize;
                    if (SUCCESS == nvme_Get_Log_Page(device, &telemOpts))
                    {
                        //save to file, or copy to the ptr we were given
                        if (saveToFile)
                        {
                            if (SEC_FILE_SUCCESS != secure_Write_File(isl, dataBuffer, pullChunkSize, sizeof(uint8_t), pullChunkSize, M_NULLPTR))
                            {
                                if (VERBOSITY_QUIET < device->deviceVerbosity)
                                {
                                    perror("Error writing data to a file!\n");
                                }
                                if (SEC_FILE_SUCCESS != secure_Close_File(isl))
                                {
                                    printf("Error closing file!\n");
                                }
                                safe_free_aligned(&dataBuffer);
                                free_Secure_File_Info(&isl);
                                return ERROR_WRITING_FILE;
                            }
                            if (SEC_FILE_SUCCESS != secure_Flush_File(isl))
                            {
                                if (VERBOSITY_QUIET < device->deviceVerbosity)
                                {
                                    perror("Error flushing data!\n");
                                }
                                if (SEC_FILE_SUCCESS != secure_Close_File(isl))
                                {
                                    printf("Error closing file!\n");
                                }
                                safe_free_aligned(&dataBuffer);
                                free_Secure_File_Info(&isl);
                                return ERROR_WRITING_FILE;
                            }
                        }
                        else
                        {
                            if (0 != safe_memcpy(&ptrData[pageNumber * LEGACY_DRIVE_SEC_SIZE], dataSize - (pageNumber * LEGACY_DRIVE_SEC_SIZE), dataBuffer, pullChunkSize))
                            {
                                safe_free_aligned(&dataBuffer);
                                return BAD_PARAMETER;
                            }
                        }
                    }
                    else
                    {
                        ret = FAILURE;
                        break;
                    }
                    memset(dataBuffer, 0, pullChunkSize);
                }
                if (VERBOSITY_QUIET < device->deviceVerbosity)
                {
                    printf("\n");
                }
                if (saveToFile)
                {
                    if (SEC_FILE_SUCCESS != secure_Flush_File(isl))
                    {
                        if (VERBOSITY_QUIET < device->deviceVerbosity)
                        {
                            perror("Error flushing data!\n");
                        }
                        if (SEC_FILE_SUCCESS != secure_Close_File(isl))
                        {
                            printf("Error closing file!\n");
                        }
                        free_Secure_File_Info(&isl);
                        return ERROR_WRITING_FILE;
                    }
                    if (SEC_FILE_SUCCESS != secure_Close_File(isl))
                    {
                        printf("Error closing file!\n");
                    }
                }
            }
            else
            {
                ret = FAILURE;
            }
        }
    }
    else
    {
        ret = NOT_SUPPORTED;
    }
    free_Secure_File_Info(&isl);
    safe_free_aligned(&dataBuffer);
    return ret;
}

eReturnValues pull_Telemetry_Log(tDevice *device, bool currentOrSaved, uint8_t islDataSet, bool saveToFile, uint8_t* ptrData, uint32_t dataSize, const char * const filePath, uint32_t transferSizeBytes)
{
    eReturnValues ret = NOT_SUPPORTED;
    switch (device->drive_info.drive_type)
    {
    case ATA_DRIVE:
        ret = ata_Pull_Telemetry_Log(device, currentOrSaved, islDataSet, saveToFile, ptrData, dataSize, filePath, transferSizeBytes);
        break;
    case NVME_DRIVE:
        ret = nvme_Pull_Telemetry_Log(device, currentOrSaved, islDataSet, saveToFile, ptrData, dataSize, filePath, transferSizeBytes);
        break;
    case SCSI_DRIVE:
        ret = scsi_Pull_Telemetry_Log(device, currentOrSaved, islDataSet, saveToFile, ptrData, dataSize, filePath, transferSizeBytes);
        break;
    default:
        break;
    }
    return ret;
}

eReturnValues print_Supported_Logs(tDevice *device, uint64_t flags)
{
    eReturnValues retStatus = NOT_SUPPORTED;

    switch (device->drive_info.drive_type)
    {
    case ATA_DRIVE:
        retStatus = print_Supported_ATA_Logs(device, flags);
        break;
    case SCSI_DRIVE:
        retStatus = print_Supported_SCSI_Logs(device, flags);
        break;
    case NVME_DRIVE:
        retStatus = print_Supported_NVMe_Logs(device, flags);
        break;
    default:
        break;
    }

    return retStatus;
}


eReturnValues print_Supported_SCSI_Logs(tDevice *device, uint64_t flags)
{
    eReturnValues retStatus = NOT_SUPPORTED;
    uint8_t *logBuffer = C_CAST(uint8_t*, safe_calloc_aligned(LEGACY_DRIVE_SEC_SIZE, sizeof(uint8_t), device->os_info.minimumAlignment));
    if (logBuffer == M_NULLPTR)
    {
        return MEMORY_FAILURE;
    }
    bool subpagesSupported = true;
    bool gotListOfPages = true;
    M_USE_UNUSED(flags);
    if (SUCCESS != scsi_Log_Sense_Cmd(device, false, LPC_CUMULATIVE_VALUES, LP_SUPPORTED_LOG_PAGES_AND_SUBPAGES, 0xFF, 0, logBuffer, LEGACY_DRIVE_SEC_SIZE))
    {
        //either device doesn't support logs, or it just doesn't support subpages, so let's try reading the list of supported pages (no subpages) before saying we need to dummy up the list
        if (SUCCESS != scsi_Log_Sense_Cmd(device, false, LPC_CUMULATIVE_VALUES, LP_SUPPORTED_LOG_PAGES, 0, 0, logBuffer, LEGACY_DRIVE_SEC_SIZE))
        {
            gotListOfPages = false;
        }
        else
        {
            subpagesSupported = false;
        }
    }
    if (gotListOfPages)
    {
        retStatus = SUCCESS;
        uint16_t logPageIter = LOG_PAGE_HEADER_LENGTH;//log page descriptors start on offset 4 and are 2 bytes long each
        uint16_t supportedPagesLength = M_BytesTo2ByteValue(logBuffer[2], logBuffer[3]);
        uint8_t incrementAmount = subpagesSupported ? 2 : 1;
        uint16_t pageLength = 0;//for each page in the supported buffer so we can report the size
        DECLARE_ZERO_INIT_ARRAY(uint8_t, logPage, 4);
        bool vsHeaderPrinted = false;
        bool reservedHeaderPrinted = false;
        printf("\n  Page Code  :  Subpage Code  :  Size (Bytes)\n");
        for (; logPageIter < M_Min(supportedPagesLength + LOG_PAGE_HEADER_LENGTH, LEGACY_DRIVE_SEC_SIZE); logPageIter += incrementAmount)
        {
            uint8_t pageCode = logBuffer[logPageIter] & 0x3F;
            uint8_t subpageCode = 0;
            pageLength = 0;
            if (subpagesSupported)
            {
                subpageCode = logBuffer[logPageIter + 1];
            }
            //page codes 30h to 3Eh are vendor specific
            if (pageCode >= 0x30 && pageCode <= 0x3E && !vsHeaderPrinted)
            {
                //vendor specific log page
                printf("\t\t------------------\n");
                printf("\tDEVICE VENDOR SPECIFIC LOGS\n");
                printf("\t\t------------------\n");
                vsHeaderPrinted = true;
            }
            else if (pageCode > 0x3E && !reservedHeaderPrinted)
            {
                //this page and subpages are marked as reserved!
                printf("\t\t------------------\n");
                printf("\tRESERVED LOGS\n");
                printf("\t\t------------------\n");
                reservedHeaderPrinted = true;
            }
            if (SUCCESS == scsi_Log_Sense_Cmd(device, false, LPC_CUMULATIVE_VALUES, pageCode, subpageCode, 0, logPage, 4))
            {
                pageLength = M_BytesTo2ByteValue(logPage[2], logPage[3]);
            }
            if (pageLength > 0)
            {
                printf("   %2" PRIu8 " (%02" PRIX8 "h)  :   %3" PRIu8 " (%02" PRIX8 "h)    :   %" PRIu32 "\n", pageCode, pageCode, subpageCode, subpageCode, pageLength + LOG_PAGE_HEADER_LENGTH);
            }
            else
            {
                printf("   %2" PRIu8 " (%02" PRIX8 "h)  :   %3" PRIu8 " (%02" PRIX8 "h)    :   Unknown\n", pageCode, pageCode, subpageCode, subpageCode);
            }
        }
    }
    else
    {
        printf("SCSI Logs not supported on this device.\n");
    }
    safe_free_aligned(&logBuffer);
    return retStatus;
}

//NOTE: smartAccess & gplAccess are used to show how a log can be read based on drive reporting.
//      driveReportBug exists for noting that a drive is incorrectly reporting access for certain logs.
static void format_print_ata_logs_info(uint16_t log, uint32_t logSize, bool smartAccess, bool gplAccess, bool driveReportBug)
{
#define ATA_LOG_ACCESS_STRING_LENGTH 10
    DECLARE_ZERO_INIT_ARRAY(char, access, ATA_LOG_ACCESS_STRING_LENGTH);
    if (smartAccess)
    {
        snprintf(access, ATA_LOG_ACCESS_STRING_LENGTH, "SL");
    }
    if (gplAccess)
    {
        if (smartAccess)
        {
            common_String_Concat(access, ATA_LOG_ACCESS_STRING_LENGTH, ", ");
        }
        common_String_Concat(access, ATA_LOG_ACCESS_STRING_LENGTH, "GPL");
    }
    if (driveReportBug)
    {
        common_String_Concat(access, ATA_LOG_ACCESS_STRING_LENGTH, " !");
    }
    printf("   %3" PRIu16 " (%02" PRIX16 "h)   :     %-5" PRIu32 "      :    %-10" PRIu32 " :   %-10s\n", log, log, (logSize / LEGACY_DRIVE_SEC_SIZE), logSize, access);
}

//To be portable between old & new, SMART and GPL, we need to read both GPL and SMART directory. Combine the results, then show them on screen.
eReturnValues print_Supported_ATA_Logs(tDevice *device, uint64_t flags)
{
    eReturnValues retStatus = NOT_SUPPORTED;
    bool legacyDriveNoLogDir = false;
    uint8_t *gplLogBuffer = C_CAST(uint8_t*, safe_calloc_aligned(LEGACY_DRIVE_SEC_SIZE, sizeof(uint8_t), device->os_info.minimumAlignment));
    uint8_t *smartLogBuffer = C_CAST(uint8_t*, safe_calloc_aligned(LEGACY_DRIVE_SEC_SIZE, sizeof(uint8_t), device->os_info.minimumAlignment));
    M_USE_UNUSED(flags);
    if (smartLogBuffer)
    {
        if (is_SMART_Enabled(device) && is_SMART_Error_Logging_Supported(device))
        {
            retStatus = ata_SMART_Read_Log(device, ATA_LOG_DIRECTORY, smartLogBuffer, 512);
            if (retStatus != SUCCESS && retStatus != WARN_INVALID_CHECKSUM)
            {
                legacyDriveNoLogDir = true; //for old drives that do not support multi-sector logs we will rely on Identify/SMART data bits to generate the output
                safe_free_aligned(&smartLogBuffer);
            }
        }
        else
        {
            safe_free_aligned(&smartLogBuffer);
        }
    }
    if (gplLogBuffer)
    {
        if (device->drive_info.ata_Options.generalPurposeLoggingSupported)
        {
            if (SUCCESS != send_ATA_Read_Log_Ext_Cmd(device, ATA_LOG_DIRECTORY, 0, gplLogBuffer, LEGACY_DRIVE_SEC_SIZE, 0))
            {
                safe_free_aligned(&gplLogBuffer);
            }
        }
        else
        {
            safe_free_aligned(&gplLogBuffer);
        }
    }
    if (gplLogBuffer || smartLogBuffer || legacyDriveNoLogDir)
    {
        uint16_t log = 0;
        uint16_t gplLogSize = 0;
        uint16_t smartLogSize = 0;
        bool bug = false;
        bool atLeastOneBug = false;
        printf("\nAccess Types:\n");
        printf("------------\n");
        printf("  SL - SMART Log\n");
        printf(" GPL - General Purpose Log\n\n");

        printf("\n  Log Address  :   # of Pages   :  Size (Bytes) :   Access\n");
        printf("---------------:----------------:---------------:-------------\n");
        for (log = 0; log < 0x80; log++)
        {
            bug = false;
            gplLogSize = 0;
            smartLogSize = 0;
            if (gplLogBuffer)
            {
                gplLogSize = M_BytesTo2ByteValue(gplLogBuffer[(log * 2) + 1], gplLogBuffer[(log * 2)]) * LEGACY_DRIVE_SEC_SIZE;
            }
            if (smartLogBuffer)
            {
                smartLogSize = M_BytesTo2ByteValue(smartLogBuffer[(log * 2) + 1], smartLogBuffer[(log * 2)]) * LEGACY_DRIVE_SEC_SIZE;
            }
            else if (legacyDriveNoLogDir)
            {
                //special case for old drives that support only single sector logs. In this case they do not support the smart directory
                //So for each log being looked at, dummy up the size
                switch (log)
                {
                case ATA_LOG_SUMMARY_SMART_ERROR_LOG:
                    smartLogSize = UINT16_C(512);
                    break;
                case ATA_LOG_COMPREHENSIVE_SMART_ERROR_LOG:
                    if (is_ATA_Identify_Word_Valid(device->drive_info.IdentifyData.ata.Word080) && (device->drive_info.IdentifyData.ata.Word080 & 0xFFC0))
                    {
                        smartLogSize = UINT16_C(512);
                    }
                    break;
                case ATA_LOG_SMART_SELF_TEST_LOG:
                    if (is_Self_Test_Supported(device))
                    {
                        smartLogSize = UINT16_C(512);
                    }
                    break;
                case ATA_LOG_SELECTIVE_SELF_TEST_LOG:
                    if (is_Self_Test_Supported(device) && is_Selective_Self_Test_Supported(device))
                    {
                        smartLogSize = UINT16_C(512);
                    }
                    break;
                default:
                    smartLogSize = 0;
                    break;
                }
            }
            //Here we need to check if the drive is reporting certain logs that are only accessible with GPL or only with SMART to set the "bug" field
            switch (log)
            {
                //smart only
            case ATA_LOG_SUMMARY_SMART_ERROR_LOG:
            case ATA_LOG_COMPREHENSIVE_SMART_ERROR_LOG:
            case ATA_LOG_SMART_SELF_TEST_LOG:
            case ATA_LOG_SELECTIVE_SELF_TEST_LOG:
                if (gplLogSize)
                {
                    bug = true;
                }
                break;
                //GPL only
            case ATA_LOG_EXTENDED_COMPREHENSIVE_SMART_ERROR_LOG:
            case ATA_LOG_EXTENDED_SMART_SELF_TEST_LOG:
            case ATA_LOG_POWER_CONDITIONS:
            case ATA_LOG_DEVICE_STATISTICS_NOTIFICATION:
            case ATA_LOG_PENDING_DEFECTS_LOG:
            case ATA_LOG_SENSE_DATA_FOR_SUCCESSFUL_NCQ_COMMANDS:
            case ATA_LOG_NCQ_COMMAND_ERROR_LOG:
            case ATA_LOG_SATA_PHY_EVENT_COUNTERS_LOG:
            case ATA_LOG_SATA_NCQ_QUEUE_MANAGEMENT_LOG:
            case ATA_LOG_SATA_NCQ_SEND_AND_RECEIVE_LOG:
            case ATA_LOG_HYBRID_INFORMATION:
            case ATA_LOG_REBUILD_ASSIST:
            case ATA_LOG_OUT_OF_BAND_MANAGEMENT_CONTROL_LOG:
            case ATA_LOG_COMMAND_DURATION_LIMITS_LOG:
            case ATA_LOG_LBA_STATUS:
            case ATA_LOG_STREAMING_PERFORMANCE:
            case ATA_LOG_WRITE_STREAM_ERROR_LOG:
            case ATA_LOG_READ_STREAM_ERROR_LOG:
            case ATA_LOG_DELAYED_LBA_LOG:
            case ATA_LOG_CURRENT_DEVICE_INTERNAL_STATUS_DATA_LOG:
            case ATA_LOG_SAVED_DEVICE_INTERNAL_STATUS_DATA_LOG:
            case ATA_LOG_SECTOR_CONFIGURATION_LOG:
            case ATA_LOG_CAPACITY_MODELNUMBER_MAPPING:
                if (smartLogSize)
                {
                    bug = true;
                }
                break;
            default://unknown log, or not a possible problem log. At this point in time, any thing not already in this list, likely no longer matters as it's almost all GPL only access or combination.
                bug = false;
                break;
            }
            if (bug)
            {
                atLeastOneBug = true;
            }
            if (smartLogSize > 0 || gplLogSize > 0)
            {
                format_print_ata_logs_info(log, M_Max(gplLogSize, smartLogSize), M_ToBool(smartLogSize), M_ToBool(gplLogSize), bug);
            }
        }
        bug = false;
        printf("\t\t------------------\n");
        printf("\t\tHOST SPECIFIC LOGS\n");
        printf("\t\t------------------\n");
        for (log = 0x80; log < 0xA0; log++)
        {
            bug = false;
            gplLogSize = 0;
            smartLogSize = 0;
            if (gplLogBuffer)
            {
                gplLogSize = M_BytesTo2ByteValue(gplLogBuffer[(log * 2) + 1], gplLogBuffer[(log * 2)]) * LEGACY_DRIVE_SEC_SIZE;
            }
            if (smartLogBuffer)
            {
                smartLogSize = M_BytesTo2ByteValue(smartLogBuffer[(log * 2) + 1], smartLogBuffer[(log * 2)]) * LEGACY_DRIVE_SEC_SIZE;
            }
            if (smartLogSize > 0 || gplLogSize > 0)
            {
                format_print_ata_logs_info(log, M_Max(gplLogSize, smartLogSize), M_ToBool(smartLogSize), M_ToBool(gplLogSize), bug);
            }
        }
        printf("\t\t------------------\n");
        printf("\tDEVICE VENDOR SPECIFIC LOGS\n");
        printf("\t\t------------------\n");
        for (log = 0xA0; log < 0xE0; log++)
        {
            bug = false;
            gplLogSize = 0;
            smartLogSize = 0;
            if (gplLogBuffer)
            {
                gplLogSize = M_BytesTo2ByteValue(gplLogBuffer[(log * 2) + 1], gplLogBuffer[(log * 2)]) * LEGACY_DRIVE_SEC_SIZE;
            }
            if (smartLogBuffer)
            {
                smartLogSize = M_BytesTo2ByteValue(smartLogBuffer[(log * 2) + 1], smartLogBuffer[(log * 2)]) * LEGACY_DRIVE_SEC_SIZE;
            }
            if (smartLogSize > 0 || gplLogSize > 0)
            {
                format_print_ata_logs_info(log, M_Max(gplLogSize, smartLogSize), M_ToBool(smartLogSize), M_ToBool(gplLogSize), bug);
            }
        }
        printf("\t\t------------------\n");
        for (log = 0xE0; log <= 0xFF; log++)
        {
            bug = false;
            gplLogSize = 0;
            smartLogSize = 0;
            if (gplLogBuffer)
            {
                gplLogSize = M_BytesTo2ByteValue(gplLogBuffer[(log * 2) + 1], gplLogBuffer[(log * 2)]) * LEGACY_DRIVE_SEC_SIZE;
            }
            if (smartLogBuffer)
            {
                smartLogSize = M_BytesTo2ByteValue(smartLogBuffer[(log * 2) + 1], smartLogBuffer[(log * 2)]) * LEGACY_DRIVE_SEC_SIZE;
            }
            if (smartLogSize > 0 || gplLogSize > 0)
            {
                format_print_ata_logs_info(log, M_Max(gplLogSize, smartLogSize), M_ToBool(smartLogSize), M_ToBool(gplLogSize), bug);
            }
        }
        safe_free_aligned(&smartLogBuffer);
        safe_free_aligned(&gplLogBuffer);
        retStatus = SUCCESS;//set success if we were able to get at least one of the log directories to use
        if (legacyDriveNoLogDir)
        {
            printf("\nNOTE: SMART log detection came from identify & smart data bits. This device\n");
            printf("      does not support the SMART log directory, and therefore no multi-sector\n");
            printf("      logs either. It may not be possible to get all possible logs on this device.\n\n");
        }
        if (atLeastOneBug)
        {
            printf("\nWARNING - At least one log was reported in a non-standard way (GPL log in SMART\n");
            printf("          or SMART in GPL). Because of this, the access type may be incorrect on\n");
            printf("          other non-standard logs. This may also lead to strange behavior when\n");
            printf("          trying to get access to some of the logs since they are not being\n");
            printf("          reported appropriately. The incorrectly reported logs will have a \"!\"\n");
            printf("          in the access column.\n\n");
        }
    }
    else
    {
        retStatus = NOT_SUPPORTED;
    }

    return retStatus;
}

eReturnValues print_Supported_NVMe_Logs(tDevice *device, uint64_t flags)
{
    eReturnValues retStatus = NOT_SUPPORTED;
    bool readSupporteLogPagesLog = false;
    bool dummyFromIdentify = false;
    M_USE_UNUSED(flags);

    if (device->drive_info.IdentifyData.nvme.ctrl.lpa & BIT5)
    {
        readSupporteLogPagesLog = true;
    }
    else if (is_Seagate_Family(device) == SEAGATE_VENDOR_SSD_PJ)
    {
        logPageMap suptLogPage;
        nvmeGetLogPageCmdOpts suptLogOpts;

        memset(&suptLogPage, 0, sizeof(logPageMap));
        memset(&suptLogOpts, 0, sizeof(nvmeGetLogPageCmdOpts));
        suptLogOpts.addr = C_CAST(uint8_t*, &suptLogPage);
        suptLogOpts.dataLen = sizeof(logPageMap);
        suptLogOpts.lid = 0xC5;
        suptLogOpts.nsid = 0;//controller data
        if (SUCCESS == nvme_Get_Log_Page(device, &suptLogOpts))
        {
            uint32_t numPage = suptLogPage.numLogPages;
            //Check if a bogus number is returned as the C5 log may be used differently on some products
            //There needs to be a better filter as this C5 page was for 2 known products and newer ones have very different designs
            if (numPage > MAX_SUPPORTED_LOG_PAGE_ENTRIES || numPage == 0)
            {
                dummyFromIdentify = true;
            }
            else
            {
                uint32_t page = 0;
                retStatus = SUCCESS;
                printf("\n  Log Pages  :   Signature    :    Version\n");
                printf("-------------:----------------:--------------\n");
                for (page = 0; page < numPage && page < MAX_SUPPORTED_LOG_PAGE_ENTRIES; page++)
                {
                    if (suptLogPage.logPageEntry[page].logPageID < 0xC0)
                    {
                        printf("  %3" PRIu32 " (%02" PRIX32 "h)  :   %-10" PRIX32 "   :    %-10" PRIu32 "\n",
                            suptLogPage.logPageEntry[page].logPageID, suptLogPage.logPageEntry[page].logPageID,
                            suptLogPage.logPageEntry[page].logPageSignature, suptLogPage.logPageEntry[page].logPageVersion);
                    }
                }
                printf("\t\t------------------\n");
                printf("\tDEVICE VENDOR SPECIFIC LOGS\n");
                printf("\t\t------------------\n");
                for (page = 0; page < numPage && page < MAX_SUPPORTED_LOG_PAGE_ENTRIES; page++)
                {
                    if (suptLogPage.logPageEntry[page].logPageID >= 0xC0)
                    {
                        printf("  %3" PRIu32 " (%02" PRIX32 "h)  :   %-10" PRIX32 "   :    %-10" PRIu32 "\n",
                            suptLogPage.logPageEntry[page].logPageID, suptLogPage.logPageEntry[page].logPageID,
                            suptLogPage.logPageEntry[page].logPageSignature, suptLogPage.logPageEntry[page].logPageVersion);
                    }
                }
            }
        }
        else
        {
            dummyFromIdentify = true;
        }
    }
    else
    {
        // in this case the supported log pages MAY be supported, but may not be.
        // So if it is not supported, dummy up a response based on other reported identify data bits and which logs are madatory in the NVMe specs.
        readSupporteLogPagesLog = true;
        dummyFromIdentify = true;
    }

    if (readSupporteLogPagesLog)
    {
        uint8_t* supportedLogsPage = C_CAST(uint8_t*, safe_calloc_aligned(1024, sizeof(uint8_t), device->os_info.minimumAlignment));
        if (supportedLogsPage)
        {
            nvmeGetLogPageCmdOpts suptLogOpts;
            memset(&suptLogOpts, 0, sizeof(nvmeGetLogPageCmdOpts));
            suptLogOpts.addr = supportedLogsPage;
            suptLogOpts.dataLen = 1024;
            suptLogOpts.lid = 0;
            suptLogOpts.nsid = 0;//controller data
            if (SUCCESS == nvme_Get_Log_Page(device, &suptLogOpts))
            {
                bool printedFabrics = false;
                bool printedIOCmdSet = false;
                bool printedVendorUnique = false;
                retStatus = SUCCESS;
                printf("\n  Log Pages (from supported pages log page)\n");
                for (uint16_t offset = 0; offset < UINT16_C(1024); offset += UINT16_C(4))
                {
                    //Using this macro so we don't have endianness issues with straight assignments or pointers - TJE
                    uint32_t currentLog = M_BytesTo4ByteValue(supportedLogsPage[offset + 3], supportedLogsPage[offset + 2], supportedLogsPage[offset + 1], supportedLogsPage[offset + 0]);
                    if (currentLog & BIT0)
                    {
                        uint16_t logNumber = offset / 4;
                        if (!printedFabrics && logNumber >= 0x70 && logNumber <= 0x7F)
                        {
                            printf("\t\t------------------\n");
                            printf("\tNVMe Over Fabrics Logs\n");
                            printf("\t\t------------------\n");
                            printedFabrics = true;
                        }
                        if (!printedIOCmdSet && logNumber >= 0x80 && logNumber <= 0xBF)
                        {
                            printf("\t\t------------------\n");
                            printf("\tIO Command Set Specific Logs\n");
                            printf("\t\t------------------\n");
                            printedIOCmdSet = true;
                        }
                        if (!printedVendorUnique && logNumber >= 0xC0)
                        {
                            printf("\t\t------------------\n");
                            printf("\tDevice Vendor Specific Logs\n");
                            printf("\t\t------------------\n");
                            printedVendorUnique = true;
                        }
                        printf("  %3" PRIu16 " (%02" PRIX16 "h)\n", logNumber, logNumber);
                    }
                }
            }
            else if (!dummyFromIdentify)
            {
                //something went wrong, so fall back to dummying it up from identify bits
                dummyFromIdentify = true;
            }
            safe_free_aligned(&supportedLogsPage);
        }
        else
        {
            retStatus = MEMORY_FAILURE;
        }
    }

    if (dummyFromIdentify)
    {
        printf("\n  Log Pages  (Generated based on identify data) \n");
        // 01 = error information always supported
        printf("   1 (01h)\n");
        // 02 = SMART/health information always supported
        printf("   2 (02h)\n");
        // 03 = firwmare slot info??? always supported???
        printf("   3 (03h)\n");
        // 04 = changed namespace list ??? oaes bit8
        if (device->drive_info.IdentifyData.nvme.ctrl.oaes & BIT8)
        {
            printf("   4 (04h)\n");
        }
        // 05 = lpa bit 1 = commands supported and affects log
        if (device->drive_info.IdentifyData.nvme.ctrl.lpa & BIT1)
        {
            printf("   5 (05h)\n");
        }
        // 06 = device self test (look for bit in identify data for support of DST feature)
        if (device->drive_info.IdentifyData.nvme.ctrl.oacs & BIT4)
        {
            printf("   6 (06h)\n");
        }
        // 07 & 08 = lpa bit 3telemetry host initiated and telepemtry controller initiated log pages
        if (device->drive_info.IdentifyData.nvme.ctrl.lpa & BIT3)
        {
            printf("   7 (07h)\n");
            printf("   8 (08h)\n");
        }
        // 09 = endurance group info - support in controller attributes ctratt bit4
        if (device->drive_info.IdentifyData.nvme.ctrl.ctratt & BIT4)
        {
            printf("   9 (09h)\n");
        }
        // 0A = predictable latency per NVM set - support in controller attributes ctratt bit5
        if (device->drive_info.IdentifyData.nvme.ctrl.ctratt & BIT5)
        {
            printf("  10 (0Ah)\n");
        }
        // 0B = predictable latency event aggregate - - oaes bit12
        if (device->drive_info.IdentifyData.nvme.ctrl.oaes & BIT12)
        {
            printf("  11 (0Bh)\n");
        }
        // 0C = asymestric namespace access - anacap bit0 in controller identify??? or bit3 CMIC??? or oaes bit11???
        if (device->drive_info.IdentifyData.nvme.ctrl.oaes & BIT11)
        {
            printf("  12 (0CAh)\n");
        }
        // 0D = lpa bit 4 = persistent event log
        if (device->drive_info.IdentifyData.nvme.ctrl.lpa & BIT4)
        {
            printf("  13 (0Dh)\n");
        }
        // 0E = LBA status information - get LBA status capability in OACS
        if (device->drive_info.IdentifyData.nvme.ctrl.oacs & BIT9)
        {
            printf("  14 (0Eh)\n");
        }
        // 0F = endurance group aggregate????     oeas bit14  
        if (device->drive_info.IdentifyData.nvme.ctrl.oaes & BIT14)
        {
            printf("  15 (0Fh)\n");
        }
        // 70 = discovery - NVMe over fabrics????
        if (device->drive_info.IdentifyData.nvme.ctrl.oaes & BIT31)
        {
            printf("\t\t------------------\n");
            printf("\tNVMe Over Fabrics Logs\n");
            printf("\t\t------------------\n");
            printf(" 112 (70h)\n");
        }
        if (device->drive_info.IdentifyData.nvme.ctrl.oncs & BIT5 || device->drive_info.IdentifyData.nvme.ctrl.sanicap > 0)
        {
            printf("\t\t------------------\n");
            printf("\tIO Command Set Specific Logs\n");
            printf("\t\t------------------\n");
        }
        // 80 = reservation notification - check for reservations support
        if (device->drive_info.IdentifyData.nvme.ctrl.oncs & BIT5)
        {
            printf(" 128 (80h)\n");
        }
        // 81 = Sanitize status - check for sanitize support
        if (device->drive_info.IdentifyData.nvme.ctrl.sanicap > 0)
        {
            printf(" 129 (81h)\n");
        }
        retStatus = SUCCESS;
    }

    return retStatus;
}

//This function needs a proper rewrite to allow pulling with offsets, other log sizes, pulling to a buffer, and more like the SCSI and ATA functions.
eReturnValues pull_Supported_NVMe_Logs(tDevice *device, uint8_t logNum, eLogPullMode mode, uint32_t nvmeLogSizeBytes)
{
    eReturnValues retStatus = SUCCESS;
    uint64_t size = nvmeLogSizeBytes;//set this for now
    uint8_t * logBuffer = M_NULLPTR;
    nvmeGetLogPageCmdOpts cmdOpts;
    if (nvmeLogSizeBytes > 0 || ((nvme_Get_Log_Size(device, logNum, &size) == SUCCESS) && size))
    {
        memset(&cmdOpts, 0, sizeof(nvmeGetLogPageCmdOpts));
        logBuffer = C_CAST(uint8_t *, safe_calloc(C_CAST(size_t, size), sizeof(uint8_t)));
        if (logBuffer != M_NULLPTR)
        {
            cmdOpts.nsid = NVME_ALL_NAMESPACES;
            cmdOpts.addr = logBuffer;
            cmdOpts.dataLen = C_CAST(uint32_t, size);
            cmdOpts.lid = logNum;
            if (nvme_Get_Log_Page(device, &cmdOpts) == SUCCESS)
            {
                if (mode == PULL_LOG_RAW_MODE)
                {
                    printf("Log Page %d Buffer:\n", logNum);
                    printf("================================\n");
                    print_Data_Buffer(C_CAST(uint8_t *, logBuffer), C_CAST(uint32_t, size), true);
                    printf("================================\n");
                }
                else if (mode == PULL_LOG_BIN_FILE_MODE)
                {
                    secureFileInfo *pLogFile = M_NULLPTR;
                    #define NVME_LOG_NAME_SIZE 16
                    DECLARE_ZERO_INIT_ARRAY(char, logName, NVME_LOG_NAME_SIZE);
                    snprintf(logName, NVME_LOG_NAME_SIZE, "LOG_PAGE_%d", logNum);
                    if (SUCCESS == create_And_Open_Secure_Log_File_Dev_EZ(device, &pLogFile, NAMING_SERIAL_NUMBER_DATE_TIME, M_NULLPTR, logName, "bin"))
                    {
                        if (SEC_FILE_SUCCESS != secure_Write_File(pLogFile, logBuffer, uint64_to_sizet(size), sizeof(uint8_t), uint64_to_sizet(size), M_NULLPTR))
                        {
                            printf("Error writing log to file!\n");
                        }
                        secure_Flush_File(pLogFile);
                        if (SEC_FILE_SUCCESS != secure_Close_File(pLogFile))
                        {
                            printf("Error closing file!\n");
                        }
                        if (VERBOSITY_QUIET < device->deviceVerbosity)
                        {
                            printf("Created %s with Log Page %" PRId32 " Information\n", pLogFile->fullpath, logNum);
                        }
                    }
                    else
                    {
                        retStatus = FILE_OPEN_ERROR;
                    }
                    free_Secure_File_Info(&pLogFile);
                }
                else
                {
                    retStatus = BAD_PARAMETER;
                }
            }
            else
            {
                retStatus = NOT_SUPPORTED;
            }
            safe_free(&logBuffer);
        }
        else
        {
            retStatus = MEMORY_FAILURE;
        }
    }
    else
    {
        retStatus = NOT_SUPPORTED;
    }
    /*switch (logNum) {
        case NVME_LOG_SMART_ID:
            switch (print_SMART_Attributes(device, SMART_ATTR_OUTPUT_RAW))
            {
            case SUCCESS:
                //nothing to print here since if it was successful, the log will be printed to the screen
                break;
            default:
                retStatus = 3;
                break;
            }
            break;
        case NVME_LOG_ERROR_ID:
            switch (nvme_Print_ERROR_Log_Page(device, 0))
            {
            case SUCCESS:
                //nothing to print here since if it was successful, the log will be printed to the screen
                break;
            default:
                retStatus = 3;
                break;
            }
            break;
        case NVME_LOG_FW_SLOT_ID:
            switch (nvme_Print_FWSLOTS_Log_Page(device))
            {
            case SUCCESS:
                //nothing to print here since if it was successful, the log will be printed to the screen
                break;
            default:

                retStatus = 3;
                break;
            }
            break;
        case NVME_LOG_CMD_SPT_EFET_ID:
            switch (nvme_Print_CmdSptEfft_Log_Page(device))
            {
            case SUCCESS:
                //nothing to print here since if it was successful, the log will be printed to the screen
                break;
            default:
                retStatus = 3;
                break;
            }
            break;
        default:

            retStatus = 3;
            break;
    }*/
    return retStatus;
}

eReturnValues print_Supported_SCSI_Error_History_Buffer_IDs(tDevice *device, uint64_t flags)
{
    eReturnValues ret = NOT_SUPPORTED;
    uint32_t errorHistorySize = SCSI_ERROR_HISTORY_DIRECTORY_LEN;
    uint8_t *errorHistoryDirectory = C_CAST(uint8_t*, safe_calloc_aligned(errorHistorySize, sizeof(uint8_t), device->os_info.minimumAlignment));
    M_USE_UNUSED(flags);
    bool rb16 = is_SCSI_Read_Buffer_16_Supported(device);
    if (errorHistoryDirectory)
    {
        if ((rb16 && SUCCESS == scsi_Read_Buffer_16(device, 0x1C, 0, 0, 0, errorHistorySize, errorHistoryDirectory)) || SUCCESS == scsi_Read_Buffer(device, 0x1C, 0, 0, errorHistorySize, errorHistoryDirectory))
        {
            ret = SUCCESS;
            DECLARE_ZERO_INIT_ARRAY(char, vendorIdentification, 9);
            uint8_t version = errorHistoryDirectory[1];
            uint16_t directoryLength = M_BytesTo2ByteValue(errorHistoryDirectory[30], errorHistoryDirectory[31]);
            memcpy(vendorIdentification, errorHistoryDirectory, 8);
            if ((C_CAST(uint32_t, directoryLength) + (UINT32_C(32)) > errorHistorySize))
            {
                errorHistorySize = directoryLength + 32;
                //realloc and re-read
                uint8_t *temp = C_CAST(uint8_t*, safe_realloc_aligned(errorHistoryDirectory, 2048, errorHistorySize, device->os_info.minimumAlignment));
                if (temp)
                {
                    errorHistoryDirectory = temp;
                    memset(errorHistoryDirectory, 0, errorHistorySize);
                    scsi_Read_Buffer(device, 0x1C, 0, 0, errorHistorySize, errorHistoryDirectory);
                    directoryLength = M_BytesTo2ByteValue(errorHistoryDirectory[30], errorHistoryDirectory[31]);
                }
                else
                {
                    ret = MEMORY_FAILURE;
                    safe_free_aligned(&errorHistoryDirectory);
                    return ret;
                }
            }
            printf("======Vendor Specific Error History Buffer IDs========\n");
            printf(" Vendor = %s\n", vendorIdentification);
            printf(" Version = %" PRIu8 "\n", version);
            printf("    Buffer ID    :    Data Format    :    Size (Bytes)\n");
            //go through the directory in a loop
            for (uint32_t iter = UINT32_C(32); iter < (directoryLength + UINT32_C(32)) && iter < errorHistorySize; iter += UINT32_C(8))
            {
#define DATA_FORMAT_STRING_LENGTH 16
                DECLARE_ZERO_INIT_ARRAY(char, dataFormatString, DATA_FORMAT_STRING_LENGTH);
                uint8_t bufferID = errorHistoryDirectory[iter + 0];
                uint8_t bufferFormat = errorHistoryDirectory[iter + 1];
                uint32_t maximumLengthAvailable = M_BytesTo4ByteValue(errorHistoryDirectory[iter + 4], errorHistoryDirectory[iter + 5], errorHistoryDirectory[iter + 6], errorHistoryDirectory[iter + 7]);
                switch (bufferFormat)
                {
                case 0://vendor specific data
                    snprintf(dataFormatString, DATA_FORMAT_STRING_LENGTH, "Vendor Specific");
                    break;
                case 1://current internal status parameter data
                    snprintf(dataFormatString, DATA_FORMAT_STRING_LENGTH, "Current ISL");
                    break;
                case 2://saved internal status parameter data
                    snprintf(dataFormatString, DATA_FORMAT_STRING_LENGTH, "Saved ISL");
                    break;
                default://unknown or reserved
                    snprintf(dataFormatString, DATA_FORMAT_STRING_LENGTH, "Reserved");
                    break;
                }
                printf("  %3" PRIu8 " (%02" PRIX8 "h)      :  %-16s :    %" PRIu32 "\n", bufferID, bufferID, dataFormatString, maximumLengthAvailable);
            }
        }
    }
    else
    {
        ret = MEMORY_FAILURE;
    }
    safe_free_aligned(&errorHistoryDirectory);
    return ret;
}

static eReturnValues pull_Generic_ATA_Log(tDevice *device, uint8_t logNum, eLogPullMode mode, const char * const filePath, uint32_t transferSizeBytes, char* logFileName)
{
    eReturnValues retStatus = NOT_SUPPORTED;
    uint32_t logSize = 0;
    uint8_t *genericLogBuf = M_NULLPTR;
    //First, setting up bools for GPL and SMART logging features based on drive capabilities
    bool gpl = device->drive_info.ata_Options.generalPurposeLoggingSupported;
    bool smart = (is_SMART_Enabled(device) && ((is_ATA_Identify_Word_Valid_With_Bits_14_And_15(device->drive_info.IdentifyData.ata.Word084) && device->drive_info.IdentifyData.ata.Word084 & BIT0) || (is_ATA_Identify_Word_Valid_With_Bits_14_And_15(device->drive_info.IdentifyData.ata.Word087) && device->drive_info.IdentifyData.ata.Word087 & BIT0)));
    //Now, using switch case to handle KNOWN logs from ATA spec. Only flipping certain logs as most every modern drive uses GPL 
    //and most logs are GPL access now (but it wasn't always that way, and this works around some bugs in drive firmware!!!)
    switch (logNum)
    {
    case ATA_LOG_SUMMARY_SMART_ERROR_LOG:
    case ATA_LOG_COMPREHENSIVE_SMART_ERROR_LOG:
    case ATA_LOG_SMART_SELF_TEST_LOG:
    case ATA_LOG_SELECTIVE_SELF_TEST_LOG:
        //All of these logs are specified as access with SMART read log only, so disabling GPL. All others should be accessible with GPL when supported.
        gpl = false;
        break;
    default:
        break;
    }
    switch (mode)
    {
    case PULL_LOG_BIN_FILE_MODE:
        retStatus = get_ATA_Log(device, logNum, logFileName, "bin", gpl, smart, false, M_NULLPTR, 0, filePath, transferSizeBytes, 0);
        break;
    case PULL_LOG_RAW_MODE:
        if (SUCCESS == get_ATA_Log_Size(device, logNum, &logSize, true, false))
        {
            genericLogBuf = C_CAST(uint8_t*, safe_calloc_aligned(logSize, sizeof(uint8_t), device->os_info.minimumAlignment));
            if (genericLogBuf)
            {
                retStatus = get_ATA_Log(device, logNum, M_NULLPTR, M_NULLPTR, true, false, true, genericLogBuf, logSize, M_NULLPTR, transferSizeBytes, 0);
                if (SUCCESS == retStatus)
                {
                    print_Data_Buffer(genericLogBuf, logSize, true);
                }
            }
            else
            {
                retStatus = MEMORY_FAILURE;
            }
        }
        break;
    default:
        break;
    }
    safe_free_aligned(&genericLogBuf);
    return retStatus;
}

static eReturnValues pull_Generic_SCSI_Log(tDevice *device, uint8_t logNum, uint8_t subpage, eLogPullMode mode, const char * const filePath, char* logFileName)
{
    eReturnValues retStatus = NOT_SUPPORTED;
    uint32_t logSize = 0;
    uint8_t *genericLogBuf = M_NULLPTR;
    switch (mode)
    {
    case PULL_LOG_BIN_FILE_MODE:
        retStatus = get_SCSI_Log(device, logNum, subpage, logFileName, "bin", false, M_NULLPTR, 0, filePath);
        break;
    case PULL_LOG_RAW_MODE:
        if (SUCCESS == get_SCSI_Log_Size(device, logNum, subpage, &logSize))
        {
            genericLogBuf = C_CAST(uint8_t*, safe_calloc_aligned(logSize, sizeof(uint8_t), device->os_info.minimumAlignment));
            if (genericLogBuf)
            {
                retStatus = get_SCSI_Log(device, logNum, subpage, M_NULLPTR, M_NULLPTR, true, genericLogBuf, logSize, M_NULLPTR);
                if (SUCCESS == retStatus)
                {
                    print_Data_Buffer(genericLogBuf, logSize, true);
                }
            }
            else
            {
                retStatus = MEMORY_FAILURE;
            }
        }
        break;
    default:
        break;
    }
    safe_free_aligned(&genericLogBuf);
    return retStatus;
}

eReturnValues pull_Generic_Log(tDevice *device, uint8_t logNum, uint8_t subpage, eLogPullMode mode, const char * const filePath, uint32_t transferSizeBytes, uint32_t logLengthOverride)
{
    eReturnValues retStatus = NOT_SUPPORTED;
#define GENERIC_LOG_FILE_NAME_LENGTH 20
#define LOG_NUMBER_POST_FIX_LENGTH 10
    DECLARE_ZERO_INIT_ARRAY(char, logFileName, GENERIC_LOG_FILE_NAME_LENGTH + LOG_NUMBER_POST_FIX_LENGTH);
    if (device->drive_info.drive_type == SCSI_DRIVE && subpage != 0)
    {
        snprintf(logFileName, GENERIC_LOG_FILE_NAME_LENGTH + LOG_NUMBER_POST_FIX_LENGTH, "GENERIC_LOG-%u-%u", logNum, subpage);
    }
    else
    {
        snprintf(logFileName, GENERIC_LOG_FILE_NAME_LENGTH + LOG_NUMBER_POST_FIX_LENGTH, "GENERIC_LOG-%u", logNum);
    }
#ifdef _DEBUG
    printf("%s: Log to Pull %d, mode %d, device type %d\n", __FUNCTION__, logNum, C_CAST(uint8_t, mode), device->drive_info.drive_type);
#endif

    switch (device->drive_info.drive_type)
    {
    case ATA_DRIVE:
        retStatus = pull_Generic_ATA_Log(device, logNum, mode, filePath, transferSizeBytes, logFileName);
        break;
    case SCSI_DRIVE:
        retStatus = pull_Generic_SCSI_Log(device, logNum, subpage, mode, filePath, logFileName);
        break;
    case NVME_DRIVE:
        retStatus = pull_Supported_NVMe_Logs(device, logNum, mode, logLengthOverride);
        break;
    default:
        break;
    }
    return retStatus;
}

eReturnValues pull_Generic_Error_History(tDevice *device, uint8_t bufferID, eLogPullMode mode, const char * const filePath, uint32_t transferSizeBytes)
{
    eReturnValues retStatus = NOT_SUPPORTED;
    uint32_t logSize = 0;
    uint8_t *genericLogBuf = M_NULLPTR;
#define ERROR_HISTORY_FILENAME_LENGTH 30
#define ERROR_HISTORY_POST_FIX_LENGTH 10
    DECLARE_ZERO_INIT_ARRAY(char, errorHistoryFileName, ERROR_HISTORY_FILENAME_LENGTH + ERROR_HISTORY_POST_FIX_LENGTH);
    snprintf(errorHistoryFileName, ERROR_HISTORY_FILENAME_LENGTH + ERROR_HISTORY_POST_FIX_LENGTH, "GENERIC_ERROR_HISTORY-%" PRIu8, bufferID);
    bool rb16 = is_SCSI_Read_Buffer_16_Supported(device);

    switch (mode)
    {
    case PULL_LOG_BIN_FILE_MODE:
        retStatus = get_SCSI_Error_History(device, bufferID, errorHistoryFileName, false, rb16, "bin", false, M_NULLPTR, 0, filePath, transferSizeBytes, M_NULLPTR);
        break;
    case PULL_LOG_RAW_MODE:
        if (SUCCESS == get_SCSI_Error_History_Size(device, bufferID, &logSize, false, rb16))
        {
            genericLogBuf = C_CAST(uint8_t*, safe_calloc_aligned(logSize, sizeof(uint8_t), device->os_info.minimumAlignment));
            if (genericLogBuf)
            {
                retStatus = get_SCSI_Error_History(device, bufferID, M_NULLPTR, false, rb16, M_NULLPTR, true, genericLogBuf, logSize, M_NULLPTR, transferSizeBytes, M_NULLPTR);
                if (SUCCESS == retStatus)
                {
                    print_Data_Buffer(genericLogBuf, logSize, true);
                }
            }
            else
            {
                retStatus = MEMORY_FAILURE;
            }
        }
        break;
    default:
        break;
    }
    safe_free_aligned(&genericLogBuf);
    return retStatus;
}

eReturnValues pull_FARM_LogPage(tDevice *device, const char * const filePath, uint32_t transferSizeBytes, uint32_t issueFactory, uint16_t logPage, uint8_t logAddress, eLogPullMode mode)
{
    bool fileOpened = false;
    secureFileInfo *fp_log = M_NULLPTR;
    eReturnValues ret = UNKNOWN;
    uint16_t pagesToReadAtATime = 1;
    uint16_t pagesToReadNow = 1;
    uint16_t currentPage = 0;
    uint16_t numberOfLogPages = C_CAST(uint16_t, FARM_SUBLOGPAGE_LEN / LEGACY_DRIVE_SEC_SIZE);
    uint8_t *logBuffer = C_CAST(uint8_t *, safe_calloc_aligned((32 * LEGACY_DRIVE_SEC_SIZE), sizeof(uint8_t), device->os_info.minimumAlignment));
    DECLARE_ZERO_INIT_ARRAY(char, logType, OPENSEA_PATH_MAX);

    if (device->drive_info.drive_type == ATA_DRIVE)
    {
        switch (mode)
        {
        case PULL_LOG_RAW_MODE:
        case PULL_LOG_PIPE_MODE:
        case PULL_LOG_ANALYZE_MODE:
            safe_free_aligned(&logBuffer);
            return NOT_SUPPORTED;
        case PULL_LOG_BIN_FILE_MODE:
        default:
            snprintf(logType, OPENSEA_PATH_MAX, "FARM_PAGE_%d", logPage);
            if (device->drive_info.interface_type != USB_INTERFACE && device->drive_info.interface_type != IEEE_1394_INTERFACE)
            {
                pagesToReadAtATime = 32;
            }
            else
            {
                //USB and IEEE 1394 should only ever be read 1 page at a time since these interfaces use cheap bridge chips that typically don't allow larger transfers.
                pagesToReadAtATime = 1;
            }

            if (transferSizeBytes)
            {
                if (transferSizeBytes % LEGACY_DRIVE_SEC_SIZE)
                {
                    safe_free_aligned(&logBuffer);
                    return BAD_PARAMETER;
                }
                //caller is telling us how much to read at a time
                pagesToReadAtATime = C_CAST(uint16_t, (transferSizeBytes / LEGACY_DRIVE_SEC_SIZE));
            }

            for (currentPage = 0; currentPage < numberOfLogPages; currentPage += pagesToReadAtATime)
            {
                pagesToReadNow = C_CAST(uint16_t, M_Min(numberOfLogPages - currentPage, pagesToReadAtATime));
                if (SUCCESS == send_ATA_Read_Log_Ext_Cmd(device, logAddress, C_CAST(uint16_t, (logPage * TOTAL_CONSTITUENT_PAGES) + currentPage), &logBuffer[currentPage * LEGACY_DRIVE_SEC_SIZE], pagesToReadNow * LEGACY_DRIVE_SEC_SIZE, (uint16_t)issueFactory))
                {
                    if (!fileOpened)
                    {
                        if (SUCCESS == create_And_Open_Secure_Log_File_Dev_EZ(device, &fp_log, NAMING_SERIAL_NUMBER_DATE_TIME, filePath, logType, "bin"))
                        {
                            fileOpened = true;
                        }
                        else
                        {
                            if (VERBOSITY_QUIET < device->deviceVerbosity)
                            {
                                printf("Failed to open file!\n");
                            }
                            ret = FAILURE;
                            safe_free_aligned(&logBuffer);
                            free_Secure_File_Info(&fp_log);
                        }
                    }
                    if (fileOpened)
                    {
                        //write the page to a file
                        if (SEC_FILE_SUCCESS != secure_Write_File(fp_log, &logBuffer[currentPage * LEGACY_DRIVE_SEC_SIZE], (32 * LEGACY_DRIVE_SEC_SIZE), sizeof(uint8_t), (pagesToReadNow * LEGACY_DRIVE_SEC_SIZE), M_NULLPTR))
                        {
                            if (VERBOSITY_QUIET < device->deviceVerbosity)
                            {
                                perror("Error writing vpd data to a file!\n");
                            }
                            if (SEC_FILE_SUCCESS != secure_Close_File(fp_log))
                            {
                                printf("Error closing file!\n");
                            }
                            fileOpened = false;
                            safe_free_aligned(&logBuffer);
                            free_Secure_File_Info(&fp_log);
                            return ERROR_WRITING_FILE;
                        }
                        ret = SUCCESS;
                    }
                }
                else
                {
                    ret = FAILURE;
                    break;
                }
            }

            break;
        }
    }
    else
    {
        ret = NOT_SUPPORTED;
    }

    if (fileOpened)
    {
        if (SEC_FILE_SUCCESS != secure_Close_File(fp_log))
        {
            printf("Error closing file!\n");
        }
        fileOpened = false;
    }
    free_Secure_File_Info(&fp_log);
    safe_free_aligned(&logBuffer);
    return ret;
}

eReturnValues pull_FARM_Log(tDevice *device, const char * const filePath, uint32_t transferSizeBytes, uint32_t issueFactory, uint8_t logAddress, eLogPullMode mode)
{
    eReturnValues ret = UNKNOWN;
    uint32_t logSize = 0;
    uint8_t* genericLogBuf = M_NULLPTR;
    if (device->drive_info.drive_type == ATA_DRIVE)
    {
        switch (logAddress)
        {
        case SEAGATE_ATA_LOG_FARM_TIME_SERIES:
            switch (mode)
            {
            case PULL_LOG_PIPE_MODE:
            case PULL_LOG_RAW_MODE:
                ret = get_ATA_Log_Size(device, logAddress, &logSize, true, false);
                if (ret == SUCCESS && logSize > 0)
                {
                    genericLogBuf = C_CAST(uint8_t*, safe_calloc_aligned(logSize, sizeof(uint8_t), device->os_info.minimumAlignment));
                }
                else
                {
                    ret = MEMORY_FAILURE;
                }

                if (genericLogBuf)
                {
                    if (issueFactory == 2)
                    {
                        ret = get_ATA_Log(device, logAddress, M_NULLPTR, M_NULLPTR, true, false, true, genericLogBuf, logSize, M_NULLPTR, logSize, SEAGATE_FARM_TIME_SERIES_FLASH);
                    }
                    else if (issueFactory == 3)
                    {
                        ret = get_ATA_Log(device, logAddress, M_NULLPTR, M_NULLPTR, true, false, true, genericLogBuf, logSize, M_NULLPTR, logSize, SEAGATE_FARM_TIME_SERIES_WLTR);
                    }
                    else if (issueFactory == 4)
                    {
                        ret = get_ATA_Log(device, logAddress, M_NULLPTR, M_NULLPTR, true, false, true, genericLogBuf, logSize, M_NULLPTR, logSize, SEAGATE_FARM_TIME_SERIES_NEURAL_NW);
                    }
                    else
                    {
                        ret = get_ATA_Log(device, logAddress, M_NULLPTR, M_NULLPTR, true, false, true, genericLogBuf, logSize, M_NULLPTR, logSize, SEAGATE_FARM_TIME_SERIES_DISC);
                    }
                }
                else
                {
                    ret = MEMORY_FAILURE;
                }

                if (SUCCESS == ret)
                {
                    if (mode == PULL_LOG_PIPE_MODE)
                    {
                        print_Pipe_Data(genericLogBuf, logSize);
                    }
                    else
                    {
                        print_Data_Buffer(genericLogBuf, logSize, true);
                    }
                }
                break;
            case PULL_LOG_BIN_FILE_MODE:
            default:
                //FARM pull time series subpages   
                //1 (feature register 0) - Default: Report all FARM frames from disc (~250ms) (SATA only)
                //2 (feature register 1) - Report all FARM data (~250ms)(SATA only)
                //3 (feature register 2) - Return WLTR data (SATA only)
                //4 (feature register 3) - Return Neural N/W data (SATA only)
                if (issueFactory == 2)
                {
                    ret = get_ATA_Log(device, logAddress, "FARM_TIME_SERIES_FLASH", "bin", true, false, false, M_NULLPTR, 0, filePath, transferSizeBytes, SEAGATE_FARM_TIME_SERIES_FLASH);
                }
                else if (issueFactory == 3)
                {
                    ret = get_ATA_Log(device, logAddress, "FARM_WLTR", "bin", true, false, false, M_NULLPTR, 0, filePath, transferSizeBytes, SEAGATE_FARM_TIME_SERIES_WLTR);
                }
                else if (issueFactory == 4)
                {
                    ret = get_ATA_Log(device, logAddress, "FARM_NEURAL_NW", "bin", true, false, false, M_NULLPTR, 0, filePath, transferSizeBytes, SEAGATE_FARM_TIME_SERIES_NEURAL_NW);
                }
                else
                {
                    ret = get_ATA_Log(device, logAddress, "FARM_TIME_SERIES_DISC", "bin", true, false, false, M_NULLPTR, 0, filePath, transferSizeBytes, SEAGATE_FARM_TIME_SERIES_DISC);
                }
                break;
            }
            break;
        default:
            switch (mode)
            {
            case PULL_LOG_PIPE_MODE:
            case PULL_LOG_RAW_MODE:
                //FARM pull Factory subpages   
                //0 - Default: Generate and report new FARM data but do not save to disc (~7ms) (SATA only)
                //1 - Generate and report new FARM data and save to disc(~45ms)(SATA only)
                //2 - Report previous FARM data from disc(~20ms)(SATA only)
                //3 - Report FARM factory data from disc(~20ms)(SATA only)
                ret = get_ATA_Log_Size(device, logAddress, &logSize, true, false);
                if (ret == SUCCESS && logSize > 0)
                {
                    genericLogBuf = C_CAST(uint8_t*, safe_calloc_aligned(logSize, sizeof(uint8_t), device->os_info.minimumAlignment));
                }
                else
                {
                    ret = MEMORY_FAILURE;
                }

                if (genericLogBuf)
                {
                    if (issueFactory == 1)
                    {
                        ret = get_ATA_Log(device, SEAGATE_ATA_LOG_FIELD_ACCESSIBLE_RELIABILITY_METRICS, M_NULLPTR, M_NULLPTR, true, false, true, genericLogBuf, logSize, M_NULLPTR, logSize, SEAGATE_FARM_GENERATE_NEW_AND_SAVE);
                    }
                    else if (issueFactory == 2)
                    {
                        ret = get_ATA_Log(device, SEAGATE_ATA_LOG_FIELD_ACCESSIBLE_RELIABILITY_METRICS, M_NULLPTR, M_NULLPTR, true, false, true, genericLogBuf, logSize, M_NULLPTR, logSize, SEAGATE_FARM_REPORT_SAVED);
                    }
                    else if (issueFactory == 3)
                    {
                        ret = get_ATA_Log(device, SEAGATE_ATA_LOG_FIELD_ACCESSIBLE_RELIABILITY_METRICS, M_NULLPTR, M_NULLPTR, true, false, true, genericLogBuf, logSize, M_NULLPTR, logSize, SEAGATE_FARM_REPORT_FACTORY_DATA);
                    }
                    else
                    {
                        ret = get_ATA_Log(device, SEAGATE_ATA_LOG_FIELD_ACCESSIBLE_RELIABILITY_METRICS, M_NULLPTR, M_NULLPTR, true, false, true, genericLogBuf, logSize, M_NULLPTR, logSize, SEAGATE_FARM_CURRENT);
                    }
                }
                else
                {
                    ret = MEMORY_FAILURE;
                }

                if (SUCCESS == ret)
                {
                    if (mode == PULL_LOG_PIPE_MODE)
                    {
                        print_Pipe_Data(genericLogBuf, logSize);
                    }
                    else
                    {
                        print_Data_Buffer(genericLogBuf, logSize, true);
                    }
                }
                break;
            case PULL_LOG_BIN_FILE_MODE:
            default:
                //FARM pull Factory subpages   
                //0 - Default: Generate and report new FARM data but do not save to disc (~7ms) (SATA only)
                //1 - Generate and report new FARM data and save to disc(~45ms)(SATA only)
                //2 - Report previous FARM data from disc(~20ms)(SATA only)
                //3 - Report FARM factory data from disc(~20ms)(SATA only)
                if (issueFactory == 1)
                {
                    ret = get_ATA_Log(device, SEAGATE_ATA_LOG_FIELD_ACCESSIBLE_RELIABILITY_METRICS, "P_AND_S_FARM", "bin", true, false, false, M_NULLPTR, 0, filePath, transferSizeBytes, SEAGATE_FARM_GENERATE_NEW_AND_SAVE);
                }
                else if (issueFactory == 2)
                {
                    ret = get_ATA_Log(device, SEAGATE_ATA_LOG_FIELD_ACCESSIBLE_RELIABILITY_METRICS, "PREVIOUS_FARM", "bin", true, false, false, M_NULLPTR, 0, filePath, transferSizeBytes, SEAGATE_FARM_REPORT_SAVED);
                }
                else if (issueFactory == 3)
                {
                    ret = get_ATA_Log(device, SEAGATE_ATA_LOG_FIELD_ACCESSIBLE_RELIABILITY_METRICS, "FACTORY_FARM", "bin", true, false, false, M_NULLPTR, 0, filePath, transferSizeBytes, SEAGATE_FARM_REPORT_FACTORY_DATA);
                }
                else
                {
                    ret = get_ATA_Log(device, SEAGATE_ATA_LOG_FIELD_ACCESSIBLE_RELIABILITY_METRICS, "FARM", "bin", true, false, false, M_NULLPTR, 0, filePath, transferSizeBytes, SEAGATE_FARM_CURRENT);
                }
                break;
            }
        }
    }
    else if (device->drive_info.drive_type == SCSI_DRIVE)
    {
        switch (mode)
        {
        case PULL_LOG_PIPE_MODE:
        case PULL_LOG_RAW_MODE:
            //FARM pull Factory subpages   
            //0 - Default: Generate and report new FARM data but do not save to disc (~7ms) (SATA only)
            //4 - factory subpage (SAS only)
            if (issueFactory == 4)
            {
                if (SUCCESS == get_SCSI_Log_Size(device, SEAGATE_LP_FARM, SEAGATE_FARM_SP_FACTORY, &logSize))
                {
                    genericLogBuf = C_CAST(uint8_t*, safe_calloc_aligned(logSize, sizeof(uint8_t), device->os_info.minimumAlignment));
                    if (genericLogBuf)
                    {
                        ret = get_SCSI_Log(device, SEAGATE_LP_FARM, SEAGATE_FARM_SP_FACTORY, M_NULLPTR, M_NULLPTR, true, genericLogBuf, logSize, M_NULLPTR);
                    }
                    else
                    {
                        ret = MEMORY_FAILURE;
                    }
                }
            }
            else
            {
                if (SUCCESS == get_SCSI_Log_Size(device, SEAGATE_LP_FARM, SEAGATE_FARM_SP_FACTORY, &logSize))
                {
                    genericLogBuf = C_CAST(uint8_t*, safe_calloc_aligned(logSize, sizeof(uint8_t), device->os_info.minimumAlignment));
                    if (genericLogBuf)
                    {
                        ret = get_SCSI_Log(device, SEAGATE_LP_FARM, SEAGATE_FARM_SP_CURRENT, M_NULLPTR, M_NULLPTR, true, genericLogBuf, logSize, M_NULLPTR);
                    }
                    else
                    {
                        ret = MEMORY_FAILURE;
                    }
                }
            }
            if (SUCCESS == ret)
            {
                if (mode == PULL_LOG_PIPE_MODE)
                {
                    print_Pipe_Data(genericLogBuf, logSize);
                }
                else
                {
                    print_Data_Buffer(genericLogBuf, logSize, true);
                }
            }
            break;
        case PULL_LOG_BIN_FILE_MODE:
        default:
            //FARM pull Factory subpages   
            //0 - Default: Generate and report new FARM data but do not save to disc (~7ms) (SATA only)
            //4 - factory subpage (SAS only)
            if (issueFactory == 4)
            {
                ret = get_SCSI_Log(device, SEAGATE_LP_FARM, SEAGATE_FARM_SP_FACTORY, "FACTORY_FARM", "bin", false, M_NULLPTR, 0, filePath);

            }
            else
            {
                ret = get_SCSI_Log(device, SEAGATE_LP_FARM, SEAGATE_FARM_SP_CURRENT, "FARM", "bin", false, M_NULLPTR, 0, filePath);
            }
            break;
        }
    }
    else
    {
        ret = NOT_SUPPORTED;
    }
    safe_free_aligned(&genericLogBuf);
    return ret;
}

bool is_FARM_Log_Supported(tDevice *device)
{
    bool supported = false;
    uint32_t logSize = 0;
#ifdef _DEBUG
    printf("%s -->\n", __FUNCTION__);
#endif

    if ((device->drive_info.drive_type == ATA_DRIVE) && (get_ATA_Log_Size(device, 0xA6, &logSize, true, false) == SUCCESS))
    {
        supported = true;
    }
    else if ((device->drive_info.drive_type == SCSI_DRIVE) && (get_SCSI_Log_Size(device, 0x3D, 0x03, &logSize) == SUCCESS))
    {
        supported = true;
    }
    //else currently not supported on NVMe. 
#ifdef _DEBUG
    printf("%s <-- (%d)\n", __FUNCTION__, supported);
#endif

    return supported;

}

bool is_Factory_FARM_Log_Supported(tDevice *device)
{
    bool supported = false;
    uint32_t logSize = 0;
#ifdef _DEBUG
    printf("%s -->\n", __FUNCTION__);
#endif

    if ((device->drive_info.drive_type == SCSI_DRIVE) && (get_SCSI_Log_Size(device, 0x3D, 0x04, &logSize) == SUCCESS))
    {
        supported = true;
    }

#ifdef _DEBUG
    printf("%s <-- (%d)\n", __FUNCTION__, supported);
#endif

    return supported;
}

bool is_FARM_Time_Series_Log_Supported(tDevice *device)
{
    bool supported = false;
    uint32_t logSize = 0;
#ifdef _DEBUG
    printf("%s -->\n", __FUNCTION__);
#endif

    if ((device->drive_info.drive_type == ATA_DRIVE) && (get_ATA_Log_Size(device, 0xC6, &logSize, true, false) == SUCCESS))
    {
        supported = true;
    }
    else if ((device->drive_info.drive_type == SCSI_DRIVE) && (get_SCSI_Log_Size(device, 0x3D, 0x10, &logSize) == SUCCESS))
    {
        supported = true;
    }
    //else currently not supported on  NVMe. 
#ifdef _DEBUG
    printf("%s <-- (%d)\n", __FUNCTION__, supported);
#endif

    return supported;

}

bool is_FARM_Sticky_Log_Supported(tDevice *device)
{
    bool supported = false;
    uint32_t logSize = 0;
#ifdef _DEBUG
    printf("%s -->\n", __FUNCTION__);
#endif

    if ((device->drive_info.drive_type == SCSI_DRIVE) && (get_SCSI_Log_Size(device, 0x3D, 0xC2, &logSize) == SUCCESS))
    {
        supported = true;
    }

#ifdef _DEBUG
    printf("%s <-- (%d)\n", __FUNCTION__, supported);
#endif

    return supported;
}

bool is_FARM_Long_Saved_Log_Supported(tDevice *device)
{
    bool supported = false;
    uint32_t logSize = 0;
#ifdef _DEBUG
    printf("%s -->\n", __FUNCTION__);
#endif

    if ((device->drive_info.drive_type == SCSI_DRIVE) && (get_SCSI_Log_Size(device, 0x3D, 0xC0, &logSize) == SUCCESS))
    {
        supported = true;
    }

#ifdef _DEBUG
    printf("%s <-- (%d)\n", __FUNCTION__, supported);
#endif

    return supported;
}
