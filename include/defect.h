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

#pragma once

#include "operations_Common.h"

#if defined(__cplusplus)
extern "C" {
#endif

    typedef struct _blockFormatAddress //used by short and long descriptors
    {
        union
        {
            uint32_t shortBlockAddress;
            uint64_t longBlockAddress;
        };
        uint8_t pad[2];//extra pad bytes for union below
    }blockFormatAddress;

    typedef struct _bytesFromIndexAddress //used by standard and extended
    {
        uint32_t cylinderNumber;
        uint8_t headNumber;
        uint32_t bytesFromIndex;
        bool multiAddressDescriptorStart;//only valid in extended bytes from index address format
    }bytesFromIndexAddress;

    typedef struct _physicalSectorAddress
    {
        uint32_t cylinderNumber;
        uint8_t headNumber;
        uint32_t sectorNumber;
        bool multiAddressDescriptorStart;//only valid in extended physical sector address format
    }physicalSectorAddress;

    typedef struct _scsiDefectList
    {
        eSCSIAddressDescriptors format;
        uint32_t numberOfElements;//number of things stored in the list in the format mentioned above
        bool containsPrimaryList;
        bool containsGrownList;
        uint16_t generation;//0 is an invalid or not supported generation code. Should be 1 through FFFFh
        bool overflow;//This will be set if the defect list length is so long it cannot be read entirely, or if it can only be read in one command and is too big of a transfer for the host to handle. (>128k in size for Windows)
        union
        {
            blockFormatAddress block[1];
            bytesFromIndexAddress bfi[1];
            physicalSectorAddress physical[1];
        };
    }scsiDefectList, *ptrSCSIDefectList;

    //This function will allocate the defect list!
    int get_SCSI_Defect_List(tDevice *device, eSCSIAddressDescriptors defectListFormat, bool grownList, bool primaryList, scsiDefectList **defects);

    void free_Defect_List(scsiDefectList **defects);


#if defined(__cplusplus)
}
#endif