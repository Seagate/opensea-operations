// SPDX-License-Identifier: MPL-2.0
//
// Do NOT modify or remove this copyright and license
//
// Copyright (c) 2024-2025 Seagate Technology LLC and/or its Affiliates, All Rights Reserved
//
// This software is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
//
// ******************************************************************************************

// \file sata_phy.c
// \brief functions to help with configuring or reading info about the SATA PHY

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

#include "ata_helper.h"
#include "ata_helper_func.h"
#include "sata_phy.h"

M_NONNULL_PARAM_LIST(1, 2)
M_PARAM_WO(1)
static M_INLINE void fill_SATA_Phy_Events_To_Structure(ptrSATAPhyEventCounters counters,
                                                       uint8_t*                phyEventLog,
                                                       uint32_t                dataLength)
{
    DISABLE_NONNULL_COMPARE
    if (counters != M_NULLPTR && phyEventLog != M_NULLPTR && dataLength >= ATA_LOG_PAGE_LEN_BYTES)
    {
        uint32_t firstInvalidSector = UINT32_C(0);
        counters->valid             = true;
        if (!is_Checksum_Valid(phyEventLog, dataLength, &firstInvalidSector))
        {
            counters->validChecksumReceived = false;
        }
        else
        {
            counters->validChecksumReceived = true;
        }
        uint8_t counterLength      = UINT8_C(0);
        counters->numberOfCounters = 0;
        for (uint16_t offset = UINT16_C(4); offset < UINT16_C(512);
             offset += C_CAST(uint16_t, UINT16_C(2) + counterLength))
        {
            counters->counters[counters->numberOfCounters].rawID =
                M_BytesTo2ByteValue(phyEventLog[offset + 1], phyEventLog[offset]);
            if (counters->counters[counters->numberOfCounters].rawID == 0)
            {
                // subtract one since this counter was not valid
                counters->numberOfCounters -= 1;
                break;
            }
            uint16_t counterBits = get_bit_range_uint16(counters->counters[counters->numberOfCounters].rawID, 14, 12);
            counters->counters[counters->numberOfCounters].eventID =
                get_bit_range_uint16(counters->counters[counters->numberOfCounters].rawID, 11, 0);
            if (counters->counters[counters->numberOfCounters].rawID & BIT15)
            {
                counters->counters[counters->numberOfCounters].vendorUnique = true;
            }
            switch (counterBits)
            {
            case 1: // 16 bits
                counters->counters[counters->numberOfCounters].counterMaxValue = UINT16_MAX;
                counters->counters[counters->numberOfCounters].counterValue =
                    M_BytesTo2ByteValue(phyEventLog[offset + 3], phyEventLog[offset + 2]);
                counterLength = 2;
                break;
            case 2: // 32 bits
                counters->counters[counters->numberOfCounters].counterMaxValue = UINT32_MAX;
                counters->counters[counters->numberOfCounters].counterValue    = M_BytesTo4ByteValue(
                    phyEventLog[offset + 5], phyEventLog[offset + 4], phyEventLog[offset + 3], phyEventLog[offset + 2]);
                counterLength = 4;
                break;
            case 3: // 48 bits
                counters->counters[counters->numberOfCounters].counterMaxValue = MAX_48_BIT_LBA;
                counters->counters[counters->numberOfCounters].counterValue =
                    M_BytesTo8ByteValue(0, 0, phyEventLog[offset + 7], phyEventLog[offset + 6], phyEventLog[offset + 5],
                                        phyEventLog[offset + 4], phyEventLog[offset + 3], phyEventLog[offset + 2]);
                counterLength = 6;
                break;
            case 4: // 64 bits
                counters->counters[counters->numberOfCounters].counterMaxValue = UINT64_MAX;
                counters->counters[counters->numberOfCounters].counterValue    = M_BytesTo8ByteValue(
                    phyEventLog[offset + 9], phyEventLog[offset + 8], phyEventLog[offset + 7], phyEventLog[offset + 6],
                    phyEventLog[offset + 5], phyEventLog[offset + 4], phyEventLog[offset + 3], phyEventLog[offset + 2]);
                counterLength = 8;
                break;
            default:
                // unknown counter length. Cannot handle this at this time.
                counterLength = 0;
                break;
            }
            if (counterLength == 0)
            {
                break;
            }
            counters->numberOfCounters += 1;
        }
    }
    RESTORE_NONNULL_COMPARE
}

eReturnValues reinitialize_SATA_Phy_Event_Counters(tDevice* device, ptrSATAPhyEventCounters counters /* optional */)
{
    eReturnValues ret = NOT_SUPPORTED;
    DISABLE_NONNULL_COMPARE
    if (device == M_NULLPTR)
    {
        return BAD_PARAMETER;
    }
    RESTORE_NONNULL_COMPARE
    if (device->drive_info.drive_type == ATA_DRIVE)
    {
        if (is_ATA_Identify_Word_Valid_SATA(le16_to_host(device->drive_info.IdentifyData.ata.Word076)) &&
            le16_to_host(device->drive_info.IdentifyData.ata.Word076) & BIT10)
        {
            DECLARE_ZERO_INIT_ARRAY(uint8_t, phyEventLog, ATA_LOG_PAGE_LEN_BYTES);
            ret = send_ATA_Read_Log_Ext_Cmd(device, ATA_LOG_SATA_PHY_EVENT_COUNTERS_LOG, 0, phyEventLog,
                                            ATA_LOG_PAGE_LEN_BYTES, ATA_SATA_PHY_EVENT_LOG_READ_AND_REINITIALIZE_FEAT);
            if (SUCCESS == ret || WARN_INVALID_CHECKSUM == ret)
            {
                ret = SUCCESS;
                if (counters != M_NULLPTR)
                {
                    fill_SATA_Phy_Events_To_Structure(counters, phyEventLog, ATA_LOG_PAGE_LEN_BYTES);
                }
            }
        }
    }
    return ret;
}

eReturnValues get_SATA_Phy_Event_Counters(tDevice* device, ptrSATAPhyEventCounters counters)
{
    eReturnValues ret = NOT_SUPPORTED;
    DISABLE_NONNULL_COMPARE
    if (device == M_NULLPTR || counters == M_NULLPTR)
    {
        return BAD_PARAMETER;
    }
    RESTORE_NONNULL_COMPARE
    if (device->drive_info.drive_type == ATA_DRIVE)
    {
        // check the ID bits that show this is supported, then just read the page.
        // SATA defines this as 512B and no more.
        if (is_ATA_Identify_Word_Valid_SATA(le16_to_host(device->drive_info.IdentifyData.ata.Word076)) &&
            le16_to_host(device->drive_info.IdentifyData.ata.Word076) & BIT10)
        {
            DECLARE_ZERO_INIT_ARRAY(uint8_t, phyEventLog, ATA_LOG_PAGE_LEN_BYTES);
            ret = send_ATA_Read_Log_Ext_Cmd(device, ATA_LOG_SATA_PHY_EVENT_COUNTERS_LOG, 0, phyEventLog,
                                            ATA_LOG_PAGE_LEN_BYTES, 0);
            if (SUCCESS == ret || WARN_INVALID_CHECKSUM == ret)
            {
                fill_SATA_Phy_Events_To_Structure(counters, phyEventLog, ATA_LOG_PAGE_LEN_BYTES);
                ret = SUCCESS;
            }
        }
    }
    return ret;
}

void print_SATA_Phy_Event_Counters(ptrSATAPhyEventCounters counters)
{
    DISABLE_NONNULL_COMPARE
    if (counters != M_NULLPTR && counters->valid)
    {
        print_str("\n====SATA Phy Event Counters====\n");
        print_str("V = Vendor Unique event tracker\n");
        print_str("M = Counter maximum value reached\n");
        print_str("D2H = Device to Host\n");
        print_str("H2D = Host to Device\n");
        // Figure out how to keep names of each event short before printing them out. -TJE

        print_str("    ID                Value Description\n");
        for (uint16_t iter = UINT16_C(0); iter < counters->numberOfCounters; ++iter)
        {
            char vendorEvent = ' ';
            char maxedCount  = ' ';
#define PHY_COUNTER_DESCRIPTION_LEN 56
            DECLARE_ZERO_INIT_ARRAY(char, counterDescription, PHY_COUNTER_DESCRIPTION_LEN);
            if (counters->counters[iter].vendorUnique)
            {
                vendorEvent = 'V';
                snprintf_err_handle(counterDescription, PHY_COUNTER_DESCRIPTION_LEN,
                                    "Vendor Unique Event %04" PRIX16 "h", counters->counters[iter].rawID);
            }
            else
            {
                switch (counters->counters[iter].eventID)
                {
                case SATA_PHY_EVENT_COMMAND_ICRC:
                    snprintf_err_handle(counterDescription, PHY_COUNTER_DESCRIPTION_LEN,
                                        "Command failed with iCRC error");
                    break;
                case SATA_PHY_EVENT_R_ERR_RESPONSE_FOR_DATA_FIS:
                    snprintf_err_handle(counterDescription, PHY_COUNTER_DESCRIPTION_LEN, "R_ERR response for data FIS");
                    break;
                case SATA_PHY_EVENT_R_ERR_RESPONSE_FOR_D2H_DATA_FIS:
                    snprintf_err_handle(counterDescription, PHY_COUNTER_DESCRIPTION_LEN,
                                        "R_ERR response for D2H data FIS");
                    break;
                case SATA_PHY_EVENT_R_ERR_RESPONSE_FOR_H2D_DATA_FIS:
                    snprintf_err_handle(counterDescription, PHY_COUNTER_DESCRIPTION_LEN,
                                        "R_ERR response for H2D data FIS");
                    break;
                case SATA_PHY_EVENT_R_ERR_RESPONSE_FOR_NON_DATA_FIS:
                    snprintf_err_handle(counterDescription, PHY_COUNTER_DESCRIPTION_LEN,
                                        "R_ERR response for non-data FIS");
                    break;
                case SATA_PHY_EVENT_R_ERR_RESPONSE_FOR_D2H_NON_DATA_FIS:
                    snprintf_err_handle(counterDescription, PHY_COUNTER_DESCRIPTION_LEN,
                                        "R_ERR response for D2H non-data FIS");
                    break;
                case SATA_PHY_EVENT_R_ERR_RESPONSE_FOR_H2D_NON_DATA_FIS:
                    snprintf_err_handle(counterDescription, PHY_COUNTER_DESCRIPTION_LEN,
                                        "R_ERR response for H2D non-data FIS");
                    break;
                case SATA_PHY_EVENT_D2H_NON_DATA_FIS_RETRIES:
                    snprintf_err_handle(counterDescription, PHY_COUNTER_DESCRIPTION_LEN, "D2H non-data FIS retries");
                    break;
                case SATA_PHY_EVENT_TRANSITIONS_FROM_PHYRDY_2_PHYRDYN:
                    snprintf_err_handle(counterDescription, PHY_COUNTER_DESCRIPTION_LEN,
                                        "Transitions from PHYRDY to PHYRDYn");
                    break;
                case SATA_PHY_EVENT_H2D_FISES_SENT_DUE_TO_COMRESET:
                    snprintf_err_handle(counterDescription, PHY_COUNTER_DESCRIPTION_LEN,
                                        "H2D FISes sent due to COMRESET");
                    break;
                case SATA_PHY_EVENT_CRC_ERRORS_WITHIN_H2D_FIS:
                    snprintf_err_handle(counterDescription, PHY_COUNTER_DESCRIPTION_LEN, "CRC errors withing H2D FIS");
                    break;
                case SATA_PHY_EVENT_NON_CRC_ERRORS_WITHIN_H2D_FIS:
                    snprintf_err_handle(counterDescription, PHY_COUNTER_DESCRIPTION_LEN,
                                        "Non-CRC errors within H2D FIS");
                    break;
                case SATA_PHY_EVENT_R_ERR_RESPONSE_H2D_DATA_FIS_CRC:
                    snprintf_err_handle(counterDescription, PHY_COUNTER_DESCRIPTION_LEN,
                                        "R_ERR response for H2D data FIS CRC");
                    break;
                case SATA_PHY_EVENT_R_ERR_RESPONSE_H2D_DATA_FIS_NONCRC:
                    snprintf_err_handle(counterDescription, PHY_COUNTER_DESCRIPTION_LEN,
                                        "R_ERR response for H2D data FIS non-CRC");
                    break;
                case SATA_PHY_EVENT_R_ERR_RESPONSE_H2D_NONDATA_FIS_CRC:
                    snprintf_err_handle(counterDescription, PHY_COUNTER_DESCRIPTION_LEN,
                                        "R_ERR response for H2D non-data FIS CRC");
                    break;
                case SATA_PHY_EVENT_R_ERR_RESPONSE_H2D_NONDATA_FIS_NONCRC:
                    snprintf_err_handle(counterDescription, PHY_COUNTER_DESCRIPTION_LEN,
                                        "R_ERR response for H2D non-data FIS non-CRC");
                    break;
                case SATA_PHY_EVENT_PM_H2D_NONDATA_FIS_R_ERR_END_STAT_COLLISION:
                    snprintf_err_handle(counterDescription, PHY_COUNTER_DESCRIPTION_LEN,
                                        "PM H2D non-data FIS R_ERR ending status from collision");
                    break;
                case SATA_PHY_EVENT_PM_SIGNATURE_REGISTER_D2H_FISES:
                    snprintf_err_handle(counterDescription, PHY_COUNTER_DESCRIPTION_LEN,
                                        "PM signature register D2H FISes");
                    break;
                case SATA_PHY_EVENT_PM_CORRUPT_CRC_PROPAGATION_D2H_FISES:
                    snprintf_err_handle(counterDescription, PHY_COUNTER_DESCRIPTION_LEN,
                                        "PM corrupt CRC propagation D2H FISes");
                    break;
                default:
                    snprintf_err_handle(counterDescription, PHY_COUNTER_DESCRIPTION_LEN, "Unknown Event %04" PRIX16 "h",
                                        counters->counters[iter].rawID);
                    break;
                }
            }
            if (counters->counters[iter].counterMaxValue == counters->counters[iter].counterValue)
            {
                maxedCount = 'M';
            }
            printf("%c%c %3" PRIu16 " %20" PRIu64 " %s\n", vendorEvent, maxedCount, counters->counters[iter].eventID,
                   counters->counters[iter].counterValue, counterDescription);
        }
        if (!counters->validChecksumReceived)
        {
            print_str("\nWARNING: Invalid checksum was received. Data may not be accurate!\n");
        }
    }
    RESTORE_NONNULL_COMPARE
}
