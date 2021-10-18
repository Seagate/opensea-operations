//
// Do NOT modify or remove this copyright and license
//
// Copyright (c) 2012-2021 Seagate Technology LLC and/or its Affiliates, All Rights Reserved
//
// This software is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
//
// ******************************************************************************************
// 
// \file zoned_operations.h
// \brief This file defines various zoned device operations.

#pragma once

#include "operations_Common.h"
#include "common_public.h"

#if defined (__cplusplus)
extern "C"
{
#endif

    OPENSEA_OPERATIONS_API int get_Number_Of_Zones(tDevice *device, eZoneReportingOptions reportingOptions, uint64_t startingLBA, uint32_t *numberOfMatchingZones);

    typedef enum _eZoneType
    {
        ZONE_TYPE_RESERVED = 0,
        ZONE_TYPE_CONVENTIONAL = 1,
        ZONE_TYPE_SEQUENTIAL_WRITE_REQUIRED = 2,
        ZONE_TYPE_SEQUENTIAL_WRITE_PREFERRED = 3,
    }eZoneType;

    typedef enum _eZoneCondition
    {
        ZONE_CONDITION_NOT_WRITE_POINTER = 0,
        ZONE_CONDITION_EMPTY = 1,
        ZONE_CONDITION_IMLICITLY_OPENED = 2,
        ZONE_CONDITION_EXPLICITYLE_OPENED = 3,
        ZONE_CONDITION_CLOSED = 4,
        ZONE_CONDITION_READ_ONLY = 0xD,
        ZONE_CONDITION_FULL = 0xE,
        ZONE_CONDITION_OFFLINE = 0xF
    }eZoneCondition;

    typedef struct _zoneDescriptor 
    {
        bool descriptorValid;
        eZoneType zoneType;
        eZoneCondition zoneCondition;
        bool nonseqBit;
        bool resetBit;
        uint64_t zoneLength;
        uint64_t zoneStartingLBA;
        uint64_t writePointerLBA;
    }zoneDescriptor, *ptrZoneDescriptor;

    OPENSEA_OPERATIONS_API int get_Zone_Descriptors(tDevice *device, eZoneReportingOptions reportingOptions, uint64_t startingLBA, uint32_t numberOfZoneDescriptors, ptrZoneDescriptor zoneDescriptors);

    //eZoneReportingOptions reportingOptions is used to print the header saying which zones we are showing (all, some, etc)
    OPENSEA_OPERATIONS_API void print_Zone_Descriptors(eZoneReportingOptions reportingOptions, uint32_t numberOfZoneDescriptors, ptrZoneDescriptor zoneDescriptors);

#if defined (__cplusplus)
}
#endif
