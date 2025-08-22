// SPDX-License-Identifier: MPL-2.0
//
// Do NOT modify or remove this copyright and license
//
// Copyright (c) 2012-2025 Seagate Technology LLC and/or its Affiliates, All Rights Reserved
//
// This software is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
//
// ******************************************************************************************
//
// \file defect.h
// \brief This file defines the functions for creating and reading defect information

#include "bit_manip.h"
#include "code_attributes.h"
#include "common_types.h"
#include "error_translation.h"
#include "io_utils.h"
#include "math_utils.h"
#include "memory_safety.h"
#include "precision_timer.h"
#include "prng.h"
#include "string_utils.h"
#include "type_conversion.h"

#include "defect.h"
#include "dst.h"
#include "logs.h"
#include "smart.h"

typedef struct s_scsiDefectDataIn
{
    tDevice*                device;
    bool                    primaryList;
    bool                    grownList;
    eSCSIAddressDescriptors defectListFormat;
    uint32_t                index;
    uint8_t*                defectData;
    uint32_t                dataLength;
} scsiDefectDataIn;

typedef struct s_scsiDefectDataOut
{
    bool                    gotDefectData;
    bool                    tenByte; // used 10 byte version of the command to read the data
    uint32_t                defectListLength;
    bool                    listHasPrimaryDescriptors;
    bool                    listHasGrownDescriptors;
    eSCSIAddressDescriptors returnedDefectListFormat;
    uint16_t                generationCode;
} scsiDefectDataOut;

M_NONNULL_PARAM_LIST(2)
M_PARAM_RW(2) static eReturnValues get_SCSI_Defect_Data(scsiDefectDataIn paramsIn, scsiDefectDataOut* paramsOut)
{
    eReturnValues ret = NOT_SUPPORTED;
    DISABLE_NONNULL_COMPARE
    if (paramsOut == M_NULLPTR)
    {
        return BAD_PARAMETER;
    }
    else
    {
        // set everything to safe default values in case this was not cleared properly before calling this function
        paramsOut->gotDefectData             = false;
        paramsOut->defectListLength          = UINT32_C(0);
        paramsOut->listHasPrimaryDescriptors = false;
        paramsOut->listHasGrownDescriptors   = false;
        paramsOut->returnedDefectListFormat  = AD_SHORT_BLOCK_FORMAT_ADDRESS_DESCRIPTOR;
        paramsOut->generationCode            = UINT16_C(0);
    }
    RESTORE_NONNULL_COMPARE
    // SCSI 2 has read defect data 12 available for optical devices, but not listed for block devices.
    // So assuming that we are talking to a block device, it needs to at least be newer than SCSI 2 for the 12B command
    if (paramsIn.device->drive_info.scsiVersion > SCSI_VERSION_SCSI2 && paramsOut->tenByte == false)
    {
        ret = scsi_Read_Defect_Data_12(paramsIn.device, paramsIn.primaryList, paramsIn.grownList,
                                       C_CAST(uint8_t, paramsIn.defectListFormat), 0, paramsIn.dataLength,
                                       paramsIn.defectData);
        if (ret == SUCCESS)
        {
            paramsOut->tenByte                   = false;
            paramsOut->gotDefectData             = true;
            paramsOut->defectListLength          = M_BytesTo4ByteValue(paramsIn.defectData[4], paramsIn.defectData[5],
                                                                       paramsIn.defectData[6], paramsIn.defectData[7]);
            paramsOut->listHasPrimaryDescriptors = M_ToBool(paramsIn.defectData[1] & BIT4);
            paramsOut->listHasGrownDescriptors   = M_ToBool(paramsIn.defectData[1] & BIT3);
            paramsOut->returnedDefectListFormat  = get_bit_range_uint8(paramsIn.defectData[1], 2, 0);
            paramsOut->generationCode            = M_BytesTo2ByteValue(paramsIn.defectData[2], paramsIn.defectData[3]);
        }
        else
        {
            if (!is_Invalid_Opcode(paramsIn.device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN))
            {
                // only continue to retry with 10B if the drive reported invalid operation code
                return ret;
            }
        }
    }
    if (!paramsOut->gotDefectData && paramsIn.index == 0)
    {
        ret = scsi_Read_Defect_Data_10(paramsIn.device, paramsIn.primaryList, paramsIn.grownList,
                                       C_CAST(uint8_t, paramsIn.defectListFormat),
                                       C_CAST(uint16_t, paramsIn.dataLength), paramsIn.defectData);
        if (ret == SUCCESS)
        {
            paramsOut->tenByte                   = true;
            paramsOut->gotDefectData             = true;
            paramsOut->defectListLength          = M_BytesTo2ByteValue(paramsIn.defectData[2], paramsIn.defectData[3]);
            paramsOut->listHasPrimaryDescriptors = M_ToBool(paramsIn.defectData[1] & BIT4);
            paramsOut->listHasGrownDescriptors   = M_ToBool(paramsIn.defectData[1] & BIT3);
            paramsOut->returnedDefectListFormat  = get_bit_range_uint8(paramsIn.defectData[1], 2, 0);
            paramsOut->generationCode            = UINT16_C(0);
        }
    }
    return ret;
}

typedef struct s_DefectListSizeInfo
{
    uint32_t numberOfElements;
    uint32_t defectAlloc;
    uint32_t increment;
} defectListSizeInfo;

M_NONNULL_PARAM_LIST(2)
M_PARAM_WO(2)
static eReturnValues get_Defect_List_Size_Info(scsiDefectDataOut defectResult, defectListSizeInfo* sizeInfo)
{
    eReturnValues ret = SUCCESS;
    DISABLE_NONNULL_COMPARE
    if (sizeInfo == M_NULLPTR)
    {
        return BAD_PARAMETER;
    }
    else
    {
        sizeInfo->defectAlloc      = UINT32_C(0);
        sizeInfo->numberOfElements = UINT32_C(0);
        sizeInfo->increment        = UINT32_C(0);
    }
    RESTORE_NONNULL_COMPARE
    // get the defect list length and
    switch (defectResult.returnedDefectListFormat)
    {
    case AD_SHORT_BLOCK_FORMAT_ADDRESS_DESCRIPTOR:
        sizeInfo->increment        = AD_LEN_SHORT_BLOCK_FORMAT_ADDRESS_DESCRIPTOR;
        sizeInfo->numberOfElements = defectResult.defectListLength / sizeInfo->increment;
        sizeInfo->defectAlloc      = sizeInfo->numberOfElements * sizeof(blockFormatAddress);
        break;
    case AD_LONG_BLOCK_FORMAT_ADDRESS_DESCRIPTOR:
        sizeInfo->increment        = AD_LEN_LONG_BLOCK_FORMAT_ADDRESS_DESCRIPTOR;
        sizeInfo->numberOfElements = defectResult.defectListLength / sizeInfo->increment;
        sizeInfo->defectAlloc      = sizeInfo->numberOfElements * sizeof(blockFormatAddress);
        break;
    case AD_EXTENDED_BYTES_FROM_INDEX_FORMAT_ADDRESS_DESCRIPTOR:
    case AD_BYTES_FROM_INDEX_FORMAT_ADDRESS_DESCRIPTOR:
        sizeInfo->increment        = AD_LEN_BYTES_FROM_INDEX_FORMAT_ADDRESS_DESCRIPTOR;
        sizeInfo->numberOfElements = defectResult.defectListLength / sizeInfo->increment;
        sizeInfo->defectAlloc      = sizeInfo->numberOfElements * sizeof(bytesFromIndexAddress);
        break;
    case AD_EXTENDED_PHYSICAL_SECTOR_FORMAT_ADDRESS_DESCRIPTOR:
    case AD_PHYSICAL_SECTOR_FORMAT_ADDRESS_DESCRIPTOR:
        sizeInfo->increment        = AD_LEN_PHYSICAL_SECTOR_FORMAT_ADDRESS_DESCRIPTOR;
        sizeInfo->numberOfElements = defectResult.defectListLength / sizeInfo->increment;
        sizeInfo->defectAlloc      = sizeInfo->numberOfElements * sizeof(physicalSectorAddress);
        break;
    case AD_VENDOR_SPECIFIC:
    case AD_RESERVED:
        ret = BAD_PARAMETER;
        break;
    }
    return ret;
}

M_NONNULL_PARAM_LIST(1, 3)
M_PARAM_WO(1)
M_PARAM_RO_SIZE(3, 4)
static M_INLINE void fill_block_address(blockFormatAddress*     address,
                                        eSCSIAddressDescriptors type,
                                        uint8_t*                dataPtr,
                                        uint32_t                datalength)
{
    switch (type)
    {
    case AD_SHORT_BLOCK_FORMAT_ADDRESS_DESCRIPTOR:
        if (datalength >= AD_LEN_SHORT_BLOCK_FORMAT_ADDRESS_DESCRIPTOR)
        {
            address->shortBlockAddress = M_BytesTo4ByteValue(dataPtr[0], dataPtr[1], dataPtr[2], dataPtr[3]);
        }
        break;
    case AD_LONG_BLOCK_FORMAT_ADDRESS_DESCRIPTOR:
        if (datalength >= AD_LEN_LONG_BLOCK_FORMAT_ADDRESS_DESCRIPTOR)
        {
            address->longBlockAddress = M_BytesTo8ByteValue(dataPtr[0], dataPtr[1], dataPtr[2], dataPtr[3], dataPtr[4],
                                                            dataPtr[5], dataPtr[6], dataPtr[7]);
        }
        break;
    case AD_EXTENDED_BYTES_FROM_INDEX_FORMAT_ADDRESS_DESCRIPTOR:
    case AD_EXTENDED_PHYSICAL_SECTOR_FORMAT_ADDRESS_DESCRIPTOR:
    case AD_BYTES_FROM_INDEX_FORMAT_ADDRESS_DESCRIPTOR:
    case AD_PHYSICAL_SECTOR_FORMAT_ADDRESS_DESCRIPTOR:
    case AD_VENDOR_SPECIFIC:
    case AD_RESERVED:
        break;
    }
}

M_NONNULL_PARAM_LIST(1, 3)
M_PARAM_WO(1)
M_PARAM_RO_SIZE(3, 4)
static M_INLINE void fill_bfi_address(bytesFromIndexAddress*  address,
                                      eSCSIAddressDescriptors type,
                                      uint8_t*                dataPtr,
                                      uint32_t                datalength)
{
    if (datalength >= AD_LEN_BYTES_FROM_INDEX_FORMAT_ADDRESS_DESCRIPTOR)
    {
        switch (type)
        {
        case AD_SHORT_BLOCK_FORMAT_ADDRESS_DESCRIPTOR:
        case AD_LONG_BLOCK_FORMAT_ADDRESS_DESCRIPTOR:
            break;
        case AD_EXTENDED_BYTES_FROM_INDEX_FORMAT_ADDRESS_DESCRIPTOR:
            address->cylinderNumber              = M_BytesTo4ByteValue(0, dataPtr[0], dataPtr[1], dataPtr[2]);
            address->headNumber                  = dataPtr[3];
            address->multiAddressDescriptorStart = M_ToBool(dataPtr[4] & BIT7);
            address->bytesFromIndex =
                M_BytesTo4ByteValue(get_bit_range_uint8(dataPtr[4], 3, 0), dataPtr[5], dataPtr[6], dataPtr[7]);
            break;
        case AD_BYTES_FROM_INDEX_FORMAT_ADDRESS_DESCRIPTOR:
            address->cylinderNumber = M_BytesTo4ByteValue(0, dataPtr[0], dataPtr[1], dataPtr[2]);
            address->headNumber     = dataPtr[3];
            address->bytesFromIndex = M_BytesTo4ByteValue(dataPtr[4], dataPtr[5], dataPtr[6], dataPtr[7]);
            break;
        case AD_EXTENDED_PHYSICAL_SECTOR_FORMAT_ADDRESS_DESCRIPTOR:
        case AD_PHYSICAL_SECTOR_FORMAT_ADDRESS_DESCRIPTOR:
        case AD_VENDOR_SPECIFIC:
        case AD_RESERVED:
            break;
        }
    }
}

M_NONNULL_PARAM_LIST(1, 3)
M_PARAM_WO(1)
M_PARAM_RO_SIZE(3, 4)
static M_INLINE void fill_physical_address(physicalSectorAddress*  address,
                                           eSCSIAddressDescriptors type,
                                           uint8_t*                dataPtr,
                                           uint32_t                datalength)
{
    if (datalength >= AD_LEN_PHYSICAL_SECTOR_FORMAT_ADDRESS_DESCRIPTOR)
    {
        switch (type)
        {
        case AD_SHORT_BLOCK_FORMAT_ADDRESS_DESCRIPTOR:
        case AD_LONG_BLOCK_FORMAT_ADDRESS_DESCRIPTOR:
        case AD_EXTENDED_BYTES_FROM_INDEX_FORMAT_ADDRESS_DESCRIPTOR:
        case AD_BYTES_FROM_INDEX_FORMAT_ADDRESS_DESCRIPTOR:
            break;
        case AD_EXTENDED_PHYSICAL_SECTOR_FORMAT_ADDRESS_DESCRIPTOR:
            address->cylinderNumber              = M_BytesTo4ByteValue(0, dataPtr[0], dataPtr[1], dataPtr[2]);
            address->headNumber                  = dataPtr[3];
            address->multiAddressDescriptorStart = M_ToBool(dataPtr[4] & BIT7);
            address->sectorNumber =
                M_BytesTo4ByteValue(get_bit_range_uint8(dataPtr[4], 3, 0), dataPtr[5], dataPtr[6], dataPtr[7]);
            break;
        case AD_PHYSICAL_SECTOR_FORMAT_ADDRESS_DESCRIPTOR:
            address->cylinderNumber = M_BytesTo4ByteValue(0, dataPtr[0], dataPtr[1], dataPtr[2]);
            address->headNumber     = dataPtr[3];
            address->sectorNumber   = M_BytesTo4ByteValue(dataPtr[4], dataPtr[5], dataPtr[6], dataPtr[7]);
            break;
        case AD_VENDOR_SPECIFIC:
        case AD_RESERVED:
            break;
        }
    }
}

static eReturnValues fill_Defect_List(ptrSCSIDefectList ptrDefects,
                                      scsiDefectDataIn  defectRequest,
                                      scsiDefectDataOut defectResult,
                                      uint32_t          elementID,
                                      uint32_t          headerLength,
                                      uint32_t          increment)
{
    eReturnValues ret = SUCCESS;
    if (ptrDefects != M_NULLPTR)
    {
        uint32_t offset = headerLength;
        for (uint32_t elementNumber = elementID; ret == SUCCESS && elementNumber < ptrDefects->numberOfElements &&
                                                 offset < (defectResult.defectListLength + headerLength);
             ++elementNumber, offset += increment)
        {
            switch (defectResult.returnedDefectListFormat)
            {
            case AD_SHORT_BLOCK_FORMAT_ADDRESS_DESCRIPTOR:
            case AD_LONG_BLOCK_FORMAT_ADDRESS_DESCRIPTOR:
                fill_block_address(&ptrDefects->block[elementNumber], defectResult.returnedDefectListFormat,
                                   &defectRequest.defectData[offset],
                                   (defectResult.defectListLength + headerLength) - offset);
                break;
            case AD_BYTES_FROM_INDEX_FORMAT_ADDRESS_DESCRIPTOR:
            case AD_EXTENDED_BYTES_FROM_INDEX_FORMAT_ADDRESS_DESCRIPTOR:
                fill_bfi_address(&ptrDefects->bfi[elementNumber], defectResult.returnedDefectListFormat,
                                 &defectRequest.defectData[offset],
                                 (defectResult.defectListLength + headerLength) - offset);
                break;
            case AD_PHYSICAL_SECTOR_FORMAT_ADDRESS_DESCRIPTOR:
            case AD_EXTENDED_PHYSICAL_SECTOR_FORMAT_ADDRESS_DESCRIPTOR:
                fill_physical_address(&ptrDefects->physical[elementNumber], defectResult.returnedDefectListFormat,
                                      &defectRequest.defectData[offset],
                                      (defectResult.defectListLength + headerLength) - offset);
                break;
            case AD_VENDOR_SPECIFIC:
            case AD_RESERVED:
                ret = BAD_PARAMETER;
                break;
            }
        }
    }
    return ret;
}

// one 10 or 12B read of the full list
static eReturnValues get_SCSI_Defects_Single_Command(scsiDefectDataIn   defectRequest,
                                                     scsiDefectDataOut  defectResult,
                                                     defectListSizeInfo sizeInfo,
                                                     scsiDefectList**   defects)
{
    eReturnValues ret = SUCCESS;
    // single command
    uint32_t headerLength    = defectResult.tenByte ? SCSI_DEFECT_DATA_10_HEADER_LEN : SCSI_DEFECT_DATA_12_HEADER_LEN;
    defectRequest.dataLength = defectResult.defectListLength + headerLength;
    if (defectResult.tenByte)
    {
        if (defectResult.defectListLength > (UINT16_MAX - SCSI_DEFECT_DATA_10_HEADER_LEN))
        {
            defectRequest.dataLength = UINT16_MAX; // we cannot pull more than this with this command. - TJE
        }
    }
    else
    {
        if (defectResult.defectListLength > (UINT32_MAX - SCSI_DEFECT_DATA_12_HEADER_LEN))
        {
            defectRequest.dataLength = UINT32_MAX; // we cannot pull more than this with this command. - TJE
        }
    }
    uint8_t* defectData =
        M_REINTERPRET_CAST(uint8_t*, safe_calloc_aligned(defectRequest.dataLength, sizeof(uint8_t),
                                                         defectRequest.device->os_info.minimumAlignment));
    if (defectData != M_NULLPTR)
    {
        defectRequest.defectData = defectData;
        ret                      = get_SCSI_Defect_Data(defectRequest, &defectResult);
        if (SUCCESS == ret)
        {
            // now allocate our list to return to the caller!
            size_t defectListAllocSize = sizeof(scsiDefectList) + uint32_to_sizet(sizeInfo.defectAlloc);
            *defects                   = M_REINTERPRET_CAST(ptrSCSIDefectList, safe_malloc(defectListAllocSize));
            if (*defects != M_NULLPTR)
            {
                ptrSCSIDefectList ptrDefects = *defects;
                safe_memset(ptrDefects, defectListAllocSize, 0, defectListAllocSize);
                ptrDefects->numberOfElements              = sizeInfo.numberOfElements;
                ptrDefects->containsGrownList             = defectResult.listHasGrownDescriptors;
                ptrDefects->containsPrimaryList           = defectResult.listHasPrimaryDescriptors;
                ptrDefects->format                        = defectResult.returnedDefectListFormat;
                ptrDefects->deviceHasMultipleLogicalUnits = M_ToBool(defectRequest.device->drive_info.numberOfLUs);
                ret = fill_Defect_List(ptrDefects, defectRequest, defectResult, UINT32_C(0), headerLength,
                                       sizeInfo.increment);
            }
            else
            {
                ret = MEMORY_FAILURE;
            }
        }
        safe_free_aligned(&defectData);
    }
    else
    {
        ret = MEMORY_FAILURE;
    }
    return ret;
}

static bool is_SCSI_Defect_List_With_Offsets_Supported(tDevice* device)
{
    bool multipleCommandsSupported = false;
    // possibly multiple commands (if address descriptor index is supported in the command...added
    // in SBC3)
    if (device->drive_info.scsiVersion >= SCSI_VERSION_SPC_4)
    {
        scsiOperationCodeInfoRequest readDefDataSupReq;
        safe_memset(&readDefDataSupReq, sizeof(scsiOperationCodeInfoRequest), 0, sizeof(scsiOperationCodeInfoRequest));
        readDefDataSupReq.operationCode      = READ_DEFECT_DATA_12_CMD;
        readDefDataSupReq.serviceActionValid = false;
        eSCSICmdSupport readDefectSupport    = is_SCSI_Operation_Code_Supported(device, &readDefDataSupReq);
        if (readDefectSupport == SCSI_CMD_SUPPORT_SUPPORTED_TO_SCSI_STANDARD)
        {
            // skipping the support data since we shouldn't be this far if the command isn't supported!
            uint32_t addressIndexSupport =
                M_BytesTo4ByteValue(readDefDataSupReq.cdbUsageData[2], readDefDataSupReq.cdbUsageData[3],
                                    readDefDataSupReq.cdbUsageData[4], readDefDataSupReq.cdbUsageData[5]);
            if (addressIndexSupport > 0)
            {
                multipleCommandsSupported = true;
            }
        }
    }
    return multipleCommandsSupported;
}

// Multiple 12B commands to read the list
static eReturnValues get_SCSI_Defects_With_Offsets(scsiDefectDataIn   defectRequest,
                                                   scsiDefectDataOut  defectResult,
                                                   defectListSizeInfo sizeInfo,
                                                   scsiDefectList**   defects)
{
    eReturnValues ret = SUCCESS;
    // read the list in multiple commands! Do this in 64k chunks.
    defectRequest.dataLength = UINT32_C(65536); // this is 64k
    uint8_t* defectData =
        M_REINTERPRET_CAST(uint8_t*, safe_calloc_aligned(defectRequest.dataLength, sizeof(uint8_t),
                                                         defectRequest.device->os_info.minimumAlignment));
    if (defectData != M_NULLPTR)
    {
        size_t defectListAllocSize = sizeof(scsiDefectList) + uint32_to_sizet(sizeInfo.defectAlloc);
        defectRequest.defectData   = defectData;
        *defects                   = M_REINTERPRET_CAST(ptrSCSIDefectList, safe_malloc(defectListAllocSize));
        if (*defects != M_NULLPTR)
        {
            uint32_t          elementNumber    = UINT32_C(0);
            ptrSCSIDefectList ptrDefects       = *defects;
            bool              filledInListInfo = false;
            safe_memset(ptrDefects, defectListAllocSize, 0, defectListAllocSize);
            ptrDefects->numberOfElements              = sizeInfo.numberOfElements;
            ptrDefects->deviceHasMultipleLogicalUnits = M_ToBool(defectRequest.device->drive_info.numberOfLUs);
            while (elementNumber < sizeInfo.numberOfElements && sizeInfo.increment > 0)
            {
                safe_memset(defectData, defectRequest.dataLength, 0, defectRequest.dataLength);
                defectRequest.index = elementNumber * sizeInfo.increment;
                ret                 = get_SCSI_Defect_Data(defectRequest, &defectResult);
                if (SUCCESS == ret)
                {
                    if (!filledInListInfo)
                    {
                        ptrDefects->containsGrownList   = defectResult.listHasGrownDescriptors;
                        ptrDefects->containsPrimaryList = defectResult.listHasPrimaryDescriptors;
                        ptrDefects->format              = defectResult.returnedDefectListFormat;
                        ptrDefects->generation          = defectResult.generationCode;
                        filledInListInfo                = true;
                    }
                    if (sizeInfo.increment > 0)
                    {
                        ret = fill_Defect_List(ptrDefects, defectRequest, defectResult, elementNumber,
                                               SCSI_DEFECT_DATA_12_HEADER_LEN, sizeInfo.increment);
                    }
                    else
                    {
                        break;
                    }
                }
            }
        }
        else
        {
            ret = MEMORY_FAILURE;
        }
        safe_free_aligned(&defectData);
    }
    else
    {
        ret = MEMORY_FAILURE;
    }
    return ret;
}

eReturnValues get_SCSI_Defect_List(tDevice*                device,
                                   eSCSIAddressDescriptors defectListFormat,
                                   bool                    grownList,
                                   bool                    primaryList,
                                   scsiDefectList**        defects)
{
    eReturnValues ret = SUCCESS;
    DISABLE_NONNULL_COMPARE
    if (defects != M_NULLPTR)
    {
        const uint32_t dataLength = UINT32_C(8);
        DECLARE_ZERO_INIT_ARRAY(uint8_t, defectHeader, UINT32_C(8));
        scsiDefectDataIn  defectRequest = {.device           = device,
                                           .grownList        = grownList,
                                           .primaryList      = primaryList,
                                           .defectListFormat = defectListFormat,
                                           .index            = UINT32_C(0),
                                           .defectData       = defectHeader,
                                           .dataLength       = dataLength};
        scsiDefectDataOut defectResult;
        safe_memset(&defectResult, sizeof(scsiDefectDataOut), 0, sizeof(scsiDefectDataOut));

        ret = get_SCSI_Defect_Data(defectRequest, &defectResult);
        defectRequest.defectData =
            M_NULLPTR; // set to NULL as we will need new memory when reading in the following functions
        defectRequest.dataLength = UINT32_C(0);

        if (defectResult.gotDefectData)
        {
            if (defectResult.defectListLength > 0)
            {
                defectListSizeInfo sizeInfo;
                safe_memset(&sizeInfo, sizeof(defectListSizeInfo), 0, sizeof(defectListSizeInfo));
                ret = get_Defect_List_Size_Info(defectResult, &sizeInfo);
                if (ret == SUCCESS)
                {
                    if (!defectResult.tenByte && is_SCSI_Defect_List_With_Offsets_Supported(device))
                    {
                        ret = get_SCSI_Defects_With_Offsets(defectRequest, defectResult, sizeInfo, defects);
                    }
                    else
                    {
                        ret = get_SCSI_Defects_Single_Command(defectRequest, defectResult, sizeInfo, defects);
                    }
                }
            }
            else
            {
                // defect list length is zero, so we don't have anything else to do but allocate the struct and populate
                // the data, then return it
                *defects = M_REINTERPRET_CAST(ptrSCSIDefectList, safe_calloc(1, sizeof(scsiDefectList)));
                if (*defects)
                {
                    ptrSCSIDefectList temp              = *defects;
                    temp->numberOfElements              = UINT32_C(0);
                    temp->containsGrownList             = defectResult.listHasGrownDescriptors;
                    temp->containsPrimaryList           = defectResult.listHasPrimaryDescriptors;
                    temp->generation                    = defectResult.generationCode;
                    temp->format                        = defectResult.returnedDefectListFormat;
                    temp->deviceHasMultipleLogicalUnits = M_ToBool(device->drive_info.numberOfLUs);
                    ret                                 = SUCCESS;
                }
                else
                {
                    ret = MEMORY_FAILURE;
                }
            }
        }
        else
        {
            ret = NOT_SUPPORTED;
        }
    }
    else
    {
        ret = BAD_PARAMETER;
    }
    RESTORE_NONNULL_COMPARE
    return ret;
}

void free_Defect_List(scsiDefectList** defects)
{
    safe_free_core(M_REINTERPRET_CAST(void**, defects));
}

static void print_SCSI_Defect_Short_Block(ptrSCSIDefectList defects)
{
    print_str("---Short Block Format---\n");
    if (defects->numberOfElements > UINT32_C(0))
    {
        printf("Total Defects in list: %" PRIu32 "\n", defects->numberOfElements);
        for (uint64_t iter = UINT64_C(0); iter < defects->numberOfElements; ++iter)
        {
            printf("%" PRIu32 "\n", defects->block[iter].shortBlockAddress);
        }
    }
    else
    {
        print_str("No Defects Found\n");
    }
}

static void print_SCSI_Defect_Long_Block(ptrSCSIDefectList defects)
{
    print_str("---Long Block Format---\n");
    if (defects->numberOfElements > UINT32_C(0))
    {
        printf("Total Defects in list: %" PRIu32 "\n", defects->numberOfElements);
        for (uint64_t iter = UINT64_C(0); iter < defects->numberOfElements; ++iter)
        {
            printf("%" PRIu64 "\n", defects->block[iter].longBlockAddress);
        }
    }
    else
    {
        print_str("No Defects Found\n");
    }
}

static void print_SCSI_Defect_XCHS(ptrSCSIDefectList defects)
{
    print_str("---Extended Physical Sector Format---\n");
    if (defects->numberOfElements > UINT32_C(0))
    {
        bool multiBit = false;
        printf("Total Defects in list: %" PRIu32 "\n", defects->numberOfElements);
        printf("  %-8s  %-3s  %10s \n", "Cylinder", "Head", "Sector");
        for (uint64_t iter = UINT64_C(0); iter < defects->numberOfElements; ++iter)
        {
            char multi          = ' ';
            bool switchMultiOff = false;
            if (defects->physical[iter].multiAddressDescriptorStart)
            {
                multiBit = true;
                multi    = '+';
                print_str("------------------------------\n");
            }
            else if (multiBit)
            {
                // multi was on, but now off...this is the last descriptor for the same error
                multi          = '+';
                switchMultiOff = true;
            }
            if (defects->physical[iter].sectorNumber == MAX_28BIT)
            {
                printf("%c %8" PRIu32 "  %3" PRIu8 "  %10s\n", multi, defects->physical[iter].cylinderNumber,
                       defects->physical[iter].headNumber, "Full Track");
            }
            else
            {
                printf("%c %8" PRIu32 "  %3" PRIu8 "  %10" PRIu32 " \n", multi, defects->physical[iter].cylinderNumber,
                       defects->physical[iter].headNumber, defects->physical[iter].sectorNumber);
            }
            if (switchMultiOff)
            {
                multiBit = false;
                print_str("------------------------------\n");
            }
        }
    }
    else
    {
        print_str("No Defects Found\n");
    }
}

static void print_SCSI_Defect_CHS(ptrSCSIDefectList defects)
{
    print_str("---Physical Sector Format---\n");
    if (defects->numberOfElements > UINT32_C(0))
    {
        printf("Total Defects in list: %" PRIu32 "\n", defects->numberOfElements);
        printf("  %-8s  %-3s  %10s \n", "Cylinder", "Head", "Sector");
        for (uint64_t iter = UINT64_C(0); iter < defects->numberOfElements; ++iter)
        {
            if (defects->physical[iter].sectorNumber == UINT32_MAX)
            {
                printf("  %8" PRIu32 "  %3" PRIu8 "  %10s\n", defects->physical[iter].cylinderNumber,
                       defects->physical[iter].headNumber, "Full Track");
            }
            else
            {
                printf("  %8" PRIu32 "  %3" PRIu8 "  %10" PRIu32 "\n", defects->physical[iter].cylinderNumber,
                       defects->physical[iter].headNumber, defects->physical[iter].sectorNumber);
            }
        }
    }
    else
    {
        print_str("No Defects Found\n");
    }
}

static void print_SCSI_Defect_XBFI(ptrSCSIDefectList defects)
{
    print_str("---Extended Bytes From Index Format---\n");
    if (defects->numberOfElements > UINT32_C(0))
    {
        bool multiBit = false;
        printf("Total Defects in list: %" PRIu32 "\n", defects->numberOfElements);
        printf("  %-8s  %-3s  %16s \n", "Cylinder", "Head", "Bytes From Index");
        for (uint64_t iter = UINT64_C(0); iter < defects->numberOfElements; ++iter)
        {
            char multi          = ' ';
            bool switchMultiOff = false;
            if (defects->bfi[iter].multiAddressDescriptorStart)
            {
                multiBit = true;
                multi    = '+';
            }
            else if (multiBit)
            {
                // multi was on, but now off...this is the last descriptor for the same error
                multi          = '+';
                switchMultiOff = true;
            }
            if (defects->bfi[iter].bytesFromIndex == MAX_28BIT)
            {
                printf("%c %8" PRIu32 "  %3" PRIu8 "  %10s\n", multi, defects->bfi[iter].cylinderNumber,
                       defects->bfi[iter].headNumber, "Full Track");
            }
            else
            {
                printf("%c %8" PRIu32 "  %3" PRIu8 "  %10" PRIu32 " \n", multi, defects->bfi[iter].cylinderNumber,
                       defects->bfi[iter].headNumber, defects->bfi[iter].bytesFromIndex);
            }
            if (switchMultiOff)
            {
                multiBit = false;
                print_str("------------------------------\n");
            }
        }
    }
    else
    {
        print_str("No Defects Found\n");
    }
}

static void print_SCSI_Defect_BFI(ptrSCSIDefectList defects)
{
    print_str("---Bytes From Index Format---\n");
    if (defects->numberOfElements > UINT32_C(0))
    {
        printf("Total Defects in list: %" PRIu32 "\n", defects->numberOfElements);
        printf("  %-8s  %-3s  %16s \n", "Cylinder", "Head", "Bytes From Index");
        for (uint64_t iter = UINT64_C(0); iter < defects->numberOfElements; ++iter)
        {
            if (defects->bfi[iter].bytesFromIndex == UINT32_MAX)
            {
                printf("  %8" PRIu32 "  %3" PRIu8 "  %16s\n", defects->bfi[iter].cylinderNumber,
                       defects->bfi[iter].headNumber, "Full Track");
            }
            else
            {
                printf("  %8" PRIu32 "  %3" PRIu8 "  %16" PRIu32 "\n", defects->bfi[iter].cylinderNumber,
                       defects->bfi[iter].headNumber, defects->bfi[iter].bytesFromIndex);
            }
        }
    }
    else
    {
        print_str("No Defects Found\n");
    }
}

void print_SCSI_Defect_List(ptrSCSIDefectList defects)
{
    DISABLE_NONNULL_COMPARE
    if (defects != M_NULLPTR)
    {
        print_str("===SCSI Defect List===\n");
        if (defects->containsPrimaryList)
        {
            print_str("\tList includes primary defects\n");
        }
        if (defects->containsGrownList)
        {
            print_str("\tList includes grown defects\n");
        }
        if (defects->generation > 0)
        {
            printf("\tGeneration Code: %" PRIu16 "\n", defects->generation);
        }
        if (defects->deviceHasMultipleLogicalUnits)
        {
            print_str("\tNOTE: At this time, reported defects are for the entire device, not a single logical unit\n");
        }
        // TODO: Add a way to handle getting per-head counts to output first
        switch (defects->format)
        {
        case AD_SHORT_BLOCK_FORMAT_ADDRESS_DESCRIPTOR:
            print_SCSI_Defect_Short_Block(defects);
            break;
        case AD_LONG_BLOCK_FORMAT_ADDRESS_DESCRIPTOR:
            print_SCSI_Defect_Long_Block(defects);
            break;
        case AD_EXTENDED_PHYSICAL_SECTOR_FORMAT_ADDRESS_DESCRIPTOR:
            print_SCSI_Defect_XCHS(defects);
            break;
        case AD_PHYSICAL_SECTOR_FORMAT_ADDRESS_DESCRIPTOR:
            print_SCSI_Defect_CHS(defects);
            break;
        case AD_EXTENDED_BYTES_FROM_INDEX_FORMAT_ADDRESS_DESCRIPTOR:
            print_SCSI_Defect_XBFI(defects);
            break;
        case AD_BYTES_FROM_INDEX_FORMAT_ADDRESS_DESCRIPTOR:
            print_SCSI_Defect_BFI(defects);
            break;
        default:
            print_str("Error: Unknown defect list format. Cannot be displayed!\n");
            break;
        }
    }
    RESTORE_NONNULL_COMPARE
}

eReturnValues create_Random_Uncorrectables(tDevice*      device,
                                           uint16_t      numberOfRandomLBAs,
                                           bool          readUncorrectables,
                                           bool          flaggedErrors,
                                           custom_Update updateFunction,
                                           void*         updateData)
{
    eReturnValues ret      = SUCCESS;
    uint16_t      iterator = UINT16_C(0);
    seed_64(C_CAST(uint64_t, time(M_NULLPTR))); // start the random number generator
    for (iterator = 0; iterator < numberOfRandomLBAs; ++iterator)
    {
        uint64_t randomLBA = random_Range_64(0, device->drive_info.deviceMaxLba);
        // align the random LBA to the physical sector
        randomLBA = align_LBA(device, randomLBA);
        // call the function to create an uncorrectable with the range set to 1 so we only corrupt 1 physical block at a
        // time randomly
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

eReturnValues create_Uncorrectables(tDevice*                    device,
                                    uint64_t                    startingLBA,
                                    uint64_t                    range,
                                    bool                        readUncorrectables,
                                    M_ATTR_UNUSED custom_Update updateFunction,
                                    M_ATTR_UNUSED void*         updateData)
{
    eReturnValues ret           = SUCCESS;
    uint64_t      iterator      = UINT64_C(0);
    bool          wue           = is_Write_Psuedo_Uncorrectable_Supported(device);
    bool          readWriteLong = is_Read_Long_Write_Long_Supported(device);
    uint16_t      logicalPerPhysicalSectors =
        C_CAST(uint16_t, device->drive_info.devicePhyBlockSize / device->drive_info.deviceBlockSize);
    uint16_t increment = logicalPerPhysicalSectors;
    if (!wue && readWriteLong && logicalPerPhysicalSectors != 1 && device->drive_info.drive_type == ATA_DRIVE)
    {
        // changing the increment amount to 1 because the ATA read/write long commands can only do a single LBA at a
        // time.
        increment = 1;
    }
    startingLBA = align_LBA(device, startingLBA);
    for (iterator = startingLBA; iterator < (startingLBA + range); iterator += increment)
    {
        ret = SUCCESS;
        if (device->deviceVerbosity > VERBOSITY_QUIET)
        {
            printf("Creating Uncorrectable error at LBA %-20" PRIu64 "\n", iterator);
        }
        if (wue)
        {
            ret = write_Psuedo_Uncorrectable_Error(device, iterator);
        }
        else // set not supported if wue is not true so that the read/write long can be used in place
        {
            ret = NOT_SUPPORTED;
        }
        if (readWriteLong && ret != SUCCESS)
        {
            ret = corrupt_LBA_Read_Write_Long(
                device, iterator, UINT16_MAX); // saying to corrupt all the data bytes to make sure we do get an error.
            if (ret == SUCCESS && wue)
            {
                // for some odd reason wue did not work but this method did...so switch to using this
                wue = false;
                if (device->drive_info.drive_type == ATA_DRIVE)
                {
                    increment = 1;
                }
            }
        }
        if (ret != SUCCESS)
        {
            break;
        }
        if (readUncorrectables)
        {
            size_t dataBufSize =
                uint32_to_sizet(device->drive_info.deviceBlockSize) * uint16_to_sizet(logicalPerPhysicalSectors);
            uint8_t* dataBuf = M_REINTERPRET_CAST(
                uint8_t*, safe_calloc_aligned(dataBufSize, sizeof(uint8_t), device->os_info.minimumAlignment));
            if (dataBuf == M_NULLPTR)
            {
                return MEMORY_FAILURE;
            }
            // don't check return status since we expect this to fail after creating the error
            if (device->deviceVerbosity > VERBOSITY_QUIET)
            {
                printf("Reading Uncorrectable error at LBA %-20" PRIu64 "\n", iterator);
            }
            read_LBA(device, iterator, false, dataBuf, logicalPerPhysicalSectors * device->drive_info.deviceBlockSize);
            // scsi_Read_16(device, 0, false, false, false, iterator, 0, logicalPerPhysicalSectors, dataBuf);
            safe_free_aligned(&dataBuf);
        }
    }
    return ret;
}

eReturnValues flag_Uncorrectables(tDevice*                    device,
                                  uint64_t                    startingLBA,
                                  uint64_t                    range,
                                  M_ATTR_UNUSED custom_Update updateFunction,
                                  M_ATTR_UNUSED void*         updateData)
{
    eReturnValues ret      = SUCCESS;
    uint64_t      iterator = UINT64_C(0);
    if (is_Write_Flagged_Uncorrectable_Supported(device))
    {
        // uint16_t logicalPerPhysicalSectors = device->drive_info.devicePhyBlockSize /
        // device->drive_info.deviceBlockSize; This function will only flag individual logical sectors since flagging
        // works differently than pseudo uncorrectables, which we write the full sector with since a psuedo
        // uncorrectable will always affect the full physical sector.
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

bool is_Read_Long_Write_Long_Supported(tDevice* device)
{
    bool supported = false;
    if (device->drive_info.drive_type == ATA_DRIVE)
    {
        if ((is_ATA_Identify_Word_Valid(le16_to_host(device->drive_info.IdentifyData.ata.Word206)) &&
             le16_to_host(device->drive_info.IdentifyData.ata.Word206) & BIT1) ||
            is_ATA_Identify_Word_Valid(
                le16_to_host(device->drive_info.IdentifyData.ata.Word022)) /*legacy drive support case*/)
        {
            supported = true;
        }
    }
    else if (device->drive_info.drive_type == SCSI_DRIVE)
    {
        // Trying to use report supported operation codes/inquiry cmdDT first on the read long command.
        // Using read long since it was removed in latest specs, so if it is not supported, then we know write long
        // won't work unless using the write uncorrectable bit.
        uint8_t operationCode = READ_LONG_10;
        if (device->drive_info.deviceMaxLba > UINT32_MAX)
        {
            operationCode = READ_LONG_16;
        }

        scsiOperationCodeInfoRequest readLongSupReq;
        safe_memset(&readLongSupReq, sizeof(scsiOperationCodeInfoRequest), 0, sizeof(scsiOperationCodeInfoRequest));
        readLongSupReq.operationCode      = operationCode;
        readLongSupReq.serviceActionValid = false;
        eSCSICmdSupport readLongSupport   = is_SCSI_Operation_Code_Supported(device, &readLongSupReq);
        if (readLongSupport == SCSI_CMD_SUPPORT_SUPPORTED_TO_SCSI_STANDARD)
        {
            supported = true;
        }
        if (readLongSupport == SCSI_CMD_SUPPORT_UNKNOWN && !supported)
        {
            // try issuing a read long command with no data transfer and see if it's treated as an error or not.
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

static eReturnValues ata_Legacy_corrupt_LBA_Read_Write_Long(tDevice* device,
                                                            uint64_t corruptLBA,
                                                            uint16_t numberOfBytesToCorrupt)
{
    eReturnValues ret                         = SUCCESS;
    bool          setFeaturesToChangeECCBytes = false;
    if (le16_to_host(device->drive_info.IdentifyData.ata.Word022) != 4)
    {
        // need to issue a set features command to specify the number of ECC bytes before doing a read or write
        // long (according to old Seagate ATA reference manual from the web)
        if (SUCCESS ==
            ata_SF_VU_ECC_Bytes_Long_Cmds(device, ATA_SF_ENABLE, M_Byte0(device->drive_info.IdentifyData.ata.Word022)))
        {
            setFeaturesToChangeECCBytes = true;
        }
    }
    uint32_t dataSize = device->drive_info.deviceBlockSize + le16_to_host(device->drive_info.IdentifyData.ata.Word022);
    uint8_t* data =
        M_REINTERPRET_CAST(uint8_t*, safe_calloc_aligned(dataSize, sizeof(uint8_t), device->os_info.minimumAlignment));
    if (data == M_NULLPTR)
    {
        return MEMORY_FAILURE;
    }
    // This drive supports the legacy 28bit read/write long commands from ATA...
    // These commands are really old and transfer weird byte based values.
    // While these transfer lengths shouldbe supported by SAT, there are some SATLs that won't handle this odd
    // case. It may or may not go through...-TJE
    if (device->drive_info.ata_Options.chsModeOnly)
    {
        uint16_t cylinder = UINT16_C(0);
        uint8_t  head     = UINT8_C(0);
        uint8_t  sector   = UINT8_C(0);
        if (SUCCESS == convert_LBA_To_CHS(device, C_CAST(uint32_t, corruptLBA), &cylinder, &head, &sector))
        {
            ret = ata_Legacy_Read_Long_CHS(device, true, cylinder, head, sector, data, dataSize);
            if (ret == SUCCESS)
            {
                // seed_64(C_CAST(uint64_t, time(M_NULLPTR)));
                // modify the user data to cause a uncorrectable error
                for (uint32_t iter = UINT32_C(0);
                     iter < numberOfBytesToCorrupt && iter < device->drive_info.deviceBlockSize - 1; ++iter)
                {
                    data[iter] = M_2sCOMPLEMENT(data[iter]); // C_CAST(uint8_t, random_Range_64(0, UINT8_MAX));
                }
                ret = ata_Legacy_Write_Long_CHS(device, true, cylinder, head, sector, data, dataSize);
            }
        }
        else // Couldn't convert or the LBA is greater than the current CHS mode
        {
            ret = NOT_SUPPORTED;
        }
    }
    else
    {
        ret = ata_Legacy_Read_Long(device, true, C_CAST(uint32_t, corruptLBA), data, dataSize);
        if (ret == SUCCESS)
        {
            // seed_64(C_CAST(uint64_t, time(M_NULLPTR)));
            // modify the user data to cause a uncorrectable error
            for (uint32_t iter = UINT32_C(0);
                 iter < numberOfBytesToCorrupt && iter < device->drive_info.deviceBlockSize - 1; ++iter)
            {
                data[iter] = M_2sCOMPLEMENT(data[iter]); // C_CAST(uint8_t, random_Range_64(0, UINT8_MAX));
            }
            ret = ata_Legacy_Write_Long(device, true, C_CAST(uint32_t, corruptLBA), data, dataSize);
        }
    }
    if (setFeaturesToChangeECCBytes)
    {
        // reverting back to drive defaults again so that we don't mess anyone else up.
        if (SUCCESS == ata_SF_VU_ECC_Bytes_Long_Cmds(device, ATA_SF_DISABLE, 0))
        {
            setFeaturesToChangeECCBytes = false;
        }
    }
    safe_free_aligned(&data);
    return ret;
}

static eReturnValues ata_SCT_corrupt_LBA_Read_Write_Long(tDevice* device,
                                                         uint64_t corruptLBA,
                                                         uint16_t numberOfBytesToCorrupt)
{
    eReturnValues ret = SUCCESS;
    // use SCT read & write long commands
    uint16_t numberOfECCCRCBytes     = UINT16_C(0);
    uint16_t numberOfBlocksRequested = UINT16_C(0);
    uint32_t dataSize                = device->drive_info.deviceBlockSize + LEGACY_DRIVE_SEC_SIZE;
    uint8_t* data =
        M_REINTERPRET_CAST(uint8_t*, safe_calloc_aligned(dataSize, sizeof(uint8_t), device->os_info.minimumAlignment));
    if (data == M_NULLPTR)
    {
        return MEMORY_FAILURE;
    }
    ret = send_ATA_SCT_Read_Write_Long(device, SCT_RWL_READ_LONG, corruptLBA, data, dataSize, &numberOfECCCRCBytes,
                                       &numberOfBlocksRequested);
    if (ret == SUCCESS)
    {
        // seed_64(C_CAST(uint64_t, time(M_NULLPTR)));
        // modify the user data to cause a uncorrectable error
        for (uint32_t iter = UINT32_C(0);
             iter < numberOfBytesToCorrupt && iter < device->drive_info.deviceBlockSize - 1; ++iter)
        {
            data[iter] = M_2sCOMPLEMENT(data[iter]); // C_CAST(uint8_t, random_Range_64(0, UINT8_MAX));
        }
        if (numberOfBlocksRequested > UINT16_C(0))
        {
            // The drive responded through SAT enough to tell us exactly how many blocks are expected...so we
            // can set the data transfer length as is expected...since this wasn't clear on non 512B logical
            // sector drives.
            dataSize = LEGACY_DRIVE_SEC_SIZE * numberOfBlocksRequested;
        }
        // now write back the data with a write long command
        ret =
            send_ATA_SCT_Read_Write_Long(device, SCT_RWL_WRITE_LONG, corruptLBA, data, dataSize, M_NULLPTR, M_NULLPTR);
    }
    safe_free_aligned(&data);
    return ret;
}

static eReturnValues ata_corrupt_LBA_Read_Write_Long(tDevice* device,
                                                     uint64_t corruptLBA,
                                                     uint16_t numberOfBytesToCorrupt)
{
    eReturnValues ret = NOT_SUPPORTED;
    if (is_ATA_Identify_Word_Valid(le16_to_host(device->drive_info.IdentifyData.ata.Word206)) &&
        le16_to_host(device->drive_info.IdentifyData.ata.Word206) & BIT1)
    {
        ret = ata_SCT_corrupt_LBA_Read_Write_Long(device, corruptLBA, numberOfBytesToCorrupt);
    }
    else if (is_ATA_Identify_Word_Valid(le16_to_host(device->drive_info.IdentifyData.ata.Word022)) &&
             corruptLBA < MAX_28_BIT_LBA) /*a value of zero may be valid on really old drives which otherwise accept
                                             this command, but this should be ok for now*/
    {
        ret = ata_Legacy_corrupt_LBA_Read_Write_Long(device, corruptLBA, numberOfBytesToCorrupt);
    }
    return ret;
}

static eReturnValues scsi_corrupt_LBA_Read_Write_Long(tDevice* device,
                                                      uint64_t corruptLBA,
                                                      uint16_t numberOfBytesToCorrupt)
{
    eReturnValues ret                        = NOT_SUPPORTED;
    bool          multipleLogicalPerPhysical = false; // used to set the physical block bit when applicable
    uint16_t      logicalPerPhysicalBlocks =
        C_CAST(uint16_t, (device->drive_info.devicePhyBlockSize / device->drive_info.deviceBlockSize));
    if (logicalPerPhysicalBlocks > 1)
    {
        // since this device has multiple logical blocks per physical block, we also need to adjust the LBA to be at the
        // start of the physical block do this by dividing by the number of logical sectors per physical sector. This
        // integer division will get us aligned
        uint64_t tempLBA = corruptLBA / logicalPerPhysicalBlocks;
        tempLBA *= logicalPerPhysicalBlocks;
        // do we need to adjust for alignment? We'll add it in later if I ever get a drive that has an alignment other
        // than 0 - TJE
        corruptLBA = tempLBA;
        // set this flag for SCSI
        multipleLogicalPerPhysical = true;
    }
    senseDataFields senseFields;
    safe_memset(&senseFields, sizeof(senseDataFields), 0, sizeof(senseDataFields));
    uint16_t dataLength = C_CAST(uint16_t, device->drive_info.deviceBlockSize *
                                               logicalPerPhysicalBlocks); // start with this size for now...
    uint8_t* dataBuffer = M_REINTERPRET_CAST(
        uint8_t*, safe_calloc_aligned(dataLength, sizeof(uint8_t), device->os_info.minimumAlignment));
    if (device->drive_info.deviceMaxLba > UINT32_MAX)
    {
        ret = scsi_Read_Long_16(device, multipleLogicalPerPhysical, true, corruptLBA, dataLength, dataBuffer);
    }
    else
    {
        ret = scsi_Read_Long_10(device, multipleLogicalPerPhysical, true, C_CAST(uint32_t, corruptLBA), dataLength,
                                dataBuffer);
    }
    // ret should not be success and we should have an illegal length indicator set so we can reallocate and read
    // the ecc bytes
    safe_memset(&senseFields, sizeof(senseDataFields), 0, sizeof(senseDataFields));
    get_Sense_Data_Fields(device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, &senseFields);
    if (senseFields.illegalLengthIndication &&
        senseFields.valid) // spec says these bit should both be zero since we didn't do this request with enough
                           // bytes to read the ECC bytes
    {
        if (senseFields.fixedFormat)
        {
            dataLength +=
                C_CAST(uint16_t,
                       M_2sCOMPLEMENT(senseFields.fixedInformation)); // length different is a twos compliment value
                                                                      // since we requested less than is available.
        }
        else
        {
            dataLength += C_CAST(
                uint16_t,
                M_2sCOMPLEMENT(senseFields.descriptorInformation)); // length different is a twos compliment value
                                                                    // since we requested less than is available.
        }
        uint8_t* temp = M_REINTERPRET_CAST(
            uint8_t*, safe_realloc_aligned(dataBuffer, 0, dataLength, device->os_info.minimumAlignment));
        if (temp != M_NULLPTR)
        {
            dataBuffer = temp;
            safe_memset(dataBuffer, dataLength, 0, dataLength);
            if (device->drive_info.deviceMaxLba > UINT32_MAX)
            {
                ret = scsi_Read_Long_16(device, multipleLogicalPerPhysical, true, corruptLBA, dataLength, dataBuffer);
            }
            else
            {
                ret = scsi_Read_Long_10(device, multipleLogicalPerPhysical, true, C_CAST(uint32_t, corruptLBA),
                                        dataLength, dataBuffer);
            }
            if (ret != SUCCESS)
            {
                ret = FAILURE;
            }
            else
            {
                // seed_64(C_CAST(uint64_t, time(M_NULLPTR)));
                // modify the user data to cause a uncorrectable error
                for (uint32_t iter = UINT32_C(0);
                     iter < numberOfBytesToCorrupt &&
                     iter < (device->drive_info.deviceBlockSize * logicalPerPhysicalBlocks - 1);
                     ++iter)
                {
                    // Originally using random values, but it was recommended to do 2's compliment of the original
                    // data instead.
                    dataBuffer[iter] =
                        M_2sCOMPLEMENT(dataBuffer[iter]); // C_CAST(uint8_t, random_Range_64(0, UINT8_MAX));
                }
                // write it back to the drive
                if (device->drive_info.deviceMaxLba > UINT32_MAX)
                {
                    ret = scsi_Write_Long_16(device, false, false, multipleLogicalPerPhysical, corruptLBA, dataLength,
                                             dataBuffer);
                }
                else
                {
                    ret = scsi_Write_Long_10(device, false, false, multipleLogicalPerPhysical,
                                             C_CAST(uint32_t, corruptLBA), dataLength, dataBuffer);
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
    return ret;
}

eReturnValues corrupt_LBA_Read_Write_Long(tDevice* device, uint64_t corruptLBA, uint16_t numberOfBytesToCorrupt)
{
    eReturnValues ret = NOT_SUPPORTED;
    if (device->drive_info.drive_type == ATA_DRIVE)
    {
        ret = ata_corrupt_LBA_Read_Write_Long(device, corruptLBA, numberOfBytesToCorrupt);
    }
    else if (device->drive_info.drive_type == SCSI_DRIVE)
    {
        ret = scsi_corrupt_LBA_Read_Write_Long(device, corruptLBA, numberOfBytesToCorrupt);
    }
    return ret;
}

eReturnValues corrupt_LBAs(tDevice*                    device,
                           uint64_t                    startingLBA,
                           uint64_t                    range,
                           bool                        readCorruptedLBAs,
                           uint16_t                    numberOfBytesToCorrupt,
                           M_ATTR_UNUSED custom_Update updateFunction,
                           M_ATTR_UNUSED void*         updateData)
{
    eReturnValues ret           = SUCCESS;
    uint64_t      iterator      = UINT64_C(0);
    bool          readWriteLong = is_Read_Long_Write_Long_Supported(device);
    uint16_t      logicalPerPhysicalSectors =
        C_CAST(uint16_t, device->drive_info.devicePhyBlockSize / device->drive_info.deviceBlockSize);
    uint16_t increment = logicalPerPhysicalSectors;
    if (readWriteLong && logicalPerPhysicalSectors != 1 && device->drive_info.drive_type == ATA_DRIVE)
    {
        // changing the increment amount to 1 because the ATA read/write long commands can only do a single LBA at a
        // time.
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
            size_t dataBufSize =
                uint32_to_sizet(device->drive_info.deviceBlockSize) * uint16_to_sizet(logicalPerPhysicalSectors);
            uint8_t* dataBuf = M_REINTERPRET_CAST(
                uint8_t*, safe_calloc_aligned(dataBufSize, sizeof(uint8_t), device->os_info.minimumAlignment));
            if (dataBuf == M_NULLPTR)
            {
                return MEMORY_FAILURE;
            }
            // don't check return status since we expect this to fail after creating the error
            if (device->deviceVerbosity > VERBOSITY_QUIET)
            {
                printf("Reading Corrupted LBA %-20" PRIu64 "\n", iterator);
            }
            read_LBA(device, iterator, false, dataBuf, logicalPerPhysicalSectors * device->drive_info.deviceBlockSize);
            // scsi_Read_16(device, 0, false, false, false, iterator, 0, logicalPerPhysicalSectors, dataBuf);
            safe_free_aligned(&dataBuf);
        }
    }
    return ret;
}

eReturnValues corrupt_Random_LBAs(tDevice*      device,
                                  uint16_t      numberOfRandomLBAs,
                                  bool          readCorruptedLBAs,
                                  uint16_t      numberOfBytesToCorrupt,
                                  custom_Update updateFunction,
                                  void*         updateData)
{
    eReturnValues ret      = SUCCESS;
    uint16_t      iterator = UINT16_C(0);
    seed_64(C_CAST(uint64_t, time(M_NULLPTR))); // start the random number generator
    for (iterator = UINT16_C(0); iterator < numberOfRandomLBAs; ++iterator)
    {
        uint64_t randomLBA = random_Range_64(UINT64_C(0), device->drive_info.deviceMaxLba);
        // align the random LBA to the physical sector
        randomLBA = align_LBA(device, randomLBA);
        // call the function to create an uncorrectable with the range set to 1 so we only corrupt 1 physical block at a
        // time randomly
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
    DISABLE_NONNULL_COMPARE
    if (defectList == M_NULLPTR || numberOfDefects == M_NULLPTR)
    {
        return BAD_PARAMETER;
    }
    RESTORE_NONNULL_COMPARE
    *numberOfDefects              = UINT32_C(0); // set to zero since it will be incremented as we read in the bad LBAs
    uint32_t totalPendingReported = UINT32_C(0);
    bool     validPendingReportedCount = false;
    if (SUCCESS ==
        get_Pending_List_Count(
            device, &totalPendingReported)) // This is useful so we know whether or not to bother reading the log
    {
        validPendingReportedCount = true;
        ret = SUCCESS; // change this to SUCCESS since we know we will get a valid count & list before we read it
    }
    if (totalPendingReported > 0 && validPendingReportedCount)
    {
        uint32_t pendingLogSize = UINT32_C(0);
        get_SCSI_Log_Size(device, LP_PENDING_DEFECTS, 0x01, &pendingLogSize);
        if (pendingLogSize > 0)
        {
            uint8_t* pendingDefectsLog = C_CAST(
                uint8_t*, safe_calloc_aligned(pendingLogSize, sizeof(uint8_t), device->os_info.minimumAlignment));
            if (pendingDefectsLog == M_NULLPTR)
            {
                return MEMORY_FAILURE;
            }
            if (SUCCESS == get_SCSI_Log(device, LP_PENDING_DEFECTS, 0x01, M_NULLPTR, M_NULLPTR, true, pendingDefectsLog,
                                        pendingLogSize, M_NULLPTR))
            {
                // First, validate that we got the right SCSI log page...I've seen some USB devices ignore the subpage
                // code and return the wrong data. - TJE
                if (get_bit_range_uint8(pendingDefectsLog[0], 5, 0) == 0x15 && pendingDefectsLog[0] & BIT6 &&
                    pendingDefectsLog[1] == 0x01)
                {
                    uint16_t pageLength = M_BytesTo2ByteValue(pendingDefectsLog[2],
                                                              pendingDefectsLog[3]); // does not include 4 byte header!
                    if (pageLength > 4)
                    {
                        uint32_t pendingDefectCount =
                            1; // will be set in loop shortly...but use this for now to enter the loop
                        uint8_t  parameterLength = UINT8_C(0);
                        uint32_t offset          = LOG_PAGE_HEADER_LENGTH; // setting to this so we start with the first
                                                                  // parameter we are given...which should be zero
                        for (uint32_t defectCounter = UINT32_C(0);
                             offset < C_CAST(uint32_t, C_CAST(uint32_t, pageLength) + LOG_PAGE_HEADER_LENGTH) &&
                             defectCounter < pendingDefectCount;
                             offset += (parameterLength + 4))
                        {
                            uint16_t parameterCode =
                                M_BytesTo2ByteValue(pendingDefectsLog[offset + 0], pendingDefectsLog[offset + 1]);
                            parameterLength =
                                pendingDefectsLog[offset + 3]; // does not include 4 byte header. The increment in the
                                                               // loop takes this into account
                            if (parameterCode == 0)
                            {
                                // this is the total count in the log
                                pendingDefectCount =
                                    M_BytesTo4ByteValue(pendingDefectsLog[offset + 4], pendingDefectsLog[offset + 5],
                                                        pendingDefectsLog[offset + 6], pendingDefectsLog[offset + 7]);
                            }
                            else if (parameterCode >= 0x0001 && parameterCode <= 0xF000)
                            {
                                // this is a pending defect entry
                                defectList[defectCounter].powerOnHours =
                                    M_BytesTo4ByteValue(pendingDefectsLog[offset + 4], pendingDefectsLog[offset + 5],
                                                        pendingDefectsLog[offset + 6], pendingDefectsLog[offset + 7]);
                                defectList[defectCounter].lba =
                                    M_BytesTo8ByteValue(pendingDefectsLog[offset + 8], pendingDefectsLog[offset + 9],
                                                        pendingDefectsLog[offset + 10], pendingDefectsLog[offset + 11],
                                                        pendingDefectsLog[offset + 12], pendingDefectsLog[offset + 13],
                                                        pendingDefectsLog[offset + 14], pendingDefectsLog[offset + 15]);
                                ++defectCounter;
                                ++(*numberOfDefects);
                            }
                            else
                            {
                                // all other parameters are reserved, so exit
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
    DISABLE_NONNULL_COMPARE
    if (defectList == M_NULLPTR || numberOfDefects == M_NULLPTR)
    {
        return BAD_PARAMETER;
    }
    RESTORE_NONNULL_COMPARE
    *numberOfDefects              = UINT32_C(0); // set to zero since it will be incremented as we read in the bad LBAs
    uint32_t totalPendingReported = UINT32_C(0);
    bool     validPendingReportedCount = false;
    if (SUCCESS ==
        get_Pending_List_Count(
            device, &totalPendingReported)) // This is useful so we know whether or not to bother reading the log
    {
        validPendingReportedCount = true;
        ret = SUCCESS; // change this to SUCCESS since we know we will get a valid count & list before we read it
    }
    if ((totalPendingReported > 0 && validPendingReportedCount) ||
        !validPendingReportedCount) // if we got a valid count and a number greater than zero, we read the list. If we
                                    // didn't get a valid count, then we still read the list...this may be an
                                    // optimization in some cases
    {
        // Check if the ACS pending log is supported and use that INSTEAD of the Seagate log...always use std spec when
        // we can
        uint32_t pendingLogSize = UINT32_C(0);
        get_ATA_Log_Size(device, ATA_LOG_PENDING_DEFECTS_LOG, &pendingLogSize, true, false);
        if (pendingLogSize > 0)
        {
            // ACS Pending List
            uint8_t* pendingList = C_CAST(
                uint8_t*, safe_calloc_aligned(pendingLogSize, sizeof(uint8_t), device->os_info.minimumAlignment));
            if (pendingList == M_NULLPTR)
            {
                return MEMORY_FAILURE;
            }
            if (SUCCESS == get_ATA_Log(device, ATA_LOG_PENDING_DEFECTS_LOG, M_NULLPTR, M_NULLPTR, true, false, true,
                                       pendingList, pendingLogSize, M_NULLPTR, 0, 0))
            {
                uint32_t numberOfDescriptors =
                    M_BytesTo4ByteValue(pendingList[3], pendingList[2], pendingList[1], pendingList[0]);
                for (uint32_t descriptorIter = UINT32_C(0), offset = UINT32_C(16);
                     descriptorIter < numberOfDescriptors && offset < pendingLogSize;
                     ++descriptorIter, offset += UINT32_C(16), ++(*numberOfDefects))
                {
                    defectList[*numberOfDefects].powerOnHours =
                        M_BytesTo4ByteValue(pendingList[3 + offset], pendingList[2 + offset], pendingList[1 + offset],
                                            pendingList[0 + offset]);
                    defectList[*numberOfDefects].lba = M_BytesTo8ByteValue(
                        pendingList[15 + offset], pendingList[14 + offset], pendingList[13 + offset],
                        pendingList[12 + offset], pendingList[11 + offset], pendingList[10 + offset],
                        pendingList[9 + offset], pendingList[8 + offset]);
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
    print_str("Pending Defects:\n");
    print_str("================\n");
    if (numberOfItemsInPendingList > UINT32_C(0))
    {
        print_str(" #\tLBA\t\t\tTimestamp\n");
        for (uint32_t pendingListIter = UINT32_C(0); pendingListIter < numberOfItemsInPendingList; ++pendingListIter)
        {
            printf("%" PRIu32 "\t%" PRIu64 "\t\t\t%" PRIu32 "\n", pendingListIter, pendingList[pendingListIter].lba,
                   pendingList[pendingListIter].powerOnHours);
        }
    }
    else
    {
        print_str("No items in pending defect list.\n");
    }
}

eReturnValues get_SCSI_Background_Scan_Results(tDevice* device, ptrBackgroundResults results, uint16_t* numberOfResults)
{
    eReturnValues ret = NOT_SUPPORTED;
    DISABLE_NONNULL_COMPARE
    if (results == M_NULLPTR || numberOfResults == M_NULLPTR)
    {
        return BAD_PARAMETER;
    }
    RESTORE_NONNULL_COMPARE
    *numberOfResults                     = UINT32_C(0);
    uint32_t backgroundScanResultsLength = UINT32_C(0);
    if (SUCCESS == get_SCSI_Log_Size(device, LP_BACKGROUND_SCAN_RESULTS, 0, &backgroundScanResultsLength))
    {
        if (backgroundScanResultsLength > 0)
        {
            // now allocate memory and read it
            uint8_t* backgroundScanResults =
                M_REINTERPRET_CAST(uint8_t*, safe_calloc_aligned(backgroundScanResultsLength, sizeof(uint8_t),
                                                                 device->os_info.minimumAlignment));
            if (backgroundScanResults == M_NULLPTR)
            {
                return MEMORY_FAILURE;
            }
            if (SUCCESS == get_SCSI_Log(device, LP_BACKGROUND_SCAN_RESULTS, 0, M_NULLPTR, M_NULLPTR, true,
                                        backgroundScanResults, backgroundScanResultsLength, M_NULLPTR))
            {
                uint16_t parameterCode   = UINT16_C(0);
                uint8_t  parameterLength = UINT8_C(0);
                for (uint32_t offset = LOG_PAGE_HEADER_LENGTH;
                     offset < backgroundScanResultsLength && *numberOfResults < MAX_BACKGROUND_SCAN_RESULTS;
                     offset += parameterLength + LOG_PAGE_HEADER_LENGTH)
                {
                    parameterCode =
                        M_BytesTo2ByteValue(backgroundScanResults[offset + 0], backgroundScanResults[offset + 1]);
                    parameterLength = backgroundScanResults[offset + 3];
                    if (parameterCode == 0)
                    {
                        // status parameter...don't need anything from here right now
                        continue;
                    }
                    else if (parameterCode >= 0x0001 && parameterCode <= 0x0800)
                    {
                        // result entry.
                        results[*numberOfResults].accumulatedPowerOnMinutes =
                            M_BytesTo4ByteValue(backgroundScanResults[offset + 4], backgroundScanResults[offset + 5],
                                                backgroundScanResults[offset + 6], backgroundScanResults[offset + 7]);
                        results[*numberOfResults].lba =
                            M_BytesTo8ByteValue(backgroundScanResults[offset + 16], backgroundScanResults[offset + 17],
                                                backgroundScanResults[offset + 18], backgroundScanResults[offset + 19],
                                                backgroundScanResults[offset + 20], backgroundScanResults[offset + 21],
                                                backgroundScanResults[offset + 22], backgroundScanResults[offset + 23]);
                        results[*numberOfResults].reassignStatus      = M_Nibble1(backgroundScanResults[offset + 8]);
                        results[*numberOfResults].senseKey            = M_Nibble0(backgroundScanResults[offset + 8]);
                        results[*numberOfResults].additionalSenseCode = backgroundScanResults[offset + 9];
                        results[*numberOfResults].additionalSenseCodeQualifier = backgroundScanResults[offset + 10];
                        ++(*numberOfResults);
                    }
                    else
                    {
                        // reserved or vendor specific parameter...just exit
                        break;
                    }
                }
                ret = SUCCESS;
            }
            else
            {
                ret = FAILURE;
            }
            safe_free_aligned_core(C_CAST(void**, &backgroundScanResults));
        }
    }
    return ret;
}

eReturnValues get_LBAs_From_SCSI_Background_Scan_Log(tDevice*         device,
                                                     ptrPendingDefect defectList,
                                                     uint32_t*        numberOfDefects)
{
    eReturnValues ret = NOT_SUPPORTED;
    DISABLE_NONNULL_COMPARE
    if (defectList == M_NULLPTR || numberOfDefects == M_NULLPTR)
    {
        return BAD_PARAMETER;
    }
    RESTORE_NONNULL_COMPARE
    if (device->drive_info.drive_type == ATA_DRIVE)
    {
        return ret;
    }
    *numberOfDefects = UINT32_C(0);
    ptrBackgroundResults bmsResults =
        M_REINTERPRET_CAST(ptrBackgroundResults, safe_malloc(sizeof(backgroundResults) * MAX_BACKGROUND_SCAN_RESULTS));
    if (bmsResults == M_NULLPTR)
    {
        return MEMORY_FAILURE;
    }
    safe_memset(bmsResults, sizeof(backgroundResults) * MAX_BACKGROUND_SCAN_RESULTS, 0,
                sizeof(backgroundResults) * MAX_BACKGROUND_SCAN_RESULTS);
    uint16_t numberOfBMSResults = UINT16_C(0);
    ret                         = get_SCSI_Background_Scan_Results(device, bmsResults, &numberOfBMSResults);
    if (ret == SUCCESS)
    {
        for (uint16_t bmsIter = UINT16_C(0); bmsIter < numberOfBMSResults; ++bmsIter)
        {
            defectList[*numberOfDefects].lba = bmsResults[bmsIter].lba;
            defectList[*numberOfDefects].powerOnHours =
                C_CAST(uint32_t, bmsResults[bmsIter].accumulatedPowerOnMinutes / UINT64_C(60));
            ++(*numberOfDefects);
        }
    }
    safe_free_background_results(&bmsResults);
    return ret;
}

// Defect list for this should be at least MAX_DST_ENTRIES in size
eReturnValues get_LBAs_From_DST_Log(tDevice* device, ptrPendingDefect defectList, uint32_t* numberOfDefects)
{
    eReturnValues ret = NOT_SUPPORTED;
    DISABLE_NONNULL_COMPARE
    if (defectList == M_NULLPTR || numberOfDefects == M_NULLPTR)
    {
        return BAD_PARAMETER;
    }
    RESTORE_NONNULL_COMPARE
    *numberOfDefects = UINT32_C(0);
    dstLogEntries dstEntries;
    safe_memset(&dstEntries, sizeof(dstLogEntries), 0, sizeof(dstLogEntries));
    ret = get_DST_Log_Entries(device, &dstEntries);
    if (ret == SUCCESS)
    {
        // Got the DST log entries, so we need to find error LBAs from read element failures to add that to the list
        for (uint8_t dstIter = UINT8_C(0); dstIter < dstEntries.numberOfEntries; ++dstIter)
        {
            if (dstEntries.dstEntry[dstIter].descriptorValid)
            {
                switch (M_Nibble1(dstEntries.dstEntry[dstIter].selfTestExecutionStatus))
                {
                case 0x07: // read element failure
                    defectList[*numberOfDefects].lba = dstEntries.dstEntry[dstIter].lbaOfFailure;
                    if (dstEntries.dstEntry[dstIter].lifetimeTimestamp > UINT32_MAX)
                    {
                        defectList[*numberOfDefects].powerOnHours = UINT32_MAX;
                    }
                    else
                    {
                        defectList[*numberOfDefects].powerOnHours =
                            C_CAST(uint32_t, dstEntries.dstEntry[dstIter].lifetimeTimestamp);
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
