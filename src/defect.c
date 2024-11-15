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
// \file defect.h
// \brief This file defines the functions for creating and reading defect information

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
#include "prng.h"

#include "defect.h"
#include "smart.h"
#include "logs.h"
#include "dst.h"

eReturnValues get_SCSI_Defect_List(tDevice *device, eSCSIAddressDescriptors defectListFormat, bool grownList, bool primaryList, scsiDefectList **defects)
{
    eReturnValues ret = SUCCESS;
    if (defects)
    {
        bool tenByte = false;
        bool gotDefectData = false;
        bool listHasPrimaryDescriptors = false;
        bool listHasGrownDescriptors = false;
        uint16_t generationCode = 0;
        uint8_t returnedDefectListFormat = UINT8_MAX;
        uint32_t dataLength = 8;
        uint8_t *defectData = C_CAST(uint8_t*, safe_calloc_aligned(dataLength, sizeof(uint8_t), device->os_info.minimumAlignment));
        uint32_t defectListLength = 0;
        if (device->drive_info.scsiVersion > SCSI_VERSION_SCSI2 && (ret = scsi_Read_Defect_Data_12(device, primaryList, grownList, C_CAST(uint8_t, defectListFormat), 0, dataLength, defectData)) == SUCCESS)
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
            ret = scsi_Read_Defect_Data_10(device, primaryList, grownList, C_CAST(uint8_t, defectListFormat), C_CAST(uint16_t, dataLength), defectData);
            if (ret == SUCCESS)
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
                        uint8_t *temp = C_CAST(uint8_t*, safe_realloc_aligned(defectData, 0, dataLength, device->os_info.minimumAlignment));
                        if (temp)
                        {
                            defectData = temp;
                            memset(defectData, 0, dataLength);
                            if (SUCCESS == (ret = scsi_Read_Defect_Data_10(device, primaryList, grownList, C_CAST(uint8_t, defectListFormat), C_CAST(uint16_t, dataLength), defectData)))
                            {
                                uint32_t offset = 4;
                                defectListLength = M_BytesTo2ByteValue(defectData[2], defectData[3]);
                                listHasPrimaryDescriptors = defectData[1] & BIT4;
                                listHasGrownDescriptors = defectData[1] & BIT3;
                                returnedDefectListFormat = M_GETBITRANGE(defectData[1], 2, 0);
                                //now allocate our list to return to the caller!
                                *defects = C_CAST(ptrSCSIDefectList, safe_malloc(sizeof(scsiDefectList) + defectAlloc));
                                if (*defects)
                                {
                                    ptrSCSIDefectList ptrDefects = *defects;
                                    memset(ptrDefects, 0, sizeof(scsiDefectList) + defectAlloc);
                                    ptrDefects->numberOfElements = numberOfElements;
                                    ptrDefects->containsGrownList = listHasGrownDescriptors;
                                    ptrDefects->containsPrimaryList = listHasPrimaryDescriptors;
                                    ptrDefects->format = returnedDefectListFormat;
                                    ptrDefects->deviceHasMultipleLogicalUnits = M_ToBool(device->drive_info.numberOfLUs);
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
                                    for (uint32_t elementNumber = 0; elementNumber < numberOfElements && offset < defectListLength + 4; ++elementNumber, offset += increment)
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
                                            break;
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
                        DECLARE_ZERO_INIT_ARRAY(uint8_t, reportOPCodeSupport, 16);
                        if (SUCCESS == scsi_Report_Supported_Operation_Codes(device, false, REPORT_OPERATION_CODE, READ_DEFECT_DATA_12_CMD, 0, 16, reportOPCodeSupport))
                        {
                            //skipping the support data since we shouldn't be this far if the command isn't supported!
                            uint32_t addressIndexSupport = M_BytesTo4ByteValue(reportOPCodeSupport[6], reportOPCodeSupport[7], reportOPCodeSupport[8], reportOPCodeSupport[9]);
                            if (addressIndexSupport > 0)
                            {
                                multipleCommandsSupported = true;
                                //before we say this works, we need to do one more test due to some drive firmware bugs
                                //try setting the offset to the end of the element list and see what the reported defect length is. If it is zero, then this is supported, otherwise it's a firmware bug reporting incorrectly
                                if (SUCCESS == scsi_Read_Defect_Data_12(device, primaryList, grownList, C_CAST(uint8_t, defectListFormat), numberOfElements + 1, dataLength, defectData))
                                {
                                    //If this reported length is less than the saved list length we already have, then this is responding to the index properly, and is therefore supported.
                                    if (M_BytesTo4ByteValue(defectData[4], defectData[5], defectData[6], defectData[7]) < defectListLength)
                                    {
                                        multipleCommandsSupported = true;
                                    }
                                    else
                                    {
                                        multipleCommandsSupported = false;
                                    }
                                }
                            }
                        }
                        if (multipleCommandsSupported)
                        {
                            //read the list in multiple commands! Do this in 64k chunks.
                            dataLength = 65536;//this is 64k
                            uint8_t *temp = C_CAST(uint8_t*, safe_realloc_aligned(defectData, 0, dataLength, device->os_info.minimumAlignment));
                            if (temp)
                            {
                                defectData = temp;
                                memset(defectData, 0, dataLength);
                                *defects = C_CAST(ptrSCSIDefectList, safe_malloc(sizeof(scsiDefectList) + defectAlloc));
                                if (*defects)
                                {
                                    uint32_t elementNumber = 0;
                                    uint32_t offset = 0;
                                    uint32_t increment = 0;
                                    ptrSCSIDefectList ptrDefects = *defects;
                                    bool filledInListInfo = false;
                                    memset(ptrDefects, 0, sizeof(scsiDefectList) + defectAlloc);
                                    ptrDefects->numberOfElements = numberOfElements;
                                    ptrDefects->deviceHasMultipleLogicalUnits = M_ToBool(device->drive_info.numberOfLUs);
                                    while (elementNumber < numberOfElements)
                                    {
                                        offset = 8;//reset the offset to 8 each time through the while loop since we will start reading the list over and over after each command
                                        memset(defectData, 0, dataLength);
                                        if (SUCCESS == (ret = scsi_Read_Defect_Data_12(device, primaryList, grownList, C_CAST(uint8_t, defectListFormat), elementNumber * increment, dataLength, defectData)))
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
                                                for (; elementNumber < numberOfElements && offset < defectListLength && offset < (dataLength + 8); ++elementNumber, offset += increment)
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
                                                        break;
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
                            uint8_t *temp = C_CAST(uint8_t*, safe_realloc_aligned(defectData, 0, dataLength, device->os_info.minimumAlignment));
                            if (temp)
                            {
                                defectData = temp;
                                memset(defectData, 0, dataLength);
                                if (SUCCESS == (ret = scsi_Read_Defect_Data_12(device, primaryList, grownList, C_CAST(uint8_t, defectListFormat), 0, dataLength, defectData)))
                                {
                                    uint32_t offset = 8;
                                    defectListLength = M_BytesTo4ByteValue(defectData[4], defectData[5], defectData[6], defectData[7]);
                                    listHasPrimaryDescriptors = defectData[1] & BIT4;
                                    listHasGrownDescriptors = defectData[1] & BIT3;
                                    returnedDefectListFormat = M_GETBITRANGE(defectData[1], 2, 0);
                                    generationCode = M_BytesTo2ByteValue(defectData[2], defectData[3]);
                                    //now allocate our list to return to the caller!
                                    *defects = C_CAST(ptrSCSIDefectList, safe_malloc(sizeof(scsiDefectList) + defectAlloc));
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
                                        for (uint32_t elementNumber = 0; elementNumber < numberOfElements && offset < (defectListLength + 8); ++elementNumber, offset += increment)
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
                                                break;
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
                *defects = C_CAST(ptrSCSIDefectList, safe_calloc(1, sizeof(scsiDefectList)));
                if (*defects)
                {
                    ptrSCSIDefectList temp = *defects;
                    temp->numberOfElements = 0;
                    temp->containsGrownList = listHasGrownDescriptors;
                    temp->containsPrimaryList = listHasPrimaryDescriptors;
                    temp->generation = generationCode;
                    temp->format = returnedDefectListFormat;
                    temp->deviceHasMultipleLogicalUnits = M_ToBool(device->drive_info.numberOfLUs);
                    ret = SUCCESS;
                }
                else
                {
                    ret = MEMORY_FAILURE;
                }
            }
        }
        safe_free_aligned(&defectData);
    }
    else
    {
        ret = BAD_PARAMETER;
    }
    return ret;
}

void free_Defect_List(scsiDefectList **defects)
{
    safe_Free(M_REINTERPRET_CAST(void**, defects));
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
        if (defects->generation > 0)
        {
            printf("\tGeneration Code: %" PRIu16 "\n", defects->generation);
        }
        if (defects->deviceHasMultipleLogicalUnits)
        {
            printf("\tNOTE: At this time, reported defects are for the entire device, not a single logical unit\n");
        }
        //TODO: Add a way to handle getting per-head counts to output first
        bool multiBit = false;
        switch (defects->format)
        {
        case AD_SHORT_BLOCK_FORMAT_ADDRESS_DESCRIPTOR:
            printf("---Short Block Format---\n");
            if (defects->numberOfElements > 0)
            {
                printf("Total Defects in list: %" PRIu32 "\n", defects->numberOfElements);
                for (uint64_t iter = 0; iter < defects->numberOfElements; ++iter)
                {
                    printf("%" PRIu32 "\n", defects->block[iter].shortBlockAddress);
                }
            }
            else
            {
                printf("No Defects Found\n");
            }
            break;
        case AD_LONG_BLOCK_FORMAT_ADDRESS_DESCRIPTOR:
            printf("---Long Block Format---\n");
            if (defects->numberOfElements > 0)
            {
                printf("Total Defects in list: %" PRIu32 "\n", defects->numberOfElements);
                for (uint64_t iter = 0; iter < defects->numberOfElements; ++iter)
                {
                    printf("%" PRIu64 "\n", defects->block[iter].longBlockAddress);
                }
            }
            else
            {
                printf("No Defects Found\n");
            }
            break;
        case AD_EXTENDED_PHYSICAL_SECTOR_FORMAT_ADDRESS_DESCRIPTOR:
            printf("---Extended Physical Sector Format---\n");
            if (defects->numberOfElements > 0)
            {
                printf("Total Defects in list: %" PRIu32 "\n", defects->numberOfElements);
                printf("  %-8s  %-3s  %10s \n", "Cylinder", "Head", "Sector");
                for (uint64_t iter = 0; iter < defects->numberOfElements; ++iter)
                {
                    char multi = ' ';
                    bool switchMultiOff = false;
                    if (defects->physical[iter].multiAddressDescriptorStart)
                    {
                        multiBit = true;
                        multi = '+';
                        printf("------------------------------\n");
                    }
                    else if (multiBit)
                    {
                        //multi was on, but now off...this is the last descriptor for the same error
                        multi = '+';
                        switchMultiOff = true;
                    }
                    if (defects->physical[iter].sectorNumber == MAX_28BIT)
                    {
                        printf("%c %8" PRIu32 "  %3" PRIu8 "  %10s\n", multi, defects->physical[iter].cylinderNumber, defects->physical[iter].headNumber, "Full Track");
                    }
                    else
                    {
                        printf("%c %8" PRIu32 "  %3" PRIu8 "  %10" PRIu32 " \n", multi, defects->physical[iter].cylinderNumber, defects->physical[iter].headNumber, defects->physical[iter].sectorNumber);
                    }
                    if (switchMultiOff)
                    {
                        multiBit = false;
                        printf("------------------------------\n");
                    }
                }
            }
            else
            {
                printf("No Defects Found\n");
            }
            break;
        case AD_PHYSICAL_SECTOR_FORMAT_ADDRESS_DESCRIPTOR:
            printf("---Physical Sector Format---\n");
            if (defects->numberOfElements > 0)
            {
                printf("Total Defects in list: %" PRIu32 "\n", defects->numberOfElements);
                printf("  %-8s  %-3s  %10s \n", "Cylinder", "Head", "Sector");
                for (uint64_t iter = 0; iter < defects->numberOfElements; ++iter)
                {
                    if (defects->physical[iter].sectorNumber == UINT32_MAX)
                    {
                        printf("  %8" PRIu32 "  %3" PRIu8 "  %10s\n", defects->physical[iter].cylinderNumber, defects->physical[iter].headNumber, "Full Track");
                    }
                    else
                    {
                        printf("  %8" PRIu32 "  %3" PRIu8 "  %10" PRIu32 "\n", defects->physical[iter].cylinderNumber, defects->physical[iter].headNumber, defects->physical[iter].sectorNumber);
                    }
                }
            }
            else
            {
                printf("No Defects Found\n");
            }
            break;
        case AD_EXTENDED_BYTES_FROM_INDEX_FORMAT_ADDRESS_DESCRIPTOR:
            printf("---Extended Bytes From Index Format---\n");
            if (defects->numberOfElements > 0)
            {
                printf("Total Defects in list: %" PRIu32 "\n", defects->numberOfElements);
                printf("  %-8s  %-3s  %16s \n", "Cylinder", "Head", "Bytes From Index");
                for (uint64_t iter = 0; iter < defects->numberOfElements; ++iter)
                {
                    char multi = ' ';
                    bool switchMultiOff = false;
                    if (defects->bfi[iter].multiAddressDescriptorStart)
                    {
                        multiBit = true;
                        multi = '+';
                    }
                    else if (multiBit)
                    {
                        //multi was on, but now off...this is the last descriptor for the same error
                        multi = '+';
                        switchMultiOff = true;
                    }
                    if (defects->bfi[iter].bytesFromIndex == MAX_28BIT)
                    {
                        printf("%c %8" PRIu32 "  %3" PRIu8 "  %10s\n", multi, defects->bfi[iter].cylinderNumber, defects->bfi[iter].headNumber, "Full Track");
                    }
                    else
                    {
                        printf("%c %8" PRIu32 "  %3" PRIu8 "  %10" PRIu32 " \n", multi, defects->bfi[iter].cylinderNumber, defects->bfi[iter].headNumber, defects->bfi[iter].bytesFromIndex);
                    }
                    if (switchMultiOff)
                    {
                        multiBit = false;
                        printf("------------------------------\n");
                    }
                }
            }
            else
            {
                printf("No Defects Found\n");
            }
            break;
        case AD_BYTES_FROM_INDEX_FORMAT_ADDRESS_DESCRIPTOR:
            printf("---Bytes From Index Format---\n");
            if (defects->numberOfElements > 0)
            {
                printf("Total Defects in list: %" PRIu32 "\n", defects->numberOfElements);
                printf("  %-8s  %-3s  %16s \n", "Cylinder", "Head", "Bytes From Index");
                for (uint64_t iter = 0; iter < defects->numberOfElements; ++iter)
                {
                    if (defects->bfi[iter].bytesFromIndex == UINT32_MAX)
                    {
                        printf("  %8" PRIu32 "  %3" PRIu8 "  %16s\n", defects->bfi[iter].cylinderNumber, defects->bfi[iter].headNumber, "Full Track");
                    }
                    else
                    {
                        printf("  %8" PRIu32 "  %3" PRIu8 "  %16" PRIu32 "\n", defects->bfi[iter].cylinderNumber, defects->bfi[iter].headNumber, defects->bfi[iter].bytesFromIndex);
                    }
                }
            }
            else
            {
                printf("No Defects Found\n");
            }
            break;
        default:
            printf("Error: Unknown defect list format. Cannot be displayed!\n");
            break;
        }
    }
}

eReturnValues create_Random_Uncorrectables(tDevice *device, uint16_t numberOfRandomLBAs, bool readUncorrectables, bool flaggedErrors, custom_Update updateFunction, void *updateData)
{
    eReturnValues ret = SUCCESS;
    uint16_t iterator = 0;
    seed_64(C_CAST(uint64_t, time(M_NULLPTR)));//start the random number generator
    for (iterator = 0; iterator < numberOfRandomLBAs; ++iterator)
    {
        uint64_t randomLBA = random_Range_64(0, device->drive_info.deviceMaxLba);
        //align the random LBA to the physical sector
        randomLBA = align_LBA(device, randomLBA);
        //call the function to create an uncorrectable with the range set to 1 so we only corrupt 1 physical block at a time randomly
        if (flaggedErrors)
        {
            ret = flag_Uncorrectables(device, randomLBA, 1, updateFunction, updateData);
        }
        else
        {
            ret = create_Uncorrectables(device, randomLBA, 1, readUncorrectables, updateFunction, updateData);
        }
        if (ret != SUCCESS)
        {
            break;
        }
    }
    return ret;
}

eReturnValues create_Uncorrectables(tDevice *device, uint64_t startingLBA, uint64_t range, bool readUncorrectables, M_ATTR_UNUSED custom_Update updateFunction, M_ATTR_UNUSED void *updateData)
{
    eReturnValues ret = SUCCESS;
    uint64_t iterator = 0;
    bool wue = is_Write_Psuedo_Uncorrectable_Supported(device);
    bool readWriteLong = is_Read_Long_Write_Long_Supported(device);
    uint16_t logicalPerPhysicalSectors = C_CAST(uint16_t, device->drive_info.devicePhyBlockSize / device->drive_info.deviceBlockSize);
    uint16_t increment = logicalPerPhysicalSectors;
    if (!wue && readWriteLong && logicalPerPhysicalSectors != 1 && device->drive_info.drive_type == ATA_DRIVE)
    {
        //changing the increment amount to 1 because the ATA read/write long commands can only do a single LBA at a time.
        increment = 1;
    }
    startingLBA = align_LBA(device, startingLBA);
    for (iterator = startingLBA; iterator < (startingLBA + range); iterator += increment)
    {
        if (device->deviceVerbosity > VERBOSITY_QUIET)
        {
            printf("Creating Uncorrectable error at LBA %-20" PRIu64 "\n", iterator);
        }
        if (wue)
        {
            ret = write_Psuedo_Uncorrectable_Error(device, iterator);
        }
        else if (readWriteLong)
        {
            ret = corrupt_LBA_Read_Write_Long(device, iterator, UINT16_MAX);//saying to corrupt all the data bytes to make sure we do get an error.
        }
        else
        {
            ret = NOT_SUPPORTED;
        }
        if (ret != SUCCESS)
        {
            break;
        }
        if (readUncorrectables)
        {
            size_t dataBufSize = C_CAST(size_t, device->drive_info.deviceBlockSize) * C_CAST(size_t, logicalPerPhysicalSectors);
            uint8_t *dataBuf = C_CAST(uint8_t*, safe_calloc_aligned(dataBufSize, sizeof(uint8_t), device->os_info.minimumAlignment));
            if (!dataBuf)
            {
                return MEMORY_FAILURE;
            }
            //don't check return status since we expect this to fail after creating the error
            if (device->deviceVerbosity > VERBOSITY_QUIET)
            {
                printf("Reading Uncorrectable error at LBA %-20"PRIu64"\n", iterator);
            }
            read_LBA(device, iterator, false, dataBuf, logicalPerPhysicalSectors * device->drive_info.deviceBlockSize);
            //scsi_Read_16(device, 0, false, false, false, iterator, 0, logicalPerPhysicalSectors, dataBuf);
            safe_free_aligned(&dataBuf);
        }
    }
    return ret;
}

eReturnValues flag_Uncorrectables(tDevice *device, uint64_t startingLBA, uint64_t range, M_ATTR_UNUSED custom_Update updateFunction, M_ATTR_UNUSED void *updateData)
{
    eReturnValues ret = SUCCESS;
    uint64_t iterator = 0;
    if (is_Write_Flagged_Uncorrectable_Supported(device))
    {
        //uint16_t logicalPerPhysicalSectors = device->drive_info.devicePhyBlockSize / device->drive_info.deviceBlockSize;
        //This function will only flag individual logical sectors since flagging works differently than pseudo uncorrectables, which we write the full sector with since a psuedo uncorrectable will always affect the full physical sector.
        startingLBA = align_LBA(device, startingLBA);
        for (iterator = startingLBA; iterator < (startingLBA + range); iterator += 1)
        {
            if (device->deviceVerbosity > VERBOSITY_QUIET)
            {
                printf("Flagging Uncorrectable error at LBA %-20" PRIu64 "\n", iterator);
            }
            ret = write_Flagged_Uncorrectable_Error(device, iterator);
            if (ret != SUCCESS)
            {
                break;
            }
        }
    }
    else
    {
        ret = NOT_SUPPORTED;
    }
    return ret;
}

bool is_Read_Long_Write_Long_Supported(tDevice *device)
{
    bool supported = false;
    if (device->drive_info.drive_type == ATA_DRIVE)
    {
        if (is_ATA_Identify_Word_Valid(device->drive_info.IdentifyData.ata.Word206) && device->drive_info.IdentifyData.ata.Word206 & BIT1)
        {
            supported = true;
        }
        /*a value of zero may be valid on really old drives which otherwise accept this command, but this should be ok for now*/
        else if (is_ATA_Identify_Word_Valid(device->drive_info.IdentifyData.ata.Word022))//legacy support check only!
        {
            supported = true;
        }
    }
    else if (device->drive_info.drive_type == SCSI_DRIVE)
    {
        bool reportSuccess = false;
        //Trying to use report supported operation codes/inquiry cmdDT first on the read long command. 
        //Using read long since it was removed in latest specs, so if it is not supported, then we know write long won't work unless using the write uncorrectable bit.
        DECLARE_ZERO_INIT_ARRAY(uint8_t, commandSupportInformation, 30);//should be more than big enough
        uint8_t operationCode = READ_LONG_10;
        uint32_t dataLength = 10;
        if (device->drive_info.deviceMaxLba > UINT32_MAX)
        {
            operationCode = READ_LONG_16;
            dataLength = 16;
        }
        if (device->drive_info.scsiVersion >= SCSI_VERSION_SPC_3)
        {
            dataLength += 4;
        }
        else if (device->drive_info.scsiVersion >= SCSI_VERSION_SPC && device->drive_info.scsiVersion < SCSI_VERSION_SPC_3)
        {
            dataLength += 6;
        }
        if (device->drive_info.scsiVersion >= SCSI_VERSION_SPC_3 && !device->drive_info.passThroughHacks.scsiHacks.noReportSupportedOperations && SUCCESS == scsi_Report_Supported_Operation_Codes(device, false, REPORT_OPERATION_CODE, operationCode, 0, dataLength, commandSupportInformation))
        {
            reportSuccess = true;
            switch (commandSupportInformation[1] & 0x07)
            {
            case 0: //not available right now...so not supported
            case 1://not supported
                break;
            case 3://supported according to spec
            case 5://supported in vendor specific mannor in same format as case 3
                supported = true;
                break;
            default:
                break;
            }
        }
        else if (device->drive_info.scsiVersion >= SCSI_VERSION_SPC && device->drive_info.scsiVersion < SCSI_VERSION_SPC_3 && SUCCESS == scsi_Inquiry(device, commandSupportInformation, dataLength, operationCode, false, true))
        {
            reportSuccess = true;
            switch (commandSupportInformation[1] & 0x07)
            {
            case 0: //not available right now...so not supported
            case 1://not supported
                break;
            case 3://supported according to spec
            case 5://supported in vendor specific mannor in same format as case 3
                supported = true;
                break;
            default:
                break;
            }
        }
        if (!reportSuccess && !supported)
        {
            //try issuing a read long command with no data transfer and see if it's treated as an error or not.
            if (device->drive_info.deviceMaxLba > UINT32_MAX)
            {
                if (SUCCESS == scsi_Read_Long_16(device, false, false, 0, 0, M_NULLPTR))
                {
                    supported = true;
                }
            }
            else
            {
                if (SUCCESS == scsi_Read_Long_10(device, false, false, 0, 0, M_NULLPTR))
                {
                    supported = true;
                }
            }
        }
    }
    return supported;
}

eReturnValues corrupt_LBA_Read_Write_Long(tDevice *device, uint64_t corruptLBA, uint16_t numberOfBytesToCorrupt)
{
    eReturnValues ret = NOT_SUPPORTED;
    bool multipleLogicalPerPhysical = false;//used to set the physical block bit when applicable
    uint16_t logicalPerPhysicalBlocks = C_CAST(uint16_t, (device->drive_info.devicePhyBlockSize / device->drive_info.deviceBlockSize));
    if (logicalPerPhysicalBlocks > 1)
    {
        //since this device has multiple logical blocks per physical block, we also need to adjust the LBA to be at the start of the physical block
        //do this by dividing by the number of logical sectors per physical sector. This integer division will get us aligned
        uint64_t tempLBA = corruptLBA / logicalPerPhysicalBlocks;
        tempLBA *= logicalPerPhysicalBlocks;
        //do we need to adjust for alignment? We'll add it in later if I ever get a drive that has an alignment other than 0 - TJE
        corruptLBA = tempLBA;
        //set this flag for SCSI
        multipleLogicalPerPhysical = true;
    }
    if (device->drive_info.drive_type == ATA_DRIVE)
    {
        if (is_ATA_Identify_Word_Valid(device->drive_info.IdentifyData.ata.Word206) && device->drive_info.IdentifyData.ata.Word206 & BIT1)
        {
            //use SCT read & write long commands
            uint16_t numberOfECCCRCBytes = 0;
            uint16_t numberOfBlocksRequested = 0;
            uint32_t dataSize = device->drive_info.deviceBlockSize + LEGACY_DRIVE_SEC_SIZE;
            uint8_t *data = C_CAST(uint8_t*, safe_calloc_aligned(dataSize, sizeof(uint8_t), device->os_info.minimumAlignment));
            if (!data)
            {
                return MEMORY_FAILURE;
            }
            ret = send_ATA_SCT_Read_Write_Long(device, SCT_RWL_READ_LONG, corruptLBA, data, dataSize, &numberOfECCCRCBytes, &numberOfBlocksRequested);
            if (ret == SUCCESS)
            {
                //seed_64(C_CAST(uint64_t, time(M_NULLPTR)));
                //modify the user data to cause a uncorrectable error
                for (uint32_t iter = 0; iter < numberOfBytesToCorrupt && iter < device->drive_info.deviceBlockSize - 1; ++iter)
                {
                    data[iter] = M_2sCOMPLEMENT(data[iter]);// C_CAST(uint8_t, random_Range_64(0, UINT8_MAX));
                }
                if (numberOfBlocksRequested)
                {
                    //The drive responded through SAT enough to tell us exactly how many blocks are expected...so we can set the data transfer length as is expected...since this wasn't clear on non 512B logical sector drives.
                    dataSize = LEGACY_DRIVE_SEC_SIZE * numberOfBlocksRequested;
                }
                //now write back the data with a write long command
                ret = send_ATA_SCT_Read_Write_Long(device, SCT_RWL_WRITE_LONG, corruptLBA, data, dataSize, M_NULLPTR, M_NULLPTR);
            }
            safe_free_aligned(&data);
        }
        else if (is_ATA_Identify_Word_Valid(device->drive_info.IdentifyData.ata.Word022) && corruptLBA < MAX_28_BIT_LBA)/*a value of zero may be valid on really old drives which otherwise accept this command, but this should be ok for now*/
        {
            bool setFeaturesToChangeECCBytes = false;
            if (device->drive_info.IdentifyData.ata.Word022 != 4)
            {
                //need to issue a set features command to specify the number of ECC bytes before doing a read or write long (according to old Seagate ATA reference manual from the web)
                if (SUCCESS == ata_Set_Features(device, SF_LEGACY_SET_VENDOR_SPECIFIC_ECC_BYTES_FOR_READ_WRITE_LONG, M_Byte0(device->drive_info.IdentifyData.ata.Word022), 0, 0, 0))
                {
                    setFeaturesToChangeECCBytes = true;
                }
            }
            uint32_t dataSize = device->drive_info.deviceBlockSize + device->drive_info.IdentifyData.ata.Word022;
            uint8_t *data = C_CAST(uint8_t*, safe_calloc_aligned(dataSize, sizeof(uint8_t), device->os_info.minimumAlignment));
            if (!data)
            {
                return MEMORY_FAILURE;
            }
            //This drive supports the legacy 28bit read/write long commands from ATA...
            //These commands are really old and transfer weird byte based values.
            //While these transfer lengths shouldbe supported by SAT, there are some SATLs that won't handle this odd case. It may or may not go through...-TJE
            if (device->drive_info.ata_Options.chsModeOnly)
            {
                uint16_t cylinder = 0;
                uint8_t head = 0;
                uint8_t sector = 0;
                if (SUCCESS == convert_LBA_To_CHS(device, C_CAST(uint32_t, corruptLBA), &cylinder, &head, &sector))
                {
                    ret = ata_Legacy_Read_Long_CHS(device, true, cylinder, head, sector, data, dataSize);
                    if (ret == SUCCESS)
                    {
                        //seed_64(C_CAST(uint64_t, time(M_NULLPTR)));
                        //modify the user data to cause a uncorrectable error
                        for (uint32_t iter = 0; iter < numberOfBytesToCorrupt && iter < device->drive_info.deviceBlockSize - 1; ++iter)
                        {
                            data[iter] = M_2sCOMPLEMENT(data[iter]); //C_CAST(uint8_t, random_Range_64(0, UINT8_MAX));
                        }
                        ret = ata_Legacy_Write_Long_CHS(device, true, cylinder, head, sector, data, dataSize);
                    }
                }
                else //Couldn't convert or the LBA is greater than the current CHS mode
                {
                    ret = NOT_SUPPORTED;
                }
            }
            else
            {
                ret = ata_Legacy_Read_Long(device, true, C_CAST(uint32_t, corruptLBA), data, dataSize);
                if (ret == SUCCESS)
                {
                    //seed_64(C_CAST(uint64_t, time(M_NULLPTR)));
                    //modify the user data to cause a uncorrectable error
                    for (uint32_t iter = 0; iter < numberOfBytesToCorrupt && iter < device->drive_info.deviceBlockSize - 1; ++iter)
                    {
                        data[iter] = M_2sCOMPLEMENT(data[iter]); // C_CAST(uint8_t, random_Range_64(0, UINT8_MAX));
                    }
                    ret = ata_Legacy_Write_Long(device, true, C_CAST(uint32_t, corruptLBA), data, dataSize);
                }
            }
            if (setFeaturesToChangeECCBytes)
            {
                //reverting back to drive defaults again so that we don't mess anyone else up.
                if (SUCCESS == ata_Set_Features(device, SF_LEGACY_SET_4_BYTES_ECC_FOR_READ_WRITE_LONG, 0, 0, 0, 0))
                {
                    setFeaturesToChangeECCBytes = false;
                }
            }
            safe_free_aligned(&data);
        }
    }
    else if (device->drive_info.drive_type == SCSI_DRIVE)
    {
        senseDataFields senseFields;
        memset(&senseFields, 0, sizeof(senseDataFields));
        uint16_t dataLength = C_CAST(uint16_t, device->drive_info.deviceBlockSize * logicalPerPhysicalBlocks);//start with this size for now...
        uint8_t *dataBuffer = C_CAST(uint8_t*, safe_calloc_aligned(dataLength, sizeof(uint8_t), device->os_info.minimumAlignment));
        if (device->drive_info.deviceMaxLba > UINT32_MAX)
        {
            ret = scsi_Read_Long_16(device, multipleLogicalPerPhysical, true, corruptLBA, dataLength, dataBuffer);
        }
        else
        {
            ret = scsi_Read_Long_10(device, multipleLogicalPerPhysical, true, C_CAST(uint32_t, corruptLBA), dataLength, dataBuffer);
        }
        //ret should not be success and we should have an illegal length indicator set so we can reallocate and read the ecc bytes
        memset(&senseFields, 0, sizeof(senseDataFields));
        get_Sense_Data_Fields(device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, &senseFields);
        if (senseFields.illegalLengthIndication && senseFields.valid)//spec says these bit should both be zero since we didn't do this request with enough bytes to read the ECC bytes
        {
            if (senseFields.fixedFormat)
            {
                dataLength += C_CAST(uint16_t, M_2sCOMPLEMENT(senseFields.fixedInformation));//length different is a twos compliment value since we requested less than is available.
            }
            else
            {
                dataLength += C_CAST(uint16_t, M_2sCOMPLEMENT(senseFields.descriptorInformation));//length different is a twos compliment value since we requested less than is available.
            }
            uint8_t *temp = C_CAST(uint8_t*, safe_realloc_aligned(dataBuffer, 0, dataLength, device->os_info.minimumAlignment));
            if (temp)
            {
                dataBuffer = temp;
                memset(dataBuffer, 0, dataLength);
                if (device->drive_info.deviceMaxLba > UINT32_MAX)
                {
                    ret = scsi_Read_Long_16(device, multipleLogicalPerPhysical, true, corruptLBA, dataLength, dataBuffer);
                }
                else
                {
                    ret = scsi_Read_Long_10(device, multipleLogicalPerPhysical, true, C_CAST(uint32_t, corruptLBA), dataLength, dataBuffer);
                }
                if (ret != SUCCESS)
                {
                    ret = FAILURE;
                }
                else
                {
                    //seed_64(C_CAST(uint64_t, time(M_NULLPTR)));
                    //modify the user data to cause a uncorrectable error
                    for (uint32_t iter = 0; iter < numberOfBytesToCorrupt && iter < (device->drive_info.deviceBlockSize * logicalPerPhysicalBlocks - 1); ++iter)
                    {
                        //Originally using random values, but it was recommended to do 2's compliment of the original data instead.
                        dataBuffer[iter] = M_2sCOMPLEMENT(dataBuffer[iter]); //C_CAST(uint8_t, random_Range_64(0, UINT8_MAX));
                    }
                    //write it back to the drive
                    if (device->drive_info.deviceMaxLba > UINT32_MAX)
                    {
                        ret = scsi_Write_Long_16(device, false, false, multipleLogicalPerPhysical, corruptLBA, dataLength, dataBuffer);
                    }
                    else
                    {
                        ret = scsi_Write_Long_10(device, false, false, multipleLogicalPerPhysical, C_CAST(uint32_t, corruptLBA), dataLength, dataBuffer);
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
            ret = NOT_SUPPORTED;
        }
        safe_free_aligned(&dataBuffer);
    }
    return ret;
}

eReturnValues corrupt_LBAs(tDevice *device, uint64_t startingLBA, uint64_t range, bool readCorruptedLBAs, uint16_t numberOfBytesToCorrupt, M_ATTR_UNUSED custom_Update updateFunction, M_ATTR_UNUSED void *updateData)
{
    eReturnValues ret = SUCCESS;
    uint64_t iterator = 0;
    bool readWriteLong = is_Read_Long_Write_Long_Supported(device);
    uint16_t logicalPerPhysicalSectors = C_CAST(uint16_t, device->drive_info.devicePhyBlockSize / device->drive_info.deviceBlockSize);
    uint16_t increment = logicalPerPhysicalSectors;
    if (readWriteLong && logicalPerPhysicalSectors != 1 && device->drive_info.drive_type == ATA_DRIVE)
    {
        //changing the increment amount to 1 because the ATA read/write long commands can only do a single LBA at a time.
        increment = 1;
    }
    startingLBA = align_LBA(device, startingLBA);
    for (iterator = startingLBA; iterator < (startingLBA + range); iterator += increment)
    {
        if (device->deviceVerbosity > VERBOSITY_QUIET)
        {
            printf("Creating Uncorrectable error at LBA %-20" PRIu64 "\n", iterator);
        }
        if (readWriteLong)
        {
            ret = corrupt_LBA_Read_Write_Long(device, iterator, numberOfBytesToCorrupt);
        }
        else
        {
            ret = NOT_SUPPORTED;
        }
        if (ret != SUCCESS)
        {
            break;
        }
        if (readCorruptedLBAs)
        {
            size_t dataBufSize = C_CAST(size_t, device->drive_info.deviceBlockSize) * C_CAST(size_t, logicalPerPhysicalSectors);
            uint8_t *dataBuf = C_CAST(uint8_t*, safe_calloc_aligned(dataBufSize, sizeof(uint8_t), device->os_info.minimumAlignment));
            if (!dataBuf)
            {
                return MEMORY_FAILURE;
            }
            //don't check return status since we expect this to fail after creating the error
            if (device->deviceVerbosity > VERBOSITY_QUIET)
            {
                printf("Reading Corrupted LBA %-20" PRIu64 "\n", iterator);
            }
            read_LBA(device, iterator, false, dataBuf, logicalPerPhysicalSectors * device->drive_info.deviceBlockSize);
            //scsi_Read_16(device, 0, false, false, false, iterator, 0, logicalPerPhysicalSectors, dataBuf);
            safe_free_aligned(&dataBuf);
        }
    }
    return ret;
}

eReturnValues corrupt_Random_LBAs(tDevice *device, uint16_t numberOfRandomLBAs, bool readCorruptedLBAs, uint16_t numberOfBytesToCorrupt, custom_Update updateFunction, void *updateData)
{
    eReturnValues ret = SUCCESS;
    uint16_t iterator = 0;
    seed_64(C_CAST(uint64_t, time(M_NULLPTR)));//start the random number generator
    for (iterator = 0; iterator < numberOfRandomLBAs; ++iterator)
    {
        uint64_t randomLBA = random_Range_64(0, device->drive_info.deviceMaxLba);
        //align the random LBA to the physical sector
        randomLBA = align_LBA(device, randomLBA);
        //call the function to create an uncorrectable with the range set to 1 so we only corrupt 1 physical block at a time randomly
        ret = corrupt_LBAs(device, randomLBA, 1, readCorruptedLBAs, numberOfBytesToCorrupt, updateFunction, updateData);
        if (ret != SUCCESS)
        {
            break;
        }
    }
    return ret;
}

eReturnValues get_LBAs_From_SCSI_Pending_List(tDevice* device, ptrPendingDefect defectList, uint32_t* numberOfDefects)
{
    eReturnValues ret = NOT_SUPPORTED;
    if (!defectList || !numberOfDefects)
    {
        return BAD_PARAMETER;
    }
    *numberOfDefects = 0;//set to zero since it will be incremented as we read in the bad LBAs
    uint32_t totalPendingReported = 0;
    bool validPendingReportedCount = false;
    if (SUCCESS == get_Pending_List_Count(device, &totalPendingReported))//This is useful so we know whether or not to bother reading the log
    {
        validPendingReportedCount = true;
        ret = SUCCESS;//change this to SUCCESS since we know we will get a valid count & list before we read it
    }
    if (totalPendingReported > 0 && validPendingReportedCount)
    {
        uint32_t pendingLogSize = 0;
        get_SCSI_Log_Size(device, LP_PENDING_DEFECTS, 0x01, &pendingLogSize);
        if (pendingLogSize > 0)
        {
            uint8_t* pendingDefectsLog = C_CAST(uint8_t*, safe_calloc_aligned(pendingLogSize, sizeof(uint8_t), device->os_info.minimumAlignment));
            if (!pendingDefectsLog)
            {
                return MEMORY_FAILURE;
            }
            if (SUCCESS == get_SCSI_Log(device, LP_PENDING_DEFECTS, 0x01, M_NULLPTR, M_NULLPTR, true, pendingDefectsLog, pendingLogSize, M_NULLPTR))
            {
                //First, validate that we got the right SCSI log page...I've seen some USB devices ignore the subpage code and return the wrong data. - TJE
                if (M_GETBITRANGE(pendingDefectsLog[0], 5, 0) == 0x15 && pendingDefectsLog[0] & BIT6 && pendingDefectsLog[1] == 0x01)
                {
                    uint16_t pageLength = M_BytesTo2ByteValue(pendingDefectsLog[2], pendingDefectsLog[3]);//does not include 4 byte header!
                    if (pageLength > 4)
                    {
                        uint32_t pendingDefectCount = 1;//will be set in loop shortly...but use this for now to enter the loop
                        uint8_t parameterLength = 0;
                        uint32_t offset = LOG_PAGE_HEADER_LENGTH;//setting to this so we start with the first parameter we are given...which should be zero
                        for (uint32_t defectCounter = 0; offset < C_CAST(uint32_t, C_CAST(uint32_t, pageLength) + LOG_PAGE_HEADER_LENGTH) && defectCounter < pendingDefectCount; offset += (parameterLength + 4))
                        {
                            uint16_t parameterCode = M_BytesTo2ByteValue(pendingDefectsLog[offset + 0], pendingDefectsLog[offset + 1]);
                            parameterLength = pendingDefectsLog[offset + 3];//does not include 4 byte header. The increment in the loop takes this into account
                            if (parameterCode == 0)
                            {
                                //this is the total count in the log
                                pendingDefectCount = M_BytesTo4ByteValue(pendingDefectsLog[offset + 4], pendingDefectsLog[offset + 5], pendingDefectsLog[offset + 6], pendingDefectsLog[offset + 7]);
                            }
                            else if (parameterCode >= 0x0001 && parameterCode <= 0xF000)
                            {
                                //this is a pending defect entry
                                defectList[defectCounter].powerOnHours = M_BytesTo4ByteValue(pendingDefectsLog[offset + 4], pendingDefectsLog[offset + 5], pendingDefectsLog[offset + 6], pendingDefectsLog[offset + 7]);
                                defectList[defectCounter].lba = M_BytesTo8ByteValue(pendingDefectsLog[offset + 8], pendingDefectsLog[offset + 9], pendingDefectsLog[offset + 10], pendingDefectsLog[offset + 11], pendingDefectsLog[offset + 12], pendingDefectsLog[offset + 13], pendingDefectsLog[offset + 14], pendingDefectsLog[offset + 15]);
                                ++defectCounter;
                                ++(*numberOfDefects);
                            }
                            else
                            {
                                //all other parameters are reserved, so exit
                                break;
                            }
                        }
                        ret = SUCCESS;
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
            safe_free_aligned(&pendingDefectsLog);
        }
    }
    return ret;
}

eReturnValues get_LBAs_From_ATA_Pending_List(tDevice* device, ptrPendingDefect defectList, uint32_t* numberOfDefects)
{
    eReturnValues ret = NOT_SUPPORTED;
    if (!defectList || !numberOfDefects)
    {
        return BAD_PARAMETER;
    }
    *numberOfDefects = 0;//set to zero since it will be incremented as we read in the bad LBAs
    uint32_t totalPendingReported = 0;
    bool validPendingReportedCount = false;
    if (SUCCESS == get_Pending_List_Count(device, &totalPendingReported))//This is useful so we know whether or not to bother reading the log
    {
        validPendingReportedCount = true;
        ret = SUCCESS;//change this to SUCCESS since we know we will get a valid count & list before we read it
    }
    if ((totalPendingReported > 0 && validPendingReportedCount) || !validPendingReportedCount)//if we got a valid count and a number greater than zero, we read the list. If we didn't get a valid count, then we still read the list...this may be an optimization in some cases
    {
        //Check if the ACS pending log is supported and use that INSTEAD of the Seagate log...always use std spec when we can
        uint32_t pendingLogSize = 0;
        get_ATA_Log_Size(device, ATA_LOG_PENDING_DEFECTS_LOG, &pendingLogSize, true, false);
        if (pendingLogSize > 0)
        {
            //ACS Pending List
            uint8_t* pendingList = C_CAST(uint8_t*, safe_calloc_aligned(pendingLogSize, sizeof(uint8_t), device->os_info.minimumAlignment));
            if (!pendingList)
            {
                return MEMORY_FAILURE;
            }
            if (SUCCESS == get_ATA_Log(device, ATA_LOG_PENDING_DEFECTS_LOG, M_NULLPTR, M_NULLPTR, true, false, true, pendingList, pendingLogSize, M_NULLPTR, 0, 0))
            {
                uint32_t numberOfDescriptors = M_BytesTo4ByteValue(pendingList[3], pendingList[2], pendingList[1], pendingList[0]);
                for (uint32_t descriptorIter = 0, offset = 16; descriptorIter < numberOfDescriptors && offset < pendingLogSize; ++descriptorIter, offset += 16, ++(*numberOfDefects))
                {
                    defectList[*numberOfDefects].powerOnHours = M_BytesTo4ByteValue(pendingList[3 + offset], pendingList[2 + offset], pendingList[1 + offset], pendingList[0 + offset]);
                    defectList[*numberOfDefects].lba = M_BytesTo8ByteValue(pendingList[15 + offset], pendingList[14 + offset], pendingList[13 + offset], pendingList[12 + offset], pendingList[11 + offset], pendingList[10 + offset], pendingList[9 + offset], pendingList[8 + offset]);
                }
                ret = SUCCESS;
            }
            else
            {
                ret = FAILURE;
            }
            safe_free_aligned(&pendingList);
        }
    }
    return ret;
}


eReturnValues get_LBAs_From_Pending_List(tDevice* device, ptrPendingDefect defectList, uint32_t* numberOfDefects)
{
    if (device->drive_info.drive_type == ATA_DRIVE)
    {
        return get_LBAs_From_ATA_Pending_List(device, defectList, numberOfDefects);
    }
    else if (device->drive_info.drive_type == SCSI_DRIVE)
    {
        return get_LBAs_From_SCSI_Pending_List(device, defectList, numberOfDefects);
    }
    return NOT_SUPPORTED;
}

void show_Pending_List(ptrPendingDefect pendingList, uint32_t numberOfItemsInPendingList)
{
    printf("Pending Defects:\n");
    printf("================\n");
    if (numberOfItemsInPendingList > 0)
    {
        printf(" #\tLBA\t\t\tTimestamp\n");
        for (uint32_t pendingListIter = 0; pendingListIter < numberOfItemsInPendingList; ++pendingListIter)
        {
            printf("%" PRIu32 "\t%" PRIu64 "\t\t\t%" PRIu32 "\n", pendingListIter, pendingList[pendingListIter].lba, pendingList[pendingListIter].powerOnHours);
        }
    }
    else
    {
        printf("No items in pending defect list.\n");
    }
}

eReturnValues get_SCSI_Background_Scan_Results(tDevice* device, ptrBackgroundResults results, uint16_t* numberOfResults)
{
    eReturnValues ret = NOT_SUPPORTED;
    if (!results || !numberOfResults)
    {
        return BAD_PARAMETER;
    }
    *numberOfResults = 0;
    uint32_t backgroundScanResultsLength = 0;
    if (SUCCESS == get_SCSI_Log_Size(device, LP_BACKGROUND_SCAN_RESULTS, 0, &backgroundScanResultsLength))
    {
        if (backgroundScanResultsLength > 0)
        {
            //now allocate memory and read it
            uint8_t* backgroundScanResults = C_CAST(uint8_t*, safe_calloc_aligned(backgroundScanResultsLength, sizeof(uint8_t), device->os_info.minimumAlignment));
            if (!backgroundScanResults)
            {
                return MEMORY_FAILURE;
            }
            if (SUCCESS == get_SCSI_Log(device, LP_BACKGROUND_SCAN_RESULTS, 0, M_NULLPTR, M_NULLPTR, true, backgroundScanResults, backgroundScanResultsLength, M_NULLPTR))
            {
                uint16_t parameterCode = 0;
                uint8_t parameterLength = 0;
                for (uint32_t offset = LOG_PAGE_HEADER_LENGTH; offset < backgroundScanResultsLength && *numberOfResults < MAX_BACKGROUND_SCAN_RESULTS; offset += parameterLength + LOG_PAGE_HEADER_LENGTH)
                {
                    parameterCode = M_BytesTo2ByteValue(backgroundScanResults[offset + 0], backgroundScanResults[offset + 1]);
                    parameterLength = backgroundScanResults[offset + 3];
                    if (parameterCode == 0)
                    {
                        //status parameter...don't need anything from here right now
                        continue;
                    }
                    else if (parameterCode >= 0x0001 && parameterCode <= 0x0800)
                    {
                        //result entry.
                        results[*numberOfResults].accumulatedPowerOnMinutes = M_BytesTo4ByteValue(backgroundScanResults[offset + 4], backgroundScanResults[offset + 5], backgroundScanResults[offset + 6], backgroundScanResults[offset + 7]);
                        results[*numberOfResults].lba = M_BytesTo8ByteValue(backgroundScanResults[offset + 16], backgroundScanResults[offset + 17], backgroundScanResults[offset + 18], backgroundScanResults[offset + 19], backgroundScanResults[offset + 20], backgroundScanResults[offset + 21], backgroundScanResults[offset + 22], backgroundScanResults[offset + 23]);
                        results[*numberOfResults].reassignStatus = M_Nibble1(backgroundScanResults[offset + 8]);
                        results[*numberOfResults].senseKey = M_Nibble0(backgroundScanResults[offset + 8]);
                        results[*numberOfResults].additionalSenseCode = backgroundScanResults[offset + 9];
                        results[*numberOfResults].additionalSenseCodeQualifier = backgroundScanResults[offset + 10];
                        ++(*numberOfResults);
                    }
                    else
                    {
                        //reserved or vendor specific parameter...just exit
                        break;
                    }
                }
                ret = SUCCESS;
            }
            else
            {
                ret = FAILURE;
            }
            safe_Free_aligned(C_CAST(void**, &backgroundScanResults));
        }
    }
    return ret;
}

eReturnValues get_LBAs_From_SCSI_Background_Scan_Log(tDevice* device, ptrPendingDefect defectList, uint32_t* numberOfDefects)
{
    eReturnValues ret = NOT_SUPPORTED;
    if (!defectList || !numberOfDefects)
    {
        return BAD_PARAMETER;
    }
    if (device->drive_info.drive_type == ATA_DRIVE)
    {
        return ret;
    }
    *numberOfDefects = 0;
    ptrBackgroundResults bmsResults = C_CAST(ptrBackgroundResults, safe_malloc(sizeof(backgroundResults) * MAX_BACKGROUND_SCAN_RESULTS));
    if (!bmsResults)
    {
        return MEMORY_FAILURE;
    }
    memset(bmsResults, 0, sizeof(backgroundResults) * MAX_BACKGROUND_SCAN_RESULTS);
    uint16_t numberOfBMSResults = 0;
    ret = get_SCSI_Background_Scan_Results(device, bmsResults, &numberOfBMSResults);
    if (ret == SUCCESS)
    {
        for (uint16_t bmsIter = 0; bmsIter < numberOfBMSResults; ++bmsIter)
        {
            defectList[*numberOfDefects].lba = bmsResults[bmsIter].lba;
            defectList[*numberOfDefects].powerOnHours = C_CAST(uint32_t, bmsResults[bmsIter].accumulatedPowerOnMinutes / UINT64_C(60));
            ++(*numberOfDefects);
        }
    }
    safe_free_background_results(&bmsResults);
    return ret;
}

//Defect list for this should be at least MAX_DST_ENTRIES in size
eReturnValues get_LBAs_From_DST_Log(tDevice* device, ptrPendingDefect defectList, uint32_t* numberOfDefects)
{
    eReturnValues ret = NOT_SUPPORTED;
    if (!defectList || !numberOfDefects)
    {
        return BAD_PARAMETER;
    }
    *numberOfDefects = 0;
    dstLogEntries dstEntries;
    memset(&dstEntries, 0, sizeof(dstLogEntries));
    ret = get_DST_Log_Entries(device, &dstEntries);
    if (ret == SUCCESS)
    {
        //Got the DST log entries, so we need to find error LBAs from read element failures to add that to the list
        for (uint8_t dstIter = 0; dstIter < dstEntries.numberOfEntries; ++dstIter)
        {
            if (dstEntries.dstEntry[dstIter].descriptorValid)
            {
                switch (M_Nibble1(dstEntries.dstEntry[dstIter].selfTestExecutionStatus))
                {
                case 0x07://read element failure
                    defectList[*numberOfDefects].lba = dstEntries.dstEntry[dstIter].lbaOfFailure;
                    if (dstEntries.dstEntry[dstIter].lifetimeTimestamp > UINT32_MAX)
                    {
                        defectList[*numberOfDefects].powerOnHours = UINT32_MAX;
                    }
                    else
                    {
                        defectList[*numberOfDefects].powerOnHours = C_CAST(uint32_t, dstEntries.dstEntry[dstIter].lifetimeTimestamp);
                    }
                    ++(*numberOfDefects);
                    break;
                default:
                    break;
                }
            }
        }
    }
    return ret;
}
