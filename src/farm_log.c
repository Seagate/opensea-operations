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
// \file farm_log.c
// \brief This file defines the functions related to FARM Log

#include "farm_log.h"
#include "logs.h"
#include "vendor/seagate/seagate_ata_types.h"

#define FARMC_LOG_HEADER_LENGTH             256
#define FARMC_LOG_DATA_SET_HEADER_LENGTH    32
#define ATA_FARM_LOG_PAGE_SIZE              (96 * 1024)         //96 KB
#define ATA_WORKLOAD_TRACE_PAGE_SIZE        (2048 * 1024)       //2048 KB
#define ATA_TIMESERIES_FRAME_LOG_SIZE       (27 * 96 * 1024)    //27 * 96 KB
#define FARM_SIGNATURE_LENGTH               16
#define FARM_DATASET_SIGNATURE_LENGTH       8

#define FARMC_LOG_MAJOR_VERSION             1
#define FARMC_LOG_MINOR_VERSION             0
#define FARMC_LOG_PATCH_VERSION             0

#define FARMC_SIGNATURE_ID                  "STX_FARM_COMBINE"

typedef enum _eFarmSubPageType
{
    SUBPAGE_TYPE_FARM_CURRENT,
    SUBPAGE_TYPE_FARM_FACTORY,
    SUBPAGE_TYPE_FARM_TIMESERIES,
    SUBPAGE_TYPE_FARM_STICKY,
    SUBPAGE_TYPE_FARM_WORKLOAD_TRACE,
    SUBPAGE_TYPE_FARM_SAVE,
    SUBPAGE_TYPE_MAX,
} eFarmSubPageType;

char farmSubPageSignatureId[SUBPAGE_TYPE_MAX][FARM_DATASET_SIGNATURE_LENGTH + 1] = { "FARM", "FACTORY", "FARMTIME", "FARMSTCK", "WORKLDTC", "FARMSAVE" };

void addDataSetEntry(int32_t subPageType, uint8_t *dataSetHeader, uint16_t *numberOfDataSets, uint16_t *headerLength, uint32_t *farmContentField,
    uint32_t dataSetLength, uint64_t startTimeStamp, uint64_t endTimeStamp)
{
    //farm current signature
    char signature[FARM_DATASET_SIGNATURE_LENGTH + 1] = { 0 };
    snprintf(signature, FARM_DATASET_SIGNATURE_LENGTH + 1, "%-*s", FARM_DATASET_SIGNATURE_LENGTH, farmSubPageSignatureId[subPageType]);
    memcpy(dataSetHeader, &signature, FARM_DATASET_SIGNATURE_LENGTH);
    memcpy(dataSetHeader + 12, &dataSetLength, sizeof(uint32_t));
    memcpy(dataSetHeader + 16, &startTimeStamp, sizeof(uint64_t));
    memcpy(dataSetHeader + 24, &endTimeStamp, sizeof(uint64_t));

    *numberOfDataSets += 1;
    *headerLength += FARMC_LOG_DATA_SET_HEADER_LENGTH;
    *farmContentField |= M_BitN(subPageType);
}

void updateDataSetEntryOffset(uint8_t *dataSetHeader, uint32_t dataSetOffset)
{
    memcpy(dataSetHeader + 8, &dataSetOffset, sizeof(uint32_t));
}

int32_t pullATAFarmLogs(tDevice *device, uint32_t transferSizeBytes, uint32_t delayTime, uint8_t *header,
    uint8_t *farmCurrentHeader, uint8_t *farmFactoryHeader, uint8_t *farmSavedHeader, uint8_t *farmTimeSeriesHeader, uint8_t *farmStickyHeader, uint8_t *farmWorkLoadTraceHeader,
    uint8_t *farmCurrentLog, uint8_t *farmFactoryLog, uint8_t *farmSavedLog, uint8_t *farmTimeSeriesLog, uint8_t *farmStickyLog, uint8_t *farmWorkLoadTraceLog)
{
    uint64_t startTimeInMilliSecs = 0, endTimeInMilliSecs = 0;
    uint16_t numberOfDataSets = 0;
    uint16_t headerLength = FARMC_LOG_HEADER_LENGTH;
    uint32_t farmContentField = 0;

    if (is_FARM_Log_Supported(device))
    {
        //FARM current logpage - (0xA6 - 0x00)
        startTimeInMilliSecs = time(NULL) * 1000;
        if (SUCCESS == get_ATA_Log(device, SEAGATE_ATA_LOG_FIELD_ACCESSIBLE_RELIABILITY_METRICS, NULL, NULL, true, false, true, farmCurrentLog, ATA_FARM_LOG_PAGE_SIZE, NULL, transferSizeBytes, SEAGATE_FARM_CURRENT, delayTime))
        {
            endTimeInMilliSecs = time(NULL) * 1000;
            addDataSetEntry(SUBPAGE_TYPE_FARM_CURRENT, farmCurrentHeader, &numberOfDataSets, &headerLength, &farmContentField, ATA_FARM_LOG_PAGE_SIZE, startTimeInMilliSecs, endTimeInMilliSecs);

#ifdef _DEBUG
            FILE *tempFile = fopen("farmcurrent.bin", "w+b");
            if (tempFile != NULL)
            {
                if (fwrite(farmCurrentLog, sizeof(uint8_t), ATA_FARM_LOG_PAGE_SIZE, tempFile) != C_CAST(size_t, ATA_FARM_LOG_PAGE_SIZE)
                    || ferror(tempFile))
                {
                    printf("error in writing farmcurrent.bin file\n");
                    return ERROR_WRITING_FILE;
                }
                fclose(tempFile);
            }
#endif
        }
        else
        {
            if (VERBOSITY_QUIET < device->deviceVerbosity)
            {
                printf("Error pulling Farm Current log\n");
            }
        }

        //FARM factory logpage - (0xA6 - 0x03)
        startTimeInMilliSecs = time(NULL) * 1000;
        if (SUCCESS == get_ATA_Log(device, SEAGATE_ATA_LOG_FIELD_ACCESSIBLE_RELIABILITY_METRICS, NULL, NULL, true, false, true, farmFactoryLog, ATA_FARM_LOG_PAGE_SIZE, NULL, transferSizeBytes, SEAGATE_FARM_REPORT_FACTORY_DATA, delayTime))
        {
            endTimeInMilliSecs = time(NULL) * 1000;
            addDataSetEntry(SUBPAGE_TYPE_FARM_FACTORY, farmFactoryHeader, &numberOfDataSets, &headerLength, &farmContentField, ATA_FARM_LOG_PAGE_SIZE, startTimeInMilliSecs, endTimeInMilliSecs);

#ifdef _DEBUG
            FILE *tempFile = fopen("farmfactory.bin", "w+b");
            if (tempFile != NULL)
            {
                if (fwrite(farmFactoryLog, sizeof(uint8_t), ATA_FARM_LOG_PAGE_SIZE, tempFile) != C_CAST(size_t, ATA_FARM_LOG_PAGE_SIZE)
                    || ferror(tempFile))
                {
                    printf("error in writing farmfactory.bin file\n");
                    return ERROR_WRITING_FILE;
                }
                fclose(tempFile);
            }
#endif
        }
        else
        {
            if (VERBOSITY_QUIET < device->deviceVerbosity)
            {
                printf("Error pulling Farm Factory log\n");
            }
        }

        //FARM saved logpage - (0xA6 - 0x02)
        startTimeInMilliSecs = time(NULL) * 1000;
        if (SUCCESS == get_ATA_Log(device, SEAGATE_ATA_LOG_FIELD_ACCESSIBLE_RELIABILITY_METRICS, NULL, NULL, true, false, true, farmSavedLog, ATA_FARM_LOG_PAGE_SIZE, NULL, transferSizeBytes, SEAGATE_FARM_REPORT_SAVED, delayTime))
        {
            endTimeInMilliSecs = time(NULL) * 1000;
            addDataSetEntry(SUBPAGE_TYPE_FARM_SAVE, farmSavedHeader, &numberOfDataSets, &headerLength, &farmContentField, ATA_FARM_LOG_PAGE_SIZE, startTimeInMilliSecs, endTimeInMilliSecs);

#ifdef _DEBUG
            FILE *tempFile = fopen("farmsaved.bin", "w+b");
            if (tempFile != NULL)
            {
                if (fwrite(farmSavedLog, sizeof(uint8_t), ATA_FARM_LOG_PAGE_SIZE, tempFile) != C_CAST(size_t, ATA_FARM_LOG_PAGE_SIZE)
                    || ferror(tempFile))
                {
                    printf("error in writing farmsaved.bin file\n");
                    return ERROR_WRITING_FILE;
                }
                fclose(tempFile);
            }
#endif
        }
        else
        {
            if (VERBOSITY_QUIET < device->deviceVerbosity)
            {
                printf("Error pulling Farm Saved log\n");
            }
        }
    }

    if (is_FARM_Time_Series_Log_Supported(device))
    {
        //FARM Time series logpage 0xC6 - feature 0x00
        uint8_t *farmTimeSeriesFramesLog = C_CAST(uint8_t*, calloc_aligned(ATA_TIMESERIES_FRAME_LOG_SIZE, sizeof(uint8_t), device->os_info.minimumAlignment));
        startTimeInMilliSecs = time(NULL) * 1000;
        if (SUCCESS == get_ATA_Log(device, SEAGATE_ATA_LOG_FARM_TIME_SERIES, NULL, NULL, true, false, true, farmTimeSeriesFramesLog, ATA_TIMESERIES_FRAME_LOG_SIZE, NULL, transferSizeBytes, SEAGATE_FARM_TIME_SERIES_DISC, delayTime))
        {
            endTimeInMilliSecs = time(NULL) * 1000;

            //copy 16 Time series frames into log buffer
            memcpy(farmTimeSeriesLog, farmTimeSeriesFramesLog, 16 * ATA_FARM_LOG_PAGE_SIZE);
            addDataSetEntry(SUBPAGE_TYPE_FARM_TIMESERIES, farmTimeSeriesHeader, &numberOfDataSets, &headerLength, &farmContentField, 16 * ATA_FARM_LOG_PAGE_SIZE, startTimeInMilliSecs, endTimeInMilliSecs);

#ifdef _DEBUG
            FILE *tempFileTimeseries = fopen("farmtimeseries.bin", "w+b");
            if (tempFileTimeseries != NULL)
            {
                if (fwrite(farmTimeSeriesLog, sizeof(uint8_t), 16 * ATA_FARM_LOG_PAGE_SIZE, tempFileTimeseries) != C_CAST(size_t, 16 * ATA_FARM_LOG_PAGE_SIZE)
                    || ferror(tempFileTimeseries))
                {
                    printf("error in writing farmtimeseries.bin file\n");
                    safe_Free_aligned(farmTimeSeriesFramesLog);
                    return ERROR_WRITING_FILE;
                }
                fclose(tempFileTimeseries);
            }
#endif

            //copy 6 Sticky frames into log buffer
            memcpy(farmStickyLog, farmTimeSeriesFramesLog + (16 * ATA_FARM_LOG_PAGE_SIZE), 6 * ATA_FARM_LOG_PAGE_SIZE);
            addDataSetEntry(SUBPAGE_TYPE_FARM_STICKY, farmStickyHeader, &numberOfDataSets, &headerLength, &farmContentField, 6 * ATA_FARM_LOG_PAGE_SIZE, startTimeInMilliSecs, endTimeInMilliSecs);

#ifdef _DEBUG
            FILE *tempFileSticky = fopen("farmsticky.bin", "w+b");
            if (tempFileSticky != NULL)
            {
                if (fwrite(farmStickyLog, sizeof(uint8_t), 6 * ATA_FARM_LOG_PAGE_SIZE, tempFileSticky) != C_CAST(size_t, 6 * ATA_FARM_LOG_PAGE_SIZE)
                    || ferror(tempFileSticky))
                {
                    printf("error in writing farmsticky.bin file\n");
                    safe_Free_aligned(farmTimeSeriesFramesLog);
                    return ERROR_WRITING_FILE;
                }
                fclose(tempFileSticky);
            }
#endif
        }
        else
        {
            if (VERBOSITY_QUIET < device->deviceVerbosity)
            {
                printf("Error pulling Farm Time series log\n");
            }
        }
        safe_Free_aligned(farmTimeSeriesFramesLog);

        //FARM Workload trace logpage 0xC6 - feature 0x02
        uint8_t *farmWorkloadTraceFramesLog = C_CAST(uint8_t*, calloc_aligned(ATA_TIMESERIES_FRAME_LOG_SIZE, sizeof(uint8_t), device->os_info.minimumAlignment));
        startTimeInMilliSecs = time(NULL) * 1000;
        if (SUCCESS == get_ATA_Log(device, SEAGATE_ATA_LOG_FARM_TIME_SERIES, NULL, NULL, true, false, true, farmWorkloadTraceFramesLog, ATA_TIMESERIES_FRAME_LOG_SIZE, NULL, transferSizeBytes, SEAGATE_FARM_TIME_SERIES_WLTR, delayTime))
        {
            endTimeInMilliSecs = time(NULL) * 1000;

            //copy 2048 KB of meaningful data in log buffer
            memcpy(farmWorkLoadTraceLog, farmWorkloadTraceFramesLog, ATA_WORKLOAD_TRACE_PAGE_SIZE);
            addDataSetEntry(SUBPAGE_TYPE_FARM_WORKLOAD_TRACE, farmWorkLoadTraceHeader, &numberOfDataSets, &headerLength, &farmContentField, ATA_WORKLOAD_TRACE_PAGE_SIZE, startTimeInMilliSecs, endTimeInMilliSecs);

#ifdef _DEBUG
            FILE *tempFile = fopen("farmworkloadtrace.bin", "w+b");
            if (tempFile != NULL)
            {
                if (fwrite(farmWorkLoadTraceLog, sizeof(uint8_t), ATA_WORKLOAD_TRACE_PAGE_SIZE, tempFile) != C_CAST(size_t, ATA_WORKLOAD_TRACE_PAGE_SIZE)
                    || ferror(tempFile))
                {
                    printf("error in writing farmworkloadtrace.bin file\n");
                    safe_Free_aligned(farmWorkloadTraceFramesLog);
                    return ERROR_WRITING_FILE;
                }
                fclose(tempFile);
            }
#endif
        }
        else
        {
            if (VERBOSITY_QUIET < device->deviceVerbosity)
            {
                printf("Error pulling Farm workload trace log\n");
            }
        }
        safe_Free_aligned(farmWorkloadTraceFramesLog);
    }

    //update subheader entry
    uint32_t datasetOffset = headerLength;
    if (farmContentField && BIT0)
    {
        updateDataSetEntryOffset(farmCurrentHeader, datasetOffset);
        datasetOffset += ATA_FARM_LOG_PAGE_SIZE;
    }

    if (farmContentField && BIT1)
    {
        updateDataSetEntryOffset(farmFactoryHeader, datasetOffset);
        datasetOffset += ATA_FARM_LOG_PAGE_SIZE;
    }

    if (farmContentField && BIT2)
    {
        updateDataSetEntryOffset(farmSavedHeader, datasetOffset);
        datasetOffset += ATA_FARM_LOG_PAGE_SIZE;
    }

    if (farmContentField && BIT3)
    {
        updateDataSetEntryOffset(farmTimeSeriesHeader, datasetOffset);
        datasetOffset += (16 * ATA_FARM_LOG_PAGE_SIZE);
    }

    if (farmContentField && BIT4)
    {
        updateDataSetEntryOffset(farmStickyHeader, datasetOffset);
        datasetOffset += (6 * ATA_FARM_LOG_PAGE_SIZE);
    }

    if (farmContentField && BIT5)
    {
        updateDataSetEntryOffset(farmWorkLoadTraceHeader, datasetOffset);
        datasetOffset += ATA_WORKLOAD_TRACE_PAGE_SIZE;
    }

    //copy remaing fields for header information
    memcpy(header + 116, &headerLength, sizeof(uint16_t));
    memcpy(header + 118, &farmContentField, sizeof(uint32_t));
    memcpy(header + 252, &numberOfDataSets, sizeof(uint16_t));

    return SUCCESS;
}

int32_t pullSCSIFarmLogs(tDevice *device, uint32_t transferSizeBytes, uint32_t delayTime, uint8_t *header,
    uint8_t *farmCurrentHeader, uint8_t *farmFactoryHeader, uint8_t *farmSavedHeader, uint8_t *farmTimeSeriesHeader, uint8_t *farmStickyHeader, uint8_t *farmWorkLoadTraceHeader,
    uint8_t *farmCurrentLog, uint8_t *farmFactoryLog, uint8_t *farmSavedLog, uint8_t *farmTimeSeriesLog, uint8_t *farmStickyLog, uint8_t *farmWorkLoadTraceLog)
{
    return NOT_SUPPORTED;
}

int pull_FARM_Combined_Log(tDevice *device, const char * const filePath, uint32_t transferSizeBytes, uint32_t delayTime)
{
    int32_t returnValue = NOT_SUPPORTED;

    if (!(is_FARM_Log_Supported(device) || is_FARM_Time_Series_Log_Supported(device))) //No farm or farm timeseries supported, then return
    {
        return returnValue;
    }

    uint8_t header[FARMC_LOG_HEADER_LENGTH] = { 0 };
    uint8_t farmCurrentHeader[FARMC_LOG_DATA_SET_HEADER_LENGTH] = { 0 };
    uint8_t farmFactoryHeader[FARMC_LOG_DATA_SET_HEADER_LENGTH] = { 0 };
    uint8_t farmSavedHeader[FARMC_LOG_DATA_SET_HEADER_LENGTH] = { 0 };
    uint8_t farmTimeSeriesHeader[FARMC_LOG_DATA_SET_HEADER_LENGTH] = { 0 };
    uint8_t farmStickyHeader[FARMC_LOG_DATA_SET_HEADER_LENGTH] = { 0 };
    uint8_t farmWorkLoadTraceHeader[FARMC_LOG_DATA_SET_HEADER_LENGTH] = { 0 };
    uint8_t *farmCurrentLog = NULL;
    uint8_t *farmFactoryLog = NULL;
    uint8_t *farmSavedLog = NULL;
    uint8_t *farmTimeSeriesLog = NULL;
    uint8_t *farmStickyLog = NULL;
    uint8_t *farmWorkLoadTraceLog = NULL;

    //set signature
    char signature[FARM_SIGNATURE_LENGTH + 1] = { 0 };
    snprintf(signature, FARM_SIGNATURE_LENGTH + 1, "%-*s", FARM_SIGNATURE_LENGTH, FARMC_SIGNATURE_ID);
    memcpy(header, &signature, FARM_SIGNATURE_LENGTH);

    //set the version number - major.minor.revision
    uint16_t majorVersion = FARMC_LOG_MAJOR_VERSION;
    uint16_t minorVersion = FARMC_LOG_MINOR_VERSION;
    uint16_t patchVersion = FARMC_LOG_PATCH_VERSION;
    memcpy(header + 22, &majorVersion, sizeof(uint16_t));
    memcpy(header + 20, &minorVersion, sizeof(uint16_t));
    memcpy(header + 18, &patchVersion, sizeof(uint16_t));

    //set interface type
    char interfaceType[4 + 1] = { 0 };
    if (device->drive_info.drive_type == ATA_DRIVE)
    {
        snprintf(interfaceType, 4 + 1, "%-*s", 4, "SATA");
    }
    else if (device->drive_info.drive_type == SCSI_DRIVE)
    {
        snprintf(interfaceType, 4 + 1, "%-*s", 4, "SAS");
    }
    else
    {
        snprintf(interfaceType, 4 + 1, "%-*s", 4, "NVMe");
    }
    memcpy(header + 24, &interfaceType, 4);

    //set model#
    char modelNumber[MODEL_NUM_LEN + 1] = { 0 };
    snprintf(modelNumber, MODEL_NUM_LEN + 1, "%-*s", MODEL_NUM_LEN, device->drive_info.product_identification);
    memcpy(header + 32, &modelNumber, MODEL_NUM_LEN);

    //set serial#
    char serialNumber[SERIAL_NUM_LEN + 1] = { 0 };
    snprintf(serialNumber, SERIAL_NUM_LEN + 1, "%-*s", SERIAL_NUM_LEN, device->drive_info.serialNumber);
    memcpy(header + 80, &serialNumber, SERIAL_NUM_LEN);

    //set firmware revision
    char firmwareVersion[FW_REV_LEN + 1] = { 0 };
    snprintf(firmwareVersion, FW_REV_LEN + 1, "%-*s", FW_REV_LEN, device->drive_info.product_revision);
    memcpy(header + 104, &firmwareVersion, FW_REV_LEN);

    //set dataset length
    uint16_t dataSetLength = FARMC_LOG_DATA_SET_HEADER_LENGTH;
    memcpy(header + 254, &dataSetLength, sizeof(uint16_t));

    do
    {
        //pull individual log subpage
        if (device->drive_info.drive_type == ATA_DRIVE)
        {
            //initialize log buffers
            farmCurrentLog = C_CAST(uint8_t*, calloc_aligned(ATA_FARM_LOG_PAGE_SIZE, sizeof(uint8_t), device->os_info.minimumAlignment));               //96KB
            farmFactoryLog = C_CAST(uint8_t*, calloc_aligned(ATA_FARM_LOG_PAGE_SIZE, sizeof(uint8_t), device->os_info.minimumAlignment));               //96KB
            farmSavedLog = C_CAST(uint8_t*, calloc_aligned(ATA_FARM_LOG_PAGE_SIZE, sizeof(uint8_t), device->os_info.minimumAlignment));                 //96KB
            farmTimeSeriesLog = C_CAST(uint8_t*, calloc_aligned(16 * ATA_FARM_LOG_PAGE_SIZE, sizeof(uint8_t), device->os_info.minimumAlignment));       //16 * 96KB
            farmStickyLog = C_CAST(uint8_t*, calloc_aligned(6 * ATA_FARM_LOG_PAGE_SIZE, sizeof(uint8_t), device->os_info.minimumAlignment));            //6 * 96KB
            farmWorkLoadTraceLog = C_CAST(uint8_t*, calloc_aligned(ATA_WORKLOAD_TRACE_PAGE_SIZE, sizeof(uint8_t), device->os_info.minimumAlignment));   //2048KB
            if (!farmCurrentLog)
            {
                if (device->deviceVerbosity > VERBOSITY_QUIET)
                {
                    perror("error memory allocation for farmCurrentLog");
                }
                return MEMORY_FAILURE;
            }
            if (!farmFactoryLog)
            {
                if (device->deviceVerbosity > VERBOSITY_QUIET)
                {
                    perror("error memory allocation for farmFactoryLog");
                }
                return MEMORY_FAILURE;
            }
            if (!farmSavedLog)
            {
                if (device->deviceVerbosity > VERBOSITY_QUIET)
                {
                    perror("error memory allocation for farmSavedLog");
                }
                return MEMORY_FAILURE;
            }
            if (!farmTimeSeriesLog)
            {
                if (device->deviceVerbosity > VERBOSITY_QUIET)
                {
                    perror("error memory allocation for farmTimeSeriesLog");
                }
                return MEMORY_FAILURE;
            }
            if (!farmStickyLog)
            {
                if (device->deviceVerbosity > VERBOSITY_QUIET)
                {
                    perror("error memory allocation for farmStickyLog");
                }
                return MEMORY_FAILURE;
            }
            if (!farmWorkLoadTraceLog)
            {
                if (device->deviceVerbosity > VERBOSITY_QUIET)
                {
                    perror("error memory allocation for farmWorkLoadTraceLog");
                }
                return MEMORY_FAILURE;
            }
            returnValue = pullATAFarmLogs(device, transferSizeBytes, delayTime, header,
                farmCurrentHeader, farmFactoryHeader, farmSavedHeader,
                farmTimeSeriesHeader, farmStickyHeader, farmWorkLoadTraceHeader,
                farmCurrentLog, farmFactoryLog, farmSavedLog,
                farmTimeSeriesLog, farmStickyLog, farmWorkLoadTraceLog);
            if (returnValue != SUCCESS)
            {
                break;
            }
        }
        else if (device->drive_info.drive_type == SCSI_DRIVE)
        {

        }
        else
        {
            return NOT_SUPPORTED;
        }

        uint32_t farmContentField = C_CAST(uint32_t, header[118]);
        if (!((farmContentField && BIT0) || (farmContentField && BIT1) || (farmContentField && BIT2) || (farmContentField && BIT3) || (farmContentField && BIT4) || (farmContentField && BIT5)))
        {
            if (device->deviceVerbosity > VERBOSITY_QUIET)
            {
                printf("No Farm log available for this drive.\n");
            }
            returnValue = FAILURE;
            break;
        }

        //create and open the log file
        FILE *farmCombinedLog = NULL;
        char *fileNameUsed = NULL;

        if (SUCCESS != create_And_Open_Log_File(device, &farmCombinedLog, filePath, "FARMC", "frmc", NAMING_SERIAL_NUMBER_DATE_TIME, &fileNameUsed))
        {
            if (device->deviceVerbosity > VERBOSITY_QUIET)
            {
                perror("Error in opening the file!\n");
            }
            returnValue = FILE_OPEN_ERROR;
            break;
        }

        //write header 
        if ((fwrite(header, sizeof(uint8_t), FARMC_LOG_HEADER_LENGTH, farmCombinedLog) != C_CAST(size_t, FARMC_LOG_HEADER_LENGTH))
            || ferror(farmCombinedLog))
        {
            if (device->deviceVerbosity > VERBOSITY_QUIET)
            {
                perror("Error in writing header data to a file!\n");
            }
            fclose(farmCombinedLog);
            returnValue = ERROR_WRITING_FILE;
            break;
        }

        //write dataset entry header
        {
            if (farmContentField && BIT0)
            {
                if ((fwrite(farmCurrentHeader, sizeof(uint8_t), FARMC_LOG_DATA_SET_HEADER_LENGTH, farmCombinedLog) != C_CAST(size_t, FARMC_LOG_DATA_SET_HEADER_LENGTH))
                    || ferror(farmCombinedLog))
                {
                    if (device->deviceVerbosity > VERBOSITY_QUIET)
                    {
                        perror("Error in writing farm current header data to a file!\n");
                    }
                    fclose(farmCombinedLog);
                    returnValue = ERROR_WRITING_FILE;
                    break;
                }
            }

            if (farmContentField && BIT1)
            {
                if ((fwrite(farmFactoryHeader, sizeof(uint8_t), FARMC_LOG_DATA_SET_HEADER_LENGTH, farmCombinedLog) != C_CAST(size_t, FARMC_LOG_DATA_SET_HEADER_LENGTH))
                    || ferror(farmCombinedLog))
                {
                    if (device->deviceVerbosity > VERBOSITY_QUIET)
                    {
                        perror("Error in writing farm factory header data to a file!\n");
                    }
                    fclose(farmCombinedLog);
                    returnValue = ERROR_WRITING_FILE;
                    break;
                }
            }

            if (farmContentField && BIT2)
            {
                if ((fwrite(farmSavedHeader, sizeof(uint8_t), FARMC_LOG_DATA_SET_HEADER_LENGTH, farmCombinedLog) != C_CAST(size_t, FARMC_LOG_DATA_SET_HEADER_LENGTH))
                    || ferror(farmCombinedLog))
                {
                    if (device->deviceVerbosity > VERBOSITY_QUIET)
                    {
                        perror("Error in writing farm saved header data to a file!\n");
                    }
                    fclose(farmCombinedLog);
                    returnValue = ERROR_WRITING_FILE;
                    break;
                }
            }

            if (farmContentField && BIT3)
            {
                if ((fwrite(farmTimeSeriesHeader, sizeof(uint8_t), FARMC_LOG_DATA_SET_HEADER_LENGTH, farmCombinedLog) != C_CAST(size_t, FARMC_LOG_DATA_SET_HEADER_LENGTH))
                    || ferror(farmCombinedLog))
                {
                    if (device->deviceVerbosity > VERBOSITY_QUIET)
                    {
                        perror("Error in writing farm time series header data to a file!\n");
                    }
                    fclose(farmCombinedLog);
                    returnValue = ERROR_WRITING_FILE;
                    break;
                }
            }

            if (farmContentField && BIT4)
            {
                if ((fwrite(farmStickyHeader, sizeof(uint8_t), FARMC_LOG_DATA_SET_HEADER_LENGTH, farmCombinedLog) != C_CAST(size_t, FARMC_LOG_DATA_SET_HEADER_LENGTH))
                    || ferror(farmCombinedLog))
                {
                    if (device->deviceVerbosity > VERBOSITY_QUIET)
                    {
                        perror("Error in writing farm sticky frame header data to a file!\n");
                    }
                    fclose(farmCombinedLog);
                    returnValue = ERROR_WRITING_FILE;
                    break;
                }
            }

            if (farmContentField && BIT5)
            {
                if ((fwrite(farmWorkLoadTraceHeader, sizeof(uint8_t), FARMC_LOG_DATA_SET_HEADER_LENGTH, farmCombinedLog) != C_CAST(size_t, FARMC_LOG_DATA_SET_HEADER_LENGTH))
                    || ferror(farmCombinedLog))
                {
                    if (device->deviceVerbosity > VERBOSITY_QUIET)
                    {
                        perror("Error in writing farm workload trace header data to a file!\n");
                    }
                    fclose(farmCombinedLog);
                    returnValue = ERROR_WRITING_FILE;
                    break;
                }
            }
        }

        //write log buffer
        {
            if (farmContentField && BIT0)
            {
                uint32_t *datasetlength = C_CAST(uint32_t*, farmCurrentHeader + 12);
                if ((fwrite(farmCurrentLog, sizeof(uint8_t), C_CAST(size_t, *datasetlength), farmCombinedLog) != C_CAST(size_t, *datasetlength))
                    || ferror(farmCombinedLog))
                {
                    if (device->deviceVerbosity > VERBOSITY_QUIET)
                    {
                        perror("Error in writing farm current log data to a file!\n");
                    }
                    fclose(farmCombinedLog);
                    returnValue = ERROR_WRITING_FILE;
                    break;
                }
            }

            if (farmContentField && BIT1)
            {
                uint32_t *datasetlength = C_CAST(uint32_t*, farmFactoryHeader + 12);
                if ((fwrite(farmFactoryLog, sizeof(uint8_t), C_CAST(size_t, *datasetlength), farmCombinedLog) != C_CAST(size_t, *datasetlength))
                    || ferror(farmCombinedLog))
                {
                    if (device->deviceVerbosity > VERBOSITY_QUIET)
                    {
                        perror("Error in writing farm factory log data to a file!\n");
                    }
                    fclose(farmCombinedLog);
                    returnValue = ERROR_WRITING_FILE;
                    break;
                }
            }

            if (farmContentField && BIT2)
            {
                uint32_t *datasetlength = C_CAST(uint32_t*, farmSavedHeader + 12);
                if ((fwrite(farmSavedLog, sizeof(uint8_t), C_CAST(size_t, *datasetlength), farmCombinedLog) != C_CAST(size_t, *datasetlength))
                    || ferror(farmCombinedLog))
                {
                    if (device->deviceVerbosity > VERBOSITY_QUIET)
                    {
                        perror("Error in writing farm save log data to a file!\n");
                    }
                    fclose(farmCombinedLog);
                    returnValue = ERROR_WRITING_FILE;
                    break;
                }
            }

            if (farmContentField && BIT3)
            {
                uint32_t *datasetlength = C_CAST(uint32_t*, farmTimeSeriesHeader + 12);
                if ((fwrite(farmTimeSeriesLog, sizeof(uint8_t), C_CAST(size_t, *datasetlength), farmCombinedLog) != C_CAST(size_t, *datasetlength))
                    || ferror(farmCombinedLog))
                {
                    if (device->deviceVerbosity > VERBOSITY_QUIET)
                    {
                        perror("Error in writing farm time series frame log data to a file!\n");
                    }
                    fclose(farmCombinedLog);
                    returnValue = ERROR_WRITING_FILE;
                    break;
                }
            }

            if (farmContentField && BIT4)
            {
                uint32_t *datasetlength = C_CAST(uint32_t*, farmStickyHeader + 12);
                if ((fwrite(farmStickyLog, sizeof(uint8_t), C_CAST(size_t, *datasetlength), farmCombinedLog) != C_CAST(size_t, *datasetlength))
                    || ferror(farmCombinedLog))
                {
                    if (device->deviceVerbosity > VERBOSITY_QUIET)
                    {
                        perror("Error in writing farm sticky frame log data to a file!\n");
                    }
                    fclose(farmCombinedLog);
                    returnValue = ERROR_WRITING_FILE;
                    break;
                }
            }

            if (farmContentField && BIT5)
            {
                uint32_t *datasetlength = C_CAST(uint32_t*, farmWorkLoadTraceHeader + 12);
                if ((fwrite(farmWorkLoadTraceLog, sizeof(uint8_t), C_CAST(size_t, *datasetlength), farmCombinedLog) != C_CAST(size_t, *datasetlength))
                    || ferror(farmCombinedLog))
                {
                    if (device->deviceVerbosity > VERBOSITY_QUIET)
                    {
                        perror("Error in writing farm workload trace log data to a file!\n");
                    }
                    fclose(farmCombinedLog);
                    returnValue = ERROR_WRITING_FILE;
                    break;
                }
            }
        }

        if ((fflush(farmCombinedLog) != 0) || ferror(farmCombinedLog))
        {
            if (device->deviceVerbosity > VERBOSITY_QUIET)
            {
                perror("Error in flushing data!\n");
            }
            fclose(farmCombinedLog);
            returnValue = ERROR_WRITING_FILE;
            break;
        }

        fclose(farmCombinedLog);
    } while (0);

    safe_Free_aligned(farmCurrentLog);
    safe_Free_aligned(farmFactoryLog);
    safe_Free_aligned(farmSavedLog);
    safe_Free_aligned(farmTimeSeriesLog);
    safe_Free_aligned(farmStickyLog);
    safe_Free_aligned(farmWorkLoadTraceLog);

    return returnValue;
}