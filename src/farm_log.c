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
#include "vendor/seagate/seagate_scsi_types.h"
#include "vendor/seagate/seagate_ata_types.h"

#define FARMC_LOG_HEADER_LENGTH             256
#define FARMC_LOG_DATA_SET_HEADER_LENGTH    32
#define ATA_FARM_LOG_PAGE_SIZE              (96 * 1024)         //96 KB
#define ATA_WORKLOAD_TRACE_PAGE_SIZE        (2048 * 1024)       //2048 KB
#define ATA_TIMESERIES_FRAME_LOG_SIZE       (27 * 96 * 1024)    //27 * 96 KB
#define FARM_SIGNATURE_LENGTH               16
#define FARM_DATASET_SIGNATURE_LENGTH       8

#define FARMC_LOG_MAJOR_VERSION             0
#define FARMC_LOG_MINOR_VERSION             0
#define FARMC_LOG_PATCH_VERSION             1

#define FARMC_SIGNATURE_ID                  "STX_FARMC_LOGS"

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

void addDataSetEntry(int32_t subPageType, uint8_t *dataSetHeader, uint32_t *numberOfDataSets, uint32_t *headerLength, uint16_t *dataSetHeaderLength, uint32_t *farmContentField, uint32_t dataSetLength)
{
    *numberOfDataSets += 1;
    *headerLength += FARMC_LOG_DATA_SET_HEADER_LENGTH;
    *dataSetHeaderLength += FARMC_LOG_DATA_SET_HEADER_LENGTH;
    *farmContentField |= M_BitN(subPageType);

    //farm current signature
    char signature[FARM_DATASET_SIGNATURE_LENGTH + 1] = { 0 };
    sprintf(signature, "%-*s", FARM_DATASET_SIGNATURE_LENGTH, farmSubPageSignatureId[subPageType]);
    memcpy(dataSetHeader, &signature, FARM_DATASET_SIGNATURE_LENGTH);
    memcpy(dataSetHeader + 12, &dataSetLength, sizeof(uint32_t));
}

void updateDataSetEntry(uint8_t *dataSetHeader, uint64_t startTimeStamp, uint64_t endTimeStamp, uint64_t offSet)
{
    memcpy(dataSetHeader + 8, &offSet, sizeof(uint32_t));
    memcpy(dataSetHeader + 16, &startTimeStamp, sizeof(uint64_t));
    memcpy(dataSetHeader + 24, &endTimeStamp, sizeof(uint64_t));
}

void generateATADataSetEntryHeaders(tDevice *device, uint32_t *numberOfDataSets, uint32_t *headerLength, uint16_t *dataSetHeaderLength, uint32_t *farmContentField,
    uint8_t *farmCurrentHeader, uint8_t *farmFactoryHeader, uint8_t *farmSaveHeader, uint8_t *farmTimeSeriesHeader, uint8_t *farmStickyHeader, uint8_t *farmWorkLoadTraceHeader,
    bool *isFarmCurrentValid, bool *isFarmFactoryValid, bool *isFarmSavedValid, bool *isFarmTimeSeriesValid, bool *isFarmStickyValid, bool *isFarmWorkloadValid)
{
    //check for farm series
    if (is_FARM_Log_Supported(device))
    {
        *isFarmCurrentValid = *isFarmFactoryValid = *isFarmSavedValid = true;

        //add entry for FARM current page (0xA6 - 0x00)
        addDataSetEntry(SUBPAGE_TYPE_FARM_CURRENT, farmCurrentHeader, numberOfDataSets, headerLength, dataSetHeaderLength, farmContentField, ATA_FARM_LOG_PAGE_SIZE);

        //add entry for FARM Factory page (0xA6 - 0x03)
        addDataSetEntry(SUBPAGE_TYPE_FARM_FACTORY, farmFactoryHeader, numberOfDataSets, headerLength, dataSetHeaderLength, farmContentField, ATA_FARM_LOG_PAGE_SIZE);

        //add entry for FARM saved page (0xA6 - 0x02)
        addDataSetEntry(SUBPAGE_TYPE_FARM_SAVE, farmSaveHeader, numberOfDataSets, headerLength, dataSetHeaderLength, farmContentField, ATA_FARM_LOG_PAGE_SIZE);
    }

    //check for farm farm time series
    if (is_FARM_Time_Series_Log_Supported(device))
    {
        *isFarmTimeSeriesValid = *isFarmStickyValid = *isFarmWorkloadValid = true;

        //add entry for FARM time series frame (0xC6 offset??)
        addDataSetEntry(SUBPAGE_TYPE_FARM_CURRENT, farmTimeSeriesHeader, numberOfDataSets, headerLength, dataSetHeaderLength, farmContentField, 16 * ATA_FARM_LOG_PAGE_SIZE);

        //add entry for FARM Sticky frame (0xC6 offset??)
        addDataSetEntry(SUBPAGE_TYPE_FARM_CURRENT, farmStickyHeader, numberOfDataSets, headerLength, dataSetHeaderLength, farmContentField, 6 * ATA_FARM_LOG_PAGE_SIZE);

        //add entry for FARM Workload Trace (0xC6 offset??)
        addDataSetEntry(SUBPAGE_TYPE_FARM_CURRENT, farmWorkLoadTraceHeader, numberOfDataSets, headerLength, dataSetHeaderLength, farmContentField, ATA_WORKLOAD_TRACE_PAGE_SIZE);
    }
}

void generateSCSIDataSetEntryHeaders(tDevice *device, uint32_t *numberOfDataSets, uint32_t *headerLength, uint16_t *dataSetHeaderLength, uint32_t *farmContentField,
    uint8_t *farmCurrentHeader, uint8_t *farmFactoryHeader, uint8_t *farmSaveHeader, uint8_t *farmTimeSeriesHeader, uint8_t *farmStickyHeader, uint8_t *farmWorkLoadTraceHeader,
    bool *isFarmCurrentValid, bool *isFarmFactoryValid, bool *isFarmSavedValid, bool *isFarmTimeSeriesValid, bool *isFarmStickyValid, bool *isFarmWorkloadValid)
{
	//check for farm series
	if (is_FARM_Log_Supported(device))
	{
		printf("\nFARM log supported");
		*isFarmCurrentValid = *isFarmFactoryValid = true;
		uint32_t returnValue = FAILURE;
		uint32_t logSize;

		//get length of FARM current page
		returnValue = get_SCSI_Log_Size(device, SEAGATE_LP_FARM, SEAGATE_FARM_SP_CURRENT, &logSize);
		//add entry for FARM current page (0x3D - 0x03)
		addDataSetEntry(SUBPAGE_TYPE_FARM_CURRENT, farmCurrentHeader, numberOfDataSets, headerLength, dataSetHeaderLength, farmContentField, logSize);

		//get length of Factory FARM page
		returnValue = get_SCSI_Log_Size(device, SEAGATE_LP_FARM, SEAGATE_FARM_SP_FACTORY, &logSize);
		//add entry for FARM Factory page (0x3D - 0x04)
		addDataSetEntry(SUBPAGE_TYPE_FARM_FACTORY, farmFactoryHeader, numberOfDataSets, headerLength, dataSetHeaderLength, farmContentField, logSize);
	}

	if (is_FARM_Time_Series_Log_Supported(device))
	{
		printf("\nFarm Time Series supported");
		*isFarmTimeSeriesValid = true;
	}


}

int32_t pullATAFarmLogs(tDevice *device, uint32_t transferSizeBytes, uint32_t delayTime, uint32_t logOffset, uint8_t *farmCurrentHeader, uint8_t *farmFactoryHeader, uint8_t *farmSaveHeader, uint8_t *farmTimeSeriesHeader, uint8_t *farmStickyHeader, uint8_t *farmWorkLoadTraceHeader,
    uint8_t *farmCurrentLog, uint8_t *farmFactoryLog, uint8_t *farmSavedLog, uint8_t *farmTimeSeriesLog, uint8_t *farmStickyLog, uint8_t *farmWorkLoadTraceLog)
{
    int32_t returnValue = FAILURE;
    uint64_t startTimeInMilliSecs = 0, endTimeInMilliSecs = 0;

    if (is_FARM_Log_Supported(device))
    {
        //FARM current logpage - (0xA6 - 0x00)
        startTimeInMilliSecs = time(NULL) * 1000;
        returnValue = get_ATA_Log(device, SEAGATE_ATA_LOG_FIELD_ACCESSIBLE_RELIABILITY_METRICS, NULL, NULL, true, false, true, farmCurrentLog, ATA_FARM_LOG_PAGE_SIZE, NULL, transferSizeBytes, SEAGATE_FARM_CURRENT, delayTime);
        if (returnValue != SUCCESS)
        {
            //print error
            printf("error in farm current get_ATA_Log, %d\n", returnValue);
            return returnValue;
        }
        else
        {
            endTimeInMilliSecs = time(NULL) * 1000;
            //update dataset header entry
            updateDataSetEntry(farmCurrentHeader, startTimeInMilliSecs, endTimeInMilliSecs, logOffset);
            logOffset += ATA_FARM_LOG_PAGE_SIZE;

            FILE *tempFile = fopen("farmcurrent.bin", "w+b");
            if (tempFile != NULL)
            {
                printf("writing into farmcurrent.bin file\n");
                if (fwrite(farmCurrentLog, sizeof(uint8_t), ATA_FARM_LOG_PAGE_SIZE, tempFile) != C_CAST(size_t, ATA_FARM_LOG_PAGE_SIZE)
                    || ferror(tempFile))
                {
                    printf("error in writing farmcurrent.bin file\n");
                }
                fclose(tempFile);
            }
        }

        //FARM factory logpage - (0xA6 - 0x03)
        startTimeInMilliSecs = time(NULL) * 1000;
        returnValue = get_ATA_Log(device, SEAGATE_ATA_LOG_FIELD_ACCESSIBLE_RELIABILITY_METRICS, NULL, NULL, true, false, true, farmFactoryLog, ATA_FARM_LOG_PAGE_SIZE, NULL, transferSizeBytes, SEAGATE_FARM_REPORT_FACTORY_DATA, delayTime);
        if (returnValue != SUCCESS)
        {
            //print error
            printf("error in farm factory get_ATA_Log, %d\n", returnValue);
            return returnValue;
        }
        else
        {
            endTimeInMilliSecs = time(NULL) * 1000;
            //add dataset header entry
            updateDataSetEntry(farmFactoryHeader, startTimeInMilliSecs, endTimeInMilliSecs, logOffset);
            logOffset += ATA_FARM_LOG_PAGE_SIZE;

            FILE *tempFile = fopen("farmfactory.bin", "w+b");
            if (tempFile != NULL)
            {
                printf("writing into farmfactory.bin file\n");
                if (fwrite(farmFactoryLog, sizeof(uint8_t), ATA_FARM_LOG_PAGE_SIZE, tempFile) != C_CAST(size_t, ATA_FARM_LOG_PAGE_SIZE)
                    || ferror(tempFile))
                {
                    printf("error in writing farmfactory.bin file\n");
                }
                fclose(tempFile);
            }
        }

        //FARM saved logpage - (0xA6 - 0x02)
        startTimeInMilliSecs = time(NULL) * 1000;
        returnValue = get_ATA_Log(device, SEAGATE_ATA_LOG_FIELD_ACCESSIBLE_RELIABILITY_METRICS, NULL, NULL, true, false, true, farmSavedLog, ATA_FARM_LOG_PAGE_SIZE, NULL, transferSizeBytes, SEAGATE_FARM_REPORT_SAVED, delayTime);
        if (returnValue != SUCCESS)
        {
            //print error
            printf("error in farm saved get_ATA_Log, %d\n", returnValue);
            return returnValue;
        }
        else
        {
            endTimeInMilliSecs = time(NULL) * 1000;
            //add dataset header entry
            updateDataSetEntry(farmSaveHeader, startTimeInMilliSecs, endTimeInMilliSecs, logOffset);
            logOffset += ATA_FARM_LOG_PAGE_SIZE;

            FILE *tempFile = fopen("farmsaved.bin", "w+b");
            if (tempFile != NULL)
            {
                printf("writing into farmsaved.bin file\n");
                if (fwrite(farmSavedLog, sizeof(uint8_t), ATA_FARM_LOG_PAGE_SIZE, tempFile) != C_CAST(size_t, ATA_FARM_LOG_PAGE_SIZE)
                    || ferror(tempFile))
                {
                    printf("error in writing farmsaved.bin file\n");
                }
                fclose(tempFile);
            }
        }
    }

    if (is_FARM_Time_Series_Log_Supported(device))
    {
        //FARM Time series logpage 0xC6 - feature 0x00

        //FARM Workload trace logpage 0xC6 - feature 0x02
    }

    return SUCCESS;
}

int32_t pullSCSIFarmLogs(tDevice *device, uint32_t transferSizeBytes, uint32_t delayTime, uint32_t logOffset, uint8_t *farmCurrentHeader, uint8_t *farmFactoryHeader, uint8_t *farmSaveHeader, uint8_t *farmTimeSeriesHeader, uint8_t *farmStickyHeader, uint8_t *farmWorkLoadTraceHeader,
    uint8_t *farmCurrentLog, uint8_t *farmFactoryLog, uint8_t *farmSavedLog, uint8_t *farmTimeSeriesLog, uint8_t *farmStickyLog, uint8_t *farmWorkLoadTraceLog)
{
	printf("\nPulling SCSI FARM logs");
	int32_t returnValue = FAILURE;
	uint64_t startTimeInMilliSecs = 0, endTimeInMilliSecs = 0;
	uint32_t currentLogSize, factoryLogSize; 

	memcpy(&currentLogSize, farmCurrentHeader + 12, sizeof(uint32_t));
	memcpy(&factoryLogSize, farmFactoryHeader + 12, sizeof(uint32_t));
	printf("\nFactory Log Size: %ld", factoryLogSize);

	//Get Current FARM log for SAS
	startTimeInMilliSecs = time(NULL) * 1000;
	returnValue = get_SCSI_Log(device, SEAGATE_LP_FARM, SEAGATE_FARM_SP_CURRENT, NULL, NULL, true, farmCurrentLog, currentLogSize , NULL);
	if (returnValue != SUCCESS)
	{
		//print error
		printf("error in farm current get_ATA_Log, %d\n", returnValue);
		return returnValue;
	}
	else
	{
		printf("\nSuccessfully pulled current FARM log");
		endTimeInMilliSecs = time(NULL) * 1000;
		//update dataset header entry
		updateDataSetEntry(farmCurrentHeader, startTimeInMilliSecs, endTimeInMilliSecs, logOffset);
		logOffset += currentLogSize;

		FILE *tempFile = fopen("farmcurrent.bin", "w+b");
		if (tempFile != NULL)
		{
			printf("writing into farmcurrent.bin file\n");
			if (fwrite(farmCurrentLog, sizeof(uint8_t), currentLogSize, tempFile) != C_CAST(size_t, currentLogSize)
				|| ferror(tempFile))
			{
				printf("error in writing farmcurrent.bin file\n");
			}
			fclose(tempFile);
		}
	}

	//Get Factory FARM log for SAS
	startTimeInMilliSecs = time(NULL) * 1000;
	returnValue = get_SCSI_Log(device, SEAGATE_LP_FARM, SEAGATE_FARM_SP_FACTORY, NULL, NULL, true, farmFactoryLog, factoryLogSize, NULL);
	if (returnValue != SUCCESS)
	{
		//print error
		printf("error in farm factory get_SCSI_Log, %d\n", returnValue);
		return returnValue;
	}
	else
	{
		printf("\nSuccessfully pulled factory FARM log");
		endTimeInMilliSecs = time(NULL) * 1000;
		//update dataset header entry
		updateDataSetEntry(farmFactoryHeader, startTimeInMilliSecs, endTimeInMilliSecs, logOffset);
		logOffset += factoryLogSize;

		FILE *tempFile = fopen("farmFactory.bin", "w+b");
		if (tempFile != NULL)
		{
			printf("writing into farmFactory.bin file\n");
			if (fwrite(farmFactoryLog, sizeof(uint8_t), factoryLogSize, tempFile) != C_CAST(size_t, factoryLogSize)
				|| ferror(tempFile))
			{
				printf("error in writing farmFactory.bin file\n");
			}
			fclose(tempFile);
		}
	}
	   	 


    //return NOT_SUPPORTED;
}

int pull_FARM_Combined_Log(tDevice *device, const char * const filePath, uint32_t transferSizeBytes, uint32_t delayTime)
{
    int32_t returnValue = NOT_SUPPORTED;
    uint8_t header[FARMC_LOG_HEADER_LENGTH] = { 0 };
    uint8_t farmCurrentHeader[FARMC_LOG_DATA_SET_HEADER_LENGTH] = { 0 };
    uint8_t farmFactoryHeader[FARMC_LOG_DATA_SET_HEADER_LENGTH] = { 0 };
    uint8_t farmSaveHeader[FARMC_LOG_DATA_SET_HEADER_LENGTH] = { 0 };
    uint8_t farmTimeSeriesHeader[FARMC_LOG_DATA_SET_HEADER_LENGTH] = { 0 };
    uint8_t farmStickyHeader[FARMC_LOG_DATA_SET_HEADER_LENGTH] = { 0 };
    uint8_t farmWorkLoadTraceHeader[FARMC_LOG_DATA_SET_HEADER_LENGTH] = { 0 };
    uint32_t numberOfDataSets = 0;
    uint32_t headerLength = FARMC_LOG_HEADER_LENGTH;
    uint16_t dataSetHeaderLength = 0;
    uint32_t farmContentField = 0;
    uint8_t *farmCurrentLog = NULL;
    uint8_t *farmFactoryLog = NULL;
    uint8_t *farmSavedLog = NULL;
    uint8_t *farmTimeSeriesLog = NULL;
    uint8_t *farmStickyLog = NULL;
    uint8_t *farmWorkLoadTraceLog = NULL;
    bool isFarmCurrentValid = false, isFarmFactoryValid = false, isFarmSavedValid = false;
    bool isFarmTimeSeriesValid = false, isFarmStickyValid = false, isFarmWorkloadValid = false;

    //set signature
    char signature[FARM_SIGNATURE_LENGTH + 1] = { 0 };
    sprintf(signature, "%-*s", FARM_SIGNATURE_LENGTH, FARMC_SIGNATURE_ID);
    memcpy(header, &signature, FARM_SIGNATURE_LENGTH);

    //set the version number - major.minor.revision
    uint64_t version = M_BytesTo4ByteValue(FARMC_LOG_MAJOR_VERSION, FARMC_LOG_MINOR_VERSION, FARMC_LOG_PATCH_VERSION, 0);
    memcpy(header + 16, &version, sizeof(uint64_t));

    //set interface type
    char interfaceType[4 + 1] = { 0 };
    if (device->drive_info.drive_type == ATA_DRIVE)
    {
        sprintf(interfaceType, "%-*s", 4, "SATA");
    }
    else if (device->drive_info.drive_type == SCSI_DRIVE)
    {
        sprintf(interfaceType, "%-*s", 4, "SAS");
    }
    else
    {
        sprintf(interfaceType, "%-*s", 4, "NVMe");
    }
    memcpy(header + 24, &interfaceType, 4);

    //set model#
    char modelNumber[MODEL_NUM_LEN + 1] = { 0 };
    sprintf(modelNumber, "%-*s", MODEL_NUM_LEN, device->drive_info.product_identification);
    memcpy(header + 28, &modelNumber, MODEL_NUM_LEN);

    //set serial#
    char serialNumber[SERIAL_NUM_LEN + 1] = { 0 };
    sprintf(serialNumber, "%-*s", SERIAL_NUM_LEN, device->drive_info.serialNumber);
    memcpy(header + 68, &serialNumber, SERIAL_NUM_LEN);

    //set firmware revision
    char firmwareVersion[FW_REV_LEN + 1] = { 0 };
    sprintf(firmwareVersion, "%-*s", FW_REV_LEN, device->drive_info.product_revision);
    memcpy(header + 88, &firmwareVersion, FW_REV_LEN);

    //generate data set entry header
    if (device->drive_info.drive_type == ATA_DRIVE)
    {
        generateATADataSetEntryHeaders(device, &numberOfDataSets, &headerLength, &dataSetHeaderLength, &farmContentField,
            farmCurrentHeader, farmFactoryHeader, farmSaveHeader, farmTimeSeriesHeader, farmStickyHeader, farmWorkLoadTraceHeader,
            &isFarmCurrentValid, &isFarmFactoryValid, &isFarmSavedValid, &isFarmTimeSeriesValid, &isFarmStickyValid, &isFarmWorkloadValid);
    }
    else if (device->drive_info.drive_type == SCSI_DRIVE)
    {
        generateSCSIDataSetEntryHeaders(device, &numberOfDataSets, &headerLength, &dataSetHeaderLength, &farmContentField,
            farmCurrentHeader, farmFactoryHeader, farmSaveHeader, farmTimeSeriesHeader, farmStickyHeader, farmWorkLoadTraceHeader,
            &isFarmCurrentValid, &isFarmFactoryValid, &isFarmSavedValid, &isFarmTimeSeriesValid, &isFarmStickyValid, &isFarmWorkloadValid);
    }
    else
    {
        return NOT_SUPPORTED;
    }

    //copy remaing fields for header information
    memcpy(header + 96, &headerLength, sizeof(uint32_t));
    memcpy(header + 100, &numberOfDataSets, sizeof(uint32_t));
    memcpy(header + 104, &dataSetHeaderLength, sizeof(uint16_t));
    memcpy(header + 106, &farmContentField, sizeof(uint32_t));

    do
    {
        //pull individual log subpage
        uint32_t logPageOffset = headerLength;
        if (device->drive_info.drive_type == ATA_DRIVE)
        {
            //initialize log buffers
            farmCurrentLog = C_CAST(uint8_t*, calloc_aligned(ATA_FARM_LOG_PAGE_SIZE, sizeof(uint8_t), device->os_info.minimumAlignment));               //96KB
            farmFactoryLog = C_CAST(uint8_t*, calloc_aligned(ATA_FARM_LOG_PAGE_SIZE, sizeof(uint8_t), device->os_info.minimumAlignment));               //96KB
            farmSavedLog = C_CAST(uint8_t*, calloc_aligned(ATA_FARM_LOG_PAGE_SIZE, sizeof(uint8_t), device->os_info.minimumAlignment));                 //96KB
            farmTimeSeriesLog = C_CAST(uint8_t*, calloc_aligned(16 * ATA_FARM_LOG_PAGE_SIZE, sizeof(uint8_t), device->os_info.minimumAlignment));       //16 * 96KB
            farmStickyLog = C_CAST(uint8_t*, calloc_aligned(6 * ATA_FARM_LOG_PAGE_SIZE, sizeof(uint8_t), device->os_info.minimumAlignment));            //6 * 96KB
            farmWorkLoadTraceLog = C_CAST(uint8_t*, calloc_aligned(ATA_WORKLOAD_TRACE_PAGE_SIZE, sizeof(uint8_t), device->os_info.minimumAlignment));   //2048KB
            returnValue = pullATAFarmLogs(device, transferSizeBytes, delayTime, logPageOffset,
                farmCurrentHeader, farmFactoryHeader, farmSaveHeader, farmTimeSeriesHeader, farmStickyHeader, farmWorkLoadTraceHeader,
                farmCurrentLog, farmFactoryLog, farmSavedLog, farmTimeSeriesLog, farmStickyLog, farmWorkLoadTraceLog);
            if (returnValue != SUCCESS)
            {
                //print error
                break;
            }
        }
        else if (device->drive_info.drive_type == SCSI_DRIVE)
        {
			uint32_t currentLogSize, factoryLogSize;

			memcpy(&currentLogSize, farmCurrentHeader + 12, sizeof(uint32_t));
			memcpy(&factoryLogSize, farmFactoryHeader + 12, sizeof(uint32_t));

			//Initialize Log Buffers
			farmCurrentLog = C_CAST(uint8_t*, calloc_aligned(currentLogSize, sizeof(uint8_t), device->os_info.minimumAlignment));               
			farmFactoryLog = C_CAST(uint8_t*, calloc_aligned(factoryLogSize, sizeof(uint8_t), device->os_info.minimumAlignment));  

			returnValue = pullSCSIFarmLogs(device, transferSizeBytes, delayTime, logPageOffset,
				farmCurrentHeader, farmFactoryHeader, farmSaveHeader, farmTimeSeriesHeader, farmStickyHeader, farmWorkLoadTraceHeader,
				farmCurrentLog, farmFactoryLog, farmSavedLog, farmTimeSeriesLog, farmStickyLog, farmWorkLoadTraceLog);
			if (returnValue != SUCCESS)
			{
				printf("Return Value : %d", returnValue);
				return returnValue;
			}
        }
        else
        {
            return NOT_SUPPORTED;
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
            if (isFarmCurrentValid)
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

            if (isFarmFactoryValid)
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

            if (isFarmSavedValid)
            {
                if ((fwrite(farmSaveHeader, sizeof(uint8_t), FARMC_LOG_DATA_SET_HEADER_LENGTH, farmCombinedLog) != C_CAST(size_t, FARMC_LOG_DATA_SET_HEADER_LENGTH))
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

            if (isFarmTimeSeriesValid)
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

            if (isFarmStickyValid)
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

            if (isFarmWorkloadValid)
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
            if (isFarmCurrentValid)
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

            if (isFarmFactoryValid)
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

            if (isFarmSavedValid)
            {
                uint32_t *datasetlength = C_CAST(uint32_t*, farmSaveHeader + 12);
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

            if (isFarmTimeSeriesValid)
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

            if (isFarmStickyValid)
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

            if (isFarmWorkloadValid)
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