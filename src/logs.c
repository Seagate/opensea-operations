//
// Do NOT modify or remove this copyright and license
//
// Copyright (c) 2012-2022 Seagate Technology LLC and/or its Affiliates, All Rights Reserved
//
// This software is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
//
// ******************************************************************************************
// 
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

int generate_Logfile_Name(tDevice *device, const char * const logName, const char * const logExtension,\
                           eLogFileNamingConvention logFileNamingConvention, char **logFileNameUsed)
{
    int ret = SUCCESS;
    time_t currentTime = 0;
    char currentTimeString[64] = { 0 };
    struct tm logTime;
    memset(&logTime, 0, sizeof(struct tm));
    #ifdef _DEBUG
    printf("%s: Drive SN: %s#\n",__FUNCTION__, device->drive_info.serialNumber);
    #endif
    //JIRA FD-103 says for log file names we always want to use the child drive SN on USB. I thought about passing in a flag for this in case we read a SCSI log page over USB, but I figured we probably won't do that and/or don't care to do that so for now this will work and is simple-TJE
    char *serialNumber = device->drive_info.serialNumber;
    if ((device->drive_info.interface_type == USB_INTERFACE || device->drive_info.interface_type == IEEE_1394_INTERFACE) && strlen(device->drive_info.bridge_info.childDriveSN) > 0)
    {
        serialNumber = device->drive_info.bridge_info.childDriveSN;
    }
    switch (logFileNamingConvention)
    {
    case NAMING_SERIAL_NUMBER_ONLY:
        snprintf(*logFileNameUsed, OPENSEA_PATH_MAX, "%s", serialNumber);
        break;
    case NAMING_SERIAL_NUMBER_DATE_TIME:
        //get current date and time
        currentTime = time(NULL);
        memset(currentTimeString, 0, sizeof(currentTimeString) / sizeof(*currentTimeString));
        strftime(currentTimeString, sizeof(currentTimeString) / sizeof(*currentTimeString), "%Y%m%dT%H%M%S", get_Localtime(&currentTime, &logTime));
        //set up the log file name
        snprintf(*logFileNameUsed, OPENSEA_PATH_MAX, "%s_%s_%s", serialNumber, logName, currentTimeString);
        break;
    case NAMING_OPENSTACK:
        return NOT_SUPPORTED;
    case NAMING_BYUSER:
        common_String_Concat(*logFileNameUsed, OPENSEA_PATH_MAX, logName);
        break;
    default:
        return BAD_PARAMETER;
    }
    char *dup = strdup(*logFileNameUsed);
    if(dup)
    {
        snprintf(*logFileNameUsed, OPENSEA_PATH_MAX, "%s.%s", dup, logExtension);
    }
    else
    {
        ret = MEMORY_FAILURE;
    }
    return ret;
}

int create_And_Open_Log_File(tDevice *device,\
                             FILE **filePtr,\
                             const char * const logPath,\
                             const char * const logName,\
                             const char * const logExtension,\
                             eLogFileNamingConvention logFileNamingConvention,\
                             char **logFileNameUsed)
{
    int ret = SUCCESS;
    char name[OPENSEA_PATH_MAX] = { 0 }; //Hopefully our file names are not bigger than this. 
    char *filename = &name[0];
    char *pathAndFileName = NULL;
    bool nullLogFileNameUsed = false;
    struct tm logTime;
    memset(&logTime, 0, sizeof(struct tm));
    #ifdef _DEBUG
    printf("%s: -->\n",__FUNCTION__);
    #endif

    if (!logName || !logExtension || !device)
    {
        return BAD_PARAMETER;
    }
    #ifdef _DEBUG
    printf("\t logPath=%s, logName=%s, logExtension=%s\n"\
                        ,logPath, logName, logExtension);
    #endif
    ret = generate_Logfile_Name(device, logName, logExtension, logFileNamingConvention, &filename);
    if (SUCCESS != ret)
    {
        return ret;
    }
    
    if (SUCCESS == ret)
    {
        #ifdef _DEBUG
        printf("\tfilename %s\n",filename);
        #endif
        if (((logPath == NULL) || (strcmp((logPath), "") == 0)) &&
            ((*logFileNameUsed) && (strcmp((*logFileNameUsed), "") == 0)))
        {
            //logPath is null or empty, logFileNameUsed is non-null but it is empty. 
            //So assigning the generated filename to logFileNameUsed
            snprintf(*logFileNameUsed, OPENSEA_PATH_MAX, "%s", filename);
        }
        else if (*logFileNameUsed)
        {
            if (strcmp((*logFileNameUsed), "") == 0)
            {
                //logPath has valid value and logFileNameUsed is empty. Prepend logpath to the generated filename
                snprintf(*logFileNameUsed, OPENSEA_PATH_MAX, "%s%c%s", logPath, SYSTEM_PATH_SEPARATOR, filename);
            }
            else
            {
                //Both logPath and logFileNameUsed have non-empty values
                char lpathNFilename[OPENSEA_PATH_MAX] = { 0 };
                char lpathNFilenameGeneration[OPENSEA_PATH_MAX] = { 0 };
                snprintf(lpathNFilename, OPENSEA_PATH_MAX, "%s", *logFileNameUsed);
                snprintf(lpathNFilenameGeneration, OPENSEA_PATH_MAX, "%s%c%s", logPath, SYSTEM_PATH_SEPARATOR, filename);
                if (strcmp(lpathNFilename, lpathNFilenameGeneration) == 0)
                {
                    snprintf(*logFileNameUsed, OPENSEA_PATH_MAX, "%s%c%s", logPath, SYSTEM_PATH_SEPARATOR, filename);
                }
                else
                {
                    snprintf(*logFileNameUsed, OPENSEA_PATH_MAX, "%s", lpathNFilenameGeneration);
                }
            }
        }
    }
    if(!*logFileNameUsed ) //when logFileNameUsed is NULL, need to allocate
    {
        nullLogFileNameUsed = true;
        if (logPath && (strcmp(logPath,"") != 0))
        {
            //need to append a path to the beginning of the file name!!!
            size_t pathAndFileNameLength = strlen(logPath) + strlen(filename) + 2;
            pathAndFileName = C_CAST(char*, calloc(pathAndFileNameLength, sizeof(char)));
            if (!pathAndFileName)
            {
                return MEMORY_FAILURE;
            }
            snprintf(pathAndFileName, pathAndFileNameLength, "%s%c%s", logPath, SYSTEM_PATH_SEPARATOR,filename);
            *logFileNameUsed = pathAndFileName;
        }
        else
        {
            *logFileNameUsed = filename;
        }
    }
    //check if file already exist
    if ((*filePtr = fopen(*logFileNameUsed, "r")) != NULL)
    {
        time_t currentTime = 0;
        char currentTimeString[64] = { 0 };
        fclose(*filePtr);
        //append timestamp
        currentTime = time(NULL);
        memset(currentTimeString, 0, 64);
        strftime(currentTimeString, 64, "%Y%m%dT%H%M%S", get_Localtime(&currentTime, &logTime));
        //Append timestamp to the log file name
        snprintf(*logFileNameUsed, OPENSEA_PATH_MAX, "_%s", &currentTimeString[0]);
    }

    #ifdef _DEBUG
    printf("logfileNameUsed = %s",*logFileNameUsed);
    #endif

    if ((*filePtr = fopen(*logFileNameUsed, "w+b")) == NULL)
    {
        printf("Couldn't open file %s\n", *logFileNameUsed);
        perror("fopen");
        ret = FILE_OPEN_ERROR;
    }
    if(nullLogFileNameUsed)
    {
        *logFileNameUsed = NULL;
    }
    #ifdef _DEBUG
    printf("%s: <--\n",__FUNCTION__);
    #endif

    safe_Free(pathAndFileName)

    return ret;
}
//TODO: A special hack is required in some cases where we may not be able to properly determine the log size..
//      this is due to drive firmware bugs and/or passthrough limitations from OSs, drivers, or adapters.
//      So certain standard logs that are known sizes will be returned in here in these cases...
int get_ATA_Log_Size(tDevice *device, uint8_t logAddress, uint32_t *logFileSize, bool gpl, bool smart)
{
    int ret = NOT_SUPPORTED;//assume the log is not supported
    bool foundInGPL = false;

    #ifdef _DEBUG
    printf("%s: logAddress %d, gpl=%s, smart=%s\n",__FUNCTION__, logAddress, gpl ? "true":"false", smart ? "true":"false");
    #endif

    uint8_t *logBuffer = C_CAST(uint8_t*, calloc_aligned(LEGACY_DRIVE_SEC_SIZE, sizeof(uint8_t), device->os_info.minimumAlignment));
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
        printf("\t generalPurposeLoggingSupported=%d\n",device->drive_info.ata_Options.generalPurposeLoggingSupported);
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
    safe_Free_aligned(logBuffer)
    return ret;
}

int get_SCSI_Log_Size(tDevice *device, uint8_t logPage, uint8_t logSubPage, uint32_t *logFileSize)
{
    int ret = NOT_SUPPORTED;//assume the log is not supported
    uint8_t *logBuffer = C_CAST(uint8_t*, calloc_aligned(255, sizeof(uint8_t), device->os_info.minimumAlignment));
    if (!logBuffer)
    {
        return MEMORY_FAILURE;
    }
    *logFileSize = 0;
    //first check that the logpage is supported
    if (logSubPage != 0 && SUCCESS == scsi_Log_Sense_Cmd(device, false, LPC_CUMULATIVE_VALUES, LP_SUPPORTED_LOG_PAGES_AND_SUBPAGES, 0xFF, 0, logBuffer, 255))
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
    safe_Free_aligned(logBuffer)
    return ret;
}

int get_SCSI_VPD_Page_Size(tDevice *device, uint8_t vpdPage, uint32_t *vpdPageSize)
{
    int ret = NOT_SUPPORTED;//assume the page is not supported
    uint32_t vpdBufferLength = INQ_RETURN_DATA_LENGTH;
    uint8_t *vpdBuffer = C_CAST(uint8_t *, calloc_aligned(vpdBufferLength, sizeof(uint8_t), device->os_info.minimumAlignment));
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
    safe_Free_aligned(vpdBuffer)
    return ret;
}

//modePageSize includes any blockdescriptors that may be present
int get_SCSI_Mode_Page_Size(tDevice *device, eScsiModePageControl mpc, uint8_t modePage, uint8_t subpage, uint32_t *modePageSize)
{
    int ret = NOT_SUPPORTED;//assume the page is not supported
    uint32_t modeLength = MODE_PARAMETER_HEADER_10_LEN;
    bool sixByte = false;
    //If device is older than SCSI2, DBD is not available and will be limited to 6 byte command
    //checking for this for old drives that may support mode pages, but not the dbd bit properly
    if (device->drive_info.scsiVersion < SCSI_VERSION_SCSI2)
    {
        sixByte = true;
        modeLength = MODE_PARAMETER_HEADER_6_LEN + SHORT_LBA_BLOCK_DESCRIPTOR_LEN;
    }
    uint8_t *modeBuffer = C_CAST(uint8_t *, calloc_aligned(modeLength, sizeof(uint8_t), device->os_info.minimumAlignment));
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
        if (SUCCESS == scsi_Mode_Sense_10(device, modePage, modeLength, subpage, true, true, mpc, modeBuffer))
        {
            *modePageSize = M_BytesTo2ByteValue(modeBuffer[0], modeBuffer[1]) + 2;
            ret = SUCCESS;
        }
        else
        {
            //if invalid operation code, then we should retry
            uint8_t senseKey = 0, asc = 0, ascq = 0, fru = 0;
            get_Sense_Key_ASC_ASCQ_FRU(device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, &senseKey, &asc, &ascq, &fru);
            if (senseKey == SENSE_KEY_ILLEGAL_REQUEST && asc == 0x20 && ascq == 0x00)//checking for invalid operation code
            {
                sixByte = true;
                modeLength = MODE_PARAMETER_HEADER_6_LEN + SHORT_LBA_BLOCK_DESCRIPTOR_LEN;
                //reallocate memory!
                uint8_t *temp = C_CAST(uint8_t*, realloc(modeBuffer, modeLength));
                if (!temp)
                {
                    return MEMORY_FAILURE;
                }
                modeBuffer = temp;
            }
        }
    }
    if (sixByte)//not an else because in the above if, we can retry the command as 6 byte if it doesn't work.
    {
        if (SUCCESS == scsi_Mode_Sense_6(device, modePage, C_CAST(uint8_t, modeLength), subpage, false, mpc, modeBuffer))//don't disable block descriptors here since this is mostly to support old drives.
        {
            *modePageSize = modeBuffer[0] + 1;
        }
    }
    safe_Free_aligned(modeBuffer)
    return ret;
}

int get_SCSI_Mode_Page(tDevice *device, eScsiModePageControl mpc, uint8_t modePage, uint8_t subpage, char *logName, char *fileExtension, bool toBuffer, uint8_t *myBuf, uint32_t bufSize, const char * const filePath, bool *used6ByteCmd)
{
    int ret = NOT_SUPPORTED;//assume the page is not supported
    uint32_t modeLength = 0;
    if (SUCCESS != get_SCSI_Mode_Page_Size(device, mpc, modePage, subpage, &modeLength))
    {
        return ret;
    }
    bool sixByte = false;
    //If device is older than SCSI2, DBD is not available and will be limited to 6 byte command
    //checking for this for old drives that may support mode pages, but not the dbd bit properly
    if (device->drive_info.scsiVersion < SCSI_VERSION_SCSI2)
    {
        sixByte = true;
    }
    uint8_t *modeBuffer = C_CAST(uint8_t *, calloc_aligned(modeLength, sizeof(uint8_t), device->os_info.minimumAlignment));
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
        if (SUCCESS == scsi_Mode_Sense_10(device, modePage, modeLength, subpage, true, true, mpc, modeBuffer))
        {
            FILE *fpmp = NULL;
            bool fileOpened = false;
            if (used6ByteCmd)
            {
                *used6ByteCmd = false;
            }
            if (!toBuffer && !fileOpened && ret != FAILURE)
            {
                char *fileNameUsed = NULL;
                if (SUCCESS == create_And_Open_Log_File(device, &fpmp, filePath, logName, fileExtension, NAMING_SERIAL_NUMBER_DATE_TIME, &fileNameUsed))
                {
                    fileOpened = true;
                }
            }
            if (fileOpened && ret != FAILURE)
            {
                //write the vpd page to a file
                if ((fwrite(modeBuffer, sizeof(uint8_t), modeLength, fpmp) != modeLength) || ferror(fpmp))
                {
                    if (VERBOSITY_QUIET < device->deviceVerbosity)
                    {
                        perror("Error writing vpd data to a file!\n");
                    }
                    fclose(fpmp);
                    fileOpened = false;
                    safe_Free_aligned(modeBuffer)
                    return ERROR_WRITING_FILE;
                }

            }
            if (toBuffer && ret != FAILURE)
            {
                if (bufSize >= modeLength)
                {
                    memcpy(myBuf, modeBuffer, modeLength);
                }
                else
                {
                    return BAD_PARAMETER;
                }
            }
            if (fileOpened)
            {
                if (fflush(fpmp) != 0 || ferror(fpmp))
                {
                    if (VERBOSITY_QUIET < device->deviceVerbosity)
                    {
                        perror("Error flushing data!\n");
                    }
                    fclose(fpmp);
                    fileOpened = false;
                    safe_Free_aligned(modeBuffer)
                    return ERROR_WRITING_FILE;
                }

                fclose(fpmp);
                fileOpened = false;
            }
            ret = SUCCESS;
        }
        else
        {
            //if invalid operation code, then we should retry
            uint8_t senseKey = 0, asc = 0, ascq = 0, fru = 0;
            get_Sense_Key_ASC_ASCQ_FRU(device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, &senseKey, &asc, &ascq, &fru);
            if (senseKey == SENSE_KEY_ILLEGAL_REQUEST && asc == 0x20 && ascq == 0x00)//checking for invalid operation code
            {
                sixByte = true;
                modeLength = MODE_PARAMETER_HEADER_6_LEN + SHORT_LBA_BLOCK_DESCRIPTOR_LEN;
                //reallocate memory!
                uint8_t *temp = C_CAST(uint8_t*, realloc(modeBuffer, modeLength));
                if (!temp)
                {
                    return MEMORY_FAILURE;
                }
                modeBuffer = temp;
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
            FILE *fpmp = NULL;
            bool fileOpened = false;
            if (used6ByteCmd)
            {
                *used6ByteCmd = true;
            }
            if (!toBuffer && !fileOpened && ret != FAILURE)
            {
                char *fileNameUsed = NULL;
                if (SUCCESS == create_And_Open_Log_File(device, &fpmp, filePath, logName, fileExtension, NAMING_SERIAL_NUMBER_DATE_TIME, &fileNameUsed))
                {
                    fileOpened = true;
                }
            }
            if (fileOpened && ret != FAILURE)
            {
                //write the vpd page to a file
                if ((fwrite(modeBuffer, sizeof(uint8_t), modeLength, fpmp) != modeLength) || ferror(fpmp))
                {
                    if (VERBOSITY_QUIET < device->deviceVerbosity)
                    {
                        perror("Error writing vpd data to a file!\n");
                    }
                    fclose(fpmp);
                    fileOpened = false;
                    safe_Free_aligned(modeBuffer)
                    return ERROR_WRITING_FILE;
                }
            }
            if (toBuffer && ret != FAILURE)
            {
                if (bufSize >= modeLength)
                {
                    memcpy(myBuf, modeBuffer, modeLength);
                }
                else
                {
                    return BAD_PARAMETER;
                }
            }
            if (fileOpened)
            {
                if (fflush(fpmp) != 0 || ferror(fpmp))
                {
                    if (VERBOSITY_QUIET < device->deviceVerbosity)
                    {
                        perror("Error flushing data!\n");
                    }
                    fclose(fpmp);
                    fileOpened = false;
                    safe_Free_aligned(modeBuffer)
                    return ERROR_WRITING_FILE;
                }
                fclose(fpmp);
                fileOpened = false;
            }
        }
        else
        {
            ret = FAILURE;
        }
    }
    safe_Free_aligned(modeBuffer)
    return ret;
}

bool is_SCSI_Read_Buffer_16_Supported(tDevice *device)
{
    bool supported = false;
    uint8_t reportSupportedOperationCode[20] = { 0 };
    if (SUCCESS == scsi_Report_Supported_Operation_Codes(device, false, REPORT_OPERATION_CODE, READ_BUFFER_16_CMD, 0, 20, reportSupportedOperationCode))
    {
        if (M_GETBITRANGE(reportSupportedOperationCode[1], 2, 0) == 3)//matches the spec
        {
            supported = true;
        }
    }
    return supported;
}

int get_SCSI_Error_History_Size(tDevice *device, uint8_t bufferID, uint32_t *errorHistorySize, bool createNewSnapshot, bool useReadBuffer16)
{
    int ret = NOT_SUPPORTED;
    if (!errorHistorySize)
    {
        return BAD_PARAMETER;
    }
    uint8_t *errorHistoryDirectory = C_CAST(uint8_t*, calloc_aligned(2088, sizeof(uint8_t), device->os_info.minimumAlignment));
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
        if (SUCCESS == scsi_Read_Buffer_16(device, 0x1C, 0, directoryID, 0, 2088, errorHistoryDirectory))
        {
            gotData = true;
        }
    }
    else
    {
        if (SUCCESS == scsi_Read_Buffer(device, 0x1C, directoryID, 0, 2088, errorHistoryDirectory))
        {
            gotData = true;
        }
    }
    if (gotData)
    {
        uint16_t directoryLength = M_BytesTo2ByteValue(errorHistoryDirectory[30], errorHistoryDirectory[31]);
        for (uint32_t directoryIter = 32; directoryIter < C_CAST(uint32_t, directoryLength + 32U); directoryIter += 8)
        {
            if (errorHistoryDirectory[directoryIter + 0] == bufferID)
            {
                *errorHistorySize = M_BytesTo4ByteValue(errorHistoryDirectory[directoryIter + 4], errorHistoryDirectory[directoryIter + 5], errorHistoryDirectory[directoryIter + 6], errorHistoryDirectory[directoryIter + 7]);
                ret = SUCCESS;
                break;
            }
        }
    }
    safe_Free_aligned(errorHistoryDirectory)
    return ret;
}

int get_SCSI_Error_History(tDevice *device, uint8_t bufferID, char *logName, bool createNewSnapshot, bool useReadBuffer16, \
    char *fileExtension, bool toBuffer, uint8_t *myBuf, uint32_t bufSize, \
    const char * const filePath, uint32_t transferSizeBytes, char *fileNameUsed, uint32_t delayTime)
{
    int ret = UNKNOWN;
    uint32_t historyLen = 0;
    char name[OPENSEA_PATH_MAX] = { 0 };
    FILE *fp_History = NULL;
    uint8_t *historyBuffer = NULL;
    if (!fileNameUsed)
    {
        fileNameUsed = &name[0];
    }
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
        historyBuffer = C_CAST(uint8_t *, calloc_aligned(increment, sizeof(uint8_t), device->os_info.minimumAlignment));

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
        //TODO: if size of error history is greater than the maximum possible offset for the read buffer command, then need to return an error for truncated data.
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
                    memcpy(&myBuf[offset], historyBuffer, increment);
                }
                if (logName && fileExtension) //Because you can also get a log file & get it in buffer. 
                {
                    if (!logFileOpened)
                    {
                        if (SUCCESS == create_And_Open_Log_File(device, &fp_History, filePath, logName, fileExtension, NAMING_SERIAL_NUMBER_DATE_TIME, &fileNameUsed))
                        {
                            logFileOpened = true;
                        }
                    }
                    if (logFileOpened)
                    {
                        //write the history data to a file
                        if ((fwrite(historyBuffer, sizeof(uint8_t), increment, fp_History) != increment) || ferror(fp_History))
                        {
                            if (VERBOSITY_QUIET < device->deviceVerbosity)
                            {
                                perror("Error writing the history data to a file!\n");
                            }
                            fclose(fp_History);
                            logFileOpened = false;
                            safe_Free_aligned(historyBuffer)
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
            if (delayTime)
            {
                delay_Milliseconds(delayTime);
            }     
        }
        if (logFileOpened && fp_History)
        {
            if (fflush(fp_History) != 0 || ferror(fp_History))
            {
                if (VERBOSITY_QUIET < device->deviceVerbosity)
                {
                    perror("Error flushing data!\n");
                }
                fclose(fp_History);
                logFileOpened = false;
                safe_Free_aligned(historyBuffer)
                return ERROR_WRITING_FILE;
            }
            fclose(fp_History);
        }
        safe_Free_aligned(historyBuffer)
    }
    return ret;
}

int get_SMART_Extended_Comprehensive_Error_Log(tDevice *device, const char * const filePath)
{
    if (device->drive_info.drive_type == ATA_DRIVE)
    {
        return get_ATA_Log(device, ATA_LOG_EXTENDED_COMPREHENSIVE_SMART_ERROR_LOG, "SMART_Ext_Comp_Error_Log", "bin", true, false, false, NULL, 0, filePath, 0,0,0);
    }
    else
    {
        return NOT_SUPPORTED;
    }
}

int get_ATA_DST_Log(tDevice *device, bool extLog, const char * const filePath)
{
    if (extLog)
    {
        //read from GPL
        return get_ATA_Log(device, ATA_LOG_EXTENDED_SMART_SELF_TEST_LOG, "Ext_SMART_Self_Test_Results", "bin", true, false, false, NULL, 0, filePath, 0,0,0);
    }
    else
    {
        //read from SMART
        return get_ATA_Log(device, ATA_LOG_SMART_SELF_TEST_LOG, "SMART_Self_Test_Results", "bin", false, true, false, NULL, 0, filePath, 0,0,0);
    }
}

int get_DST_Log(tDevice *device, const char * const filePath)
{
    if (device->drive_info.drive_type == ATA_DRIVE)
    {
        return get_ATA_DST_Log(device, device->drive_info.ata_Options.generalPurposeLoggingSupported, filePath);
    }
    else if (device->drive_info.drive_type == SCSI_DRIVE)
    {
        return get_SCSI_Log(device, LP_SELF_TEST_RESULTS, 0, "Self_Test_Results", "bin", false, NULL, 0, filePath);
    }
    else if (device->drive_info.drive_type == NVME_DRIVE) 
    {
        return pull_Supported_NVMe_Logs(device, 6, PULL_LOG_BIN_FILE_MODE);
    }
    else
    {
        return NOT_SUPPORTED;
    }
}

int get_Pending_Defect_List(tDevice *device, const char * const filePath)
{
    if (device->drive_info.drive_type == ATA_DRIVE)
    {
        //new is ACS4. Can be read with standard read log command if the drive supports the log.
        return get_ATA_Log(device, ATA_LOG_PENDING_DEFECTS_LOG, "Pending_Defects", "plst", true, false, false, NULL, 0, filePath, 0,0,0);
    }
    else if (device->drive_info.drive_type == SCSI_DRIVE)
    {
        //this is new in SBC4. We can read this with a logsense command. (if the drive supports it)
        return get_SCSI_Log(device, LP_PENDING_DEFECTS, 0x01, "Pending_Defects", "plst", false, NULL, 0, filePath);
    }
    else
    {
        return NOT_SUPPORTED;
    }
}

int get_Identify_Device_Data_Log(tDevice *device, const char * const filePath)
{
    if (device->drive_info.drive_type == ATA_DRIVE)
    {
        return get_ATA_Log(device, ATA_LOG_IDENTIFY_DEVICE_DATA, "Identify_Device_Data_Log", "bin", true, true, false, NULL, 0, filePath, 0,0,0);
    }
    else
    {
        return NOT_SUPPORTED;
    }
}

int get_SATA_Phy_Event_Counters_Log(tDevice *device, const char * const filePath)
{
    if (device->drive_info.drive_type == ATA_DRIVE)
    {
        return get_ATA_Log(device, ATA_LOG_SATA_PHY_EVENT_COUNTERS_LOG, "SATA_Phy_Event_Counters", "bin", true, false, false, NULL, 0, filePath, 0,0,0);
    }
    else
    {
        return NOT_SUPPORTED;
    }
}

int get_Device_Statistics_Log(tDevice *device, const char * const filePath)
{
    if (device->drive_info.drive_type == ATA_DRIVE)
    {
        return get_ATA_Log(device, ATA_LOG_DEVICE_STATISTICS, "Device_Statistics", "bin", true, true, false, NULL,0, filePath, 0,0,0);
    }
    else if (device->drive_info.drive_type == SCSI_DRIVE)
    {
        return get_SCSI_Log(device, LP_GENERAL_STATISTICS_AND_PERFORMANCE, 0, "Device_Statistics", "bin", false, NULL, 0, filePath);
    }
    else
    {
        return NOT_SUPPORTED;
    }
}

//PowerCondition log
int get_EPC_log(tDevice *device, const char * const filePath)
{
    int ret = FAILURE;
    if (device->drive_info.drive_type == ATA_DRIVE)
    {
        //old code was reading address 0x12, however the ACS3 spec says 0x12 is the NCQ Queue Management log and 0x08 is the Power Conditions log
        ret = get_ATA_Log(device, ATA_LOG_POWER_CONDITIONS, "EPC", "EPC", true, false, false, NULL, 0, filePath, LEGACY_DRIVE_SEC_SIZE * 2,0,0);//sending in an override to read both pages in one command - TJE
    }
    else if (device->drive_info.drive_type == SCSI_DRIVE)
    {
        ret = get_SCSI_VPD(device, POWER_CONDITION, "EPC", "EPC", false, NULL, 0, filePath);
    }
    else
    {
        ret = NOT_SUPPORTED;
    }
    return ret;
}

int pull_SCSI_G_List(tDevice *device, const char * const filePath)
{
    int ret = UNKNOWN;
    uint32_t addressDescriptorIndex = 0;
    uint32_t defectDataSize = 8;//set to size of defect data without any address descriptors so we know how much we will be pulling
    uint8_t *defectData = C_CAST(uint8_t*, calloc_aligned(defectDataSize, sizeof(uint8_t), device->os_info.minimumAlignment));
    if (!defectData)
    {
        return MEMORY_FAILURE;
    }
    ret = scsi_Read_Defect_Data_12(device, false, true, AD_LONG_BLOCK_FORMAT_ADDRESS_DESCRIPTOR, addressDescriptorIndex, defectDataSize, defectData);
    if (ret == SUCCESS)
    {
        FILE *gListData = NULL;
        char* fileNameUsed = NULL;
        bool fileOpened = false;
        uint32_t defectListLength = M_BytesTo4ByteValue(defectData[4], defectData[5], defectData[6], defectData[7]);
        //each address descriptor is 8 bytes in size
        defectDataSize = 4096;//pull 4096 at a time
        uint8_t *temp = C_CAST(uint8_t*, realloc(defectData, defectDataSize * sizeof(uint8_t)));
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
                    if (SUCCESS == create_And_Open_Log_File(device, &gListData, filePath, "GLIST", "bin", NAMING_SERIAL_NUMBER_DATE_TIME, &fileNameUsed))
                    {
                        fileOpened = true;
                    }
                }
                if (fileOpened)
                {
                    //write out to a file
                    if ((fwrite(defectData, sizeof(uint8_t), defectDataSize, gListData) != defectDataSize) || ferror(gListData))
                    {
                        if (VERBOSITY_QUIET < device->deviceVerbosity)
                        {
                            perror("Error writing the defect data to a file!\n");
                        }
                        fclose(gListData);
                        fileOpened = false;
                        safe_Free_aligned(defectData)
                        return ERROR_WRITING_FILE;
                    }
                }
                if (fileOpened)
                {
                    if (fflush(gListData) != 0 || ferror(gListData))
                    {
                        if (VERBOSITY_QUIET < device->deviceVerbosity)
                        {
                            perror("Error flushing data!\n");
                        }
                        fclose(gListData);
                        fileOpened = false;
                        safe_Free_aligned(defectData)
                        return ERROR_WRITING_FILE;
                    }
                    fclose(gListData);
                }
            }
        }
    }
    safe_Free_aligned(defectData)
    return ret;
}

int pull_SCSI_Informational_Exceptions_Log(tDevice *device, const char * const filePath)
{
    if (device->drive_info.drive_type == SCSI_DRIVE)
    {
        return get_SCSI_Log(device, LP_INFORMATION_EXCEPTIONS, 0, "Informational_Exceptions", "bin", false, NULL, 0, filePath);
    }
    else
    {
        return NOT_SUPPORTED;
    }
}

int get_ATA_Log(tDevice *device, uint8_t logAddress, char *logName, char *fileExtension, bool GPL,\
    bool SMART, bool toBuffer, uint8_t *myBuf, uint32_t bufSize, const char * const filePath, \
    uint32_t transferSizeBytes, uint16_t featureRegister, uint32_t delayTime)
{
    int ret = UNKNOWN;
    uint32_t logSize = 0;

    #ifdef _DEBUG
    printf("%s: -->\n",__FUNCTION__);
    #endif

    if (transferSizeBytes % LEGACY_DRIVE_SEC_SIZE)
    {
        return BAD_PARAMETER;
    }
          
    ret = get_ATA_Log_Size(device, logAddress, &logSize, GPL, SMART);
    if (ret == SUCCESS)
    {
        bool logFromGPL = false;
        bool fileOpened = false;
        FILE *fp_log = NULL;
        uint8_t *logBuffer = C_CAST(uint8_t *, calloc_aligned(logSize, sizeof(uint8_t), device->os_info.minimumAlignment));
        if (!logBuffer)
        {
            perror("Calloc Failure!\n");
            return MEMORY_FAILURE;
        }

        if (GPL)
        {
            char *fileNameUsed = NULL;
            //read each log 1 page at a time since some can get to be so large some controllers won't let you pull it.
            uint16_t pagesToReadAtATime = 1;
            uint16_t numberOfLogPages = C_CAST(uint16_t, logSize / LEGACY_DRIVE_SEC_SIZE);
            uint16_t remainderPages = 0;
            uint16_t currentPage = 0;
            switch (logAddress)
            {
            case 0xA2:
                if (is_Seagate_Family(device) == SEAGATE)
                {
                    //this log needs to be read 16 pages at a time (upped from 8 to 16 for ST10000NM*...)
                    pagesToReadAtATime = 16;
                    break;
                }
                M_FALLTHROUGH
            default:
                if (device->drive_info.interface_type != USB_INTERFACE && device->drive_info.interface_type != IEEE_1394_INTERFACE)
                {
                    if (numberOfLogPages >= UINT16_C(32))
                    {
                        pagesToReadAtATime = C_CAST(uint16_t, (M_Min(UINT16_C(32), logSize / LEGACY_DRIVE_SEC_SIZE)));//16k at a time should be a little faster...especially on larger logs
                    }
                    else
                    {
                        pagesToReadAtATime = C_CAST(uint16_t, (logSize / LEGACY_DRIVE_SEC_SIZE));
                    }
                }
                else
                {
                    //USB and IEEE 1394 should only ever be read 1 page at a time since these interfaces use cheap bridge chips that typically don't allow larger transfers.
                    pagesToReadAtATime = 1;
                }
                break;
            }
            if (transferSizeBytes)
            {
                //caller is telling us how much to read at a time...so let them.
                pagesToReadAtATime = C_CAST(uint16_t, (transferSizeBytes / LEGACY_DRIVE_SEC_SIZE));
            }
            logFromGPL = true;
            remainderPages = numberOfLogPages % pagesToReadAtATime;
            for (currentPage = 0; currentPage < (numberOfLogPages - remainderPages); currentPage += pagesToReadAtATime)
            {
                ret = SUCCESS;//assume success
                //loop and read each page or set of pages, then save to a file
                if (SUCCESS == send_ATA_Read_Log_Ext_Cmd(device, logAddress, currentPage, &logBuffer[currentPage * LEGACY_DRIVE_SEC_SIZE], pagesToReadAtATime * LEGACY_DRIVE_SEC_SIZE, featureRegister))
                {
                    if (device->deviceVerbosity > VERBOSITY_QUIET)
                    {
                        if (currentPage % 20 == 0)
                        {
                            printf(".");
                            fflush(stdout);
                        }
                    }
                    if (!toBuffer && !fileOpened)
                    {
                        if (SUCCESS == create_And_Open_Log_File(device, &fp_log, filePath, logName, fileExtension, NAMING_SERIAL_NUMBER_DATE_TIME, &fileNameUsed))
                        {
                            fileOpened = true;
                        }
                    }
                    if (fileOpened)
                    {
                        //write out to a file
                        if ((fwrite(&logBuffer[currentPage * LEGACY_DRIVE_SEC_SIZE], sizeof(uint8_t), pagesToReadAtATime * LEGACY_DRIVE_SEC_SIZE, fp_log) != (size_t)(pagesToReadAtATime * LEGACY_DRIVE_SEC_SIZE)) || ferror(fp_log))
                        {
                            if (VERBOSITY_QUIET < device->deviceVerbosity)
                            {
                                perror("Error writing a file!\n");
                            }
                            fclose(fp_log);
                            fileOpened = false;
                            safe_Free_aligned(logBuffer)
                            return ERROR_WRITING_FILE;
                        }
                        ret = SUCCESS;
                    }
                    if (toBuffer)
                    {
                        if (bufSize >= logSize)
                        {
                            memcpy(&myBuf[currentPage * LEGACY_DRIVE_SEC_SIZE], &logBuffer[currentPage * LEGACY_DRIVE_SEC_SIZE], pagesToReadAtATime * LEGACY_DRIVE_SEC_SIZE);
                        }
                        else
                        {
                            return BAD_PARAMETER;
                        }
                    }
                }
                else
                {
                    ret = FAILURE;
                    logSize = 0;
                    logFromGPL = true;
                    break;
                }
            }
            if (remainderPages > 0 && ret == SUCCESS)
            {
                //read the remaining chunk of pages at once.
                if (SUCCESS == send_ATA_Read_Log_Ext_Cmd(device, logAddress, currentPage, &logBuffer[currentPage * LEGACY_DRIVE_SEC_SIZE], remainderPages * LEGACY_DRIVE_SEC_SIZE, 0))
                {
                    if (!toBuffer && !fileOpened)
                    {
                        if (SUCCESS == create_And_Open_Log_File(device, &fp_log, filePath, logName, fileExtension, NAMING_SERIAL_NUMBER_DATE_TIME, &fileNameUsed))
                        {
                            fileOpened = true;
                        }
                    }
                    if (fileOpened)
                    {
                        //write out to a file
                        if ((fwrite(&logBuffer[currentPage * LEGACY_DRIVE_SEC_SIZE], sizeof(uint8_t), remainderPages * LEGACY_DRIVE_SEC_SIZE, fp_log) != (size_t)(remainderPages * LEGACY_DRIVE_SEC_SIZE)) || ferror(fp_log))
                        {
                            if (VERBOSITY_QUIET < device->deviceVerbosity)
                            {
                                perror("Error writing a file!\n");
                            }
                            fclose(fp_log);
                            fileOpened = false;
                            safe_Free_aligned(logBuffer)
                            return ERROR_WRITING_FILE;
                        }
                        ret = SUCCESS;
                    }
                    if (toBuffer)
                    {
                        if (bufSize >= logSize)
                        {
                            memcpy(&myBuf[currentPage * LEGACY_DRIVE_SEC_SIZE], &logBuffer[currentPage * LEGACY_DRIVE_SEC_SIZE], remainderPages * LEGACY_DRIVE_SEC_SIZE);
                        }
                        else
                        {
                            return BAD_PARAMETER;
                        }
                    }
                }
                else
                {
                    ret = FAILURE;
                    logSize = 0;
                    logFromGPL = true;
                }
            }
            if (device->deviceVerbosity > VERBOSITY_QUIET)
            {
                printf("\n");
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
                    char *fileNameUsed = NULL;
                    if (SUCCESS == create_And_Open_Log_File(device, &fp_log, filePath, logName, fileExtension, NAMING_SERIAL_NUMBER_DATE_TIME, &fileNameUsed))
                    {
                        fileOpened = true;
                    }
                }
                if (fileOpened)
                {
                    //write out to a file
                    if ((fwrite(logBuffer, sizeof(uint8_t), logSize, fp_log) != logSize) || ferror(fp_log))
                    {
                        if (VERBOSITY_QUIET < device->deviceVerbosity)
                        {
                            perror("Error writing a file!\n");
                        }
                        fclose(fp_log);
                        fileOpened = false;
                        safe_Free_aligned(logBuffer)
                        return ERROR_WRITING_FILE;
                    }
                    ret = SUCCESS;
                }
                if (toBuffer)
                {
                    if (bufSize >= logSize)
                    {
                        memcpy(myBuf, logBuffer, logSize);
                    }
                    else
                    {
                        return BAD_PARAMETER;
                    }
                }
            }
            else
            {
                //failed to read the log...
                ret = FAILURE;
            }
        }
        if (delayTime)
        {
            delay_Milliseconds(delayTime);
        }
        if (fileOpened)
        {
            if (fflush(fp_log) != 0 || ferror(fp_log))
            {
                if (VERBOSITY_QUIET < device->deviceVerbosity)
                {
                    perror("Error flushing data!\n");
                }
                fclose(fp_log);
                fileOpened = false;
                safe_Free_aligned(logBuffer)
                return ERROR_WRITING_FILE;
            }
            fclose(fp_log);
            fileOpened = false;
        }
        safe_Free_aligned(logBuffer)
    }

    #ifdef _DEBUG
    printf("%s: <--\n",__FUNCTION__);
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
//!   \param[in]  logName - a string that is the name of the log (NO SPACES please!) NULL if no file output needed
//!   \param[in]  fileExtension - a string for the file extension. You do not need to include a dot character.
//!   \param[in]  toBuffer - boolean flag specifying if you want to return data in buffer 
//!   \param[in]  myBuf - buffer to return data in if toBuffer is true
//!   \param[in]  bufSize - size of the buffer to get data filled into it (use get_SCSI_Log_Size)
//!   \param[in]  filePath - string with path to output the file to. Can be NULL for current directory.
//!
//  Exit:
//!   \return SUCCESS = good, !SUCCESS something went wrong see error codes
//
//-----------------------------------------------------------------------------

int get_SCSI_Log(tDevice *device, uint8_t logAddress, uint8_t subpage, char *logName, \
                 char *fileExtension, bool toBuffer, uint8_t *myBuf, uint32_t bufSize,\
                 const char * const filePath)
{
    int ret = UNKNOWN;
    uint32_t pageLen = 0;
    char name[OPENSEA_PATH_MAX]; 
    FILE *fp_log = NULL;
    uint8_t *logBuffer = NULL;
    char *fileNameUsed = &name[0];
    
    ret = get_SCSI_Log_Size(device, logAddress, subpage, &pageLen);
    
    if (ret == SUCCESS)
    {
        //If the user wants it in a buffer...just return. 
        if ( (toBuffer) && (bufSize < pageLen) )
            return BAD_PARAMETER;
        
        //TODO: Improve this since if caller has already has enough memory, no need to allocate this. 
        logBuffer = C_CAST(uint8_t *, calloc_aligned(pageLen, sizeof(uint8_t), device->os_info.minimumAlignment));
        
        if (!logBuffer)
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
            memset(&name[0], 0, OPENSEA_PATH_MAX);
            if (logName && fileExtension) //Because you can also get a log file & get it in buffer. 
            {
                if (SUCCESS == create_And_Open_Log_File(device, &fp_log, filePath, logName, fileExtension, NAMING_SERIAL_NUMBER_DATE_TIME, &fileNameUsed))
                {
                    //write the log to a file
                    if ((fwrite(logBuffer, sizeof(uint8_t), M_Min(pageLen, returnedPageLength), fp_log) != M_Min(pageLen, returnedPageLength)) || ferror(fp_log))
                    {
                        if (VERBOSITY_QUIET < device->deviceVerbosity)
                        {
                            perror("Error writing to a file!\n");
                        }
                        fclose(fp_log);
                        safe_Free_aligned(logBuffer)
                        return ERROR_WRITING_FILE;
                    }
                    if ((fflush(fp_log) != 0) || ferror(fp_log))
                    {
                        if (VERBOSITY_QUIET < device->deviceVerbosity)
                        {
                            perror("Error flushing data!\n");
                        }
                        fclose(fp_log);
                        safe_Free_aligned(logBuffer)
                        return ERROR_WRITING_FILE;
                    }
                    fclose(fp_log);
                }
            }
            if (toBuffer) //NOTE: the buffer size checked earlier. 
            {
                memcpy(myBuf, logBuffer, M_Min(pageLen, returnedPageLength));
            }
        }
        else
        {
            ret = FAILURE;
        }
        safe_Free_aligned(logBuffer)
    }
    return ret;
}

int get_SCSI_VPD(tDevice *device, uint8_t pageCode, char *logName, char *fileExtension, bool toBuffer, uint8_t *myBuf, uint32_t bufSize, const char * const filePath)
{
    int     ret = UNKNOWN;
    uint32_t vpdBufferLength = 0;
    ret = get_SCSI_VPD_Page_Size(device, pageCode, &vpdBufferLength);
    if (ret == SUCCESS)
    {
        FILE *fp_vpd = NULL;
        uint8_t *vpdBuffer = C_CAST(uint8_t *, calloc_aligned(vpdBufferLength, sizeof(uint8_t), device->os_info.minimumAlignment));
        bool fileOpened = false;
        if (!vpdBuffer)
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
                char *fileNameUsed = NULL;
                if (SUCCESS == create_And_Open_Log_File(device, &fp_vpd, filePath, logName, fileExtension, NAMING_SERIAL_NUMBER_DATE_TIME, &fileNameUsed))
                {
                    fileOpened = true;
                }
            }
            if (fileOpened && ret != FAILURE)
            {
                //write the vpd page to a file
                if ((fwrite(vpdBuffer, sizeof(uint8_t), vpdBufferLength, fp_vpd) != vpdBufferLength) || ferror(fp_vpd))
                {
                    if (VERBOSITY_QUIET < device->deviceVerbosity)
                    {
                        perror("Error writing vpd page to a file!\n");
                    }
                    fclose(fp_vpd);
                    fileOpened = false;
                    safe_Free_aligned(vpdBuffer)
                    return ERROR_WRITING_FILE;
                }
            }
            if (toBuffer && ret != FAILURE)
            {
                if (bufSize >= vpdBufferLength)
                {
                    memcpy(myBuf, vpdBuffer, vpdBufferLength);
                }
                else
                {
                    return BAD_PARAMETER;
                }
            }
        }
        if (fileOpened)
        {
            if ((fflush(fp_vpd) != 0) || ferror(fp_vpd))
            {
                if (VERBOSITY_QUIET < device->deviceVerbosity)
                {
                    perror("Error flushing data!\n");
                }
                fclose(fp_vpd);
                fileOpened = false;
                safe_Free_aligned(vpdBuffer)
                return ERROR_WRITING_FILE;
            }
            fclose(fp_vpd);
            fileOpened = false;
        }
        safe_Free_aligned(vpdBuffer)
    }
    return ret;
}

static int ata_Pull_Telemetry_Log(tDevice *device, bool currentOrSaved, uint8_t islDataSet,\
                             bool saveToFile, uint8_t* ptrData, uint32_t dataSize,\
                            const char * const filePath, uint32_t transferSizeBytes)
{
    int ret = SUCCESS;
    char fileName[OPENSEA_PATH_MAX] = {0};
    char * fileNameUsed = &fileName[0];
    FILE *isl = NULL;
    if (transferSizeBytes % LEGACY_DRIVE_SEC_SIZE)
    {
        return BAD_PARAMETER;
    }
    uint8_t *dataBuffer = C_CAST(uint8_t*, calloc_aligned(LEGACY_DRIVE_SEC_SIZE, sizeof(uint8_t), device->os_info.minimumAlignment));
    if (dataBuffer == NULL)
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
            if (saveToFile == true)
            {
                if (SUCCESS == create_And_Open_Log_File(device, &isl, filePath, "TELEMETRY", "bin", NAMING_SERIAL_NUMBER_DATE_TIME, &fileNameUsed))
                {
                    //fileOpened = true;
                    if (VERBOSITY_QUIET < device->deviceVerbosity)
                    {
                        printf("Saving telemetry log to file %s\n", fileNameUsed);
                    }
                }
                else
                {
                    ret = FILE_OPEN_ERROR;
                    safe_Free_aligned(dataBuffer)
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
                uint32_t pullChunkSize = 8 * LEGACY_DRIVE_SEC_SIZE;//pull the remainder of the log in 4k chunks
                if (transferSizeBytes)
                {
                    pullChunkSize = transferSizeBytes;
                }
                uint8_t *temp = NULL;
                //saving first page to file
                if (saveToFile == true)
                {
                    if ((fwrite(dataBuffer, LEGACY_DRIVE_SEC_SIZE, 1, isl) != 1) || ferror(isl))
                    {
                        if (VERBOSITY_QUIET < device->deviceVerbosity)
                        {
                            perror("Error writing first page to a file!\n");
                        }
                        fclose(isl);
                        safe_Free_aligned(dataBuffer)
                        return ERROR_WRITING_FILE;
                    }
                    if ((fflush(isl) != 0) || ferror(isl))
                    {
                        if (VERBOSITY_QUIET < device->deviceVerbosity)
                        {
                            perror("Error flushing data!\n");
                        }
                        fclose(isl);
                        safe_Free_aligned(dataBuffer)
                        return ERROR_WRITING_FILE;
                    }
                }
                else if (dataSize >= C_CAST(uint32_t, pageNumber * LEGACY_DRIVE_SEC_SIZE) && ptrData != NULL)
                {
                    memcpy(&ptrData[0], dataBuffer, LEGACY_DRIVE_SEC_SIZE);
                }
                else
                {
                    safe_Free_aligned(dataBuffer)
                    return BAD_PARAMETER;
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
                    M_FALLTHROUGH
                case 2://medium
                    islPullingSize = reportedMediumSize;
                    if (islPullingSize > 0)
                    {
                        break;
                    }
                    M_FALLTHROUGH
                case 1://small
                    M_FALLTHROUGH
                default:
                    islPullingSize = reportedSmallSize;
                    break;                    
                }
                //increment pageNumber to 1 and reallocate the local data buffer
                pageNumber += 1;
                temp = C_CAST(uint8_t*, realloc_aligned(dataBuffer, 512, pullChunkSize, device->os_info.minimumAlignment));
                if (temp == NULL)
                {
                    safe_Free_aligned(dataBuffer)
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
                        pullChunkSize = (islPullingSize - pageNumber) * LEGACY_DRIVE_SEC_SIZE;
                    }
                    //read each remaining chunk with the trigger bit set to 0
                    if (SUCCESS == send_ATA_Read_Log_Ext_Cmd(device, islLogToPull, pageNumber, dataBuffer, pullChunkSize, 0))
                    {
                        //save to file, or copy to the ptr we were given
                        if (saveToFile == true)
                        {
                            if ((fwrite(dataBuffer, pullChunkSize, 1, isl) != 1) || ferror(isl))
                            {
                                if (VERBOSITY_QUIET < device->deviceVerbosity)
                                {
                                    perror("Error writing to a file!\n");
                                }
                                fclose(isl);
                                safe_Free_aligned(dataBuffer)
                                return ERROR_WRITING_FILE;
                            }
                            if ((fflush(isl) != 0) || ferror(isl))
                            {
                                if (VERBOSITY_QUIET < device->deviceVerbosity)
                                {
                                    perror("Error flushing data!\n");
                                }
                                fclose(isl);
                                safe_Free_aligned(dataBuffer)
                                return ERROR_WRITING_FILE;
                            }
                        }
                        else if (dataSize >= C_CAST(uint32_t, pageNumber * LEGACY_DRIVE_SEC_SIZE) && ptrData != NULL)
                        {
                            memcpy(&ptrData[pageNumber * LEGACY_DRIVE_SEC_SIZE], dataBuffer, pullChunkSize);
                        }
                        else
                        {
                            safe_Free_aligned(dataBuffer)
                            return BAD_PARAMETER;
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
                if (saveToFile == true)
                {
                    if ((fflush(isl) != 0) || ferror(isl))
                    {
                        if (VERBOSITY_QUIET < device->deviceVerbosity)
                        {
                            perror("Error flushing data!\n");
                        }
                        fclose(isl);
                        return ERROR_WRITING_FILE;
                    }
                    fclose(isl);
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
    safe_Free_aligned(dataBuffer)
    return ret;
}

static int scsi_Pull_Telemetry_Log(tDevice *device, bool currentOrSaved, uint8_t islDataSet,\
                              bool saveToFile, uint8_t* ptrData, uint32_t dataSize,\
                              const char * const filePath, uint32_t transferSizeBytes)
{
    int ret = SUCCESS;
    FILE *isl = NULL;
    char fileName[OPENSEA_PATH_MAX] = {0};
    char * fileNameUsed = &fileName[0];
    uint8_t islLogToPull = 0xFF;
    if (transferSizeBytes % LEGACY_DRIVE_SEC_SIZE)
    {
        //NOTE: We may be able to pull this in any size, but for now and for compatibility only allow 512B sizes.
        return BAD_PARAMETER;
    }
    uint8_t *dataBuffer = C_CAST(uint8_t*, calloc_aligned(LEGACY_DRIVE_SEC_SIZE, sizeof(uint8_t), device->os_info.minimumAlignment));

    #ifdef _DEBUG
    printf("--> %s\n",__FUNCTION__);
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
                uint8_t *temp = NULL;
                if (saveToFile == true)
                {
                    if (SUCCESS == create_And_Open_Log_File(device, &isl, filePath, "TELEMETRY", "bin", NAMING_SERIAL_NUMBER_DATE_TIME, &fileNameUsed))
                    {
                        if (VERBOSITY_QUIET < device->deviceVerbosity)
                        {
                            printf("Saving to file %s\n", fileNameUsed);
                        }
                        if ((fwrite(dataBuffer, LEGACY_DRIVE_SEC_SIZE, 1, isl) != 1) || ferror(isl))
                        {
                            if (VERBOSITY_QUIET < device->deviceVerbosity)
                            {
                                perror("Error writing to file!\n");
                            }
                            fclose(isl);
                            safe_Free_aligned(dataBuffer)
                            return ERROR_WRITING_FILE;
                        }
                        if ((fflush(isl) != 0) || ferror(isl))
                        {
                            if (VERBOSITY_QUIET < device->deviceVerbosity)
                            {
                                perror("Error flushing data!\n");
                            }
                            fclose(isl);
                            safe_Free_aligned(dataBuffer)
                            return ERROR_WRITING_FILE;
                        }
                    }
                    else
                    {
                        ret = FILE_OPEN_ERROR;
                        safe_Free_aligned(dataBuffer)
                        return ret;
                    }
                }
                else if (dataSize >= C_CAST(uint32_t, pageNumber * LEGACY_DRIVE_SEC_SIZE) && ptrData != NULL)
                {
                    memcpy(&ptrData[0], dataBuffer, LEGACY_DRIVE_SEC_SIZE);
                }
                else
                {
                    safe_Free_aligned(dataBuffer)
                    return BAD_PARAMETER;
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
                    M_FALLTHROUGH
                case 3://large
                    islPullingSize = reportedLargeSize;
                    if (islPullingSize > 0)
                    {
                        break;
                    }
                    M_FALLTHROUGH
                case 2://medium
                    islPullingSize = reportedMediumSize;
                    if (islPullingSize > 0)
                    {
                        break;
                    }
                    M_FALLTHROUGH
                case 1://small
                    M_FALLTHROUGH
                default:
                    islPullingSize = reportedSmallSize;
                    break;
                }
                //increment pageNumber to 1 and reallocate the local data buffer
                pageNumber += 1;
                temp = C_CAST(uint8_t*, realloc_aligned(dataBuffer, 512, pullChunkSize, device->os_info.minimumAlignment));
                if (temp == NULL)
                {
                    safe_Free_aligned(dataBuffer)
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
                        if (saveToFile == true)
                        {
                            if ((fwrite(dataBuffer, pullChunkSize, 1, isl) != 1) || ferror(isl))
                            {
                                if (VERBOSITY_QUIET < device->deviceVerbosity)
                                {
                                    perror("Error writing to file!\n");
                                }
                                fclose(isl);
                                safe_Free_aligned(dataBuffer)
                                return ERROR_WRITING_FILE;
                            }
                            if ((fflush(isl) != 0) || ferror(isl))
                            {
                                if (VERBOSITY_QUIET < device->deviceVerbosity)
                                {
                                    perror("Error flushing data!\n");
                                }
                                fclose(isl);
                                safe_Free_aligned(dataBuffer)
                                return ERROR_WRITING_FILE;
                            }
                        }
                        else if (dataSize >= C_CAST(uint32_t, pageNumber * pullChunkSize) && ptrData != NULL)
                        {
                            memcpy(&ptrData[pageNumber * LEGACY_DRIVE_SEC_SIZE], dataBuffer, pullChunkSize);
                        }
                        else
                        {
                            safe_Free_aligned(dataBuffer)
                            return BAD_PARAMETER;
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
                if (saveToFile == true)
                {
                    fflush(isl);
                    fclose(isl);
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
    safe_Free_aligned(dataBuffer)
    return ret;
}

static int nvme_Pull_Telemetry_Log(tDevice *device, bool currentOrSaved, uint8_t islDataSet, \
    bool saveToFile, uint8_t* ptrData, uint32_t dataSize, \
    const char * const filePath, uint32_t transferSizeBytes)
{
    int ret = SUCCESS;
    char fileName[OPENSEA_PATH_MAX] = { 0 };
    char * fileNameUsed = &fileName[0];
    FILE *isl = NULL;
    if (transferSizeBytes % LEGACY_DRIVE_SEC_SIZE)
    {
        return BAD_PARAMETER;
    }
    uint8_t *dataBuffer = C_CAST(uint8_t*, calloc_aligned(LEGACY_DRIVE_SEC_SIZE, sizeof(uint8_t), device->os_info.minimumAlignment));
    if (!dataBuffer)
    {
        perror("calloc failure");
        return MEMORY_FAILURE;
    }
    //check if the nvme telemetry log is supported in the identify data
    if (device->drive_info.IdentifyData.nvme.ctrl.lpa & BIT3)//If this bit is set, then BOTH host and controller initiated are supported
    {
        uint8_t islLogToPull = 0;
        if (currentOrSaved == true)
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
            if (saveToFile == true)
            {
                if (SUCCESS == create_And_Open_Log_File(device, &isl, filePath, "TELEMETRY", "bin", NAMING_SERIAL_NUMBER_DATE_TIME, &fileNameUsed))
                {
                    //fileOpened = true;
                    if (VERBOSITY_QUIET < device->deviceVerbosity)
                    {
                        printf("Saving Telemetry log to file %s\n", fileNameUsed);
                    }
                }
                else
                {
                    ret = FILE_OPEN_ERROR;
                    safe_Free_aligned(dataBuffer)
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
                uint8_t *temp = NULL;
                //saving first page to file
                if (saveToFile == true)
                {
                    if ((fwrite(dataBuffer, LEGACY_DRIVE_SEC_SIZE, 1, isl) != 1) || ferror(isl))
                    {
                        if (VERBOSITY_QUIET < device->deviceVerbosity)
                        {
                            perror("Error writing data to a file!\n");
                        }
                        fclose(isl);
                        safe_Free_aligned(dataBuffer)
                        return ERROR_WRITING_FILE;
                    }
                    if ((fflush(isl) != 0) || ferror(isl))
                    {
                        if (VERBOSITY_QUIET < device->deviceVerbosity)
                        {
                            perror("Error flushing data!\n");
                        }
                        fclose(isl);
                        safe_Free_aligned(dataBuffer)
                        return ERROR_WRITING_FILE;
                    }
                }
                else if (dataSize >= C_CAST(uint32_t, pageNumber * LEGACY_DRIVE_SEC_SIZE) && ptrData != NULL)
                {
                    memcpy(&ptrData[0], dataBuffer, LEGACY_DRIVE_SEC_SIZE);
                }
                else
                {
                    safe_Free_aligned(dataBuffer)
                    return BAD_PARAMETER;
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
                    M_FALLTHROUGH
                case 2://medium
                    islPullingSize = reportedMediumSize;
                    if (islPullingSize > 0)
                    {
                        break;
                    }
                    M_FALLTHROUGH
                case 1://small
                    M_FALLTHROUGH
                default:
                    islPullingSize = reportedSmallSize;
                    break;
                }
                //increment pageNumber to 1 and reallocate the local data buffer
                pageNumber += 1;
                temp = C_CAST(uint8_t*, realloc_aligned(dataBuffer, 512, pullChunkSize, device->os_info.minimumAlignment));
                if (temp == NULL)
                {
                    safe_Free_aligned(dataBuffer)
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
                        pullChunkSize = (islPullingSize - pageNumber) * LEGACY_DRIVE_SEC_SIZE;
                    }
                    //read each remaining chunk with the trigger bit set to 0
                    telemOpts.lsp = 0;
                    telemOpts.offset = pageNumber * 512;
                    if (SUCCESS == nvme_Get_Log_Page(device, &telemOpts))
                    {
                        //save to file, or copy to the ptr we were given
                        if (saveToFile == true)
                        {
                            if ((fwrite(dataBuffer, pullChunkSize, 1, isl) != 1) || ferror(isl))
                            {
                                if (VERBOSITY_QUIET < device->deviceVerbosity)
                                {
                                    perror("Error writing data to a file!\n");
                                }
                                fclose(isl);
                                safe_Free_aligned(dataBuffer)
                                return ERROR_WRITING_FILE;
                            }
                            if ((fflush(isl) != 0) || ferror(isl))
                            {
                                if (VERBOSITY_QUIET < device->deviceVerbosity)
                                {
                                    perror("Error flushing data!\n");
                                }
                                fclose(isl);
                                safe_Free_aligned(dataBuffer)
                                return ERROR_WRITING_FILE;
                            }
                        }
                        else if (dataSize >= C_CAST(uint32_t, pageNumber * LEGACY_DRIVE_SEC_SIZE) && ptrData != NULL)
                        {
                            memcpy(&ptrData[pageNumber * LEGACY_DRIVE_SEC_SIZE], dataBuffer, pullChunkSize);
                        }
                        else
                        {
                            safe_Free_aligned(dataBuffer)
                            return BAD_PARAMETER;
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
                if (saveToFile == true)
                {
                    if ((fflush(isl) != 0) || ferror(isl))
                    {
                        if (VERBOSITY_QUIET < device->deviceVerbosity)
                        {
                            perror("Error flushing data!\n");
                        }
                        fclose(isl);
                        return ERROR_WRITING_FILE;
                    }
                    fclose(isl);
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
    safe_Free_aligned(dataBuffer)
    return ret;
}

//TODO: extra bool to trigger or not trigger???
int pull_Telemetry_Log(tDevice *device, bool currentOrSaved, uint8_t islDataSet, bool saveToFile, uint8_t* ptrData, uint32_t dataSize, const char * const filePath, uint32_t transferSizeBytes)
{
    int ret = NOT_SUPPORTED;
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

int print_Supported_Logs(tDevice *device, uint64_t flags)
{
    int retStatus = NOT_SUPPORTED;

    switch(device->drive_info.drive_type)
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


int print_Supported_SCSI_Logs(tDevice *device, uint64_t flags)
{ 
    int retStatus = NOT_SUPPORTED;
    uint8_t *logBuffer = C_CAST(uint8_t*, calloc_aligned(LEGACY_DRIVE_SEC_SIZE, sizeof(uint8_t), device->os_info.minimumAlignment));
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
        uint16_t supportedPagesLength = M_BytesTo2ByteValue(logBuffer[2],logBuffer[3]);
        uint8_t incrementAmount = subpagesSupported ? 2 : 1;
        uint16_t pageLength = 0;//for each page in the supported buffer so we can report the size
        uint8_t logPage[4] = { 0 };
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
    safe_Free_aligned(logBuffer)
    return retStatus;
}

//NOTE: smartAccess & gplAccess are used to show how a log can be read based on drive reporting.
//      driveReportBug exists for noting that a drive is incorrectly reporting access for certain logs.
static void format_print_ata_logs_info(uint16_t log, uint32_t logSize, bool smartAccess, bool gplAccess, bool driveReportBug)
{
#define ATA_LOG_ACCESS_STRING_LENGTH 10
    char access[ATA_LOG_ACCESS_STRING_LENGTH] = { 0 };
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
//TODO: Add SMART vs GPL info to output to know which logs are accessible in which way. Part of this could come from the spec, others from code and parsing what the drive reported.
int print_Supported_ATA_Logs(tDevice *device, uint64_t flags)
{
    int retStatus = NOT_SUPPORTED;
    bool legacyDriveNoLogDir = false;
    uint8_t *gplLogBuffer = C_CAST(uint8_t*, calloc_aligned(LEGACY_DRIVE_SEC_SIZE, sizeof(uint8_t), device->os_info.minimumAlignment));
    uint8_t *smartLogBuffer = C_CAST(uint8_t*, calloc_aligned(LEGACY_DRIVE_SEC_SIZE, sizeof(uint8_t), device->os_info.minimumAlignment));
    M_USE_UNUSED(flags);
    if (smartLogBuffer)
    {
        if (is_SMART_Enabled(device) && is_SMART_Error_Logging_Supported(device))
        {
            retStatus = ata_SMART_Read_Log(device, ATA_LOG_DIRECTORY, smartLogBuffer, 512);
            if (retStatus != SUCCESS && retStatus != WARN_INVALID_CHECKSUM)
            {
                legacyDriveNoLogDir = true; //for old drives that do not support multi-sector logs we will rely on Identify/SMART data bits to generate the output
                safe_Free_aligned(smartLogBuffer)
            }
        }
        else
        {
            safe_Free_aligned(smartLogBuffer)
        }
    }
    if (gplLogBuffer)
    {
        if (device->drive_info.ata_Options.generalPurposeLoggingSupported)
        {
            if (SUCCESS != send_ATA_Read_Log_Ext_Cmd(device, ATA_LOG_DIRECTORY, 0, gplLogBuffer, LEGACY_DRIVE_SEC_SIZE, 0))
            {
                safe_Free_aligned(gplLogBuffer)
            }
        }
        else
        {
            safe_Free_aligned(gplLogBuffer)
        }
    }
    if (gplLogBuffer || smartLogBuffer || legacyDriveNoLogDir)
    {
        uint16_t log = 0;
        uint16_t gplLogSize = 0, smartLogSize = 0;
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
        safe_Free_aligned(smartLogBuffer)
        safe_Free_aligned(gplLogBuffer)
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
        retStatus =  NOT_SUPPORTED;
    }
    
    return retStatus;
}

int print_Supported_NVMe_Logs(tDevice *device, uint64_t flags)
{
    int retStatus = NOT_SUPPORTED;
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
        suptLogOpts.lid = 0xc5;
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
                    if (suptLogPage.logPageEntry[page].logPageID < 0xc0)
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
                    if (suptLogPage.logPageEntry[page].logPageID >= 0xc0)
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
        uint8_t* supportedLogsPage = C_CAST(uint8_t*, calloc_aligned(1024, sizeof(uint8_t), device->os_info.minimumAlignment));
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
                //TODO: LID specific?
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
            else if(!dummyFromIdentify)
            {
                //something went wrong, so fall back to dummying it up from identify bits
                dummyFromIdentify = true;
            }
            safe_Free_aligned(supportedLogsPage);
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

int pull_Supported_NVMe_Logs(tDevice *device, uint8_t logNum, eLogPullMode mode)
{
    //Since 0 is reserved log
    int retStatus=0;
    uint64_t size = 0;
    uint8_t * logBuffer = NULL;
    nvmeGetLogPageCmdOpts cmdOpts;
    if ((nvme_Get_Log_Size(logNum, &size) == SUCCESS) && size) {
        memset(&cmdOpts, 0, sizeof(nvmeGetLogPageCmdOpts));
        if (NVME_LOG_ERROR_ID == logNum)
        {
            size = 32 * size; //Get first 32 entries.
        }
        logBuffer = C_CAST(uint8_t *, calloc(C_CAST(size_t, size), sizeof(uint8_t)));
        if (logBuffer != NULL) {
            cmdOpts.nsid = NVME_ALL_NAMESPACES;
            cmdOpts.addr = logBuffer;
            cmdOpts.dataLen = C_CAST(uint32_t, size);
            cmdOpts.lid = logNum;
            if (nvme_Get_Log_Page(device, &cmdOpts) == SUCCESS) {
                if (mode == PULL_LOG_RAW_MODE)
                {
                    printf("Log Page %d Buffer:\n", logNum);
                    printf("================================\n");
                    print_Data_Buffer(C_CAST(uint8_t *, logBuffer), C_CAST(uint32_t, size), true);
                    printf("================================\n");
                }
                else if (mode == PULL_LOG_BIN_FILE_MODE) {
                    FILE * pLogFile = NULL;
                    char identifyFileName[OPENSEA_PATH_MAX] = { 0 };
                    char * fileNameUsed = &identifyFileName[0];
#define NVME_LOG_NAME_SIZE 16
                    char logName[NVME_LOG_NAME_SIZE];
                    snprintf(logName, NVME_LOG_NAME_SIZE, "LOG_PAGE_%d", logNum);
                    if (SUCCESS == create_And_Open_Log_File(device, &pLogFile, NULL, \
                        logName, "bin", 1, &fileNameUsed)) {
                        fwrite(logBuffer, sizeof(uint8_t), C_CAST(size_t, size), pLogFile);
                        fflush(pLogFile);
                        fclose(pLogFile);
                        if (VERBOSITY_QUIET < device->deviceVerbosity)
                        {
                            printf("Created %s with Log Page %" PRId32 " Information\n", fileNameUsed, logNum);
                        }
                    }
                    else {
                        retStatus = 3;
                    }
                }
                else {
                    retStatus = 3;
                }
            }
            else {
                retStatus = 3;
            }
            safe_Free(logBuffer)
        }
        else {
            retStatus = 3;
        }
    }
    else {
        retStatus = 4;
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
    


int print_Supported_SCSI_Error_History_Buffer_IDs(tDevice *device, uint64_t flags)
{
    int ret = NOT_SUPPORTED;
    uint32_t errorHistorySize = 2048;
    uint8_t *errorHistoryDirectory = C_CAST(uint8_t*, calloc_aligned(errorHistorySize, sizeof(uint8_t), device->os_info.minimumAlignment));
    M_USE_UNUSED(flags);
    bool rb16 = is_SCSI_Read_Buffer_16_Supported(device);
    if (errorHistoryDirectory)
    {
        if ((rb16 && SUCCESS == scsi_Read_Buffer_16(device, 0x1C, 0, 0, 0, errorHistorySize, errorHistoryDirectory)) ||  SUCCESS == scsi_Read_Buffer(device, 0x1C, 0, 0, errorHistorySize, errorHistoryDirectory))
        {
            ret = SUCCESS;
            char vendorIdentification[9] = { 0 };
            uint8_t version = errorHistoryDirectory[1];
            uint16_t directoryLength = M_BytesTo2ByteValue(errorHistoryDirectory[30], errorHistoryDirectory[31]);
            memcpy(vendorIdentification, errorHistoryDirectory, 8);
            if ((C_CAST(uint32_t, directoryLength) + (UINT32_C(32)) > errorHistorySize))
            {
                errorHistorySize = directoryLength + 32;
                //realloc and re-read
                uint8_t *temp = C_CAST(uint8_t*, realloc_aligned(errorHistoryDirectory, 2048, errorHistorySize, device->os_info.minimumAlignment));
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
                    safe_Free_aligned(errorHistoryDirectory)
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
                char dataFormatString[DATA_FORMAT_STRING_LENGTH] = { 0 };
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
    safe_Free_aligned(errorHistoryDirectory)
    return ret;
}

int pull_Generic_Log(tDevice *device, uint8_t logNum, uint8_t subpage, eLogPullMode mode, const char * const filePath, uint32_t transferSizeBytes, uint32_t delayTime)
{
    int retStatus = NOT_SUPPORTED;
    uint32_t logSize = 0;
    uint8_t *genericLogBuf = NULL;
#define GENERIC_LOG_FILE_NAME_LENGTH 20
#define LOG_NUMBER_POST_FIX_LENGTH 10
    char logFileName[GENERIC_LOG_FILE_NAME_LENGTH] = "GENERIC_LOG-";
    char logNumPostfix[LOG_NUMBER_POST_FIX_LENGTH] = { 0 };
    if (device->drive_info.drive_type == SCSI_DRIVE && subpage != 0)
    {
        snprintf(logNumPostfix, LOG_NUMBER_POST_FIX_LENGTH, "%u-%u", logNum, subpage);
    }
    else
    {
        snprintf(logNumPostfix, LOG_NUMBER_POST_FIX_LENGTH, "%u", logNum);
    }
    common_String_Concat(logFileName, GENERIC_LOG_FILE_NAME_LENGTH, logNumPostfix);
    #ifdef _DEBUG
    printf("%s: Log to Pull %d, mode %d, device type %d\n",__FUNCTION__, logNum, C_CAST(uint8_t, mode), device->drive_info.drive_type);
    #endif

    switch (device->drive_info.drive_type)
    {
    case ATA_DRIVE:
        {
            //TODO: Instead of a scope, this should be a function.
            //First, setting up bools for GPL and SMART logging features based on drive capabilities
            bool gpl = device->drive_info.ata_Options.generalPurposeLoggingSupported;
            bool smart = (is_SMART_Enabled(device) && (device->drive_info.IdentifyData.ata.Word084 & BIT0 || device->drive_info.IdentifyData.ata.Word087 & BIT0) );
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
                retStatus = get_ATA_Log(device, logNum, logFileName, "bin", gpl, smart, false, NULL, 0, filePath, transferSizeBytes,0,delayTime);
                break;
            case PULL_LOG_RAW_MODE:
                if (SUCCESS == get_ATA_Log_Size(device, logNum, &logSize, true, false))
                {
                    genericLogBuf = C_CAST(uint8_t*, calloc_aligned(logSize, sizeof(uint8_t), device->os_info.minimumAlignment));
                    if (genericLogBuf)
                    {
                        retStatus = get_ATA_Log(device, logNum, NULL, NULL, true, false, true, genericLogBuf, logSize, NULL, transferSizeBytes,0,delayTime);
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
        }
        break;
    case SCSI_DRIVE:
        switch (mode)
        {
        case PULL_LOG_BIN_FILE_MODE:
            retStatus = get_SCSI_Log(device,logNum, subpage, logFileName, "bin", false, NULL, 0, filePath);
            break;
        case PULL_LOG_RAW_MODE:
            if (SUCCESS == get_SCSI_Log_Size(device, logNum, subpage, &logSize))
            {
                genericLogBuf = C_CAST(uint8_t*, calloc_aligned(logSize, sizeof(uint8_t), device->os_info.minimumAlignment));
                if (genericLogBuf)
                {
                    retStatus = get_SCSI_Log(device, logNum, subpage, NULL, NULL, true, genericLogBuf, logSize, NULL);
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
        break;
    case NVME_DRIVE:
        retStatus = pull_Supported_NVMe_Logs(device, logNum, mode);
        break;
    default:
        break;
    }
    safe_Free_aligned(genericLogBuf)
    return retStatus;
}

int pull_Generic_Error_History(tDevice *device, uint8_t bufferID, eLogPullMode mode, const char * const filePath, uint32_t transferSizeBytes, uint32_t delayTime)
{
    int retStatus = NOT_SUPPORTED;
    uint32_t logSize = 0;
    uint8_t *genericLogBuf = NULL;
#define ERROR_HISTORY_FILENAME_LENGTH 30
#define ERROR_HISTORY_POST_FIX_LENGTH 10
    char errorHistoryFileName[ERROR_HISTORY_FILENAME_LENGTH] = "GENERIC_ERROR_HISTORY-";
    char errorHistoryNumPostfix[ERROR_HISTORY_POST_FIX_LENGTH] = { 0 };
    snprintf(errorHistoryNumPostfix, ERROR_HISTORY_POST_FIX_LENGTH, "%" PRIu8, bufferID);
    common_String_Concat(errorHistoryFileName, ERROR_HISTORY_FILENAME_LENGTH, errorHistoryNumPostfix);
    bool rb16 = is_SCSI_Read_Buffer_16_Supported(device);

    switch (mode)
    {
    case PULL_LOG_BIN_FILE_MODE:
        retStatus = get_SCSI_Error_History(device, bufferID, errorHistoryFileName, false, rb16, "bin", false, NULL, 0, filePath, transferSizeBytes, NULL, delayTime);
        break;
    case PULL_LOG_RAW_MODE:
        if (SUCCESS == get_SCSI_Error_History_Size(device, bufferID, &logSize, false, rb16))
        {
            genericLogBuf = C_CAST(uint8_t*, calloc_aligned(logSize, sizeof(uint8_t), device->os_info.minimumAlignment));
            if (genericLogBuf)
            {
                retStatus = get_SCSI_Error_History(device, bufferID, NULL, false, rb16, NULL, true, genericLogBuf, logSize, NULL, transferSizeBytes, NULL, delayTime);
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
    safe_Free_aligned(genericLogBuf)
    return retStatus;
}

int pull_FARM_Log(tDevice *device,const char * const filePath, uint32_t transferSizeBytes, uint32_t issueFactory, uint8_t logAddress, uint32_t delayTime)
{
    int ret = UNKNOWN;
    if (device->drive_info.drive_type == ATA_DRIVE)
    {        
        switch (logAddress)
        {
            case SEAGATE_ATA_LOG_FARM_TIME_SERIES:
            {
                //FARM pull time series subpages   
                //1 (feature register 0) - Default: Report all FARM frames from disc (~250ms) (SATA only)
                //2 (feature register 1) - Report all FARM data (~250ms)(SATA only)
                //3 (feature register 2) - Return WLTR data (SATA only)

                if (issueFactory == 2)
                {
                    ret = get_ATA_Log(device, logAddress, "FARM_TIME_SERIES_FLASH", "bin", true, false, false, NULL, 0, filePath, transferSizeBytes, SEAGATE_FARM_TIME_SERIES_FLASH, delayTime);
                }
                else if (issueFactory == 3)
                {
                    ret = get_ATA_Log(device, logAddress, "FARM_WLTR", "bin", true, false, false, NULL, 0, filePath, transferSizeBytes, SEAGATE_FARM_TIME_SERIES_WLTR, delayTime);
                }               
                else
                {
                    ret = get_ATA_Log(device, logAddress, "FARM_TIME_SERIES_DISC", "bin", true, false, false, NULL, 0, filePath, transferSizeBytes, SEAGATE_FARM_TIME_SERIES_DISC, delayTime);
                }

                break;
            }
            default:
            {
                //FARM pull Factory subpages   
                //0 � Default: Generate and report new FARM data but do not save to disc (~7ms) (SATA only)
                //1 � Generate and report new FARM data and save to disc(~45ms)(SATA only)
                //2 � Report previous FARM data from disc(~20ms)(SATA only)
                //3 � Report FARM factory data from disc(~20ms)(SATA only)
                if (issueFactory == 1)
                {
                    ret = get_ATA_Log(device, SEAGATE_ATA_LOG_FIELD_ACCESSIBLE_RELIABILITY_METRICS, "P_AND_S_FARM", "bin", true, false, false, NULL, 0, filePath, transferSizeBytes, SEAGATE_FARM_GENERATE_NEW_AND_SAVE, delayTime);
                }
                else if (issueFactory == 2)
                {
                    ret = get_ATA_Log(device, SEAGATE_ATA_LOG_FIELD_ACCESSIBLE_RELIABILITY_METRICS, "PREVIOUS_FARM", "bin", true, false, false, NULL, 0, filePath, transferSizeBytes, SEAGATE_FARM_REPORT_SAVED, delayTime);
                }
                else if (issueFactory == 3)
                {
                    ret = get_ATA_Log(device, SEAGATE_ATA_LOG_FIELD_ACCESSIBLE_RELIABILITY_METRICS, "FACTORY_FARM", "bin", true, false, false, NULL, 0, filePath, transferSizeBytes, SEAGATE_FARM_REPORT_FACTORY_DATA, delayTime);
                }
                else
                {
                    ret = get_ATA_Log(device, SEAGATE_ATA_LOG_FIELD_ACCESSIBLE_RELIABILITY_METRICS, "FARM", "bin", true, false, false, NULL, 0, filePath, transferSizeBytes, SEAGATE_FARM_CURRENT, delayTime);
                }
                break;
            }

        }
    }
    else if (device->drive_info.drive_type == SCSI_DRIVE)
    {
        //FARM pull Factory subpages   
       //0 � Default: Generate and report new FARM data but do not save to disc (~7ms) (SATA only)
       //4 - factory subpage (SAS only)
        if (issueFactory == 4)
        {
            ret = get_SCSI_Log(device, SEAGATE_LP_FARM, SEAGATE_FARM_SP_FACTORY, "FACTORY_FARM", "bin", false, NULL, 0, filePath);
            
        }
        else
        {
            ret = get_SCSI_Log(device, SEAGATE_LP_FARM, SEAGATE_FARM_SP_CURRENT, "FARM", "bin", false, NULL, 0, filePath);
        }
    }
    else
    {
        ret = NOT_SUPPORTED;
    }
    return ret;
}

bool is_FARM_Log_Supported(tDevice *device)
{
    bool supported = false;
    uint32_t logSize = 0;
#ifdef _DEBUG
    printf("%s -->\n",__FUNCTION__);
#endif

    if ( (device->drive_info.drive_type == ATA_DRIVE) && (get_ATA_Log_Size(device, 0xA6, &logSize, true, false) == SUCCESS) )
    {
        supported = true;
    }
    else if ( (device->drive_info.drive_type == SCSI_DRIVE) && ( get_SCSI_Log_Size(device, 0x3D, 0x03, &logSize) == SUCCESS) )
    {
        supported = true;
    }
    //else currently not supported on NVMe. 
#ifdef _DEBUG
    printf("%s <-- (%d)\n",__FUNCTION__, supported);
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
    //else currently not supported on SAS or NVMe. 
#ifdef _DEBUG
    printf("%s <-- (%d)\n", __FUNCTION__, supported);
#endif

    return supported;

}
