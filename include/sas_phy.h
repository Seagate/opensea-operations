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
// \file sas_phy.h
// \brief This file holds options to do things on a SAS phy.

#pragma once

#if defined (__cplusplus)
extern "C"
{
#endif

    #include "operations_Common.h"
    #include "scsi_helper.h"
    #include "scsi_helper_func.h"

    //checks if it is SAS protocol and Diagnostic page 3F is supported
    OPENSEA_OPERATIONS_API bool is_SAS_Phy_Diagnostic_Page_Supported(tDevice *device);

    typedef enum _eSASPhyTestPattern
    {
        SAS_PHY_PATTERN_RESERVED            = 0x00,
        SAS_PHY_PATTERN_JTPAT               = 0x01,
        SAS_PHY_PATTERN_CJTPAT              = 0x02,
        SAS_PHY_PATTERN_PRBS9               = 0x03,
        SAS_PHY_PATTERN_PRRBS15             = 0x04,
        //05 - 0F are reserved
        SAS_PHY_PATTERN_TRAIN               = 0x10,
        SAS_PHY_PATTERN_TRAIN_DONE          = 0x11,
        SAS_PHY_PATTERN_IDLE                = 0x12,
        SAS_PHY_PATTERN_SCRAMBLED_0         = 0x13,
        //14 - 3F are reserved
        SAS_PHY_PATTERN_TWO_DWORDS          = 0x40,
        //41 - EF are reserved
        //F0 - FF : Vendor Specific
        SAS_PHY_PATTERN_BEGIN_VENDOR_UNIQUE = 0xF0,
    }eSASPhyTestPattern;

    typedef enum _eSASPhyTestFunction
    {
        SAS_PHY_FUNC_STOP = 0x00,
        SAS_PHY_FUNC_TRANSMIT_PATTERN = 0x01,
        //02 - EF are reserved
        //D0 - FF are vendor specific
    }eSASPhyTestFunction;

    typedef enum _eSASPhyTestFunctionSSC
    {
        SAS_PHY_SSC_NO_SPREADING = 0x00,
        SAS_PHY_SSC_CENTER_SPREADING = 0x01,
        SAS_PHY_SSC_DOWN_SPREADING = 0x02,
        SAS_PHY_SSC_RESERVED = 0x03
    }eSASPhyTestFunctionSSC;

    typedef enum _eSASPhyPhysicalLinkRate
    {
        SAS_PHY_LINK_RATE_1_5G = 0x08,
        SAS_PHY_LINK_RATE_3G = 0x09,
        SAS_PHY_LINK_RATE_6G = 0x0A,
        SAS_PHY_LINK_RATE_12G = 0x0B,
        SAS_PHY_LINK_RATE_22_5G = 0x0C
    }eSASPhyPhysicalLinkRate;

    typedef enum _eSASPhyDwordControl
    {
        SAS_PHY_DWORD_CONTROL_DATA_CHARACTER_NO_SCRAMBLING = 0x00,
        SAS_PHY_DWORD_CONTROL_5TH_BYTE_CONTROL_CHARACTER_NO_SCRAMBLING = 0x08,
        SAS_PHY_DWORD_CONTROL_1ST_BYTE_CONTROL_CHARACTER_NO_SCRAMBLING = 0x80,
        SAS_PHY_DWORD_CONTROL_1ST_AND_5TH_BYTE_CONTROL_CHARACTER_NO_SCRAMBLING = 0x88,
        //all others are reserved
    }eSASPhyDwordControl;

    #define PHY_TEST_PATTERN_DWORD_D10_2            UINT64_C(0x4A4A4A4A4A4A4A4A)
    #define PHY_TEST_PATTERN_DWORD_D21_5            UINT64_C(0xB5B5B5B5B5B5B5B5)
    #define PHY_TEST_PATTERN_DWORD_D24_3            UINT64_C(0x7878787878787878)
    #define PHY_TEST_PATTERN_DWORD_D25_6_AND_D6_1   UINT64_C(0xD926D926D926D926)
    #define PHY_TEST_PATTERN_DWORD_D30_3            UINT64_C(0x7E7E7E7E7E7E7E7E)
    #define PHY_TEST_PATTERN_DWORD_ALIGN_0          UINT64_C(0xBC4A4A7BBC4A4A7B)
    #define PHY_TEST_PATTERN_DWORD_ALIGN_1          UINT64_C(0xBC070707BC070707)
    #define PHY_TEST_PATTERN_DWORD_PAIR_ALIGN_0     UINT64_C(0xBC4A4A7B4A787E7E)

    //Takes all the inputs to start a test pattern
    OPENSEA_OPERATIONS_API int start_SAS_Test_Pattern(tDevice *device, uint8_t phyIdentifier, eSASPhyTestPattern pattern, bool sataTestFunction, eSASPhyTestFunctionSSC testFunctionSSC, eSASPhyPhysicalLinkRate linkRate, eSASPhyDwordControl dwordControl, uint64_t phyTestPatternDwords);

    //will stop a test pattern on a specified phy
    OPENSEA_OPERATIONS_API int stop_SAS_Test_Pattern(tDevice *device, uint8_t phyIdentifier, eSASPhyPhysicalLinkRate linkRate);

#if defined (__cplusplus)
}
#endif
