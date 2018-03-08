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
// \file depopulate.h
// \brief This file defines the functions for depopulating physical/storage elements on a drive (Remanufacture)

#pragma once

#include "operations_Common.h"

#if defined(__cplusplus)
extern "C" {
#endif

    OPENSEA_OPERATIONS_API bool is_Depopulation_Feature_Supported(tDevice *device, uint64_t *depopulationTime);

    OPENSEA_OPERATIONS_API int get_Number_Of_Descriptors(tDevice *device, uint32_t *numberOfDescriptors);

    typedef enum _ePhysicalElementType
    {
        PHYSICAL_ELEMENT_RESERVED = 0,
        PHYSICAL_ELEMENT_STORAGE_ELEMENT = 1,
    }ePhysicalElementType;

    typedef struct _physicalElement
    {
        uint32_t elementIdentifier;
        ePhysicalElementType elementType;
        uint8_t elementHealth;
        uint64_t associatedCapacity;
    }physicalElement, *ptrPhysicalElement;

    OPENSEA_OPERATIONS_API int get_Physical_Element_Descriptors(tDevice *device, uint32_t numberOfElementsExpected, ptrPhysicalElement elementList);

    OPENSEA_OPERATIONS_API void show_Physical_Element_Descriptors(uint32_t numberOfElements, ptrPhysicalElement elementList, uint64_t depopulateTime);//this doesn't need the seagateVU flag since the get function will mask the Seagate methods output into something this can print

    //requested max LBA can be zero to let the drive decide. NOTE: This is irrelevant in the Seagate VU method
    OPENSEA_OPERATIONS_API int depopulate_Physical_Element(tDevice *device, uint32_t elementDescriptorID, uint64_t requestedMaxLBA);

#if defined(__cplusplus)
}
#endif