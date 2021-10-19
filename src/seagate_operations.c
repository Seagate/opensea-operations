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
// \file seagate_operations.c
// \brief This file defines the functions for Seagate drive specific operations that are customer safe

#include "seagate_operations.h"
#include "logs.h"
#include "smart.h"
#include "sector_repair.h"
#include "dst.h"
#include "sanitize.h"
#include "format.h"
#include "vendor/seagate/seagate_ata_types.h"
#include "vendor/seagate/seagate_scsi_types.h"
#include <float.h> //for DBL_MAX
#include "platform_helper.h"
#include "depopulate.h"

int seagate_ata_SCT_SATA_phy_speed(tDevice *device, uint8_t speedGen)
{
    int ret = UNKNOWN;
    uint8_t *sctSATAPhySpeed = C_CAST(uint8_t*, calloc_aligned(LEGACY_DRIVE_SEC_SIZE, sizeof(uint8_t), device->os_info.minimumAlignment));
    if (sctSATAPhySpeed == NULL)
    {
        perror("Calloc Failure!\n");
        return MEMORY_FAILURE;
    }
    //speedGen = 1 means generation 1 (1.5Gb/s), 2 =  2nd Generation (3.0Gb/s), 3 = 3rd Generation (6.0Gb/s)
    if (speedGen > 3)
    {
        safe_Free_aligned(sctSATAPhySpeed)
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

    safe_Free_aligned(sctSATAPhySpeed)
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
int scsi_Set_Phy_Speed(tDevice *device, uint8_t phySpeedGen, bool allPhys, uint8_t phyNumber)
{
    int ret = SUCCESS;
    if (phySpeedGen > 5)
    {
        return NOT_SUPPORTED;
    }
    uint16_t phyControlLength = 104 + MODE_PARAMETER_HEADER_10_LEN;//size of 104 comes from 8 byte page header + (2 * 48bytes) for 2 phy descriptors + then add 8 bytes for mode parameter header. This is assuming drives only have 2...which is true right now, but the code will detect when it needs to reallocate and read more from the drive.
    uint8_t *sasPhyControl = C_CAST(uint8_t*, calloc_aligned(phyControlLength, sizeof(uint8_t), device->os_info.minimumAlignment));
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
                phyControlLength = pageLength + MODE_PARAMETER_HEADER_10_LEN + blockDescriptorLength;
                uint8_t *temp = realloc_aligned(sasPhyControl, 0, phyControlLength * sizeof(uint8_t), device->os_info.minimumAlignment);
                if (!temp)
                {
                    return MEMORY_FAILURE;
                }
                sasPhyControl = temp;
                if (SUCCESS != scsi_Mode_Sense_10(device, MP_PROTOCOL_SPECIFIC_PORT, phyControlLength, 0x01, true, false, MPC_CURRENT_VALUES, sasPhyControl))
                {
                    safe_Free_aligned(sasPhyControl)
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
                            uint8_t matchedRate = (hardwareMaximumLinkRate << 4) | hardwareMaximumLinkRate;
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
    safe_Free_aligned(sasPhyControl)
    return ret;
}

int set_phy_speed(tDevice *device, uint8_t phySpeedGen, bool allPhys, uint8_t phyIdentifier)
{
    int ret = UNKNOWN;
    if (device->drive_info.drive_type == ATA_DRIVE)
    {
        if (is_Seagate_Family(device) == SEAGATE)
        {
            if (device->drive_info.IdentifyData.ata.Word206 & BIT7 && !is_SSD(device))
            {
                if (phySpeedGen > 3)
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
        if (device->drive_info.IdentifyData.ata.Word206 & BIT4)
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
        if ((device->drive_info.IdentifyData.ata.Word206 & BIT4) && sctCommandSupported)
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
            ata_Identify(device, (uint8_t*)&device->drive_info.IdentifyData.ata.Word000, LEGACY_DRIVE_SEC_SIZE);
            if (device->drive_info.IdentifyData.ata.Word155 & BIT1)
            {
                lowPowerSpinUpEnabled = 1;
            }
        }
    }
    return lowPowerSpinUpEnabled;
}

int seagate_SCT_Low_Current_Spinup(tDevice *device, eSeagateLCSpinLevel spinupLevel)
{
    int ret = NOT_SUPPORTED;
    if (device->drive_info.IdentifyData.ata.Word206 & BIT4)
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

int set_Low_Current_Spin_Up(tDevice *device, bool useSCTCommand, uint8_t state)
{
    int ret = NOT_SUPPORTED;
    if (device->drive_info.drive_type == ATA_DRIVE && is_Seagate_Family(device) == SEAGATE)
    {
        if (state == 0)
        {
            return NOT_SUPPORTED;
        }
        if (useSCTCommand)
        {
            if (state == 0)
            {
                state = SEAGATE_LOW_CURRENT_SPINUP_STATE_DEFAULT;
            }
            ret = seagate_SCT_Low_Current_Spinup(device, state);
        }
        else
        {
            //use set features command for 2.5" products
            uint8_t secCnt = SEAGATE_SF_LCS_ENABLE;
            if (state == 2)//0 means disable, 2 is here for compatibility with SCT command inputs
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

int set_SSC_Feature_SATA(tDevice *device, eSSCFeatureState mode)
{
    int ret = NOT_SUPPORTED;
    if (device->drive_info.drive_type == ATA_DRIVE)
    {
        if (is_Seagate_Family(device) == SEAGATE)
        {
            if (device->drive_info.IdentifyData.ata.Word206 & BIT4)
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

int get_SSC_Feature_SATA(tDevice *device, eSSCFeatureState *mode)
{
    int ret = NOT_SUPPORTED;
    if (device->drive_info.drive_type == ATA_DRIVE)
    {
        if (is_Seagate_Family(device) == SEAGATE)
        {
            if (device->drive_info.IdentifyData.ata.Word206 & BIT4)
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

int seagate_SAS_Get_JIT_Modes(tDevice *device, ptrSeagateJITModes jitModes)
{
    int ret = NOT_SUPPORTED;
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
            uint8_t seagateUnitAttentionParameters[12 + MODE_PARAMETER_HEADER_10_LEN] = { 0 };
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

int seagate_Get_JIT_Modes(tDevice *device, ptrSeagateJITModes jitModes)
{
    int ret = NOT_SUPPORTED;
    if (device->drive_info.drive_type == SCSI_DRIVE)
    {
        ret = seagate_SAS_Get_JIT_Modes(device, jitModes);
    }
    return ret;
}

int seagate_SAS_Set_JIT_Modes(tDevice *device, bool disableVjit, uint8_t jitMode, bool revertToDefaults, bool nonvolatile)
{
    int ret = NOT_SUPPORTED;
    eSeagateFamily family = is_Seagate_Family(device);
    if (family == SEAGATE || family == SEAGATE_VENDOR_A)
    {
        if (!is_SSD(device))
        {
            //HDD, so we can do this.
            uint8_t seagateUnitAttentionParameters[12 + MODE_PARAMETER_HEADER_10_LEN] = { 0 };
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
                    M_FALLTHROUGH
                case 1:
                    seagateUnitAttentionParameters[headerLength + 4] |= BIT1;
                    M_FALLTHROUGH
                case 2:
                    seagateUnitAttentionParameters[headerLength + 4] |= BIT2;
                    M_FALLTHROUGH
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

int seagate_Set_JIT_Modes(tDevice *device, bool disableVjit, uint8_t jitMode, bool revertToDefaults, bool nonvolatile)
{
    int ret = NOT_SUPPORTED;
    if (device->drive_info.drive_type == SCSI_DRIVE)
    {
        ret = seagate_SAS_Set_JIT_Modes(device, disableVjit, jitMode, revertToDefaults, nonvolatile);
    }
    return ret;
}

int seagate_Get_Power_Balance(tDevice *device, bool *supported, bool *enabled)
{
    int ret = NOT_SUPPORTED;
    if (device->drive_info.drive_type == ATA_DRIVE)
    {
        if (is_Seagate_Family(device) == SEAGATE)
        {
            ret = SUCCESS;
            if (supported)
            {
                //BIT8 for older products with this feature. EX: ST10000NM*
                //Bit10 for newwer products with this feature. EX: ST12000NM*
                if (device->drive_info.IdentifyData.ata.Word149 & BIT8 || device->drive_info.IdentifyData.ata.Word149 & BIT10)
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
                if (device->drive_info.IdentifyData.ata.Word149 & BIT9 || device->drive_info.IdentifyData.ata.Word149 & BIT11)
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
    else if(device->drive_info.drive_type == SCSI_DRIVE)
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
            uint8_t *pcModePage = C_CAST(uint8_t*, calloc_aligned(MODE_PARAMETER_HEADER_10_LEN + 16, sizeof(uint8_t), device->os_info.minimumAlignment));
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
                if (pcModePage[MODE_PARAMETER_HEADER_10_LEN + 7]  == 0xFF && (M_GETBITRANGE(pcModePage[MODE_PARAMETER_HEADER_10_LEN + 6],2, 0) == 0))
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
                else if(M_GETBITRANGE(pcModePage[MODE_PARAMETER_HEADER_10_LEN + 6], 2, 0) == 3 && pcModePage[MODE_PARAMETER_HEADER_10_LEN + 7] == 0)
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
                        else if(enabled)
                        {
                            //I guess say it's off???
                            *enabled = false;
                        }
                        ret = SUCCESS;
                    }
                }
            }
            safe_Free_aligned(pcModePage)
        }
    }
    return ret;
}

int seagate_Set_Power_Balance(tDevice *device, bool enable)
{
    int ret = NOT_SUPPORTED;
    if (device->drive_info.drive_type == ATA_DRIVE)
    {
        if (enable)
        {
            ret = ata_Set_Features(device, SEAGATE_FEATURE_POWER_BALANCE, 0, POWER_BALANCE_LBA_LOW_ENABLE, 0, 0);
        }
        else
        {
            ret = ata_Set_Features(device, SEAGATE_FEATURE_POWER_BALANCE, 0, POWER_BALANCE_LBA_LOW_DISABLE, 0, 0);
        }
    }
    else if (device->drive_info.drive_type == SCSI_DRIVE)
    {
        bool oldMethod = false;
        uint8_t *pcModePage = C_CAST(uint8_t*, calloc_aligned(16 + MODE_PARAMETER_HEADER_10_LEN, sizeof(uint8_t), device->os_info.minimumAlignment));
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
                if (enable)
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
                if (enable)
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
        safe_Free_aligned(pcModePage)
    }
    return ret;
}

int get_IDD_Support(tDevice *device, ptrIDDSupportedFeatures iddSupport)
{
    int ret = NOT_SUPPORTED;
    //IDD is only on ATA drives
    if (device->drive_info.drive_type == ATA_DRIVE && is_SMART_Enabled(device))
    {
        //IDD is seagate specific
        if (is_Seagate_Family(device) == SEAGATE)
        {
            uint8_t *smartData = C_CAST(uint8_t*, calloc_aligned(LEGACY_DRIVE_SEC_SIZE, sizeof(uint8_t), device->os_info.minimumAlignment));
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
            safe_Free_aligned(smartData)
        }
    }
    else if (device->drive_info.drive_type == SCSI_DRIVE)
    {
        //IDD is seagate specific
        if (is_Seagate_Family(device) == SEAGATE)
        {
            uint8_t *iddDiagPage = C_CAST(uint8_t*, calloc_aligned(12, sizeof(uint8_t), device->os_info.minimumAlignment));
            if (iddDiagPage)
            {
                if (SUCCESS == scsi_Receive_Diagnostic_Results(device, true, SEAGATE_DIAG_IN_DRIVE_DIAGNOSTICS, 12, iddDiagPage, 15))
                {
                    ret = SUCCESS;
                    iddSupport->iddShort = true;//short
                    iddSupport->iddLong = true;//long
                }
            }
            safe_Free_aligned(iddDiagPage)
        }
    }
    return ret;
}

#define IDD_READY_TIME_SECONDS 120

int get_Approximate_IDD_Time(tDevice *device, eIDDTests iddTest, uint64_t *timeInSeconds)
{
    int ret = NOT_SUPPORTED;
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
                *timeInSeconds = IDD_READY_TIME_SECONDS;
                break;
            case SEAGATE_IDD_LONG:
                get_SMART_Attributes(device, &smartData);
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
                break;
            case SEAGATE_IDD_LONG:
                *timeInSeconds = UINT64_MAX;
                break;
            default:
                break;
            }
        }
    }
    return ret;
}

int get_IDD_Status(tDevice *device, uint8_t *status)
{
    int ret = NOT_SUPPORTED;
    if (device->drive_info.drive_type == ATA_DRIVE)
    {
        uint32_t percentComplete = 0;
        return ata_Get_DST_Progress(device, &percentComplete, status);
    }
    else if (device->drive_info.drive_type == SCSI_DRIVE)
    {
        //read diagnostic page
        uint8_t *iddDiagPage = C_CAST(uint8_t*, calloc_aligned(12, sizeof(uint8_t), device->os_info.minimumAlignment));
        if (iddDiagPage)
        {
            //do not use the return value from this since IDD can return a few different sense codes with unit attention, that we may otherwise call an error
            ret = scsi_Receive_Diagnostic_Results(device, true, SEAGATE_DIAG_IN_DRIVE_DIAGNOSTICS, 12, iddDiagPage, 15);
            if (ret != SUCCESS)
            {
                uint8_t senseKey = 0, asc = 0, ascq = 0, fru = 0;
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
        safe_Free_aligned(iddDiagPage)
    }
    return ret;
}

//NOTE: If IDD is ever supported on NVMe, this may need updates.
//TODO: It may be possible to read the DST log to return slightly better messages about IDD
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


static int start_IDD_Operation(tDevice *device, eIDDTests iddOperation, bool captiveForeground)
{
    int ret = NOT_SUPPORTED;
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
        uint8_t *iddDiagPage = C_CAST(uint8_t*, calloc_aligned(12, sizeof(uint8_t), device->os_info.minimumAlignment));
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
                safe_Free(iddDiagPage)
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
            safe_Free_aligned(iddDiagPage)
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
            uint8_t senseKey = 0, asc = 0, ascq = 0, fru = 0;
            get_Sense_Key_ASC_ASCQ_FRU(device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, &senseKey, &asc, &ascq, &fru);
            if (senseKey == SENSE_KEY_ILLEGAL_REQUEST)
            {
                //TODO: Do we need to check for asc = 26h, ascq = 0h? For now this should be ok
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
    uint32_t commandTimeSeconds = C_CAST(uint32_t, device->drive_info.lastCommandTimeNanoSeconds / 1e9);
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
int run_IDD(tDevice *device, eIDDTests IDDtest, bool pollForProgress, bool captive)
{
    int result = UNKNOWN;
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
                //Moving this code to start_IDD_Operation function, as we want to lock the drive for 2 mins.
                //This is to make sure that drive is not getting any command, even outside of tool for 2 mins.
                /*if (result != SUCCESS)
                {
                    if (device->drive_info.drive_type == SCSI_DRIVE)
                    {
                        //check the sense data. The problem may be that captive/foreground mode isn't supported for the long test
                        uint8_t senseKey = 0, asc = 0, ascq = 0, fru = 0;
                        get_Sense_Key_ASC_ASCQ_FRU(device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, &senseKey, &asc, &ascq, &fru);
                        if (senseKey == SENSE_KEY_ILLEGAL_REQUEST)
                        {
                            //TODO: Do we need to check for asc = 26h, ascq = 0h? For now this should be ok
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
                uint32_t commandTimeSeconds = C_CAST(uint32_t, device->drive_info.lastCommandTimeNanoSeconds / 1e9);
                if (commandTimeSeconds < IDD_READY_TIME_SECONDS)
                {
                    //we need to make sure we waited at least 2 minutes since command was sent to the drive before pinging it with another command.
                    //It needs time to spin back up and be ready to accept commands again.
                    //This is being done in both captive/foreground and offline/background modes due to differences between some drive firmwares.
                    delay_Seconds(IDD_READY_TIME_SECONDS - commandTimeSeconds);
                }*/
                if (SUCCESS == result && captiveForeground)
                {
                    int ret = get_IDD_Status(device, &status);
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
                    int ret = SUCCESS;//for use in the loop below...assume that we are successful
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

//TODO: These are in the spec, but need to be verified before they are used.
#define ATA_POWER_TELEMETRY_LOG_SIZE_BYTES UINT16_C(8192)
#define SCSI_POWER_TELEMETRY_LOG_SIZE_BYTES UINT16_C(6240)

//This can be used to save this log to a binary file to be read later.
int pull_Power_Telemetry_Log(tDevice *device, const char * const filePath, uint32_t transferSizeBytes)
{
    int ret = NOT_SUPPORTED;
    if (device->drive_info.drive_type == ATA_DRIVE)
    {
        ret = get_ATA_Log(device, SEAGATE_ATA_LOG_POWER_TELEMETRY, "PWRTEL", "pwr", true, false, false, NULL, 0, filePath, transferSizeBytes, 0);
    }
    else if (device->drive_info.drive_type == SCSI_DRIVE)
    {
        ret = get_SCSI_Error_History(device, SEAGATE_ERR_HIST_POWER_TELEMETRY, "PWRTEL", false, is_SCSI_Read_Buffer_16_Supported(device), "pwr", false, NULL, 0, filePath, transferSizeBytes, NULL);
    }
    return ret;
}

int request_Power_Measurement(tDevice *device, uint16_t timeMeasurementSeconds, ePowerTelemetryMeasurementOptions measurementOption)
{
    int ret = NOT_SUPPORTED;
    if (device->drive_info.drive_type == ATA_DRIVE)
    {
        uint8_t pwrTelLogPg[512] = { 0 };
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
        uint8_t pwrTelDiagPg[16] = { 0 };
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

int get_Power_Telemetry_Data(tDevice *device, ptrSeagatePwrTelemetry pwrTelData)
{
    int ret = NOT_SUPPORTED;
    if (!pwrTelData)
    {
        return BAD_PARAMETER;
    }
    uint32_t powerTelemetryLogSize = 0;
    uint8_t *powerTelemetryLog = NULL;
    //first, determine how much data there is, allocate memory, then read it all into that buffer
    if (device->drive_info.drive_type == ATA_DRIVE)
    {
        ret = get_ATA_Log_Size(device, SEAGATE_ATA_LOG_POWER_TELEMETRY, &powerTelemetryLogSize, true, false);
        if (ret == SUCCESS && powerTelemetryLogSize > 0)
        {
            powerTelemetryLog = C_CAST(uint8_t *, calloc_aligned(powerTelemetryLogSize, sizeof(uint8_t), device->os_info.minimumAlignment));
            if (powerTelemetryLog)
            {
                ret = get_ATA_Log(device, SEAGATE_ATA_LOG_POWER_TELEMETRY, NULL, NULL, true, false, true, powerTelemetryLog, powerTelemetryLogSize, NULL, 0, 0);
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
            powerTelemetryLog = C_CAST(uint8_t *, calloc_aligned(powerTelemetryLogSize, sizeof(uint8_t), device->os_info.minimumAlignment));
            if (powerTelemetryLog)
            {
                ret = get_SCSI_Error_History(device, SEAGATE_ERR_HIST_POWER_TELEMETRY, NULL, false, rb16, NULL, true, powerTelemetryLog, powerTelemetryLogSize, NULL, 0, NULL);
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
        pwrTelData->driveTimeStampForHostRequestedMeasurement = M_BytesTo8ByteValue(0,0, powerTelemetryLog[15], powerTelemetryLog[14], powerTelemetryLog[13], powerTelemetryLog[12], powerTelemetryLog[11], powerTelemetryLog[10]);
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
    safe_Free_aligned(powerTelemetryLog)
    return ret;
}

void show_Power_Telemetry_Data(ptrSeagatePwrTelemetry pwrTelData)
{
    if (pwrTelData)
    {
        //doubles for end statistics of measurement
        double sum5v = 0, sum12v = 0, min5v = DBL_MAX, max5v = DBL_MIN, min12v = DBL_MAX, max12v = DBL_MIN;
        double stepTime = pwrTelData->measurementWindowTimeMilliseconds; //TODO: Convert from milliseconds to something else???

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

        //TODO: Host requested time and log retrieval time??? Is this necessary?

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
                measurementTime += pwrTelData->driveTimeStampWhenTheLogWasRetrieved;
            }
            else
            {
                measurementTime += pwrTelData->driveTimeStampForHostRequestedMeasurement;
            }
            if (pwrTelData->measurement[measurementNumber].fiveVoltMilliWatts == 0 && pwrTelData->measurement[measurementNumber].twelveVoltMilliWatts == 0)
            {
                break;
            }
            //TODO: IF format is %v only or 12V only, handle showing N/A for some output.
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
                    uint8_t smartData[LEGACY_DRIVE_SEC_SIZE] = { 0 };
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
                bool stdDepopSupported = is_Depopulation_Feature_Supported(device, NULL);
                //bool stdRepopSupported = is_Repopulate_Feature_Supported(device, NULL);
                //TODO: Should checking for set sector configuration bit be here??? it's not clear if drives with this, but not depop will work or not at this time.
                if (stdDepopSupported /*&& !stdRepopSupported*/)
                {
                    supported = true;
                }
            }
        }
    }
    return supported;
}

int seagate_Quick_Format(tDevice *device)
{
    int ret = NOT_SUPPORTED;
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
        ret = ata_SMART_Command(device, ATA_SMART_EXEC_OFFLINE_IMM, 0xD3, NULL, 0, timeout, false, 0);
        os_Unlock_Device(device);
    }
    return ret;
}
