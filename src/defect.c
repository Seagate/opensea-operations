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
// \file defect.h
// \brief This file defines the functions for creating and reading defect information

#include "defect.h"

int get_SCSI_Defect_List(tDevice *device, eSCSIAddressDescriptors defectListFormat, bool grownList, bool primaryList, scsiDefectList **defects)
{
    int ret = SUCCESS;
    if (defects)
    {
        bool tenByte = false;
        bool gotDefectData = false;
        bool listHasPrimaryDescriptors = false;
        bool listHasGrownDescriptors = false;
        uint16_t generationCode = 0;
        uint8_t returnedDefectListFormat = UINT8_MAX;
        uint32_t dataLength = 8;
        uint8_t *defectData = (uint8_t*)calloc(dataLength, sizeof(uint8_t));
        uint32_t defectListLength = 0;
        if (device->drive_info.scsiVersion > 2 && (ret = scsi_Read_Defect_Data_12(device, primaryList, grownList, defectListFormat, 0, dataLength, defectData)) == SUCCESS)
        {
            gotDefectData = true;
            defectListLength = M_BytesTo4ByteValue(defectData[4], defectData[5], defectData[6], defectData[7]);
            listHasPrimaryDescriptors = defectData[1] & BIT4;
            listHasGrownDescriptors = defectData[1] & BIT3;
            returnedDefectListFormat = M_GETBITRANGE(defectData[1], 2, 0);
            generationCode = M_BytesTo2ByteValue(defectData[2], defectData[3]);
        }
        else
        {
            dataLength = 4;
            if ((ret = scsi_Read_Defect_Data_10(device, primaryList, grownList, defectListFormat, dataLength, defectData) == SUCCESS))
            {
                tenByte = true;
                gotDefectData = true;
                defectListLength = M_BytesTo2ByteValue(defectData[2], defectData[3]);
                listHasPrimaryDescriptors = defectData[1] & BIT4;
                listHasGrownDescriptors = defectData[1] & BIT3;
                returnedDefectListFormat = M_GETBITRANGE(defectData[1], 2, 0);
            }
        }
        if (gotDefectData)
        {
            if (defectListLength > 0)
            {
                uint32_t numberOfElements = 0;
                uint32_t defectAlloc = 0;
                //get the defect list length and
                switch (returnedDefectListFormat)
                {
                case AD_SHORT_BLOCK_FORMAT_ADDRESS_DESCRIPTOR:
                    numberOfElements = defectListLength / 4;
                    defectAlloc = numberOfElements * sizeof(blockFormatAddress);
                    break;
                case AD_LONG_BLOCK_FORMAT_ADDRESS_DESCRIPTOR:
                    numberOfElements = defectListLength / 8;
                    defectAlloc = numberOfElements * sizeof(blockFormatAddress);
                    break;
                case AD_EXTENDED_BYTES_FROM_INDEX_FORMAT_ADDRESS_DESCRIPTOR:
                case AD_BYTES_FROM_INDEX_FORMAT_ADDRESS_DESCRIPTOR:
                    numberOfElements = defectListLength / 8;
                    defectAlloc = numberOfElements * sizeof(bytesFromIndexAddress);
                    break;
                case AD_EXTENDED_PHYSICAL_SECTOR_FORMAT_ADDRESS_DESCRIPTOR:
                case AD_PHYSICAL_SECTOR_FORMAT_ADDRESS_DESCRIPTOR:
                    numberOfElements = defectListLength / 8;
                    defectAlloc = numberOfElements * sizeof(physicalSectorAddress);
                    break;
                case AD_VENDOR_SPECIFIC:
                case AD_RESERVED:
                    //TODO: should we just return the raw data in an array?
                    ret = BAD_PARAMETER;
                    break;
                }
                if (ret == SUCCESS)
                {
                    if (tenByte)
                    {
                        //single command
                        dataLength += defectListLength;
                        if (dataLength > UINT16_MAX)
                        {
                            dataLength = UINT16_MAX; //we cannot pull more than this with this command. - TJE
                        }
                        uint8_t *temp = (uint8_t*)realloc(defectData, dataLength);
                        if (temp)
                        {
                            defectData = temp;
                            memset(defectData, 0, dataLength);
                            if (SUCCESS == (ret = scsi_Read_Defect_Data_10(device, primaryList, grownList, defectListFormat, dataLength, defectData)))
                            {
                                uint32_t offset = 4;
                                defectListLength = M_BytesTo2ByteValue(defectData[2], defectData[3]);
                                listHasPrimaryDescriptors = defectData[1] & BIT4;
                                listHasGrownDescriptors = defectData[1] & BIT3;
                                returnedDefectListFormat = M_GETBITRANGE(defectData[1], 2, 0);
                                //now allocate our list to return to the caller!
                                *defects = (ptrSCSIDefectList)malloc(sizeof(scsiDefectList) + defectAlloc);
                                if (*defects)
                                {
                                    ptrSCSIDefectList ptrDefects = *defects;
                                    memset(ptrDefects, 0, sizeof(scsiDefectList) + defectAlloc);
                                    ptrDefects->numberOfElements = numberOfElements;
                                    ptrDefects->containsGrownList = listHasGrownDescriptors;
                                    ptrDefects->containsPrimaryList = listHasPrimaryDescriptors;
                                    ptrDefects->format = returnedDefectListFormat;
                                    uint8_t increment = 0;
                                    switch (returnedDefectListFormat)
                                    {
                                    case AD_SHORT_BLOCK_FORMAT_ADDRESS_DESCRIPTOR:
                                        increment = 4;
                                        break;
                                    case AD_LONG_BLOCK_FORMAT_ADDRESS_DESCRIPTOR:
                                    case AD_EXTENDED_BYTES_FROM_INDEX_FORMAT_ADDRESS_DESCRIPTOR:
                                    case AD_BYTES_FROM_INDEX_FORMAT_ADDRESS_DESCRIPTOR:
                                    case AD_EXTENDED_PHYSICAL_SECTOR_FORMAT_ADDRESS_DESCRIPTOR:
                                    case AD_PHYSICAL_SECTOR_FORMAT_ADDRESS_DESCRIPTOR:
                                        increment = 8;
                                        break;
                                    }
                                    for (uint32_t elementNumber = 0; elementNumber < numberOfElements && offset < defectListLength; ++elementNumber, offset += increment)
                                    {
                                        switch (returnedDefectListFormat)
                                        {
                                        case AD_SHORT_BLOCK_FORMAT_ADDRESS_DESCRIPTOR:
                                            ptrDefects->block[elementNumber].shortBlockAddress = M_BytesTo4ByteValue(defectData[offset + 0], defectData[offset + 1], defectData[offset + 2], defectData[offset + 3]);
                                            break;
                                        case AD_LONG_BLOCK_FORMAT_ADDRESS_DESCRIPTOR:
                                            ptrDefects->block[elementNumber].longBlockAddress = M_BytesTo8ByteValue(defectData[offset + 0], defectData[offset + 1], defectData[offset + 2], defectData[offset + 3], defectData[offset + 4], defectData[offset + 5], defectData[offset + 6], defectData[offset + 7]);
                                            break;
                                        case AD_EXTENDED_BYTES_FROM_INDEX_FORMAT_ADDRESS_DESCRIPTOR:
                                            ptrDefects->bfi[elementNumber].cylinderNumber = M_BytesTo4ByteValue(0, defectData[offset + 0], defectData[offset + 1], defectData[offset + 2]);
                                            ptrDefects->bfi[elementNumber].headNumber = defectData[offset + 3];
                                            ptrDefects->bfi[elementNumber].multiAddressDescriptorStart = defectData[offset + 4] & BIT7;
                                            ptrDefects->bfi[elementNumber].bytesFromIndex = M_BytesTo4ByteValue(M_GETBITRANGE(defectData[offset + 4], 3, 0), defectData[offset + 5], defectData[offset + 6], defectData[offset + 7]);
                                            break;
                                        case AD_BYTES_FROM_INDEX_FORMAT_ADDRESS_DESCRIPTOR:
                                            ptrDefects->bfi[elementNumber].cylinderNumber = M_BytesTo4ByteValue(0, defectData[offset + 0], defectData[offset + 1], defectData[offset + 2]);
                                            ptrDefects->bfi[elementNumber].headNumber = defectData[offset + 3];
                                            ptrDefects->bfi[elementNumber].bytesFromIndex = M_BytesTo4ByteValue(defectData[offset + 4], defectData[offset + 5], defectData[offset + 6], defectData[offset + 7]);
                                            break;
                                        case AD_EXTENDED_PHYSICAL_SECTOR_FORMAT_ADDRESS_DESCRIPTOR:
                                            ptrDefects->physical[elementNumber].cylinderNumber = M_BytesTo4ByteValue(0, defectData[offset + 0], defectData[offset + 1], defectData[offset + 2]);
                                            ptrDefects->physical[elementNumber].headNumber = defectData[offset + 3];
                                            ptrDefects->physical[elementNumber].multiAddressDescriptorStart = defectData[offset + 4] & BIT7;
                                            ptrDefects->physical[elementNumber].sectorNumber = M_BytesTo4ByteValue(M_GETBITRANGE(defectData[offset + 4], 3, 0), defectData[offset + 5], defectData[offset + 6], defectData[offset + 7]);
                                        case AD_PHYSICAL_SECTOR_FORMAT_ADDRESS_DESCRIPTOR:
                                            ptrDefects->physical[elementNumber].cylinderNumber = M_BytesTo4ByteValue(0, defectData[offset + 0], defectData[offset + 1], defectData[offset + 2]);
                                            ptrDefects->physical[elementNumber].headNumber = defectData[offset + 3];
                                            ptrDefects->physical[elementNumber].sectorNumber = M_BytesTo4ByteValue(defectData[offset + 4], defectData[offset + 5], defectData[offset + 6], defectData[offset + 7]);
                                            break;
                                        }
                                    }
                                }
                                else
                                {
                                    ret = MEMORY_FAILURE;
                                }
                            }
                        }
                        else
                        {
                            ret = MEMORY_FAILURE;
                        }
                    }
                    else
                    {
                        bool multipleCommandsSupported = false;
                        //possibly multiple commands (if address descriptor index is supported in the command...added in SBC3)
                        uint8_t reportOPCodeSupport[16] = { 0 };
                        if (SUCCESS == scsi_Report_Supported_Operation_Codes(device, false, REPORT_OPERATION_CODE, READ_DEFECT_DATA_12_CMD, 0, 16, reportOPCodeSupport))
                        {
                            //skipping the support data since we shouldn't be this far if the command isn't supported!
                            uint32_t addressIndexSupport = M_BytesTo4ByteValue(reportOPCodeSupport[6], reportOPCodeSupport[7], reportOPCodeSupport[8], reportOPCodeSupport[9]);
                            if (addressIndexSupport > 0)
                            {
                                multipleCommandsSupported = true;
                            }
                        }
                        if (multipleCommandsSupported)
                        {
                            //read the list in multiple commands! Do this in 64k chunks.
                            dataLength = 65536;//this is 64k
                            uint8_t *temp = (uint8_t*)realloc(defectData, dataLength);
                            if (temp)
                            {
                                defectData = temp;
                                memset(defectData, 0, dataLength);
                                *defects = (ptrSCSIDefectList)malloc(sizeof(scsiDefectList) + defectAlloc);
                                if (*defects)
                                {
                                    uint32_t elementNumber = 0;
                                    uint32_t offset = 0;
                                    uint32_t increment = 0;
                                    ptrSCSIDefectList ptrDefects = *defects;
                                    bool filledInListInfo = false;
                                    memset(ptrDefects, 0, sizeof(scsiDefectList) + defectAlloc);
                                    ptrDefects->numberOfElements = numberOfElements;
                                    while (elementNumber < numberOfElements)
                                    {
                                        offset = 8;//reset the offset to 8 each time through the while loop since we will start reading the list over and over after each command
                                        if (SUCCESS == (ret = scsi_Read_Defect_Data_12(device, primaryList, grownList, defectListFormat, elementNumber, dataLength, defectData)))
                                        {
                                            defectListLength = M_BytesTo4ByteValue(defectData[4], defectData[5], defectData[6], defectData[7]);
                                            listHasPrimaryDescriptors = defectData[1] & BIT4;
                                            listHasGrownDescriptors = defectData[1] & BIT3;
                                            returnedDefectListFormat = M_GETBITRANGE(defectData[1], 2, 0);
                                            generationCode = M_BytesTo2ByteValue(defectData[2], defectData[3]);
                                            if (!filledInListInfo)
                                            {
                                                ptrDefects->containsGrownList = listHasGrownDescriptors;
                                                ptrDefects->containsPrimaryList = listHasPrimaryDescriptors;
                                                ptrDefects->format = returnedDefectListFormat;
                                                ptrDefects->generation = generationCode;
                                                filledInListInfo = true;
                                            }
                                            //set the increment amount
                                            switch (returnedDefectListFormat)
                                            {
                                            case AD_SHORT_BLOCK_FORMAT_ADDRESS_DESCRIPTOR:
                                                increment = 4;
                                                break;
                                            case AD_LONG_BLOCK_FORMAT_ADDRESS_DESCRIPTOR:
                                            case AD_EXTENDED_BYTES_FROM_INDEX_FORMAT_ADDRESS_DESCRIPTOR:
                                            case AD_BYTES_FROM_INDEX_FORMAT_ADDRESS_DESCRIPTOR:
                                            case AD_EXTENDED_PHYSICAL_SECTOR_FORMAT_ADDRESS_DESCRIPTOR:
                                            case AD_PHYSICAL_SECTOR_FORMAT_ADDRESS_DESCRIPTOR:
                                                increment = 8;
                                                break;
                                            default://shouldn't get here!
                                                break;
                                            }
                                            if (increment > 0)
                                            {
                                                for (; elementNumber < numberOfElements && offset < defectListLength; ++elementNumber, offset += increment)
                                                {
                                                    switch (returnedDefectListFormat)
                                                    {
                                                    case AD_SHORT_BLOCK_FORMAT_ADDRESS_DESCRIPTOR:
                                                        ptrDefects->block[elementNumber].shortBlockAddress = M_BytesTo4ByteValue(defectData[offset + 0], defectData[offset + 1], defectData[offset + 2], defectData[offset + 3]);
                                                        break;
                                                    case AD_LONG_BLOCK_FORMAT_ADDRESS_DESCRIPTOR:
                                                        ptrDefects->block[elementNumber].longBlockAddress = M_BytesTo8ByteValue(defectData[offset + 0], defectData[offset + 1], defectData[offset + 2], defectData[offset + 3], defectData[offset + 4], defectData[offset + 5], defectData[offset + 6], defectData[offset + 7]);
                                                        break;
                                                    case AD_EXTENDED_BYTES_FROM_INDEX_FORMAT_ADDRESS_DESCRIPTOR:
                                                        ptrDefects->bfi[elementNumber].cylinderNumber = M_BytesTo4ByteValue(0, defectData[offset + 0], defectData[offset + 1], defectData[offset + 2]);
                                                        ptrDefects->bfi[elementNumber].headNumber = defectData[offset + 3];
                                                        ptrDefects->bfi[elementNumber].multiAddressDescriptorStart = defectData[offset + 4] & BIT7;
                                                        ptrDefects->bfi[elementNumber].bytesFromIndex = M_BytesTo4ByteValue(M_GETBITRANGE(defectData[offset + 4], 3, 0), defectData[offset + 5], defectData[offset + 6], defectData[offset + 7]);
                                                        break;
                                                    case AD_BYTES_FROM_INDEX_FORMAT_ADDRESS_DESCRIPTOR:
                                                        ptrDefects->bfi[elementNumber].cylinderNumber = M_BytesTo4ByteValue(0, defectData[offset + 0], defectData[offset + 1], defectData[offset + 2]);
                                                        ptrDefects->bfi[elementNumber].headNumber = defectData[offset + 3];
                                                        ptrDefects->bfi[elementNumber].bytesFromIndex = M_BytesTo4ByteValue(defectData[offset + 4], defectData[offset + 5], defectData[offset + 6], defectData[offset + 7]);
                                                        break;
                                                    case AD_EXTENDED_PHYSICAL_SECTOR_FORMAT_ADDRESS_DESCRIPTOR:
                                                        ptrDefects->physical[elementNumber].cylinderNumber = M_BytesTo4ByteValue(0, defectData[offset + 0], defectData[offset + 1], defectData[offset + 2]);
                                                        ptrDefects->physical[elementNumber].headNumber = defectData[offset + 3];
                                                        ptrDefects->physical[elementNumber].multiAddressDescriptorStart = defectData[offset + 4] & BIT7;
                                                        ptrDefects->physical[elementNumber].sectorNumber = M_BytesTo4ByteValue(M_GETBITRANGE(defectData[offset + 4], 3, 0), defectData[offset + 5], defectData[offset + 6], defectData[offset + 7]);
                                                    case AD_PHYSICAL_SECTOR_FORMAT_ADDRESS_DESCRIPTOR:
                                                        ptrDefects->physical[elementNumber].cylinderNumber = M_BytesTo4ByteValue(0, defectData[offset + 0], defectData[offset + 1], defectData[offset + 2]);
                                                        ptrDefects->physical[elementNumber].headNumber = defectData[offset + 3];
                                                        ptrDefects->physical[elementNumber].sectorNumber = M_BytesTo4ByteValue(defectData[offset + 4], defectData[offset + 5], defectData[offset + 6], defectData[offset + 7]);
                                                        break;
                                                    }
                                                }
                                            }
                                            else
                                            {
                                                break;
                                            }
                                        }
                                        if (increment == 0)
                                        {
                                            //don't want an infinite loop
                                            break;
                                        }
                                    }
                                }
                                else
                                {
                                    ret = MEMORY_FAILURE;
                                }
                            }
                            else
                            {
                                ret = MEMORY_FAILURE;
                            }
                        }
                        else
                        {
                            //single command
                            if (defectListLength > (UINT32_MAX - 8))
                            {
                                dataLength = UINT32_MAX; //we cannot pull more than this with this command. - TJE
                            }
                            else
                            {
                                dataLength += defectListLength;
                            }
                            uint8_t *temp = (uint8_t*)realloc(defectData, dataLength);
                            if (temp)
                            {
                                defectData = temp;
                                memset(defectData, 0, dataLength);
                                if (SUCCESS == (ret = scsi_Read_Defect_Data_12(device, primaryList, grownList, defectListFormat, 0, dataLength, defectData)))
                                {
                                    uint32_t offset = 8;
                                    defectListLength = M_BytesTo4ByteValue(defectData[4], defectData[5], defectData[6], defectData[7]);
                                    listHasPrimaryDescriptors = defectData[1] & BIT4;
                                    listHasGrownDescriptors = defectData[1] & BIT3;
                                    returnedDefectListFormat = M_GETBITRANGE(defectData[1], 2, 0);
                                    generationCode = M_BytesTo2ByteValue(defectData[2], defectData[3]);
                                    //now allocate our list to return to the caller!
                                    *defects = (ptrSCSIDefectList)malloc(sizeof(scsiDefectList) + defectAlloc);
                                    if (*defects)
                                    {
                                        ptrSCSIDefectList ptrDefects = *defects;
                                        memset(ptrDefects, 0, sizeof(scsiDefectList) + defectAlloc);
                                        ptrDefects->numberOfElements = numberOfElements;
                                        ptrDefects->containsGrownList = listHasGrownDescriptors;
                                        ptrDefects->containsPrimaryList = listHasPrimaryDescriptors;
                                        ptrDefects->format = returnedDefectListFormat;
                                        ptrDefects->generation = generationCode;
                                        uint8_t increment = 0;
                                        switch (returnedDefectListFormat)
                                        {
                                        case AD_SHORT_BLOCK_FORMAT_ADDRESS_DESCRIPTOR:
                                            increment = 4;
                                            break;
                                        case AD_LONG_BLOCK_FORMAT_ADDRESS_DESCRIPTOR:
                                        case AD_EXTENDED_BYTES_FROM_INDEX_FORMAT_ADDRESS_DESCRIPTOR:
                                        case AD_BYTES_FROM_INDEX_FORMAT_ADDRESS_DESCRIPTOR:
                                        case AD_EXTENDED_PHYSICAL_SECTOR_FORMAT_ADDRESS_DESCRIPTOR:
                                        case AD_PHYSICAL_SECTOR_FORMAT_ADDRESS_DESCRIPTOR:
                                            increment = 8;
                                            break;
                                        }
                                        for (uint32_t elementNumber = 0; elementNumber < numberOfElements && offset < defectListLength; ++elementNumber, offset += increment)
                                        {
                                            switch (returnedDefectListFormat)
                                            {
                                            case AD_SHORT_BLOCK_FORMAT_ADDRESS_DESCRIPTOR:
                                                ptrDefects->block[elementNumber].shortBlockAddress = M_BytesTo4ByteValue(defectData[offset + 0], defectData[offset + 1], defectData[offset + 2], defectData[offset + 3]);
                                                break;
                                            case AD_LONG_BLOCK_FORMAT_ADDRESS_DESCRIPTOR:
                                                ptrDefects->block[elementNumber].longBlockAddress = M_BytesTo8ByteValue(defectData[offset + 0], defectData[offset + 1], defectData[offset + 2], defectData[offset + 3], defectData[offset + 4], defectData[offset + 5], defectData[offset + 6], defectData[offset + 7]);
                                                break;
                                            case AD_EXTENDED_BYTES_FROM_INDEX_FORMAT_ADDRESS_DESCRIPTOR:
                                                ptrDefects->bfi[elementNumber].cylinderNumber = M_BytesTo4ByteValue(0, defectData[offset + 0], defectData[offset + 1], defectData[offset + 2]);
                                                ptrDefects->bfi[elementNumber].headNumber = defectData[offset + 3];
                                                ptrDefects->bfi[elementNumber].multiAddressDescriptorStart = defectData[offset + 4] & BIT7;
                                                ptrDefects->bfi[elementNumber].bytesFromIndex = M_BytesTo4ByteValue(M_GETBITRANGE(defectData[offset + 4], 3, 0), defectData[offset + 5], defectData[offset + 6], defectData[offset + 7]);
                                                break;
                                            case AD_BYTES_FROM_INDEX_FORMAT_ADDRESS_DESCRIPTOR:
                                                ptrDefects->bfi[elementNumber].cylinderNumber = M_BytesTo4ByteValue(0, defectData[offset + 0], defectData[offset + 1], defectData[offset + 2]);
                                                ptrDefects->bfi[elementNumber].headNumber = defectData[offset + 3];
                                                ptrDefects->bfi[elementNumber].bytesFromIndex = M_BytesTo4ByteValue(defectData[offset + 4], defectData[offset + 5], defectData[offset + 6], defectData[offset + 7]);
                                                break;
                                            case AD_EXTENDED_PHYSICAL_SECTOR_FORMAT_ADDRESS_DESCRIPTOR:
                                                ptrDefects->physical[elementNumber].cylinderNumber = M_BytesTo4ByteValue(0, defectData[offset + 0], defectData[offset + 1], defectData[offset + 2]);
                                                ptrDefects->physical[elementNumber].headNumber = defectData[offset + 3];
                                                ptrDefects->physical[elementNumber].multiAddressDescriptorStart = defectData[offset + 4] & BIT7;
                                                ptrDefects->physical[elementNumber].sectorNumber = M_BytesTo4ByteValue(M_GETBITRANGE(defectData[offset + 4], 3, 0), defectData[offset + 5], defectData[offset + 6], defectData[offset + 7]);
                                            case AD_PHYSICAL_SECTOR_FORMAT_ADDRESS_DESCRIPTOR:
                                                ptrDefects->physical[elementNumber].cylinderNumber = M_BytesTo4ByteValue(0, defectData[offset + 0], defectData[offset + 1], defectData[offset + 2]);
                                                ptrDefects->physical[elementNumber].headNumber = defectData[offset + 3];
                                                ptrDefects->physical[elementNumber].sectorNumber = M_BytesTo4ByteValue(defectData[offset + 4], defectData[offset + 5], defectData[offset + 6], defectData[offset + 7]);
                                                break;
                                            }
                                        }
                                    }
                                    else
                                    {
                                        ret = MEMORY_FAILURE;
                                    }
                                }
                            }
                            else
                            {
                                ret = MEMORY_FAILURE;
                            }
                        }
                    }
                }
            }
            else
            {
                //defect list length is zero, so we don't have anything else to do but allocate the struct and populate the data, then return it
                *defects = (ptrSCSIDefectList)calloc(1, sizeof(scsiDefectList));
                if (*defects)
                {
                    ptrSCSIDefectList temp = *defects;
                    temp->numberOfElements = 0;
                    temp->containsGrownList = listHasGrownDescriptors;
                    temp->containsPrimaryList = listHasPrimaryDescriptors;
                    temp->generation = generationCode;
                    temp->format = returnedDefectListFormat;
                    ret = SUCCESS;
                }
                else
                {
                    ret = MEMORY_FAILURE;
                }
            }
        }
        safe_Free(defectData);
    }
    else
    {
        ret = BAD_PARAMETER;
    }
    return ret;
}

void free_Defect_List(scsiDefectList **defects)
{
    safe_Free(*defects);
}

void print_SCSI_Defect_List(ptrSCSIDefectList defects)
{
    if (defects)
    {
        printf("===SCSI Defect List===\n");
        if (defects->containsPrimaryList)
        {
            printf("\tList includes primary defects\n");
        }
        if (defects->containsGrownList)
        {
            printf("\tList includes grown defects\n");
        }
        //TODO: Add a way to handle getting per-head counts to output first

        switch (defects->format)
        {
        default:
            printf("Error: Unknown defect list format. Cannot be displayed!\n");
            break;
        }
    }
}