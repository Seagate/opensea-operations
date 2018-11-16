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
// \file set_sector_size.c
// \brief This file defines the functions for performing some sector size changes

#include "set_sector_size.h"
#include "format_unit.h"
#include "logs.h"

bool is_Set_Sector_Configuration_Supported(tDevice *device)
{
    if (device->drive_info.drive_type == ATA_DRIVE)
    {
        uint8_t idDataLogSupportedCapabilities[LEGACY_DRIVE_SEC_SIZE] = { 0 };
        if (SUCCESS == send_ATA_Read_Log_Ext_Cmd(device, ATA_LOG_IDENTIFY_DEVICE_DATA, ATA_ID_DATA_LOG_SUPPORTED_CAPABILITIES, idDataLogSupportedCapabilities, LEGACY_DRIVE_SEC_SIZE, 0))
        {
            uint64_t supportedCapabilitiesQWord = M_BytesTo8ByteValue(&idDataLogSupportedCapabilities[15], &idDataLogSupportedCapabilities[14], &idDataLogSupportedCapabilities[13], &idDataLogSupportedCapabilities[12], &idDataLogSupportedCapabilities[11], &idDataLogSupportedCapabilities[10], &idDataLogSupportedCapabilities[9], &idDataLogSupportedCapabilities[8]);
            if (supportedCapabilitiesQWord & BIT49)
            {
                return true;
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
        return scsiSectorSizesSupported;
    }
    else
    {
        return 0;
    }
}

int get_Supported_Sector_Sizes(tDevice *device, sectorSize * ptrSectorSizeList, uint32_t numberOfSectorSizeStructs)
{
    int ret = NOT_SUPPORTED;
    if (!ptrSectorSizeList)
    {
        return BAD_PARAMETER;
    }
    if (is_Set_Sector_Configuration_Supported(device))
    {
        if (device->drive_info.drive_type == ATA_DRIVE)
        {
            uint8_t sectorConfigurationLog[LEGACY_DRIVE_SEC_SIZE] = { 0 };
            if (SUCCESS == send_ATA_Read_Log_Ext_Cmd(device, ATA_LOG_SECTOR_CONFIGURATION_LOG, 0, sectorConfigurationLog, LEGACY_DRIVE_SEC_SIZE, 0))
            {
                for (uint16_t iter = 0, sectorSizeCounter = 0; iter < LEGACY_DRIVE_SEC_SIZE && sectorSizeCounter < numberOfSectorSizeStructs; iter += 16, ++sectorSizeCounter)
                {
                    ptrSectorSizeList[sectorSizeCounter].logicalBlockLength = M_BytesTo4ByteValue(sectorConfigurationLog[7 + iter], sectorConfigurationLog[6 + iter], sectorConfigurationLog[5 + iter], sectorConfigurationLog[4 + iter]) * 2;
                    ptrSectorSizeList[sectorSizeCounter].ataSetSectorFields.descriptorCheck = M_BytesTo2ByteValue(sectorConfigurationLog[3 + iter], sectorConfigurationLog[2 + iter]);
					if (ptrSectorSizeList[sectorSizeCounter].logicalBlockLength > 0 && ptrSectorSizeList[sectorSizeCounter].ataSetSectorFields.descriptorCheck != 0)
					{
						ptrSectorSizeList[sectorSizeCounter].valid = true;
					}
                    ptrSectorSizeList[sectorSizeCounter].ataSetSectorFields.descriptorIndex = (uint8_t)(iter / 16);
                }
                ret = SUCCESS;
            }
        }
        else if (device->drive_info.drive_type == SCSI_DRIVE)
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
                    for (uint32_t iter = 4, sectorSizeCounter = 0; (iter + 8) < supportedSectorSizesDataLength && sectorSizeCounter < numberOfSectorSizeStructs; iter += 8, ++sectorSizeCounter)
                    {
                        ptrSectorSizeList[sectorSizeCounter].valid = true;
                        ptrSectorSizeList[sectorSizeCounter].logicalBlockLength = M_BytesTo4ByteValue(supportedBlockLengthsData[iter + 0], supportedBlockLengthsData[iter + 1], supportedBlockLengthsData[iter + 2], supportedBlockLengthsData[iter + 3]);
                        ptrSectorSizeList[sectorSizeCounter].scsiSectorBits.p_i_i_sup = (bool)(supportedBlockLengthsData[iter + 4] & BIT6);
                        ptrSectorSizeList[sectorSizeCounter].scsiSectorBits.no_pi_chk = (bool)(supportedBlockLengthsData[iter + 4] & BIT3);
                        ptrSectorSizeList[sectorSizeCounter].scsiSectorBits.grd_chk = (bool)(supportedBlockLengthsData[iter + 4] & BIT2);
                        ptrSectorSizeList[sectorSizeCounter].scsiSectorBits.app_chk = (bool)(supportedBlockLengthsData[iter + 4] & BIT1);
                        ptrSectorSizeList[sectorSizeCounter].scsiSectorBits.ref_chk = (bool)(supportedBlockLengthsData[iter + 5] & BIT0);
                        ptrSectorSizeList[sectorSizeCounter].scsiSectorBits.t3ps = (bool)(supportedBlockLengthsData[iter + 5] & BIT3);
                        ptrSectorSizeList[sectorSizeCounter].scsiSectorBits.t2ps = (bool)(supportedBlockLengthsData[iter + 5] & BIT2);
                        ptrSectorSizeList[sectorSizeCounter].scsiSectorBits.t1ps = (bool)(supportedBlockLengthsData[iter + 5] & BIT1);
                        ptrSectorSizeList[sectorSizeCounter].scsiSectorBits.t0ps = (bool)(supportedBlockLengthsData[iter + 5] & BIT0);
                    }
                    ret = SUCCESS;
                }
                safe_Free(supportedBlockLengthsData);
            }
        }
    }
    return ret;
}

int show_Supported_Sector_Sizes(tDevice *device)
{
    int ret = NOT_SUPPORTED;
    uint32_t numberOfSectorSizes = get_Number_Of_Supported_Sector_Sizes(device);
    if (numberOfSectorSizes == 0)
    {
        printf("Device does not support changing sector size or does not report available sector sizes\n");
        return ret;
    }
    sectorSize *sectorSizes = (sectorSize*)calloc(numberOfSectorSizes * sizeof(sectorSize), sizeof(sectorSize));
    ret = get_Supported_Sector_Sizes(device, sectorSizes, numberOfSectorSizes);
    if (SUCCESS == ret)
    {
        printf("Supported Logical Block Lengths:\n");
        for (uint8_t sectorSizeIter = 0; sectorSizeIter < numberOfSectorSizes; ++sectorSizeIter)
        {
            if (!sectorSizes[sectorSizeIter].valid)
            {
                break;
            }
            printf("\tLogical Block Size = %"PRIu32"\n", sectorSizes[sectorSizeIter].logicalBlockLength);
            if (device->drive_info.drive_type == SCSI_DRIVE)
            {
                printf("\t\tProtection Types: ");
                bool printed = false;
                if (sectorSizes[sectorSizeIter].scsiSectorBits.t3ps)
                {
                    printed = true;
                    printf("3");
                }
                if (printed)
                {
                    printf(", ");
                    printed = false;
                }
                if (sectorSizes[sectorSizeIter].scsiSectorBits.t2ps)
                {
                    printed = true;
                    printf("2");
                }
                if (printed)
                {
                    printf(", ");
                    printed = false;
                }
                if (sectorSizes[sectorSizeIter].scsiSectorBits.t1ps)
                {
                    printed = true;
                    printf("1");
                }
                if (printed)
                {
                    printf(", ");
                    printed = false;
                }
                if (sectorSizes[sectorSizeIter].scsiSectorBits.t0ps)
                {
                    printf("0");
                }
            }
            printf("\n");
        }
        ret = SUCCESS;
    }
    else
    {
        printf("Device does not support changing sector size or does not report available sector sizes\n");
    }
    safe_Free(sectorSizes);
    return ret;
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
        sectorSize *sectorSizes = (sectorSize*)calloc(MAX_NUMBER_SUPPORTED_SECTOR_SIZES * sizeof(sectorSize), sizeof(sectorSize));
        if (!sectorSizes)
        {
            return MEMORY_FAILURE;
        }
        ret = get_Supported_Sector_Sizes(device, sectorSizes, MAX_NUMBER_SUPPORTED_SECTOR_SIZES);
        if (SUCCESS == ret)
        {
            for (uint8_t sectorSizeIter = 0; sectorSizeIter < MAX_NUMBER_SUPPORTED_SECTOR_SIZES; ++sectorSizeIter)
            {
                if (!sectorSizes[sectorSizeIter].valid)
                {
                    break;
                }
                if (sectorSizes[sectorSizeIter].logicalBlockLength == logicalBlockLength)
                {
                    *descriptorCheckCode = sectorSizes[sectorSizeIter].ataSetSectorFields.descriptorCheck;
                    *descriptorIndex = sectorSizes[sectorSizeIter].ataSetSectorFields.descriptorIndex;
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
