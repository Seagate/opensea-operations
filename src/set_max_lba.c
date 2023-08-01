//
// Do NOT modify or remove this copyright and license
//
// Copyright (c) 2012-2023 Seagate Technology LLC and/or its Affiliates, All Rights Reserved
//
// This software is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
//
// ******************************************************************************************
// 
// \file set_max_lba.c
// \brief This file defines the functions for setting the maxLBA on a device

#include "operations_Common.h"
#include "set_max_lba.h"
#include "scsi_helper_func.h"
#include "ata_device_config_overlay.h"

int ata_Get_Native_Max_LBA(tDevice *device, uint64_t *nativeMaxLBA)
{
    int ret = SUCCESS;
    if (device->drive_info.IdentifyData.ata.Word119 & BIT8) //accessible max address feature set
    {
        ret = ata_Get_Native_Max_Address_Ext(device, nativeMaxLBA);
    }
    else if (device->drive_info.IdentifyData.ata.Word082 & BIT10) //HPA feature set
    {
        ret = ata_Read_Native_Max_Address(device, nativeMaxLBA, device->drive_info.ata_Options.fourtyEightBitAddressFeatureSetSupported);
    }
    else //no other feature sets to check. DCO is handled in a different file since it affects more than just maxLBA
    {
        *nativeMaxLBA = UINT64_MAX;//invalid maxlba
        ret = NOT_SUPPORTED;
    }
    return ret;
}

int get_Native_Max_LBA(tDevice *device, uint64_t *nativeMaxLBA)
{
    int ret = UNKNOWN;
    *nativeMaxLBA = UINT64_MAX;//this is invalid, but useful for scsi since reseting to native max means using this value (see the reset code for scsi_Set_Max_LBA)
    if (device->drive_info.drive_type == ATA_DRIVE)
    {
        ret = ata_Get_Native_Max_LBA(device, nativeMaxLBA);
    }
    else
    {
        ret = NOT_SUPPORTED;
    }
    return ret;
}

int scsi_Set_Max_LBA(tDevice *device, uint64_t newMaxLBA, bool reset)
{
    int ret = UNKNOWN;
    uint8_t *scsiDataBuffer = C_CAST(uint8_t*, calloc_aligned(0x18, sizeof(uint8_t), device->os_info.minimumAlignment));//this should be big enough to get back the block descriptor we care about
    if (scsiDataBuffer == NULL)
    {
        perror("calloc failure");
        return MEMORY_FAILURE;
    }
    //always do a mode sense command to get back a block descriptor. Always request default values because if we are doing a reset, we just send it right back, otherwise we will overwrite the returned data ourselves
    if (SUCCESS == scsi_Mode_Sense_10(device, 0, 0x18, 0, false, true, MPC_DEFAULT_VALUES, scsiDataBuffer))
    {
        newMaxLBA += 1;//Need to add 1 for SCSI so that this will match the -i report. If this is not done, then  we end up with 1 less than the value provided.
        scsiDataBuffer[0] = 0;
        scsiDataBuffer[1] = 0;//clear out the mode datalen
        scsiDataBuffer[3] = 0;//clear the device specific parameter
        scsiDataBuffer[4] |= BIT0;//set the LLBAA bit
        scsiDataBuffer[7] = 0x10;
        //now we have a block descriptor, so lets do what we need to do with it
        if (reset == false)
        {
            //set the input LBA starting at the end of the header
            scsiDataBuffer[MODE_PARAMETER_HEADER_10_LEN] = M_Byte7(newMaxLBA);
            scsiDataBuffer[MODE_PARAMETER_HEADER_10_LEN + 1] = M_Byte6(newMaxLBA);
            scsiDataBuffer[MODE_PARAMETER_HEADER_10_LEN + 2] = M_Byte5(newMaxLBA);
            scsiDataBuffer[MODE_PARAMETER_HEADER_10_LEN + 3] = M_Byte4(newMaxLBA);
            scsiDataBuffer[MODE_PARAMETER_HEADER_10_LEN + 4] = M_Byte3(newMaxLBA);
            scsiDataBuffer[MODE_PARAMETER_HEADER_10_LEN + 5] = M_Byte2(newMaxLBA);
            scsiDataBuffer[MODE_PARAMETER_HEADER_10_LEN + 6] = M_Byte1(newMaxLBA);
            scsiDataBuffer[MODE_PARAMETER_HEADER_10_LEN + 7] = M_Byte0(newMaxLBA);
        }
        else
        {
            scsiDataBuffer[MODE_PARAMETER_HEADER_10_LEN] = 0xFF;
            scsiDataBuffer[MODE_PARAMETER_HEADER_10_LEN + 1] = 0xFF;
            scsiDataBuffer[MODE_PARAMETER_HEADER_10_LEN + 2] = 0xFF;
            scsiDataBuffer[MODE_PARAMETER_HEADER_10_LEN + 3] = 0xFF;
            scsiDataBuffer[MODE_PARAMETER_HEADER_10_LEN + 4] = 0xFF;
            scsiDataBuffer[MODE_PARAMETER_HEADER_10_LEN + 5] = 0xFF;
            scsiDataBuffer[MODE_PARAMETER_HEADER_10_LEN + 6] = 0xFF;
            scsiDataBuffer[MODE_PARAMETER_HEADER_10_LEN + 7] = 0xFF;
        }
        //now issue the mode select 10 command
        ret = scsi_Mode_Select_10(device, 0x18, true, true, false, scsiDataBuffer, 0x18);
        if (ret == SUCCESS)
        {
            if (reset)
            {
                uint8_t readCap10Data[8] = { 0 };
                uint8_t readCap16Data[32] = { 0 };
                //read capacity command to get the max LBA
                if (SUCCESS == scsi_Read_Capacity_10(device, readCap10Data, 8))
                {
                    uint64_t tempMax = 0;
                    copy_Read_Capacity_Info(&device->drive_info.deviceBlockSize, &device->drive_info.devicePhyBlockSize, &tempMax, &device->drive_info.sectorAlignment, readCap10Data, false);
                    if (tempMax == UINT32_MAX || tempMax == 0)
                    {
                        if (SUCCESS == scsi_Read_Capacity_16(device, readCap16Data, 32))
                        {
                            copy_Read_Capacity_Info(&device->drive_info.deviceBlockSize, &device->drive_info.devicePhyBlockSize, &device->drive_info.deviceMaxLba, &device->drive_info.sectorAlignment, readCap16Data, true);
                        }
                    }
                    else
                    {
                        device->drive_info.deviceMaxLba = tempMax;
                    }
                }
                else if (SUCCESS == scsi_Read_Capacity_16(device, readCap16Data, 32))
                {
                    copy_Read_Capacity_Info(&device->drive_info.deviceBlockSize, &device->drive_info.devicePhyBlockSize, &device->drive_info.deviceMaxLba, &device->drive_info.sectorAlignment, readCap16Data, true);
                }
            }
            else
            {
                device->drive_info.deviceMaxLba = newMaxLBA;
            }
        }
    }
    else
    {
        if (VERBOSITY_QUIET < device->deviceVerbosity)
        {
            printf("Failed to retrieve block descriptor from device!\n");
        }
        ret = FAILURE;
    }
    safe_Free_aligned(scsiDataBuffer)
    return ret;
}

int ata_Set_Max_LBA(tDevice *device, uint64_t newMaxLBA, bool reset)
{
    int ret = NOT_SUPPORTED;
    //first do an identify to figure out which method we can use to set the maxLBA (legacy, or new Max addressable address feature set)
    uint64_t nativeMaxLBA = 0;
    //always get the native max first (even if that's only a restriction of the HPA feature set)
    if (SUCCESS == (ret = get_Native_Max_LBA(device, &nativeMaxLBA)))
    {
        if (reset == true)
        {
            newMaxLBA = nativeMaxLBA;
        }
        if ((reset && (newMaxLBA - 1) == device->drive_info.deviceMaxLba) || (newMaxLBA == device->drive_info.deviceMaxLba))//The -1 is due to how maxlba is read and saved in the tDevice struct.
        {
            //already at maxLBA. Do not make a change.
            //Both HPA and AMAC will require a power cycle between calls to setting the maxLBA, so no need to use that up if this has already been set.
            ret = SUCCESS;
        }
        else
        {
            if (is_ATA_Identify_Word_Valid_With_Bits_14_And_15(device->drive_info.IdentifyData.ata.Word119) && device->drive_info.IdentifyData.ata.Word119 & BIT8)
            {
                //accessible Max Address Configuration feature set supported
                ret = ata_Set_Accessible_Max_Address_Ext(device, newMaxLBA);
            }
            else if (is_ATA_Identify_Word_Valid(device->drive_info.IdentifyData.ata.Word082) && device->drive_info.IdentifyData.ata.Word082 & BIT10) //HPA feature set
            {
                if (device->drive_info.ata_Options.fourtyEightBitAddressFeatureSetSupported)
                {
                    ret = ata_Set_Max_Address_Ext(device, newMaxLBA, true); //this is a non-volitile command
                }
                else
                {
                    if (newMaxLBA <= MAX_28BIT)
                    {
                        ret = ata_Set_Max_Address(device, C_CAST(uint32_t, newMaxLBA), true); //this is a non-volitile command
                    }
                    else
                    {
                        ret = NOT_SUPPORTED;
                    }
                }
            }
            else //shouldn't even get here right now...
            {
                if (VERBOSITY_QUIET < device->deviceVerbosity)
                {
                    printf("Setting max LBA is not supported on this device\n");
                }
                ret = NOT_SUPPORTED;
            }
        }
    }
    if (ret == SUCCESS)
    {
        device->drive_info.deviceMaxLba = newMaxLBA;//successfully changed so update the device structure
    }
    return ret;
}

int set_Max_LBA(tDevice *device, uint64_t newMaxLBA, bool reset)
{
    int ret = NOT_SUPPORTED;
    if (device->drive_info.drive_type == SCSI_DRIVE)
    {
        ret = scsi_Set_Max_LBA(device, newMaxLBA, reset);
    }
    else if (device->drive_info.drive_type == ATA_DRIVE)
    {
        ret = ata_Set_Max_LBA(device, newMaxLBA, reset);
    }
    else
    {
        if (VERBOSITY_QUIET < device->deviceVerbosity)
        {
            printf("Setting the max LBA is not supported on this device type at this time\n");
        }
        ret = NOT_SUPPORTED;
    }
    return ret;
}

//This function is specifically named since it has a main purpose: restoring max LBA to erase a drive as much as possible
//or to allow validation of an erase as much as possible.
//Because of this, it handles all the ATA checks to make sure all features are restored or a proper
//error code for frozen or access denied is returned (HPA/AMAC/DCO and HPA security are all handled)
int restore_Max_LBA_For_Erase(tDevice* device)
{
    int ret = NOT_SUPPORTED;
    if (device->drive_info.drive_type == SCSI_DRIVE)
    {
        ret = scsi_Set_Max_LBA(device, 0, true);
    }
    else if (device->drive_info.drive_type == ATA_DRIVE)
    {
        //before calling the reset, check if HPA security might be active. This could block this from working
        bool hpaSecurityEnabled = false;
        if (is_ATA_Identify_Word_Valid(device->drive_info.IdentifyData.ata.Word083) && device->drive_info.IdentifyData.ata.Word083 & BIT8)
        {
            //HPA security is supported...check if enabled to be able to return proper error code if cannot do restoration
            if (is_ATA_Identify_Word_Valid(device->drive_info.IdentifyData.ata.Word086) && device->drive_info.IdentifyData.ata.Word086 & BIT8)
            {
                hpaSecurityEnabled = true;
            }
        }
        ret = ata_Set_Max_LBA(device, 0, true);
        if (ret != SUCCESS && hpaSecurityEnabled)
        {
            //most likely command aborted because this feature is active and has locked the HPA from being changed/restored
            return DEVICE_ACCESS_DENIED;
        }
        //After the restore, check if DCO feature is on the drive and if it reports a higher max LBA to restore to. HPA must be restored first which should have already happened in the previous function.
        if (ret == SUCCESS && is_DCO_Supported(device, NULL))
        {
            //need to restore DCO as well.
            //TODO: one thing we may need to consider is that DCO has disabled features or more importantly DMA modes for compatibility.
            //      This would be especially important on PATA drives if there was a controller bug where certain DMA modes do not work properly.
            //      So the best thing to do would be to figure out what has been disabled by DCO, save that info, restore DCO, then go and disable those features/modes but leave the maxLBA as high as it can go.
            ret = dco_Restore(device);
        }
    }
    return ret;
}
