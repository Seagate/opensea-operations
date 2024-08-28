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
// \file seagate_operations.c
// \brief This file defines the functions for Seagate drive specific operations that are customer safe

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
#include "sleep.h"
#include <float.h>

#include "seagate_operations.h"
#include "logs.h"
#include "smart.h"
#include "sector_repair.h"
#include "dst.h"
#include "sanitize.h"
#include "format.h"
#include "vendor/seagate/seagate_ata_types.h"
#include "vendor/seagate/seagate_scsi_types.h"
#include "platform_helper.h"
#include "depopulate.h"

eReturnValues seagate_ata_SCT_SATA_phy_speed(tDevice *device, uint8_t speedGen)
{
    eReturnValues ret = UNKNOWN;
    uint8_t *sctSATAPhySpeed = C_CAST(uint8_t*, safe_calloc_aligned(LEGACY_DRIVE_SEC_SIZE, sizeof(uint8_t), device->os_info.minimumAlignment));
    if (sctSATAPhySpeed == M_NULLPTR)
    {
        perror("Calloc Failure!\n");
        return MEMORY_FAILURE;
    }
    //speedGen = 1 means generation 1 (1.5Gb/s), 2 =  2nd Generation (3.0Gb/s), 3 = 3rd Generation (6.0Gb/s)
    if (speedGen > 3)
    {
        safe_free_aligned(&sctSATAPhySpeed);
        return BAD_PARAMETER;
    }

    //fill in the buffer with the correct information
    //action code
    sctSATAPhySpeed[0] = M_Byte0(SCT_SEAGATE_SPEED_CONTROL);
    sctSATAPhySpeed[1] = M_Byte1(SCT_SEAGATE_SPEED_CONTROL);
    //function code
    sctSATAPhySpeed[2] = M_Byte0(BIST_SET_SATA_PHY_SPEED);
    sctSATAPhySpeed[3] = M_Byte1(BIST_SET_SATA_PHY_SPEED);
    //feature code
    sctSATAPhySpeed[4] = RESERVED;
    sctSATAPhySpeed[5] = RESERVED;
    //state
    sctSATAPhySpeed[6] = RESERVED;
    sctSATAPhySpeed[7] = RESERVED;
    //option flags
    sctSATAPhySpeed[8] = 0x01;//save feature to disk...I'm assuming this is always what's wanted since the other flags don't make sense since this feature requires a power cycle
    sctSATAPhySpeed[9] = 0x00;
    //reserved words are from byte 10:27
    //the data transferspeed goes in bytes 28 and 29, although byte 28 will be zero
    sctSATAPhySpeed[28] = speedGen;
    sctSATAPhySpeed[29] = 0x00;

    ret = send_ATA_SCT_Command(device, sctSATAPhySpeed, LEGACY_DRIVE_SEC_SIZE, false);

    safe_free_aligned(&sctSATAPhySpeed);
    return ret;
}

typedef enum _eSASPhySpeeds
{
    SAS_PHY_NO_CHANGE = 0x0,
    //reserved
    SAS_PHY_1_5_Gbs = 0x8,
    SAS_PHY_3_Gbs = 0x9,
    SAS_PHY_6_Gbs = 0xA,
    SAS_PHY_12_Gbs = 0xB,
    SAS_PHY_22_5_Gbs = 0xC,
    //values Dh - Fh are reserved for future speeds
}eSASPhySpeeds;

//valid phySpeedGen values are 1 - 5. This will need to be modified if SAS get's higher link rates than 22.5Gb/s
eReturnValues scsi_Set_Phy_Speed(tDevice *device, uint8_t phySpeedGen, bool allPhys, uint8_t phyNumber)
{
    eReturnValues ret = SUCCESS;
    if (phySpeedGen > SET_PHY_SPEED_MAX_GENERATION)
    {
        return NOT_SUPPORTED;
    }
    uint16_t phyControlLength = UINT16_C(104) + MODE_PARAMETER_HEADER_10_LEN;//size of 104 comes from 8 byte page header + (2 * 48bytes) for 2 phy descriptors + then add 8 bytes for mode parameter header. This is assuming drives only have 2...which is true right now, but the code will detect when it needs to reallocate and read more from the drive.
    uint8_t *sasPhyControl = C_CAST(uint8_t*, safe_calloc_aligned(phyControlLength, sizeof(uint8_t), device->os_info.minimumAlignment));
    if (!sasPhyControl)
    {
        return MEMORY_FAILURE;
    }
    if (SUCCESS == scsi_Mode_Sense_10(device, MP_PROTOCOL_SPECIFIC_PORT, phyControlLength, 0x01, true, false, MPC_CURRENT_VALUES, sasPhyControl))
    {
        //make sure we got the header as we expect it, then validate we got all the data we needed.
        //uint16_t modeDataLength = M_BytesTo2ByteValue(sasPhyControl[0], sasPhyControl[1]);
        uint16_t blockDescriptorLength = M_BytesTo2ByteValue(sasPhyControl[6], sasPhyControl[7]);
        //validate we got the right page
        if ((sasPhyControl[MODE_PARAMETER_HEADER_10_LEN + blockDescriptorLength + 0] & 0x3F) == 0x19 && (sasPhyControl[MODE_PARAMETER_HEADER_10_LEN + blockDescriptorLength + 1]) == 0x01 && (sasPhyControl[MODE_PARAMETER_HEADER_10_LEN + blockDescriptorLength + 0] & BIT6) > 0)
        {
            uint16_t pageLength = M_BytesTo2ByteValue(sasPhyControl[MODE_PARAMETER_HEADER_10_LEN + blockDescriptorLength + 2], sasPhyControl[MODE_PARAMETER_HEADER_10_LEN + blockDescriptorLength + 3]) + 4;//add 4 for first 4 bytes of page before descriptors
            //check that we were able to read the full page! If we didn't get the entire thing, we need to reread it and adjust the phyControlLength variable!
            if ((pageLength + MODE_PARAMETER_HEADER_10_LEN + blockDescriptorLength) > phyControlLength)
            {
                //reread the page for the larger length
                phyControlLength = C_CAST(uint16_t, pageLength + MODE_PARAMETER_HEADER_10_LEN + blockDescriptorLength);
                uint8_t *temp = safe_reallocf_aligned(C_CAST(void**, &sasPhyControl), 0, phyControlLength * sizeof(uint8_t), device->os_info.minimumAlignment);
                if (!temp)
                {
                    return MEMORY_FAILURE;
                }
                sasPhyControl = temp;
                if (SUCCESS != scsi_Mode_Sense_10(device, MP_PROTOCOL_SPECIFIC_PORT, phyControlLength, 0x01, true, false, MPC_CURRENT_VALUES, sasPhyControl))
                {
                    safe_free_aligned(&sasPhyControl);
                    return FAILURE;
                }
            }
            if ((sasPhyControl[MODE_PARAMETER_HEADER_10_LEN + blockDescriptorLength + 5] & 0x0F) == 6)//make sure it's the SAS protocol page
            {
                uint8_t numberOfPhys = sasPhyControl[MODE_PARAMETER_HEADER_10_LEN + blockDescriptorLength + 7];
                uint32_t phyDescriptorOffset = MODE_PARAMETER_HEADER_10_LEN + blockDescriptorLength + 8;//this will be set to the beginning of the phy descriptors so that when looping through them, it is easier code to read.
                for (uint16_t phyIter = 0; phyIter < C_CAST(uint16_t, numberOfPhys) && phyDescriptorOffset < phyControlLength; ++phyIter, phyDescriptorOffset += 48)
                {
                    uint8_t phyIdentifier = sasPhyControl[phyDescriptorOffset + 1];
                    //check if the caller requested changing all phys or a specific phy and only modify it's descriptor if either of those are true.
                    if (allPhys || phyNumber == phyIdentifier)
                    {
                        uint8_t hardwareMaximumLinkRate = M_Nibble0(sasPhyControl[phyDescriptorOffset + 33]);
                        if (phySpeedGen == 0)
                        {
                            //they want it back to default, so read the hardware maximum physical link rate and set it to the programmed maximum
                            uint8_t matchedRate = C_CAST(uint8_t, (hardwareMaximumLinkRate << 4) | hardwareMaximumLinkRate);
                            sasPhyControl[phyDescriptorOffset + 33] = matchedRate;
                        }
                        else
                        {
                            //they are requesting a specific speed, so set the value in the page.
                            switch (phySpeedGen)
                            {
                            case 1://1.5 Gb/s
                                sasPhyControl[phyDescriptorOffset + 33] = (SAS_PHY_1_5_Gbs << 4) | hardwareMaximumLinkRate;
                                break;
                            case 2://3.0 Gb/s
                                sasPhyControl[phyDescriptorOffset + 33] = (SAS_PHY_3_Gbs << 4) | hardwareMaximumLinkRate;
                                break;
                            case 3://6.0 Gb/s
                                sasPhyControl[phyDescriptorOffset + 33] = (SAS_PHY_6_Gbs << 4) | hardwareMaximumLinkRate;
                                break;
                            case 4://12.0 Gb/s
                                sasPhyControl[phyDescriptorOffset + 33] = (SAS_PHY_12_Gbs << 4) | hardwareMaximumLinkRate;
                                break;
                            case 5://22.5 Gb/s
                                sasPhyControl[phyDescriptorOffset + 33] = (SAS_PHY_22_5_Gbs << 4) | hardwareMaximumLinkRate;
                                break;
                            default:
                                //error! should be caught above!
                                break;
                            }
                        }
                    }
                }
                //we've finished making our changes to the mode page, so it's time to write it back!
                if (SUCCESS != scsi_Mode_Select_10(device, phyControlLength, true, true, false, sasPhyControl, phyControlLength))
                {
                    ret = FAILURE;
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
    }
    else
    {
        ret = FAILURE;
    }
    safe_free_aligned(&sasPhyControl);
    return ret;
}

eReturnValues set_phy_speed(tDevice *device, uint8_t phySpeedGen, bool allPhys, uint8_t phyIdentifier)
{
    eReturnValues ret = UNKNOWN;
    if (device->drive_info.drive_type == ATA_DRIVE)
    {
        if (is_Seagate_Family(device) == SEAGATE)
        {
            if (is_ATA_Identify_Word_Valid(device->drive_info.IdentifyData.ata.Word206) && device->drive_info.IdentifyData.ata.Word206 & BIT7 && !is_SSD(device))
            {
                if (phySpeedGen > SET_PHY_SPEED_SATA_MAX_GENERATION)
                {
                    //error, invalid input
                    if (VERBOSITY_QUIET < device->deviceVerbosity)
                    {
                        printf("Invalid PHY generation speed input. Please use 0 - 3.\n");
                    }
                    return BAD_PARAMETER;
                }
                ret = seagate_ata_SCT_SATA_phy_speed(device, phySpeedGen);
            }
            else
            {
                ret = NOT_SUPPORTED;
            }
        }
        else
        {
            if (VERBOSITY_QUIET < device->deviceVerbosity)
            {
                printf("Setting the PHY speed of a device is only available on Seagate Drives.\n");
            }
            ret = NOT_SUPPORTED;
        }
    }
    else if (device->drive_info.drive_type == SCSI_DRIVE)
    {
        //call the scsi/sas function to set the phy speed.
        ret = scsi_Set_Phy_Speed(device, phySpeedGen, allPhys, phyIdentifier);
    }
    else
    {
        ret = NOT_SUPPORTED;
    }
    return ret;
}

bool is_SCT_Low_Current_Spinup_Supported(tDevice *device)
{
    bool supported = false;
    if (device->drive_info.drive_type == ATA_DRIVE && is_Seagate_Family(device) == SEAGATE)
    {
        if (is_ATA_Identify_Word_Valid(device->drive_info.IdentifyData.ata.Word206) && device->drive_info.IdentifyData.ata.Word206 & BIT4)
        {
            uint16_t optionFlags = 0x0000;
            uint16_t state = 0x0000;
            if (SUCCESS == send_ATA_SCT_Feature_Control(device, SCT_FEATURE_FUNCTION_RETURN_CURRENT_STATE, SEAGATE_SCT_FEATURE_CONTROL_LOW_CURRENT_SPINUP, &state, &optionFlags))
            {
                supported = true;
            }
        }
    }
    return supported;
}

int is_Low_Current_Spin_Up_Enabled(tDevice *device, bool sctCommandSupported)
{
    int lowPowerSpinUpEnabled = 0;
    if (device->drive_info.drive_type == ATA_DRIVE && is_Seagate_Family(device) == SEAGATE)
    {
        //first try the SCT feature control command to get it's state
        if (is_ATA_Identify_Word_Valid(device->drive_info.IdentifyData.ata.Word206) && (device->drive_info.IdentifyData.ata.Word206 & BIT4) && sctCommandSupported)
        {
            uint16_t optionFlags = 0x0000;
            uint16_t state = 0x0000;
            if (SUCCESS == send_ATA_SCT_Feature_Control(device, SCT_FEATURE_FUNCTION_RETURN_CURRENT_STATE, SEAGATE_SCT_FEATURE_CONTROL_LOW_CURRENT_SPINUP, &state, &optionFlags))
            {
                lowPowerSpinUpEnabled = state;
            }
        }
        else if (!sctCommandSupported)//check the identify data for a bit (2.5" drives only I think) - TJE
        {
            //refresh Identify data
            ata_Identify(device, C_CAST(uint8_t*, &device->drive_info.IdentifyData.ata.Word000), LEGACY_DRIVE_SEC_SIZE);
            if (is_ATA_Identify_Word_Valid(device->drive_info.IdentifyData.ata.Word155) && device->drive_info.IdentifyData.ata.Word155 & BIT1)
            {
                lowPowerSpinUpEnabled = 1;
            }
        }
    }
    return lowPowerSpinUpEnabled;
}

eReturnValues seagate_SCT_Low_Current_Spinup(tDevice *device, eSeagateLCSpinLevel spinupLevel)
{
    eReturnValues ret = NOT_SUPPORTED;
    if (is_ATA_Identify_Word_Valid(device->drive_info.IdentifyData.ata.Word206) && device->drive_info.IdentifyData.ata.Word206 & BIT4)
    {
        uint16_t saveToDrive = 0x0001;//always set this because this feature requires saving for it to even work.
        uint16_t state = C_CAST(uint16_t, spinupLevel);
        if (SUCCESS == send_ATA_SCT_Feature_Control(device, SCT_FEATURE_FUNCTION_SET_STATE_AND_OPTIONS, SEAGATE_SCT_FEATURE_CONTROL_LOW_CURRENT_SPINUP, &state, &saveToDrive))
        {
            ret = SUCCESS;
        }
    }
    return ret;
}

eReturnValues set_Low_Current_Spin_Up(tDevice *device, bool useSCTCommand, eSeagateLCSpinLevel state)
{
    eReturnValues ret = NOT_SUPPORTED;
    if (device->drive_info.drive_type == ATA_DRIVE && is_Seagate_Family(device) == SEAGATE)
    {
        if (state == 0)
        {
            return BAD_PARAMETER;
        }
        if (useSCTCommand)
        {
            ret = seagate_SCT_Low_Current_Spinup(device, state);
        }
        else
        {
            //use set features command for 2.5" products
            uint8_t secCnt = SEAGATE_SF_LCS_ENABLE;
            if (state == SEAGATE_LOW_CURRENT_SPINUP_STATE_DEFAULT)//0 means disable, 2 is here for compatibility with SCT command inputs
            {
                secCnt = SEAGATE_SF_LCS_DISABLE;
            }
            if (state >= 3)
            {
                return NOT_SUPPORTED;
            }
            if (SUCCESS == ata_Set_Features(device, C_CAST(eATASetFeaturesSubcommands, SEAGATE_SF_LOW_CURRENT_SPINUP), 0, secCnt, LOW_CURRENT_SPINUP_LBA_MID_SIG, LOW_CURRENT_SPINUP_LBA_HI_SIG))
            {
                ret = SUCCESS;
            }
        }
    }
    return ret;
}

eReturnValues set_SSC_Feature_SATA(tDevice *device, eSSCFeatureState mode)
{
    eReturnValues ret = NOT_SUPPORTED;
    if (device->drive_info.drive_type == ATA_DRIVE)
    {
        if (is_Seagate_Family(device) == SEAGATE)
        {
            if (is_ATA_Identify_Word_Valid(device->drive_info.IdentifyData.ata.Word206) && device->drive_info.IdentifyData.ata.Word206 & BIT4)
            {
                uint16_t state = C_CAST(uint16_t, mode);
                uint16_t saveToDrive = 0x0001;
                if (SUCCESS == send_ATA_SCT_Feature_Control(device, SCT_FEATURE_FUNCTION_SET_STATE_AND_OPTIONS, SEAGATE_SCT_FEATURE_CONTROL_SPEAD_SPECTRUM_CLOCKING, &state, &saveToDrive))
                {
                    ret = SUCCESS;
                }
                else
                {
                    ret = FAILURE;
                }
            }
        }
        else
        {
            if (VERBOSITY_QUIET < device->deviceVerbosity)
            {
                printf("Setting the SSC feature of a device is only available on Seagate Drives.\n");
            }
            ret = NOT_SUPPORTED;
        }
    }
    return ret;
}

eReturnValues get_SSC_Feature_SATA(tDevice *device, eSSCFeatureState *mode)
{
    eReturnValues ret = NOT_SUPPORTED;
    if (device->drive_info.drive_type == ATA_DRIVE)
    {
        if (is_Seagate_Family(device) == SEAGATE)
        {
            if (is_ATA_Identify_Word_Valid(device->drive_info.IdentifyData.ata.Word206) && device->drive_info.IdentifyData.ata.Word206 & BIT4)
            {
                uint16_t state = 0;
                uint16_t saveToDrive = 0;
                if (SUCCESS == send_ATA_SCT_Feature_Control(device, SCT_FEATURE_FUNCTION_RETURN_CURRENT_STATE, SEAGATE_SCT_FEATURE_CONTROL_SPEAD_SPECTRUM_CLOCKING, &state, &saveToDrive))
                {
                    ret = SUCCESS;
                    *mode = C_CAST(eSSCFeatureState, state);
                }
                else
                {
                    ret = FAILURE;
                }
            }
        }
        else
        {
            if (VERBOSITY_QUIET < device->deviceVerbosity)
            {
                printf("Getting the SSC feature of a device is only available on Seagate Drives.\n");
            }
            ret = NOT_SUPPORTED;
        }
    }
    return ret;
}

static eReturnValues seagate_SAS_Get_JIT_Modes(tDevice *device, ptrSeagateJITModes jitModes)
{
    eReturnValues ret = NOT_SUPPORTED;
    eSeagateFamily family = is_Seagate_Family(device);
    if (!jitModes)
    {
        return BAD_PARAMETER;
    }
    if (family == SEAGATE || family == SEAGATE_VENDOR_A)
    {
        if (!is_SSD(device))
        {
            //HDD, so we can do this.
            DECLARE_ZERO_INIT_ARRAY(uint8_t, seagateUnitAttentionParameters, 12 + MODE_PARAMETER_HEADER_10_LEN);
            bool readPage = false;
            uint8_t headerLength = MODE_PARAMETER_HEADER_10_LEN;
            if (SUCCESS == scsi_Mode_Sense_10(device, 0, 12 + MODE_PARAMETER_HEADER_10_LEN, 0, true, false, MPC_CURRENT_VALUES, seagateUnitAttentionParameters))
            {
                readPage = true;
            }
            else if (SUCCESS == scsi_Mode_Sense_6(device, 0, 12 + MODE_PARAMETER_HEADER_6_LEN, 0, true, MPC_CURRENT_VALUES, seagateUnitAttentionParameters))
            {
                readPage = true;
                headerLength = MODE_PARAMETER_HEADER_6_LEN;
            }
            if (readPage)
            {
                ret = SUCCESS;
                jitModes->valid = true;
                if (seagateUnitAttentionParameters[headerLength + 4] & BIT7)//vjit disabled
                {
                    jitModes->vJIT = true;
                }
                else
                {
                    jitModes->vJIT = false;
                }
                if (seagateUnitAttentionParameters[headerLength + 4] & BIT3)
                {
                    jitModes->jit3 = true;
                }
                if (seagateUnitAttentionParameters[headerLength + 4] & BIT2)
                {
                    jitModes->jit2 = true;
                }
                if (seagateUnitAttentionParameters[headerLength + 4] & BIT1)
                {
                    jitModes->jit1 = true;
                }
                if (seagateUnitAttentionParameters[headerLength + 4] & BIT0)
                {
                    jitModes->jit0 = true;
                }
            }
            else
            {
                ret = FAILURE;//Or not supported??
            }
        }
    }
    return ret;
}

eReturnValues seagate_Get_JIT_Modes(tDevice *device, ptrSeagateJITModes jitModes)
{
    eReturnValues ret = NOT_SUPPORTED;
    if (device->drive_info.drive_type == SCSI_DRIVE)
    {
        ret = seagate_SAS_Get_JIT_Modes(device, jitModes);
    }
    return ret;
}

static eReturnValues seagate_SAS_Set_JIT_Modes(tDevice *device, bool disableVjit, uint8_t jitMode, bool revertToDefaults, bool nonvolatile)
{
    eReturnValues ret = NOT_SUPPORTED;
    eSeagateFamily family = is_Seagate_Family(device);
    if (family == SEAGATE || family == SEAGATE_VENDOR_A)
    {
        if (!is_SSD(device))
        {
            //HDD, so we can do this.
            DECLARE_ZERO_INIT_ARRAY(uint8_t, seagateUnitAttentionParameters, 12 + MODE_PARAMETER_HEADER_10_LEN);
            bool readPage = false;
            uint8_t headerLength = MODE_PARAMETER_HEADER_10_LEN;
            if (revertToDefaults)
            {
                //We need to read the default mode page to get the status of the JIT bits, save them, then pass them along...
                bool readDefaults = false;
                if (SUCCESS == scsi_Mode_Sense_10(device, 0, 12 + MODE_PARAMETER_HEADER_10_LEN, 0, true, false, MPC_DEFAULT_VALUES, seagateUnitAttentionParameters))
                {
                    readDefaults = true;
                    headerLength = MODE_PARAMETER_HEADER_10_LEN;
                }
                else if (SUCCESS == scsi_Mode_Sense_6(device, 0, 12 + MODE_PARAMETER_HEADER_6_LEN, 0, true, MPC_DEFAULT_VALUES, seagateUnitAttentionParameters))
                {
                    readDefaults = true;
                    headerLength = MODE_PARAMETER_HEADER_6_LEN;
                }
                if (readDefaults)
                {
                    if (seagateUnitAttentionParameters[headerLength + 4] & BIT7)//vjit disabled
                    {
                        disableVjit = false;
                    }
                    else
                    {
                        disableVjit = true;
                    }
                    if (seagateUnitAttentionParameters[headerLength + 4] & BIT0)
                    {
                        jitMode = 0;
                    }
                    else if (seagateUnitAttentionParameters[headerLength + 4] & BIT1)
                    {
                        jitMode = 1;
                    }
                    else if (seagateUnitAttentionParameters[headerLength + 4] & BIT2)
                    {
                        jitMode = 2;
                    }
                    else if (seagateUnitAttentionParameters[headerLength + 4] & BIT3)
                    {
                        jitMode = 3;
                    }
                }
                else
                {
                    return FAILURE;
                }
            }
            if (SUCCESS == scsi_Mode_Sense_10(device, 0, 12 + MODE_PARAMETER_HEADER_10_LEN, 0, true, false, MPC_CURRENT_VALUES, seagateUnitAttentionParameters))
            {
                readPage = true;
                headerLength = MODE_PARAMETER_HEADER_10_LEN;
            }
            else if (SUCCESS == scsi_Mode_Sense_6(device, 0, 12 + MODE_PARAMETER_HEADER_6_LEN, 0, true, MPC_CURRENT_VALUES, seagateUnitAttentionParameters))
            {
                readPage = true;
                headerLength = MODE_PARAMETER_HEADER_6_LEN;
            }
            if (readPage)
            {
                //We've read the page, so now we must set up the requested JIT modes
                seagateUnitAttentionParameters[headerLength + 4] &= 0x70;//clear all bits to zero, except 4, 5, & 6 since those are reserved (and if they are ever used, we don't want to touch them now) - TJE
                if (!disableVjit)
                {
                    seagateUnitAttentionParameters[headerLength + 4] |= BIT7;
                }
                switch (jitMode)//Spec says that faster modes allow the drive to continue accessing slower modes, so might as well set each bit below the requested mode.
                {
                default:
                case 0:
                    seagateUnitAttentionParameters[headerLength + 4] |= BIT0;
                    M_FALLTHROUGH;
                case 1:
                    seagateUnitAttentionParameters[headerLength + 4] |= BIT1;
                    M_FALLTHROUGH;
                case 2:
                    seagateUnitAttentionParameters[headerLength + 4] |= BIT2;
                    M_FALLTHROUGH;
                case 3:
                    seagateUnitAttentionParameters[headerLength + 4] |= BIT3;
                }
                //Now we need to do a mode select to send this data back to the drive!!
                if (headerLength == MODE_PARAMETER_HEADER_10_LEN)
                {
                    ret = scsi_Mode_Select_10(device, 12 + headerLength, false, nonvolatile, false, seagateUnitAttentionParameters, 12 + headerLength);
                }
                else
                {
                    ret = scsi_Mode_Select_6(device, 12 + headerLength, false, nonvolatile, false, seagateUnitAttentionParameters, 12 + headerLength);
                }
            }
            else
            {
                ret = FAILURE;//Or not supported??
            }
        }
    }
    return ret;
}

eReturnValues seagate_Set_JIT_Modes(tDevice *device, bool disableVjit, uint8_t jitMode, bool revertToDefaults, bool nonvolatile)
{
    eReturnValues ret = NOT_SUPPORTED;
    if (device->drive_info.drive_type == SCSI_DRIVE)
    {
        ret = seagate_SAS_Set_JIT_Modes(device, disableVjit, jitMode, revertToDefaults, nonvolatile);
    }
    return ret;
}

eReturnValues seagate_Get_Power_Balance(tDevice *device, bool *supported, bool *enabled)
{
    eReturnValues ret = NOT_SUPPORTED;
    if (device->drive_info.drive_type == ATA_DRIVE)
    {
        if (is_Seagate_Family(device) == SEAGATE)
        {
            ret = SUCCESS;
            if (supported)
            {
                //BIT8 for older products with this feature. EX: ST10000NM*
                //Bit10 for newwer products with this feature. EX: ST12000NM*
                if (is_ATA_Identify_Word_Valid(device->drive_info.IdentifyData.ata.Word149) && (device->drive_info.IdentifyData.ata.Word149 & BIT8 || device->drive_info.IdentifyData.ata.Word149 & BIT10))
                {
                    *supported = true;
                }
                else
                {
                    *supported = false;
                }
            }
            if (enabled)
            {
                //BIT9 for older products with this feature. EX: ST10000NM*
                //Bit11 for newwer products with this feature. EX: ST12000NM*
                if (is_ATA_Identify_Word_Valid(device->drive_info.IdentifyData.ata.Word149) && (device->drive_info.IdentifyData.ata.Word149 & BIT9 || device->drive_info.IdentifyData.ata.Word149 & BIT11))
                {
                    *enabled = true;
                }
                else
                {
                    *enabled = false;
                }
            }
        }
    }
    else if (device->drive_info.drive_type == SCSI_DRIVE)
    {
        //NOTE: this uses the standard spec power consumption mode page.
        //      This feature conflicts with use other use of this page, at least on old drives.
        //      New drives use the active power mode identifier and the "highest" means disabled and "lowest" means enabled for this feature - TJE
        if (is_Seagate_Family(device) == SEAGATE)
        {
            uint32_t powerConsumptionLength = 0;
            if (SUCCESS == get_SCSI_VPD_Page_Size(device, POWER_CONSUMPTION, &powerConsumptionLength))
            {
                //If this page is supported, we're calling power balance on SAS not supported.
                //Note: This may need changing in the future, but right now this is still accurate - TJE
                if (supported)
                {
                    *supported = false;
                }
                return SUCCESS;
            }
            uint8_t *pcModePage = C_CAST(uint8_t*, safe_calloc_aligned(MODE_PARAMETER_HEADER_10_LEN + 16, sizeof(uint8_t), device->os_info.minimumAlignment));
            if (!pcModePage)
            {
                return MEMORY_FAILURE;
            }
            //read changeable values to get supported
            if (SUCCESS == scsi_Mode_Sense_10(device, MP_POWER_CONSUMPTION, MODE_PARAMETER_HEADER_10_LEN + 16, 0x01, true, false, MPC_CHANGABLE_VALUES, pcModePage))
            {
                ret = SUCCESS;
                //This is as close as I can figure the best way to check for power balance support - TJE
                ///Active mode cannot be changable, then the power consumption VPD page must also not be supported.
                //The above comment was true for OLD drives. New ones now use the active power mode field to enable/disable this feature
                if (pcModePage[MODE_PARAMETER_HEADER_10_LEN + 7] == 0xFF && (M_GETBITRANGE(pcModePage[MODE_PARAMETER_HEADER_10_LEN + 6], 2, 0) == 0))
                {
                    //If in here, this is an old drive since it doesn't allow setting the active power mode.
                    if (supported)
                    {
                        *supported = true;
                    }
                    //read current values to get enabled/disabled
                    memset(pcModePage, 0, MODE_PARAMETER_HEADER_10_LEN + 16);
                    if (SUCCESS == scsi_Mode_Sense_10(device, MP_POWER_CONSUMPTION, MODE_PARAMETER_HEADER_10_LEN + 16, 0x01, true, false, MPC_CURRENT_VALUES, pcModePage))
                    {
                        //check the active level to make sure it is zero
                        uint8_t activeLevel = pcModePage[MODE_PARAMETER_HEADER_10_LEN + 6] & 0x07;
                        if (activeLevel == 0 && pcModePage[MODE_PARAMETER_HEADER_10_LEN + 7] == 1 && enabled)
                        {
                            *enabled = true;
                        }
                        ret = SUCCESS;
                    }
                }
                else if (M_GETBITRANGE(pcModePage[MODE_PARAMETER_HEADER_10_LEN + 6], 2, 0) == 3 && pcModePage[MODE_PARAMETER_HEADER_10_LEN + 7] == 0)
                {
                    //if in here, this is a new drive which only allows this change via the active mode field.
                    //On these drives, we can check to make sure the changable fields apply to the active mode field, but NOT the power condition identifier.
                    if (supported)
                    {
                        *supported = true;
                    }
                    //read current values to get enabled/disabled
                    memset(pcModePage, 0, MODE_PARAMETER_HEADER_10_LEN + 16);
                    if (SUCCESS == scsi_Mode_Sense_10(device, MP_POWER_CONSUMPTION, MODE_PARAMETER_HEADER_10_LEN + 16, 0x01, true, false, MPC_CURRENT_VALUES, pcModePage))
                    {
                        //check the active level to make sure it is zero
                        uint8_t activeLevel = pcModePage[MODE_PARAMETER_HEADER_10_LEN + 6] & 0x07;
                        if (activeLevel == 3 && pcModePage[MODE_PARAMETER_HEADER_10_LEN + 7] == 0 && enabled)
                        {
                            *enabled = true;
                        }
                        else if (activeLevel == 1 && pcModePage[MODE_PARAMETER_HEADER_10_LEN + 7] == 0 && enabled)
                        {
                            *enabled = false;
                        }
                        else if (enabled)
                        {
                            //I guess say it's off???
                            *enabled = false;
                        }
                        ret = SUCCESS;
                    }
                }
            }
            safe_free_aligned(&pcModePage);
        }
    }
    return ret;
}

eReturnValues seagate_Set_Power_Balance(tDevice *device, ePowerBalanceMode powerMode)
{
    eReturnValues ret = NOT_SUPPORTED;
    if (device->drive_info.drive_type == ATA_DRIVE)
    {
        switch (powerMode)
        {
        case POWER_BAL_ENABLE:
            ret = ata_Set_Features(device, SEAGATE_FEATURE_POWER_BALANCE, 0, POWER_BALANCE_LBA_LOW_ENABLE, 0, 0);
            break;
        case POWER_BAL_DISABLE:
            ret = ata_Set_Features(device, SEAGATE_FEATURE_POWER_BALANCE, 0, POWER_BALANCE_LBA_LOW_DISABLE, 0, 0);
            break;
        case POWER_BAL_LIMITED:
            ret = ata_Set_Features(device, SEAGATE_FEATURE_POWER_BALANCE, 0, POWER_BALANCE_LBA_LOW_LIMITED, 0, 0);
            break;
        default:
            break;
        }
    }
    else if (device->drive_info.drive_type == SCSI_DRIVE)
    {
        bool oldMethod = false;
        uint8_t *pcModePage = C_CAST(uint8_t*, safe_calloc_aligned(16 + MODE_PARAMETER_HEADER_10_LEN, sizeof(uint8_t), device->os_info.minimumAlignment));
        if (!pcModePage)
        {
            return MEMORY_FAILURE;
        }
        //First, need to read changable values page to see if this is a drive needing the old method, or the new one. - TJE
        if (SUCCESS == scsi_Mode_Sense_10(device, MP_POWER_CONSUMPTION, MODE_PARAMETER_HEADER_10_LEN + 16, 0x01, true, false, MPC_CHANGABLE_VALUES, pcModePage))
        {
            //Detect the old method by seeing if active mode is not changable, but power condition identifier is.
            if (pcModePage[MODE_PARAMETER_HEADER_10_LEN + 7] == 0xFF && (M_GETBITRANGE(pcModePage[MODE_PARAMETER_HEADER_10_LEN + 6], 2, 0) == 0))
            {
                oldMethod = true;
            }
            //Assume for now that otherwise this is changable by modifying the active field.
        }
        //not read and modify the page to enable or disable this feature.
        if (SUCCESS == scsi_Mode_Sense_10(device, MP_POWER_CONSUMPTION, 16 + MODE_PARAMETER_HEADER_10_LEN, 0x01, true, false, MPC_CURRENT_VALUES, pcModePage))
        {
            pcModePage[MODE_PARAMETER_HEADER_10_LEN + 6] &= 0xFC;//clear lower 2 bits to 0
            if (oldMethod)
            {
                //Active field is NOT used, only the power condition identifier, which must be 0 or 1.
                if (powerMode == POWER_BAL_ENABLE)
                {
                    pcModePage[MODE_PARAMETER_HEADER_10_LEN + 7] = 1;
                }
                else
                {
                    pcModePage[MODE_PARAMETER_HEADER_10_LEN + 7] = 0;
                }
            }
            else
            {
                //Active field IS used and must be set to highest (disabled) or lowest (enabled)
                if (powerMode == POWER_BAL_ENABLE)
                {
                    pcModePage[MODE_PARAMETER_HEADER_10_LEN + 6] = 3;
                }
                else
                {
                    pcModePage[MODE_PARAMETER_HEADER_10_LEN + 6] = 1;
                }
            }
            //now do mode select with the data for the mode to set
            ret = scsi_Mode_Select_10(device, 16 + MODE_PARAMETER_HEADER_10_LEN, true, true, false, pcModePage, 16 + MODE_PARAMETER_HEADER_10_LEN);
        }
        safe_free_aligned(&pcModePage);
    }
    return ret;
}

eReturnValues get_IDD_Support(tDevice *device, ptrIDDSupportedFeatures iddSupport)
{
    eReturnValues ret = NOT_SUPPORTED;
    //IDD is only on ATA drives
    if (device->drive_info.drive_type == ATA_DRIVE && is_SMART_Enabled(device))
    {
        //IDD is seagate specific
        if (is_Seagate_Family(device) == SEAGATE)
        {
            uint8_t *smartData = C_CAST(uint8_t*, safe_calloc_aligned(LEGACY_DRIVE_SEC_SIZE, sizeof(uint8_t), device->os_info.minimumAlignment));
            if (!smartData)
            {
                return MEMORY_FAILURE;
            }
            if (ata_SMART_Read_Data(device, smartData, LEGACY_DRIVE_SEC_SIZE) == SUCCESS)
            {
                ret = SUCCESS;
                if (smartData[0x1EE] & BIT0)
                {
                    iddSupport->iddShort = true;
                }
                if (smartData[0x1EE] & BIT1)
                {
                    iddSupport->iddLong = true;
                }
            }
            else
            {
                ret = FAILURE;
            }
            safe_free_aligned(&smartData);
        }
    }
    else if (device->drive_info.drive_type == SCSI_DRIVE)
    {
        //IDD is seagate specific
        if (is_Seagate_Family(device) == SEAGATE)
        {
            uint8_t *iddDiagPage = C_CAST(uint8_t*, safe_calloc_aligned(12, sizeof(uint8_t), device->os_info.minimumAlignment));
            if (iddDiagPage)
            {
                if (SUCCESS == scsi_Receive_Diagnostic_Results(device, true, SEAGATE_DIAG_IN_DRIVE_DIAGNOSTICS, 12, iddDiagPage, 15))
                {
                    ret = SUCCESS;
                    iddSupport->iddShort = true;//short
                    iddSupport->iddLong = true;//long
                }
            }
            safe_free_aligned(&iddDiagPage);
        }
    }
    return ret;
}

#define IDD_READY_TIME_SECONDS 120

eReturnValues get_Approximate_IDD_Time(tDevice *device, eIDDTests iddTest, uint64_t *timeInSeconds)
{
    eReturnValues ret = NOT_SUPPORTED;
    *timeInSeconds = 0;
    //IDD is only on ATA drives
    if (device->drive_info.drive_type == ATA_DRIVE && is_SMART_Enabled(device))
    {
        //IDD is seagate specific
        if (is_Seagate_Family(device) == SEAGATE)
        {
            uint32_t numberOfLbasInLists = 0;
            smartLogData smartData;
            memset(&smartData, 0, sizeof(smartLogData));
            switch (iddTest)
            {
            case SEAGATE_IDD_SHORT:
                ret = SUCCESS;
                *timeInSeconds = IDD_READY_TIME_SECONDS;
                break;
            case SEAGATE_IDD_LONG:
                ret = get_SMART_Attributes(device, &smartData);
                if (ret == SUCCESS)
                {
                    if (smartData.attributes.ataSMARTAttr.attributes[197].valid)
                    {
                        numberOfLbasInLists += M_BytesTo4ByteValue(smartData.attributes.ataSMARTAttr.attributes[197].data.rawData[3], \
                            smartData.attributes.ataSMARTAttr.attributes[197].data.rawData[2], \
                            smartData.attributes.ataSMARTAttr.attributes[197].data.rawData[1], \
                            smartData.attributes.ataSMARTAttr.attributes[197].data.rawData[0]);
                    }
                    if (smartData.attributes.ataSMARTAttr.attributes[5].valid)
                    {
                        numberOfLbasInLists += M_BytesTo4ByteValue(smartData.attributes.ataSMARTAttr.attributes[5].data.rawData[3], \
                            smartData.attributes.ataSMARTAttr.attributes[5].data.rawData[2], \
                            smartData.attributes.ataSMARTAttr.attributes[5].data.rawData[1], \
                            smartData.attributes.ataSMARTAttr.attributes[5].data.rawData[0]);
                    }
                    *timeInSeconds = (numberOfLbasInLists * 3) + IDD_READY_TIME_SECONDS;
                }
                break;
            default:
                break;
            }
        }
    }
    else if (device->drive_info.drive_type == SCSI_DRIVE)
    {
        //IDD is seagate specific
        if (is_Seagate_Family(device) == SEAGATE)
        {
            switch (iddTest)
            {
            case SEAGATE_IDD_SHORT:
                *timeInSeconds = IDD_READY_TIME_SECONDS;
                ret = SUCCESS;
                break;
            case SEAGATE_IDD_LONG:
                *timeInSeconds = UINT64_MAX;
                ret = SUCCESS;
                break;
            default:
                break;
            }
        }
    }
    return ret;
}

eReturnValues get_IDD_Status(tDevice *device, uint8_t *status)
{
    eReturnValues ret = NOT_SUPPORTED;
    if (device->drive_info.drive_type == ATA_DRIVE)
    {
        uint32_t percentComplete = 0;
        return ata_Get_DST_Progress(device, &percentComplete, status);
    }
    else if (device->drive_info.drive_type == SCSI_DRIVE)
    {
        //read diagnostic page
        uint8_t *iddDiagPage = C_CAST(uint8_t*, safe_calloc_aligned(12, sizeof(uint8_t), device->os_info.minimumAlignment));
        if (iddDiagPage)
        {
            //do not use the return value from this since IDD can return a few different sense codes with unit attention, that we may otherwise call an error
            ret = scsi_Receive_Diagnostic_Results(device, true, SEAGATE_DIAG_IN_DRIVE_DIAGNOSTICS, 12, iddDiagPage, 15);
            if (ret != SUCCESS)
            {
                uint8_t senseKey = 0;
                uint8_t asc = 0;
                uint8_t ascq = 0;
                uint8_t fru = 0;
                get_Sense_Key_ASC_ASCQ_FRU(device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, &senseKey, &asc, &ascq, &fru);
                //We are checking if the reset part of the test is still in progress by checking for this sense data and will try again after a second until we know this part of the test is complete.
                if (senseKey == SENSE_KEY_UNIT_ATTENTION && asc == 0x29 && ascq == 0x01 && fru == 0x0A)
                {
                    *status = 0x0F;//this is setting that the IDD is still in progress. This is because we got specific sense data related to this test, so that is why we are setting this here instead of trying to look at the data buffer.
                    return SUCCESS;
                }
            }
            if (iddDiagPage[0] == SEAGATE_DIAG_IN_DRIVE_DIAGNOSTICS && M_BytesTo2ByteValue(iddDiagPage[2], iddDiagPage[3]) == 0x0008)//check that the page and pagelength match what we expect
            {
                ret = SUCCESS;
                *status = M_Nibble0(iddDiagPage[4]);
            }
            else
            {
                ret = FAILURE;
            }
        }
        else
        {
            ret = MEMORY_FAILURE;
        }
        safe_free_aligned(&iddDiagPage);
    }
    return ret;
}

//NOTE: If IDD is ever supported on NVMe, this may need updates.
void translate_IDD_Status_To_String(uint8_t status, char *translatedString, bool justRanDST)
{
    if (!translatedString)
    {
        return;
    }
    switch (status)
    {
    case 0x00:
        if (justRanDST)
        {
            snprintf(translatedString, MAX_DST_STATUS_STRING_LENGTH, "The IDD routine completed without error.");
        }
        else
        {
            snprintf(translatedString, MAX_DST_STATUS_STRING_LENGTH, "The previous IDD routine completed without error or no IDD has ever been run.");
        }
        break;
    case 0x01:

        snprintf(translatedString, MAX_DST_STATUS_STRING_LENGTH, "The IDD routine was aborted by the host.");
        break;
    case 0x02:
        snprintf(translatedString, MAX_DST_STATUS_STRING_LENGTH, "The IDD routine was interrupted by the host with a hardware or software reset.");
        break;
    case 0x03:
        snprintf(translatedString, MAX_DST_STATUS_STRING_LENGTH, "A fatal error or unknown test error occurred while the device was executing its IDD routine and the device was unable to complete the IDD routine.");
        break;
    case 0x04:
        if (justRanDST)
        {
            snprintf(translatedString, MAX_DST_STATUS_STRING_LENGTH, "The IDD completed having a test element that failed and the test element that failed is not known.");
        }
        else
        {
            snprintf(translatedString, MAX_DST_STATUS_STRING_LENGTH, "The previous IDD completed having a test element that failed and the test element that failed is not known.");
        }
        break;
    case 0x05:
        if (justRanDST)
        {
            snprintf(translatedString, MAX_DST_STATUS_STRING_LENGTH, "The IDD completed having the electrical element of the test failed.");
        }
        else
        {
            snprintf(translatedString, MAX_DST_STATUS_STRING_LENGTH, "The previous IDD completed having the electrical element of the test failed.");
        }
        break;
    case 0x06:
        if (justRanDST)
        {
            snprintf(translatedString, MAX_DST_STATUS_STRING_LENGTH, "The IDD completed having the servo (and/or seek) test element of the test failed.");
        }
        else
        {
            snprintf(translatedString, MAX_DST_STATUS_STRING_LENGTH, "The previous IDD completed having the servo (and/or seek) test element of the test failed.");
        }
        break;
    case 0x07:
        if (justRanDST)
        {
            snprintf(translatedString, MAX_DST_STATUS_STRING_LENGTH, "The IDD completed having the read element of the test failed.");
        }
        else
        {
            snprintf(translatedString, MAX_DST_STATUS_STRING_LENGTH, "The previous IDD completed having the read element of the test failed.");
        }
        break;
    case 0x08:
        if (justRanDST)
        {
            snprintf(translatedString, MAX_DST_STATUS_STRING_LENGTH, "The IDD completed having a test element that failed and the device is suspected of having handling damage.");
        }
        else
        {
            snprintf(translatedString, MAX_DST_STATUS_STRING_LENGTH, "The previous IDD completed having a test element that failed and the device is suspected of having handling damage.");
        }
        break;
    case 0x09:
    case 0x0A:
    case 0x0B:
    case 0x0C:
    case 0x0D:
    case 0x0E:
        snprintf(translatedString, MAX_DST_STATUS_STRING_LENGTH, "Reserved Status.");
        break;
    case 0x0F:
        snprintf(translatedString, MAX_DST_STATUS_STRING_LENGTH, "IDD in progress.");
        break;
    default:
        snprintf(translatedString, MAX_DST_STATUS_STRING_LENGTH, "Error, unknown status: %" PRIX8 "h.", status);
    }
    return;
}


static eReturnValues start_IDD_Operation(tDevice *device, eIDDTests iddOperation, bool captiveForeground)
{
    eReturnValues ret = NOT_SUPPORTED;
    os_Lock_Device(device);
    if (device->drive_info.drive_type == ATA_DRIVE)
    {
        uint8_t iddTestNumber = 0;
        uint32_t timeoutSeconds = SEAGATE_IDD_TIMEOUT;//make this super long just in case...
        if (captiveForeground)
        {
            if (os_Is_Infinite_Timeout_Supported())
            {
                timeoutSeconds = INFINITE_TIMEOUT_VALUE;
            }
            else
            {
                timeoutSeconds = MAX_CMD_TIMEOUT_SECONDS;
            }
        }
        switch (iddOperation)
        {
        case SEAGATE_IDD_SHORT:
            iddTestNumber = SEAGATE_ST_IDD_SHORT_OFFLINE;
            if (captiveForeground)
            {
                iddTestNumber = SEAGATE_ST_IDD_SHORT_CAPTIVE;
            }
            break;
        case SEAGATE_IDD_LONG:
            iddTestNumber = SEAGATE_ST_IDD_LONG_OFFLINE;
            if (captiveForeground)
            {
                iddTestNumber = SEAGATE_ST_IDD_LONG_CAPTIVE;
            }
            break;
        default:
            return NOT_SUPPORTED;
        }
        ret = ata_SMART_Offline(device, iddTestNumber, timeoutSeconds);
    }
    else if (device->drive_info.drive_type == SCSI_DRIVE)
    {
        //send diagnostic
        uint8_t *iddDiagPage = C_CAST(uint8_t*, safe_calloc_aligned(12, sizeof(uint8_t), device->os_info.minimumAlignment));
        if (iddDiagPage)
        {
            uint32_t commandTimeoutSeconds = SEAGATE_IDD_TIMEOUT;
            iddDiagPage[0] = SEAGATE_DIAG_IN_DRIVE_DIAGNOSTICS;//page code
            switch (iddOperation)
            {
            case SEAGATE_IDD_SHORT:
                iddDiagPage[1] |= BIT7;
                break;
            case SEAGATE_IDD_LONG:
                iddDiagPage[1] |= BIT6;
                break;
            default:
                safe_free(&iddDiagPage);
                os_Unlock_Device(device);
                return NOT_SUPPORTED;
            }
            if (captiveForeground)
            {
                if (os_Is_Infinite_Timeout_Supported())
                {
                    commandTimeoutSeconds = INFINITE_TIMEOUT_VALUE;
                }
                else
                {
                    commandTimeoutSeconds = MAX_CMD_TIMEOUT_SECONDS;
                }
                iddDiagPage[1] |= BIT4;
            }
            else
            {
                iddDiagPage[1] |= BIT5;
            }
            iddDiagPage[2] = 0;
            iddDiagPage[3] = 0x08;//page length
            iddDiagPage[4] = 1 << 4;//revision number 1, status of zero
            ret = scsi_Send_Diagnostic(device, 0, 1, 0, 0, 0, 12, iddDiagPage, 12, commandTimeoutSeconds);
            safe_free_aligned(&iddDiagPage);
        }
        else
        {
            ret = MEMORY_FAILURE;
        }
    }
    if (ret != SUCCESS)
    {
        if (device->drive_info.drive_type == SCSI_DRIVE)
        {
            //check the sense data. The problem may be that captive/foreground mode isn't supported for the long test
            uint8_t senseKey = 0;
            uint8_t asc = 0;
            uint8_t ascq = 0;
            uint8_t fru = 0;
            get_Sense_Key_ASC_ASCQ_FRU(device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, &senseKey, &asc, &ascq, &fru);
            if (senseKey == SENSE_KEY_ILLEGAL_REQUEST)
            {
                //Do we need to check for asc = 26h, ascq = 0h? For now this should be ok
                return NOT_SUPPORTED;
            }
            else
            {
                return FAILURE;
            }
        }
        else
        {
            return FAILURE;
        }
    }
    uint32_t commandTimeSeconds = C_CAST(uint32_t, device->drive_info.lastCommandTimeNanoSeconds / UINT64_C(1000000000));
    if (commandTimeSeconds < IDD_READY_TIME_SECONDS)
    {
        //we need to make sure we waited at least 2 minutes since command was sent to the drive before pinging it with another command.
        //It needs time to spin back up and be ready to accept commands again.
        //This is being done in both captive/foreground and offline/background modes due to differences between some drive firmwares.
        delay_Seconds(IDD_READY_TIME_SECONDS - commandTimeSeconds);
    }
    os_Unlock_Device(device);
    return ret;
}

//this is a seagate drive specific feature. Will now work on other drives
eReturnValues run_IDD(tDevice *device, eIDDTests IDDtest, bool pollForProgress, bool captive)
{
    eReturnValues result = UNKNOWN;
    if (is_Seagate_Family(device) != NON_SEAGATE)
    {
        iddSupportedFeatures iddSupport;
        memset(&iddSupport, 0, sizeof(iddSupportedFeatures));
        switch (IDDtest)
        {
        case SEAGATE_IDD_SHORT:
        case SEAGATE_IDD_LONG:
            break;
        default:
            return BAD_PARAMETER;
        }

        if (SUCCESS == get_IDD_Support(device, &iddSupport))
        {
            //check if the IDD operation requested is supported then run it if it is
            if ((IDDtest == SEAGATE_IDD_SHORT && iddSupport.iddShort) || (IDDtest == SEAGATE_IDD_LONG && iddSupport.iddLong))
            {
                uint8_t status = 0xF;
                bool captiveForeground = false;
                if (IDDtest == SEAGATE_IDD_SHORT || captive)
                {
                    //SCSI says that the short test must be run in foreground...so let's do that for both ATA and SCSI...
                    //Long test may be ran in background or foreground
                    captiveForeground = true;
                }
                //check if a test is already in progress first
                get_IDD_Status(device, &status);
                if (status == 0xF)
                {
                    return IN_PROGRESS;
                }
                //if we are here, then an operation isn't already in progress so time to start it
                result = start_IDD_Operation(device, IDDtest, captiveForeground);
                if (SUCCESS == result && captiveForeground)
                {
                    eReturnValues ret = get_IDD_Status(device, &status);
                    if (status == 0 && ret == SUCCESS)
                    {
                        pollForProgress = false;
                        result = SUCCESS; //we passed.
                    }
                    else
                    {
                        switch (status)
                        {
                        case 1://aborted by host
                        case 2://interrupted by reset
                            result = ABORTED;
                            break;
                        case 9://ready to start...guessing this means success?
                            result = SUCCESS;
                            break;
                        case 0x0F://still in progress. Go into the polling loop below to finish waiting
                            result = SUCCESS;
                            pollForProgress = true;
                            break;
                        case 3://fatal error
                        default:
                            if (IDDtest == SEAGATE_IDD_SHORT)
                            {
                                result = FAILURE;
                            }
                            else
                            {
                                //IDD may still be in progress, so start polling for progress just to make sure it finished.
                                //This is a special case for the long test since it can take more than 2 minutes to complete.
                                pollForProgress = true;
                            }
                            break;
                        }
                    }
                }
                if (SUCCESS == result && pollForProgress)
                {
                    status = 0xF;//assume that the operation is in progress until it isn't anymore
                    eReturnValues ret = SUCCESS;//for use in the loop below...assume that we are successful
                    while (status > 0x08 && ret == SUCCESS)
                    {
                        ret = get_IDD_Status(device, &status);
                        if (VERBOSITY_QUIET < device->deviceVerbosity)
                        {
                            printf("\n    IDD test is still in progress...please wait");
                            fflush(stdout);
                        }
                        delay_Seconds(5);//5 second delay between progress checks
                    }
                    printf("\n\n");
                    if (status == 0 && ret == SUCCESS)
                    {
                        result = SUCCESS; //we passed.
                    }
                    else
                    {
                        switch (status)
                        {
                        case 0x01://aborted by host
                        case 0x02://interrupted by reset
                            result = ABORTED;
                            break;
                        case 0x09://ready to start...guessing this means success?
                            result = SUCCESS;
                            break;
                        default:
                            result = FAILURE;
                            break;
                        }
                    }
                }
                else if (!pollForProgress && result != SUCCESS)
                {
                    if (VERBOSITY_QUIET < device->deviceVerbosity)
                    {
                        printf("An error occured while trying to start an IDD test.\n");
                    }
                    result = FAILURE;
                }
            }
            else
            {
                //IDD test specified not supported
                result = NOT_SUPPORTED;
            }
        }
        else
        {
            return NOT_SUPPORTED;
        }
    }
    else
    {
        return NOT_SUPPORTED;
    }
    return result;
}

bool is_Seagate_Power_Telemetry_Feature_Supported(tDevice *device)
{
    bool supported = false;
    if (device->drive_info.drive_type == ATA_DRIVE)
    {
        //check if log page E3h has data
        uint32_t logSize = 0;
        if (SUCCESS == get_ATA_Log_Size(device, SEAGATE_ATA_LOG_POWER_TELEMETRY, &logSize, true, false))
        {
            if (logSize > 0)
            {
                supported = true;
            }
        }
    }
    else if (device->drive_info.drive_type == SCSI_DRIVE)
    {
        //check for non-zero value for buffer ID 54h
        uint32_t bufferSize = 0;
        if (SUCCESS == get_SCSI_Error_History_Size(device, SEAGATE_ERR_HIST_POWER_TELEMETRY, &bufferSize, false, is_SCSI_Read_Buffer_16_Supported(device)))
        {
            if (bufferSize > 0)
            {
                supported = true;
            }
        }
    }
    return supported;
}

//These are in the spec, but need to be verified before they are used.
#define ATA_POWER_TELEMETRY_LOG_SIZE_BYTES UINT16_C(8192)
#define SCSI_POWER_TELEMETRY_LOG_SIZE_BYTES UINT16_C(6240)

//This can be used to save this log to a binary file to be read later.
eReturnValues pull_Power_Telemetry_Log(tDevice *device, const char * const filePath, uint32_t transferSizeBytes)
{
    eReturnValues ret = NOT_SUPPORTED;
    if (device->drive_info.drive_type == ATA_DRIVE)
    {
        ret = get_ATA_Log(device, SEAGATE_ATA_LOG_POWER_TELEMETRY, "PWRTEL", "pwr", true, false, false, M_NULLPTR, 0, filePath, transferSizeBytes, 0);
    }
    else if (device->drive_info.drive_type == SCSI_DRIVE)
    {
        ret = get_SCSI_Error_History(device, SEAGATE_ERR_HIST_POWER_TELEMETRY, "PWRTEL", false, is_SCSI_Read_Buffer_16_Supported(device), "pwr", false, M_NULLPTR, 0, filePath, transferSizeBytes, M_NULLPTR);
    }
    return ret;
}

eReturnValues request_Power_Measurement(tDevice *device, uint16_t timeMeasurementSeconds, ePowerTelemetryMeasurementOptions measurementOption)
{
    eReturnValues ret = NOT_SUPPORTED;
    if (device->drive_info.drive_type == ATA_DRIVE)
    {
        DECLARE_ZERO_INIT_ARRAY(uint8_t, pwrTelLogPg, 512);
        pwrTelLogPg[0] = C_CAST(uint8_t, SEAGATE_ATA_LOG_POWER_TELEMETRY);
        pwrTelLogPg[1] = POWER_TELEMETRY_REQUEST_MEASUREMENT_VERSION;//version 1
        pwrTelLogPg[2] = RESERVED;
        pwrTelLogPg[3] = RESERVED;
        pwrTelLogPg[4] = M_Byte0(timeMeasurementSeconds);
        pwrTelLogPg[5] = M_Byte1(timeMeasurementSeconds);
        pwrTelLogPg[6] = C_CAST(uint8_t, measurementOption);
        //remaining bytes are reserved
        //send write log ext
        ret = ata_Write_Log_Ext(device, SEAGATE_ATA_LOG_POWER_TELEMETRY, 0, pwrTelLogPg, 512, device->drive_info.ata_Options.readLogWriteLogDMASupported, false);
    }
    else if (device->drive_info.drive_type == SCSI_DRIVE)
    {
        DECLARE_ZERO_INIT_ARRAY(uint8_t, pwrTelDiagPg, 16);
        pwrTelDiagPg[0] = C_CAST(uint8_t, SEAGATE_DIAG_POWER_MEASUREMENT);
        pwrTelDiagPg[1] = POWER_TELEMETRY_REQUEST_MEASUREMENT_VERSION;//version 1
        pwrTelDiagPg[2] = M_Byte1(12);//page length msb
        pwrTelDiagPg[3] = M_Byte0(12);//page length lsb
        pwrTelDiagPg[4] = M_Byte1(timeMeasurementSeconds);
        pwrTelDiagPg[5] = M_Byte0(timeMeasurementSeconds);
        pwrTelDiagPg[6] = C_CAST(uint8_t, measurementOption);
        pwrTelDiagPg[7] = RESERVED;
        pwrTelDiagPg[8] = RESERVED;
        pwrTelDiagPg[9] = RESERVED;
        pwrTelDiagPg[10] = RESERVED;
        pwrTelDiagPg[11] = RESERVED;
        pwrTelDiagPg[12] = RESERVED;
        pwrTelDiagPg[13] = RESERVED;
        pwrTelDiagPg[14] = RESERVED;
        pwrTelDiagPg[15] = RESERVED;
        //send diagnostic command
        ret = scsi_Send_Diagnostic(device, 0, 1, 0, 0, 0, 16, pwrTelDiagPg, 16, 15);
    }
    return ret;
}

eReturnValues get_Power_Telemetry_Data(tDevice *device, ptrSeagatePwrTelemetry pwrTelData)
{
    eReturnValues ret = NOT_SUPPORTED;
    if (!pwrTelData)
    {
        return BAD_PARAMETER;
    }
    uint32_t powerTelemetryLogSize = 0;
    uint8_t *powerTelemetryLog = M_NULLPTR;
    //first, determine how much data there is, allocate memory, then read it all into that buffer
    if (device->drive_info.drive_type == ATA_DRIVE)
    {
        ret = get_ATA_Log_Size(device, SEAGATE_ATA_LOG_POWER_TELEMETRY, &powerTelemetryLogSize, true, false);
        if (ret == SUCCESS && powerTelemetryLogSize > 0)
        {
            powerTelemetryLog = C_CAST(uint8_t *, safe_calloc_aligned(powerTelemetryLogSize, sizeof(uint8_t), device->os_info.minimumAlignment));
            if (powerTelemetryLog)
            {
                ret = get_ATA_Log(device, SEAGATE_ATA_LOG_POWER_TELEMETRY, M_NULLPTR, M_NULLPTR, true, false, true, powerTelemetryLog, powerTelemetryLogSize, M_NULLPTR, 0, 0);
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
    }
    else if (device->drive_info.drive_type == SCSI_DRIVE)
    {
        bool rb16 = is_SCSI_Read_Buffer_16_Supported(device);
        ret = get_SCSI_Error_History_Size(device, SEAGATE_ERR_HIST_POWER_TELEMETRY, &powerTelemetryLogSize, false, rb16);
        if (ret == SUCCESS && powerTelemetryLogSize > 0)
        {
            powerTelemetryLog = C_CAST(uint8_t *, safe_calloc_aligned(powerTelemetryLogSize, sizeof(uint8_t), device->os_info.minimumAlignment));
            if (powerTelemetryLog)
            {
                ret = get_SCSI_Error_History(device, SEAGATE_ERR_HIST_POWER_TELEMETRY, M_NULLPTR, false, rb16, M_NULLPTR, true, powerTelemetryLog, powerTelemetryLogSize, M_NULLPTR, 0, M_NULLPTR);
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
    }

    if (ret == SUCCESS && powerTelemetryLogSize > 0)
    {
        //got the data, now parse it into the correct fields.
        //Everything, but the strings, are reported in little endian by the drive.
        //This makes it easy, so just need to convert to the host's endianness if necessary
        memset(pwrTelData, 0, sizeof(seagatePwrTelemetry));
        memcpy(pwrTelData->serialNumber, &powerTelemetryLog[0], 8);
        pwrTelData->powerCycleCount = M_BytesTo2ByteValue(powerTelemetryLog[9], powerTelemetryLog[8]);
        //drive timestamps will be reported as uint64 in this structure so that they can be converted to whatever is easy by other users
        pwrTelData->driveTimeStampForHostRequestedMeasurement = M_BytesTo8ByteValue(0, 0, powerTelemetryLog[15], powerTelemetryLog[14], powerTelemetryLog[13], powerTelemetryLog[12], powerTelemetryLog[11], powerTelemetryLog[10]);
        pwrTelData->driveTimeStampWhenTheLogWasRetrieved = M_BytesTo8ByteValue(0, 0, powerTelemetryLog[21], powerTelemetryLog[20], powerTelemetryLog[19], powerTelemetryLog[18], powerTelemetryLog[17], powerTelemetryLog[16]);
        pwrTelData->majorRevision = powerTelemetryLog[22];
        pwrTelData->minorRevision = powerTelemetryLog[23];
        memcpy(pwrTelData->signature, &powerTelemetryLog[24], 8);
        pwrTelData->totalMeasurementTimeRequested = M_BytesTo2ByteValue(powerTelemetryLog[33], powerTelemetryLog[32]);
        uint16_t dataLength = M_BytesTo2ByteValue(powerTelemetryLog[35], powerTelemetryLog[34]);
        pwrTelData->numberOfMeasurements = M_BytesTo2ByteValue(powerTelemetryLog[37], powerTelemetryLog[36]);
        uint32_t measurementOffset = M_BytesTo2ByteValue(powerTelemetryLog[39], powerTelemetryLog[38]);
        pwrTelData->measurementFormat = powerTelemetryLog[40];
        pwrTelData->temperatureCelcius = powerTelemetryLog[41];
        pwrTelData->measurementWindowTimeMilliseconds = M_BytesTo2ByteValue(powerTelemetryLog[43], powerTelemetryLog[42]);

        pwrTelData->multipleLogicalUnits = (device->drive_info.numberOfLUs > 1) ? true : false;

        for (uint16_t measurementNumber = 0; measurementNumber < pwrTelData->numberOfMeasurements && measurementOffset < dataLength && measurementOffset < powerTelemetryLogSize; ++measurementNumber, measurementOffset += 6)
        {
            pwrTelData->measurement[measurementNumber].fiveVoltMilliWatts = M_BytesTo2ByteValue(powerTelemetryLog[measurementOffset + 1], powerTelemetryLog[measurementOffset + 0]);
            pwrTelData->measurement[measurementNumber].twelveVoltMilliWatts = M_BytesTo2ByteValue(powerTelemetryLog[measurementOffset + 3], powerTelemetryLog[measurementOffset + 2]);
            pwrTelData->measurement[measurementNumber].reserved = M_BytesTo2ByteValue(powerTelemetryLog[measurementOffset + 5], powerTelemetryLog[measurementOffset + 4]);
            if (pwrTelData->measurement[measurementNumber].fiveVoltMilliWatts == 0 && pwrTelData->measurement[measurementNumber].twelveVoltMilliWatts == 0)
            {
                //invalid or empty entry should only happen once at the end! loop conditions should also protect from this!
                break;
            }
        }
    }
    safe_free_aligned(&powerTelemetryLog);
    return ret;
}

void show_Power_Telemetry_Data(ptrSeagatePwrTelemetry pwrTelData)
{
    if (pwrTelData)
    {
        //doubles for end statistics of measurement
        double sum5v = 0, sum12v = 0, min5v = DBL_MAX, max5v = DBL_MIN, min12v = DBL_MAX, max12v = DBL_MIN;
        double stepTime = pwrTelData->measurementWindowTimeMilliseconds;

        printf("Power Telemetry\n");
        printf("\tSerial Number: %s\n", pwrTelData->serialNumber);
        printf("\tRevision: %" PRIu8 ".%" PRIu8 "\n", pwrTelData->majorRevision, pwrTelData->minorRevision);
        printf("\tTemperature (C): %" PRIu8 "\n", pwrTelData->temperatureCelcius);
        printf("\tPower Cycle Count: %" PRIu16 "\n", pwrTelData->powerCycleCount);
        //printf("\tNumber Of Measurements: %" PRIu16 "\n", pwrTelData->numberOfMeasurements);
        if (pwrTelData->totalMeasurementTimeRequested == 0)
        {
            printf("\tMeasurement Time (seconds): 600\t (No previous request. Free-running mode)\n");
        }
        else
        {
            printf("\tMeasurement Time (seconds): %" PRIu16 "\n", pwrTelData->totalMeasurementTimeRequested);
        }
        printf("\tMeasurement Window (ms): %" PRIu16 "\n", pwrTelData->measurementWindowTimeMilliseconds);

        printf("\nIndividual Power Measurements\n");
        //Note, while the spacing may not make much sense, it definitely works with the widths below.
        printf("    #\t     Time       \t  5V Pwr (W)\t  12V Pwr (W)\t  Total (W)\n");
        uint16_t measurementCounter = 0;
        for (uint16_t measurementNumber = 0; measurementNumber < pwrTelData->numberOfMeasurements && measurementNumber < POWER_TELEMETRY_MAXIMUM_MEASUREMENTS; ++measurementNumber)
        {
            double power5VWatts = pwrTelData->measurement[measurementNumber].fiveVoltMilliWatts / 1000.0;
            double power12VWatts = pwrTelData->measurement[measurementNumber].twelveVoltMilliWatts / 1000.0;
            double measurementTime = measurementNumber * stepTime;
            if (pwrTelData->totalMeasurementTimeRequested == 0)
            {
                measurementTime += C_CAST(double, pwrTelData->driveTimeStampWhenTheLogWasRetrieved);
            }
            else
            {
                measurementTime += C_CAST(double, pwrTelData->driveTimeStampForHostRequestedMeasurement);
            }
            if (pwrTelData->measurement[measurementNumber].fiveVoltMilliWatts == 0 && pwrTelData->measurement[measurementNumber].twelveVoltMilliWatts == 0)
            {
                break;
            }
            //NOTE: Original format was 10.6, 6.3, 6.3, 6.3. Trying to widen to match the header
            if (pwrTelData->measurementFormat == 5)
            {
                printf("%5" PRIu16 "\t%16.6f\t%12.3f\t%13s\t%11.3f\n", measurementNumber, measurementTime, power5VWatts, "N/A", power5VWatts);
            }
            else if (pwrTelData->measurementFormat == 12)
            {
                printf("%5" PRIu16 "\t%16.6f\t%12s\t%13.3f\t%11.3f\n", measurementNumber, measurementTime, "N/A", power12VWatts, power12VWatts);
            }
            else
            {
                printf("%5" PRIu16 "\t%16.6f\t%12.3f\t%13.3f\t%11.3f\n", measurementNumber, measurementTime, power5VWatts, power12VWatts, power5VWatts + power12VWatts);
            }
            //update min/max values
            if (power5VWatts < min5v)
            {
                min5v = power5VWatts;
            }
            if (power12VWatts < min12v)
            {
                min12v = power12VWatts;
            }
            if (power5VWatts > max5v)
            {
                max5v = power5VWatts;
            }
            if (power12VWatts > max12v)
            {
                max12v = power12VWatts;
            }
            sum5v += power5VWatts;
            sum12v += power12VWatts;
            ++measurementCounter;
        }
        if (measurementCounter > 0)
        {
            printf("\n");
            if (pwrTelData->measurementFormat == 0 || pwrTelData->measurementFormat == 5)
            {
                printf(" 5 Volt Power (W):\tAverage: %6.3f \tMinimum: %6.3f \tMaximum: %6.3f\n", sum5v / measurementCounter, min5v, max5v);
            }
            if (pwrTelData->measurementFormat == 0 || pwrTelData->measurementFormat == 12)
            {
                printf("12 Volt Power (W):\tAverage: %6.3f \tMinimum: %6.3f \tMaximum: %6.3f\n", sum12v / measurementCounter, min12v, max12v);
            }
        }
        if (pwrTelData->multipleLogicalUnits)
        {
            printf("NOTE: All power measurements are for the full device, not individual logical units.\n");
        }
    }
    return;
}

bool is_Seagate_Quick_Format_Supported(tDevice *device)
{
    bool supported = false;
    if (device->drive_info.drive_type == ATA_DRIVE)//This is only available on SATA drives.
    {
        eSeagateFamily family = is_Seagate_Family(device);
        if (family == SEAGATE)
        {
            if (device->drive_info.drive_type == ATA_DRIVE)
            {
                if (is_SMART_Enabled(device))
                {
                    DECLARE_ZERO_INIT_ARRAY(uint8_t, smartData, LEGACY_DRIVE_SEC_SIZE);
                    if (SUCCESS == ata_SMART_Read_Data(device, smartData, LEGACY_DRIVE_SEC_SIZE))
                    {
                        if (smartData[0x1EE] & BIT3)
                        {
                            supported = true;
                        }
                    }
                }
            }

            if (!supported)
            {
                //This above support bit was discontinued, so need to check for support of other features..This is not a guaranteed "it will work" but it is what we have to work with.
                bool stdDepopSupported = is_Depopulation_Feature_Supported(device, M_NULLPTR);
                //bool stdRepopSupported = is_Repopulate_Feature_Supported(device, M_NULLPTR);
                if (stdDepopSupported /*&& !stdRepopSupported*/)
                {
                    supported = true;
                }
            }
        }
    }
    return supported;
}

eReturnValues seagate_Quick_Format(tDevice *device)
{
    eReturnValues ret = NOT_SUPPORTED;
    if (device->drive_info.drive_type == ATA_DRIVE)
    {
        uint32_t timeout = 0;
        if (os_Is_Infinite_Timeout_Supported())
        {
            timeout = INFINITE_TIMEOUT_VALUE;
        }
        else
        {
            timeout = MAX_CMD_TIMEOUT_SECONDS;
        }
        os_Lock_Device(device);
        ret = ata_SMART_Command(device, ATA_SMART_EXEC_OFFLINE_IMM, 0xD3, M_NULLPTR, 0, timeout, false, 0);
        os_Unlock_Device(device);
    }
    return ret;
}

//These are in the nvme-cli plugin.
//This applies to older enterprise drives, but not the current products. 
//Check for SEAGATE_VENDOR_SSD_PJ

/***************************************
* Extended-SMART Information
***************************************/
const char* print_ext_smart_id(uint8_t attrId)
{
    switch (attrId) {
    case VS_ATTR_ID_SOFT_READ_ERROR_RATE:
        return "Soft ECC error count";
    case VS_ATTR_ID_REALLOCATED_SECTOR_COUNT:
        return "Bad NAND block count";
    case VS_ATTR_ID_POWER_ON_HOURS:
        return "Power On Hours";
    case VS_ATTR_ID_POWER_FAIL_EVENT_COUNT:
        return "Power Fail Event Count";
    case VS_ATTR_ID_DEVICE_POWER_CYCLE_COUNT:
        return "Device Power Cycle Count";
    case VS_ATTR_ID_RAW_READ_ERROR_RATE:
        return "Uncorrectable read error count";
        /**********************************************
                case 30:
                    return "LIFETIME_WRITES0_TO_FLASH";
                case 31:
                    return "LIFETIME_WRITES1_TO_FLASH";
                case 32:
                    return "LIFETIME_WRITES0_FROM_HOST";
                case 33:
                    return "LIFETIME_WRITES1_FROM_HOST";
                case 34:
                    return "LIFETIME_READ0_FROM_HOST";
                case 35:
                    return "LIFETIME_READ1_FROM_HOST";
                case 36:
                    return "PCIE_PHY_CRC_ERROR";
                case 37:
                    return "BAD_BLOCK_COUNT_SYSTEM";
                case 38:
                    return "BAD_BLOCK_COUNT_USER";
                case 39:
                    return "THERMAL_THROTTLING_STATUS";
        **********************************************/
    case VS_ATTR_ID_GROWN_BAD_BLOCK_COUNT:
        return "Bad NAND block count";
    case VS_ATTR_ID_END_2_END_CORRECTION_COUNT:
        return "SSD End to end correction counts";
    case VS_ATTR_ID_MIN_MAX_WEAR_RANGE_COUNT:
        return "User data erase counts";
    case VS_ATTR_ID_REFRESH_COUNT:
        return "Refresh count";
    case VS_ATTR_ID_BAD_BLOCK_COUNT_USER:
        return "User data erase fail count";
    case VS_ATTR_ID_BAD_BLOCK_COUNT_SYSTEM:
        return "System area erase fail count";
    case VS_ATTR_ID_THERMAL_THROTTLING_STATUS:
        return "Thermal throttling status and count";
    case VS_ATTR_ID_ALL_PCIE_CORRECTABLE_ERROR_COUNT:
        return "PCIe Correctable Error count";
    case VS_ATTR_ID_ALL_PCIE_UNCORRECTABLE_ERROR_COUNT:
        return "PCIe Uncorrectable Error count";
    case VS_ATTR_ID_INCOMPLETE_SHUTDOWN_COUNT:
        return "Incomplete shutdowns";
    case VS_ATTR_ID_GB_ERASED_LSB:
        return "LSB of Flash GB erased";
    case VS_ATTR_ID_GB_ERASED_MSB:
        return "MSB of Flash GB erased";
    case VS_ATTR_ID_LIFETIME_DEVSLEEP_EXIT_COUNT:
        return "LIFETIME_DEV_SLEEP_EXIT_COUNT";
    case VS_ATTR_ID_LIFETIME_ENTERING_PS4_COUNT:
        return "LIFETIME_ENTERING_PS4_COUNT";
    case VS_ATTR_ID_LIFETIME_ENTERING_PS3_COUNT:
        return "LIFETIME_ENTERING_PS3_COUNT";
    case VS_ATTR_ID_RETIRED_BLOCK_COUNT:
        return "Retired block count"; /*VS_ATTR_ID_RETIRED_BLOCK_COUNT*/
    case VS_ATTR_ID_PROGRAM_FAILURE_COUNT:
        return "Program fail count";
    case VS_ATTR_ID_ERASE_FAIL_COUNT:
        return "Erase Fail Count";
    case VS_ATTR_ID_AVG_ERASE_COUNT:
        return "System data % used";
    case VS_ATTR_ID_UNEXPECTED_POWER_LOSS_COUNT:
        return "Unexpected power loss count";
    case VS_ATTR_ID_WEAR_RANGE_DELTA:
        return "Wear range delta";
    case VS_ATTR_ID_SATA_INTERFACE_DOWNSHIFT_COUNT:
        return "PCIE_INTF_DOWNSHIFT_COUNT";
    case VS_ATTR_ID_END_TO_END_CRC_ERROR_COUNT:
        return "E2E_CRC_ERROR_COUNT";
    case VS_ATTR_ID_UNCORRECTABLE_ECC_ERRORS:
        return "Soft ECC error count";
    case VS_ATTR_ID_MAX_LIFE_TEMPERATURE:
        return "Max lifetime temperature";/*VS_ATTR_ID_MAX_LIFE_TEMPERATURE for extended*/
    case VS_ATTR_ID_RAISE_ECC_CORRECTABLE_ERROR_COUNT:
        return "RAIS_ECC_CORRECT_ERR_COUNT";
    case VS_ATTR_ID_UNCORRECTABLE_RAISE_ERRORS:
        return "Uncorrectable read error count";/*VS_ATTR_ID_UNCORRECTABLE_RAISE_ERRORS*/
    case VS_ATTR_ID_DRIVE_LIFE_PROTECTION_STATUS:
        return "DRIVE_LIFE_PROTECTION_STATUS";
    case VS_ATTR_ID_REMAINING_SSD_LIFE:
        return "Remaining SSD life";
    case VS_ATTR_ID_LIFETIME_WRITES_TO_FLASH_LSB:
        return "LSB of Physical (NAND) bytes written";
    case VS_ATTR_ID_LIFETIME_WRITES_TO_FLASH_MSB:
        return "MSB of Physical (NAND) bytes written";
    case VS_ATTR_ID_LIFETIME_WRITES_FROM_HOST_LSB:
        return "LSB of Physical (HOST) bytes written";
    case VS_ATTR_ID_LIFETIME_WRITES_FROM_HOST_MSB:
        return "MSB of Physical (HOST) bytes written";
    case VS_ATTR_ID_LIFETIME_READS_TO_HOST_LSB:
        return "LSB of Physical (NAND) bytes read";
    case VS_ATTR_ID_LIFETIME_READS_TO_HOST_MSB:
        return "MSB of Physical (NAND) bytes read";
    case VS_ATTR_ID_FREE_SPACE:
        return "Free Space";
    case VS_ATTR_ID_TRIM_COUNT_LSB:
        return "LSB of Trim count";
    case VS_ATTR_ID_TRIM_COUNT_MSB:
        return "MSB of Trim count";
    case VS_ATTR_ID_OP_PERCENTAGE:
        return "OP percentage";
    case VS_ATTR_ID_MAX_SOC_LIFE_TEMPERATURE:
        return "Max lifetime SOC temperature";
    default:
        return "Un-Known";
    }
}

uint64_t smart_attribute_vs(uint16_t verNo, SmartVendorSpecific attr)
{
    uint64_t val = 0;
    fb_smart_attribute_data *attrFb;

    /**
     * These are all FaceBook specific attributes.
     */
    if (verNo >= EXTENDED_SMART_VERSION_FB) {
        attrFb = (fb_smart_attribute_data *)&attr;
        val = attrFb->MSDword;
        val = (val << 32) | attrFb->LSDword;
        return val;
    }

    /******************************************************************
        if(attr.AttributeNumber == VS_ATTR_POWER_CONSUMPTION) {
            attrFb = (fb_smart_attribute_data *)&attr;
            return attrFb->LSDword;
        }
        else if(attr.AttributeNumber == VS_ATTR_THERMAL_THROTTLING_STATUS) {
            fb_smart_attribute_data *attrFb;
            attrFb = (fb_smart_attribute_data *)&attr;
            return attrFb->LSDword;
        }
        else if(attr.AttributeNumber == VS_ATTR_PCIE_PHY_CRC_ERROR) {
            fb_smart_attribute_data *attrFb;
            attrFb = (fb_smart_attribute_data *)&attr;
            return attrFb->LSDword;
        }
        else if(attr.AttributeNumber == VS_ATTR_BAD_BLOCK_COUNT_USER) {
            fb_smart_attribute_data *attrFb;
            attrFb = (fb_smart_attribute_data *)&attr;
            return attrFb->LSDword;
        }
        else if(attr.AttributeNumber == VS_ATTR_BAD_BLOCK_COUNT_SYSTEM) {
            fb_smart_attribute_data *attrFb;
            attrFb = (fb_smart_attribute_data *)&attr;
            return attrFb->LSDword;
        }
        else if(attr.AttributeNumber == VS_ATTR_LIFETIME_READ1_FROM_HOST) {
            fb_smart_attribute_data *attrFb;
            attrFb = (fb_smart_attribute_data *)&attr;
            val = attrFb->MSDword;
            val = (val << 32) | attrFb->LSDword ;
            return val;
        }
        else if(attr.AttributeNumber == VS_ATTR_LIFETIME_READ0_FROM_HOST) {
            fb_smart_attribute_data *attrFb;
            attrFb = (fb_smart_attribute_data *)&attr;
            val = attrFb->MSDword;
            val = (val << 32) | attrFb->LSDword ;
            return val;
        }
        else if(attr.AttributeNumber == VS_ATTR_LIFETIME_WRITES1_FROM_HOST) {
            fb_smart_attribute_data *attrFb;
            attrFb = (fb_smart_attribute_data *)&attr;
            val = attrFb->MSDword;
            val = (val << 32) | attrFb->LSDword ;
            return val;
        }
        else if(attr.AttributeNumber == VS_ATTR_LIFETIME_WRITES0_FROM_HOST) {
            fb_smart_attribute_data *attrFb;
            attrFb = (fb_smart_attribute_data *)&attr;
            val = attrFb->MSDword;
            val = (val << 32) | attrFb->LSDword ;
            return val;
        }
        else if(attr.AttributeNumber == VS_ATTR_LIFETIME_WRITES1_TO_FLASH) {
            fb_smart_attribute_data *attrFb;
            attrFb = (fb_smart_attribute_data *)&attr;
            val = attrFb->MSDword;
            val = (val << 32) | attrFb->LSDword ;
            return val;
        }
        else if(attr.AttributeNumber == VS_ATTR_LIFETIME_WRITES0_TO_FLASH) {
            fb_smart_attribute_data *attrFb;
            attrFb = (fb_smart_attribute_data *)&attr;
            val = attrFb->MSDword;
            val = (val << 32) | attrFb->LSDword ;
            return val;
        }
    ******************************************************************/

    else
        return attr.Raw0_3;
}



void print_smart_log(uint16_t verNo, SmartVendorSpecific attr, int lastAttr)
{
    static uint64_t lsbGbErased = 0, msbGbErased = 0, lsbLifWrtToFlash = 0, msbLifWrtToFlash = 0, lsbLifWrtFrmHost = 0, msbLifWrtFrmHost = 0, lsbLifRdToHost = 0, msbLifRdToHost = 0, lsbTrimCnt = 0, msbTrimCnt = 0;
    DECLARE_ZERO_INIT_ARRAY(char, buf, 40);
#define NVME_PRINT_SMART_LOG_STRING_BUFFER_LENGTH 35
    DECLARE_ZERO_INIT_ARRAY(char, strBuf, NVME_PRINT_SMART_LOG_STRING_BUFFER_LENGTH);
    int hideAttr = 0;

    if (attr.AttributeNumber == VS_ATTR_ID_GB_ERASED_LSB)
    {
        lsbGbErased = smart_attribute_vs(verNo, attr);
        hideAttr = 1;
    }

    if (attr.AttributeNumber == VS_ATTR_ID_GB_ERASED_MSB)
    {
        msbGbErased = smart_attribute_vs(verNo, attr);
        hideAttr = 1;
    }

    if (attr.AttributeNumber == VS_ATTR_ID_LIFETIME_WRITES_TO_FLASH_LSB)
    {
        lsbLifWrtToFlash = smart_attribute_vs(verNo, attr);
        hideAttr = 1;
    }

    if (attr.AttributeNumber == VS_ATTR_ID_LIFETIME_WRITES_TO_FLASH_MSB)
    {
        msbLifWrtToFlash = smart_attribute_vs(verNo, attr);
        hideAttr = 1;
    }

    if (attr.AttributeNumber == VS_ATTR_ID_LIFETIME_WRITES_FROM_HOST_LSB)
    {
        lsbLifWrtFrmHost = smart_attribute_vs(verNo, attr);
        hideAttr = 1;
    }

    if (attr.AttributeNumber == VS_ATTR_ID_LIFETIME_WRITES_FROM_HOST_MSB) {
        msbLifWrtFrmHost = smart_attribute_vs(verNo, attr);
        hideAttr = 1;
    }

    if (attr.AttributeNumber == VS_ATTR_ID_LIFETIME_READS_TO_HOST_LSB) {
        lsbLifRdToHost = smart_attribute_vs(verNo, attr);
        hideAttr = 1;
    }

    if (attr.AttributeNumber == VS_ATTR_ID_LIFETIME_READS_TO_HOST_MSB)
    {
        msbLifRdToHost = smart_attribute_vs(verNo, attr);
        hideAttr = 1;
    }

    if (attr.AttributeNumber == VS_ATTR_ID_TRIM_COUNT_LSB)
    {
        lsbTrimCnt = smart_attribute_vs(verNo, attr);
        hideAttr = 1;
    }

    if (attr.AttributeNumber == VS_ATTR_ID_TRIM_COUNT_MSB)
    {
        msbTrimCnt = smart_attribute_vs(verNo, attr);
        hideAttr = 1;
    }

    if ((attr.AttributeNumber != 0) && (hideAttr != 1)) {
        printf("%-40s", print_ext_smart_id(attr.AttributeNumber));
        printf("%-15d", attr.AttributeNumber);
        printf(" 0x%016" PRIX64 "", smart_attribute_vs(verNo, attr));
        printf("\n");
    }

    if (lastAttr == 1) {

        snprintf(strBuf, NVME_PRINT_SMART_LOG_STRING_BUFFER_LENGTH, "%s", (print_ext_smart_id(VS_ATTR_ID_GB_ERASED_LSB) + 7));
        printf("%-40s", strBuf);

        printf("%-15d", VS_ATTR_ID_GB_ERASED_MSB << 8 | VS_ATTR_ID_GB_ERASED_LSB);

        snprintf(buf, NVME_PRINT_SMART_LOG_STRING_BUFFER_LENGTH, "0x%016" PRIX64 "%016" PRIX64 "", msbGbErased, lsbGbErased);
        printf(" %s", buf);
        printf("\n");

        snprintf(strBuf, NVME_PRINT_SMART_LOG_STRING_BUFFER_LENGTH, "%s", (print_ext_smart_id(VS_ATTR_ID_LIFETIME_WRITES_TO_FLASH_LSB) + 7));
        printf("%-40s", strBuf);

        printf("%-15d", VS_ATTR_ID_LIFETIME_WRITES_TO_FLASH_MSB << 8 | VS_ATTR_ID_LIFETIME_WRITES_TO_FLASH_LSB);

        snprintf(buf, NVME_PRINT_SMART_LOG_STRING_BUFFER_LENGTH, "0x%016" PRIX64 "%016" PRIX64, msbLifWrtToFlash, lsbLifWrtToFlash);
        printf(" %s", buf);
        printf("\n");

        snprintf(strBuf, NVME_PRINT_SMART_LOG_STRING_BUFFER_LENGTH, "%s", (print_ext_smart_id(VS_ATTR_ID_LIFETIME_WRITES_FROM_HOST_LSB) + 7));
        printf("%-40s", strBuf);

        printf("%-15d", VS_ATTR_ID_LIFETIME_WRITES_FROM_HOST_MSB << 8 | VS_ATTR_ID_LIFETIME_WRITES_FROM_HOST_LSB);

        snprintf(buf, NVME_PRINT_SMART_LOG_STRING_BUFFER_LENGTH, "0x%016" PRIX64 "%016" PRIX64, msbLifWrtFrmHost, lsbLifWrtFrmHost);
        printf(" %s", buf);
        printf("\n");

        snprintf(strBuf, NVME_PRINT_SMART_LOG_STRING_BUFFER_LENGTH, "%s", (print_ext_smart_id(VS_ATTR_ID_LIFETIME_READS_TO_HOST_LSB) + 7));
        printf("%-40s", strBuf);

        printf("%-15d", VS_ATTR_ID_LIFETIME_READS_TO_HOST_MSB << 8 | VS_ATTR_ID_LIFETIME_READS_TO_HOST_LSB);

        snprintf(buf, NVME_PRINT_SMART_LOG_STRING_BUFFER_LENGTH, "0x%016" PRIX64 "%016" PRIX64, msbLifRdToHost, lsbLifRdToHost);
        printf(" %s", buf);
        printf("\n");

        snprintf(strBuf, NVME_PRINT_SMART_LOG_STRING_BUFFER_LENGTH, "%s", (print_ext_smart_id(VS_ATTR_ID_TRIM_COUNT_LSB) + 7));
        printf("%-40s", strBuf);
        printf("%-15d", VS_ATTR_ID_TRIM_COUNT_MSB << 8 | VS_ATTR_ID_TRIM_COUNT_LSB);

        snprintf(buf, NVME_PRINT_SMART_LOG_STRING_BUFFER_LENGTH, "0x%016" PRIX64 "%016" PRIX64, msbTrimCnt, lsbTrimCnt);
        printf(" %s", buf);
        printf("\n");
    }
}

void print_smart_log_CF(fb_log_page_CF *pLogPageCF)
{
    uint64_t currentTemp, maxTemp;
    printf("\n\nSeagate DRAM Supercap SMART Attributes :\n");
    printf("%-39s %-19s \n", "Description", "Supercap Attributes");

    printf("%-40s", "Super-cap current temperature");
    currentTemp = pLogPageCF->AttrCF.SuperCapCurrentTemperature;
    /*currentTemp = currentTemp ? currentTemp - 273 : 0;*/
    printf(" 0x%016" PRIX64 "", currentTemp);
    printf("\n");

    maxTemp = pLogPageCF->AttrCF.SuperCapMaximumTemperature;
    /*maxTemp = maxTemp ? maxTemp - 273 : 0;*/
    printf("%-40s", "Super-cap maximum temperature");
    printf(" 0x%016" PRIX64 "", maxTemp);
    printf("\n");

    printf("%-40s", "Super-cap status");
    printf(" 0x%016" PRIX64 "", C_CAST(uint64_t, pLogPageCF->AttrCF.SuperCapStatus));
    printf("\n");

    printf("%-40s", "Data units read to DRAM namespace");
    printf(" 0x%016" PRIX64 "%016" PRIX64 "", pLogPageCF->AttrCF.DataUnitsReadToDramNamespace.MSU64,
        pLogPageCF->AttrCF.DataUnitsReadToDramNamespace.LSU64);
    printf("\n");

    printf("%-40s", "Data units written to DRAM namespace");
    printf(" 0x%016" PRIX64 "%016" PRIX64 "", pLogPageCF->AttrCF.DataUnitsWrittenToDramNamespace.MSU64,
        pLogPageCF->AttrCF.DataUnitsWrittenToDramNamespace.LSU64);
    printf("\n");

    printf("%-40s", "DRAM correctable error count");
    printf(" 0x%016" PRIX64 "", pLogPageCF->AttrCF.DramCorrectableErrorCount);
    printf("\n");

    printf("%-40s", "DRAM uncorrectable error count");
    printf(" 0x%016" PRIX64 "", pLogPageCF->AttrCF.DramUncorrectableErrorCount);
    printf("\n");

}

//Seagate Unique...
eReturnValues get_Ext_Smrt_Log(tDevice *device)//, nvmeGetLogPageCmdOpts * getLogPageCmdOpts)
{
    if (is_Seagate_Family(device) == SEAGATE_VENDOR_SSD_PJ)
    {
#ifdef _DEBUG
        printf("-->%s\n", __FUNCTION__);
#endif
        eReturnValues ret = 0;
        int index = 0;
        EXTENDED_SMART_INFO_T ExtdSMARTInfo;
        memset(&ExtdSMARTInfo, 0x00, sizeof(ExtdSMARTInfo));
        ret = nvme_Read_Ext_Smt_Log(device, &ExtdSMARTInfo);
        if (!ret) {
            printf("%-39s %-15s %-19s \n", "Description", "Ext-Smart-Id", "Ext-Smart-Value");
            for (index = 0; index < 80; index++)
                printf("-");
            printf("\n");
            for (index = 0; index < NUMBER_EXTENDED_SMART_ATTRIBUTES; index++)
                print_smart_log(ExtdSMARTInfo.Version, ExtdSMARTInfo.vendorData[index], index == (NUMBER_EXTENDED_SMART_ATTRIBUTES - 1));

        }
        return SUCCESS;
    }
    else
    {
        return NOT_SUPPORTED;
    }
}

eReturnValues clr_Pcie_Correctable_Errs(tDevice *device)
{
    if (is_Seagate_Family(device) == SEAGATE_VENDOR_SSD_PJ)
    {
        //const char *desc = "Clear Seagate PCIe Correctable counters for the given device ";
        //const char *save = "specifies that the controller shall save the attribute";
        eReturnValues err = SUCCESS;

        nvmeFeaturesCmdOpt clearPCIeCorrectableErrors;
        memset(&clearPCIeCorrectableErrors, 0, sizeof(nvmeFeaturesCmdOpt));
        clearPCIeCorrectableErrors.fid = 0xE1;
        clearPCIeCorrectableErrors.featSetGetValue = 0xCB;
        clearPCIeCorrectableErrors.sv = false;
        err = nvme_Set_Features(device, &clearPCIeCorrectableErrors);

        return err;
    }
    else
    {
        return NOT_SUPPORTED;
    }
}

typedef enum _eATAMaxSupportLogEntries
{
    SUPPORTED_MAX_ENTRIES_VERSION_1 = 8,
    SUPPORTED_MAX_ENTRIES_VERSION_2 = 22
} eATAMaxSupportLogEntries;

bool is_Seagate_DeviceStatistics_Supported(tDevice *device)
{
    bool supported = false;
    uint32_t logSize = 0;

    if ((device->drive_info.drive_type == ATA_DRIVE) && (get_ATA_Log_Size(device, 0xC7, &logSize, true, false) == SUCCESS))
    {
        supported = true;
    }
    else if ((device->drive_info.drive_type == SCSI_DRIVE) && (get_SCSI_Log_Size(device, 0x2F, 0x00, &logSize) == SUCCESS))
    {
        supported = true;
    }
#if defined (_DEBUG)
    else
        printf("\nSeagate Device Statistics logs not supported.\n");
#endif

    return supported;
}

static eReturnValues get_Seagate_ATA_DeviceStatistics(tDevice *device, ptrSeagateDeviceStatistics seagateDeviceStats)
{
    eReturnValues ret = NOT_SUPPORTED;
    if (!seagateDeviceStats)
    {
        return BAD_PARAMETER;
    }

    uint32_t deviceStatsSize = 0;
    //need to get the seagate device statistics log
    if (SUCCESS == get_ATA_Log_Size(device, 0xC7, &deviceStatsSize, true, false))
    {
        uint8_t* deviceStatsLog = C_CAST(uint8_t*, safe_calloc_aligned(deviceStatsSize, sizeof(uint8_t), device->os_info.minimumAlignment));
        if (!deviceStatsLog)
        {
            return MEMORY_FAILURE;
        }

        if (SUCCESS == get_ATA_Log(device, 0xC7, M_NULLPTR, M_NULLPTR, true, false, true, deviceStatsLog, deviceStatsSize, M_NULLPTR, 0, 0))
        {
            ret = SUCCESS;
            uint8_t maxLogEntries = 0;
            uint64_t* qwordPtrDeviceStatsLog = C_CAST(uint64_t*, &deviceStatsLog[0]);       //log version
            if (*qwordPtrDeviceStatsLog == 0x01)
                maxLogEntries = SUPPORTED_MAX_ENTRIES_VERSION_1;
            else if (*qwordPtrDeviceStatsLog == 0x02)
                maxLogEntries = SUPPORTED_MAX_ENTRIES_VERSION_2;
            else
                return NOT_SUPPORTED;

            seagateDeviceStats->sataStatistics.version = maxLogEntries;
            for (uint8_t logEntry = 0; logEntry < maxLogEntries; ++logEntry)
            {
                uint32_t offset = UINT32_C(16) + (C_CAST(uint32_t, logEntry) * UINT32_C(8));
                if (offset > deviceStatsSize)
                {
                    break;
                }
                qwordPtrDeviceStatsLog = C_CAST(uint64_t*, &deviceStatsLog[offset]);
                switch (logEntry)
                {
                case 0:
                    //Sanitize Crypto Erase Pass Count Statistic
                    seagateDeviceStats->sataStatistics.sanitizeCryptoErasePassCount.isSupported = qwordPtrDeviceStatsLog[0] & BIT63;
                    seagateDeviceStats->sataStatistics.sanitizeCryptoErasePassCount.isValueValid = qwordPtrDeviceStatsLog[0] & BIT62;
                    seagateDeviceStats->sataStatistics.sanitizeCryptoErasePassCount.isNormalized = qwordPtrDeviceStatsLog[0] & BIT61;
                    seagateDeviceStats->sataStatistics.sanitizeCryptoErasePassCount.failureInfo = M_Byte5(qwordPtrDeviceStatsLog[0]);
                    seagateDeviceStats->sataStatistics.sanitizeCryptoErasePassCount.isTimeStampsInMinutes = qwordPtrDeviceStatsLog[0] & BIT39;
                    seagateDeviceStats->sataStatistics.sanitizeCryptoErasePassCount.statisticsDataValue = M_DoubleWord0(qwordPtrDeviceStatsLog[0]);
                    break;

                case 1:
                    //Sanitize Crypto Erase Pass Timestamp Statistic
                    seagateDeviceStats->sataStatistics.sanitizeCryptoErasePassTimeStamp.isSupported = qwordPtrDeviceStatsLog[0] & BIT63;
                    seagateDeviceStats->sataStatistics.sanitizeCryptoErasePassTimeStamp.isValueValid = qwordPtrDeviceStatsLog[0] & BIT62;
                    seagateDeviceStats->sataStatistics.sanitizeCryptoErasePassTimeStamp.isNormalized = qwordPtrDeviceStatsLog[0] & BIT61;
                    seagateDeviceStats->sataStatistics.sanitizeCryptoErasePassTimeStamp.failureInfo = M_Byte5(qwordPtrDeviceStatsLog[0]);
                    seagateDeviceStats->sataStatistics.sanitizeCryptoErasePassTimeStamp.isTimeStampsInMinutes = qwordPtrDeviceStatsLog[0] & BIT39;
                    seagateDeviceStats->sataStatistics.sanitizeCryptoErasePassTimeStamp.statisticsDataValue = M_DoubleWord0(qwordPtrDeviceStatsLog[0]);
                    break;

                case 2:
                    //Sanitize Overwrite Erase Pass Count Statistic
                    seagateDeviceStats->sataStatistics.sanitizeOverwriteErasePassCount.isSupported = qwordPtrDeviceStatsLog[0] & BIT63;
                    seagateDeviceStats->sataStatistics.sanitizeOverwriteErasePassCount.isValueValid = qwordPtrDeviceStatsLog[0] & BIT62;
                    seagateDeviceStats->sataStatistics.sanitizeOverwriteErasePassCount.isNormalized = qwordPtrDeviceStatsLog[0] & BIT61;
                    seagateDeviceStats->sataStatistics.sanitizeOverwriteErasePassCount.failureInfo = M_Byte5(qwordPtrDeviceStatsLog[0]);
                    seagateDeviceStats->sataStatistics.sanitizeOverwriteErasePassCount.isTimeStampsInMinutes = qwordPtrDeviceStatsLog[0] & BIT39;
                    seagateDeviceStats->sataStatistics.sanitizeOverwriteErasePassCount.statisticsDataValue = M_DoubleWord0(qwordPtrDeviceStatsLog[0]);
                    break;

                case 3:
                    //Sanitize Overwrite Erase Pass Timestamp Statistic
                    seagateDeviceStats->sataStatistics.sanitizeOverwriteErasePassTimeStamp.isSupported = qwordPtrDeviceStatsLog[0] & BIT63;
                    seagateDeviceStats->sataStatistics.sanitizeOverwriteErasePassTimeStamp.isValueValid = qwordPtrDeviceStatsLog[0] & BIT62;
                    seagateDeviceStats->sataStatistics.sanitizeOverwriteErasePassTimeStamp.isNormalized = qwordPtrDeviceStatsLog[0] & BIT61;
                    seagateDeviceStats->sataStatistics.sanitizeOverwriteErasePassTimeStamp.failureInfo = M_Byte5(qwordPtrDeviceStatsLog[0]);
                    seagateDeviceStats->sataStatistics.sanitizeOverwriteErasePassTimeStamp.isTimeStampsInMinutes = qwordPtrDeviceStatsLog[0] & BIT39;
                    seagateDeviceStats->sataStatistics.sanitizeOverwriteErasePassTimeStamp.statisticsDataValue = M_DoubleWord0(qwordPtrDeviceStatsLog[0]);
                    break;

                case 4:
                    //Sanitize Block Erase Pass Count Statistic
                    seagateDeviceStats->sataStatistics.sanitizeBlockErasePassCount.isSupported = qwordPtrDeviceStatsLog[0] & BIT63;
                    seagateDeviceStats->sataStatistics.sanitizeBlockErasePassCount.isValueValid = qwordPtrDeviceStatsLog[0] & BIT62;
                    seagateDeviceStats->sataStatistics.sanitizeBlockErasePassCount.isNormalized = qwordPtrDeviceStatsLog[0] & BIT61;
                    seagateDeviceStats->sataStatistics.sanitizeBlockErasePassCount.failureInfo = M_Byte5(qwordPtrDeviceStatsLog[0]);
                    seagateDeviceStats->sataStatistics.sanitizeBlockErasePassCount.isTimeStampsInMinutes = qwordPtrDeviceStatsLog[0] & BIT39;
                    seagateDeviceStats->sataStatistics.sanitizeBlockErasePassCount.statisticsDataValue = M_DoubleWord0(qwordPtrDeviceStatsLog[0]);
                    break;

                case 5:
                    //Sanitize Block Erase Pass Timestamp Statistic
                    seagateDeviceStats->sataStatistics.sanitizeBlockErasePassTimeStamp.isSupported = qwordPtrDeviceStatsLog[0] & BIT63;
                    seagateDeviceStats->sataStatistics.sanitizeBlockErasePassTimeStamp.isValueValid = qwordPtrDeviceStatsLog[0] & BIT62;
                    seagateDeviceStats->sataStatistics.sanitizeBlockErasePassTimeStamp.isNormalized = qwordPtrDeviceStatsLog[0] & BIT61;
                    seagateDeviceStats->sataStatistics.sanitizeBlockErasePassTimeStamp.failureInfo = M_Byte5(qwordPtrDeviceStatsLog[0]);
                    seagateDeviceStats->sataStatistics.sanitizeBlockErasePassTimeStamp.isTimeStampsInMinutes = qwordPtrDeviceStatsLog[0] & BIT39;
                    seagateDeviceStats->sataStatistics.sanitizeBlockErasePassTimeStamp.statisticsDataValue = M_DoubleWord0(qwordPtrDeviceStatsLog[0]);
                    break;

                case 6:
                    //Ata Security Erase Unit Pass Count Statistic
                    seagateDeviceStats->sataStatistics.ataSecurityEraseUnitPassCount.isSupported = qwordPtrDeviceStatsLog[0] & BIT63;
                    seagateDeviceStats->sataStatistics.ataSecurityEraseUnitPassCount.isValueValid = qwordPtrDeviceStatsLog[0] & BIT62;
                    seagateDeviceStats->sataStatistics.ataSecurityEraseUnitPassCount.isNormalized = qwordPtrDeviceStatsLog[0] & BIT61;
                    seagateDeviceStats->sataStatistics.ataSecurityEraseUnitPassCount.failureInfo = M_Byte5(qwordPtrDeviceStatsLog[0]);
                    seagateDeviceStats->sataStatistics.ataSecurityEraseUnitPassCount.isTimeStampsInMinutes = qwordPtrDeviceStatsLog[0] & BIT39;
                    seagateDeviceStats->sataStatistics.ataSecurityEraseUnitPassCount.statisticsDataValue = M_DoubleWord0(qwordPtrDeviceStatsLog[0]);
                    break;

                case 7:
                    //Ata Security Erase Unit Pass Timestamp Statistic
                    seagateDeviceStats->sataStatistics.ataSecurityEraseUnitPassTimeStamp.isSupported = qwordPtrDeviceStatsLog[0] & BIT63;
                    seagateDeviceStats->sataStatistics.ataSecurityEraseUnitPassTimeStamp.isValueValid = qwordPtrDeviceStatsLog[0] & BIT62;
                    seagateDeviceStats->sataStatistics.ataSecurityEraseUnitPassTimeStamp.isNormalized = qwordPtrDeviceStatsLog[0] & BIT61;
                    seagateDeviceStats->sataStatistics.ataSecurityEraseUnitPassTimeStamp.failureInfo = M_Byte5(qwordPtrDeviceStatsLog[0]);
                    seagateDeviceStats->sataStatistics.ataSecurityEraseUnitPassTimeStamp.isTimeStampsInMinutes = qwordPtrDeviceStatsLog[0] & BIT39;
                    seagateDeviceStats->sataStatistics.ataSecurityEraseUnitPassTimeStamp.statisticsDataValue = M_DoubleWord0(qwordPtrDeviceStatsLog[0]);
                    break;

                case 8:
                    //Erase Security File Failure Count Statistic
                    seagateDeviceStats->sataStatistics.eraseSecurityFileFailureCount.isSupported = qwordPtrDeviceStatsLog[0] & BIT63;
                    seagateDeviceStats->sataStatistics.eraseSecurityFileFailureCount.isValueValid = qwordPtrDeviceStatsLog[0] & BIT62;
                    seagateDeviceStats->sataStatistics.eraseSecurityFileFailureCount.isNormalized = qwordPtrDeviceStatsLog[0] & BIT61;
                    seagateDeviceStats->sataStatistics.eraseSecurityFileFailureCount.failureInfo = M_Byte5(qwordPtrDeviceStatsLog[0]);
                    seagateDeviceStats->sataStatistics.eraseSecurityFileFailureCount.isTimeStampsInMinutes = qwordPtrDeviceStatsLog[0] & BIT39;
                    seagateDeviceStats->sataStatistics.eraseSecurityFileFailureCount.statisticsDataValue = M_DoubleWord0(qwordPtrDeviceStatsLog[0]);
                    break;

                case 9:
                    //Erase Security File Failure Timestamp Statistic
                    seagateDeviceStats->sataStatistics.eraseSecurityFileFailureTimeStamp.isSupported = qwordPtrDeviceStatsLog[0] & BIT63;
                    seagateDeviceStats->sataStatistics.eraseSecurityFileFailureTimeStamp.isValueValid = qwordPtrDeviceStatsLog[0] & BIT62;
                    seagateDeviceStats->sataStatistics.eraseSecurityFileFailureTimeStamp.isNormalized = qwordPtrDeviceStatsLog[0] & BIT61;
                    seagateDeviceStats->sataStatistics.eraseSecurityFileFailureTimeStamp.failureInfo = M_Byte5(qwordPtrDeviceStatsLog[0]);
                    seagateDeviceStats->sataStatistics.eraseSecurityFileFailureTimeStamp.isTimeStampsInMinutes = qwordPtrDeviceStatsLog[0] & BIT39;
                    seagateDeviceStats->sataStatistics.eraseSecurityFileFailureTimeStamp.statisticsDataValue = M_DoubleWord0(qwordPtrDeviceStatsLog[0]);
                    break;

                case 10:
                    //Ata Security Erase Unit Enhanced Pass Count Statistic
                    seagateDeviceStats->sataStatistics.ataSecurityEraseUnitEnhancedPassCount.isSupported = qwordPtrDeviceStatsLog[0] & BIT63;
                    seagateDeviceStats->sataStatistics.ataSecurityEraseUnitEnhancedPassCount.isValueValid = qwordPtrDeviceStatsLog[0] & BIT62;
                    seagateDeviceStats->sataStatistics.ataSecurityEraseUnitEnhancedPassCount.isNormalized = qwordPtrDeviceStatsLog[0] & BIT61;
                    seagateDeviceStats->sataStatistics.ataSecurityEraseUnitEnhancedPassCount.failureInfo = M_Byte5(qwordPtrDeviceStatsLog[0]);
                    seagateDeviceStats->sataStatistics.ataSecurityEraseUnitEnhancedPassCount.isTimeStampsInMinutes = qwordPtrDeviceStatsLog[0] & BIT39;
                    seagateDeviceStats->sataStatistics.ataSecurityEraseUnitEnhancedPassCount.statisticsDataValue = M_DoubleWord0(qwordPtrDeviceStatsLog[0]);
                    break;

                case 11:
                    //Ata Security Erase Unit Enhanced Pass Timestamp Statistic
                    seagateDeviceStats->sataStatistics.ataSecurityEraseUnitEnhancedPassTimeStamp.isSupported = qwordPtrDeviceStatsLog[0] & BIT63;
                    seagateDeviceStats->sataStatistics.ataSecurityEraseUnitEnhancedPassTimeStamp.isValueValid = qwordPtrDeviceStatsLog[0] & BIT62;
                    seagateDeviceStats->sataStatistics.ataSecurityEraseUnitEnhancedPassTimeStamp.isNormalized = qwordPtrDeviceStatsLog[0] & BIT61;
                    seagateDeviceStats->sataStatistics.ataSecurityEraseUnitEnhancedPassTimeStamp.failureInfo = M_Byte5(qwordPtrDeviceStatsLog[0]);
                    seagateDeviceStats->sataStatistics.ataSecurityEraseUnitEnhancedPassTimeStamp.isTimeStampsInMinutes = qwordPtrDeviceStatsLog[0] & BIT39;
                    seagateDeviceStats->sataStatistics.ataSecurityEraseUnitEnhancedPassTimeStamp.statisticsDataValue = M_DoubleWord0(qwordPtrDeviceStatsLog[0]);
                    break;

                case 12:
                    //Sanitize Crypto Erase Failure Count Statistic
                    seagateDeviceStats->sataStatistics.sanitizeCryptoEraseFailCount.isSupported = qwordPtrDeviceStatsLog[0] & BIT63;
                    seagateDeviceStats->sataStatistics.sanitizeCryptoEraseFailCount.isValueValid = qwordPtrDeviceStatsLog[0] & BIT62;
                    seagateDeviceStats->sataStatistics.sanitizeCryptoEraseFailCount.isNormalized = qwordPtrDeviceStatsLog[0] & BIT61;
                    seagateDeviceStats->sataStatistics.sanitizeCryptoEraseFailCount.failureInfo = M_Byte5(qwordPtrDeviceStatsLog[0]);
                    seagateDeviceStats->sataStatistics.sanitizeCryptoEraseFailCount.isTimeStampsInMinutes = qwordPtrDeviceStatsLog[0] & BIT39;
                    seagateDeviceStats->sataStatistics.sanitizeCryptoEraseFailCount.statisticsDataValue = M_DoubleWord0(qwordPtrDeviceStatsLog[0]);
                    break;

                case 13:
                    //Sanitize Crypto Erase Failure Timestamp Statistic
                    seagateDeviceStats->sataStatistics.sanitizeCryptoEraseFailTimeStamp.isSupported = qwordPtrDeviceStatsLog[0] & BIT63;
                    seagateDeviceStats->sataStatistics.sanitizeCryptoEraseFailTimeStamp.isValueValid = qwordPtrDeviceStatsLog[0] & BIT62;
                    seagateDeviceStats->sataStatistics.sanitizeCryptoEraseFailTimeStamp.isNormalized = qwordPtrDeviceStatsLog[0] & BIT61;
                    seagateDeviceStats->sataStatistics.sanitizeCryptoEraseFailTimeStamp.failureInfo = M_Byte5(qwordPtrDeviceStatsLog[0]);
                    seagateDeviceStats->sataStatistics.sanitizeCryptoEraseFailTimeStamp.isTimeStampsInMinutes = qwordPtrDeviceStatsLog[0] & BIT39;
                    seagateDeviceStats->sataStatistics.sanitizeCryptoEraseFailTimeStamp.statisticsDataValue = M_DoubleWord0(qwordPtrDeviceStatsLog[0]);
                    break;

                case 14:
                    //Sanitize Overwrite Erase Failure Count Statistic
                    seagateDeviceStats->sataStatistics.sanitizeOverwriteEraseFailCount.isSupported = qwordPtrDeviceStatsLog[0] & BIT63;
                    seagateDeviceStats->sataStatistics.sanitizeOverwriteEraseFailCount.isValueValid = qwordPtrDeviceStatsLog[0] & BIT62;
                    seagateDeviceStats->sataStatistics.sanitizeOverwriteEraseFailCount.isNormalized = qwordPtrDeviceStatsLog[0] & BIT61;
                    seagateDeviceStats->sataStatistics.sanitizeOverwriteEraseFailCount.failureInfo = M_Byte5(qwordPtrDeviceStatsLog[0]);
                    seagateDeviceStats->sataStatistics.sanitizeOverwriteEraseFailCount.isTimeStampsInMinutes = qwordPtrDeviceStatsLog[0] & BIT39;
                    seagateDeviceStats->sataStatistics.sanitizeOverwriteEraseFailCount.statisticsDataValue = M_DoubleWord0(qwordPtrDeviceStatsLog[0]);
                    break;

                case 15:
                    //Sanitize Overwrite Erase Failure Timestamp Statistic
                    seagateDeviceStats->sataStatistics.sanitizeOverwriteEraseFailTimeStamp.isSupported = qwordPtrDeviceStatsLog[0] & BIT63;
                    seagateDeviceStats->sataStatistics.sanitizeOverwriteEraseFailTimeStamp.isValueValid = qwordPtrDeviceStatsLog[0] & BIT62;
                    seagateDeviceStats->sataStatistics.sanitizeOverwriteEraseFailTimeStamp.isNormalized = qwordPtrDeviceStatsLog[0] & BIT61;
                    seagateDeviceStats->sataStatistics.sanitizeOverwriteEraseFailTimeStamp.failureInfo = M_Byte5(qwordPtrDeviceStatsLog[0]);
                    seagateDeviceStats->sataStatistics.sanitizeOverwriteEraseFailTimeStamp.isTimeStampsInMinutes = qwordPtrDeviceStatsLog[0] & BIT39;
                    seagateDeviceStats->sataStatistics.sanitizeOverwriteEraseFailTimeStamp.statisticsDataValue = M_DoubleWord0(qwordPtrDeviceStatsLog[0]);
                    break;

                case 16:
                    //Sanitize Block Erase Failure Count Statistic
                    seagateDeviceStats->sataStatistics.sanitizeBlockEraseFailCount.isSupported = qwordPtrDeviceStatsLog[0] & BIT63;
                    seagateDeviceStats->sataStatistics.sanitizeBlockEraseFailCount.isValueValid = qwordPtrDeviceStatsLog[0] & BIT62;
                    seagateDeviceStats->sataStatistics.sanitizeBlockEraseFailCount.isNormalized = qwordPtrDeviceStatsLog[0] & BIT61;
                    seagateDeviceStats->sataStatistics.sanitizeBlockEraseFailCount.failureInfo = M_Byte5(qwordPtrDeviceStatsLog[0]);
                    seagateDeviceStats->sataStatistics.sanitizeBlockEraseFailCount.isTimeStampsInMinutes = qwordPtrDeviceStatsLog[0] & BIT39;
                    seagateDeviceStats->sataStatistics.sanitizeBlockEraseFailCount.statisticsDataValue = M_DoubleWord0(qwordPtrDeviceStatsLog[0]);
                    break;

                case 17:
                    //Sanitize Block Erase Failure Timestamp Statistic
                    seagateDeviceStats->sataStatistics.sanitizeBlockEraseFailTimeStamp.isSupported = qwordPtrDeviceStatsLog[0] & BIT63;
                    seagateDeviceStats->sataStatistics.sanitizeBlockEraseFailTimeStamp.isValueValid = qwordPtrDeviceStatsLog[0] & BIT62;
                    seagateDeviceStats->sataStatistics.sanitizeBlockEraseFailTimeStamp.isNormalized = qwordPtrDeviceStatsLog[0] & BIT61;
                    seagateDeviceStats->sataStatistics.sanitizeBlockEraseFailTimeStamp.failureInfo = M_Byte5(qwordPtrDeviceStatsLog[0]);
                    seagateDeviceStats->sataStatistics.sanitizeBlockEraseFailTimeStamp.isTimeStampsInMinutes = qwordPtrDeviceStatsLog[0] & BIT39;
                    seagateDeviceStats->sataStatistics.sanitizeBlockEraseFailTimeStamp.statisticsDataValue = M_DoubleWord0(qwordPtrDeviceStatsLog[0]);
                    break;

                case 18:
                    //Ata Security Erase Unit Failure Count Statistic
                    seagateDeviceStats->sataStatistics.ataSecurityEraseUnitFailCount.isSupported = qwordPtrDeviceStatsLog[0] & BIT63;
                    seagateDeviceStats->sataStatistics.ataSecurityEraseUnitFailCount.isValueValid = qwordPtrDeviceStatsLog[0] & BIT62;
                    seagateDeviceStats->sataStatistics.ataSecurityEraseUnitFailCount.isNormalized = qwordPtrDeviceStatsLog[0] & BIT61;
                    seagateDeviceStats->sataStatistics.ataSecurityEraseUnitFailCount.failureInfo = M_Byte5(qwordPtrDeviceStatsLog[0]);
                    seagateDeviceStats->sataStatistics.ataSecurityEraseUnitFailCount.isTimeStampsInMinutes = qwordPtrDeviceStatsLog[0] & BIT39;
                    seagateDeviceStats->sataStatistics.ataSecurityEraseUnitFailCount.statisticsDataValue = M_DoubleWord0(qwordPtrDeviceStatsLog[0]);
                    break;

                case 19:
                    //Ata Security Erase Unit Failure Timestamp Statistic
                    seagateDeviceStats->sataStatistics.ataSecurityEraseUnitFailTimeStamp.isSupported = qwordPtrDeviceStatsLog[0] & BIT63;
                    seagateDeviceStats->sataStatistics.ataSecurityEraseUnitFailTimeStamp.isValueValid = qwordPtrDeviceStatsLog[0] & BIT62;
                    seagateDeviceStats->sataStatistics.ataSecurityEraseUnitFailTimeStamp.isNormalized = qwordPtrDeviceStatsLog[0] & BIT61;
                    seagateDeviceStats->sataStatistics.ataSecurityEraseUnitFailTimeStamp.failureInfo = M_Byte5(qwordPtrDeviceStatsLog[0]);
                    seagateDeviceStats->sataStatistics.ataSecurityEraseUnitFailTimeStamp.isTimeStampsInMinutes = qwordPtrDeviceStatsLog[0] & BIT39;
                    seagateDeviceStats->sataStatistics.ataSecurityEraseUnitFailTimeStamp.statisticsDataValue = M_DoubleWord0(qwordPtrDeviceStatsLog[0]);
                    break;

                case 20:
                    //Ata Security Erase Unit Enhanced Failure Count Statistic
                    seagateDeviceStats->sataStatistics.ataSecurityEraseUnitEnhancedFailCount.isSupported = qwordPtrDeviceStatsLog[0] & BIT63;
                    seagateDeviceStats->sataStatistics.ataSecurityEraseUnitEnhancedFailCount.isValueValid = qwordPtrDeviceStatsLog[0] & BIT62;
                    seagateDeviceStats->sataStatistics.ataSecurityEraseUnitEnhancedFailCount.isNormalized = qwordPtrDeviceStatsLog[0] & BIT61;
                    seagateDeviceStats->sataStatistics.ataSecurityEraseUnitEnhancedFailCount.failureInfo = M_Byte5(qwordPtrDeviceStatsLog[0]);
                    seagateDeviceStats->sataStatistics.ataSecurityEraseUnitEnhancedFailCount.isTimeStampsInMinutes = qwordPtrDeviceStatsLog[0] & BIT39;
                    seagateDeviceStats->sataStatistics.ataSecurityEraseUnitEnhancedFailCount.statisticsDataValue = M_DoubleWord0(qwordPtrDeviceStatsLog[0]);
                    break;

                case 21:
                    //Ata Security Erase Unit Enhanced Failure Timestamp Statistic
                    seagateDeviceStats->sataStatistics.ataSecurityEraseUnitEnhancedFailTimeStamp.isSupported = qwordPtrDeviceStatsLog[0] & BIT63;
                    seagateDeviceStats->sataStatistics.ataSecurityEraseUnitEnhancedFailTimeStamp.isValueValid = qwordPtrDeviceStatsLog[0] & BIT62;
                    seagateDeviceStats->sataStatistics.ataSecurityEraseUnitEnhancedFailTimeStamp.isNormalized = qwordPtrDeviceStatsLog[0] & BIT61;
                    seagateDeviceStats->sataStatistics.ataSecurityEraseUnitEnhancedFailTimeStamp.failureInfo = M_Byte5(qwordPtrDeviceStatsLog[0]);
                    seagateDeviceStats->sataStatistics.ataSecurityEraseUnitEnhancedFailTimeStamp.isTimeStampsInMinutes = qwordPtrDeviceStatsLog[0] & BIT39;
                    seagateDeviceStats->sataStatistics.ataSecurityEraseUnitEnhancedFailTimeStamp.statisticsDataValue = M_DoubleWord0(qwordPtrDeviceStatsLog[0]);
                    break;

                default:
                    break;
                }
            }
        }

        safe_free_aligned(&deviceStatsLog);
    }

    return ret;
}

typedef enum _eSeagateSMARTStatusLogPageParamCode
{
    SANITIZE_CRYPTO_ERASE_STATISTICS = 0x0040,
    SANITIZE_OVERWRITE_STATISTICS = 0x0041,
    SANITIZE_BLOCK_ERASE_STATISTICS = 0x0042,
    ERASE_SECURITY_FILE_FAILURES = 0x0050,
} eSeagateSMARTStatusLogPageParamCode;

static eReturnValues get_Seagate_SCSI_DeviceStatistics(tDevice *device, ptrSeagateDeviceStatistics seagateDeviceStats)
{
    eReturnValues ret = NOT_SUPPORTED;
    if (!seagateDeviceStats)
    {
        return BAD_PARAMETER;
    }

    uint32_t deviceStatsSize = 0;
    if (SUCCESS == get_SCSI_Log_Size(device, 0x2F, 0x00, &deviceStatsSize))
    {
        uint8_t* deviceStatsLog = C_CAST(uint8_t*, safe_calloc_aligned(deviceStatsSize, sizeof(uint8_t), device->os_info.minimumAlignment));
        if (!deviceStatsLog)
        {
            return MEMORY_FAILURE;
        }

        if (SUCCESS == get_SCSI_Log(device, 0x2F, 0x00, M_NULLPTR, M_NULLPTR, true, deviceStatsLog, deviceStatsSize, M_NULLPTR))
        {
            ret = SUCCESS;

            uint16_t pageLength = M_BytesTo2ByteValue(deviceStatsLog[2], deviceStatsLog[3]);
            uint8_t parameterLength = 0;
            for (uint16_t iter = UINT16_C(4); iter < pageLength; iter += C_CAST(uint16_t, parameterLength + UINT16_C(4)))
            {
                uint16_t parameterCode = M_BytesTo2ByteValue(deviceStatsLog[iter], deviceStatsLog[iter + 1]);
                parameterLength = deviceStatsLog[iter + 3];
                switch (parameterCode)
                {
                case SANITIZE_CRYPTO_ERASE_STATISTICS:
                    seagateDeviceStats->sasStatistics.sanitizeCryptoEraseCount.isValueValid = true;
                    seagateDeviceStats->sasStatistics.sanitizeCryptoEraseCount.statisticsDataValue = M_BytesTo4ByteValue(deviceStatsLog[iter + 4], deviceStatsLog[iter + 5], deviceStatsLog[iter + 6], deviceStatsLog[iter + 7]);
                    if (seagateDeviceStats->sasStatistics.sanitizeCryptoEraseCount.statisticsDataValue != 0)
                    {
                        seagateDeviceStats->sasStatistics.sanitizeCryptoEraseTimeStamp.isValueValid = true;
                        seagateDeviceStats->sasStatistics.sanitizeCryptoEraseTimeStamp.isTimeStampsInMinutes = true;
                        seagateDeviceStats->sasStatistics.sanitizeCryptoEraseTimeStamp.statisticsDataValue = M_BytesTo4ByteValue(deviceStatsLog[iter + 8], deviceStatsLog[iter + 9], deviceStatsLog[iter + 10], deviceStatsLog[iter + 11]);
                    }
                    break;

                case SANITIZE_OVERWRITE_STATISTICS:
                    seagateDeviceStats->sasStatistics.sanitizeOverwriteEraseCount.isValueValid = true;
                    seagateDeviceStats->sasStatistics.sanitizeOverwriteEraseCount.statisticsDataValue = M_BytesTo4ByteValue(deviceStatsLog[iter + 4], deviceStatsLog[iter + 5], deviceStatsLog[iter + 6], deviceStatsLog[iter + 7]);
                    if (seagateDeviceStats->sasStatistics.sanitizeOverwriteEraseCount.statisticsDataValue != 0)
                    {
                        seagateDeviceStats->sasStatistics.sanitizeOverwriteEraseTimeStamp.isValueValid = true;
                        seagateDeviceStats->sasStatistics.sanitizeOverwriteEraseTimeStamp.isTimeStampsInMinutes = true;
                        seagateDeviceStats->sasStatistics.sanitizeOverwriteEraseTimeStamp.statisticsDataValue = M_BytesTo4ByteValue(deviceStatsLog[iter + 8], deviceStatsLog[iter + 9], deviceStatsLog[iter + 10], deviceStatsLog[iter + 11]);
                    }
                    break;

                case SANITIZE_BLOCK_ERASE_STATISTICS:
                    seagateDeviceStats->sasStatistics.sanitizeBlockEraseCount.isValueValid = true;
                    seagateDeviceStats->sasStatistics.sanitizeBlockEraseCount.statisticsDataValue = M_BytesTo4ByteValue(deviceStatsLog[iter + 4], deviceStatsLog[iter + 5], deviceStatsLog[iter + 6], deviceStatsLog[iter + 7]);
                    if (seagateDeviceStats->sasStatistics.sanitizeBlockEraseCount.statisticsDataValue != 0)
                    {
                        seagateDeviceStats->sasStatistics.sanitizeBlockEraseTimeStamp.isValueValid = true;
                        seagateDeviceStats->sasStatistics.sanitizeBlockEraseTimeStamp.isTimeStampsInMinutes = true;
                        seagateDeviceStats->sasStatistics.sanitizeBlockEraseTimeStamp.statisticsDataValue = M_BytesTo4ByteValue(deviceStatsLog[iter + 8], deviceStatsLog[iter + 9], deviceStatsLog[iter + 10], deviceStatsLog[iter + 11]);
                    }
                    break;

                case ERASE_SECURITY_FILE_FAILURES:
                    seagateDeviceStats->sasStatistics.eraseSecurityFileFailureCount.isValueValid = true;
                    seagateDeviceStats->sasStatistics.eraseSecurityFileFailureCount.statisticsDataValue = M_BytesTo4ByteValue(deviceStatsLog[iter + 4], deviceStatsLog[iter + 5], deviceStatsLog[iter + 6], deviceStatsLog[iter + 7]);
                    if (seagateDeviceStats->sasStatistics.eraseSecurityFileFailureCount.statisticsDataValue != 0)
                    {
                        seagateDeviceStats->sasStatistics.eraseSecurityFileFailureTimeStamp.isValueValid = true;
                        seagateDeviceStats->sasStatistics.eraseSecurityFileFailureTimeStamp.isTimeStampsInMinutes = true;
                        seagateDeviceStats->sasStatistics.eraseSecurityFileFailureTimeStamp.statisticsDataValue = M_BytesTo4ByteValue(deviceStatsLog[iter + 8], deviceStatsLog[iter + 9], deviceStatsLog[iter + 10], deviceStatsLog[iter + 11]);
                    }
                    break;

                default:
                    break;
                }
            }
        }

        safe_free_aligned(&deviceStatsLog);
    }

    return ret;
}

eReturnValues get_Seagate_DeviceStatistics(tDevice *device, ptrSeagateDeviceStatistics seagateDeviceStats)
{
    eReturnValues ret = NOT_SUPPORTED;
    if (!seagateDeviceStats)
    {
        return BAD_PARAMETER;
    }

    if (device->drive_info.drive_type == ATA_DRIVE)
    {
        return get_Seagate_ATA_DeviceStatistics(device, seagateDeviceStats);
    }
    else if (device->drive_info.drive_type == SCSI_DRIVE)
    {
        return get_Seagate_SCSI_DeviceStatistics(device, seagateDeviceStats);
    }

    return ret;
}

static void print_Count_Statistics(const char *statisticsName, seagateStatistic statistics)
{
    printf("%-60s", statisticsName);
    if (statistics.isValueValid)
        printf("%"PRIu32, statistics.statisticsDataValue);
    else
        printf("Not Available");
    printf("\n");
}

static void print_TimeStamp_Statistics(const char *statisticsName, seagateStatistic statistics)
{
    printf("%-60s", statisticsName);
    if (statistics.isValueValid)
    {
        uint64_t timeInMinutes = C_CAST(uint64_t, statistics.statisticsDataValue);
        if (!statistics.isTimeStampsInMinutes)
            timeInMinutes *= UINT64_C(60);
        printf("%" PRIu64 " minutes", timeInMinutes);
    }
    else
        printf("Not Available");
    printf("\n");
}

static void print_Seagate_ATA_DeviceStatistics(ptrSeagateDeviceStatistics seagateDeviceStats)
{
    if (!seagateDeviceStats)
    {
        return;
    }

    printf("===Seagate Device Statistics===\n");

    printf(" %-60s %-16s\n", "Statistic Name:", "Value:");
    uint8_t maxLogEntries = seagateDeviceStats->sataStatistics.version;
    for (uint8_t logEntry = 0; logEntry < maxLogEntries; ++logEntry)
    {
        switch (logEntry)
        {
        case 0:
            print_Count_Statistics("Sanitize Crypto Erase Count", seagateDeviceStats->sataStatistics.sanitizeCryptoErasePassCount);
            break;

        case 1:
            print_TimeStamp_Statistics("Sanitize Crypto Erase Timestamp", seagateDeviceStats->sataStatistics.sanitizeCryptoErasePassTimeStamp);
            break;

        case 2:
            print_Count_Statistics("Sanitize Overwrite Count", seagateDeviceStats->sataStatistics.sanitizeOverwriteErasePassCount);
            break;

        case 3:
            print_TimeStamp_Statistics("Sanitize Overwrite Timestamp", seagateDeviceStats->sataStatistics.sanitizeOverwriteErasePassTimeStamp);
            break;

        case 4:
            print_Count_Statistics("Sanitize Block Erase Count", seagateDeviceStats->sataStatistics.sanitizeBlockErasePassCount);
            break;

        case 5:
            print_TimeStamp_Statistics("Sanitize Block Erase Timestamp", seagateDeviceStats->sataStatistics.sanitizeBlockErasePassTimeStamp);
            break;

        case 6:
            print_Count_Statistics("ATA Security Erase Unit Count", seagateDeviceStats->sataStatistics.ataSecurityEraseUnitPassCount);
            break;

        case 7:
            print_TimeStamp_Statistics("ATA Security Erase Unit Timestamp", seagateDeviceStats->sataStatistics.ataSecurityEraseUnitPassTimeStamp);
            break;

        case 8:
            print_Count_Statistics("Erase Security File Failure Count", seagateDeviceStats->sataStatistics.eraseSecurityFileFailureCount);
            break;

        case 9:
            print_TimeStamp_Statistics("Erase Security File Failure Timestamp", seagateDeviceStats->sataStatistics.eraseSecurityFileFailureTimeStamp);
            break;

        case 10:
            print_Count_Statistics("ATA Security Erase Unit Enhanced Count", seagateDeviceStats->sataStatistics.ataSecurityEraseUnitEnhancedPassCount);
            break;

        case 11:
            print_TimeStamp_Statistics("ATA Security Erase Unit Enhanced Timestamp", seagateDeviceStats->sataStatistics.ataSecurityEraseUnitEnhancedPassTimeStamp);
            break;

        case 12:
            print_Count_Statistics("Sanitize Crypto Erase Failure Count", seagateDeviceStats->sataStatistics.sanitizeCryptoEraseFailCount);
            break;

        case 13:
            print_TimeStamp_Statistics("Sanitize Crypto Erase Failure Timestamp", seagateDeviceStats->sataStatistics.sanitizeCryptoEraseFailTimeStamp);
            break;

        case 14:
            print_Count_Statistics("Sanitize Overwrite Failure Count", seagateDeviceStats->sataStatistics.sanitizeOverwriteEraseFailCount);
            break;

        case 15:
            print_TimeStamp_Statistics("Sanitize Overwrite Failure Timestamp", seagateDeviceStats->sataStatistics.sanitizeOverwriteEraseFailTimeStamp);
            break;

        case 16:
            print_Count_Statistics("Sanitize Block Erase Failure Count", seagateDeviceStats->sataStatistics.sanitizeBlockEraseFailCount);
            break;

        case 17:
            print_TimeStamp_Statistics("Sanitize Block Erase Failure Timestamp", seagateDeviceStats->sataStatistics.sanitizeBlockEraseFailTimeStamp);
            break;

        case 18:
            print_Count_Statistics("ATA Security Erase Unit Failure Count", seagateDeviceStats->sataStatistics.ataSecurityEraseUnitFailCount);
            break;

        case 19:
            print_TimeStamp_Statistics("ATA Security Erase Unit Failure Timestamp", seagateDeviceStats->sataStatistics.ataSecurityEraseUnitFailTimeStamp);
            break;

        case 20:
            print_Count_Statistics("ATA Security Erase Unit Enhanced Failure Count", seagateDeviceStats->sataStatistics.ataSecurityEraseUnitEnhancedFailCount);
            break;

        case 21:
            print_TimeStamp_Statistics("ATA Security Erase Unit Enhanced Failure Timestamp", seagateDeviceStats->sataStatistics.ataSecurityEraseUnitEnhancedFailTimeStamp);
            break;

        default:
            break;
        }
    }
    printf("\n\n");

    //latest result for Sanitize Crypto 
    if (seagateDeviceStats->sataStatistics.sanitizeCryptoErasePassCount.isValueValid
        && seagateDeviceStats->sataStatistics.sanitizeCryptoEraseFailCount.isValueValid)
    {
        uint64_t timestampInMinutesForPass = 0;
        uint64_t timestampInMinutesForFail = 0;
        if (seagateDeviceStats->sataStatistics.sanitizeCryptoErasePassTimeStamp.isValueValid)
        {
            timestampInMinutesForPass = seagateDeviceStats->sataStatistics.sanitizeCryptoErasePassTimeStamp.isTimeStampsInMinutes
                ? seagateDeviceStats->sataStatistics.sanitizeCryptoErasePassTimeStamp.statisticsDataValue
                : seagateDeviceStats->sataStatistics.sanitizeCryptoErasePassTimeStamp.statisticsDataValue * UINT64_C(60);
        }
        if (seagateDeviceStats->sataStatistics.sanitizeCryptoEraseFailTimeStamp.isValueValid)
        {
            timestampInMinutesForPass = seagateDeviceStats->sataStatistics.sanitizeCryptoEraseFailTimeStamp.isTimeStampsInMinutes
                ? seagateDeviceStats->sataStatistics.sanitizeCryptoEraseFailTimeStamp.statisticsDataValue
                : seagateDeviceStats->sataStatistics.sanitizeCryptoEraseFailTimeStamp.statisticsDataValue * UINT64_C(60);
        }

        if (timestampInMinutesForPass != 0 && timestampInMinutesForFail != 0)
        {
            if (timestampInMinutesForPass > timestampInMinutesForFail)
                printf("Last Sanitize Crypto Erase Passed.\n");
            else
                printf("Last Sanitize Crypto Erase Failed.\n");
        }
    }
    else if (seagateDeviceStats->sataStatistics.sanitizeBlockErasePassCount.isValueValid
        && seagateDeviceStats->sataStatistics.sanitizeCryptoErasePassTimeStamp.isValueValid)
        printf("Last Sanitize Crypto Erase Passed.\n");
    else if (seagateDeviceStats->sataStatistics.sanitizeCryptoEraseFailCount.isValueValid
        && seagateDeviceStats->sataStatistics.sanitizeCryptoEraseFailTimeStamp.isValueValid)
        printf("Last Sanitize Crypto Erase Failed.\n");

    //latest result for Sanitize Overwrite 
    if (seagateDeviceStats->sataStatistics.sanitizeOverwriteErasePassCount.isValueValid
        && seagateDeviceStats->sataStatistics.sanitizeOverwriteEraseFailCount.isValueValid)
    {
        uint64_t timestampInMinutesForPass = 0;
        uint64_t timestampInMinutesForFail = 0;
        if (seagateDeviceStats->sataStatistics.sanitizeOverwriteErasePassTimeStamp.isValueValid)
        {
            timestampInMinutesForPass = seagateDeviceStats->sataStatistics.sanitizeOverwriteErasePassTimeStamp.isTimeStampsInMinutes
                ? seagateDeviceStats->sataStatistics.sanitizeOverwriteErasePassTimeStamp.statisticsDataValue
                : seagateDeviceStats->sataStatistics.sanitizeOverwriteErasePassTimeStamp.statisticsDataValue * UINT64_C(60);
        }
        if (seagateDeviceStats->sataStatistics.sanitizeOverwriteEraseFailTimeStamp.isValueValid)
        {
            timestampInMinutesForPass = seagateDeviceStats->sataStatistics.sanitizeOverwriteEraseFailTimeStamp.isTimeStampsInMinutes
                ? seagateDeviceStats->sataStatistics.sanitizeOverwriteEraseFailTimeStamp.statisticsDataValue
                : seagateDeviceStats->sataStatistics.sanitizeOverwriteEraseFailTimeStamp.statisticsDataValue * UINT64_C(60);
        }

        if (timestampInMinutesForPass != 0 && timestampInMinutesForFail != 0)
        {
            if (timestampInMinutesForPass > timestampInMinutesForFail)
                printf("Last Sanitize Overwrite Erase Passed.\n");
            else
                printf("Last Sanitize Overwrite Erase Failed.\n");
        }
    }
    else if (seagateDeviceStats->sataStatistics.sanitizeOverwriteErasePassCount.isValueValid
        && seagateDeviceStats->sataStatistics.sanitizeOverwriteErasePassTimeStamp.isValueValid)
        printf("Last Sanitize Overwrite Erase Passed.\n");
    else if (seagateDeviceStats->sataStatistics.sanitizeOverwriteEraseFailCount.isValueValid
        && seagateDeviceStats->sataStatistics.sanitizeOverwriteEraseFailTimeStamp.isValueValid)
        printf("Last Sanitize Overwrite Erase Failed.\n");

    //latest result for Sanitize Block 
    if (seagateDeviceStats->sataStatistics.sanitizeBlockErasePassCount.isValueValid
        && seagateDeviceStats->sataStatistics.sanitizeBlockEraseFailCount.isValueValid)
    {
        uint64_t timestampInMinutesForPass = 0;
        uint64_t timestampInMinutesForFail = 0;
        if (seagateDeviceStats->sataStatistics.sanitizeBlockErasePassTimeStamp.isValueValid)
        {
            timestampInMinutesForPass = seagateDeviceStats->sataStatistics.sanitizeBlockErasePassTimeStamp.isTimeStampsInMinutes
                ? seagateDeviceStats->sataStatistics.sanitizeBlockErasePassTimeStamp.statisticsDataValue
                : seagateDeviceStats->sataStatistics.sanitizeBlockErasePassTimeStamp.statisticsDataValue * UINT64_C(60);
        }
        if (seagateDeviceStats->sataStatistics.sanitizeBlockEraseFailTimeStamp.isValueValid)
        {
            timestampInMinutesForPass = seagateDeviceStats->sataStatistics.sanitizeBlockEraseFailTimeStamp.isTimeStampsInMinutes
                ? seagateDeviceStats->sataStatistics.sanitizeBlockEraseFailTimeStamp.statisticsDataValue
                : seagateDeviceStats->sataStatistics.sanitizeBlockEraseFailTimeStamp.statisticsDataValue * UINT64_C(60);
        }

        if (timestampInMinutesForPass != 0 && timestampInMinutesForFail != 0)
        {
            if (timestampInMinutesForPass > timestampInMinutesForFail)
                printf("Last Sanitize Block Erase Passed.\n");
            else
                printf("Last Sanitize Block Erase Failed.\n");
        }
    }
    else if (seagateDeviceStats->sataStatistics.sanitizeBlockErasePassCount.isValueValid
        && seagateDeviceStats->sataStatistics.sanitizeBlockErasePassTimeStamp.isValueValid)
        printf("Last Sanitize Block Erase Passed.\n");
    else if (seagateDeviceStats->sataStatistics.sanitizeBlockEraseFailCount.isValueValid
        && seagateDeviceStats->sataStatistics.sanitizeBlockEraseFailTimeStamp.isValueValid)
        printf("Last Sanitize Block Erase Failed.\n");

    //latest result for Ata Security Erase Unit 
    if (seagateDeviceStats->sataStatistics.ataSecurityEraseUnitPassCount.isValueValid
        && seagateDeviceStats->sataStatistics.ataSecurityEraseUnitFailCount.isValueValid)
    {
        uint64_t timestampInMinutesForPass = 0;
        uint64_t timestampInMinutesForFail = 0;
        if (seagateDeviceStats->sataStatistics.ataSecurityEraseUnitPassTimeStamp.isValueValid)
        {
            timestampInMinutesForPass = seagateDeviceStats->sataStatistics.ataSecurityEraseUnitPassTimeStamp.isTimeStampsInMinutes
                ? seagateDeviceStats->sataStatistics.ataSecurityEraseUnitPassTimeStamp.statisticsDataValue
                : seagateDeviceStats->sataStatistics.ataSecurityEraseUnitPassTimeStamp.statisticsDataValue * UINT64_C(60);
        }
        if (seagateDeviceStats->sataStatistics.ataSecurityEraseUnitFailTimeStamp.isValueValid)
        {
            timestampInMinutesForPass = seagateDeviceStats->sataStatistics.ataSecurityEraseUnitFailTimeStamp.isTimeStampsInMinutes
                ? seagateDeviceStats->sataStatistics.ataSecurityEraseUnitFailTimeStamp.statisticsDataValue
                : seagateDeviceStats->sataStatistics.ataSecurityEraseUnitFailTimeStamp.statisticsDataValue * UINT64_C(60);
        }

        if (timestampInMinutesForPass != 0 && timestampInMinutesForFail != 0)
        {
            if (timestampInMinutesForPass > timestampInMinutesForFail)
                printf("Last ATA Security Erase Unit Passed.\n");
            else
                printf("Last ATA Security Erase Unit Failed.\n");
        }
    }
    else if (seagateDeviceStats->sataStatistics.ataSecurityEraseUnitPassCount.isValueValid
        && seagateDeviceStats->sataStatistics.ataSecurityEraseUnitPassTimeStamp.isValueValid)
        printf("Last ATA Security Erase Unit Passed.\n");
    else if (seagateDeviceStats->sataStatistics.ataSecurityEraseUnitFailCount.isValueValid
        && seagateDeviceStats->sataStatistics.ataSecurityEraseUnitFailTimeStamp.isValueValid)
        printf("Last ATA Security Erase Unit Failed.\n");

    //latest result for Ata Security Erase Unit Enhanced 
    if (seagateDeviceStats->sataStatistics.ataSecurityEraseUnitEnhancedPassCount.isValueValid
        && seagateDeviceStats->sataStatistics.ataSecurityEraseUnitEnhancedFailCount.isValueValid)
    {
        uint64_t timestampInMinutesForPass = 0;
        uint64_t timestampInMinutesForFail = 0;
        if (seagateDeviceStats->sataStatistics.ataSecurityEraseUnitEnhancedPassTimeStamp.isValueValid)
        {
            timestampInMinutesForPass = seagateDeviceStats->sataStatistics.ataSecurityEraseUnitEnhancedPassTimeStamp.isTimeStampsInMinutes
                ? seagateDeviceStats->sataStatistics.ataSecurityEraseUnitEnhancedPassTimeStamp.statisticsDataValue
                : seagateDeviceStats->sataStatistics.ataSecurityEraseUnitEnhancedPassTimeStamp.statisticsDataValue * UINT64_C(60);
        }
        if (seagateDeviceStats->sataStatistics.ataSecurityEraseUnitEnhancedFailTimeStamp.isValueValid)
        {
            timestampInMinutesForPass = seagateDeviceStats->sataStatistics.ataSecurityEraseUnitEnhancedFailTimeStamp.isTimeStampsInMinutes
                ? seagateDeviceStats->sataStatistics.ataSecurityEraseUnitEnhancedFailTimeStamp.statisticsDataValue
                : seagateDeviceStats->sataStatistics.ataSecurityEraseUnitEnhancedFailTimeStamp.statisticsDataValue * UINT64_C(60);
        }

        if (timestampInMinutesForPass != 0 && timestampInMinutesForFail != 0)
        {
            if (timestampInMinutesForPass > timestampInMinutesForFail)
                printf("Last ATA Security Erase Unit Enhanced Passed.\n");
            else
                printf("Last ATA Security Erase Unit Enhanced Failed.\n");
        }
    }
    else if (seagateDeviceStats->sataStatistics.ataSecurityEraseUnitEnhancedPassCount.isValueValid
        && seagateDeviceStats->sataStatistics.ataSecurityEraseUnitEnhancedPassTimeStamp.isValueValid)
        printf("Last ATA Security Erase Unit Enhanced Passed.\n");
    else if (seagateDeviceStats->sataStatistics.ataSecurityEraseUnitEnhancedFailCount.isValueValid
        && seagateDeviceStats->sataStatistics.ataSecurityEraseUnitEnhancedFailTimeStamp.isValueValid)
        printf("Last ATA Security Erase Unit Enhanced Failed.\n");

    return;
}

static void print_Seagate_SCSI_DeviceStatistics(ptrSeagateDeviceStatistics seagateDeviceStats)
{
    if (!seagateDeviceStats)
    {
        return;
    }

    printf("\n\n===Seagate Device Statistics===\n");

    printf(" %-60s %-16s\n", "Statistic Name:", "Value:");
    print_Count_Statistics("Sanitize Crypo Erase Count", seagateDeviceStats->sasStatistics.sanitizeCryptoEraseCount);
    print_TimeStamp_Statistics("Sanitize Crypo Erase Requested Time", seagateDeviceStats->sasStatistics.sanitizeCryptoEraseTimeStamp);

    print_Count_Statistics("Sanitize Overwrite Erase Count", seagateDeviceStats->sasStatistics.sanitizeOverwriteEraseCount);
    print_TimeStamp_Statistics("Sanitize Overwrite Erase Requested Time", seagateDeviceStats->sasStatistics.sanitizeOverwriteEraseTimeStamp);

    print_Count_Statistics("Sanitize Block Erase Count", seagateDeviceStats->sasStatistics.sanitizeBlockEraseCount);
    print_TimeStamp_Statistics("Sanitize Block Erase Requested Time", seagateDeviceStats->sasStatistics.sanitizeBlockEraseTimeStamp);

    print_Count_Statistics("Erase Security File Failures Count", seagateDeviceStats->sasStatistics.eraseSecurityFileFailureCount);
    print_TimeStamp_Statistics("Erase Security File Failures Requested Time", seagateDeviceStats->sasStatistics.eraseSecurityFileFailureTimeStamp);
}

void print_Seagate_DeviceStatistics(tDevice *device, ptrSeagateDeviceStatistics seagateDeviceStats)
{
    if (device->drive_info.drive_type == ATA_DRIVE)
    {
        print_Seagate_ATA_DeviceStatistics(seagateDeviceStats);
    }
    else if (device->drive_info.drive_type == SCSI_DRIVE)
    {
        print_Seagate_SCSI_DeviceStatistics(seagateDeviceStats);
    }
}

eReturnValues get_Seagate_SCSI_Firmware_Numbers(tDevice* device, ptrSeagateSCSIFWNumbers fwNumbers)
{
    eReturnValues ret = NOT_SUPPORTED;
    if (!fwNumbers)
    {
        return BAD_PARAMETER;
    }
    if (device->drive_info.drive_type == SCSI_DRIVE && SEAGATE == is_Seagate_Family(device))
    {
        DECLARE_ZERO_INIT_ARRAY(uint8_t, firmwareNumbersPage, 60);
        if (SUCCESS == scsi_Inquiry(device, firmwareNumbersPage, 60, 0xC0, true, false))
        {
            ret = SUCCESS;
            memcpy(fwNumbers->scsiFirmwareReleaseNumber, &firmwareNumbersPage[4], FIRMWARE_RELEASE_NUM_LEN);
            memcpy(fwNumbers->servoFirmwareReleaseNumber, &firmwareNumbersPage[12], SERVO_FIRMWARE_RELEASE_NUM_LEN);
            memcpy(fwNumbers->sapBlockPointNumbers, &firmwareNumbersPage[20], SAP_BP_NUM_LEN);
            memcpy(fwNumbers->servoFirmmwareReleaseDate, &firmwareNumbersPage[28], SERVO_FW_RELEASE_DATE_LEN);
            memcpy(fwNumbers->servoRomReleaseDate, &firmwareNumbersPage[32], SERVO_ROM_RELEASE_DATE_LEN);
            memcpy(fwNumbers->sapFirmwareReleaseNumber, &firmwareNumbersPage[36], SAP_FW_RELEASE_NUM_LEN);
            memcpy(fwNumbers->sapFirmwareReleaseDate, &firmwareNumbersPage[44], SAP_FW_RELEASE_DATE_LEN);
            memcpy(fwNumbers->sapFirmwareReleaseYear, &firmwareNumbersPage[48], SAP_FW_RELEASE_YEAR_LEN);
            memcpy(fwNumbers->sapManufacturingKey, &firmwareNumbersPage[52], SAP_MANUFACTURING_KEY_LEN);
            memcpy(fwNumbers->servoFirmwareProductFamilyAndProductFamilyMemberIDs, &firmwareNumbersPage[56], SERVO_PRODUCT_FAMILY_LEN);
        }
    }
    return ret;
}
