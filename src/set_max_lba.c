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

static uint64_t get_ATA_MaxLBA(tDevice* device)
{
    uint64_t maxLBA = 0;
    //read the max LBA from idenfity data.
    //TODO: read from identify device data log???
    //now we need to compare read capacity data and ATA identify data.
    if (SUCCESS == ata_Identify(device, C_CAST(uint8_t*, &device->drive_info.IdentifyData.ata.Word000), 512))
    {
        uint8_t* identifyData = C_CAST(uint8_t*, &device->drive_info.IdentifyData.ata.Word000);
        if (device->drive_info.IdentifyData.ata.Word083 & BIT10)
        {
            //acs4 - word 69 bit3 means extended number of user addressable sectors word supported (words 230 - 233) (Use this to get the max LBA since words 100 - 103 may only contain a value of FFFF_FFFF)
            if (device->drive_info.IdentifyData.ata.Word069 & BIT3)
            {
                maxLBA = M_BytesTo8ByteValue(identifyData[467], identifyData[466], identifyData[465], identifyData[464], identifyData[463], identifyData[462], identifyData[461], identifyData[460]);
            }
            else
            {
                maxLBA = M_BytesTo8ByteValue(identifyData[207], identifyData[206], identifyData[205], identifyData[204], identifyData[203], identifyData[202], identifyData[201], identifyData[200]);
            }
        }
        else
        {
            maxLBA = M_BytesTo4ByteValue(identifyData[123], identifyData[122], identifyData[121], identifyData[120]);
        }
        //adjust to make it report more like SCSI since that is how all this library works
        if (maxLBA > 0)
        {
            maxLBA -= 1;
        }
    }
    return maxLBA;
}

static uint64_t get_SCSI_MaxLBA(tDevice* device)
{
    uint64_t maxLBA = 0;
    uint32_t blockSize = 0, physBlockSize = 0;
    uint16_t alignment = 0;
    //read capacity 10 first. If that reports FFFFFFFFh then do read capacity 16.
    //if read capacity 10 fails, retry with read capacity 16
    uint8_t* readCapBuf = C_CAST(uint8_t*, calloc_aligned(READ_CAPACITY_10_LEN, sizeof(uint8_t), device->os_info.minimumAlignment));
    if (!readCapBuf)
    {
        return maxLBA;
    }
    if (SUCCESS == scsi_Read_Capacity_10(device, readCapBuf, READ_CAPACITY_10_LEN))
    {
        copy_Read_Capacity_Info(&blockSize, &physBlockSize, &maxLBA, &alignment, readCapBuf, false);
        if (device->drive_info.scsiVersion > 3)//SPC2 and higher can reference SBC2 and higher which introduced read capacity 16
        {
            //try a read capacity 16 anyways and see if the data from that was valid or not since that will give us a physical sector size whereas readcap10 data will not
            uint8_t* temp = C_CAST(uint8_t*, realloc_aligned(readCapBuf, READ_CAPACITY_10_LEN, READ_CAPACITY_16_LEN, device->os_info.minimumAlignment));
            if (!temp)
            {
                safe_Free_aligned(readCapBuf)
                return maxLBA;
            }
            readCapBuf = temp;
            memset(readCapBuf, 0, READ_CAPACITY_16_LEN);
            if (SUCCESS == scsi_Read_Capacity_16(device, readCapBuf, READ_CAPACITY_16_LEN))
            {
                uint64_t tempmaxLBA = 0;
                copy_Read_Capacity_Info(&blockSize, &physBlockSize, &maxLBA, &alignment, readCapBuf, true);
                //some USB drives will return success and no data, so check if this local var is 0 or not...if not, we can use this data
                if (tempmaxLBA != 0)
                {
                    maxLBA = maxLBA;
                }
            }
        }
    }
    else
    {
        //try read capacity 16, if that fails we are done trying
        uint8_t* temp = C_CAST(uint8_t*, realloc_aligned(readCapBuf, READ_CAPACITY_10_LEN, READ_CAPACITY_16_LEN, device->os_info.minimumAlignment));
        if (temp == NULL)
        {
            safe_Free_aligned(readCapBuf)
            return maxLBA;
        }
        readCapBuf = temp;
        memset(readCapBuf, 0, READ_CAPACITY_16_LEN);
        if (SUCCESS == scsi_Read_Capacity_16(device, readCapBuf, READ_CAPACITY_16_LEN))
        {
            copy_Read_Capacity_Info(&blockSize, &physBlockSize, &maxLBA, &alignment, readCapBuf, true);
        }
    }
    safe_Free_aligned(readCapBuf)
    return maxLBA;
}

bool is_Max_LBA_In_Sync_With_Adapter_Or_Driver(tDevice* device)
{
    bool inSync = false;
    if (device->drive_info.drive_type == SCSI_DRIVE)
    {
        //there is no translator present so nothing to synchronize
        inSync = true;
    }
    else if (device->drive_info.drive_type == ATA_DRIVE)
    {
        uint64_t ataMaxLBA = 0;
        uint64_t scsiMaxLBA = 0;
        //Always assume there is a translator present because there should be.
        //It will either be software, a driver, or hardware.
        //opensea-transport has a software based translator present so all requests in here will be handled by it if there is not a driver or hardware translator
        //We need to first try reading VPD page 89h. This is the SAT page and it requires pulling fresh identify data....it MIGHT update the controller, but it might not.
        uint8_t satVPDPage89[VPD_ATA_INFORMATION_LEN] = { 0 };
        if (SUCCESS == scsi_Inquiry(device, satVPDPage89, VPD_ATA_INFORMATION_LEN, ATA_INFORMATION, true, false))
        {
            //TODO: Do anything with the data?
            //      Note that the identify data in this page can be modified by the SATL in some versions of SAT, so do not trust it.
        }
        ataMaxLBA = get_ATA_MaxLBA(device);
        scsiMaxLBA = get_SCSI_MaxLBA(device);
        //before comparing, know that ATA and SCSI report max LBA slightly differently.
        //It's an off by 1 scenario.
        //In SCSI, max LBA is reported as last readable LBA of that value read lba <=maxLBA
        //In ATA, maxLBA is a count of readable LBAs...so                 read lba < maxLBA
        //The functions used to get max lba are already taking this difference into account
        //The other special case to have in mind is USB.
        //USB drives may report a lower value as well.
        //USB drives may also do reverse 4k emulation which will also change how this is reported.
        //So USB has yet another special case to handle when this happens.
        if (is_Sector_Size_Emulation_Active(device))
        {
            //likely only USB drives from the Windows XP era
            //in this case take the ata max LBA and divide it by the scsi sector size before comparing.
            uint64_t usbAdjustedMaxLBA = ataMaxLBA / device->drive_info.bridge_info.childDeviceBlockSize;
            if (usbAdjustedMaxLBA == scsiMaxLBA)
            {
                inSync = true;
            }
            else if ((usbAdjustedMaxLBA - 1) == scsiMaxLBA)
            {
                //possibly USB being off by one since it can use maxLBA to save info for the adapter.
                //Considering this in sync because of this special case
                inSync = true;
            }
        }
        else
        {
            //most devices used today, USB or HBA.
            if (ataMaxLBA == scsiMaxLBA)
            {
                inSync = true;
            }
            else if ((ataMaxLBA - 1) ==scsiMaxLBA)
            {
                //possibly USB being off by one since it can use maxLBA to save info for the adapter.
                //Considering this in sync because of this special case
                inSync = true;
            }
        }
    }
    else if (device->drive_info.drive_type == NVME_DRIVE)
    {
        //TODO: There are not the same style of commands on nvme to change capacity as there is with SAS and SATA
        //      In NVMe there is the namespace management feature, but that is far more complicated than this command.
        //      For now just returning true for NVMe since the only real encapsulated NVMe drives will be on USB
        //      and it is extremely unlikely any namespace management commands will work over USB
        inSync = true;
    }
    return inSync;
}
