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
// \file sas_phy.c
// \brief This file holds options to do things on a SAS phy.

#include "bit_manip.h"
#include "code_attributes.h"
#include "common_types.h"
#include "error_translation.h"
#include "io_utils.h"
#include "math_utils.h"
#include "memory_safety.h"
#include "precision_timer.h"
#include "string_utils.h"
#include "type_conversion.h"

#include "sas_phy.h"

bool is_SAS_Phy_Diagnostic_Page_Supported(tDevice* device)
{
    DECLARE_ZERO_INIT_ARRAY(uint8_t, supportedDiagnosticPages, 50);
    if (SUCCESS == scsi_Send_Diagnostic(device, 0, 1, 0, 0, 0, 50, supportedDiagnosticPages, 50, 15) &&
        SUCCESS == scsi_Receive_Diagnostic_Results(device, true, 0x00, 50, supportedDiagnosticPages, 15))
    {
        // check that page 3F is supported.
        uint16_t pageLength = M_BytesTo2ByteValue(supportedDiagnosticPages[2], supportedDiagnosticPages[3]);
        for (uint32_t iter = UINT32_C(4); iter < C_CAST(uint32_t, pageLength + UINT16_C(4)) && iter < UINT16_C(50);
             ++iter)
        {
            switch (supportedDiagnosticPages[iter])
            {
            case DIAG_PAGE_PROTOCOL_SPECIFIC:
                return true;
            default:
                break;
            }
        }
    }
    return false;
}

static eReturnValues build_SAS_SSP_Diagnostic_Page(uint8_t                 diagPage[32],
                                                   uint8_t                 phyIdentifier,
                                                   eSASPhyTestFunction     testFunction,
                                                   eSASPhyTestPattern      pattern,
                                                   bool                    sataTestFunction,
                                                   eSASPhyTestFunctionSSC  testFunctionSSC,
                                                   eSASPhyPhysicalLinkRate linkRate,
                                                   eSASPhyDwordControl     dwordControl,
                                                   uint64_t                phyTestPatternDwords)
{
    eReturnValues ret = SUCCESS;
    if (diagPage == M_NULLPTR)
    {
        return BAD_PARAMETER;
    }
    diagPage[0] = DIAG_PAGE_PROTOCOL_SPECIFIC;
    diagPage[1] = 0x06; // protocol identifier = 6 for SAS
    diagPage[2] = 0x00;
    diagPage[3] = 0x1C; // see SPL
    diagPage[4] = phyIdentifier;
    diagPage[5] = C_CAST(uint8_t, testFunction);
    diagPage[6] = C_CAST(uint8_t, pattern);
    // link rate
    diagPage[7] = C_CAST(uint8_t, linkRate);
    // phy test function ssc
    diagPage[7] |= C_CAST(uint8_t, testFunctionSSC << 4);
    // phy test function SATA
    if (sataTestFunction)
    {
        diagPage[7] |= BIT6;
    }
    diagPage[8]  = RESERVED;
    diagPage[9]  = RESERVED;
    diagPage[10] = RESERVED;
    diagPage[11] = C_CAST(uint8_t, dwordControl);
    diagPage[12] = M_Byte7(phyTestPatternDwords);
    diagPage[13] = M_Byte6(phyTestPatternDwords);
    diagPage[14] = M_Byte5(phyTestPatternDwords);
    diagPage[15] = M_Byte4(phyTestPatternDwords);
    diagPage[16] = M_Byte3(phyTestPatternDwords);
    diagPage[17] = M_Byte2(phyTestPatternDwords);
    diagPage[18] = M_Byte1(phyTestPatternDwords);
    diagPage[19] = M_Byte0(phyTestPatternDwords);
    diagPage[20] = RESERVED;
    diagPage[21] = RESERVED;
    diagPage[22] = RESERVED;
    diagPage[23] = RESERVED;
    diagPage[24] = RESERVED;
    diagPage[25] = RESERVED;
    diagPage[26] = RESERVED;
    diagPage[27] = RESERVED;
    diagPage[28] = RESERVED;
    diagPage[29] = RESERVED;
    diagPage[30] = RESERVED;
    diagPage[31] = RESERVED;
    return ret;
}

eReturnValues start_SAS_Test_Pattern(tDevice*                device,
                                     uint8_t                 phyIdentifier,
                                     eSASPhyTestPattern      pattern,
                                     bool                    sataTestFunction,
                                     eSASPhyTestFunctionSSC  testFunctionSSC,
                                     eSASPhyPhysicalLinkRate linkRate,
                                     eSASPhyDwordControl     dwordControl,
                                     uint64_t                phyTestPatternDwords)
{
    eReturnValues ret = SUCCESS;
    DECLARE_ZERO_INIT_ARRAY(uint8_t, sasDiagPage, 32);
    ret =
        build_SAS_SSP_Diagnostic_Page(sasDiagPage, phyIdentifier, SAS_PHY_FUNC_TRANSMIT_PATTERN, pattern,
                                      sataTestFunction, testFunctionSSC, linkRate, dwordControl, phyTestPatternDwords);
    if (ret == SUCCESS)
    {
        ret = scsi_Send_Diagnostic(device, 0, 1, 0, 0, 0, 32, sasDiagPage, 32, 15);
    }
    return ret;
}

eReturnValues stop_SAS_Test_Pattern(tDevice* device, uint8_t phyIdentifier, eSASPhyPhysicalLinkRate linkRate)
{
    eReturnValues ret = SUCCESS;
    DECLARE_ZERO_INIT_ARRAY(uint8_t, sasDiagPage, 32);
    ret = build_SAS_SSP_Diagnostic_Page(
        sasDiagPage, phyIdentifier, SAS_PHY_FUNC_STOP, 0, false, 0, linkRate, 0,
        0); // I'm assuming the stop command doesn't need to specify anything else that matches the running test. - TJE
    if (ret == SUCCESS)
    {
        ret = scsi_Send_Diagnostic(device, 0, 1, 0, 0, 0, 32, sasDiagPage, 32, 15);
    }
    return ret;
}
