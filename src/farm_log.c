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
// \file farm_log.c
// \brief This file defines the functions related to FARM Log

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

#include "farm_log.h"
#include "logs.h"
#include "vendor/seagate/seagate_ata_types.h"
#include "vendor/seagate/seagate_scsi_types.h"

#define FARMC_LOG_HEADER_LENGTH             256
#define FARMC_LOG_DATA_SET_HEADER_LENGTH    32
#define ATA_FARM_LOG_PAGE_SIZE              (UINT32_C(96) * UINT32_C(1024))         //96 KB
#define ATA_WORKLOAD_TRACE_PAGE_SIZE        (UINT32_C(2048) * UINT32_C(1024))       //2048 KB
#define ATA_TIMESERIES_FRAME_LOG_SIZE       (UINT32_C(27) * UINT32_C(96) * UINT32_C(1024))    //27 * 96 KB
#define FARM_SIGNATURE_LENGTH               16
#define FARM_DATASET_SIGNATURE_LENGTH       8
#define DATASET_ENTRY_START_OFFSET          512

#define FARMC_LOG_MAJOR_VERSION             1
#define FARMC_LOG_MINOR_VERSION             1
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
    SUBPAGE_TYPE_FARM_LONG_SAVE,
    SUBPAGE_TYPE_MAX,
} eFarmSubPageType;

typedef struct _tSASLogpageSize
{
    uint32_t currentLog;
    uint32_t factoryLog;
    uint32_t timeSeriesLog;     //each frame size
    uint32_t longSavedLog;      //each frame size
    uint32_t stickyLog;         //each frame size
} tSASLogpageSize;

typedef struct _tZeroPaddingBufferSize
{
    uint32_t headerZeroPadding;
    uint32_t farmCurrentZeroPadding;
    uint32_t farmFactoryZeroPadding;
    uint32_t farmSavedZeroPadding;
    uint32_t farmTimeSeriesZeroPadding;
    uint32_t farmLongSavedZeroPadding;
    uint32_t farmStickyZeroPadding;
} tZeroPaddingBufferSize;

char farmSubPageSignatureId[SUBPAGE_TYPE_MAX][FARM_DATASET_SIGNATURE_LENGTH + 1] = { "FARM", "FACTORY", "FARMTIME", "FARMSTCK", "WORKLDTC", "FARMSAVE", "FARMLONG" };

static void addDataSetEntry(int32_t subPageType, uint8_t *dataSetHeader, uint16_t *numberOfDataSets, uint16_t *headerLength, uint32_t *farmContentField,
    uint32_t dataSetLength, uint64_t startTimeStamp, uint64_t endTimeStamp)
{
    //farm current signature
    DECLARE_ZERO_INIT_ARRAY(char, signature, FARM_DATASET_SIGNATURE_LENGTH + 1);
    snprintf(signature, FARM_DATASET_SIGNATURE_LENGTH + 1, "%-*s", FARM_DATASET_SIGNATURE_LENGTH, farmSubPageSignatureId[subPageType]);
    memcpy(dataSetHeader, &signature, FARM_DATASET_SIGNATURE_LENGTH);
    memcpy(dataSetHeader + 12, &dataSetLength, sizeof(uint32_t));
    memcpy(dataSetHeader + 16, &startTimeStamp, sizeof(uint64_t));
    memcpy(dataSetHeader + 24, &endTimeStamp, sizeof(uint64_t));

    *numberOfDataSets += 1;
    *headerLength += FARMC_LOG_DATA_SET_HEADER_LENGTH;
    *farmContentField |= M_BitN(subPageType);
}

static void updateDataSetEntryOffset(uint8_t *dataSetHeader, uint32_t dataSetOffset)
{
    memcpy(dataSetHeader + 8, &dataSetOffset, sizeof(uint32_t));
}

static eReturnValues pullATAFarmLogs(tDevice *device, uint32_t transferSizeBytes, int sataFarmCopyType, uint8_t *header, tZeroPaddingBufferSize *zeroPaddingBufferSize,
    uint8_t *farmCurrentHeader, uint8_t *farmFactoryHeader, uint8_t *farmSavedHeader, uint8_t *farmTimeSeriesHeader, uint8_t *farmLongSavedHeader, uint8_t *farmStickyHeader, uint8_t *farmWorkLoadTraceHeader,
    uint8_t *farmCurrentLog, uint8_t *farmFactoryLog, uint8_t *farmSavedLog, uint8_t *farmTimeSeriesLog, uint8_t *farmLongSavedLog, uint8_t *farmStickyLog, uint8_t *farmWorkLoadTraceLog)
{
    uint64_t startTimeInMilliSecs = 0;
    uint64_t endTimeInMilliSecs = 0;
    uint16_t numberOfDataSets = 0;
    uint16_t headerLength = FARMC_LOG_HEADER_LENGTH;
    uint32_t farmContentField = 0;

    if (sataFarmCopyType == SATA_FARM_COPY_TYPE_DISC)
    {
        if (is_FARM_Log_Supported(device))
        {
            //FARM current logpage - (0xA6 - 0x00)
            startTimeInMilliSecs = get_Milliseconds_Since_Unix_Epoch();
            if (SUCCESS == get_ATA_Log(device, SEAGATE_ATA_LOG_FIELD_ACCESSIBLE_RELIABILITY_METRICS, M_NULLPTR, M_NULLPTR, true, false, true, farmCurrentLog, ATA_FARM_LOG_PAGE_SIZE, M_NULLPTR, transferSizeBytes, SEAGATE_FARM_CURRENT))
            {
                endTimeInMilliSecs = get_Milliseconds_Since_Unix_Epoch();
                addDataSetEntry(SUBPAGE_TYPE_FARM_CURRENT, farmCurrentHeader, &numberOfDataSets, &headerLength, &farmContentField, ATA_FARM_LOG_PAGE_SIZE, startTimeInMilliSecs, endTimeInMilliSecs);

#ifdef _DEBUG
                FILE *tempFile = fopen("farmcurrent.bin", "w+b");
                if (tempFile != M_NULLPTR)
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
            startTimeInMilliSecs = get_Milliseconds_Since_Unix_Epoch();
            if (SUCCESS == get_ATA_Log(device, SEAGATE_ATA_LOG_FIELD_ACCESSIBLE_RELIABILITY_METRICS, M_NULLPTR, M_NULLPTR, true, false, true, farmFactoryLog, ATA_FARM_LOG_PAGE_SIZE, M_NULLPTR, transferSizeBytes, SEAGATE_FARM_REPORT_FACTORY_DATA))
            {
                endTimeInMilliSecs = get_Milliseconds_Since_Unix_Epoch();
                addDataSetEntry(SUBPAGE_TYPE_FARM_FACTORY, farmFactoryHeader, &numberOfDataSets, &headerLength, &farmContentField, ATA_FARM_LOG_PAGE_SIZE, startTimeInMilliSecs, endTimeInMilliSecs);

#ifdef _DEBUG
                FILE *tempFile = fopen("farmfactory.bin", "w+b");
                if (tempFile != M_NULLPTR)
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
            startTimeInMilliSecs = get_Milliseconds_Since_Unix_Epoch();
            if (SUCCESS == get_ATA_Log(device, SEAGATE_ATA_LOG_FIELD_ACCESSIBLE_RELIABILITY_METRICS, M_NULLPTR, M_NULLPTR, true, false, true, farmSavedLog, ATA_FARM_LOG_PAGE_SIZE, M_NULLPTR, transferSizeBytes, SEAGATE_FARM_REPORT_SAVED))
            {
                endTimeInMilliSecs = get_Milliseconds_Since_Unix_Epoch();
                addDataSetEntry(SUBPAGE_TYPE_FARM_SAVE, farmSavedHeader, &numberOfDataSets, &headerLength, &farmContentField, ATA_FARM_LOG_PAGE_SIZE, startTimeInMilliSecs, endTimeInMilliSecs);

#ifdef _DEBUG
                FILE *tempFile = fopen("farmsaved.bin", "w+b");
                if (tempFile != M_NULLPTR)
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
            uint8_t *farmTimeSeriesFramesLog = C_CAST(uint8_t*, safe_calloc_aligned(ATA_TIMESERIES_FRAME_LOG_SIZE, sizeof(uint8_t), device->os_info.minimumAlignment));
            startTimeInMilliSecs = get_Milliseconds_Since_Unix_Epoch();
            if (SUCCESS == get_ATA_Log(device, SEAGATE_ATA_LOG_FARM_TIME_SERIES, M_NULLPTR, M_NULLPTR, true, false, true, farmTimeSeriesFramesLog, ATA_TIMESERIES_FRAME_LOG_SIZE, M_NULLPTR, transferSizeBytes, SEAGATE_FARM_TIME_SERIES_DISC))
            {
                endTimeInMilliSecs = get_Milliseconds_Since_Unix_Epoch();

                //copy 16 Time series frames into log buffer
                memcpy(farmTimeSeriesLog, farmTimeSeriesFramesLog, 16 * ATA_FARM_LOG_PAGE_SIZE);
                addDataSetEntry(SUBPAGE_TYPE_FARM_TIMESERIES, farmTimeSeriesHeader, &numberOfDataSets, &headerLength, &farmContentField, 16 * ATA_FARM_LOG_PAGE_SIZE, startTimeInMilliSecs, endTimeInMilliSecs);

#ifdef _DEBUG
                FILE *tempFileTimeseries = fopen("farmtimeseries.bin", "w+b");
                if (tempFileTimeseries != M_NULLPTR)
                {
                    if (fwrite(farmTimeSeriesLog, sizeof(uint8_t), 16 * ATA_FARM_LOG_PAGE_SIZE, tempFileTimeseries) != C_CAST(size_t, 16 * ATA_FARM_LOG_PAGE_SIZE)
                        || ferror(tempFileTimeseries))
                    {
                        printf("error in writing farmtimeseries.bin file\n");
                        safe_free_aligned(&farmTimeSeriesFramesLog);
                        return ERROR_WRITING_FILE;
                    }
                    fclose(tempFileTimeseries);
                }
#endif

                //copy 2 Long term saved frames into log buffer
                memcpy(farmLongSavedLog, farmTimeSeriesFramesLog + (16 * ATA_FARM_LOG_PAGE_SIZE), 2 * ATA_FARM_LOG_PAGE_SIZE);
                addDataSetEntry(SUBPAGE_TYPE_FARM_LONG_SAVE, farmLongSavedHeader, &numberOfDataSets, &headerLength, &farmContentField, 2 * ATA_FARM_LOG_PAGE_SIZE, startTimeInMilliSecs, endTimeInMilliSecs);

#ifdef _DEBUG
                FILE *tempFileLongSave = fopen("farmlongsaved.bin", "w+b");
                if (tempFileLongSave != M_NULLPTR)
                {
                    if (fwrite(farmLongSavedLog, sizeof(uint8_t), 2 * ATA_FARM_LOG_PAGE_SIZE, tempFileLongSave) != C_CAST(size_t, 2 * ATA_FARM_LOG_PAGE_SIZE)
                        || ferror(tempFileLongSave))
                    {
                        printf("error in writing farmlongsaved.bin file\n");
                        safe_free_aligned(&farmTimeSeriesFramesLog);
                        return ERROR_WRITING_FILE;
                    }
                    fclose(tempFileLongSave);
                }
#endif

                //copy 6 Sticky frames into log buffer
                memcpy(farmStickyLog, farmTimeSeriesFramesLog + (18 * ATA_FARM_LOG_PAGE_SIZE), 6 * ATA_FARM_LOG_PAGE_SIZE);
                addDataSetEntry(SUBPAGE_TYPE_FARM_STICKY, farmStickyHeader, &numberOfDataSets, &headerLength, &farmContentField, 6 * ATA_FARM_LOG_PAGE_SIZE, startTimeInMilliSecs, endTimeInMilliSecs);

#ifdef _DEBUG
                FILE *tempFileSticky = fopen("farmsticky.bin", "w+b");
                if (tempFileSticky != M_NULLPTR)
                {
                    if (fwrite(farmStickyLog, sizeof(uint8_t), 6 * ATA_FARM_LOG_PAGE_SIZE, tempFileSticky) != C_CAST(size_t, 6 * ATA_FARM_LOG_PAGE_SIZE)
                        || ferror(tempFileSticky))
                    {
                        printf("error in writing farmsticky.bin file\n");
                        safe_free_aligned(&farmTimeSeriesFramesLog);
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
            safe_free_aligned(&farmTimeSeriesFramesLog);

            //FARM Workload trace logpage 0xC6 - feature 0x02
            uint8_t *farmWorkloadTraceFramesLog = C_CAST(uint8_t*, safe_calloc_aligned(ATA_TIMESERIES_FRAME_LOG_SIZE, sizeof(uint8_t), device->os_info.minimumAlignment));
            startTimeInMilliSecs = get_Milliseconds_Since_Unix_Epoch();
            if (SUCCESS == get_ATA_Log(device, SEAGATE_ATA_LOG_FARM_TIME_SERIES, M_NULLPTR, M_NULLPTR, true, false, true, farmWorkloadTraceFramesLog, ATA_TIMESERIES_FRAME_LOG_SIZE, M_NULLPTR, transferSizeBytes, SEAGATE_FARM_TIME_SERIES_WLTR))
            {
                endTimeInMilliSecs = get_Milliseconds_Since_Unix_Epoch();

                //copy 2048 KB of meaningful data in log buffer
                memcpy(farmWorkLoadTraceLog, farmWorkloadTraceFramesLog, ATA_WORKLOAD_TRACE_PAGE_SIZE);
                addDataSetEntry(SUBPAGE_TYPE_FARM_WORKLOAD_TRACE, farmWorkLoadTraceHeader, &numberOfDataSets, &headerLength, &farmContentField, ATA_WORKLOAD_TRACE_PAGE_SIZE, startTimeInMilliSecs, endTimeInMilliSecs);

#ifdef _DEBUG
                FILE *tempFile = fopen("farmworkloadtrace.bin", "w+b");
                if (tempFile != M_NULLPTR)
                {
                    if (fwrite(farmWorkLoadTraceLog, sizeof(uint8_t), ATA_WORKLOAD_TRACE_PAGE_SIZE, tempFile) != C_CAST(size_t, ATA_WORKLOAD_TRACE_PAGE_SIZE)
                        || ferror(tempFile))
                    {
                        printf("error in writing farmworkloadtrace.bin file\n");
                        safe_free_aligned(&farmWorkloadTraceFramesLog);
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
            safe_free_aligned(&farmWorkloadTraceFramesLog);
        }
    }
    else if (sataFarmCopyType == SATA_FARM_COPY_TYPE_FLASH)
    {
        if (is_FARM_Time_Series_Log_Supported(device))
        {
            //FARM Time series logpage 0xC6 - feature 0x01
            uint8_t *farmTimeSeriesFramesLog = C_CAST(uint8_t*, safe_calloc_aligned(ATA_TIMESERIES_FRAME_LOG_SIZE, sizeof(uint8_t), device->os_info.minimumAlignment));
            startTimeInMilliSecs = get_Milliseconds_Since_Unix_Epoch();
            if (SUCCESS == get_ATA_Log(device, SEAGATE_ATA_LOG_FARM_TIME_SERIES, M_NULLPTR, M_NULLPTR, true, false, true, farmTimeSeriesFramesLog, ATA_TIMESERIES_FRAME_LOG_SIZE, M_NULLPTR, transferSizeBytes, SEAGATE_FARM_TIME_SERIES_FLASH))
            {
                endTimeInMilliSecs = get_Milliseconds_Since_Unix_Epoch();

                //copy Farm current into log buffer
                memcpy(farmCurrentLog, farmTimeSeriesFramesLog, ATA_FARM_LOG_PAGE_SIZE);
                addDataSetEntry(SUBPAGE_TYPE_FARM_CURRENT, farmCurrentHeader, &numberOfDataSets, &headerLength, &farmContentField, ATA_FARM_LOG_PAGE_SIZE, startTimeInMilliSecs, endTimeInMilliSecs);

#ifdef _DEBUG
                FILE *tempCurrentFile = fopen("farmcurrent.bin", "w+b");
                if (tempCurrentFile != M_NULLPTR)
                {
                    if (fwrite(farmCurrentLog, sizeof(uint8_t), ATA_FARM_LOG_PAGE_SIZE, tempCurrentFile) != C_CAST(size_t, ATA_FARM_LOG_PAGE_SIZE)
                        || ferror(tempCurrentFile))
                    {
                        printf("error in writing farmcurrent.bin file\n");
                        safe_free_aligned(&farmTimeSeriesFramesLog);
                        return ERROR_WRITING_FILE;
                    }
                    fclose(tempCurrentFile);
                }
#endif

                //copy Farm Saved into log buffer
                memcpy(farmSavedLog, farmTimeSeriesFramesLog + ATA_FARM_LOG_PAGE_SIZE, ATA_FARM_LOG_PAGE_SIZE);
                addDataSetEntry(SUBPAGE_TYPE_FARM_SAVE, farmSavedHeader, &numberOfDataSets, &headerLength, &farmContentField, ATA_FARM_LOG_PAGE_SIZE, startTimeInMilliSecs, endTimeInMilliSecs);

#ifdef _DEBUG
                FILE *tempSavedFile = fopen("farmsaved.bin", "w+b");
                if (tempSavedFile != M_NULLPTR)
                {
                    if (fwrite(farmSavedLog, sizeof(uint8_t), ATA_FARM_LOG_PAGE_SIZE, tempSavedFile) != C_CAST(size_t, ATA_FARM_LOG_PAGE_SIZE)
                        || ferror(tempSavedFile))
                    {
                        printf("error in writing farmsaved.bin file\n");
                        safe_free_aligned(&farmTimeSeriesFramesLog);
                        return ERROR_WRITING_FILE;
                    }
                    fclose(tempSavedFile);
                }
#endif

                //copy 16 Timeseries frame into log buffer
                memcpy(farmTimeSeriesLog, farmTimeSeriesFramesLog + (2 * ATA_FARM_LOG_PAGE_SIZE), 16 * ATA_FARM_LOG_PAGE_SIZE);
                addDataSetEntry(SUBPAGE_TYPE_FARM_TIMESERIES, farmTimeSeriesHeader, &numberOfDataSets, &headerLength, &farmContentField, 16 * ATA_FARM_LOG_PAGE_SIZE, startTimeInMilliSecs, endTimeInMilliSecs);

#ifdef _DEBUG
                FILE *tempTimeSeriesFile = fopen("farmtimeseries.bin", "w+b");
                if (tempTimeSeriesFile != M_NULLPTR)
                {
                    if (fwrite(farmTimeSeriesLog, sizeof(uint8_t), 16 * ATA_FARM_LOG_PAGE_SIZE, tempTimeSeriesFile) != C_CAST(size_t, 16 * ATA_FARM_LOG_PAGE_SIZE)
                        || ferror(tempTimeSeriesFile))
                    {
                        printf("error in writing farmtimeseries.bin file\n");
                        safe_free_aligned(&farmTimeSeriesFramesLog);
                        return ERROR_WRITING_FILE;
                    }
                    fclose(tempTimeSeriesFile);
                }
#endif

                //copy 2 Long term saved frame into log buffer
                memcpy(farmLongSavedLog, farmTimeSeriesFramesLog + (18 * ATA_FARM_LOG_PAGE_SIZE), 2 * ATA_FARM_LOG_PAGE_SIZE);
                addDataSetEntry(SUBPAGE_TYPE_FARM_LONG_SAVE, farmLongSavedHeader, &numberOfDataSets, &headerLength, &farmContentField, 2 * ATA_FARM_LOG_PAGE_SIZE, startTimeInMilliSecs, endTimeInMilliSecs);

#ifdef _DEBUG
                FILE *tempLongSavedFile = fopen("farmlongsaved.bin", "w+b");
                if (tempLongSavedFile != M_NULLPTR)
                {
                    if (fwrite(farmLongSavedLog, sizeof(uint8_t), 2 * ATA_FARM_LOG_PAGE_SIZE, tempLongSavedFile) != C_CAST(size_t, 2 * ATA_FARM_LOG_PAGE_SIZE)
                        || ferror(tempLongSavedFile))
                    {
                        printf("error in writing farmlongsaved.bin file\n");
                        safe_free_aligned(&farmTimeSeriesFramesLog);
                        return ERROR_WRITING_FILE;
                    }
                    fclose(tempLongSavedFile);
                }
#endif

                //copy 6 sticky frame into log buffer
                memcpy(farmStickyLog, farmTimeSeriesFramesLog + (20 * ATA_FARM_LOG_PAGE_SIZE), 6 * ATA_FARM_LOG_PAGE_SIZE);
                addDataSetEntry(SUBPAGE_TYPE_FARM_STICKY, farmStickyHeader, &numberOfDataSets, &headerLength, &farmContentField, 6 * ATA_FARM_LOG_PAGE_SIZE, startTimeInMilliSecs, endTimeInMilliSecs);

#ifdef _DEBUG
                FILE *tempStickyFile = fopen("farmsticky.bin", "w+b");
                if (tempStickyFile != M_NULLPTR)
                {
                    if (fwrite(farmStickyLog, sizeof(uint8_t), 6 * ATA_FARM_LOG_PAGE_SIZE, tempStickyFile) != C_CAST(size_t, 6 * ATA_FARM_LOG_PAGE_SIZE)
                        || ferror(tempStickyFile))
                    {
                        printf("error in writing farmsticky.bin file\n");
                        safe_free_aligned(&farmTimeSeriesFramesLog);
                        return ERROR_WRITING_FILE;
                    }
                    fclose(tempStickyFile);
                }
#endif

                //copy Farm factory into log buffer
                memcpy(farmFactoryLog, farmTimeSeriesFramesLog + (26 * ATA_FARM_LOG_PAGE_SIZE), ATA_FARM_LOG_PAGE_SIZE);
                addDataSetEntry(SUBPAGE_TYPE_FARM_FACTORY, farmFactoryHeader, &numberOfDataSets, &headerLength, &farmContentField, ATA_FARM_LOG_PAGE_SIZE, startTimeInMilliSecs, endTimeInMilliSecs);

#ifdef _DEBUG
                FILE *tempFactoryFile = fopen("farmfactory.bin", "w+b");
                if (tempFactoryFile != M_NULLPTR)
                {
                    if (fwrite(farmFactoryLog, sizeof(uint8_t), ATA_FARM_LOG_PAGE_SIZE, tempFactoryFile) != C_CAST(size_t, ATA_FARM_LOG_PAGE_SIZE)
                        || ferror(tempFactoryFile))
                    {
                        printf("error in writing farmfactory.bin file\n");
                        safe_free_aligned(&farmTimeSeriesFramesLog);
                        return ERROR_WRITING_FILE;
                    }
                    fclose(tempFactoryFile);
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
            safe_free_aligned(&farmTimeSeriesFramesLog);

            //FARM Workload trace logpage 0xC6 - feature 0x02
            uint8_t *farmWorkloadTraceFramesLog = C_CAST(uint8_t*, safe_calloc_aligned(ATA_TIMESERIES_FRAME_LOG_SIZE, sizeof(uint8_t), device->os_info.minimumAlignment));
            startTimeInMilliSecs = get_Milliseconds_Since_Unix_Epoch();
            if (SUCCESS == get_ATA_Log(device, SEAGATE_ATA_LOG_FARM_TIME_SERIES, M_NULLPTR, M_NULLPTR, true, false, true, farmWorkloadTraceFramesLog, ATA_TIMESERIES_FRAME_LOG_SIZE, M_NULLPTR, transferSizeBytes, SEAGATE_FARM_TIME_SERIES_WLTR))
            {
                endTimeInMilliSecs = get_Milliseconds_Since_Unix_Epoch();

                //copy 2048 KB of meaningful data in log buffer
                memcpy(farmWorkLoadTraceLog, farmWorkloadTraceFramesLog, ATA_WORKLOAD_TRACE_PAGE_SIZE);
                addDataSetEntry(SUBPAGE_TYPE_FARM_WORKLOAD_TRACE, farmWorkLoadTraceHeader, &numberOfDataSets, &headerLength, &farmContentField, ATA_WORKLOAD_TRACE_PAGE_SIZE, startTimeInMilliSecs, endTimeInMilliSecs);

#ifdef _DEBUG
                FILE *tempFile = fopen("farmworkloadtrace.bin", "w+b");
                if (tempFile != M_NULLPTR)
                {
                    if (fwrite(farmWorkLoadTraceLog, sizeof(uint8_t), ATA_WORKLOAD_TRACE_PAGE_SIZE, tempFile) != C_CAST(size_t, ATA_WORKLOAD_TRACE_PAGE_SIZE)
                        || ferror(tempFile))
                    {
                        printf("error in writing farmworkloadtrace.bin file\n");
                        safe_free_aligned(&farmWorkloadTraceFramesLog);
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
            safe_free_aligned(&farmWorkloadTraceFramesLog);
        }
    }
    else
    {
        return NOT_SUPPORTED;
    }

    //update subheader entry
    uint32_t datasetOffset = headerLength;
    if (headerLength % DATASET_ENTRY_START_OFFSET)
    {
        datasetOffset = ((headerLength / DATASET_ENTRY_START_OFFSET) + UINT32_C(1)) * DATASET_ENTRY_START_OFFSET;
        zeroPaddingBufferSize->headerZeroPadding = datasetOffset - headerLength;
    }

    if (farmContentField & BIT0)
    {
        updateDataSetEntryOffset(farmCurrentHeader, datasetOffset);
        datasetOffset += ATA_FARM_LOG_PAGE_SIZE;
        if (datasetOffset % DATASET_ENTRY_START_OFFSET)
        {
            zeroPaddingBufferSize->farmCurrentZeroPadding = ((datasetOffset / DATASET_ENTRY_START_OFFSET) + 1) * DATASET_ENTRY_START_OFFSET - datasetOffset;
            datasetOffset += zeroPaddingBufferSize->farmCurrentZeroPadding;
        }
    }

    if (farmContentField & BIT1)
    {
        updateDataSetEntryOffset(farmFactoryHeader, datasetOffset);
        datasetOffset += ATA_FARM_LOG_PAGE_SIZE;
        if (datasetOffset % DATASET_ENTRY_START_OFFSET)
        {
            zeroPaddingBufferSize->farmFactoryZeroPadding = ((datasetOffset / DATASET_ENTRY_START_OFFSET) + 1) * DATASET_ENTRY_START_OFFSET - datasetOffset;
            datasetOffset += zeroPaddingBufferSize->farmFactoryZeroPadding;
        }
    }

    if (farmContentField & BIT5)
    {
        updateDataSetEntryOffset(farmSavedHeader, datasetOffset);
        datasetOffset += ATA_FARM_LOG_PAGE_SIZE;
        if (datasetOffset % DATASET_ENTRY_START_OFFSET)
        {
            zeroPaddingBufferSize->farmSavedZeroPadding = ((datasetOffset / DATASET_ENTRY_START_OFFSET) + 1) * DATASET_ENTRY_START_OFFSET - datasetOffset;
            datasetOffset += zeroPaddingBufferSize->farmSavedZeroPadding;
        }
    }

    if (farmContentField & BIT2)
    {
        updateDataSetEntryOffset(farmTimeSeriesHeader, datasetOffset);
        datasetOffset += (16 * ATA_FARM_LOG_PAGE_SIZE);
        if (datasetOffset % DATASET_ENTRY_START_OFFSET)
        {
            zeroPaddingBufferSize->farmTimeSeriesZeroPadding = ((datasetOffset / DATASET_ENTRY_START_OFFSET) + 1) * DATASET_ENTRY_START_OFFSET - datasetOffset;
            datasetOffset += zeroPaddingBufferSize->farmTimeSeriesZeroPadding;
        }
    }

    if (farmContentField & BIT6)
    {
        updateDataSetEntryOffset(farmLongSavedHeader, datasetOffset);
        datasetOffset += (2 * ATA_FARM_LOG_PAGE_SIZE);
        if (datasetOffset % DATASET_ENTRY_START_OFFSET)
        {
            zeroPaddingBufferSize->farmLongSavedZeroPadding = ((datasetOffset / DATASET_ENTRY_START_OFFSET) + 1) * DATASET_ENTRY_START_OFFSET - datasetOffset;
            datasetOffset += zeroPaddingBufferSize->farmLongSavedZeroPadding;
        }
    }

    if (farmContentField & BIT3)
    {
        updateDataSetEntryOffset(farmStickyHeader, datasetOffset);
        datasetOffset += (6 * ATA_FARM_LOG_PAGE_SIZE);
        if (datasetOffset % DATASET_ENTRY_START_OFFSET)
        {
            zeroPaddingBufferSize->farmStickyZeroPadding = ((datasetOffset / DATASET_ENTRY_START_OFFSET) + 1) * DATASET_ENTRY_START_OFFSET - datasetOffset;
            datasetOffset += zeroPaddingBufferSize->farmStickyZeroPadding;
        }
    }

    if (farmContentField & BIT4)
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

static eReturnValues pullSCSIFarmLogs(tDevice *device, uint8_t *header, tZeroPaddingBufferSize *zeroPaddingBufferSize,
    uint8_t *farmCurrentHeader, uint8_t *farmFactoryHeader, uint8_t *farmTimeSeriesHeader, uint8_t *farmLongSavedHeader, uint8_t *farmStickyHeader,
    uint8_t *farmCurrentLog, uint8_t *farmFactoryLog, uint8_t *farmTimeSeriesLog, uint8_t *farmLongSavedLog, uint8_t *farmStickyLog, tSASLogpageSize logpageSize)
{
    eReturnValues returnValue = FAILURE;
    uint64_t startTimeInMilliSecs = 0;
    uint64_t endTimeInMilliSecs = 0;
    uint16_t numberOfDataSets = 0;
    uint16_t headerLength = FARMC_LOG_HEADER_LENGTH;
    uint32_t farmContentField = 0;
    uint32_t longSavedLogLength = 0;
    uint32_t timeSeriesLogLength = 0;
    uint32_t stickyLogLength = 0;

    //get Current FARM for SAS logpage 0x3D - feature 0x03
    if (is_FARM_Log_Supported(device))
    {
        startTimeInMilliSecs = get_Milliseconds_Since_Unix_Epoch();
        returnValue = get_SCSI_Log(device, SEAGATE_LP_FARM, SEAGATE_FARM_SP_CURRENT, M_NULLPTR, M_NULLPTR, true, farmCurrentLog, logpageSize.currentLog, M_NULLPTR);
        if (returnValue != SUCCESS)
        {
            //print error
            if (VERBOSITY_QUIET < device->deviceVerbosity)
            {
                printf("Error pulling Farm Current log\n");
            }
        }
        else
        {
            endTimeInMilliSecs = get_Milliseconds_Since_Unix_Epoch();
            addDataSetEntry(SUBPAGE_TYPE_FARM_CURRENT, farmCurrentHeader, &numberOfDataSets, &headerLength, &farmContentField, logpageSize.currentLog, startTimeInMilliSecs, endTimeInMilliSecs);

#ifdef  _DEBUG
            FILE *tempFile = fopen("farmcurrent.bin", "w+b");
            if (tempFile != M_NULLPTR)
            {
                if (fwrite(farmCurrentLog, sizeof(uint8_t), logpageSize.currentLog, tempFile) != C_CAST(size_t, logpageSize.currentLog)
                    || ferror(tempFile))
                {
                    printf("error in writing farmcurrent.bin file\n");
                    return ERROR_WRITING_FILE;
                }
                fclose(tempFile);
            }
#endif //  _DEBUG
        }
    }

    //get Factory FARM for SAS logpage 0x3D - feature 0x04
    if (is_Factory_FARM_Log_Supported(device))
    {
        startTimeInMilliSecs = get_Milliseconds_Since_Unix_Epoch();
        returnValue = get_SCSI_Log(device, SEAGATE_LP_FARM, SEAGATE_FARM_SP_FACTORY, M_NULLPTR, M_NULLPTR, true, farmFactoryLog, logpageSize.factoryLog, M_NULLPTR);
        if (returnValue != SUCCESS)
        {
            //print error
            if (VERBOSITY_QUIET < device->deviceVerbosity)
            {
                printf("Error pulling Farm Factory log\n");
            }
        }
        else
        {
            endTimeInMilliSecs = get_Milliseconds_Since_Unix_Epoch();
            addDataSetEntry(SUBPAGE_TYPE_FARM_FACTORY, farmFactoryHeader, &numberOfDataSets, &headerLength, &farmContentField, logpageSize.factoryLog, startTimeInMilliSecs, endTimeInMilliSecs);

#ifdef  _DEBUG
            FILE *tempFile = fopen("farmfactory.bin", "w+b");
            if (tempFile != M_NULLPTR)
            {
                if (fwrite(farmFactoryLog, sizeof(uint8_t), logpageSize.factoryLog, tempFile) != C_CAST(size_t, logpageSize.factoryLog)
                    || ferror(tempFile))
                {
                    printf("error in writing farmfactory.bin file\n");
                    return ERROR_WRITING_FILE;
                }
                fclose(tempFile);
            }
#endif //  _DEBUG
        }
    }

    //get Time Series FARM for SAS logpage 0x3D - 0x10 : 0x1F
    if (is_FARM_Time_Series_Log_Supported(device))
    {
        uint8_t *tempTimeSeries = farmTimeSeriesLog;
        bool isTimeSeriesPulled = false;
        startTimeInMilliSecs = get_Milliseconds_Since_Unix_Epoch();

        for (uint8_t i = SEAGATE_FARM_SP_TIME_SERIES_START; i <= SEAGATE_FARM_SP_TIME_SERIES_END; i++)
        {
            returnValue = get_SCSI_Log(device, SEAGATE_LP_FARM, i, M_NULLPTR, M_NULLPTR, true, tempTimeSeries, logpageSize.timeSeriesLog, M_NULLPTR);
            if (returnValue != SUCCESS)
            {
                //print error
                if (VERBOSITY_QUIET < device->deviceVerbosity)
                {
                    printf("Error pulling Farm TimeSeries log Subpage %d\n", i);
                }
            }
            else
            {
                //set the flag to true, which indicates atleast one timeseries frame was pulled
                isTimeSeriesPulled = true;
                tempTimeSeries = tempTimeSeries + logpageSize.timeSeriesLog;
                timeSeriesLogLength += logpageSize.timeSeriesLog;
            }
        }

        if (isTimeSeriesPulled)  //if atleast one time series frame was pulled then add into the dataset entry
        {
            endTimeInMilliSecs = get_Milliseconds_Since_Unix_Epoch();
            addDataSetEntry(SUBPAGE_TYPE_FARM_TIMESERIES, farmTimeSeriesHeader, &numberOfDataSets, &headerLength, &farmContentField, timeSeriesLogLength, startTimeInMilliSecs, endTimeInMilliSecs);

#ifdef _DEBUG
            FILE *tempFile = fopen("farmtimeseries.bin", "w+b");
            if (tempFile != M_NULLPTR)
            {
                printf("writing into farmtimeseries.bin file\n");
                if (fwrite(farmTimeSeriesLog, sizeof(uint8_t), timeSeriesLogLength, tempFile) != C_CAST(size_t, timeSeriesLogLength)
                    || ferror(tempFile))
                {
                    printf("error in writing farmtimeseries.bin file\n");
                    return ERROR_WRITING_FILE;
                }
                fclose(tempFile);
            }
#endif // DEBUG
        }
    }

    //get Long term saved Farm frames logpage 0x3D - 0xC0, 0xC1
    if (is_FARM_Long_Saved_Log_Supported(device))
    {
        uint8_t *tempLongSavedFarm = farmLongSavedLog;
        bool isLongSavedPulled = false;
        startTimeInMilliSecs = get_Milliseconds_Since_Unix_Epoch();

        //pull 0xC0
        returnValue = get_SCSI_Log(device, SEAGATE_LP_FARM, SEAGATE_FARM_SP_TIME_SERIES_ADD1, M_NULLPTR, M_NULLPTR, true, tempLongSavedFarm, logpageSize.longSavedLog, M_NULLPTR);
        if (returnValue != SUCCESS)
        {
            if (VERBOSITY_QUIET < device->deviceVerbosity)
            {
                printf("Error pulling Farm Saved log Subpage %d\n", SEAGATE_FARM_SP_TIME_SERIES_ADD1);
            }
        }
        else
        {
            //set the flag to true, which indicates atleast on saved frame was pulled
            isLongSavedPulled = true;
            tempLongSavedFarm = tempLongSavedFarm + logpageSize.longSavedLog;
            longSavedLogLength += logpageSize.longSavedLog;
        }

        //pull 0xC1
        returnValue = get_SCSI_Log(device, SEAGATE_LP_FARM, SEAGATE_FARM_SP_TIME_SERIES_ADD2, M_NULLPTR, M_NULLPTR, true, tempLongSavedFarm, logpageSize.longSavedLog, M_NULLPTR);
        if (returnValue != SUCCESS)
        {
            if (VERBOSITY_QUIET < device->deviceVerbosity)
            {
                printf("Error pulling Farm Long Saved log Subpage %d\n", SEAGATE_FARM_SP_TIME_SERIES_ADD2);
            }
        }
        else
        {
            //set the flag to true, which indicates atleast one saved frame was pulled
            isLongSavedPulled = true;
            tempLongSavedFarm = tempLongSavedFarm + logpageSize.longSavedLog;
            longSavedLogLength += logpageSize.longSavedLog;
        }

        if (isLongSavedPulled)  //if atleast one saved frame was pulled then add into the dataset entry
        {
            endTimeInMilliSecs = get_Milliseconds_Since_Unix_Epoch();
            addDataSetEntry(SUBPAGE_TYPE_FARM_LONG_SAVE, farmLongSavedHeader, &numberOfDataSets, &headerLength, &farmContentField, longSavedLogLength, startTimeInMilliSecs, endTimeInMilliSecs);

#ifdef _DEBUG
            FILE *tempFile = fopen("farmlongsaved.bin", "w+b");
            if (tempFile != M_NULLPTR)
            {
                if (fwrite(farmLongSavedLog, sizeof(uint8_t), longSavedLogLength, tempFile) != C_CAST(size_t, longSavedLogLength)
                    || ferror(tempFile))
                {
                    printf("error in writing farmlongsaved.bin file\n");
                    return ERROR_WRITING_FILE;
                }
                fclose(tempFile);
            }
#endif // DEBUG
        }
    }

    //Get Sticky FARM Log for SAS logpage 0x3D - feature 0xC2 - 0xC7
    if (is_FARM_Sticky_Log_Supported(device))
    {
        uint8_t *tempSticky = farmStickyLog;
        bool isStickyPulled = false;

        startTimeInMilliSecs = get_Milliseconds_Since_Unix_Epoch();
        for (uint8_t i = SEAGATE_FARM_SP_STICKY_START; i <= SEAGATE_FARM_SP_STICKY_END; i++)
        {
            returnValue = get_SCSI_Log(device, SEAGATE_LP_FARM, i, M_NULLPTR, M_NULLPTR, true, tempSticky, logpageSize.stickyLog, M_NULLPTR);
            if (returnValue != SUCCESS)
            {
                //print error
                if (VERBOSITY_QUIET < device->deviceVerbosity)
                {
                    printf("Error pulling Farm Sticky log Subpage %d\n", i);
                }
            }
            else
            {
                //set the flag to true, which indicates atleast one sticky frame was pulled
                isStickyPulled = true;
                tempSticky = tempSticky + logpageSize.stickyLog;
                stickyLogLength += logpageSize.stickyLog;
            }
        }

        if (isStickyPulled)
        {
            endTimeInMilliSecs = get_Milliseconds_Since_Unix_Epoch();
            addDataSetEntry(SUBPAGE_TYPE_FARM_STICKY, farmStickyHeader, &numberOfDataSets, &headerLength, &farmContentField, stickyLogLength, startTimeInMilliSecs, endTimeInMilliSecs);

#ifdef _DEBUG
            FILE *tempFile = fopen("farmsticky.bin", "w+b");
            if (tempFile != M_NULLPTR)
            {
                if (fwrite(farmStickyLog, sizeof(uint8_t), stickyLogLength, tempFile) != C_CAST(size_t, stickyLogLength)
                    || ferror(tempFile))
                {
                    printf("error in writing farmsticky.bin file\n");
                    return ERROR_WRITING_FILE;
                }
                fclose(tempFile);
            }
#endif // _DEBUG
        }
    }

    //update subheader entry
    uint32_t datasetOffset = headerLength;
    if (headerLength % DATASET_ENTRY_START_OFFSET)
    {
        datasetOffset = ((headerLength / DATASET_ENTRY_START_OFFSET) + UINT32_C(1)) * DATASET_ENTRY_START_OFFSET;
        zeroPaddingBufferSize->headerZeroPadding = datasetOffset - headerLength;
    }

    if (farmContentField & BIT0)
    {
        updateDataSetEntryOffset(farmCurrentHeader, datasetOffset);
        datasetOffset += logpageSize.currentLog;
        if (datasetOffset % DATASET_ENTRY_START_OFFSET)
        {
            zeroPaddingBufferSize->farmCurrentZeroPadding = ((datasetOffset / DATASET_ENTRY_START_OFFSET) + 1) * DATASET_ENTRY_START_OFFSET - datasetOffset;
            datasetOffset += zeroPaddingBufferSize->farmCurrentZeroPadding;
        }
    }

    if (farmContentField & BIT1)
    {
        updateDataSetEntryOffset(farmFactoryHeader, datasetOffset);
        datasetOffset += logpageSize.factoryLog;
        if (datasetOffset % DATASET_ENTRY_START_OFFSET)
        {
            zeroPaddingBufferSize->farmFactoryZeroPadding = ((datasetOffset / DATASET_ENTRY_START_OFFSET) + 1) * DATASET_ENTRY_START_OFFSET - datasetOffset;
            datasetOffset += zeroPaddingBufferSize->farmFactoryZeroPadding;
        }
    }

    if (farmContentField & BIT2)
    {
        updateDataSetEntryOffset(farmTimeSeriesHeader, datasetOffset);
        datasetOffset += timeSeriesLogLength;
        if (datasetOffset % DATASET_ENTRY_START_OFFSET)
        {
            zeroPaddingBufferSize->farmTimeSeriesZeroPadding = ((datasetOffset / DATASET_ENTRY_START_OFFSET) + 1) * DATASET_ENTRY_START_OFFSET - datasetOffset;
            datasetOffset += zeroPaddingBufferSize->farmTimeSeriesZeroPadding;
        }
    }

    if (farmContentField & BIT6)
    {
        updateDataSetEntryOffset(farmLongSavedHeader, datasetOffset);
        datasetOffset += longSavedLogLength;
        if (datasetOffset % DATASET_ENTRY_START_OFFSET)
        {
            zeroPaddingBufferSize->farmLongSavedZeroPadding = ((datasetOffset / DATASET_ENTRY_START_OFFSET) + 1) * DATASET_ENTRY_START_OFFSET - datasetOffset;
            datasetOffset += zeroPaddingBufferSize->farmLongSavedZeroPadding;
        }
    }

    if (farmContentField & BIT3)
    {
        updateDataSetEntryOffset(farmStickyHeader, datasetOffset);
        datasetOffset += stickyLogLength;
    }

    //copy remaing fields for header information
    memcpy(header + 116, &headerLength, sizeof(uint16_t));
    memcpy(header + 118, &farmContentField, sizeof(uint32_t));
    memcpy(header + 252, &numberOfDataSets, sizeof(uint16_t));

    return SUCCESS;
}

static eReturnValues write_FARM_Zero_Padding(uint32_t paddingSize, secureFileInfo *farmFile)
{
    eReturnValues returnValue = SUCCESS;
    if (paddingSize > 0)
    {
        size_t zeroPaddingLen = uint32_to_sizet(paddingSize);
        uint8_t* zeroPadding = C_CAST(uint8_t*, safe_calloc(zeroPaddingLen, sizeof(uint8_t)));
        if (zeroPadding)
        {
            if (SEC_FILE_SUCCESS != secure_Write_File(farmFile, zeroPadding, zeroPaddingLen, sizeof(uint8_t), zeroPaddingLen, M_NULLPTR))
            {
                if (SEC_FILE_SUCCESS != secure_Close_File(farmFile))
                {
                    printf("Error closing file!\n");
                }
                free_Secure_File_Info(&farmFile);
                returnValue = ERROR_WRITING_FILE;
            }
        }
        else
        {
            if (SEC_FILE_SUCCESS != secure_Close_File(farmFile))
            {
                printf("Error closing file!\n");
            }
            free_Secure_File_Info(&farmFile);
            returnValue = MEMORY_FAILURE;
        }
        safe_free(&zeroPadding);
    }
    return returnValue;
}

eReturnValues pull_FARM_Combined_Log(tDevice *device, const char * const filePath, uint32_t transferSizeBytes, int sataFarmCopyType)
{
    eReturnValues returnValue = NOT_SUPPORTED;

    if (device->drive_info.drive_type == ATA_DRIVE)
    {
        if (!(is_FARM_Log_Supported(device) || is_FARM_Time_Series_Log_Supported(device))) //No farm or farm timeseries supported, then return
        {
            return returnValue;
        }
    }
    else if (device->drive_info.drive_type == SCSI_DRIVE)
    {
        if (!(is_FARM_Log_Supported(device) || is_Factory_FARM_Log_Supported(device) || is_FARM_Long_Saved_Log_Supported(device) || is_FARM_Time_Series_Log_Supported(device) || is_FARM_Sticky_Log_Supported(device))) //No farm or farm timeseries supported, then return
        {
            return returnValue;
        }
    }
    else
    {
        return returnValue;
    }

    DECLARE_ZERO_INIT_ARRAY(uint8_t, header, FARMC_LOG_HEADER_LENGTH);
    DECLARE_ZERO_INIT_ARRAY(uint8_t, farmCurrentHeader, FARMC_LOG_DATA_SET_HEADER_LENGTH);
    DECLARE_ZERO_INIT_ARRAY(uint8_t, farmFactoryHeader, FARMC_LOG_DATA_SET_HEADER_LENGTH);
    DECLARE_ZERO_INIT_ARRAY(uint8_t, farmSavedHeader, FARMC_LOG_DATA_SET_HEADER_LENGTH);
    DECLARE_ZERO_INIT_ARRAY(uint8_t, farmTimeSeriesHeader, FARMC_LOG_DATA_SET_HEADER_LENGTH);
    DECLARE_ZERO_INIT_ARRAY(uint8_t, farmLongSavedHeader, FARMC_LOG_DATA_SET_HEADER_LENGTH);
    DECLARE_ZERO_INIT_ARRAY(uint8_t, farmStickyHeader, FARMC_LOG_DATA_SET_HEADER_LENGTH);
    DECLARE_ZERO_INIT_ARRAY(uint8_t, farmWorkLoadTraceHeader, FARMC_LOG_DATA_SET_HEADER_LENGTH);
    uint8_t *farmCurrentLog = M_NULLPTR;
    uint8_t *farmFactoryLog = M_NULLPTR;
    uint8_t *farmSavedLog = M_NULLPTR;
    uint8_t *farmTimeSeriesLog = M_NULLPTR;
    uint8_t *farmLongSavedLog = M_NULLPTR;
    uint8_t *farmStickyLog = M_NULLPTR;
    uint8_t *farmWorkLoadTraceLog = M_NULLPTR;
    uint8_t *farmCurrentZeroPaddingBuffer = M_NULLPTR;
    uint8_t *farmFactoryZeroPaddingBuffer = M_NULLPTR;
    uint8_t *farmSavedZeroPaddingBuffer = M_NULLPTR;
    uint8_t *farmTimeSeriesZeroPaddingBuffer = M_NULLPTR;
    uint8_t *farmLongSavedZeroPaddingBuffer = M_NULLPTR;
    uint8_t *farmStickyZeroPaddingBuffer = M_NULLPTR;
    tZeroPaddingBufferSize zeroPaddingBufferSize;
    memset(&zeroPaddingBufferSize, 0, sizeof(tZeroPaddingBufferSize));

    //set signature
    DECLARE_ZERO_INIT_ARRAY(char, signature, FARM_SIGNATURE_LENGTH + 1);
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
    DECLARE_ZERO_INIT_ARRAY(char, interfaceType, 4 + 1);
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
    DECLARE_ZERO_INIT_ARRAY(char, modelNumber, MODEL_NUM_LEN + 1);
    snprintf(modelNumber, MODEL_NUM_LEN + 1, "%-*s", MODEL_NUM_LEN, device->drive_info.product_identification);
    memcpy(header + 32, &modelNumber, MODEL_NUM_LEN);

    //set serial#
    DECLARE_ZERO_INIT_ARRAY(char, serialNumber, SERIAL_NUM_LEN + 1);
    snprintf(serialNumber, SERIAL_NUM_LEN + 1, "%-*s", SERIAL_NUM_LEN, device->drive_info.serialNumber);
    memcpy(header + 80, &serialNumber, SERIAL_NUM_LEN);

    //set firmware revision
    DECLARE_ZERO_INIT_ARRAY(char, firmwareVersion, FW_REV_LEN + 1);
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
            farmCurrentLog = C_CAST(uint8_t*, safe_calloc_aligned(ATA_FARM_LOG_PAGE_SIZE, sizeof(uint8_t), device->os_info.minimumAlignment));               //96KB
            farmFactoryLog = C_CAST(uint8_t*, safe_calloc_aligned(ATA_FARM_LOG_PAGE_SIZE, sizeof(uint8_t), device->os_info.minimumAlignment));               //96KB
            farmSavedLog = C_CAST(uint8_t*, safe_calloc_aligned(ATA_FARM_LOG_PAGE_SIZE, sizeof(uint8_t), device->os_info.minimumAlignment));                 //96KB
            farmTimeSeriesLog = C_CAST(uint8_t*, safe_calloc_aligned(16 * ATA_FARM_LOG_PAGE_SIZE, sizeof(uint8_t), device->os_info.minimumAlignment));       //16 * 96KB
            farmLongSavedLog = C_CAST(uint8_t*, safe_calloc_aligned(2 * ATA_FARM_LOG_PAGE_SIZE, sizeof(uint8_t), device->os_info.minimumAlignment));         //2 * 96KB
            farmStickyLog = C_CAST(uint8_t*, safe_calloc_aligned(6 * ATA_FARM_LOG_PAGE_SIZE, sizeof(uint8_t), device->os_info.minimumAlignment));            //6 * 96KB
            farmWorkLoadTraceLog = C_CAST(uint8_t*, safe_calloc_aligned(ATA_WORKLOAD_TRACE_PAGE_SIZE, sizeof(uint8_t), device->os_info.minimumAlignment));   //2048KB
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
            if (!farmLongSavedLog)
            {
                if (device->deviceVerbosity > VERBOSITY_QUIET)
                {
                    perror("error memory allocation for farmLongSavedLog");
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
            returnValue = pullATAFarmLogs(device, transferSizeBytes, sataFarmCopyType, header, &zeroPaddingBufferSize,
                farmCurrentHeader, farmFactoryHeader, farmSavedHeader, farmTimeSeriesHeader, farmLongSavedHeader, farmStickyHeader, farmWorkLoadTraceHeader,
                farmCurrentLog, farmFactoryLog, farmSavedLog, farmTimeSeriesLog, farmLongSavedLog, farmStickyLog, farmWorkLoadTraceLog);
            if (returnValue != SUCCESS)
            {
                break;
            }
        }
        else if (device->drive_info.drive_type == SCSI_DRIVE)
        {
            uint32_t logSize = UINT32_C(0);
            tSASLogpageSize logpageSize = { UINT32_C(0), UINT32_C(0), UINT32_C(0), UINT32_C(0), UINT32_C(0) };

            //get the log size of all log subpages
            if (is_FARM_Log_Supported(device))
            {
                //get length of FARM current page
                returnValue = get_SCSI_Log_Size(device, SEAGATE_LP_FARM, SEAGATE_FARM_SP_CURRENT, &logSize);
                logpageSize.currentLog = logSize;
                farmCurrentLog = C_CAST(uint8_t*, safe_calloc_aligned(logpageSize.currentLog, sizeof(uint8_t), device->os_info.minimumAlignment));
                if (!farmCurrentLog)
                {
                    if (device->deviceVerbosity > VERBOSITY_QUIET)
                    {
                        perror("error memory allocation for farmCurrentLog");
                    }
                    return MEMORY_FAILURE;
                }
            }

            if (is_Factory_FARM_Log_Supported(device))
            {
                //get length of Factory FARM page
                returnValue = get_SCSI_Log_Size(device, SEAGATE_LP_FARM, SEAGATE_FARM_SP_FACTORY, &logSize);
                logpageSize.factoryLog = logSize;
                farmFactoryLog = C_CAST(uint8_t*, safe_calloc_aligned(logpageSize.factoryLog, sizeof(uint8_t), device->os_info.minimumAlignment));
                if (!farmFactoryLog)
                {
                    if (device->deviceVerbosity > VERBOSITY_QUIET)
                    {
                        perror("error memory allocation for farmFactoryLog");
                    }
                    return MEMORY_FAILURE;
                }
            }

            if (is_FARM_Time_Series_Log_Supported(device))
            {
                //get length of FARM time series log 
                returnValue = get_SCSI_Log_Size(device, SEAGATE_LP_FARM, SEAGATE_FARM_SP_TIME_SERIES_START, &logSize);
                logpageSize.timeSeriesLog = logSize;
                farmTimeSeriesLog = C_CAST(uint8_t*, safe_calloc_aligned((logpageSize.timeSeriesLog * 16), sizeof(uint8_t), device->os_info.minimumAlignment));
                if (!farmTimeSeriesLog)
                {
                    if (device->deviceVerbosity > VERBOSITY_QUIET)
                    {
                        perror("error memory allocation for farmTimeSeriesLog");
                    }
                    return MEMORY_FAILURE;
                }
            }

            if (is_FARM_Long_Saved_Log_Supported(device))
            {
                //get length of Farm Long saved frames
                returnValue = get_SCSI_Log_Size(device, SEAGATE_LP_FARM, SEAGATE_FARM_SP_TIME_SERIES_ADD1, &logSize);
                logpageSize.longSavedLog = logSize;
                farmLongSavedLog = C_CAST(uint8_t*, safe_calloc_aligned((logpageSize.longSavedLog * 2), sizeof(uint8_t), device->os_info.minimumAlignment));
                if (!farmLongSavedLog)
                {
                    if (device->deviceVerbosity > VERBOSITY_QUIET)
                    {
                        perror("error memory allocation for farmLongSavedLog");
                    }
                    return MEMORY_FAILURE;
                }
            }

            if (is_FARM_Sticky_Log_Supported(device))
            {
                //get length of FARM sticky log
                returnValue = get_SCSI_Log_Size(device, SEAGATE_LP_FARM, SEAGATE_FARM_SP_STICKY_START, &logSize);
                logpageSize.stickyLog = logSize;
                farmStickyLog = C_CAST(uint8_t*, safe_calloc_aligned((logpageSize.stickyLog * 6), sizeof(uint8_t), device->os_info.minimumAlignment));
                if (!farmStickyLog)
                {
                    if (device->deviceVerbosity > VERBOSITY_QUIET)
                    {
                        perror("error memory allocation for farmStickyLog");
                    }
                    return MEMORY_FAILURE;
                }
            }

            //pull the FARM SCSi log 
            returnValue = pullSCSIFarmLogs(device, header, &zeroPaddingBufferSize,
                farmCurrentHeader, farmFactoryHeader, farmTimeSeriesHeader, farmLongSavedHeader, farmStickyHeader,
                farmCurrentLog, farmFactoryLog, farmTimeSeriesLog, farmLongSavedLog, farmStickyLog, logpageSize);
            if (returnValue != SUCCESS)
            {
                break;
            }
        }
        else
        {
            return NOT_SUPPORTED;
        }

        uint32_t farmContentField = C_CAST(uint32_t, header[118]);
        if (!((farmContentField & BIT0) || (farmContentField & BIT1) || (farmContentField & BIT2) || (farmContentField & BIT3) || (farmContentField & BIT4) || (farmContentField & BIT5) || (farmContentField & BIT6)))
        {
            if (device->deviceVerbosity > VERBOSITY_QUIET)
            {
                printf("No Farm log available for this drive.\n");
            }
            returnValue = FAILURE;
            break;
        }

        //create and open the log file
        secureFileInfo *farmCombinedLog = M_NULLPTR;
        if (SUCCESS != create_And_Open_Secure_Log_File_Dev_EZ(device, &farmCombinedLog, NAMING_SERIAL_NUMBER_DATE_TIME, filePath, "FARMC", "frmc"))
        {
            if (device->deviceVerbosity > VERBOSITY_QUIET)
            {
                perror("Error in opening the file!\n");
            }
            returnValue = FILE_OPEN_ERROR;
            break;
        }

        //write header 
        if (SEC_FILE_SUCCESS != secure_Write_File(farmCombinedLog, header,FARMC_LOG_HEADER_LENGTH, sizeof(uint8_t), FARMC_LOG_HEADER_LENGTH, M_NULLPTR))
        {
            if (device->deviceVerbosity > VERBOSITY_QUIET)
            {
                perror("Error in writing header data to a file!\n");
            }
            if (SEC_FILE_SUCCESS != secure_Close_File(farmCombinedLog))
            {
                printf("Error closing file!\n");
            }
            free_Secure_File_Info(&farmCombinedLog);
            returnValue = ERROR_WRITING_FILE;
            break;
        }

        //write dataset entry header
        {
            if (farmContentField & BIT0)
            {
                if (SEC_FILE_SUCCESS != secure_Write_File(farmCombinedLog, farmCurrentHeader, FARMC_LOG_DATA_SET_HEADER_LENGTH, sizeof(uint8_t), FARMC_LOG_DATA_SET_HEADER_LENGTH, M_NULLPTR))
                {
                    if (device->deviceVerbosity > VERBOSITY_QUIET)
                    {
                        perror("Error in writing farm current header data to a file!\n");
                    }
                    if (SEC_FILE_SUCCESS != secure_Close_File(farmCombinedLog))
                    {
                        printf("Error closing file!\n");
                    }
                    free_Secure_File_Info(&farmCombinedLog);
                    returnValue = ERROR_WRITING_FILE;
                    break;
                }
            }

            if (farmContentField & BIT1)
            {
                if (SEC_FILE_SUCCESS != secure_Write_File(farmCombinedLog, farmFactoryHeader, FARMC_LOG_DATA_SET_HEADER_LENGTH, sizeof(uint8_t), FARMC_LOG_DATA_SET_HEADER_LENGTH, M_NULLPTR))
                {
                    if (device->deviceVerbosity > VERBOSITY_QUIET)
                    {
                        perror("Error in writing farm factory header data to a file!\n");
                    }
                    if (SEC_FILE_SUCCESS != secure_Close_File(farmCombinedLog))
                    {
                        printf("Error closing file!\n");
                    }
                    free_Secure_File_Info(&farmCombinedLog);
                    returnValue = ERROR_WRITING_FILE;
                    break;
                }
            }

            if (farmContentField & BIT5)
            {
                if (SEC_FILE_SUCCESS != secure_Write_File(farmCombinedLog, farmSavedHeader, FARMC_LOG_DATA_SET_HEADER_LENGTH, sizeof(uint8_t), FARMC_LOG_DATA_SET_HEADER_LENGTH, M_NULLPTR))
                {
                    if (device->deviceVerbosity > VERBOSITY_QUIET)
                    {
                        perror("Error in writing farm saved header data to a file!\n");
                    }
                    if (SEC_FILE_SUCCESS != secure_Close_File(farmCombinedLog))
                    {
                        printf("Error closing file!\n");
                    }
                    free_Secure_File_Info(&farmCombinedLog);
                    returnValue = ERROR_WRITING_FILE;
                    break;
                }
            }

            if (farmContentField & BIT2)
            {
                if (SEC_FILE_SUCCESS != secure_Write_File(farmCombinedLog, farmTimeSeriesHeader, FARMC_LOG_DATA_SET_HEADER_LENGTH, sizeof(uint8_t), FARMC_LOG_DATA_SET_HEADER_LENGTH, M_NULLPTR))
                {
                    if (device->deviceVerbosity > VERBOSITY_QUIET)
                    {
                        perror("Error in writing farm time series header data to a file!\n");
                    }
                    if (SEC_FILE_SUCCESS != secure_Close_File(farmCombinedLog))
                    {
                        printf("Error closing file!\n");
                    }
                    free_Secure_File_Info(&farmCombinedLog);
                    returnValue = ERROR_WRITING_FILE;
                    break;
                }
            }

            if (farmContentField & BIT6)
            {
                if (SEC_FILE_SUCCESS != secure_Write_File(farmCombinedLog, farmLongSavedHeader, FARMC_LOG_DATA_SET_HEADER_LENGTH, sizeof(uint8_t), FARMC_LOG_DATA_SET_HEADER_LENGTH, M_NULLPTR))
                {
                    if (device->deviceVerbosity > VERBOSITY_QUIET)
                    {
                        perror("Error in writing farm long term saved header data to a file!\n");
                    }
                    if (SEC_FILE_SUCCESS != secure_Close_File(farmCombinedLog))
                    {
                        printf("Error closing file!\n");
                    }
                    free_Secure_File_Info(&farmCombinedLog);
                    returnValue = ERROR_WRITING_FILE;
                    break;
                }
            }

            if (farmContentField & BIT3)
            {
                if (SEC_FILE_SUCCESS != secure_Write_File(farmCombinedLog, farmStickyHeader, FARMC_LOG_DATA_SET_HEADER_LENGTH, sizeof(uint8_t), FARMC_LOG_DATA_SET_HEADER_LENGTH, M_NULLPTR))
                {
                    if (device->deviceVerbosity > VERBOSITY_QUIET)
                    {
                        perror("Error in writing farm sticky frame header data to a file!\n");
                    }
                    if (SEC_FILE_SUCCESS != secure_Close_File(farmCombinedLog))
                    {
                        printf("Error closing file!\n");
                    }
                    free_Secure_File_Info(&farmCombinedLog);
                    returnValue = ERROR_WRITING_FILE;
                    break;
                }
            }

            if (farmContentField & BIT4)
            {
                if (SEC_FILE_SUCCESS != secure_Write_File(farmCombinedLog, farmWorkLoadTraceHeader, FARMC_LOG_DATA_SET_HEADER_LENGTH, sizeof(uint8_t), FARMC_LOG_DATA_SET_HEADER_LENGTH, M_NULLPTR))
                {
                    if (device->deviceVerbosity > VERBOSITY_QUIET)
                    {
                        perror("Error in writing farm workload trace header data to a file!\n");
                    }
                    if (SEC_FILE_SUCCESS != secure_Close_File(farmCombinedLog))
                    {
                        printf("Error closing file!\n");
                    }
                    free_Secure_File_Info(&farmCombinedLog);
                    returnValue = ERROR_WRITING_FILE;
                    break;
                }
            }

            //write zero padding
            returnValue = write_FARM_Zero_Padding(zeroPaddingBufferSize.headerZeroPadding, farmCombinedLog);
            if (returnValue != SUCCESS)
            {
                break;
            }
        }

        //write log buffer
        {
            if (farmContentField & BIT0)
            {
                uint32_t *datasetlength = C_CAST(uint32_t*, farmCurrentHeader + 12);
                if (SEC_FILE_SUCCESS != secure_Write_File(farmCombinedLog, farmCurrentLog, *datasetlength, sizeof(uint8_t), *datasetlength, M_NULLPTR))
                {
                    if (device->deviceVerbosity > VERBOSITY_QUIET)
                    {
                        perror("Error in writing farm current log data to a file!\n");
                    }
                    if (SEC_FILE_SUCCESS != secure_Close_File(farmCombinedLog))
                    {
                        printf("Error closing file!\n");
                    }
                    free_Secure_File_Info(&farmCombinedLog);
                    returnValue = ERROR_WRITING_FILE;
                    break;
                }

                //add zero padding
                returnValue = write_FARM_Zero_Padding(zeroPaddingBufferSize.farmCurrentZeroPadding, farmCombinedLog);
                if (returnValue != SUCCESS)
                {
                    break;
                }
            }

            if (farmContentField & BIT1)
            {
                uint32_t *datasetlength = C_CAST(uint32_t*, farmFactoryHeader + 12);
                if (SEC_FILE_SUCCESS != secure_Write_File(farmCombinedLog, farmFactoryLog, *datasetlength, sizeof(uint8_t), *datasetlength, M_NULLPTR))
                {
                    if (device->deviceVerbosity > VERBOSITY_QUIET)
                    {
                        perror("Error in writing farm factory log data to a file!\n");
                    }
                    if (SEC_FILE_SUCCESS != secure_Close_File(farmCombinedLog))
                    {
                        printf("Error closing file!\n");
                    }
                    free_Secure_File_Info(&farmCombinedLog);
                    returnValue = ERROR_WRITING_FILE;
                    break;
                }

                //add zero padding
                returnValue = write_FARM_Zero_Padding(zeroPaddingBufferSize.farmFactoryZeroPadding, farmCombinedLog);
                if (returnValue != SUCCESS)
                {
                    break;
                }
            }

            if (farmContentField & BIT5)
            {
                uint32_t *datasetlength = C_CAST(uint32_t*, farmSavedHeader + 12);
                if (SEC_FILE_SUCCESS != secure_Write_File(farmCombinedLog, farmSavedLog, *datasetlength, sizeof(uint8_t), *datasetlength, M_NULLPTR))
                {
                    if (device->deviceVerbosity > VERBOSITY_QUIET)
                    {
                        perror("Error in writing farm save log data to a file!\n");
                    }
                    if (SEC_FILE_SUCCESS != secure_Close_File(farmCombinedLog))
                    {
                        printf("Error closing file!\n");
                    }
                    free_Secure_File_Info(&farmCombinedLog);
                    returnValue = ERROR_WRITING_FILE;
                    break;
                }

                //add zero padding
                returnValue = write_FARM_Zero_Padding(zeroPaddingBufferSize.farmSavedZeroPadding, farmCombinedLog);
                if (returnValue != SUCCESS)
                {
                    break;
                }
            }

            if (farmContentField & BIT2)
            {
                uint32_t *datasetlength = C_CAST(uint32_t*, farmTimeSeriesHeader + 12);
                if (SEC_FILE_SUCCESS != secure_Write_File(farmCombinedLog, farmTimeSeriesLog, *datasetlength, sizeof(uint8_t), *datasetlength, M_NULLPTR))
                {
                    if (device->deviceVerbosity > VERBOSITY_QUIET)
                    {
                        perror("Error in writing farm time series frame log data to a file!\n");
                    }
                    if (SEC_FILE_SUCCESS != secure_Close_File(farmCombinedLog))
                    {
                        printf("Error closing file!\n");
                    }
                    free_Secure_File_Info(&farmCombinedLog);
                    returnValue = ERROR_WRITING_FILE;
                    break;
                }

                //add zero padding
                returnValue = write_FARM_Zero_Padding(zeroPaddingBufferSize.farmTimeSeriesZeroPadding, farmCombinedLog);
                if (returnValue != SUCCESS)
                {
                    break;
                }
            }

            if (farmContentField & BIT6)
            {
                uint32_t *datasetlength = C_CAST(uint32_t*, farmLongSavedHeader + 12);
                if (SEC_FILE_SUCCESS != secure_Write_File(farmCombinedLog, farmLongSavedLog, *datasetlength, sizeof(uint8_t), *datasetlength, M_NULLPTR))
                {
                    if (device->deviceVerbosity > VERBOSITY_QUIET)
                    {
                        perror("Error in writing farm long term saved frame log data to a file!\n");
                    }
                    if (SEC_FILE_SUCCESS != secure_Close_File(farmCombinedLog))
                    {
                        printf("Error closing file!\n");
                    }
                    free_Secure_File_Info(&farmCombinedLog);
                    returnValue = ERROR_WRITING_FILE;
                    break;
                }

                //add zero padding
                returnValue = write_FARM_Zero_Padding(zeroPaddingBufferSize.farmLongSavedZeroPadding, farmCombinedLog);
                if (returnValue != SUCCESS)
                {
                    break;
                }
            }

            if (farmContentField & BIT3)
            {
                uint32_t *datasetlength = C_CAST(uint32_t*, farmStickyHeader + 12);
                if (SEC_FILE_SUCCESS != secure_Write_File(farmCombinedLog, farmStickyLog, *datasetlength, sizeof(uint8_t), *datasetlength, M_NULLPTR))
                {
                    if (device->deviceVerbosity > VERBOSITY_QUIET)
                    {
                        perror("Error in writing farm sticky frame log data to a file!\n");
                    }
                    if (SEC_FILE_SUCCESS != secure_Close_File(farmCombinedLog))
                    {
                        printf("Error closing file!\n");
                    }
                    free_Secure_File_Info(&farmCombinedLog);
                    returnValue = ERROR_WRITING_FILE;
                    break;
                }

                //add zero padding
                returnValue = write_FARM_Zero_Padding(zeroPaddingBufferSize.farmStickyZeroPadding, farmCombinedLog);
                if (returnValue != SUCCESS)
                {
                    break;
                }
            }

            if (farmContentField & BIT4)
            {
                uint32_t *datasetlength = C_CAST(uint32_t*, farmWorkLoadTraceHeader + 12);
                if (SEC_FILE_SUCCESS != secure_Write_File(farmCombinedLog, farmWorkLoadTraceLog, *datasetlength, sizeof(uint8_t), *datasetlength, M_NULLPTR))
                {
                    if (device->deviceVerbosity > VERBOSITY_QUIET)
                    {
                        perror("Error in writing farm workload trace log data to a file!\n");
                    }
                    if (SEC_FILE_SUCCESS != secure_Close_File(farmCombinedLog))
                    {
                        printf("Error closing file!\n");
                    }
                    free_Secure_File_Info(&farmCombinedLog);
                    returnValue = ERROR_WRITING_FILE;
                    break;
                }
            }
        }

        if (SEC_FILE_SUCCESS != secure_Flush_File(farmCombinedLog))
        {
            if (device->deviceVerbosity > VERBOSITY_QUIET)
            {
                perror("Error in flushing data!\n");
            }
                    if (SEC_FILE_SUCCESS != secure_Close_File(farmCombinedLog))
                    {
                        printf("Error closing file!\n");
                    }
            free_Secure_File_Info(&farmCombinedLog);
            returnValue = ERROR_WRITING_FILE;
            break;
        }

        if (SEC_FILE_SUCCESS != secure_Close_File(farmCombinedLog))
        {
            printf("Error closing file!\n");
        }
        free_Secure_File_Info(&farmCombinedLog);
    } while (0);

    safe_free_aligned(&farmCurrentLog);
    safe_free_aligned(&farmFactoryLog);
    safe_free_aligned(&farmSavedLog);
    safe_free_aligned(&farmTimeSeriesLog);
    safe_free_aligned(&farmLongSavedLog);
    safe_free_aligned(&farmStickyLog);
    safe_free_aligned(&farmWorkLoadTraceLog);
    safe_free_aligned(&farmCurrentZeroPaddingBuffer);
    safe_free_aligned(&farmFactoryZeroPaddingBuffer);
    safe_free_aligned(&farmSavedZeroPaddingBuffer);
    safe_free_aligned(&farmTimeSeriesZeroPaddingBuffer);
    safe_free_aligned(&farmLongSavedZeroPaddingBuffer);
    safe_free_aligned(&farmStickyZeroPaddingBuffer);

    return returnValue;
}
