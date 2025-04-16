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

#include "bit_manip.h"
#include "code_attributes.h"
#include "common_types.h"
#include "error_translation.h"
#include "io_utils.h"
#include "math_utils.h"
#include "memory_safety.h"
#include "precision_timer.h"
#include "secure_file.h"
#include "string_utils.h"
#include "time_utils.h"
#include "type_conversion.h"

#include "farm_log.h"
#include "logs.h"
#include "vendor/seagate/seagate_ata_types.h"
#include "vendor/seagate/seagate_scsi_types.h"

#define FARMC_LOG_HEADER_LENGTH          UINT16_C(256)
#define FARMC_LOG_DATA_SET_HEADER_LENGTH 32
#define ATA_FARM_LOG_PAGE_SIZE           (UINT32_C(96) * UINT32_C(1024))                // 96 KB
#define ATA_WORKLOAD_TRACE_PAGE_SIZE     (UINT32_C(2048) * UINT32_C(1024))              // 2048 KB
#define ATA_TIMESERIES_FRAME_LOG_SIZE    (UINT32_C(27) * UINT32_C(96) * UINT32_C(1024)) // 27 * 96 KB
#define FARM_SIGNATURE_LENGTH            16
#define FARM_DATASET_SIGNATURE_LENGTH    8
#define DATASET_ENTRY_START_OFFSET       512

#define FARM_TIME_SERIES_PAGES           UINT32_C(16)
#define FARM_LONG_SAVED_PAGES            UINT32_C(2)
#define FARM_STICKY_PAGES                UINT32_C(6)

#define FARMC_LOG_MAJOR_VERSION          UINT16_C(1)
#define FARMC_LOG_MINOR_VERSION          UINT16_C(1)
#define FARMC_LOG_PATCH_VERSION          UINT16_C(0)

#define FARMC_SIGNATURE_ID               "STX_FARM_COMBINE"

typedef enum eFarmSubPageTypeEnum
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

typedef struct s_tSASLogpageSize
{
    uint32_t currentLog;
    uint32_t factoryLog;
    uint32_t timeSeriesLog; // each frame size
    uint32_t longSavedLog;  // each frame size
    uint32_t stickyLog;     // each frame size
} tSASLogpageSize;

typedef struct s_tZeroPaddingBufferSize
{
    uint32_t headerZeroPadding;
    uint32_t farmCurrentZeroPadding;
    uint32_t farmFactoryZeroPadding;
    uint32_t farmSavedZeroPadding;
    uint32_t farmTimeSeriesZeroPadding;
    uint32_t farmLongSavedZeroPadding;
    uint32_t farmStickyZeroPadding;
} tZeroPaddingBufferSize;

char farmSubPageSignatureId[][FARM_DATASET_SIGNATURE_LENGTH + 1] = {"FARM",     "FACTORY",  "FARMTIME", "FARMSTCK",
                                                                    "WORKLDTC", "FARMSAVE", "FARMLONG"};

static void addDataSetEntry(int32_t   subPageType,
                            uint8_t*  dataSetHeader,
                            uint16_t* numberOfDataSets,
                            uint16_t* headerLength,
                            uint32_t* farmContentField,
                            uint32_t  dataSetLength,
                            uint64_t  startTimeStamp,
                            uint64_t  endTimeStamp)
{
    // farm current signature
    DECLARE_ZERO_INIT_ARRAY(char, signature, FARM_DATASET_SIGNATURE_LENGTH + 1);
    snprintf_err_handle(signature, FARM_DATASET_SIGNATURE_LENGTH + 1, "%-*s", FARM_DATASET_SIGNATURE_LENGTH,
                        farmSubPageSignatureId[subPageType]);
    safe_memcpy(dataSetHeader, FARMC_LOG_DATA_SET_HEADER_LENGTH, &signature, FARM_DATASET_SIGNATURE_LENGTH);
    safe_memcpy(dataSetHeader + 12, FARMC_LOG_DATA_SET_HEADER_LENGTH - 12, &dataSetLength, sizeof(uint32_t));
    safe_memcpy(dataSetHeader + 16, FARMC_LOG_DATA_SET_HEADER_LENGTH - 16, &startTimeStamp, sizeof(uint64_t));
    safe_memcpy(dataSetHeader + 24, FARMC_LOG_DATA_SET_HEADER_LENGTH - 24, &endTimeStamp, sizeof(uint64_t));

    *numberOfDataSets += 1;
    *headerLength += FARMC_LOG_DATA_SET_HEADER_LENGTH;
    *farmContentField |= M_BitN(subPageType);
}

M_NONNULL_PARAM_LIST(1)
M_PARAM_RW(1) static void updateDataSetEntryOffset(uint8_t* dataSetHeader, uint32_t dataSetOffset)
{
    safe_memcpy(dataSetHeader + 8, FARMC_LOG_DATA_SET_HEADER_LENGTH - 8, &dataSetOffset, sizeof(uint32_t));
}

typedef struct s_farmPtrAndLen
{
    uint8_t* ptr;
    size_t   alloclen;
} farmPtrAndLen;

static eReturnValues pullATAFarmLogs(tDevice*                device,
                                     uint32_t                transferSizeBytes,
                                     int                     sataFarmCopyType,
                                     uint8_t*                header,
                                     tZeroPaddingBufferSize* zeroPaddingBufferSize,
                                     uint8_t*                farmCurrentHeader,
                                     uint8_t*                farmFactoryHeader,
                                     uint8_t*                farmSavedHeader,
                                     uint8_t*                farmTimeSeriesHeader,
                                     uint8_t*                farmLongSavedHeader,
                                     uint8_t*                farmStickyHeader,
                                     uint8_t*                farmWorkLoadTraceHeader,
                                     farmPtrAndLen           farmCurrentLog,
                                     farmPtrAndLen           farmFactoryLog,
                                     farmPtrAndLen           farmSavedLog,
                                     farmPtrAndLen           farmTimeSeriesLog,
                                     farmPtrAndLen           farmLongSavedLog,
                                     farmPtrAndLen           farmStickyLog,
                                     farmPtrAndLen           farmWorkLoadTraceLog)
{
    uint64_t startTimeInMilliSecs = UINT64_C(0);
    uint64_t endTimeInMilliSecs   = UINT64_C(0);
    uint16_t numberOfDataSets     = UINT16_C(0);
    uint16_t headerLength         = FARMC_LOG_HEADER_LENGTH;
    uint32_t farmContentField     = UINT32_C(0);

    if (sataFarmCopyType == SATA_FARM_COPY_TYPE_DISC)
    {
        if (is_FARM_Log_Supported(device))
        {
            // FARM current logpage - (0xA6 - 0x00)
            startTimeInMilliSecs = get_Milliseconds_Since_Unix_Epoch();
            if (SUCCESS == get_ATA_Log(device, SEAGATE_ATA_LOG_FIELD_ACCESSIBLE_RELIABILITY_METRICS, M_NULLPTR,
                                       M_NULLPTR, true, false, true, farmCurrentLog.ptr,
                                       M_STATIC_CAST(uint32_t, farmCurrentLog.alloclen), M_NULLPTR, transferSizeBytes,
                                       SEAGATE_FARM_CURRENT))
            {
                endTimeInMilliSecs = get_Milliseconds_Since_Unix_Epoch();
                addDataSetEntry(SUBPAGE_TYPE_FARM_CURRENT, farmCurrentHeader, &numberOfDataSets, &headerLength,
                                &farmContentField, M_STATIC_CAST(uint32_t, farmCurrentLog.alloclen),
                                startTimeInMilliSecs, endTimeInMilliSecs);

#ifdef _DEBUG
                FILE*   tempFile    = M_NULLPTR;
                errno_t fileopenerr = safe_fopen(&tempFile, "farmcurrent.bin", "w+b");
                if (fileopenerr == 0 && tempFile != M_NULLPTR)
                {
                    if (fwrite(farmCurrentLog.ptr, sizeof(uint8_t), farmCurrentLog.alloclen, tempFile) !=
                            farmCurrentLog.alloclen ||
                        ferror(tempFile))
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

            // FARM factory logpage - (0xA6 - 0x03)
            startTimeInMilliSecs = get_Milliseconds_Since_Unix_Epoch();
            if (SUCCESS == get_ATA_Log(device, SEAGATE_ATA_LOG_FIELD_ACCESSIBLE_RELIABILITY_METRICS, M_NULLPTR,
                                       M_NULLPTR, true, false, true, farmFactoryLog.ptr,
                                       M_STATIC_CAST(uint32_t, farmFactoryLog.alloclen), M_NULLPTR, transferSizeBytes,
                                       SEAGATE_FARM_REPORT_FACTORY_DATA))
            {
                endTimeInMilliSecs = get_Milliseconds_Since_Unix_Epoch();
                addDataSetEntry(SUBPAGE_TYPE_FARM_FACTORY, farmFactoryHeader, &numberOfDataSets, &headerLength,
                                &farmContentField, ATA_FARM_LOG_PAGE_SIZE, startTimeInMilliSecs, endTimeInMilliSecs);

#ifdef _DEBUG
                FILE*   tempFile    = M_NULLPTR;
                errno_t fileopenerr = safe_fopen(&tempFile, "farmfactory.bin", "w+b");
                if (fileopenerr == 0 && tempFile != M_NULLPTR)
                {
                    if (fwrite(farmFactoryLog.ptr, sizeof(uint8_t), farmFactoryLog.alloclen, tempFile) !=
                            farmFactoryLog.alloclen ||
                        ferror(tempFile))
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

            // FARM saved logpage - (0xA6 - 0x02)
            startTimeInMilliSecs = get_Milliseconds_Since_Unix_Epoch();
            if (SUCCESS == get_ATA_Log(device, SEAGATE_ATA_LOG_FIELD_ACCESSIBLE_RELIABILITY_METRICS, M_NULLPTR,
                                       M_NULLPTR, true, false, true, farmSavedLog.ptr,
                                       M_STATIC_CAST(uint32_t, farmSavedLog.alloclen), M_NULLPTR, transferSizeBytes,
                                       SEAGATE_FARM_REPORT_SAVED))
            {
                endTimeInMilliSecs = get_Milliseconds_Since_Unix_Epoch();
                addDataSetEntry(SUBPAGE_TYPE_FARM_SAVE, farmSavedHeader, &numberOfDataSets, &headerLength,
                                &farmContentField, ATA_FARM_LOG_PAGE_SIZE, startTimeInMilliSecs, endTimeInMilliSecs);

#ifdef _DEBUG
                FILE*   tempFile    = M_NULLPTR;
                errno_t fileopenerr = safe_fopen(&tempFile, "farmsaved.bin", "w+b");
                if (fileopenerr == 0 && tempFile != M_NULLPTR)
                {
                    if (fwrite(farmSavedLog.ptr, sizeof(uint8_t), ATA_FARM_LOG_PAGE_SIZE, tempFile) !=
                            farmSavedLog.alloclen ||
                        ferror(tempFile))
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
            // FARM Time series logpage 0xC6 - feature 0x00
            uint8_t* farmTimeSeriesFramesLog =
                M_REINTERPRET_CAST(uint8_t*, safe_calloc_aligned(uint32_to_sizet(ATA_TIMESERIES_FRAME_LOG_SIZE),
                                                                 sizeof(uint8_t), device->os_info.minimumAlignment));
            startTimeInMilliSecs = get_Milliseconds_Since_Unix_Epoch();
            if (SUCCESS == get_ATA_Log(device, SEAGATE_ATA_LOG_FARM_TIME_SERIES, M_NULLPTR, M_NULLPTR, true, false,
                                       true, farmTimeSeriesFramesLog, ATA_TIMESERIES_FRAME_LOG_SIZE, M_NULLPTR,
                                       transferSizeBytes, SEAGATE_FARM_TIME_SERIES_DISC))
            {
                endTimeInMilliSecs = get_Milliseconds_Since_Unix_Epoch();

                // copy 16 Time series frames into log buffer
                safe_memcpy(farmTimeSeriesLog.ptr, farmTimeSeriesLog.alloclen, farmTimeSeriesFramesLog,
                            farmTimeSeriesLog.alloclen);
                addDataSetEntry(SUBPAGE_TYPE_FARM_TIMESERIES, farmTimeSeriesHeader, &numberOfDataSets, &headerLength,
                                &farmContentField, M_STATIC_CAST(uint32_t, farmTimeSeriesLog.alloclen),
                                startTimeInMilliSecs, endTimeInMilliSecs);

#ifdef _DEBUG
                FILE*   tempFileTimeseries = M_NULLPTR;
                errno_t fileopenerr        = safe_fopen(&tempFileTimeseries, "farmtimeseries.bin", "w+b");
                if (fileopenerr == 0 && tempFileTimeseries != M_NULLPTR)
                {
                    if (fwrite(farmTimeSeriesLog.ptr, sizeof(uint8_t), farmTimeSeriesLog.alloclen,
                               tempFileTimeseries) != farmTimeSeriesLog.alloclen ||
                        ferror(tempFileTimeseries))
                    {
                        printf("error in writing farmtimeseries.bin file\n");
                        safe_free_aligned(&farmTimeSeriesFramesLog);
                        return ERROR_WRITING_FILE;
                    }
                    fclose(tempFileTimeseries);
                }
#endif

                // copy 2 Long term saved frames into log buffer
                safe_memcpy(farmLongSavedLog.ptr, farmLongSavedLog.alloclen,
                            farmTimeSeriesFramesLog + uint32_to_sizet(FARM_TIME_SERIES_PAGES * ATA_FARM_LOG_PAGE_SIZE),
                            farmLongSavedLog.alloclen);
                addDataSetEntry(SUBPAGE_TYPE_FARM_LONG_SAVE, farmLongSavedHeader, &numberOfDataSets, &headerLength,
                                &farmContentField, M_STATIC_CAST(uint32_t, farmLongSavedLog.alloclen),
                                startTimeInMilliSecs, endTimeInMilliSecs);

#ifdef _DEBUG
                FILE* tempFileLongSave = M_NULLPTR;
                fileopenerr            = safe_fopen(&tempFileLongSave, "farmlongsaved.bin", "w+b");
                if (fileopenerr == 0 && tempFileLongSave != M_NULLPTR)
                {
                    if (fwrite(farmLongSavedLog.ptr, sizeof(uint8_t), farmLongSavedLog.alloclen, tempFileLongSave) !=
                            farmLongSavedLog.alloclen ||
                        ferror(tempFileLongSave))
                    {
                        printf("error in writing farmlongsaved.bin file\n");
                        safe_free_aligned(&farmTimeSeriesFramesLog);
                        return ERROR_WRITING_FILE;
                    }
                    fclose(tempFileLongSave);
                }
#endif

                // copy 6 Sticky frames into log buffer
                safe_memcpy(farmStickyLog.ptr, farmStickyLog.alloclen,
                            farmTimeSeriesFramesLog + (uint32_to_sizet(FARM_TIME_SERIES_PAGES + FARM_LONG_SAVED_PAGES) *
                                                       uint32_to_sizet(ATA_FARM_LOG_PAGE_SIZE)),
                            farmStickyLog.alloclen);
                addDataSetEntry(SUBPAGE_TYPE_FARM_STICKY, farmStickyHeader, &numberOfDataSets, &headerLength,
                                &farmContentField, M_STATIC_CAST(uint32_t, farmStickyLog.alloclen),
                                startTimeInMilliSecs, endTimeInMilliSecs);

#ifdef _DEBUG
                FILE* tempFileSticky = M_NULLPTR;
                fileopenerr          = safe_fopen(&tempFileSticky, "farmsticky.bin", "w+b");
                if (fileopenerr == 0 && tempFileSticky != M_NULLPTR)
                {
                    if (fwrite(farmStickyLog.ptr, sizeof(uint8_t), farmStickyLog.alloclen, tempFileSticky) !=
                            farmStickyLog.alloclen ||
                        ferror(tempFileSticky))
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

            // FARM Workload trace logpage 0xC6 - feature 0x02
            uint8_t* farmWorkloadTraceFramesLog =
                M_REINTERPRET_CAST(uint8_t*, safe_calloc_aligned(ATA_TIMESERIES_FRAME_LOG_SIZE, sizeof(uint8_t),
                                                                 device->os_info.minimumAlignment));
            startTimeInMilliSecs = get_Milliseconds_Since_Unix_Epoch();
            if (SUCCESS == get_ATA_Log(device, SEAGATE_ATA_LOG_FARM_TIME_SERIES, M_NULLPTR, M_NULLPTR, true, false,
                                       true, farmWorkloadTraceFramesLog, ATA_TIMESERIES_FRAME_LOG_SIZE, M_NULLPTR,
                                       transferSizeBytes, SEAGATE_FARM_TIME_SERIES_WLTR))
            {
                endTimeInMilliSecs = get_Milliseconds_Since_Unix_Epoch();

                // copy 2048 KB of meaningful data in log buffer
                safe_memcpy(farmWorkLoadTraceLog.ptr, farmStickyLog.alloclen, farmWorkloadTraceFramesLog,
                            ATA_WORKLOAD_TRACE_PAGE_SIZE);
                addDataSetEntry(SUBPAGE_TYPE_FARM_WORKLOAD_TRACE, farmWorkLoadTraceHeader, &numberOfDataSets,
                                &headerLength, &farmContentField, M_STATIC_CAST(uint32_t, farmStickyLog.alloclen),
                                startTimeInMilliSecs, endTimeInMilliSecs);

#ifdef _DEBUG
                FILE*   tempFile    = M_NULLPTR;
                errno_t fileopenerr = safe_fopen(&tempFile, "farmworkloadtrace.bin", "w+b");
                if (fileopenerr == 0 && tempFile != M_NULLPTR)
                {
                    if (fwrite(farmWorkLoadTraceLog.ptr, sizeof(uint8_t), farmStickyLog.alloclen, tempFile) !=
                            farmStickyLog.alloclen ||
                        ferror(tempFile))
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
            // FARM Time series logpage 0xC6 - feature 0x01
            uint8_t* farmTimeSeriesFramesLog =
                M_REINTERPRET_CAST(uint8_t*, safe_calloc_aligned(ATA_TIMESERIES_FRAME_LOG_SIZE, sizeof(uint8_t),
                                                                 device->os_info.minimumAlignment));
            startTimeInMilliSecs = get_Milliseconds_Since_Unix_Epoch();
            if (SUCCESS == get_ATA_Log(device, SEAGATE_ATA_LOG_FARM_TIME_SERIES, M_NULLPTR, M_NULLPTR, true, false,
                                       true, farmTimeSeriesFramesLog, ATA_TIMESERIES_FRAME_LOG_SIZE, M_NULLPTR,
                                       transferSizeBytes, SEAGATE_FARM_TIME_SERIES_FLASH))
            {
                endTimeInMilliSecs = get_Milliseconds_Since_Unix_Epoch();

                // copy Farm current into log buffer
                safe_memcpy(farmCurrentLog.ptr, farmCurrentLog.alloclen, farmTimeSeriesFramesLog,
                            ATA_FARM_LOG_PAGE_SIZE);
                addDataSetEntry(SUBPAGE_TYPE_FARM_CURRENT, farmCurrentHeader, &numberOfDataSets, &headerLength,
                                &farmContentField, M_STATIC_CAST(uint32_t, farmCurrentLog.alloclen),
                                startTimeInMilliSecs, endTimeInMilliSecs);

#ifdef _DEBUG
                FILE*   tempCurrentFile = M_NULLPTR;
                errno_t fileopenerr     = safe_fopen(&tempCurrentFile, "farmcurrent.bin", "w+b");
                if (fileopenerr == 0 && tempCurrentFile != M_NULLPTR)
                {
                    if (fwrite(farmCurrentLog.ptr, sizeof(uint8_t), farmCurrentLog.alloclen, tempCurrentFile) !=
                            farmCurrentLog.alloclen ||
                        ferror(tempCurrentFile))
                    {
                        printf("error in writing farmcurrent.bin file\n");
                        safe_free_aligned(&farmTimeSeriesFramesLog);
                        return ERROR_WRITING_FILE;
                    }
                    fclose(tempCurrentFile);
                }
#endif

                // copy Farm Saved into log buffer
                safe_memcpy(farmSavedLog.ptr, farmSavedLog.alloclen,
                            farmTimeSeriesFramesLog + uint32_to_sizet(ATA_FARM_LOG_PAGE_SIZE), farmSavedLog.alloclen);
                addDataSetEntry(SUBPAGE_TYPE_FARM_SAVE, farmSavedHeader, &numberOfDataSets, &headerLength,
                                &farmContentField, M_STATIC_CAST(uint32_t, farmSavedLog.alloclen), startTimeInMilliSecs,
                                endTimeInMilliSecs);

#ifdef _DEBUG
                FILE* tempSavedFile = M_NULLPTR;
                fileopenerr         = safe_fopen(&tempSavedFile, "farmsaved.bin", "w+b");
                if (fileopenerr == 0 && tempSavedFile != M_NULLPTR)
                {
                    if (fwrite(farmSavedLog.ptr, sizeof(uint8_t), farmSavedLog.alloclen, tempSavedFile) !=
                            farmSavedLog.alloclen ||
                        ferror(tempSavedFile))
                    {
                        printf("error in writing farmsaved.bin file\n");
                        safe_free_aligned(&farmTimeSeriesFramesLog);
                        return ERROR_WRITING_FILE;
                    }
                    fclose(tempSavedFile);
                }
#endif

                // copy 16 Timeseries frame into log buffer
                safe_memcpy(farmTimeSeriesLog.ptr, farmTimeSeriesLog.alloclen,
                            farmTimeSeriesFramesLog + uint32_to_sizet(FARM_LONG_SAVED_PAGES * ATA_FARM_LOG_PAGE_SIZE),
                            farmSavedLog.alloclen);
                addDataSetEntry(SUBPAGE_TYPE_FARM_TIMESERIES, farmTimeSeriesHeader, &numberOfDataSets, &headerLength,
                                &farmContentField, M_STATIC_CAST(uint32_t, farmSavedLog.alloclen), startTimeInMilliSecs,
                                endTimeInMilliSecs);

#ifdef _DEBUG
                FILE* tempTimeSeriesFile = M_NULLPTR;
                fileopenerr              = safe_fopen(&tempTimeSeriesFile, "farmtimeseries.bin", "w+b");
                if (fileopenerr == 0 && tempTimeSeriesFile != M_NULLPTR)
                {
                    if (fwrite(farmTimeSeriesLog.ptr, sizeof(uint8_t), farmSavedLog.alloclen, tempTimeSeriesFile) !=
                            farmSavedLog.alloclen ||
                        ferror(tempTimeSeriesFile))
                    {
                        printf("error in writing farmtimeseries.bin file\n");
                        safe_free_aligned(&farmTimeSeriesFramesLog);
                        return ERROR_WRITING_FILE;
                    }
                    fclose(tempTimeSeriesFile);
                }
#endif

                // copy 2 Long term saved frame into log buffer
                safe_memcpy(farmLongSavedLog.ptr, farmLongSavedLog.alloclen,
                            farmTimeSeriesFramesLog + (uint32_to_sizet(FARM_TIME_SERIES_PAGES + FARM_LONG_SAVED_PAGES) *
                                                       uint32_to_sizet(ATA_FARM_LOG_PAGE_SIZE)),
                            farmLongSavedLog.alloclen);
                addDataSetEntry(SUBPAGE_TYPE_FARM_LONG_SAVE, farmLongSavedHeader, &numberOfDataSets, &headerLength,
                                &farmContentField, M_STATIC_CAST(uint32_t, farmLongSavedLog.alloclen),
                                startTimeInMilliSecs, endTimeInMilliSecs);

#ifdef _DEBUG
                FILE* tempLongSavedFile = M_NULLPTR;
                fileopenerr             = safe_fopen(&tempLongSavedFile, "farmlongsaved.bin", "w+b");
                if (fileopenerr == 0 && tempLongSavedFile != M_NULLPTR)
                {
                    if (fwrite(farmLongSavedLog.ptr, sizeof(uint8_t), farmLongSavedLog.alloclen, tempLongSavedFile) !=
                            farmLongSavedLog.alloclen ||
                        ferror(tempLongSavedFile))
                    {
                        printf("error in writing farmlongsaved.bin file\n");
                        safe_free_aligned(&farmTimeSeriesFramesLog);
                        return ERROR_WRITING_FILE;
                    }
                    fclose(tempLongSavedFile);
                }
#endif

                // copy 6 sticky frame into log buffer
                safe_memcpy(farmStickyLog.ptr, farmStickyLog.alloclen,
                            farmTimeSeriesFramesLog + (SIZE_T_C(20) * uint32_to_sizet(ATA_FARM_LOG_PAGE_SIZE)),
                            farmStickyLog.alloclen);
                addDataSetEntry(SUBPAGE_TYPE_FARM_STICKY, farmStickyHeader, &numberOfDataSets, &headerLength,
                                &farmContentField, M_STATIC_CAST(uint32_t, farmStickyLog.alloclen),
                                startTimeInMilliSecs, endTimeInMilliSecs);

#ifdef _DEBUG
                FILE* tempStickyFile = M_NULLPTR;
                fileopenerr          = safe_fopen(&tempStickyFile, "farmsticky.bin", "w+b");
                if (fileopenerr == 0 && tempStickyFile != M_NULLPTR)
                {
                    if (fwrite(farmStickyLog.ptr, sizeof(uint8_t), farmStickyLog.alloclen, tempStickyFile) !=
                            farmStickyLog.alloclen ||
                        ferror(tempStickyFile))
                    {
                        printf("error in writing farmsticky.bin file\n");
                        safe_free_aligned(&farmTimeSeriesFramesLog);
                        return ERROR_WRITING_FILE;
                    }
                    fclose(tempStickyFile);
                }
#endif

                // copy Farm factory into log buffer
                safe_memcpy(farmFactoryLog.ptr, farmFactoryLog.alloclen,
                            farmTimeSeriesFramesLog + (SIZE_T_C(26) * uint32_to_sizet(ATA_FARM_LOG_PAGE_SIZE)),
                            ATA_FARM_LOG_PAGE_SIZE);
                addDataSetEntry(SUBPAGE_TYPE_FARM_FACTORY, farmFactoryHeader, &numberOfDataSets, &headerLength,
                                &farmContentField, ATA_FARM_LOG_PAGE_SIZE, startTimeInMilliSecs, endTimeInMilliSecs);

#ifdef _DEBUG
                FILE* tempFactoryFile = M_NULLPTR;
                fileopenerr           = safe_fopen(&tempFactoryFile, "farmfactory.bin", "w+b");
                if (fileopenerr == 0 && tempFactoryFile != M_NULLPTR)
                {
                    if (fwrite(farmFactoryLog.ptr, sizeof(uint8_t), farmFactoryLog.alloclen, tempFactoryFile) !=
                            farmFactoryLog.alloclen ||
                        ferror(tempFactoryFile))
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

            // FARM Workload trace logpage 0xC6 - feature 0x02
            uint8_t* farmWorkloadTraceFramesLog =
                M_REINTERPRET_CAST(uint8_t*, safe_calloc_aligned(ATA_TIMESERIES_FRAME_LOG_SIZE, sizeof(uint8_t),
                                                                 device->os_info.minimumAlignment));
            startTimeInMilliSecs = get_Milliseconds_Since_Unix_Epoch();
            if (SUCCESS == get_ATA_Log(device, SEAGATE_ATA_LOG_FARM_TIME_SERIES, M_NULLPTR, M_NULLPTR, true, false,
                                       true, farmWorkloadTraceFramesLog, ATA_TIMESERIES_FRAME_LOG_SIZE, M_NULLPTR,
                                       transferSizeBytes, SEAGATE_FARM_TIME_SERIES_WLTR))
            {
                endTimeInMilliSecs = get_Milliseconds_Since_Unix_Epoch();

                // copy 2048 KB of meaningful data in log buffer
                safe_memcpy(farmWorkLoadTraceLog.ptr, farmWorkLoadTraceLog.alloclen, farmWorkloadTraceFramesLog,
                            ATA_WORKLOAD_TRACE_PAGE_SIZE);
                addDataSetEntry(SUBPAGE_TYPE_FARM_WORKLOAD_TRACE, farmWorkLoadTraceHeader, &numberOfDataSets,
                                &headerLength, &farmContentField,
                                M_STATIC_CAST(uint32_t, farmWorkLoadTraceLog.alloclen), startTimeInMilliSecs,
                                endTimeInMilliSecs);

#ifdef _DEBUG
                FILE*   tempFile    = M_NULLPTR;
                errno_t fileopenerr = safe_fopen(&tempFile, "farmworkloadtrace.bin", "w+b");
                if (fileopenerr == 0 && tempFile != M_NULLPTR)
                {
                    if (fwrite(farmWorkLoadTraceLog.ptr, sizeof(uint8_t), farmWorkLoadTraceLog.alloclen, tempFile) !=
                            farmWorkLoadTraceLog.alloclen ||
                        ferror(tempFile))
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

    // update subheader entry
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
            zeroPaddingBufferSize->farmCurrentZeroPadding =
                ((datasetOffset / DATASET_ENTRY_START_OFFSET) + 1) * DATASET_ENTRY_START_OFFSET - datasetOffset;
            datasetOffset += zeroPaddingBufferSize->farmCurrentZeroPadding;
        }
    }

    if (farmContentField & BIT1)
    {
        updateDataSetEntryOffset(farmFactoryHeader, datasetOffset);
        datasetOffset += ATA_FARM_LOG_PAGE_SIZE;
        if (datasetOffset % DATASET_ENTRY_START_OFFSET)
        {
            zeroPaddingBufferSize->farmFactoryZeroPadding =
                ((datasetOffset / DATASET_ENTRY_START_OFFSET) + 1) * DATASET_ENTRY_START_OFFSET - datasetOffset;
            datasetOffset += zeroPaddingBufferSize->farmFactoryZeroPadding;
        }
    }

    if (farmContentField & BIT5)
    {
        updateDataSetEntryOffset(farmSavedHeader, datasetOffset);
        datasetOffset += ATA_FARM_LOG_PAGE_SIZE;
        if (datasetOffset % DATASET_ENTRY_START_OFFSET)
        {
            zeroPaddingBufferSize->farmSavedZeroPadding =
                ((datasetOffset / DATASET_ENTRY_START_OFFSET) + 1) * DATASET_ENTRY_START_OFFSET - datasetOffset;
            datasetOffset += zeroPaddingBufferSize->farmSavedZeroPadding;
        }
    }

    if (farmContentField & BIT2)
    {
        updateDataSetEntryOffset(farmTimeSeriesHeader, datasetOffset);
        datasetOffset += (FARM_TIME_SERIES_PAGES * ATA_FARM_LOG_PAGE_SIZE);
        if (datasetOffset % DATASET_ENTRY_START_OFFSET)
        {
            zeroPaddingBufferSize->farmTimeSeriesZeroPadding =
                ((datasetOffset / DATASET_ENTRY_START_OFFSET) + 1) * DATASET_ENTRY_START_OFFSET - datasetOffset;
            datasetOffset += zeroPaddingBufferSize->farmTimeSeriesZeroPadding;
        }
    }

    if (farmContentField & BIT6)
    {
        updateDataSetEntryOffset(farmLongSavedHeader, datasetOffset);
        datasetOffset += (FARM_LONG_SAVED_PAGES * ATA_FARM_LOG_PAGE_SIZE);
        if (datasetOffset % DATASET_ENTRY_START_OFFSET)
        {
            zeroPaddingBufferSize->farmLongSavedZeroPadding =
                ((datasetOffset / DATASET_ENTRY_START_OFFSET) + 1) * DATASET_ENTRY_START_OFFSET - datasetOffset;
            datasetOffset += zeroPaddingBufferSize->farmLongSavedZeroPadding;
        }
    }

    if (farmContentField & BIT3)
    {
        updateDataSetEntryOffset(farmStickyHeader, datasetOffset);
        datasetOffset += (FARM_STICKY_PAGES * ATA_FARM_LOG_PAGE_SIZE);
        if (datasetOffset % DATASET_ENTRY_START_OFFSET)
        {
            zeroPaddingBufferSize->farmStickyZeroPadding =
                ((datasetOffset / DATASET_ENTRY_START_OFFSET) + 1) * DATASET_ENTRY_START_OFFSET - datasetOffset;
            datasetOffset += zeroPaddingBufferSize->farmStickyZeroPadding;
        }
    }

    if (farmContentField & BIT4)
    {
        updateDataSetEntryOffset(farmWorkLoadTraceHeader, datasetOffset);
        datasetOffset += ATA_WORKLOAD_TRACE_PAGE_SIZE;
    }

    // copy remaing fields for header information
    safe_memcpy(header + 116, uint16_to_sizet(FARMC_LOG_HEADER_LENGTH) - RSIZE_T_C(116), &headerLength,
                sizeof(uint16_t));
    safe_memcpy(header + 118, uint16_to_sizet(FARMC_LOG_HEADER_LENGTH) - RSIZE_T_C(118), &farmContentField,
                sizeof(uint32_t));
    safe_memcpy(header + 252, uint16_to_sizet(FARMC_LOG_HEADER_LENGTH) - RSIZE_T_C(252), &numberOfDataSets,
                sizeof(uint16_t));

    return SUCCESS;
}

static eReturnValues pullSCSIFarmLogs(tDevice*                device,
                                      uint8_t*                header,
                                      tZeroPaddingBufferSize* zeroPaddingBufferSize,
                                      uint8_t*                farmCurrentHeader,
                                      uint8_t*                farmFactoryHeader,
                                      uint8_t*                farmTimeSeriesHeader,
                                      uint8_t*                farmLongSavedHeader,
                                      uint8_t*                farmStickyHeader,
                                      farmPtrAndLen           farmCurrentLog,
                                      farmPtrAndLen           farmFactoryLog,
                                      farmPtrAndLen           farmTimeSeriesLog,
                                      farmPtrAndLen           farmLongSavedLog,
                                      farmPtrAndLen           farmStickyLog,
                                      tSASLogpageSize         logpageSize)
{
    eReturnValues returnValue          = FAILURE;
    uint64_t      startTimeInMilliSecs = UINT64_C(0);
    uint64_t      endTimeInMilliSecs   = UINT64_C(0);
    uint16_t      numberOfDataSets     = UINT16_C(0);
    uint16_t      headerLength         = FARMC_LOG_HEADER_LENGTH;
    uint32_t      farmContentField     = UINT32_C(0);
    uint32_t      longSavedLogLength   = UINT32_C(0);
    uint32_t      timeSeriesLogLength  = UINT32_C(0);
    uint32_t      stickyLogLength      = UINT32_C(0);

    // get Current FARM for SAS logpage 0x3D - feature 0x03
    if (is_FARM_Log_Supported(device))
    {
        startTimeInMilliSecs = get_Milliseconds_Since_Unix_Epoch();
        returnValue = get_SCSI_Log(device, SEAGATE_LP_FARM, SEAGATE_FARM_SP_CURRENT, M_NULLPTR, M_NULLPTR, true,
                                   farmCurrentLog.ptr, logpageSize.currentLog, M_NULLPTR);
        if (returnValue != SUCCESS)
        {
            // print error
            if (VERBOSITY_QUIET < device->deviceVerbosity)
            {
                printf("Error pulling Farm Current log\n");
            }
        }
        else
        {
            endTimeInMilliSecs = get_Milliseconds_Since_Unix_Epoch();
            addDataSetEntry(SUBPAGE_TYPE_FARM_CURRENT, farmCurrentHeader, &numberOfDataSets, &headerLength,
                            &farmContentField, logpageSize.currentLog, startTimeInMilliSecs, endTimeInMilliSecs);

#ifdef _DEBUG
            FILE*   tempFile    = M_NULLPTR;
            errno_t fileopenerr = safe_fopen(&tempFile, "farmcurrent.bin", "w+b");
            if (fileopenerr == 0 && tempFile != M_NULLPTR)
            {
                if (fwrite(farmCurrentLog.ptr, sizeof(uint8_t), farmCurrentLog.alloclen, tempFile) !=
                        farmCurrentLog.alloclen ||
                    ferror(tempFile))
                {
                    printf("error in writing farmcurrent.bin file\n");
                    return ERROR_WRITING_FILE;
                }
                fclose(tempFile);
            }
#endif //  _DEBUG
        }
    }

    // get Factory FARM for SAS logpage 0x3D - feature 0x04
    if (is_Factory_FARM_Log_Supported(device))
    {
        startTimeInMilliSecs = get_Milliseconds_Since_Unix_Epoch();
        returnValue = get_SCSI_Log(device, SEAGATE_LP_FARM, SEAGATE_FARM_SP_FACTORY, M_NULLPTR, M_NULLPTR, true,
                                   farmFactoryLog.ptr, logpageSize.factoryLog, M_NULLPTR);
        if (returnValue != SUCCESS)
        {
            // print error
            if (VERBOSITY_QUIET < device->deviceVerbosity)
            {
                printf("Error pulling Farm Factory log\n");
            }
        }
        else
        {
            endTimeInMilliSecs = get_Milliseconds_Since_Unix_Epoch();
            addDataSetEntry(SUBPAGE_TYPE_FARM_FACTORY, farmFactoryHeader, &numberOfDataSets, &headerLength,
                            &farmContentField, logpageSize.factoryLog, startTimeInMilliSecs, endTimeInMilliSecs);

#ifdef _DEBUG
            FILE*   tempFile    = M_NULLPTR;
            errno_t fileopenerr = safe_fopen(&tempFile, "farmfactory.bin", "w+b");
            if (fileopenerr == 0 && tempFile != M_NULLPTR)
            {
                if (fwrite(farmFactoryLog.ptr, sizeof(uint8_t), farmCurrentLog.alloclen, tempFile) !=
                        farmCurrentLog.alloclen ||
                    ferror(tempFile))
                {
                    printf("error in writing farmfactory.bin file\n");
                    return ERROR_WRITING_FILE;
                }
                fclose(tempFile);
            }
#endif //  _DEBUG
        }
    }

    // get Time Series FARM for SAS logpage 0x3D - 0x10 : 0x1F
    if (is_FARM_Time_Series_Log_Supported(device))
    {
        uint8_t* tempTimeSeries     = farmTimeSeriesLog.ptr;
        bool     isTimeSeriesPulled = false;
        startTimeInMilliSecs        = get_Milliseconds_Since_Unix_Epoch();

        for (int i = SEAGATE_FARM_SP_TIME_SERIES_START; i <= SEAGATE_FARM_SP_TIME_SERIES_END; i++)
        {
            returnValue = get_SCSI_Log(device, SEAGATE_LP_FARM, M_STATIC_CAST(uint8_t, i), M_NULLPTR, M_NULLPTR, true,
                                       tempTimeSeries, logpageSize.timeSeriesLog, M_NULLPTR);
            if (returnValue != SUCCESS)
            {
                // print error
                if (VERBOSITY_QUIET < device->deviceVerbosity)
                {
                    printf("Error pulling Farm TimeSeries log Subpage %d\n", i);
                }
            }
            else
            {
                // set the flag to true, which indicates atleast one timeseries frame was pulled
                isTimeSeriesPulled = true;
                tempTimeSeries     = tempTimeSeries + logpageSize.timeSeriesLog;
                timeSeriesLogLength += logpageSize.timeSeriesLog;
            }
        }

        if (isTimeSeriesPulled) // if atleast one time series frame was pulled then add into the dataset entry
        {
            endTimeInMilliSecs = get_Milliseconds_Since_Unix_Epoch();
            addDataSetEntry(SUBPAGE_TYPE_FARM_TIMESERIES, farmTimeSeriesHeader, &numberOfDataSets, &headerLength,
                            &farmContentField, timeSeriesLogLength, startTimeInMilliSecs, endTimeInMilliSecs);

#ifdef _DEBUG
            FILE*   tempFile    = M_NULLPTR;
            errno_t fileopenerr = safe_fopen(&tempFile, "farmtimeseries.bin", "w+b");
            if (fileopenerr == 0 && tempFile != M_NULLPTR)
            {
                printf("writing into farmtimeseries.bin file\n");
                if (fwrite(farmTimeSeriesLog.ptr, sizeof(uint8_t), farmCurrentLog.alloclen, tempFile) !=
                        farmCurrentLog.alloclen ||
                    ferror(tempFile))
                {
                    printf("error in writing farmtimeseries.bin file\n");
                    return ERROR_WRITING_FILE;
                }
                fclose(tempFile);
            }
#endif // DEBUG
        }
    }

    // get Long term saved Farm frames logpage 0x3D - 0xC0, 0xC1
    if (is_FARM_Long_Saved_Log_Supported(device))
    {
        uint8_t* tempLongSavedFarm = farmLongSavedLog.ptr;
        bool     isLongSavedPulled = false;
        startTimeInMilliSecs       = get_Milliseconds_Since_Unix_Epoch();

        // pull 0xC0
        returnValue = get_SCSI_Log(device, SEAGATE_LP_FARM, SEAGATE_FARM_SP_TIME_SERIES_ADD1, M_NULLPTR, M_NULLPTR,
                                   true, tempLongSavedFarm, logpageSize.longSavedLog, M_NULLPTR);
        if (returnValue != SUCCESS)
        {
            if (VERBOSITY_QUIET < device->deviceVerbosity)
            {
                printf("Error pulling Farm Saved log Subpage %d\n", SEAGATE_FARM_SP_TIME_SERIES_ADD1);
            }
        }
        else
        {
            // set the flag to true, which indicates atleast on saved frame was pulled
            isLongSavedPulled = true;
            tempLongSavedFarm = tempLongSavedFarm + logpageSize.longSavedLog;
            longSavedLogLength += logpageSize.longSavedLog;
        }

        // pull 0xC1
        returnValue = get_SCSI_Log(device, SEAGATE_LP_FARM, SEAGATE_FARM_SP_TIME_SERIES_ADD2, M_NULLPTR, M_NULLPTR,
                                   true, tempLongSavedFarm, logpageSize.longSavedLog, M_NULLPTR);
        if (returnValue != SUCCESS)
        {
            if (VERBOSITY_QUIET < device->deviceVerbosity)
            {
                printf("Error pulling Farm Long Saved log Subpage %d\n", SEAGATE_FARM_SP_TIME_SERIES_ADD2);
            }
        }
        else
        {
            // set the flag to true, which indicates atleast one saved frame was pulled
            isLongSavedPulled = true;
            tempLongSavedFarm = tempLongSavedFarm + logpageSize.longSavedLog;
            longSavedLogLength += logpageSize.longSavedLog;
        }

        if (isLongSavedPulled) // if atleast one saved frame was pulled then add into the dataset entry
        {
            endTimeInMilliSecs = get_Milliseconds_Since_Unix_Epoch();
            addDataSetEntry(SUBPAGE_TYPE_FARM_LONG_SAVE, farmLongSavedHeader, &numberOfDataSets, &headerLength,
                            &farmContentField, longSavedLogLength, startTimeInMilliSecs, endTimeInMilliSecs);

#ifdef _DEBUG
            FILE*   tempFile    = M_NULLPTR;
            errno_t fileopenerr = safe_fopen(&tempFile, "farmlongsaved.bin", "w+b");
            if (fileopenerr == 0 && tempFile != M_NULLPTR)
            {
                if (fwrite(farmLongSavedLog.ptr, sizeof(uint8_t), farmLongSavedLog.alloclen, tempFile) !=
                        farmLongSavedLog.alloclen ||
                    ferror(tempFile))
                {
                    printf("error in writing farmlongsaved.bin file\n");
                    return ERROR_WRITING_FILE;
                }
                fclose(tempFile);
            }
#endif // DEBUG
        }
    }

    // Get Sticky FARM Log for SAS logpage 0x3D - feature 0xC2 - 0xC7
    if (is_FARM_Sticky_Log_Supported(device))
    {
        uint8_t* tempSticky     = farmStickyLog.ptr;
        bool     isStickyPulled = false;

        startTimeInMilliSecs = get_Milliseconds_Since_Unix_Epoch();
        for (int i = SEAGATE_FARM_SP_STICKY_START; i <= SEAGATE_FARM_SP_STICKY_END; i++)
        {
            returnValue = get_SCSI_Log(device, SEAGATE_LP_FARM, M_STATIC_CAST(uint8_t, i), M_NULLPTR, M_NULLPTR, true,
                                       tempSticky, logpageSize.stickyLog, M_NULLPTR);
            if (returnValue != SUCCESS)
            {
                // print error
                if (VERBOSITY_QUIET < device->deviceVerbosity)
                {
                    printf("Error pulling Farm Sticky log Subpage %d\n", i);
                }
            }
            else
            {
                // set the flag to true, which indicates atleast one sticky frame was pulled
                isStickyPulled = true;
                tempSticky     = tempSticky + logpageSize.stickyLog;
                stickyLogLength += logpageSize.stickyLog;
            }
        }

        if (isStickyPulled)
        {
            endTimeInMilliSecs = get_Milliseconds_Since_Unix_Epoch();
            addDataSetEntry(SUBPAGE_TYPE_FARM_STICKY, farmStickyHeader, &numberOfDataSets, &headerLength,
                            &farmContentField, stickyLogLength, startTimeInMilliSecs, endTimeInMilliSecs);

#ifdef _DEBUG
            FILE*   tempFile    = M_NULLPTR;
            errno_t fileopenerr = safe_fopen(&tempFile, "farmsticky.bin", "w+b");
            if (fileopenerr == 0 && tempFile != M_NULLPTR)
            {
                if (fwrite(farmStickyLog.ptr, sizeof(uint8_t), farmStickyLog.alloclen, tempFile) !=
                        farmStickyLog.alloclen ||
                    ferror(tempFile))
                {
                    printf("error in writing farmsticky.bin file\n");
                    return ERROR_WRITING_FILE;
                }
                fclose(tempFile);
            }
#endif // _DEBUG
        }
    }

    // update subheader entry
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
            zeroPaddingBufferSize->farmCurrentZeroPadding =
                ((datasetOffset / DATASET_ENTRY_START_OFFSET) + 1) * DATASET_ENTRY_START_OFFSET - datasetOffset;
            datasetOffset += zeroPaddingBufferSize->farmCurrentZeroPadding;
        }
    }

    if (farmContentField & BIT1)
    {
        updateDataSetEntryOffset(farmFactoryHeader, datasetOffset);
        datasetOffset += logpageSize.factoryLog;
        if (datasetOffset % DATASET_ENTRY_START_OFFSET)
        {
            zeroPaddingBufferSize->farmFactoryZeroPadding =
                ((datasetOffset / DATASET_ENTRY_START_OFFSET) + 1) * DATASET_ENTRY_START_OFFSET - datasetOffset;
            datasetOffset += zeroPaddingBufferSize->farmFactoryZeroPadding;
        }
    }

    if (farmContentField & BIT2)
    {
        updateDataSetEntryOffset(farmTimeSeriesHeader, datasetOffset);
        datasetOffset += timeSeriesLogLength;
        if (datasetOffset % DATASET_ENTRY_START_OFFSET)
        {
            zeroPaddingBufferSize->farmTimeSeriesZeroPadding =
                ((datasetOffset / DATASET_ENTRY_START_OFFSET) + 1) * DATASET_ENTRY_START_OFFSET - datasetOffset;
            datasetOffset += zeroPaddingBufferSize->farmTimeSeriesZeroPadding;
        }
    }

    if (farmContentField & BIT6)
    {
        updateDataSetEntryOffset(farmLongSavedHeader, datasetOffset);
        datasetOffset += longSavedLogLength;
        if (datasetOffset % DATASET_ENTRY_START_OFFSET)
        {
            zeroPaddingBufferSize->farmLongSavedZeroPadding =
                ((datasetOffset / DATASET_ENTRY_START_OFFSET) + 1) * DATASET_ENTRY_START_OFFSET - datasetOffset;
            datasetOffset += zeroPaddingBufferSize->farmLongSavedZeroPadding;
        }
    }

    if (farmContentField & BIT3)
    {
        updateDataSetEntryOffset(farmStickyHeader, datasetOffset);
        datasetOffset += stickyLogLength;
    }

    // copy remaing fields for header information
    safe_memcpy(header + 116, uint16_to_sizet(FARMC_LOG_HEADER_LENGTH) - RSIZE_T_C(116), &headerLength,
                sizeof(uint16_t));
    safe_memcpy(header + 118, uint16_to_sizet(FARMC_LOG_HEADER_LENGTH) - RSIZE_T_C(118), &farmContentField,
                sizeof(uint32_t));
    safe_memcpy(header + 252, uint16_to_sizet(FARMC_LOG_HEADER_LENGTH) - RSIZE_T_C(252), &numberOfDataSets,
                sizeof(uint16_t));

    return SUCCESS;
}

static eReturnValues write_FARM_Zero_Padding(uint32_t paddingSize, secureFileInfo* farmFile)
{
    eReturnValues returnValue = SUCCESS;
    if (paddingSize > 0)
    {
        size_t   zeroPaddingLen = uint32_to_sizet(paddingSize);
        uint8_t* zeroPadding    = M_REINTERPRET_CAST(uint8_t*, safe_calloc(zeroPaddingLen, sizeof(uint8_t)));
        if (zeroPadding != M_NULLPTR)
        {
            if (SEC_FILE_SUCCESS !=
                secure_Write_File(farmFile, zeroPadding, zeroPaddingLen, sizeof(uint8_t), zeroPaddingLen, M_NULLPTR))
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

eReturnValues pull_FARM_Combined_Log(tDevice*                 device,
                                     const char*              filePath,
                                     uint32_t                 transferSizeBytes,
                                     int                      sataFarmCopyType,
                                     eLogFileNamingConvention fileNameType)
{
    eReturnValues returnValue = NOT_SUPPORTED;

    if (device->drive_info.drive_type == ATA_DRIVE)
    {
        if (!(is_FARM_Log_Supported(device) ||
              is_FARM_Time_Series_Log_Supported(device))) // No farm or farm timeseries supported, then return
        {
            return returnValue;
        }
    }
    else if (device->drive_info.drive_type == SCSI_DRIVE)
    {
        if (!(is_FARM_Log_Supported(device) || is_Factory_FARM_Log_Supported(device) ||
              is_FARM_Long_Saved_Log_Supported(device) || is_FARM_Time_Series_Log_Supported(device) ||
              is_FARM_Sticky_Log_Supported(device))) // No farm or farm timeseries supported, then return
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
    farmPtrAndLen          farmCurrentLog    = {.ptr = M_NULLPTR, .alloclen = uint32_to_sizet(ATA_FARM_LOG_PAGE_SIZE)};
    farmPtrAndLen          farmFactoryLog    = {.ptr = M_NULLPTR, .alloclen = uint32_to_sizet(ATA_FARM_LOG_PAGE_SIZE)};
    farmPtrAndLen          farmSavedLog      = {.ptr = M_NULLPTR, .alloclen = uint32_to_sizet(ATA_FARM_LOG_PAGE_SIZE)};
    farmPtrAndLen          farmTimeSeriesLog = {.ptr = M_NULLPTR,
                                                .alloclen = uint32_to_sizet(FARM_TIME_SERIES_PAGES * ATA_FARM_LOG_PAGE_SIZE)};
    farmPtrAndLen          farmLongSavedLog  = {.ptr = M_NULLPTR,
                                                .alloclen = uint32_to_sizet(FARM_LONG_SAVED_PAGES * ATA_FARM_LOG_PAGE_SIZE)};
    farmPtrAndLen          farmStickyLog     = {.ptr      = M_NULLPTR,
                                                .alloclen = uint32_to_sizet(FARM_STICKY_PAGES * ATA_FARM_LOG_PAGE_SIZE)};
    farmPtrAndLen          farmWorkLoadTraceLog         = {.ptr = M_NULLPTR, .alloclen = ATA_WORKLOAD_TRACE_PAGE_SIZE};
    uint8_t*               farmCurrentZeroPaddingBuffer = M_NULLPTR;
    uint8_t*               farmFactoryZeroPaddingBuffer = M_NULLPTR;
    uint8_t*               farmSavedZeroPaddingBuffer   = M_NULLPTR;
    uint8_t*               farmTimeSeriesZeroPaddingBuffer = M_NULLPTR;
    uint8_t*               farmLongSavedZeroPaddingBuffer  = M_NULLPTR;
    uint8_t*               farmStickyZeroPaddingBuffer     = M_NULLPTR;
    tZeroPaddingBufferSize zeroPaddingBufferSize;
    safe_memset(&zeroPaddingBufferSize, sizeof(tZeroPaddingBufferSize), 0, sizeof(tZeroPaddingBufferSize));

    // set signature
    DECLARE_ZERO_INIT_ARRAY(char, signature, FARM_SIGNATURE_LENGTH + 1);
    snprintf_err_handle(signature, FARM_SIGNATURE_LENGTH + 1, "%-*s", FARM_SIGNATURE_LENGTH, FARMC_SIGNATURE_ID);
    safe_memcpy(header, uint16_to_sizet(FARMC_LOG_HEADER_LENGTH), &signature, FARM_SIGNATURE_LENGTH);

    // set the version number - major.minor.revision
    uint16_t majorVersion = FARMC_LOG_MAJOR_VERSION;
    uint16_t minorVersion = FARMC_LOG_MINOR_VERSION;
    uint16_t patchVersion = FARMC_LOG_PATCH_VERSION;
    safe_memcpy(header + 22, uint16_to_sizet(FARMC_LOG_HEADER_LENGTH) - RSIZE_T_C(22), &majorVersion, sizeof(uint16_t));
    safe_memcpy(header + 20, uint16_to_sizet(FARMC_LOG_HEADER_LENGTH) - RSIZE_T_C(20), &minorVersion, sizeof(uint16_t));
    safe_memcpy(header + 18, uint16_to_sizet(FARMC_LOG_HEADER_LENGTH) - RSIZE_T_C(18), &patchVersion, sizeof(uint16_t));

    // set interface type
    DECLARE_ZERO_INIT_ARRAY(char, interfaceType, 4 + 1);
    if (device->drive_info.drive_type == ATA_DRIVE)
    {
        snprintf_err_handle(interfaceType, 4 + 1, "%-*s", 4, "SATA");
    }
    else if (device->drive_info.drive_type == SCSI_DRIVE)
    {
        snprintf_err_handle(interfaceType, 4 + 1, "%-*s", 4, "SAS");
    }
    else
    {
        snprintf_err_handle(interfaceType, 4 + 1, "%-*s", 4, "NVMe");
    }
    safe_memcpy(header + 24, uint16_to_sizet(FARMC_LOG_HEADER_LENGTH) - RSIZE_T_C(24), &interfaceType, 4);

    // set model#
    DECLARE_ZERO_INIT_ARRAY(char, modelNumber, MODEL_NUM_LEN + 1);
    snprintf_err_handle(modelNumber, MODEL_NUM_LEN + 1, "%-*s", MODEL_NUM_LEN,
                        device->drive_info.product_identification);
    safe_memcpy(header + 32, uint16_to_sizet(FARMC_LOG_HEADER_LENGTH) - RSIZE_T_C(32), &modelNumber, MODEL_NUM_LEN);

    // set serial#
    DECLARE_ZERO_INIT_ARRAY(char, serialNumber, SERIAL_NUM_LEN + 1);
    snprintf_err_handle(serialNumber, SERIAL_NUM_LEN + 1, "%-*s", SERIAL_NUM_LEN, device->drive_info.serialNumber);
    safe_memcpy(header + 80, uint16_to_sizet(FARMC_LOG_HEADER_LENGTH) - RSIZE_T_C(80), &serialNumber, SERIAL_NUM_LEN);

    // set firmware revision
    DECLARE_ZERO_INIT_ARRAY(char, firmwareVersion, FW_REV_LEN + 1);
    snprintf_err_handle(firmwareVersion, FW_REV_LEN + 1, "%-*s", FW_REV_LEN, device->drive_info.product_revision);
    safe_memcpy(header + 104, uint16_to_sizet(FARMC_LOG_HEADER_LENGTH) - RSIZE_T_C(104), &firmwareVersion, FW_REV_LEN);

    // set dataset length
    uint16_t dataSetLength = FARMC_LOG_DATA_SET_HEADER_LENGTH;
    safe_memcpy(header + 254, uint16_to_sizet(FARMC_LOG_HEADER_LENGTH) - RSIZE_T_C(254), &dataSetLength,
                sizeof(uint16_t));

    do
    {
        // pull individual log subpage
        if (device->drive_info.drive_type == ATA_DRIVE)
        {
            // initialize log buffers
            farmCurrentLog.ptr =
                M_REINTERPRET_CAST(uint8_t*, safe_calloc_aligned(farmCurrentLog.alloclen, sizeof(uint8_t),
                                                                 device->os_info.minimumAlignment)); // 96KB
            farmFactoryLog.ptr =
                M_REINTERPRET_CAST(uint8_t*, safe_calloc_aligned(farmFactoryLog.alloclen, sizeof(uint8_t),
                                                                 device->os_info.minimumAlignment)); // 96KB
            farmSavedLog.ptr =
                M_REINTERPRET_CAST(uint8_t*, safe_calloc_aligned(farmSavedLog.alloclen, sizeof(uint8_t),
                                                                 device->os_info.minimumAlignment)); // 96KB
            farmTimeSeriesLog.ptr =
                M_REINTERPRET_CAST(uint8_t*, safe_calloc_aligned(farmTimeSeriesLog.alloclen, sizeof(uint8_t),
                                                                 device->os_info.minimumAlignment)); // 16 * 96KB
            farmLongSavedLog.ptr =
                M_REINTERPRET_CAST(uint8_t*, safe_calloc_aligned(farmLongSavedLog.alloclen, sizeof(uint8_t),
                                                                 device->os_info.minimumAlignment)); // 2 * 96KB
            farmStickyLog.ptr =
                M_REINTERPRET_CAST(uint8_t*, safe_calloc_aligned(farmStickyLog.alloclen, sizeof(uint8_t),
                                                                 device->os_info.minimumAlignment)); // 6 * 96KB
            farmWorkLoadTraceLog.ptr =
                M_REINTERPRET_CAST(uint8_t*, safe_calloc_aligned(farmWorkLoadTraceLog.alloclen, sizeof(uint8_t),
                                                                 device->os_info.minimumAlignment)); // 2048KB
            if (!farmCurrentLog.ptr)
            {
                if (device->deviceVerbosity > VERBOSITY_QUIET)
                {
                    perror("error memory allocation for farmCurrentLog");
                }
                return MEMORY_FAILURE;
            }
            if (!farmFactoryLog.ptr)
            {
                if (device->deviceVerbosity > VERBOSITY_QUIET)
                {
                    perror("error memory allocation for farmFactoryLog");
                }
                return MEMORY_FAILURE;
            }
            if (!farmSavedLog.ptr)
            {
                if (device->deviceVerbosity > VERBOSITY_QUIET)
                {
                    perror("error memory allocation for farmSavedLog");
                }
                return MEMORY_FAILURE;
            }
            if (!farmTimeSeriesLog.ptr)
            {
                if (device->deviceVerbosity > VERBOSITY_QUIET)
                {
                    perror("error memory allocation for farmTimeSeriesLog");
                }
                return MEMORY_FAILURE;
            }
            if (!farmLongSavedLog.ptr)
            {
                if (device->deviceVerbosity > VERBOSITY_QUIET)
                {
                    perror("error memory allocation for farmLongSavedLog");
                }
                return MEMORY_FAILURE;
            }
            if (!farmStickyLog.ptr)
            {
                if (device->deviceVerbosity > VERBOSITY_QUIET)
                {
                    perror("error memory allocation for farmStickyLog");
                }
                return MEMORY_FAILURE;
            }
            if (!farmWorkLoadTraceLog.ptr)
            {
                if (device->deviceVerbosity > VERBOSITY_QUIET)
                {
                    perror("error memory allocation for farmWorkLoadTraceLog");
                }
                return MEMORY_FAILURE;
            }
            returnValue = pullATAFarmLogs(device, transferSizeBytes, sataFarmCopyType, header, &zeroPaddingBufferSize,
                                          farmCurrentHeader, farmFactoryHeader, farmSavedHeader, farmTimeSeriesHeader,
                                          farmLongSavedHeader, farmStickyHeader, farmWorkLoadTraceHeader,
                                          farmCurrentLog, farmFactoryLog, farmSavedLog, farmTimeSeriesLog,
                                          farmLongSavedLog, farmStickyLog, farmWorkLoadTraceLog);
            if (returnValue != SUCCESS)
            {
                break;
            }
        }
        else if (device->drive_info.drive_type == SCSI_DRIVE)
        {
            uint32_t        logSize     = UINT32_C(0);
            tSASLogpageSize logpageSize = {UINT32_C(0), UINT32_C(0), UINT32_C(0), UINT32_C(0), UINT32_C(0)};

            // get the log size of all log subpages
            if (is_FARM_Log_Supported(device))
            {
                // get length of FARM current page
                returnValue             = get_SCSI_Log_Size(device, SEAGATE_LP_FARM, SEAGATE_FARM_SP_CURRENT, &logSize);
                logpageSize.currentLog  = logSize;
                farmCurrentLog.alloclen = uint32_to_sizet(logSize);
                farmCurrentLog.ptr      = M_REINTERPRET_CAST(
                    uint8_t*, safe_calloc_aligned(uint32_to_sizet(logpageSize.currentLog), sizeof(uint8_t),
                                                       device->os_info.minimumAlignment));
                if (!farmCurrentLog.ptr)
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
                // get length of Factory FARM page
                returnValue             = get_SCSI_Log_Size(device, SEAGATE_LP_FARM, SEAGATE_FARM_SP_FACTORY, &logSize);
                logpageSize.factoryLog  = logSize;
                farmFactoryLog.alloclen = uint32_to_sizet(logSize);
                farmFactoryLog.ptr      = M_REINTERPRET_CAST(
                    uint8_t*, safe_calloc_aligned(uint32_to_sizet(logpageSize.factoryLog), sizeof(uint8_t),
                                                       device->os_info.minimumAlignment));
                if (!farmFactoryLog.ptr)
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
                // get length of FARM time series log
                returnValue = get_SCSI_Log_Size(device, SEAGATE_LP_FARM, SEAGATE_FARM_SP_TIME_SERIES_START, &logSize);
                logpageSize.timeSeriesLog  = logSize;
                farmTimeSeriesLog.alloclen = uint32_to_sizet(logSize) * FARM_TIME_SERIES_PAGES;
                farmTimeSeriesLog.ptr      = M_REINTERPRET_CAST(
                    uint8_t*, safe_calloc_aligned(uint32_to_sizet(logpageSize.timeSeriesLog) * FARM_TIME_SERIES_PAGES,
                                                       sizeof(uint8_t), device->os_info.minimumAlignment));
                if (!farmTimeSeriesLog.ptr)
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
                // get length of Farm Long saved frames
                returnValue = get_SCSI_Log_Size(device, SEAGATE_LP_FARM, SEAGATE_FARM_SP_TIME_SERIES_ADD1, &logSize);
                logpageSize.longSavedLog  = logSize;
                farmLongSavedLog.alloclen = uint32_to_sizet(logSize) * FARM_LONG_SAVED_PAGES;
                farmLongSavedLog.ptr      = M_REINTERPRET_CAST(
                    uint8_t*, safe_calloc_aligned(uint32_to_sizet(logpageSize.longSavedLog) * FARM_LONG_SAVED_PAGES,
                                                       sizeof(uint8_t), device->os_info.minimumAlignment));
                if (!farmLongSavedLog.ptr)
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
                // get length of FARM sticky log
                returnValue = get_SCSI_Log_Size(device, SEAGATE_LP_FARM, SEAGATE_FARM_SP_STICKY_START, &logSize);
                logpageSize.stickyLog  = logSize;
                farmStickyLog.alloclen = uint32_to_sizet(logSize) * FARM_STICKY_PAGES;
                farmStickyLog.ptr      = M_REINTERPRET_CAST(
                    uint8_t*, safe_calloc_aligned(uint32_to_sizet(logpageSize.stickyLog) * FARM_STICKY_PAGES,
                                                       sizeof(uint8_t), device->os_info.minimumAlignment));
                if (!farmStickyLog.ptr)
                {
                    if (device->deviceVerbosity > VERBOSITY_QUIET)
                    {
                        perror("error memory allocation for farmStickyLog");
                    }
                    return MEMORY_FAILURE;
                }
            }

            // pull the FARM SCSi log
            returnValue =
                pullSCSIFarmLogs(device, header, &zeroPaddingBufferSize, farmCurrentHeader, farmFactoryHeader,
                                 farmTimeSeriesHeader, farmLongSavedHeader, farmStickyHeader, farmCurrentLog,
                                 farmFactoryLog, farmTimeSeriesLog, farmLongSavedLog, farmStickyLog, logpageSize);
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
        if (!((farmContentField & BIT0) || (farmContentField & BIT1) || (farmContentField & BIT2) ||
              (farmContentField & BIT3) || (farmContentField & BIT4) || (farmContentField & BIT5) ||
              (farmContentField & BIT6)))
        {
            if (device->deviceVerbosity > VERBOSITY_QUIET)
            {
                printf("No Farm log available for this drive.\n");
            }
            returnValue = FAILURE;
            break;
        }

        // create and open the log file
        secureFileInfo* farmCombinedLog = M_NULLPTR;

        if (SUCCESS !=
            create_And_Open_Secure_Log_File_Dev_EZ(device, &farmCombinedLog, fileNameType, filePath, "FARMC", "frmc"))
        {
            if (device->deviceVerbosity > VERBOSITY_QUIET)
            {
                perror("Error in opening the file!\n");
            }
            returnValue = FILE_OPEN_ERROR;
            break;
        }

        // write header
        if (SEC_FILE_SUCCESS != secure_Write_File(farmCombinedLog, header, uint16_to_sizet(FARMC_LOG_HEADER_LENGTH),
                                                  sizeof(uint8_t), uint16_to_sizet(FARMC_LOG_HEADER_LENGTH), M_NULLPTR))
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

        // write dataset entry header
        {
            if (farmContentField & BIT0)
            {
                if (SEC_FILE_SUCCESS != secure_Write_File(farmCombinedLog, farmCurrentHeader,
                                                          FARMC_LOG_DATA_SET_HEADER_LENGTH, sizeof(uint8_t),
                                                          FARMC_LOG_DATA_SET_HEADER_LENGTH, M_NULLPTR))
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
                if (SEC_FILE_SUCCESS != secure_Write_File(farmCombinedLog, farmFactoryHeader,
                                                          FARMC_LOG_DATA_SET_HEADER_LENGTH, sizeof(uint8_t),
                                                          FARMC_LOG_DATA_SET_HEADER_LENGTH, M_NULLPTR))
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
                if (SEC_FILE_SUCCESS != secure_Write_File(farmCombinedLog, farmSavedHeader,
                                                          FARMC_LOG_DATA_SET_HEADER_LENGTH, sizeof(uint8_t),
                                                          FARMC_LOG_DATA_SET_HEADER_LENGTH, M_NULLPTR))
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
                if (SEC_FILE_SUCCESS != secure_Write_File(farmCombinedLog, farmTimeSeriesHeader,
                                                          FARMC_LOG_DATA_SET_HEADER_LENGTH, sizeof(uint8_t),
                                                          FARMC_LOG_DATA_SET_HEADER_LENGTH, M_NULLPTR))
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
                if (SEC_FILE_SUCCESS != secure_Write_File(farmCombinedLog, farmLongSavedHeader,
                                                          FARMC_LOG_DATA_SET_HEADER_LENGTH, sizeof(uint8_t),
                                                          FARMC_LOG_DATA_SET_HEADER_LENGTH, M_NULLPTR))
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
                if (SEC_FILE_SUCCESS != secure_Write_File(farmCombinedLog, farmStickyHeader,
                                                          FARMC_LOG_DATA_SET_HEADER_LENGTH, sizeof(uint8_t),
                                                          FARMC_LOG_DATA_SET_HEADER_LENGTH, M_NULLPTR))
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
                if (SEC_FILE_SUCCESS != secure_Write_File(farmCombinedLog, farmWorkLoadTraceHeader,
                                                          FARMC_LOG_DATA_SET_HEADER_LENGTH, sizeof(uint8_t),
                                                          FARMC_LOG_DATA_SET_HEADER_LENGTH, M_NULLPTR))
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

            // write zero padding
            returnValue = write_FARM_Zero_Padding(zeroPaddingBufferSize.headerZeroPadding, farmCombinedLog);
            if (returnValue != SUCCESS)
            {
                break;
            }
        }

        // write log buffer
        {
            if (farmContentField & BIT0)
            {
                uint32_t* datasetlength = C_CAST(uint32_t*, farmCurrentHeader + 12);
                if (SEC_FILE_SUCCESS != secure_Write_File(farmCombinedLog, farmCurrentLog.ptr, *datasetlength,
                                                          sizeof(uint8_t), *datasetlength, M_NULLPTR))
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

                // add zero padding
                returnValue = write_FARM_Zero_Padding(zeroPaddingBufferSize.farmCurrentZeroPadding, farmCombinedLog);
                if (returnValue != SUCCESS)
                {
                    break;
                }
            }

            if (farmContentField & BIT1)
            {
                uint32_t* datasetlength = C_CAST(uint32_t*, farmFactoryHeader + 12);
                if (SEC_FILE_SUCCESS != secure_Write_File(farmCombinedLog, farmFactoryLog.ptr, *datasetlength,
                                                          sizeof(uint8_t), *datasetlength, M_NULLPTR))
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

                // add zero padding
                returnValue = write_FARM_Zero_Padding(zeroPaddingBufferSize.farmFactoryZeroPadding, farmCombinedLog);
                if (returnValue != SUCCESS)
                {
                    break;
                }
            }

            if (farmContentField & BIT5)
            {
                uint32_t* datasetlength = C_CAST(uint32_t*, farmSavedHeader + 12);
                if (SEC_FILE_SUCCESS != secure_Write_File(farmCombinedLog, farmSavedLog.ptr, *datasetlength,
                                                          sizeof(uint8_t), *datasetlength, M_NULLPTR))
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

                // add zero padding
                returnValue = write_FARM_Zero_Padding(zeroPaddingBufferSize.farmSavedZeroPadding, farmCombinedLog);
                if (returnValue != SUCCESS)
                {
                    break;
                }
            }

            if (farmContentField & BIT2)
            {
                uint32_t* datasetlength = C_CAST(uint32_t*, farmTimeSeriesHeader + 12);
                if (SEC_FILE_SUCCESS != secure_Write_File(farmCombinedLog, farmTimeSeriesLog.ptr, *datasetlength,
                                                          sizeof(uint8_t), *datasetlength, M_NULLPTR))
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

                // add zero padding
                returnValue = write_FARM_Zero_Padding(zeroPaddingBufferSize.farmTimeSeriesZeroPadding, farmCombinedLog);
                if (returnValue != SUCCESS)
                {
                    break;
                }
            }

            if (farmContentField & BIT6)
            {
                uint32_t* datasetlength = C_CAST(uint32_t*, farmLongSavedHeader + 12);
                if (SEC_FILE_SUCCESS != secure_Write_File(farmCombinedLog, farmLongSavedLog.ptr, *datasetlength,
                                                          sizeof(uint8_t), *datasetlength, M_NULLPTR))
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

                // add zero padding
                returnValue = write_FARM_Zero_Padding(zeroPaddingBufferSize.farmLongSavedZeroPadding, farmCombinedLog);
                if (returnValue != SUCCESS)
                {
                    break;
                }
            }

            if (farmContentField & BIT3)
            {
                uint32_t* datasetlength = C_CAST(uint32_t*, farmStickyHeader + 12);
                if (SEC_FILE_SUCCESS != secure_Write_File(farmCombinedLog, farmStickyLog.ptr, *datasetlength,
                                                          sizeof(uint8_t), *datasetlength, M_NULLPTR))
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

                // add zero padding
                returnValue = write_FARM_Zero_Padding(zeroPaddingBufferSize.farmStickyZeroPadding, farmCombinedLog);
                if (returnValue != SUCCESS)
                {
                    break;
                }
            }

            if (farmContentField & BIT4)
            {
                uint32_t* datasetlength = C_CAST(uint32_t*, farmWorkLoadTraceHeader + 12);
                if (SEC_FILE_SUCCESS != secure_Write_File(farmCombinedLog, farmWorkLoadTraceLog.ptr, *datasetlength,
                                                          sizeof(uint8_t), *datasetlength, M_NULLPTR))
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

    safe_free_aligned(&farmCurrentLog.ptr);
    safe_free_aligned(&farmFactoryLog.ptr);
    safe_free_aligned(&farmSavedLog.ptr);
    safe_free_aligned(&farmTimeSeriesLog.ptr);
    safe_free_aligned(&farmLongSavedLog.ptr);
    safe_free_aligned(&farmStickyLog.ptr);
    safe_free_aligned(&farmWorkLoadTraceLog.ptr);
    safe_free_aligned(&farmCurrentZeroPaddingBuffer);
    safe_free_aligned(&farmFactoryZeroPaddingBuffer);
    safe_free_aligned(&farmSavedZeroPaddingBuffer);
    safe_free_aligned(&farmTimeSeriesZeroPaddingBuffer);
    safe_free_aligned(&farmLongSavedZeroPaddingBuffer);
    safe_free_aligned(&farmStickyZeroPaddingBuffer);

    return returnValue;
}

// This function is generic to read any FARM page.
// This may do some unexpected things with fields that have strings though!
// NOTE: Make a function to handle string extraction from various fields after this function has run.-TJE
static farmGenericPage* generic_SATA_Read_FARM_Log(uint8_t* ptrData, uint32_t dataLength, farmGenericPage* farmStruct)
{
    DISABLE_NONNULL_COMPARE
    if (ptrData != M_NULLPTR && dataLength >= FARM_PAGE_LEN && farmStruct != M_NULLPTR)
    {
        // qwordptr since all fields are qwords
        uint64_t* qwordptr = M_REINTERPRET_CAST(uint64_t*, ptrData);
        // start by zeroing it all out
        safe_memset(farmStruct, sizeof(farmGenericPage), 0, sizeof(farmGenericPage));
        // set page # and copy #
        farmStruct->pageNumber = le64_to_host(qwordptr[0]);
        farmStruct->copyNumber = le64_to_host(qwordptr[1]);
        for (uint16_t fieldIter = UINT16_C(0); fieldIter < FARM_LOG_MAX_FIELDS_PER_PAGE; ++fieldIter)
        {
            // TODO: May need a way to not do byteswapping when reading ASCII fields. Will need to test to be
            // certain.-TJE
            farmStruct->fields[fieldIter] = le64_to_host(qwordptr[2 + fieldIter]);
        }
    }
    RESTORE_NONNULL_COMPARE
    return farmStruct;
}

// ptrdata points directly to the parameter code at the beginning of parameter data!
static void sas_Read_FARM_Header_Param(uint8_t* ptrData, farmHeader* header)
{
    if (ptrData != M_NULLPTR && header != M_NULLPTR)
    {
        uint16_t parameter = bytes_To_Uint16(ptrData[0], ptrData[1]);
        if (parameter == 0x0000)
        {
            uint64_t* qwordptr = M_REINTERPRET_CAST(uint64_t*, &ptrData[LOG_PAGE_HEADER_LENGTH]);
            // TODO: Signature may not need byteswapping
            header->signature              = be64_to_host(qwordptr[0]);
            header->majorVersion           = be64_to_host(qwordptr[1]);
            header->minorVersion           = be64_to_host(qwordptr[2]);
            header->numberOfPagesSupported = be64_to_host(qwordptr[3]);
            header->logSizeInBytes         = be64_to_host(qwordptr[4]);
            header->maxDriveHeadsSupported = be64_to_host(qwordptr[6]);
            header->reasonForFrameCapture  = be64_to_host(qwordptr[8]);
        }
    }
}

static M_INLINE uint32_t sas_ASCII_DWord_Replace_Null_With_Space(uint32_t asciiDWord)
{
    if (M_Byte0(asciiDWord) == UINT8_C(0))
    {
        asciiDWord |= UINT32_C(0x20);
    }
    if (M_Byte1(asciiDWord) == UINT8_C(0))
    {
        asciiDWord |= UINT32_C(0x2000);
    }
    if (M_Byte2(asciiDWord) == UINT8_C(0))
    {
        asciiDWord |= UINT32_C(0x200000);
    }
    if (M_Byte3(asciiDWord) == UINT8_C(0))
    {
        asciiDWord |= UINT32_C(0x20000000);
    }
    return asciiDWord;
}

// NOTE: Assumes each FARM ASCII str is 2 qwords long
static M_INLINE void sas_FARM_ASCII_Field(uint64_t* firstQword, uint64_t* firstSTR64)
{
    if (firstQword != M_NULLPTR && firstSTR64 != M_NULLPTR)
    {
        uint32_t asciidword = UINT32_C(0);
        // start with the SECOND qword
        *(firstSTR64 + 1) = be64_to_host(*firstQword);
        asciidword        = sas_ASCII_DWord_Replace_Null_With_Space(get_DWord0(*(firstSTR64 + 1)));
        *(firstSTR64 + 1) &= UINT64_C(0xFFFFFFFF00000000);
        *(firstSTR64 + 1) |= w_swap_32(asciidword);
        // now the first
        *firstSTR64 = be64_to_host(*(firstQword + 1));
        asciidword  = sas_ASCII_DWord_Replace_Null_With_Space(get_DWord0(*firstSTR64));
        *firstSTR64 &= UINT64_C(0xFFFFFFFF00000000);
        *firstSTR64 |= w_swap_32(asciidword);
    }
}

static M_INLINE void sas_FARM_ASCII_Field_In_Order(uint64_t* firstQword, uint64_t* firstSTR64, size_t maxFieldLen)
{
    if (firstQword != M_NULLPTR && firstSTR64 != M_NULLPTR)
    {
        for (size_t offset = UINT64_C(0); offset < maxFieldLen; ++offset)
        {
            uint32_t asciidword    = UINT32_C(0);
            *(firstSTR64 + offset) = be64_to_host(*(firstQword + offset));
            asciidword             = sas_ASCII_DWord_Replace_Null_With_Space(get_DWord0(*(firstSTR64 + offset)));
            *(firstSTR64 + offset) &= UINT64_C(0xFFFFFFFF00000000);
            *(firstSTR64 + offset) |= w_swap_32(asciidword);
        }
    }
}

static M_INLINE void sas_FARM_WWN_Field(uint64_t* firstQword, uint64_t* firstSTR64)
{
    if (firstQword != M_NULLPTR && firstSTR64 != M_NULLPTR)
    {
        uint32_t asciidword = UINT32_C(0);
        // start with the SECOND qword
        *(firstSTR64 + 1) = be64_to_host(*firstQword);
        asciidword        = get_DWord0(*(firstSTR64 + 1));
        *(firstSTR64 + 1) &= UINT64_C(0xFFFFFFFF00000000);
        *(firstSTR64 + 1) |= w_swap_32(asciidword);
        // now the first
        *firstSTR64 = be64_to_host(*(firstQword + 1));
        asciidword  = get_DWord0(*firstSTR64);
        *firstSTR64 &= UINT64_C(0xFFFFFFFF00000000);
        *firstSTR64 |= w_swap_32(asciidword);
    }
}

static void sas_Read_FARM_General_Drive_Info(uint8_t* ptrData, farmDriveInfo* info)
{
    if (ptrData != M_NULLPTR && info != M_NULLPTR)
    {
        uint64_t* qwordptr  = M_NULLPTR;
        uint16_t  parameter = bytes_To_Uint16(ptrData[0], ptrData[1]);
        if (parameter == 0x0001)
        {
            qwordptr         = M_REINTERPRET_CAST(uint64_t*, &ptrData[LOG_PAGE_HEADER_LENGTH]);
            info->pageNumber = be64_to_host(qwordptr[0]);
            info->copyNumber = be64_to_host(qwordptr[1]);
            sas_FARM_ASCII_Field(&qwordptr[2], info->sn);
            sas_FARM_WWN_Field(&qwordptr[4], info->wwn);
            info->driveInterface = be64_to_host(qwordptr[6]);
            uint32_t asciidword  = sas_ASCII_DWord_Replace_Null_With_Space(get_DWord0(info->driveInterface));
            info->driveInterface &= UINT64_C(0xFFFFFFFF00000000);
            info->driveInterface |= asciidword;
            info->driveCapacity      = be64_to_host(qwordptr[7]);
            info->physicalSectorSize = be64_to_host(qwordptr[8]);
            info->logicalSectorSize  = be64_to_host(qwordptr[9]);
            info->deviceBufferSize   = be64_to_host(qwordptr[10]);
            info->numberOfHeads      = be64_to_host(qwordptr[11]);
            info->deviceFormFactor   = be64_to_host(qwordptr[12]);
            info->rotationRate       = be64_to_host(qwordptr[13]);
            sas_FARM_ASCII_Field(&qwordptr[14], info->fwrev);
            info->powerOnHours                          = be64_to_host(qwordptr[19]);
            info->powerCycleCount                       = be64_to_host(qwordptr[23]);
            info->hardwareResetCount                    = be64_to_host(qwordptr[24]);
            info->nvcStatusOnPoweron                    = be64_to_host(qwordptr[26]);
            info->timeAvailableToSaveUDToNVMem          = be64_to_host(qwordptr[27]);
            info->lowestPOHForTimeRestrictedParameters  = be64_to_host(qwordptr[28]);
            info->highestPOHForTimeRestrictedParameters = be64_to_host(qwordptr[29]);

            info->dateOfAssembly = be64_to_host(qwordptr[30]);
            asciidword           = sas_ASCII_DWord_Replace_Null_With_Space(get_DWord0(info->dateOfAssembly));
            info->dateOfAssembly &= UINT64_C(0xFFFFFFFF00000000);
            info->dateOfAssembly |= b_swap_32(asciidword);
        }
        else if (parameter == 0x0006)
        {
            qwordptr = M_REINTERPRET_CAST(uint64_t*, &ptrData[LOG_PAGE_HEADER_LENGTH]);
            // emulating SATA so skip page number and copy
            info->depopulationHeadMask = be64_to_host(qwordptr[2]);
            sas_FARM_ASCII_Field_In_Order(&qwordptr[3], info->modelNumber, 4);
            info->driveRecordingType                                    = be64_to_host(qwordptr[7]);
            info->isDriveDepopulated                                    = be64_to_host(qwordptr[8]);
            info->maxAvailableSectorsForReassignment                    = be64_to_host(qwordptr[9]);
            info->timeToReadyOfLastPowerCycle                           = be64_to_host(qwordptr[10]);
            info->timeDriveHeldInStaggeredSpinDuringLastPowerOnSequence = be64_to_host(qwordptr[11]);
            info->spinUpTimeMilliseconds                                = be64_to_host(qwordptr[12]);
            info->hamrDataProtectStatus                                 = be64_to_host(qwordptr[13]);
            info->regenHeadMask                                         = be64_to_host(qwordptr[14]);
            info->lowestPOHForTimeRestrictedParameters                  = be64_to_host(qwordptr[15]);
            info->highestPOHForTimeRestrictedParameters                 = be64_to_host(qwordptr[16]);
        }
    }
}

static void sas_Read_FARM_Workload_Info(uint8_t* ptrData, farmWorkload* work)
{
    if (ptrData != M_NULLPTR && work != M_NULLPTR)
    {
        uint64_t* qwordptr  = M_NULLPTR;
        uint16_t  parameter = bytes_To_Uint16(ptrData[0], ptrData[1]);
        if (parameter == 0x0002)
        {
            qwordptr                                 = M_REINTERPRET_CAST(uint64_t*, &ptrData[LOG_PAGE_HEADER_LENGTH]);
            work->pageNumber                         = be64_to_host(qwordptr[0]);
            work->copyNumber                         = be64_to_host(qwordptr[1]);
            work->ratedWorkloadPercentage            = be64_to_host(qwordptr[2]);
            work->totalReadCommands                  = be64_to_host(qwordptr[3]);
            work->totalWriteCommands                 = be64_to_host(qwordptr[4]);
            work->totalRandomReadCommands            = be64_to_host(qwordptr[5]);
            work->totalRandomWriteCommands           = be64_to_host(qwordptr[6]);
            work->totalOtherCommands                 = be64_to_host(qwordptr[7]);
            work->logicalSectorsWritten              = be64_to_host(qwordptr[8]);
            work->logicalSectorsRead                 = be64_to_host(qwordptr[9]);
            work->numReadsInLBA0To3125PercentRange   = be64_to_host(qwordptr[10]);
            work->numReadsInLBA0To3125PercentRange   = be64_to_host(qwordptr[11]);
            work->numReadsInLBA3125To25PercentRange  = be64_to_host(qwordptr[12]);
            work->numReadsInLBA25To50PercentRange    = be64_to_host(qwordptr[13]);
            work->numReadsInLBA50To100PercentRange   = be64_to_host(qwordptr[14]);
            work->numWritesInLBA0To3125PercentRange  = be64_to_host(qwordptr[15]);
            work->numWritesInLBA3125To25PercentRange = be64_to_host(qwordptr[16]);
            work->numWritesInLBA25To50PercentRange   = be64_to_host(qwordptr[17]);
            work->numWritesInLBA50To100PercentRange  = be64_to_host(qwordptr[18]);
            work->numReadsOfXferLenLT16KB            = be64_to_host(qwordptr[19]);
            work->numReadsOfXferLen16KBTo512KB       = be64_to_host(qwordptr[20]);
            work->numReadsOfXferLen512KBTo2MB        = be64_to_host(qwordptr[21]);
            work->numReadsOfXferLenGT2MB             = be64_to_host(qwordptr[22]);
            work->numWritesOfXferLenLT16KB           = be64_to_host(qwordptr[23]);
            work->numWritesOfXferLen16KBTo512KB      = be64_to_host(qwordptr[24]);
            work->numWritesOfXferLen512KBTo2MB       = be64_to_host(qwordptr[25]);
            work->numWritesOfXferLenGT2MB            = be64_to_host(qwordptr[26]);
        }
        else if (parameter == 0x0008)
        {
            qwordptr                         = M_REINTERPRET_CAST(uint64_t*, &ptrData[LOG_PAGE_HEADER_LENGTH]);
            work->countQD1at30sInterval      = be64_to_host(qwordptr[2]);
            work->countQD2at30sInterval      = be64_to_host(qwordptr[3]);
            work->countQD3To4at30sInterval   = be64_to_host(qwordptr[4]);
            work->countQD5To8at30sInterval   = be64_to_host(qwordptr[5]);
            work->countQD9To16at30sInterval  = be64_to_host(qwordptr[6]);
            work->countQD17To32at30sInterval = be64_to_host(qwordptr[7]);
            work->countQD33To64at30sInterval = be64_to_host(qwordptr[8]);
            work->countGTQD64at30sInterval   = be64_to_host(qwordptr[9]);
        }
    }
}

// Assumes FARM_MAX_HEADS for array size!!!!
static M_INLINE void sas_Fill_By_Head_Data(uint8_t* paramptr, uint8_t paramlen, uint64_t* byheadarray)
{
    if (paramptr != M_NULLPTR && byheadarray != M_NULLPTR)
    {
        uint64_t* qwordptr = M_REINTERPRET_CAST(uint64_t*, &paramptr[0]);
        uint8_t   maxHeads = paramlen / UINT8_C(8);
        for (uint8_t headiter = UINT8_C(0); headiter < maxHeads && headiter < FARM_MAX_HEADS; ++headiter)
        {
            byheadarray[headiter] = be64_to_host(qwordptr[headiter]);
        }
    }
}

static M_INLINE void sas_Fill_By_Head_Data_2D_Array(uint8_t* paramptr,
                                                    uint8_t  paramlen,
                                                    uint64_t (*byheadarray)[3],
                                                    unsigned int dimension)
{
    if (paramptr != M_NULLPTR && byheadarray != M_NULLPTR && dimension < 3U)
    {
        uint64_t* qwordptr = M_REINTERPRET_CAST(uint64_t*, &paramptr[0]);
        uint8_t   maxHeads = paramlen / UINT8_C(8);
        for (uint8_t headiter = UINT8_C(0); headiter < maxHeads && headiter < FARM_MAX_HEADS; ++headiter)
        {
            (byheadarray[headiter])[dimension] = be64_to_host(qwordptr[headiter]);
        }
    }
}

static void sas_Read_FARM_Error_Info(uint8_t* ptrData, farmErrorStatistics* error)
{
    if (ptrData != M_NULLPTR && error != M_NULLPTR)
    {
        uint64_t* qwordptr  = M_REINTERPRET_CAST(uint64_t*, &ptrData[LOG_PAGE_HEADER_LENGTH]);
        uint16_t  parameter = bytes_To_Uint16(ptrData[0], ptrData[1]);
        uint8_t   paramlen  = ptrData[3];
        switch (parameter)
        {
        case 0x0003:
            error->pageNumber                               = be64_to_host(qwordptr[0]);
            error->copyNumber                               = be64_to_host(qwordptr[1]);
            error->numberOfUnrecoverableReadErrors          = be64_to_host(qwordptr[2]);
            error->numberOfUnrecoverableWriteErrors         = be64_to_host(qwordptr[3]);
            error->numberOfReadRecoveryAttempts             = be64_to_host(qwordptr[4]);
            error->numberOfMechanicalStartRetries           = be64_to_host(qwordptr[6]);
            error->sasFRUTrips[0]                           = be64_to_host(qwordptr[8]);
            error->sasFRUTrips[1]                           = be64_to_host(qwordptr[9]);
            error->sasErr.fruCodeOfSMARTTripMostRecentFrame = be64_to_host(qwordptr[11]);
            error->sasErr.portAinvDWordCount                = be64_to_host(qwordptr[12]);
            error->sasErr.portBinvDWordCount                = be64_to_host(qwordptr[13]);
            error->sasErr.portADisparityErrCount            = be64_to_host(qwordptr[14]);
            error->sasErr.portBDisparityErrCount            = be64_to_host(qwordptr[15]);
            error->sasErr.portAlossOfDWordSync              = be64_to_host(qwordptr[16]);
            error->sasErr.portBlossOfDWordSync              = be64_to_host(qwordptr[17]);
            error->sasErr.portAphyResetProblem              = be64_to_host(qwordptr[18]);
            error->sasErr.portBphyResetProblem              = be64_to_host(qwordptr[19]);
            break;
        case 0x0028:
            sas_Fill_By_Head_Data(ptrData + 4, paramlen, error->cumLTUnrecReadRepeatByHead);
            break;
        case 0x0029:
            sas_Fill_By_Head_Data(ptrData + 4, paramlen, error->cumLTUnrecReadUniqueByHead);
            break;
        case 0x0052:
            error->numberOfReallocatedSectors                                              = be64_to_host(qwordptr[3]);
            error->numberOfReallocationCandidateSectors                                    = be64_to_host(qwordptr[4]);
            error->numberReallocatedSectorsSinceLastFARMTimeSeriesFrameSaved               = be64_to_host(qwordptr[5]);
            error->numberReallocatedSectorsBetweenFarmTimeSeriesFrameNandNminus1           = be64_to_host(qwordptr[6]);
            error->numberReallocationCandidateSectorsSinceLastFARMTimeSeriesFrameSaved     = be64_to_host(qwordptr[7]);
            error->numberReallocationCandidateSectorsBetweenFarmTimeSeriesFrameNandNminus1 = be64_to_host(qwordptr[8]);
            break;
        case 0x0062:
            error->numberOfReallocatedSectorsActuator1                                    = be64_to_host(qwordptr[3]);
            error->numberOfReallocationCandidateSectorsActuator1                          = be64_to_host(qwordptr[4]);
            error->numberReallocatedSectorsSinceLastFARMTimeSeriesFrameSavedActuator1     = be64_to_host(qwordptr[5]);
            error->numberReallocatedSectorsBetweenFarmTimeSeriesFrameNandNminus1Actuator1 = be64_to_host(qwordptr[6]);
            error->numberReallocationCandidateSectorsSinceLastFARMTimeSeriesFrameSavedActuator1 =
                be64_to_host(qwordptr[7]);
            error->numberReallocationCandidateSectorsBetweenFarmTimeSeriesFrameNandNminus1Actuator1 =
                be64_to_host(qwordptr[8]);
            break;
        case 0x0107:
            sas_Fill_By_Head_Data(ptrData + 4, paramlen,
                                  error->numberUniqueUnrecoverableSectorsSinceLastFARMTimeSeriesFrameSavedByHead);
            break;
        case 0x0108:
            sas_Fill_By_Head_Data(ptrData + 4, paramlen,
                                  error->numberUniqueUnrecoverableSectorsBetweenFarmTimeSeriesFrameNandNminus1ByHead);
            break;
        }
    }
}

static void sas_Read_FARM_Environment_Info(uint8_t* ptrData, farmEnvironmentStatistics* env)
{
    if (ptrData != M_NULLPTR && env != M_NULLPTR)
    {
        uint64_t* qwordptr  = M_NULLPTR;
        uint16_t  parameter = bytes_To_Uint16(ptrData[0], ptrData[1]);
        if (parameter == 0x0004)
        {
            qwordptr                     = M_REINTERPRET_CAST(uint64_t*, &ptrData[LOG_PAGE_HEADER_LENGTH]);
            env->pageNumber              = be64_to_host(qwordptr[0]);
            env->copyNumber              = be64_to_host(qwordptr[1]);
            env->currentTemperature      = be64_to_host(qwordptr[2]);
            env->highestTemperature      = be64_to_host(qwordptr[3]);
            env->lowestTemperature       = be64_to_host(qwordptr[4]);
            env->specifiedMaxTemp        = be64_to_host(qwordptr[13]);
            env->specifiedMinTemp        = be64_to_host(qwordptr[14]);
            env->currentRelativeHumidity = be64_to_host(qwordptr[17]);
            env->currentMotorPowerFromMostRecentSMARTSummaryFrame = be64_to_host(qwordptr[19]);
            env->average12Vpwr                                    = be64_to_host(qwordptr[20]);
            env->min12VPwr                                        = be64_to_host(qwordptr[21]);
            env->max12VPwr                                        = be64_to_host(qwordptr[22]);
            env->average5Vpwr                                     = be64_to_host(qwordptr[23]);
            env->min5Vpwr                                         = be64_to_host(qwordptr[24]);
            env->max5Vpwr                                         = be64_to_host(qwordptr[25]);
        }
        else if (parameter == 0x0007)
        {
            qwordptr             = M_REINTERPRET_CAST(uint64_t*, &ptrData[LOG_PAGE_HEADER_LENGTH]);
            env->current12Vinput = be64_to_host(qwordptr[2]);
            env->min12Vinput     = be64_to_host(qwordptr[3]);
            env->max12Vinput     = be64_to_host(qwordptr[4]);
            env->current5Vinput  = be64_to_host(qwordptr[5]);
            env->min5Vinput      = be64_to_host(qwordptr[6]);
            env->max5Vinput      = be64_to_host(qwordptr[7]);
        }
    }
}

static void sas_Read_FARM_Reliability_Info(uint8_t*                   ptrData,
                                           farmReliabilityStatistics* reli,
                                           farmErrorStatistics*       error,
                                           farmDriveInfo*             info)
{
    if (ptrData != M_NULLPTR)
    {
        uint64_t* qwordptr  = M_REINTERPRET_CAST(uint64_t*, &ptrData[LOG_PAGE_HEADER_LENGTH]);
        uint16_t  parameter = bytes_To_Uint16(ptrData[0], ptrData[1]);
        uint8_t   paramlen  = ptrData[3];
        switch (parameter)
        {
        case 0x0005:
            if (reli != M_NULLPTR)
            {
                reli->pageNumber       = be64_to_host(qwordptr[0]);
                reli->copyNumber       = be64_to_host(qwordptr[1]);
                reli->numRAWOperations = be64_to_host(qwordptr[15]);
            }
            if (error != M_NULLPTR)
            {
                error->cumulativeLifetimeUnrecoverableReadErrorsDueToERC = be64_to_host(qwordptr[16]);
            }
            if (reli != M_NULLPTR)
            {
                reli->highPriorityUnloadEvents           = be64_to_host(qwordptr[22]);
                reli->numDiscSlipRecalibrationsPerformed = be64_to_host(qwordptr[24]);
                reli->heliumPressureThresholdTrip        = be64_to_host(qwordptr[25]);
            }
            break;
        case 0x001A: // head resistance (reliability)
            sas_Fill_By_Head_Data(ptrData + 4, paramlen, reli->mrHeadResistanceByHead);
            break;
        case 0x001F: // h2SAT by head (reliability)
            sas_Fill_By_Head_Data(ptrData + 4, paramlen, reli->currentH2SATamplitudeByHeadTZAvg);
            break;
        case 0x0020: // h2SAT by head (reliability)
            sas_Fill_By_Head_Data(ptrData + 4, paramlen, reli->currentH2SATasymmetryByHeadTZAvg);
            break;
        case 0x0021: // realloc by head (reliability)
            sas_Fill_By_Head_Data(ptrData + 4, paramlen, reli->numReallocatedSectorsByHead);
            break;
        case 0x0022: // realloc candidate by head (reliability)
            sas_Fill_By_Head_Data(ptrData + 4, paramlen, reli->numReallocationCandidateSectorsByHead);
            break;
        case 0x0026: // write workload by head (reliability)
            sas_Fill_By_Head_Data(ptrData + 4, paramlen, reli->writeWorkloadPowerOnTimeByHead);
            break;
        case 0x0030: // h2SAT by head (reliability)
            sas_Fill_By_Head_Data_2D_Array(ptrData + 4, paramlen,
                                           &reli->currentH2SATtrimmedMeanBitsInErrorByHeadZone[0], 0);
            break;
        case 0x0031: // h2SAT by head (reliability)
            sas_Fill_By_Head_Data_2D_Array(ptrData + 4, paramlen,
                                           &reli->currentH2SATtrimmedMeanBitsInErrorByHeadZone[0], 1);
            break;
        case 0x0032: // h2SAT by head (reliability)
            sas_Fill_By_Head_Data_2D_Array(ptrData + 4, paramlen,
                                           &reli->currentH2SATtrimmedMeanBitsInErrorByHeadZone[0], 2);
            break;
        case 0x0033: // h2SAT by head (reliability)
            sas_Fill_By_Head_Data_2D_Array(ptrData + 4, paramlen, &reli->currentH2SATiterationsToConvergeByHeadZone[0],
                                           0);
            break;
        case 0x0034: // h2SAT by head (reliability)
            sas_Fill_By_Head_Data_2D_Array(ptrData + 4, paramlen, &reli->currentH2SATiterationsToConvergeByHeadZone[0],
                                           1);
            break;
        case 0x0035: // h2SAT by head (reliability)
            sas_Fill_By_Head_Data_2D_Array(ptrData + 4, paramlen, &reli->currentH2SATiterationsToConvergeByHeadZone[0],
                                           2);
            break;
        case 0x0043: // second mr head resistance by head (reliability)
            sas_Fill_By_Head_Data(ptrData + 4, paramlen, reli->secondHeadMRHeadResistanceByHead);
            break;
        case 0x0050: // actuator 0 reli stats (reliability)
            if (info != M_NULLPTR)
            {
                info->headLoadEvents = be64_to_host(qwordptr[3]);
            }
            if (reli != M_NULLPTR)
            {
                reli->numDOSScansPerformed                             = be64_to_host(qwordptr[16]);
                reli->numLBAsCorrectedByISP                            = be64_to_host(qwordptr[17]);
                reli->numLBAsCorrectedByParitySector                   = be64_to_host(qwordptr[22]);
                reli->superParityCoveragePercent                       = be64_to_host(qwordptr[29]);
                reli->primarySuperParityCoveragePercentageSMR_HSMR_SWR = be64_to_host(qwordptr[30]);
            }
            break;
        case 0x0051: // actuator 0 fled stats (reliability)
            if (error != M_NULLPTR)
            {
                error->totalFlashLEDEvents = be64_to_host(qwordptr[3]);
                error->lastFLEDIndex       = be64_to_host(qwordptr[4]);
                for (uint8_t fledIndex = UINT8_C(0); fledIndex < UINT8_C(8); ++fledIndex)
                {
                    error->last8FLEDEvents[fledIndex]        = be64_to_host(qwordptr[5 + fledIndex]);
                    error->timestampOfLast8FLEDs[fledIndex]  = be64_to_host(qwordptr[14 + fledIndex]);
                    error->powerCycleOfLast8FLEDs[fledIndex] = be64_to_host(qwordptr[22 + fledIndex]);
                }
            }
            break;
        case 0x0060: // actuator 1 reli stats (reliability)
            if (info != M_NULLPTR)
            {
                info->headLoadEventsActuator1 = be64_to_host(qwordptr[3]);
            }
            if (error != M_NULLPTR)
            {
                reli->numDOSScansPerformedActuator1           = be64_to_host(qwordptr[16]);
                reli->numLBAsCorrectedByISPActuator1          = be64_to_host(qwordptr[17]);
                reli->numLBAsCorrectedByParitySectorActuator1 = be64_to_host(qwordptr[22]);
                // TODO: Where was this on SATA? Was it missed???? Need to add it to this structure somewhere-TJE
                // reli->superParityCoveragePercentActuator1 = be64_to_host(qwordptr[29]);
                reli->primarySuperParityCoveragePercentageSMR_HSMR_SWRActuator1 = be64_to_host(qwordptr[30]);
            }
            break;
        case 0x0061: // actuator 1 fled stats (reliability)
            if (error != M_NULLPTR)
            {
                error->totalFlashLEDEventsActuator1 = be64_to_host(qwordptr[3]);
                error->lastFLEDIndexActuator1       = be64_to_host(qwordptr[4]);
                for (uint8_t fledIndex = UINT8_C(0); fledIndex < UINT8_C(8); ++fledIndex)
                {
                    error->last8FLEDEventsActuator1[fledIndex]        = be64_to_host(qwordptr[5 + fledIndex]);
                    error->timestampOfLast8FLEDsActuator1[fledIndex]  = be64_to_host(qwordptr[14 + fledIndex]);
                    error->powerCycleOfLast8FLEDsActuator1[fledIndex] = be64_to_host(qwordptr[22 + fledIndex]);
                }
            }
            break;
        case 0x0100: // terabytes written by head (reliability)
            sas_Fill_By_Head_Data(ptrData + 4, paramlen, reli->lifetimeTerabytesWrittenperHead);
            break;
        }
    }
}

// NOTE: All fields in SAS are big endian.
//       Unlike SATA, there is not as much padding or reserved bytes around to deal with.
//       ASCII data may require special handling in both SAS and SATA FARM logs. These are mostly on page 1 (generic
//       drive info)
static farmLogData* sas_Read_FARM_Log(uint8_t* ptrData, uint32_t dataLength, farmLogData* farmdata)
{
    DISABLE_NONNULL_COMPARE
    if (ptrData != M_NULLPTR && farmdata != M_NULLPTR)
    {
        // First verify page/subpage codes
        uint8_t page    = get_bit_range_uint8(ptrData[0], 5, 0);
        uint8_t subpage = ptrData[1];
        if (page == 0x3D && (subpage == 0x03 || subpage == 0x04))
        {
            uint16_t pagelen      = bytes_To_Uint16(ptrData[2], ptrData[3]);
            uint16_t parameterlen = UINT8_C(0);
            for (uint32_t farmoffset = LOG_PAGE_HEADER_LENGTH;
                 farmoffset < (M_STATIC_CAST(uint32_t, pagelen) + LOG_PAGE_HEADER_LENGTH) && farmoffset < dataLength;
                 farmoffset += parameterlen + 4)
            {
                uint16_t parameterCode = bytes_To_Uint16(ptrData[farmoffset], ptrData[farmoffset + 1]);
                parameterlen           = ptrData[farmoffset + 3];
                // printf("Parameter %" PRIX16 "h = len %" PRIu8 "\n", parameterCode, parameterlen);
                if (parameterlen == UINT8_C(0))
                {
                    break;
                }
                switch (parameterCode)
                {
                case 0x0000: // header
                    sas_Read_FARM_Header_Param(&ptrData[farmoffset], &farmdata->header);
                    break;
                case 0x0001: // drive info
                case 0x0006: // Drive info continued
                    sas_Read_FARM_General_Drive_Info(&ptrData[farmoffset], &farmdata->driveinfo);
                    break;
                case 0x0002: // Workload
                case 0x0008: // workload continued
                    sas_Read_FARM_Workload_Info(&ptrData[farmoffset], &farmdata->workload);
                    break;
                case 0x0003: // error
                case 0x0028: // cumulative lifetime unrec read repeat by head (error)
                case 0x0029: // cumulative lifetime unrec read unique by head (error)
                case 0x0052: // actuator 0 realloc stats (error)
                case 0x0062: // actuator 1 realloc stats (error)
                case 0x0107: // unique unrec read since last frame (error)
                case 0x0108: // unique unrec read between N, N-1 frame (error)
                    sas_Read_FARM_Error_Info(&ptrData[farmoffset], &farmdata->error);
                    break;
                case 0x0004: // environment
                case 0x0007: // environment continued
                    sas_Read_FARM_Environment_Info(&ptrData[farmoffset], &farmdata->environment);
                    break;
                case 0x0005: // reliability
                case 0x001A: // head resistance (reliability)
                case 0x001F: // h2SAT by head (reliability)
                case 0x0020: // h2SAT by head (reliability)
                case 0x0021: // realloc by head (reliability)
                case 0x0022: // realloc candidate by head (reliability)
                case 0x0026: // write workload by head (reliability)
                case 0x0030: // h2SAT by head (reliability)
                case 0x0031: // h2SAT by head (reliability)
                case 0x0032: // h2SAT by head (reliability)
                case 0x0033: // h2SAT by head (reliability)
                case 0x0034: // h2SAT by head (reliability)
                case 0x0035: // h2SAT by head (reliability)
                case 0x0043: // second mr head resistance by head (reliability)
                case 0x0050: // actuator 0 reli stats (reliability)
                case 0x0051: // actuator 0 fled stats (reliability)
                case 0x0060: // actuator 1 reli stats (reliability)
                case 0x0061: // actuator 1 fled stats (reliability)
                case 0x0100: // terabytes written by head (reliability)
                    sas_Read_FARM_Reliability_Info(&ptrData[farmoffset], &farmdata->reliability, &farmdata->error,
                                                   &farmdata->driveinfo);
                    break;
                }
            }
        }
    }
    RESTORE_NONNULL_COMPARE
    return farmdata;
}

static farmLogData* sata_Read_FARM_Log(uint8_t* ptrData, uint32_t dataLength, farmLogData* farmdata)
{
    DISABLE_NONNULL_COMPARE
    if (ptrData != M_NULLPTR && farmdata != M_NULLPTR)
    {
        // for each 16KB page, call the generic SATA read function. A bit simpler than the SAS implementation :) - TJE
        for (uint32_t offset = UINT32_C(0);
             offset < dataLength && offset <= (FARM_PAGE_LEN * FARM_PAGE_RELIABILITY_STATS); offset += FARM_PAGE_LEN)
        {
            farmGenericPage* genpage = M_NULLPTR;
            switch (offset / FARM_PAGE_LEN)
            {
            case FARM_PAGE_HEADER: // header
                genpage = M_REINTERPRET_CAST(farmGenericPage*, &farmdata->header);
                break;
            case FARM_PAGE_DRIVE_INFO:
                genpage = M_REINTERPRET_CAST(farmGenericPage*, &farmdata->driveinfo);
                break;
            case FARM_PAGE_WORKLOAD:
                genpage = M_REINTERPRET_CAST(farmGenericPage*, &farmdata->workload);
                break;
            case FARM_PAGE_ERROR_STATS:
                genpage = M_REINTERPRET_CAST(farmGenericPage*, &farmdata->error);
                break;
            case FARM_PAGE_ENVIRONMENT_STATS:
                genpage = M_REINTERPRET_CAST(farmGenericPage*, &farmdata->environment);
                break;
            case FARM_PAGE_RELIABILITY_STATS:
                genpage = M_REINTERPRET_CAST(farmGenericPage*, &farmdata->reliability);
                break;
            }
            if (genpage == M_NULLPTR)
            {
                break;
            }
            generic_SATA_Read_FARM_Log(&ptrData[offset], FARM_PAGE_LEN, genpage);
        }
    }
    RESTORE_NONNULL_COMPARE
    return farmdata;
}

// TODO: Option to select which FARM data between current, saved, factory
eReturnValues read_FARM_Data(tDevice* device, farmLogData* farmdata)
{
    eReturnValues ret = NOT_SUPPORTED;
    if (device->drive_info.drive_type == ATA_DRIVE)
    {
        uint32_t datalen     = FARM_PAGE_LEN * (FARM_PAGE_RELIABILITY_STATS + 1); // 96KiB
        uint8_t* rawFarmData = M_REINTERPRET_CAST(
            uint8_t*, safe_calloc_aligned(datalen, sizeof(uint8_t), device->os_info.minimumAlignment));
        if (rawFarmData != M_NULLPTR)
        {
            ret = get_ATA_Log(device, SEAGATE_ATA_LOG_FIELD_ACCESSIBLE_RELIABILITY_METRICS, M_NULLPTR, M_NULLPTR, true,
                              false, true, rawFarmData, datalen, M_NULLPTR, 0, 0);
            if (ret == SUCCESS)
            {
                sata_Read_FARM_Log(rawFarmData, datalen, farmdata);
            }
            safe_free_aligned(&rawFarmData);
        }
    }
    else if (device->drive_info.drive_type == SCSI_DRIVE)
    {
        uint32_t datalen     = UINT16_MAX; // reading FARM on SAS can be done in one 64k transfer
        uint8_t* rawFarmData = M_REINTERPRET_CAST(
            uint8_t*, safe_calloc_aligned(datalen, sizeof(uint8_t), device->os_info.minimumAlignment));
        if (rawFarmData != M_NULLPTR)
        {
            ret = get_SCSI_Log(device, SEAGATE_LP_FARM, SEAGATE_FARM_SP_CURRENT, M_NULLPTR, M_NULLPTR, true,
                               rawFarmData, datalen, M_NULLPTR);
            if (ret == SUCCESS)
            {
                sas_Read_FARM_Log(rawFarmData, datalen, farmdata);
            }
            safe_free_aligned(&rawFarmData);
        }
    }
    return ret;
}

static M_INLINE void print_Statistic_Name(const char* statisticname)
{
    const char* stat = statisticname;
    if (statisticname == M_NULLPTR)
    {
        stat = "Unknown Statistic";
    }
    printf("%-50s", stat);
}

static M_INLINE bool print_Stat_If_Supported_And_Valid_Uint64(const char* statisticname, uint64_t statisticData)
{
    bool    printed = false;
    uint8_t status  = get_Farm_Status_Byte(statisticData);
    if ((status & FARM_FIELD_SUPPORTED_BIT) > 0)
    {
        print_Statistic_Name(statisticname);
        if ((status & FARM_FIELD_VALID_BIT) > 0)
        {
            printed = printf("\t\t%" PRIu64 "\n", get_Farm_Qword_Data(statisticData)) > 0 ? true : false;
        }
        else
        {
            printed = printf("\t\tInvalid\n") > 0 ? true : false;
        }
    }
    return printed;
}

static M_INLINE bool print_Stat_If_Supported_And_Valid_int64(const char* statisticname, uint64_t statisticData)
{
    bool    printed = false;
    uint8_t status  = get_Farm_Status_Byte(statisticData);
    if ((status & FARM_FIELD_SUPPORTED_BIT) > 0)
    {
        print_Statistic_Name(statisticname);
        if ((status & FARM_FIELD_VALID_BIT) > 0)
        {
            int64_t signedval = M_STATIC_CAST(int64_t, get_Farm_Qword_Data(statisticData));
            if (M_Byte6(M_STATIC_CAST(uint64_t, signedval)) & BIT7)
            {
                // sign bit is set. To make sure this converts as we expect it to we need to make sure the int64_t sign
                // bit of the host is set properly.
                signedval = M_STATIC_CAST(int64_t, M_STATIC_CAST(uint64_t, signedval) | UINT64_C(0xFFFF000000000000));
            }
            printed = printf("\t\t%" PRId64 "\n", signedval) > 0 ? true : false;
        }
        else
        {
            printed = printf("\t\tInvalid\n") > 0 ? true : false;
        }
    }
    return printed;
}

// If a statistic is a counter in 1/1000 or .1%, etc can provide a conversion factor with this
static M_INLINE bool print_Stat_If_Supported_And_Valid_Uint64_Factor(const char* statisticname,
                                                                     uint64_t    statisticData,
                                                                     double      conversionFactor)
{
    bool    printed = false;
    uint8_t status  = get_Farm_Status_Byte(statisticData);
    if ((status & FARM_FIELD_SUPPORTED_BIT) > 0)
    {
        print_Statistic_Name(statisticname);
        if ((status & FARM_FIELD_VALID_BIT) > 0)
        {
            int precision = 2;
            if (conversionFactor <= 0.001)
            {
                precision = 3;
            }
            if (conversionFactor <= 0.0001)
            {
                precision = 4;
            }
            if (conversionFactor <= 0.00001)
            {
                precision = 5;
            }
            printed = printf("\t\t%.*f\n", precision,
                             M_STATIC_CAST(double, get_Farm_Qword_Data(statisticData)) * conversionFactor) > 0
                          ? true
                          : false;
        }
        else
        {
            printed = printf("\t\tInvalid\n") > 0 ? true : false;
        }
    }
    return printed;
}

static M_INLINE bool print_Stat_If_Supported_And_Valid_int64_Factor(const char* statisticname,
                                                                    uint64_t    statisticData,
                                                                    double      conversionFactor)
{
    bool    printed = false;
    uint8_t status  = get_Farm_Status_Byte(statisticData);
    if ((status & FARM_FIELD_SUPPORTED_BIT) > 0)
    {
        print_Statistic_Name(statisticname);
        if ((status & FARM_FIELD_VALID_BIT) > 0)
        {
            int precision = 2;
            if (conversionFactor <= 0.001)
            {
                precision = 3;
            }
            if (conversionFactor <= 0.0001)
            {
                precision = 4;
            }
            if (conversionFactor <= 0.00001)
            {
                precision = 5;
            }
            int64_t signedval = M_STATIC_CAST(int64_t, get_Farm_Qword_Data(statisticData));
            if (M_Byte6(M_STATIC_CAST(uint64_t, signedval)) & BIT7)
            {
                // sign bit is set. To make sure this converts as we expect it to we need to make sure the int64_t sign
                // bit of the host is set properly.
                signedval = M_STATIC_CAST(int64_t, M_STATIC_CAST(uint64_t, signedval) | UINT64_C(0xFFFF000000000000));
            }
            printed = printf("\t\t%0.*f\n", precision, M_STATIC_CAST(double, signedval) * conversionFactor) > 0 ? true
                                                                                                                : false;
        }
        else
        {
            printed = printf("\t\tInvalid\n") > 0 ? true : false;
        }
    }
    return printed;
}

#define MICRO_SECONDS_PER_HOUR          3600000000.0
#define MICRO_SECONDS_PER_MINUTE        60000000.0
#define MICRO_SECONDS_PER_SECOND        1000000.0
#define MICRO_SECONDS_PER_MILLI_SECONDS 1000.0

static M_INLINE bool print_Stat_If_Supported_And_Valid_Time(const char* statisticname,
                                                            uint64_t    statisticData,
                                                            double      conversionToMicroseconds)
{
    bool    printed = false;
    uint8_t status  = get_Farm_Status_Byte(statisticData);
    if ((status & FARM_FIELD_SUPPORTED_BIT) > 0)
    {
        print_Statistic_Name(statisticname);
        if ((status & FARM_FIELD_VALID_BIT) > 0)
        {
            // Take the input time in an integer, multiply it by the conversion factor to get the microsecond time as
            // the base unit. From there we can forward convert to hours. The reason for this is because to keep some of
            // the output simpler and different statistics use different units, microseconds is lowest seen unit and
            // hours is the most common output unit -TJE
            double timeMicroseconds =
                M_STATIC_CAST(double, get_Farm_Qword_Data(statisticData)) * conversionToMicroseconds;

            printed = printf("\t\t%0.02f\n", timeMicroseconds / MICRO_SECONDS_PER_HOUR) > 0 ? true : false;
        }
        else
        {
            printed = printf("\t\tInvalid\n") > 0 ? true : false;
        }
    }
    return printed;
}

static M_INLINE bool print_Stat_If_Supported_And_Valid_HexUint64(const char* statisticname, uint64_t statisticData)
{
    bool    printed = false;
    uint8_t status  = get_Farm_Status_Byte(statisticData);
    if ((status & FARM_FIELD_SUPPORTED_BIT) > 0)
    {
        print_Statistic_Name(statisticname);
        if ((status & FARM_FIELD_VALID_BIT) > 0)
        {
            printed = printf("\t\t%" PRIX64 "\n", get_Farm_Qword_Data(statisticData)) > 0 ? true : false;
        }
        else
        {
            printed = printf("\t\tInvalid\n") > 0 ? true : false;
        }
    }
    return printed;
}

#define FARM_FLOAT_PERCENT_DELTA_FACTORY_BIT BIT0
#define FARM_FLOAT_NEGATIVE_BIT              BIT1

static M_INLINE uint8_t get_Farm_Float_Bits(uint64_t floatData)
{
    return M_Byte6(floatData);
}

// For Mrheadresistance type output
static bool print_Stat_If_Supported_And_Valid_Float(const char* statisticname, uint64_t statisticData)
{
    bool    printed = false;
    uint8_t status  = get_Farm_Status_Byte(statisticData);
    if ((status & FARM_FIELD_SUPPORTED_BIT) > 0)
    {
        print_Statistic_Name(statisticname);
        if ((status & FARM_FIELD_VALID_BIT) > 0)
        {
            uint8_t bits = get_Farm_Float_Bits(statisticData);
            if (bits & FARM_FLOAT_PERCENT_DELTA_FACTORY_BIT || bits & FARM_FLOAT_NEGATIVE_BIT)
            {
                double  calculated  = 0.0;
                double  decimalPart = M_STATIC_CAST(double, get_DWord0(statisticData));
                int16_t wholePart   = M_STATIC_CAST(int16_t, M_Word2(statisticData));

                calculated = M_STATIC_CAST(double, wholePart) + (decimalPart * 0.0001);
                if (bits & FARM_FLOAT_NEGATIVE_BIT)
                {
                    calculated *= -1.0;
                }
                printed = printf("\t\t%0.02f\n", calculated) > 0 ? true : false;
            }
            else
            {
                printed = printf("\t\t%" PRIu16 "\n", M_Word0(statisticData)) > 0 ? true : false;
            }
        }
        else
        {
            printed = printf("\t\tInvalid\n") > 0 ? true : false;
        }
    }
    return printed;
}

static void single_qword_print_Stat_If_Supported_And_Valid_ASCII(const char* statisticname, uint64_t firstqword)
{
    size_t asciilen      = SIZE_T_C(5);
    char*  farmASCIIData = M_REINTERPRET_CAST(char*, safe_calloc(asciilen, sizeof(char)));
    if (farmASCIIData != M_NULLPTR)
    {
        size_t   asciioffset = SIZE_T_C(0);
        uint32_t rawdata     = b_swap_32(M_DoubleWord0(firstqword));
        safe_memcpy(&farmASCIIData[asciioffset], asciilen, &rawdata, sizeof(uint32_t));
        farmASCIIData[asciilen] = 0;
        print_Statistic_Name(statisticname);
        printf("\t\t%s\n", farmASCIIData);
        safe_free(&farmASCIIData);
    }
}

// TODO: Might want standalone qwords to str function - TJE
static void print_Stat_If_Supported_And_Valid_ASCII(const char* statisticname, uint64_t* firstqword, uint8_t numQwords)
{
    if (firstqword != M_NULLPTR)
    {
        if (numQwords == 1)
        {
            single_qword_print_Stat_If_Supported_And_Valid_ASCII(statisticname, firstqword[0]);
        }
        else
        {
            size_t asciilen =
                (uint8_to_sizet(numQwords) * SIZE_T_C(4)) +
                SIZE_T_C(1); // ASCII fields currently hold 4 characters in a single qword. +1 for null terminator
            char* farmASCIIData = M_REINTERPRET_CAST(char*, safe_calloc(asciilen, sizeof(char)));
            if (farmASCIIData != M_NULLPTR)
            {
                size_t asciioffset = SIZE_T_C(0);
                for (uint8_t qwordIter = UINT8_C(0); qwordIter < numQwords; ++qwordIter, asciioffset += 4)
                {
                    uint32_t rawdata = w_swap_32(b_swap_32(M_DoubleWord0(firstqword[qwordIter])));
                    safe_memcpy(&farmASCIIData[asciioffset], asciilen, &rawdata, sizeof(uint32_t));
                }
                farmASCIIData[asciilen] = 0;
                print_Statistic_Name(statisticname);
                printf("\t\t%s\n", farmASCIIData);
                safe_free(&farmASCIIData);
            }
        }
    }
}

static void print_Stat_If_Supported_And_Valid_Date_Of_Assembly(uint64_t doaQword)
{
    size_t asciilen      = (SIZE_T_C(5));
    char*  farmASCIIData = M_REINTERPRET_CAST(char*, safe_calloc(asciilen, sizeof(char)));
    if (farmASCIIData != M_NULLPTR)
    {
        size_t   asciioffset = SIZE_T_C(0);
        uint32_t rawdata     = M_DoubleWord0(doaQword);
        safe_memcpy(&farmASCIIData[asciioffset], asciilen, &rawdata, sizeof(uint32_t));
        farmASCIIData[asciilen] = 0;
        print_Statistic_Name("Date Of Assembly\t\t");
        // first 2 digits are year
        DECLARE_ZERO_INIT_ARRAY(char, year, 5);
        year[0] = '2';
        year[1] = '0';
        safe_memcpy(&year[2], 3, farmASCIIData, 2);
        // second 2 digits are week of the year
        DECLARE_ZERO_INIT_ARRAY(char, week, 3);
        safe_memcpy(&week[0], 3, &farmASCIIData[2], 2);
        printf("Week %s, %s\n", week, year);
        safe_free(&farmASCIIData);
    }
}

// for WWN
static void print_Stat_If_Supported_And_Valid_2Qwords_To_UINT64_Hex(const char* statisticname, uint64_t* firstqword)
{
    if (firstqword != M_NULLPTR)
    {
        uint64_t finalData =
            M_DWordsTo8ByteValue(w_swap_32(M_DoubleWord0(firstqword[0])), w_swap_32(M_DoubleWord0(firstqword[1])));
        print_Statistic_Name(statisticname);
        printf("\t\t%016" PRIX64 "\n", finalData);
    }
}

static M_INLINE void print_Stat_If_Supported_And_Valid_Recording_Type(const char* statisticname, uint64_t statisticData)
{
    uint8_t status = get_Farm_Status_Byte(statisticData);
    if ((status & FARM_FIELD_SUPPORTED_BIT) > 0)
    {
        print_Statistic_Name(statisticname);
        if ((status & FARM_FIELD_VALID_BIT) > 0)
        {
            uint64_t recordingType = get_Farm_Qword_Data(statisticData);
            // TODO: There is likely a better way to deal with this - TJE
            printf("\t\t");
            if (recordingType & FARM_DRIVE_RECORDING_SMR)
            {
                printf("SMR");
            }
            if (recordingType & FARM_DRIVE_RECORDING_SMR && recordingType & FARM_DRIVE_RECORDING_CMR)
            {
                printf(", CMR");
            }
            else if (recordingType & FARM_DRIVE_RECORDING_CMR)
            {
                printf("CMR");
            }
            printf("\n");
        }
        else
        {
            printf("\t\tInvalid\n");
        }
    }
}

static M_INLINE void print_Stat_If_Supported_And_Valid_Bool(const char* statisticname,
                                                            uint64_t    statisticData,
                                                            const char* truestring,
                                                            const char* falsestring)
{
    uint8_t status = get_Farm_Status_Byte(statisticData);
    if ((status & FARM_FIELD_SUPPORTED_BIT) > 0)
    {
        print_Statistic_Name(statisticname);
        if ((status & FARM_FIELD_VALID_BIT) > 0)
        {
            const char* booltrue   = "true";
            const char* boolfalse  = "false";
            const char* printtrue  = booltrue;
            const char* printfalse = boolfalse;
            if (truestring != M_NULLPTR)
            {
                printtrue = truestring;
            }
            if (falsestring != M_NULLPTR)
            {
                printfalse = falsestring;
            }
            if (get_Farm_Qword_Data(statisticData) > 0)
            {
                printf("\t\t%s", printtrue);
            }
            else
            {
                printf("\t\t%s", printfalse);
            }
            printf("\n");
        }
        else
        {
            printf("\t\tInvalid\n");
        }
    }
}

typedef enum eFARMByHeadOutputFormat
{
    FARM_BY_HEAD_UINT64 = 0,
    FARM_BY_HEAD_UINT64_FACTOR,
    FARM_BY_HEAD_INT64,
    FARM_BY_HEAD_INT64_FACTOR,
    FARM_BY_HEAD_HEX,
    FARM_BY_HEAD_FLOAT,
    FARM_BY_HEAD_TIME
} eFARMByHeadOutputFormat;

#define BY_HEAD_INFO_STR_LEN 8 // max length of " Head xx"
static bool print_Stat_If_Supported_And_Valid_By_Head(const char*             statisticname,
                                                      uint64_t                byhead[FARM_MAX_HEADS],
                                                      uint64_t                numberOfHeads,
                                                      eFARMByHeadOutputFormat outputFormat,
                                                      double                  conversionfactor)
{
    if (byhead != M_NULLPTR)
    {
        uint64_t printCount = UINT64_C(0);
        for (uint64_t headiter = UINT64_C(0); headiter < FARM_MAX_HEADS && headiter < numberOfHeads; ++headiter)
        {
            size_t byheadstatstrlen = safe_strlen(statisticname);
            if (byheadstatstrlen == 0)
            {
                byheadstatstrlen = safe_strlen("Unknown Statistic");
            }
            byheadstatstrlen += BY_HEAD_INFO_STR_LEN + 1; // +1 for null terminator
            char* byheadstatname = safe_calloc(byheadstatstrlen, sizeof(char));
            if (byheadstatname != M_NULLPTR)
            {
                if (statisticname != M_NULLPTR)
                {
                    snprintf_err_handle(byheadstatname, byheadstatstrlen, "%s Head %2" PRIu64, statisticname, headiter);
                }
                else
                {
                    snprintf_err_handle(byheadstatname, byheadstatstrlen, "Unknown Statistic Head %2" PRIu64, headiter);
                }
            }
            bool printed = false;
            switch (outputFormat)
            {
            case FARM_BY_HEAD_UINT64:
                printed = print_Stat_If_Supported_And_Valid_Uint64(byheadstatname, byhead[headiter]);
                break;
            case FARM_BY_HEAD_UINT64_FACTOR:
                printed =
                    print_Stat_If_Supported_And_Valid_Uint64_Factor(byheadstatname, byhead[headiter], conversionfactor);
                break;
            case FARM_BY_HEAD_INT64:
                printed = print_Stat_If_Supported_And_Valid_int64(byheadstatname, byhead[headiter]);
                break;
            case FARM_BY_HEAD_INT64_FACTOR:
                printed =
                    print_Stat_If_Supported_And_Valid_int64_Factor(byheadstatname, byhead[headiter], conversionfactor);
                break;
            case FARM_BY_HEAD_HEX:
                printed = print_Stat_If_Supported_And_Valid_HexUint64(byheadstatname, byhead[headiter]);
                break;
            case FARM_BY_HEAD_FLOAT:
                printed = print_Stat_If_Supported_And_Valid_Float(byheadstatname, byhead[headiter]);
                break;
            case FARM_BY_HEAD_TIME:
                printed = print_Stat_If_Supported_And_Valid_Time(byheadstatname, byhead[headiter], conversionfactor);
                break;
            }
            if (printed)
            {
                ++printCount;
            }
        }
        if (printCount > UINT64_C(0))
        {
            return true;
        }
    }
    return false;
}

static void print_Farm_Drive_Info(farmDriveInfo* driveInfo, eFARMDriveInterface* farmInterface)
{
    if (driveInfo != M_NULLPTR)
    {
        if (get_Farm_Qword_Data(driveInfo->pageNumber) == FARM_PAGE_DRIVE_INFO)
        {
            printf("---Drive Info---\n");
            print_Stat_If_Supported_And_Valid_ASCII("Model Number", &driveInfo->modelNumber[0],
                                                    FARM_DRIVE_INFO_MN_ASCII_LEN);
            print_Stat_If_Supported_And_Valid_ASCII("Serial Number", &driveInfo->sn[0], FARM_DRIVE_INFO_SN_ASCII_LEN);
            print_Stat_If_Supported_And_Valid_ASCII("Firmware Revision", &driveInfo->fwrev[0],
                                                    FARM_DRIVE_INFO_FWREV_ASCII_LEN);
            print_Stat_If_Supported_And_Valid_2Qwords_To_UINT64_Hex("World Wide Name", &driveInfo->wwn[0]);
            print_Stat_If_Supported_And_Valid_Date_Of_Assembly(driveInfo->dateOfAssembly);
            print_Stat_If_Supported_And_Valid_ASCII("Drive Interface", &driveInfo->driveInterface, 1);
            // This is a very overly simple hack to detect interface.
            // It's a string set to "SAS" or "SATA" so this will work for now - TJE
            uint64_t drvint = get_Farm_Qword_Data(driveInfo->driveInterface);
            if (M_NULLPTR == memchr(&drvint, 'T', sizeof(drvint)))
            {
                *farmInterface = FARM_DRIVE_INTERFACE_SAS;
            }
            else
            {
                *farmInterface = FARM_DRIVE_INTERFACE_SATA;
            }
            print_Stat_If_Supported_And_Valid_Uint64("Device Capacity (LBAs)", driveInfo->driveCapacity);
            print_Stat_If_Supported_And_Valid_Uint64("Number of LBAs (HSMR SWR capacity)", driveInfo->numberOfLBAs);
            print_Stat_If_Supported_And_Valid_Uint64("Physical Sector Size (B)", driveInfo->physicalSectorSize);
            print_Stat_If_Supported_And_Valid_Uint64("Logical Sector Size (B)", driveInfo->logicalSectorSize);
            print_Stat_If_Supported_And_Valid_Uint64("Device Buffer Size (B)", driveInfo->deviceBufferSize);
            print_Stat_If_Supported_And_Valid_Uint64("Number Of Heads", driveInfo->numberOfHeads);
            print_Stat_If_Supported_And_Valid_Recording_Type("Drive Recording Type", driveInfo->driveRecordingType);
            print_Stat_If_Supported_And_Valid_HexUint64("Form Factor", driveInfo->deviceFormFactor);
            print_Stat_If_Supported_And_Valid_Uint64("Rotation Rate", driveInfo->rotationRate);
            print_Stat_If_Supported_And_Valid_HexUint64("ATA Security State", driveInfo->ataSecurityState);
            print_Stat_If_Supported_And_Valid_HexUint64("ATA Features Supported", driveInfo->ataFeaturesSupported);
            print_Stat_If_Supported_And_Valid_HexUint64("ATA Features Enabled", driveInfo->ataFeaturesEnabled);
            print_Stat_If_Supported_And_Valid_Time("Power On Hours", driveInfo->powerOnHours, MICRO_SECONDS_PER_HOUR);
            print_Stat_If_Supported_And_Valid_Time("Spindle Power On Hours", driveInfo->spindlePowerOnHours,
                                                   MICRO_SECONDS_PER_HOUR);
            print_Stat_If_Supported_And_Valid_Time("Head Flight Hours", driveInfo->headFlightHours,
                                                   MICRO_SECONDS_PER_HOUR);
            print_Stat_If_Supported_And_Valid_Time("Head Flight Hours, Actuator 1", driveInfo->headFlightHoursActuator1,
                                                   MICRO_SECONDS_PER_HOUR);
            print_Stat_If_Supported_And_Valid_Uint64("Head Load Events", driveInfo->headLoadEvents);
            print_Stat_If_Supported_And_Valid_Uint64("Head Load Events, Actuator 1",
                                                     driveInfo->headLoadEventsActuator1);
            print_Stat_If_Supported_And_Valid_Uint64("Power Cycle Count", driveInfo->powerCycleCount);
            print_Stat_If_Supported_And_Valid_Uint64("Hardware Reset Count", driveInfo->hardwareResetCount);
            print_Stat_If_Supported_And_Valid_Uint64("Spin up time (ms)", driveInfo->spinUpTimeMilliseconds);
            print_Stat_If_Supported_And_Valid_Uint64("Time to ready, last power cycle (ms)",
                                                     driveInfo->timeToReadyOfLastPowerCycle);
            print_Stat_If_Supported_And_Valid_Uint64("Time in staggered spinup, last power on sequence (ms)",
                                                     driveInfo->timeDriveHeldInStaggeredSpinDuringLastPowerOnSequence);
            if (*farmInterface == FARM_DRIVE_INTERFACE_SAS)
            {
                print_Stat_If_Supported_And_Valid_Uint64("NVC Status at Power On", driveInfo->nvcStatusOnPoweron);
                print_Stat_If_Supported_And_Valid_Uint64("Time Availabe to Save User Data To NV Mem",
                                                         driveInfo->timeAvailableToSaveUDToNVMem); // 100us
            }
            print_Stat_If_Supported_And_Valid_Time("Lowest POH timestamp (Hours)",
                                                   driveInfo->lowestPOHForTimeRestrictedParameters,
                                                   MICRO_SECONDS_PER_MILLI_SECONDS);
            print_Stat_If_Supported_And_Valid_Time("Highest POH timestamp (Hours)",
                                                   driveInfo->highestPOHForTimeRestrictedParameters,
                                                   MICRO_SECONDS_PER_MILLI_SECONDS);
            print_Stat_If_Supported_And_Valid_Bool("Depopulation Status", driveInfo->isDriveDepopulated, "Depopulated",
                                                   "Not Depopulated");
            print_Stat_If_Supported_And_Valid_HexUint64("Depopulation Head Mask", driveInfo->depopulationHeadMask);
            print_Stat_If_Supported_And_Valid_HexUint64("Regeneration Head Mask", driveInfo->regenHeadMask);
            print_Stat_If_Supported_And_Valid_By_Head("Physical Element Status",
                                                      driveInfo->getPhysicalElementStatusByHead,
                                                      driveInfo->numberOfHeads, FARM_BY_HEAD_HEX, 0.0);
            print_Stat_If_Supported_And_Valid_Uint64("Max # Available Disc Sectors for Reassignment",
                                                     driveInfo->maxAvailableSectorsForReassignment);
            print_Stat_If_Supported_And_Valid_Bool("HAMR Data Protect Status", driveInfo->hamrDataProtectStatus,
                                                   "Data Protect", "No Data Protect");
            print_Stat_If_Supported_And_Valid_Time("POH of Most Recent FARM Time Series Frame",
                                                   driveInfo->pohOfMostRecentTimeseriesFrame,
                                                   MICRO_SECONDS_PER_MILLI_SECONDS);
            print_Stat_If_Supported_And_Valid_Time("POH of 2nd Most Recent FARM Time Series Frame",
                                                   driveInfo->pohOfSecondMostRecentTimeseriesFrame,
                                                   MICRO_SECONDS_PER_MILLI_SECONDS);
            print_Stat_If_Supported_And_Valid_Uint64(
                "Seq or Before Req for Active Zone Config",
                driveInfo->sequentialOrBeforeWriteRequiredForActiveZoneConfiguration);
            print_Stat_If_Supported_And_Valid_Uint64(
                "Seq Write Req Active Zone Config",
                driveInfo->sequentialOrBeforeWriteRequiredForActiveZoneConfiguration);
        }
    }
}

static void print_FARM_Workload_Info(farmWorkload* work, uint64_t timerestrictedRangems)
{
    if (work != M_NULLPTR)
    {
        if (get_Farm_Qword_Data(work->pageNumber) == FARM_PAGE_WORKLOAD)
        {
            printf("---Workload Info---\n");
            print_Stat_If_Supported_And_Valid_Uint64("Rated Workload (%)", work->ratedWorkloadPercentage);
            print_Stat_If_Supported_And_Valid_Uint64("Total # of Read Commands", work->totalReadCommands);
            print_Stat_If_Supported_And_Valid_Uint64("Total # of Write Commands", work->totalWriteCommands);
            print_Stat_If_Supported_And_Valid_Uint64("Total # of Random Read Commands", work->totalRandomReadCommands);
            print_Stat_If_Supported_And_Valid_Uint64("Total # of Random Write Commands",
                                                     work->totalRandomWriteCommands);
            print_Stat_If_Supported_And_Valid_Uint64("Total # of Other Commands", work->totalOtherCommands);
            print_Stat_If_Supported_And_Valid_Uint64("LBAs Written", work->logicalSectorsWritten);
            print_Stat_If_Supported_And_Valid_Uint64("LBAs Read", work->logicalSectorsRead);
            print_Stat_If_Supported_And_Valid_Uint64("# of Dither events in power cycle",
                                                     work->numberOfDitherEventsInCurrentPowerCycle);
            print_Stat_If_Supported_And_Valid_Uint64("# of Dither events in power cycle, Actuator 1",
                                                     work->numberOfDitherEventsInCurrentPowerCycleActuator1);
            print_Stat_If_Supported_And_Valid_Uint64("# dither pause - random workloads in power cycle",
                                                     work->numberDitherHeldOffDueToRandomWorkloadsInCurrentPowerCycle);
            print_Stat_If_Supported_And_Valid_Uint64(
                "# dither pause - random workloads in power cycle, Actuator 1",
                work->numberDitherHeldOffDueToRandomWorkloadsInCurrentPowerCycleActuator1);
            print_Stat_If_Supported_And_Valid_Uint64(
                "# dither pause - sequential workloads in power cycle",
                work->numberDitherHeldOffDueToSequentialWorkloadsInCurrentPowerCycle);
            print_Stat_If_Supported_And_Valid_Uint64(
                "# dither pause - sequential workloads in power cycle, Actuator 1",
                work->numberDitherHeldOffDueToSequentialWorkloadsInCurrentPowerCycleActuator1);
            bool commandCover = false;
            commandCover =
                true == print_Stat_If_Supported_And_Valid_Uint64("# of read commands between 0-3.125% LBA space",
                                                                 work->numReadsInLBA0To3125PercentRange)
                    ? true
                    : commandCover;
            commandCover =
                true == print_Stat_If_Supported_And_Valid_Uint64("# of read commands between 3.125-25% LBA space",
                                                                 work->numReadsInLBA3125To25PercentRange)
                    ? true
                    : commandCover;
            commandCover =
                true == print_Stat_If_Supported_And_Valid_Uint64("# of read commands between 25-50% LBA space",
                                                                 work->numReadsInLBA25To50PercentRange)
                    ? true
                    : commandCover;
            commandCover =
                true == print_Stat_If_Supported_And_Valid_Uint64("# of read commands between 50-100% LBA space",
                                                                 work->numReadsInLBA50To100PercentRange)
                    ? true
                    : commandCover;
            commandCover =
                true == print_Stat_If_Supported_And_Valid_Uint64("# of write commands between 0-3.125% LBA space",
                                                                 work->numWritesInLBA0To3125PercentRange)
                    ? true
                    : commandCover;
            commandCover =
                true == print_Stat_If_Supported_And_Valid_Uint64("# of write commands between 3.125-25% LBA space",
                                                                 work->numWritesInLBA3125To25PercentRange)
                    ? true
                    : commandCover;
            commandCover =
                true == print_Stat_If_Supported_And_Valid_Uint64("# of write commands between 25-50% LBA space",
                                                                 work->numWritesInLBA25To50PercentRange)
                    ? true
                    : commandCover;
            commandCover =
                true == print_Stat_If_Supported_And_Valid_Uint64("# of write commands between 50-100% LBA space",
                                                                 work->numWritesInLBA50To100PercentRange)
                    ? true
                    : commandCover;
            if (commandCover)
            {
                print_Stat_If_Supported_And_Valid_Time("  Time that Commands Cover (Hours)",
                                                       timerestrictedRangems ^ (BIT63 | BIT62),
                                                       MICRO_SECONDS_PER_MILLI_SECONDS);
            }
            commandCover = false;
            commandCover = true == print_Stat_If_Supported_And_Valid_Uint64("# of read commands with xfer <= 16KiB",
                                                                            work->numReadsOfXferLenLT16KB)
                               ? true
                               : commandCover;
            commandCover =
                true == print_Stat_If_Supported_And_Valid_Uint64("# of read commands with xfer 16Kib - 512KiB",
                                                                 work->numReadsOfXferLen16KBTo512KB)
                    ? true
                    : commandCover;
            commandCover = true == print_Stat_If_Supported_And_Valid_Uint64(
                                       "# of read commands with xfer 512KiB - 2MiB", work->numReadsOfXferLen512KBTo2MB)
                               ? true
                               : commandCover;
            commandCover = true == print_Stat_If_Supported_And_Valid_Uint64("# of read commands with xfer > 2MiB",
                                                                            work->numReadsOfXferLenGT2MB)
                               ? true
                               : commandCover;
            commandCover = true == print_Stat_If_Supported_And_Valid_Uint64("# of write commands with xfer <= 16KiB",
                                                                            work->numWritesOfXferLenLT16KB)
                               ? true
                               : commandCover;
            commandCover =
                true == print_Stat_If_Supported_And_Valid_Uint64("# of write commands with xfer 16Kib - 512KiB",
                                                                 work->numWritesOfXferLen16KBTo512KB)
                    ? true
                    : commandCover;
            commandCover =
                true == print_Stat_If_Supported_And_Valid_Uint64("# of write commands with xfer 512KiB - 2MiB",
                                                                 work->numWritesOfXferLen512KBTo2MB)
                    ? true
                    : commandCover;
            commandCover = true == print_Stat_If_Supported_And_Valid_Uint64("# of write commands with xfer > 2MiB",
                                                                            work->numWritesOfXferLenGT2MB)
                               ? true
                               : commandCover;
            if (commandCover)
            {
                print_Stat_If_Supported_And_Valid_Time("  Time that Commands Cover (Hours)",
                                                       timerestrictedRangems ^ (BIT63 | BIT62),
                                                       MICRO_SECONDS_PER_MILLI_SECONDS);
            }
            commandCover = false;
            commandCover = true == print_Stat_If_Supported_And_Valid_Uint64("Queue Depth = 1 in 30s intervals",
                                                                            work->countQD1at30sInterval)
                               ? true
                               : commandCover;
            commandCover = true == print_Stat_If_Supported_And_Valid_Uint64("Queue Depth = 2 in 30s intervals",
                                                                            work->countQD2at30sInterval)
                               ? true
                               : commandCover;
            commandCover = true == print_Stat_If_Supported_And_Valid_Uint64("Queue Depth 3-4 in 30s intervals",
                                                                            work->countQD3To4at30sInterval)
                               ? true
                               : commandCover;
            commandCover = true == print_Stat_If_Supported_And_Valid_Uint64("Queue Depth 5-8 in 30s intervals",
                                                                            work->countQD5To8at30sInterval)
                               ? true
                               : commandCover;
            commandCover = true == print_Stat_If_Supported_And_Valid_Uint64("Queue Depth 9-16 in 30s intervals",
                                                                            work->countQD9To16at30sInterval)
                               ? true
                               : commandCover;
            commandCover = true == print_Stat_If_Supported_And_Valid_Uint64("Queue Depth 17-32 in 30s intervals",
                                                                            work->countQD17To32at30sInterval)
                               ? true
                               : commandCover;
            commandCover = true == print_Stat_If_Supported_And_Valid_Uint64("Queue Depth 33-64 in 30s intervals",
                                                                            work->countQD33To64at30sInterval)
                               ? true
                               : commandCover;
            commandCover = true == print_Stat_If_Supported_And_Valid_Uint64("Queue Depth > 64 in 30s intervals",
                                                                            work->countGTQD64at30sInterval)
                               ? true
                               : commandCover;
            if (commandCover)
            {
                print_Stat_If_Supported_And_Valid_Time("  Time that Queue Bins Cover (Hours)",
                                                       timerestrictedRangems ^ (BIT63 | BIT62),
                                                       MICRO_SECONDS_PER_MILLI_SECONDS);
            }

            commandCover = false;
            commandCover =
                true == print_Stat_If_Supported_And_Valid_Uint64("# of reads of xfer bin 4, last 3 SSF",
                                                                 work->numReadsXferLenBin4Last3SMARTSummaryFrames)
                    ? true
                    : commandCover;
            commandCover =
                true == print_Stat_If_Supported_And_Valid_Uint64("# of reads of xfer bin 5, last 3 SSF",
                                                                 work->numReadsXferLenBin5Last3SMARTSummaryFrames)
                    ? true
                    : commandCover;
            commandCover =
                true == print_Stat_If_Supported_And_Valid_Uint64("# of reads of xfer bin 6, last 3 SSF",
                                                                 work->numReadsXferLenBin6Last3SMARTSummaryFrames)
                    ? true
                    : commandCover;
            commandCover =
                true == print_Stat_If_Supported_And_Valid_Uint64("# of reads of xfer bin 7, last 3 SSF",
                                                                 work->numReadsXferLenBin7Last3SMARTSummaryFrames)
                    ? true
                    : commandCover;
            commandCover =
                true == print_Stat_If_Supported_And_Valid_Uint64("# of writes of xfer bin 4, last 3 SSF",
                                                                 work->numWritesXferLenBin4Last3SMARTSummaryFrames)
                    ? true
                    : commandCover;
            commandCover =
                true == print_Stat_If_Supported_And_Valid_Uint64("# of writes of xfer bin 5, last 3 SSF",
                                                                 work->numWritesXferLenBin5Last3SMARTSummaryFrames)
                    ? true
                    : commandCover;
            commandCover =
                true == print_Stat_If_Supported_And_Valid_Uint64("# of writes of xfer bin 6, last 3 SSF",
                                                                 work->numWritesXferLenBin6Last3SMARTSummaryFrames)
                    ? true
                    : commandCover;
            commandCover =
                true == print_Stat_If_Supported_And_Valid_Uint64("# of writes of xfer bin 7, last 3 SSF",
                                                                 work->numWritesXferLenBin7Last3SMARTSummaryFrames)
                    ? true
                    : commandCover;
            if (commandCover)
            {
                print_Stat_If_Supported_And_Valid_Time("  Time that XFer Bins Cover (Hours)",
                                                       timerestrictedRangems ^ (BIT63 | BIT62),
                                                       MICRO_SECONDS_PER_MILLI_SECONDS);
            }
        }
    }
}

#define FLED_INFO_STR_LEN        20
#define RWRETRY_INFO_STR_LEN     20
#define FLEDTIMESTAMP_STR_LEN    20
#define FLED_POWER_CYCLE_STR_LEN 20

static char* get_Farm_FLED_Info_String(char* str, size_t strlen, uint64_t fledqword)
{
    uint8_t  fledInfoStatus = get_Farm_Status_Byte(fledqword);
    uint64_t fled           = get_Farm_Qword_Data(fledqword);
    if (str != M_NULLPTR)
    {
        if ((fledInfoStatus & FARM_FIELD_SUPPORTED_BIT) > UINT8_C(0) &&
            (fledInfoStatus & FARM_FIELD_VALID_BIT) > UINT8_C(0))
        {
            snprintf_err_handle(str, strlen, "%" PRIu64, fled);
        }
        else
        {
            snprintf_err_handle(str, strlen, "-");
        }
    }
    return str;
}

static void print_FARM_Error_Info_Flash_LED_Data(eFARMActuator actuator,
                                                 uint64_t      totalFLEDs,
                                                 uint64_t      fledIndex,
                                                 uint64_t      fledInfo[FARM_FLED_EVENTS],
                                                 uint64_t      rwRetry[FARM_RW_RETRY_EVENTS],
                                                 uint64_t      fledtimestamp[FARM_FLED_EVENTS],
                                                 uint64_t      powerCycleFLED[FARM_FLED_EVENTS])
{
    const char* fullDriveStr    = "";
    const char* actuator0Str    = "Actuator 0 ";
    const char* actuator1Str    = "Actuator 1 ";
    const char* farmActuatorStr = M_NULLPTR;
    switch (actuator)
    {
    case FARM_ACTUATOR_0:
        farmActuatorStr = actuator0Str;
        break;
    case FARM_ACTUATOR_1:
        farmActuatorStr = actuator1Str;
        break;
    case FARM_ACTUATOR_FULL_DRIVE:
        farmActuatorStr = fullDriveStr;
        break;
    }
    uint8_t totalStatus = get_Farm_Status_Byte(totalFLEDs);
    if (totalStatus & FARM_FIELD_SUPPORTED_BIT && totalStatus & FARM_FIELD_VALID_BIT)
    {
        printf("%s\n", farmActuatorStr);
        print_Stat_If_Supported_And_Valid_Uint64("Total Flash LED Events", totalFLEDs);
    }
    int64_t index  = INT64_C(0);
    uint8_t status = get_Farm_Status_Byte(fledIndex);
    if (status & FARM_FIELD_SUPPORTED_BIT && status & FARM_FIELD_VALID_BIT)
    {
        index = M_STATIC_CAST(int64_t, get_Farm_Qword_Data(fledIndex));
    }
    else
    {
        return;
    }
    uint8_t eventCount = UINT8_C(0);
    printf("%*s                %-*s  %-*s  %-*s  %-*s\n", M_STATIC_CAST(int, safe_strlen(farmActuatorStr)), "",
           FLED_INFO_STR_LEN, "FLED", RWRETRY_INFO_STR_LEN, "RW Retry", FLEDTIMESTAMP_STR_LEN, "Timestamp",
           FLED_POWER_CYCLE_STR_LEN, "Power Cycle");
    while (eventCount < FARM_FLED_EVENTS && index < FARM_FLED_EVENTS && index > INT64_C(0))
    {
        DECLARE_ZERO_INIT_ARRAY(char, fledInfoStr, FLED_INFO_STR_LEN);
        DECLARE_ZERO_INIT_ARRAY(char, rwRetryStr, RWRETRY_INFO_STR_LEN);
        DECLARE_ZERO_INIT_ARRAY(char, timestampStr, FLEDTIMESTAMP_STR_LEN);
        DECLARE_ZERO_INIT_ARRAY(char, powerCycleStr, FLED_POWER_CYCLE_STR_LEN);

        printf("%sFlash LED Info: %-*s  %-*s  %-*s  %-*s\n", farmActuatorStr, FLED_INFO_STR_LEN,
               get_Farm_FLED_Info_String(fledInfoStr, FLED_INFO_STR_LEN, fledInfo[index]), FLED_INFO_STR_LEN,
               get_Farm_FLED_Info_String(rwRetryStr, RWRETRY_INFO_STR_LEN, rwRetry[index]), FLED_INFO_STR_LEN,
               get_Farm_FLED_Info_String(timestampStr, FLEDTIMESTAMP_STR_LEN, fledtimestamp[index]), FLED_INFO_STR_LEN,
               get_Farm_FLED_Info_String(powerCycleStr, FLED_POWER_CYCLE_STR_LEN, powerCycleFLED[index]));

        // decrement index. Reset to zero if we go past the last event since this is a wrapping array.
        --index;
        if (index < INT64_C(0))
        {
            index = FARM_FLED_EVENTS;
        }
        ++eventCount;
    }
}

static void print_FARM_Error_Info(farmErrorStatistics* error, uint64_t numheads, eFARMDriveInterface driveInterface)
{
    if (error != M_NULLPTR)
    {
        if (get_Farm_Qword_Data(error->pageNumber) == FARM_PAGE_ERROR_STATS)
        {
            printf("---Error Info---\n");
            print_Stat_If_Supported_And_Valid_Uint64("# of Unrecoverable Read Errors",
                                                     error->numberOfUnrecoverableReadErrors);
            print_Stat_If_Supported_And_Valid_Uint64("# of Unrecoverable Write Errors",
                                                     error->numberOfUnrecoverableWriteErrors);
            print_Stat_If_Supported_And_Valid_Uint64("# of Reallocated Sectors", error->numberOfReallocatedSectors);
            print_Stat_If_Supported_And_Valid_Uint64("# of Reallocated Sectors, Actuator 1",
                                                     error->numberOfReallocatedSectorsActuator1);
            print_Stat_If_Supported_And_Valid_Uint64("# of Read Recovery Attempts",
                                                     error->numberOfReadRecoveryAttempts);
            print_Stat_If_Supported_And_Valid_Uint64("# of Mechanical Start Retries",
                                                     error->numberOfMechanicalStartRetries);
            print_Stat_If_Supported_And_Valid_Uint64("# of Reallocation Candidate Sectors",
                                                     error->numberOfReallocationCandidateSectors);
            print_Stat_If_Supported_And_Valid_Uint64("# of Reallocated Candidate Sectors, Actuator 1",
                                                     error->numberOfReallocationCandidateSectorsActuator1);
            if (driveInterface == FARM_DRIVE_INTERFACE_SATA)
            {
                // SATA
                print_Stat_If_Supported_And_Valid_Uint64("# of ASR Events", error->sataErr.numberOfASREvents);
                print_Stat_If_Supported_And_Valid_Uint64("# of Interface CRC Errors",
                                                         error->sataErr.numberOfInterfaceCRCErrors);
                print_Stat_If_Supported_And_Valid_Uint64("Spin Retry Count", error->sataErr.spinRetryCount);
                print_Stat_If_Supported_And_Valid_Uint64("Normalized Spin Retry Count",
                                                         error->sataErr.spinRetryCountNormalized);
                print_Stat_If_Supported_And_Valid_Uint64("Worst Ever Spin Retry Count",
                                                         error->sataErr.spinRetryCountWorstEver);
                print_Stat_If_Supported_And_Valid_Uint64("# Of IOEDC Errors", error->sataErr.numberOfIOEDCErrors);
                print_Stat_If_Supported_And_Valid_Uint64("# Of Command Timeouts", error->sataErr.commandTimeoutTotal);
                print_Stat_If_Supported_And_Valid_Uint64("# Of Command Timeouts > 5 seconds",
                                                         error->sataErr.commandTimeoutOver5s);
                print_Stat_If_Supported_And_Valid_Uint64("# Of Command Timeouts > 7.5 seconds",
                                                         error->sataErr.commandTimeoutOver7pt5s);
            }
            else if (driveInterface == FARM_DRIVE_INTERFACE_SAS)
            {
                // SAS
                print_Stat_If_Supported_And_Valid_HexUint64("FRU of SMART Trip Most Recent Frame",
                                                            error->sasErr.fruCodeOfSMARTTripMostRecentFrame);
                print_Stat_If_Supported_And_Valid_Uint64("Port A Invalid Dword Count",
                                                         error->sasErr.portAinvDWordCount);
                print_Stat_If_Supported_And_Valid_Uint64("Port B Invalid Dword Count",
                                                         error->sasErr.portBinvDWordCount);
                print_Stat_If_Supported_And_Valid_Uint64("Port A Disparity Error Count",
                                                         error->sasErr.portADisparityErrCount);
                print_Stat_If_Supported_And_Valid_Uint64("Port B Disparity Error Count",
                                                         error->sasErr.portBDisparityErrCount);
                print_Stat_If_Supported_And_Valid_Uint64("Port A Loss of DWord Sync",
                                                         error->sasErr.portAlossOfDWordSync);
                print_Stat_If_Supported_And_Valid_Uint64("Port B Loss of DWord Sync",
                                                         error->sasErr.portBlossOfDWordSync);
                print_Stat_If_Supported_And_Valid_Uint64("Port A Phy Reset Problem",
                                                         error->sasErr.portAphyResetProblem);
                print_Stat_If_Supported_And_Valid_Uint64("Port B Phy Reset Problem",
                                                         error->sasErr.portBphyResetProblem);
            }
            print_FARM_Error_Info_Flash_LED_Data(FARM_ACTUATOR_0, error->totalFlashLEDEvents, error->lastFLEDIndex,
                                                 error->last8FLEDEvents, error->last8ReadWriteRetryEvents,
                                                 error->timestampOfLast8FLEDs, error->powerCycleOfLast8FLEDs);
            print_FARM_Error_Info_Flash_LED_Data(
                FARM_ACTUATOR_1, error->totalFlashLEDEventsActuator1, error->lastFLEDIndexActuator1,
                error->last8FLEDEventsActuator1, error->last8ReadWriteRetryEvents,
                error->timestampOfLast8FLEDsActuator1, error->powerCycleOfLast8FLEDsActuator1);
            print_Stat_If_Supported_And_Valid_Uint64("Lifetime # Unrecoverable Read Errors due to ERC",
                                                     error->cumulativeLifetimeUnrecoverableReadErrorsDueToERC);
            print_Stat_If_Supported_And_Valid_By_Head("Cumulative Lifetime Unrecoverable Read Repeating",
                                                      error->cumLTUnrecReadRepeatByHead, numheads, FARM_BY_HEAD_UINT64,
                                                      0.0);
            print_Stat_If_Supported_And_Valid_By_Head("Cumulative Lifetime Unrecoverable Read Unique",
                                                      error->cumLTUnrecReadUniqueByHead, numheads, FARM_BY_HEAD_UINT64,
                                                      0.0);
            print_Stat_If_Supported_And_Valid_HexUint64("SMART Trip Flags 1", error->sataPFAAttributes[0]);
            print_Stat_If_Supported_And_Valid_HexUint64("SMART Trip Flags 2", error->sataPFAAttributes[1]);
            print_Stat_If_Supported_And_Valid_Uint64("# Reallocated Sectors since last FARM Time Series Frame",
                                                     error->numberReallocatedSectorsSinceLastFARMTimeSeriesFrameSaved);
            print_Stat_If_Supported_And_Valid_Uint64(
                "# Reallocated Sectors between N & N-1 FARM Time Series Frame",
                error->numberReallocatedSectorsBetweenFarmTimeSeriesFrameNandNminus1);
            print_Stat_If_Supported_And_Valid_Uint64(
                "# Reallocation Candidate Sectors since last FARM Time Series Frame",
                error->numberReallocationCandidateSectorsSinceLastFARMTimeSeriesFrameSaved);
            print_Stat_If_Supported_And_Valid_Uint64(
                "# Reallocation Candidate between N & N-1 FARM Time Series Frame",
                error->numberReallocationCandidateSectorsBetweenFarmTimeSeriesFrameNandNminus1);
            print_Stat_If_Supported_And_Valid_Uint64(
                "# Reallocated Sectors since last FARM Time Series Frame, Actuator 1",
                error->numberReallocatedSectorsSinceLastFARMTimeSeriesFrameSavedActuator1);
            print_Stat_If_Supported_And_Valid_Uint64(
                "# Reallocated Sectors between N & N-1 FARM Time Series Frame, Actuator 1",
                error->numberReallocatedSectorsBetweenFarmTimeSeriesFrameNandNminus1Actuator1);
            print_Stat_If_Supported_And_Valid_Uint64(
                "# Reallocation Candidate Sectors since last FARM Time Series Frame Actuator 1",
                error->numberReallocationCandidateSectorsSinceLastFARMTimeSeriesFrameSavedActuator1);
            print_Stat_If_Supported_And_Valid_Uint64(
                "# Reallocation Candidate between N & N-1 FARM Time Series Frame Actuator 1",
                error->numberReallocationCandidateSectorsBetweenFarmTimeSeriesFrameNandNminus1Actuator1);
            print_Stat_If_Supported_And_Valid_By_Head(
                "# Unique Unrecoverable sectors since last FARM Time Series Frame",
                error->numberUniqueUnrecoverableSectorsSinceLastFARMTimeSeriesFrameSavedByHead, numheads,
                FARM_BY_HEAD_UINT64, 0.0);
            print_Stat_If_Supported_And_Valid_By_Head(
                "# Unique Unrecoverable sectors between N & N-1 FARM Time Series Frame",
                error->numberUniqueUnrecoverableSectorsBetweenFarmTimeSeriesFrameNandNminus1ByHead, numheads,
                FARM_BY_HEAD_UINT64, 0.0);
        }
    }
}

static void print_FARM_Environment_Info(farmEnvironmentStatistics* env,
                                        uint64_t                   timerestrictedRangems,
                                        eFARMDriveInterface        farminterface)
{
    if (env != M_NULLPTR)
    {
        if (get_Farm_Qword_Data(env->pageNumber) == FARM_PAGE_ENVIRONMENT_STATS)
        {
            printf("---Environment Info---\n");
            print_Stat_If_Supported_And_Valid_int64_Factor("Current Temperature (C)", env->currentTemperature,
                                                           farminterface == FARM_DRIVE_INTERFACE_SAS ? 0.1 : 1.0);
            print_Stat_If_Supported_And_Valid_int64_Factor("Highest Temperature (C)", env->highestTemperature,
                                                           farminterface == FARM_DRIVE_INTERFACE_SAS ? 0.1 : 1.0);
            print_Stat_If_Supported_And_Valid_int64_Factor("Lowest Temperature (C)", env->lowestTemperature,
                                                           farminterface == FARM_DRIVE_INTERFACE_SAS ? 0.1 : 1.0);
            print_Stat_If_Supported_And_Valid_Uint64("Average Short Term Temperature (C)", env->avgShortTermTemp);
            print_Stat_If_Supported_And_Valid_Uint64("Average Long Term Temperature (C)", env->avgLongTermTemp);
            print_Stat_If_Supported_And_Valid_Uint64("Highest Average Short Term Temperature (C)",
                                                     env->highestAvgShortTermTemp);
            print_Stat_If_Supported_And_Valid_Uint64("Lowest Average Short Term Temperature (C)",
                                                     env->lowestAvgShortTermTemp);
            print_Stat_If_Supported_And_Valid_Uint64("Highest Average Long Term Temperature (C)",
                                                     env->highestAvgLongTermTemp);
            print_Stat_If_Supported_And_Valid_Uint64("Lowest Average Long Term Temperature (C)",
                                                     env->lowestAvgLongTermTemp);
            print_Stat_If_Supported_And_Valid_Time("Time in Over Temperature (Hours)", env->timeOverTemp,
                                                   MICRO_SECONDS_PER_MINUTE);
            print_Stat_If_Supported_And_Valid_Time("Time in Under Temperature (Hours)", env->timeUnderTemp,
                                                   MICRO_SECONDS_PER_MINUTE);
            print_Stat_If_Supported_And_Valid_Uint64("Specified Max Temperature (C)", env->specifiedMaxTemp);
            print_Stat_If_Supported_And_Valid_Uint64("Specified Min Temperature (C)", env->specifiedMinTemp);
            print_Stat_If_Supported_And_Valid_Uint64_Factor("Current Relative Humidity (%)",
                                                            env->currentRelativeHumidity, 0.1);
            if (print_Stat_If_Supported_And_Valid_Uint64_Factor(
                    "Current Motor Power (W)", env->currentMotorPowerFromMostRecentSMARTSummaryFrame, 0.001))
            {
                print_Stat_If_Supported_And_Valid_Time("  Time Coverage for Motor Power (Hours)",
                                                       timerestrictedRangems ^ (BIT63 | BIT62),
                                                       MICRO_SECONDS_PER_MILLI_SECONDS);
            }
            bool powerCov = false;
            powerCov      = true == print_Stat_If_Supported_And_Valid_Uint64_Factor("Current 12v input (V)",
                                                                                    env->current12Vinput, 0.001)
                                ? true
                                : powerCov;
            powerCov =
                true == print_Stat_If_Supported_And_Valid_Uint64_Factor("Min 12v input (V)", env->min12Vinput, 0.001)
                    ? true
                    : powerCov;
            powerCov =
                true == print_Stat_If_Supported_And_Valid_Uint64_Factor("Max 12v input (V)", env->max12Vinput, 0.001)
                    ? true
                    : powerCov;
            powerCov = true == print_Stat_If_Supported_And_Valid_Uint64_Factor("Current 5v input (V)",
                                                                               env->current5Vinput, 0.001)
                           ? true
                           : powerCov;
            powerCov =
                true == print_Stat_If_Supported_And_Valid_Uint64_Factor("Min 5v input (V)", env->min5Vinput, 0.001)
                    ? true
                    : powerCov;
            powerCov =
                true == print_Stat_If_Supported_And_Valid_Uint64_Factor("Max 5v input (V)", env->max5Vinput, 0.001)
                    ? true
                    : powerCov;
            if (powerCov)
            {
                print_Stat_If_Supported_And_Valid_Time("  Time Coverage for 12v & 5v voltage (Hours)",
                                                       timerestrictedRangems ^ (BIT63 | BIT62),
                                                       MICRO_SECONDS_PER_MILLI_SECONDS);
            }
            powerCov = false;
            powerCov = true == print_Stat_If_Supported_And_Valid_Uint64_Factor("Average 12v power (W)",
                                                                               env->average12Vpwr, 0.001)
                           ? true
                           : powerCov;
            powerCov =
                true == print_Stat_If_Supported_And_Valid_Uint64_Factor("Min 12v power (W)", env->min12VPwr, 0.001)
                    ? true
                    : powerCov;
            powerCov =
                true == print_Stat_If_Supported_And_Valid_Uint64_Factor("Max 12v power (W)", env->max12VPwr, 0.001)
                    ? true
                    : powerCov;
            powerCov = true == print_Stat_If_Supported_And_Valid_Uint64_Factor("Average 5v power (W)",
                                                                               env->average5Vpwr, 0.001)
                           ? true
                           : powerCov;
            powerCov = true == print_Stat_If_Supported_And_Valid_Uint64_Factor("Min 5v power (W)", env->min5Vpwr, 0.001)
                           ? true
                           : powerCov;
            powerCov = true == print_Stat_If_Supported_And_Valid_Uint64_Factor("Max 5v power (W)", env->max5Vpwr, 0.001)
                           ? true
                           : powerCov;
            if (powerCov)
            {
                print_Stat_If_Supported_And_Valid_Time("  Time Coverage for 12v & 5v power (Hours)",
                                                       timerestrictedRangems ^ (BIT63 | BIT62),
                                                       MICRO_SECONDS_PER_MILLI_SECONDS);
            }
        }
    }
}

// unitStr should be the units this field is in if it is NOT % delta
static bool print_Stat_If_Supported_And_Valid_Unit_Or_Percent_Delta_By_Head(const char* fieldStr,
                                                                            const char* unitStr,
                                                                            uint64_t    byhead[FARM_MAX_HEADS],
                                                                            uint64_t    numberOfHeads)
{
    bool   printed = false;
    size_t len     = safe_strlen(fieldStr) + M_Max(safe_strlen(unitStr), safe_strlen("% delta")) +
                 SIZE_T_C(4); //+4 for parens, spaces, null terminator
    char* fieldNameAndUnit = M_STATIC_CAST(char*, safe_calloc(len, sizeof(char)));
    if (fieldNameAndUnit != M_NULLPTR)
    {
        if (get_Farm_Float_Bits(byhead[0]) & FARM_FLOAT_PERCENT_DELTA_FACTORY_BIT ||
            get_Farm_Float_Bits(byhead[0]) & FARM_FLOAT_NEGATIVE_BIT)
        {
            snprintf_err_handle(fieldNameAndUnit, len, "%s (%% delta)", fieldStr);
            printed = print_Stat_If_Supported_And_Valid_By_Head(fieldNameAndUnit, byhead, numberOfHeads,
                                                                FARM_BY_HEAD_FLOAT, 0.0);
        }
        else
        {
            snprintf_err_handle(fieldNameAndUnit, len, "%s (%s)", fieldStr, unitStr);
            printed = print_Stat_If_Supported_And_Valid_By_Head(fieldNameAndUnit, byhead, numberOfHeads,
                                                                FARM_BY_HEAD_UINT64, 0.001);
        }
    }
    return printed;
}

#define THREE_STATS_IN_ONE 3
static M_INLINE bool print_3_Stat_If_Supported_And_Valid_int64_Factor(const char* statisticname,
                                                                      uint64_t    statisticData[THREE_STATS_IN_ONE],
                                                                      double      conversionFactor)
{
    bool    printed = false;
    uint8_t status  = get_Farm_Status_Byte(statisticData[0]);
    if ((status & FARM_FIELD_SUPPORTED_BIT) > 0)
    {
        print_Statistic_Name(statisticname);
        if ((status & FARM_FIELD_VALID_BIT) > 0)
        {
            int printCnt  = 0;
            int precision = 2;
            if (conversionFactor <= 0.001)
            {
                precision = 3;
            }
            if (conversionFactor <= 0.0001)
            {
                precision = 4;
            }
            if (conversionFactor <= 0.00001)
            {
                precision = 5;
            }
            printf("\t");
            for (int statNum = 0; statNum < THREE_STATS_IN_ONE; ++statNum)
            {
                int64_t signedval = M_STATIC_CAST(int64_t, get_Farm_Qword_Data(statisticData[statNum]));
                if (M_Byte6(M_STATIC_CAST(uint64_t, signedval)) & BIT7)
                {
                    // sign bit is set. To make sure this converts as we expect it to we need to make sure the int64_t
                    // sign bit of the host is set properly.
                    signedval =
                        M_STATIC_CAST(int64_t, M_STATIC_CAST(uint64_t, signedval) | UINT64_C(0xFFFF000000000000));
                }
                printCnt = printf("\t%0.*f", precision, M_STATIC_CAST(double, signedval) * conversionFactor);
            }
            printf("\n");
            if (printCnt > 0)
            {
                printed = true;
            }
        }
        else
        {
            printed = printf("\t\tInvalid\tInvalid\tInvalid\n") > 0 ? true : false;
        }
    }
    return printed;
}

// If a statistic is a counter in 1/1000 or .1%, etc can provide a conversion factor with this
static M_INLINE bool print_3_Stat_If_Supported_And_Valid_Uint64_Factor(const char* statisticname,
                                                                       uint64_t    statisticData[THREE_STATS_IN_ONE],
                                                                       double      conversionFactor)
{
    bool    printed = false;
    uint8_t status  = get_Farm_Status_Byte(statisticData[0]);
    if ((status & FARM_FIELD_SUPPORTED_BIT) > 0)
    {
        print_Statistic_Name(statisticname);
        if ((status & FARM_FIELD_VALID_BIT) > 0)
        {
            int precision = 2;
            if (conversionFactor <= 0.001)
            {
                precision = 3;
            }
            if (conversionFactor <= 0.0001)
            {
                precision = 4;
            }
            int printCnt = 0;
            printf("\t");
            for (int statNum = 0; statNum < THREE_STATS_IN_ONE; ++statNum)
            {
                printCnt =
                    printf("\t%.*f", precision,
                           M_STATIC_CAST(double, get_Farm_Qword_Data(statisticData[statNum])) * conversionFactor);
            }
            printf("\n");
            if (printCnt > 0)
            {
                printed = true;
            }
        }
        else
        {
            printed = printf("\t\tInvalid\n") > 0 ? true : false;
        }
    }
    return printed;
}

#define BY_HEAD_INFO_STR_LEN 8 // max length of " Head xx"
static bool print_3_Stat_If_Supported_And_Valid_By_Head(const char* statisticname,
                                                        uint64_t    byhead[FARM_MAX_HEADS][THREE_STATS_IN_ONE],
                                                        uint64_t    numberOfHeads,
                                                        eFARMByHeadOutputFormat outputFormat,
                                                        double                  conversionfactor)
{
    if (byhead != M_NULLPTR)
    {
        uint64_t printCount = UINT64_C(0);
        for (uint64_t headiter = UINT64_C(0); headiter < FARM_MAX_HEADS && headiter < numberOfHeads; ++headiter)
        {
            size_t byheadstatstrlen = safe_strlen(statisticname);
            if (byheadstatstrlen == 0)
            {
                byheadstatstrlen = safe_strlen("Unknown Statistic");
            }
            byheadstatstrlen += BY_HEAD_INFO_STR_LEN + 1; // +1 for null terminator
            char* byheadstatname = safe_calloc(byheadstatstrlen, sizeof(char));
            if (byheadstatname != M_NULLPTR)
            {
                if (statisticname != M_NULLPTR)
                {
                    snprintf_err_handle(byheadstatname, byheadstatstrlen, "%s Head %2" PRIu64, statisticname, headiter);
                }
                else
                {
                    snprintf_err_handle(byheadstatname, byheadstatstrlen, "Unknown Statistic Head %2" PRIu64, headiter);
                }
            }
            bool printed = false;
            switch (outputFormat)
            {
            case FARM_BY_HEAD_UINT64:
                printed = print_3_Stat_If_Supported_And_Valid_Uint64_Factor(byheadstatname, byhead[headiter], 1.0);
                break;
            case FARM_BY_HEAD_UINT64_FACTOR:
                printed = print_3_Stat_If_Supported_And_Valid_Uint64_Factor(byheadstatname, byhead[headiter],
                                                                            conversionfactor);
                break;
            case FARM_BY_HEAD_INT64:
                printed = print_3_Stat_If_Supported_And_Valid_int64_Factor(byheadstatname, byhead[headiter], 1.0);
                break;
            case FARM_BY_HEAD_INT64_FACTOR:
                printed = print_3_Stat_If_Supported_And_Valid_int64_Factor(byheadstatname, byhead[headiter],
                                                                           conversionfactor);
                break;
            case FARM_BY_HEAD_HEX:
                // TODO: Add this in when we have a field that needs this.
                break;
            case FARM_BY_HEAD_FLOAT:
                // TODO: Add this in when we have a field that needs this.
                break;
            case FARM_BY_HEAD_TIME:
                // TODO: Add this in when we have a field that needs this.
                break;
            }
            if (printed)
            {
                ++printCount;
            }
        }
        if (printCount > UINT64_C(0))
        {
            return true;
        }
    }
    return false;
}

static M_INLINE bool print_Stat_If_Supported_And_Valid_Fly_Height_Clearance_By_Head(
    const char* fieldStr,
    uint64_t    byhead[FARM_MAX_HEADS][THREE_STATS_IN_ONE],
    uint64_t    numberOfHeads)
{
    return print_3_Stat_If_Supported_And_Valid_By_Head(fieldStr, byhead, numberOfHeads, FARM_BY_HEAD_INT64_FACTOR,
                                                       0.001);
}

static void print_FARM_Reliability_Info(farmReliabilityStatistics* reli,
                                        uint64_t                   numheads,
                                        eFARMDriveInterface        farminterface,
                                        uint64_t                   timerestrictedRangems)
{
    if (reli != M_NULLPTR)
    {
        if (get_Farm_Qword_Data(reli->pageNumber) == FARM_PAGE_RELIABILITY_STATS)
        {
            printf("---Reliability Info---\n");
            print_Stat_If_Supported_And_Valid_Uint64("# DOS Scans Performed", reli->numDOSScansPerformed);
            print_Stat_If_Supported_And_Valid_Uint64("# LBAs corrected by ISP", reli->numLBAsCorrectedByISP);
            print_Stat_If_Supported_And_Valid_Uint64("# DOS Scans Performed Actuator 1",
                                                     reli->numDOSScansPerformedActuator1);
            print_Stat_If_Supported_And_Valid_Uint64("# LBAs corrected by ISP Actuator 1",
                                                     reli->numLBAsCorrectedByISPActuator1);
            print_Stat_If_Supported_And_Valid_By_Head("DVGA Skip Write Detect", reli->dvgaSkipWriteDetectByHead,
                                                      numheads, FARM_BY_HEAD_UINT64, 0.0);
            print_Stat_If_Supported_And_Valid_By_Head("RVGA Skip Write Detect", reli->rvgaSkipWriteDetectByHead,
                                                      numheads, FARM_BY_HEAD_UINT64, 0.0);
            print_Stat_If_Supported_And_Valid_By_Head("FVGA Skip Write Detect", reli->fvgaSkipWriteDetectByHead,
                                                      numheads, FARM_BY_HEAD_UINT64, 0.0);
            print_Stat_If_Supported_And_Valid_By_Head("Skip Write Detect Threshold Exceeded",
                                                      reli->skipWriteDetectExceedsThresholdByHead, numheads,
                                                      FARM_BY_HEAD_UINT64, 0.0);
            if (farminterface == FARM_DRIVE_INTERFACE_SATA)
            {
                print_Stat_If_Supported_And_Valid_Uint64("Read Error Rate", reli->readErrorRate);
            }
            else
            {
                print_Stat_If_Supported_And_Valid_Uint64("# Read After Write (RAW) Operations", reli->numRAWOperations);
            }
            print_Stat_If_Supported_And_Valid_Uint64("Read Error Rate Normalized", reli->readErrorRateNormalized);
            print_Stat_If_Supported_And_Valid_Uint64("Read Error Rate Worst Ever", reli->readErrorRateWorstEver);
            print_Stat_If_Supported_And_Valid_Uint64("Seek Error Rate", reli->seekErrorRate);
            print_Stat_If_Supported_And_Valid_Uint64("Seek Error Rate Normalized", reli->seekErrorRateNormalized);
            print_Stat_If_Supported_And_Valid_Uint64("Seek Error Rate Worst Ever", reli->seekErrorRateWorstEver);
            print_Stat_If_Supported_And_Valid_Uint64("High Priority Unload Events", reli->highPriorityUnloadEvents);
            print_Stat_If_Supported_And_Valid_Unit_Or_Percent_Delta_By_Head("MR Head Resistance", "ohms",
                                                                            reli->mrHeadResistanceByHead, numheads);
            print_Stat_If_Supported_And_Valid_Unit_Or_Percent_Delta_By_Head(
                "2nd MR Head Resistance", "ohms", reli->secondHeadMRHeadResistanceByHead, numheads);
            bool velObs = false;

            velObs = true == print_Stat_If_Supported_And_Valid_By_Head("# of Velocity Observer",
                                                                       reli->velocityObserverByHead, numheads,
                                                                       FARM_BY_HEAD_UINT64, 0.0)
                         ? true
                         : velObs;
            velObs = true == print_Stat_If_Supported_And_Valid_By_Head("# of Velocity Observer No TMD",
                                                                       reli->numberOfVelocityObserverNoTMDByHead,
                                                                       numheads, FARM_BY_HEAD_UINT64, 0.0)
                         ? true
                         : velObs;
            if (velObs)
            {
                print_Stat_If_Supported_And_Valid_Time("  Time Coverage for Velocity Observer (Hours)",
                                                       timerestrictedRangems ^ (BIT63 | BIT62),
                                                       MICRO_SECONDS_PER_MILLI_SECONDS);
            }
            print_3_Stat_If_Supported_And_Valid_By_Head("H2SAT Trimmed Mean Bits in Error (Z1, Z2, Z3)",
                                                        reli->currentH2SATtrimmedMeanBitsInErrorByHeadZone, numheads,
                                                        FARM_BY_HEAD_UINT64_FACTOR, 0.10);
            print_3_Stat_If_Supported_And_Valid_By_Head("H2SAT Iterations to Converge (Z1, Z2, Z3)",
                                                        reli->currentH2SATiterationsToConvergeByHeadZone, numheads,
                                                        FARM_BY_HEAD_UINT64_FACTOR, 0.10);
            print_Stat_If_Supported_And_Valid_By_Head("Average H2SAT % Codeword at Iteration Level",
                                                      reli->currentH2SATpercentCodewordsPerIterByHeadTZAvg, numheads,
                                                      FARM_BY_HEAD_UINT64, 0.0);
            print_Stat_If_Supported_And_Valid_By_Head("Average H2SAT Amplitude", reli->currentH2SATamplitudeByHeadTZAvg,
                                                      numheads, FARM_BY_HEAD_UINT64, 0.0);
            print_Stat_If_Supported_And_Valid_By_Head("Average H2SAT Asymmetry", reli->currentH2SATasymmetryByHeadTZAvg,
                                                      numheads, FARM_BY_HEAD_INT64_FACTOR, 0.10);
            print_Stat_If_Supported_And_Valid_Fly_Height_Clearance_By_Head(
                "Applied Fly Height Clearance Delta (Angstroms) (OD, ID, MD)",
                reli->appliedFlyHeightClearanceDeltaByHead, numheads);
            print_Stat_If_Supported_And_Valid_Uint64("# Disc Slip Recalibrations Performed",
                                                     reli->numDiscSlipRecalibrationsPerformed);
            print_Stat_If_Supported_And_Valid_By_Head("# Reallocated Sectors", reli->numReallocatedSectorsByHead,
                                                      numheads, FARM_BY_HEAD_UINT64, 0.0);
            print_Stat_If_Supported_And_Valid_By_Head("# Reallocated Candidate Sectors",
                                                      reli->numReallocationCandidateSectorsByHead, numheads,
                                                      FARM_BY_HEAD_UINT64, 0.0);
            print_Stat_If_Supported_And_Valid_Bool("Helium Pressure Threshold", reli->heliumPressureThresholdTrip,
                                                   "Tripped", "Not Tripped");
            print_Stat_If_Supported_And_Valid_By_Head("# DOS Ought To Scan", reli->dosOughtScanCountByHead, numheads,
                                                      FARM_BY_HEAD_UINT64, 0.0);
            print_Stat_If_Supported_And_Valid_By_Head("# DOS Need To Scan", reli->dosNeedToScanCountByHead, numheads,
                                                      FARM_BY_HEAD_UINT64, 0.0);
            print_Stat_If_Supported_And_Valid_By_Head("# DOS Write Fault Scans", reli->dosWriteFaultScansByHead,
                                                      numheads, FARM_BY_HEAD_UINT64, 0.0);
            print_Stat_If_Supported_And_Valid_By_Head("Write Workload Power-on Time (Hours)",
                                                      reli->writeWorkloadPowerOnTimeByHead, numheads, FARM_BY_HEAD_TIME,
                                                      MICRO_SECONDS_PER_SECOND);
            print_Stat_If_Supported_And_Valid_Uint64("# LBAs Corrected By Parity Sector",
                                                     reli->numLBAsCorrectedByParitySector);
            print_Stat_If_Supported_And_Valid_Uint64("# LBAs Corrected By Parity Sector Actuator 1",
                                                     reli->numLBAsCorrectedByParitySectorActuator1);
            print_Stat_If_Supported_And_Valid_Uint64("Primary Super Parity Coverage %",
                                                     reli->superParityCoveragePercent);
            print_Stat_If_Supported_And_Valid_Uint64("Primary Super Parity Coverage SMR/HSMR-SWR %",
                                                     reli->primarySuperParityCoveragePercentageSMR_HSMR_SWR);
            print_Stat_If_Supported_And_Valid_Uint64("Primary Super Parity Coverage SMR/HSMR-SWR % Actuator 1",
                                                     reli->primarySuperParityCoveragePercentageSMR_HSMR_SWRActuator1);
        }
    }
}

void print_FARM_Data(farmLogData* farmdata)
{
    DISABLE_NONNULL_COMPARE
    if (farmdata != M_NULLPTR)
    {
        // TODO: Validate signature
        //  TODO: Determine number of actuators so we can output "Actuator #" when necessary
        //  TODO: Save drive interface from drive info section to populate other fields correctly
        //  TODO: Pass farm major/minor versions along. Ex: SAS before major version 3 uses different ASCII string
        //  formatting
        uint64_t maxHeads = get_Farm_Qword_Data(farmdata->header.maxDriveHeadsSupported);
        uint64_t numheads = get_Farm_Qword_Data(farmdata->driveinfo.numberOfHeads);
        uint64_t headcnt  = M_Min(M_Min(numheads, maxHeads), FARM_MAX_HEADS);
        uint64_t timeRestrictedRangeMS =
            get_Farm_Qword_Data(farmdata->driveinfo.highestPOHForTimeRestrictedParameters) -
            get_Farm_Qword_Data(farmdata->driveinfo.lowestPOHForTimeRestrictedParameters);
        if (timeRestrictedRangeMS == 0 ||
            !(get_Farm_Status_Byte(farmdata->driveinfo.highestPOHForTimeRestrictedParameters) &
              (FARM_FIELD_SUPPORTED_BIT | FARM_FIELD_VALID_BIT)) ||
            !(get_Farm_Status_Byte(farmdata->driveinfo.lowestPOHForTimeRestrictedParameters) &
              (FARM_FIELD_SUPPORTED_BIT | FARM_FIELD_VALID_BIT)))
        {
            timeRestrictedRangeMS |= BIT63 | BIT62;
        }
        eFARMDriveInterface farminterface = FARM_DRIVE_INTERFACE_SATA;
        if (headcnt == 0)
        {
            headcnt = FARM_MAX_HEADS;
        }
        printf("=== Field Accessible Reliability Metrics ===\n");
        printf("FARM Version: %" PRIu64 ".%" PRIu64 "\n", get_Farm_Qword_Data(farmdata->header.majorVersion),
               get_Farm_Qword_Data(farmdata->header.minorVersion));
        print_Farm_Drive_Info(&farmdata->driveinfo, &farminterface);
        print_FARM_Workload_Info(&farmdata->workload, timeRestrictedRangeMS);
        print_FARM_Error_Info(&farmdata->error, headcnt, farminterface);
        print_FARM_Environment_Info(&farmdata->environment, timeRestrictedRangeMS, farminterface);
        print_FARM_Reliability_Info(&farmdata->reliability, headcnt, farminterface, timeRestrictedRangeMS);
    }
    RESTORE_NONNULL_COMPARE
}
