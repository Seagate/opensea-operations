// SPDX-License-Identifier: MPL-2.0
//
// Do NOT modify or remove this copyright and license
//
// Copyright (c) 2024-2024 Seagate Technology LLC and/or its Affiliates, All Rights Reserved
//
// This software is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
//
// ******************************************************************************************

// \file sata_phy.h
// \brief functions to help with configuring or reading info about the SATA PHY

#pragma once
#if defined (__cplusplus)
extern "C" {
#endif //__cplusplus

#include "common_types.h"
#include "operations_Common.h"

typedef enum _eSATA_Phy_Event_ID
{
    SATA_PHY_EVENT_NONE                                         = 0x000,
    SATA_PHY_EVENT_COMMAND_ICRC                                 = 0x001,
    SATA_PHY_EVENT_R_ERR_RESPONSE_FOR_DATA_FIS                  = 0x002,
    SATA_PHY_EVENT_R_ERR_RESPONSE_FOR_D2H_DATA_FIS              = 0x003,
    SATA_PHY_EVENT_R_ERR_RESPONSE_FOR_H2D_DATA_FIS              = 0x004,
    SATA_PHY_EVENT_R_ERR_RESPONSE_FOR_NON_DATA_FIS              = 0x005,
    SATA_PHY_EVENT_R_ERR_RESPONSE_FOR_D2H_NON_DATA_FIS          = 0x006,
    SATA_PHY_EVENT_R_ERR_RESPONSE_FOR_H2D_NON_DATA_FIS          = 0x007,
    SATA_PHY_EVENT_D2H_NON_DATA_FIS_RETRIES                     = 0x008,
    SATA_PHY_EVENT_TRANSITIONS_FROM_PHYRDY_2_PHYRDYN            = 0x009,
    SATA_PHY_EVENT_H2D_FISES_SENT_DUE_TO_COMRESET               = 0x00A,
    SATA_PHY_EVENT_CRC_ERRORS_WITHIN_H2D_FIS                    = 0x00B,
    SATA_PHY_EVENT_NON_CRC_ERRORS_WITHIN_H2D_FIS                = 0x00D,
    SATA_PHY_EVENT_R_ERR_RESPONSE_H2D_DATA_FIS_CRC              = 0x00F,
    SATA_PHY_EVENT_R_ERR_RESPONSE_H2D_DATA_FIS_NONCRC           = 0x010,
    SATA_PHY_EVENT_R_ERR_RESPONSE_H2D_NONDATA_FIS_CRC           = 0x012,
    SATA_PHY_EVENT_R_ERR_RESPONSE_H2D_NONDATA_FIS_NONCRC        = 0x013,
    SATA_PHY_EVENT_PM_H2D_NONDATA_FIS_R_ERR_END_STAT_COLLISION  = 0xC00,
    SATA_PHY_EVENT_PM_SIGNATURE_REGISTER_D2H_FISES              = 0xC01,
    SATA_PHY_EVENT_PM_CORRUPT_CRC_PROPAGATION_D2H_FISES         = 0xC02,
}eSATA_Phy_Event_ID;

#define VENDOR_SPECIFIC_PHY_EVENT_ID_CHECK BIT15

typedef struct _phyEventCounter
{
    bool vendorUnique;//true if the counter is a vendor unique definition
    uint16_t eventID;//NOTE: This is ONLY the ID. Bits 11:0. The others are removed
    uint16_t rawID;//the raw ID value including vendor unique bit and lenth bits.
    uint64_t counterMaxValue;//use this to check if a counter has hit the maximum value it can hold or not.
    uint64_t counterValue;
}phyEventCounter;

//This should be enought to read standard and vendor counters, but can be adjusted if needed.
#define MAX_PHY_EVENT_COUNTERS UINT8_C(32)

typedef struct _sataPhyEventCounters
{
    bool valid;//must be true for any other data to have meaning in this structure.
    bool validChecksumReceived;//if this is false, then the following data may be corrupt, but some of it may be valid.
    uint8_t numberOfCounters;//number of counters in the following array
    phyEventCounter counters[MAX_PHY_EVENT_COUNTERS];
}sataPhyEventCounters, *ptrSATAPhyEventCounters;

OPENSEA_OPERATIONS_API eReturnValues get_SATA_Phy_Event_Counters(tDevice* device, ptrSATAPhyEventCounters counters);

OPENSEA_OPERATIONS_API void print_SATA_Phy_Event_Counters(ptrSATAPhyEventCounters counters);

#if defined (__cplusplus)
}
#endif //__cplusplus
