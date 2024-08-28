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
// \file set_max_lba.c
// \brief This file defines the functions for setting the maxLBA on a device

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

#include "operations_Common.h"
#include "set_max_lba.h"
#include "scsi_helper_func.h"
#include "ata_device_config_overlay.h"
#include "platform_helper.h"
#include "logs.h"
#include <ctype.h>

eReturnValues ata_Get_Native_Max_LBA(tDevice *device, uint64_t *nativeMaxLBA)
{
    eReturnValues ret = SUCCESS;
    if ((is_ATA_Identify_Word_Valid(device->drive_info.IdentifyData.ata.Word086) && device->drive_info.IdentifyData.ata.Word086 & BIT15)/*validate words 119,120 are valid first, then validate that word*/
        && (is_ATA_Identify_Word_Valid_With_Bits_14_And_15(device->drive_info.IdentifyData.ata.Word119) && device->drive_info.IdentifyData.ata.Word119 & BIT8)) //accessible max address feature set
    {
        ret = ata_Get_Native_Max_Address_Ext(device, nativeMaxLBA);
    }
    else if (is_ATA_Identify_Word_Valid(device->drive_info.IdentifyData.ata.Word082) && device->drive_info.IdentifyData.ata.Word082 & BIT10) //HPA feature set
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

eReturnValues get_Native_Max_LBA(tDevice *device, uint64_t *nativeMaxLBA)
{
    eReturnValues ret = UNKNOWN;
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

eReturnValues scsi_Set_Max_LBA(tDevice* device, uint64_t newMaxLBA, bool reset)
{
    return scsi_Set_Max_LBA_2(device, newMaxLBA, reset, false);
}

eReturnValues scsi_Set_Max_LBA_2(tDevice* device, uint64_t newMaxLBA, bool reset, bool changeId)
{
    eReturnValues ret = UNKNOWN;
    uint8_t *scsiDataBuffer = C_CAST(uint8_t*, safe_calloc_aligned(0x18, sizeof(uint8_t), device->os_info.minimumAlignment));//this should be big enough to get back the block descriptor we care about
    if (scsiDataBuffer == M_NULLPTR)
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
        if (changeId)
        {
            scsiDataBuffer[3] |= BIT5;//set the CAPPID bit
        }
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
                DECLARE_ZERO_INIT_ARRAY(uint8_t, readCap10Data, 8);
                DECLARE_ZERO_INIT_ARRAY(uint8_t, readCap16Data, 32);
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
    safe_free_aligned(&scsiDataBuffer);
    return ret;
}

eReturnValues ata_Set_Max_LBA(tDevice * device, uint64_t newMaxLBA, bool reset)
{
    return ata_Set_Max_LBA_2(device, newMaxLBA, reset, false);
}

eReturnValues ata_Set_Max_LBA_2(tDevice * device, uint64_t newMaxLBA, bool reset, bool changeId)
{
    eReturnValues ret = NOT_SUPPORTED;
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
            if ((is_ATA_Identify_Word_Valid(device->drive_info.IdentifyData.ata.Word086) && device->drive_info.IdentifyData.ata.Word086 & BIT15) && /*validate 119 and 120 will be valid first*/
                (is_ATA_Identify_Word_Valid_With_Bits_14_And_15(device->drive_info.IdentifyData.ata.Word119) && device->drive_info.IdentifyData.ata.Word119 & BIT8))
            {
                //accessible Max Address Configuration feature set supported
                ret = ata_Set_Accessible_Max_Address_Ext(device, newMaxLBA, changeId);
            }
            else if (is_ATA_Identify_Word_Valid(device->drive_info.IdentifyData.ata.Word082) && device->drive_info.IdentifyData.ata.Word082 & BIT10) //HPA feature set
            {
                if (changeId)
                {
                    printf("Change model number is not supported on this device\n");
                    ret = NOT_SUPPORTED;
                }
                else
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

eReturnValues set_Max_LBA(tDevice * device, uint64_t newMaxLBA, bool reset)
{
    return set_Max_LBA_2(device, newMaxLBA, reset, false);
}

eReturnValues set_Max_LBA_2(tDevice * device, uint64_t newMaxLBA, bool reset, bool changeId)
{
    eReturnValues ret = NOT_SUPPORTED;
    if (device->drive_info.drive_type == SCSI_DRIVE)
    {
        ret = scsi_Set_Max_LBA_2(device, newMaxLBA, reset, changeId);
    }
    else if (device->drive_info.drive_type == ATA_DRIVE)
    {
        ret = ata_Set_Max_LBA_2(device, newMaxLBA, reset, changeId);
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
eReturnValues restore_Max_LBA_For_Erase(tDevice* device)
{
    eReturnValues ret = NOT_SUPPORTED;
    if (device->drive_info.drive_type == SCSI_DRIVE)
    {
        ret = scsi_Set_Max_LBA(device, 0, true);
    }
    else if (device->drive_info.drive_type == ATA_DRIVE)
    {
        //before calling the reset, check if HPA security might be active. This could block this from working
        bool hpaSecurityEnabled = false;
        uint64_t currentMaxLBA = device->drive_info.deviceMaxLba;
        uint64_t hpaamacMax = 0;
        dcoData dcoIDData;
        memset(&dcoIDData, 0, sizeof(dcoData));
        if (is_ATA_Identify_Word_Valid(device->drive_info.IdentifyData.ata.Word083) && device->drive_info.IdentifyData.ata.Word083 & BIT8)
        {
            //HPA security is supported...check if enabled to be able to return proper error code if cannot do restoration
            if (is_ATA_Identify_Word_Valid(device->drive_info.IdentifyData.ata.Word086) && device->drive_info.IdentifyData.ata.Word086 & BIT8)
            {
                hpaSecurityEnabled = true;
            }
        }

        if (device->drive_info.bridge_info.isValid)
        {
            currentMaxLBA = device->drive_info.bridge_info.childDeviceMaxLba;
        }

        //Before attempting any restore commands, get the current MaxLBA, HPA/AMAC native MaxLBA, and the DCO identify MaxLBA.
        //Compare them to see which, if any, needs to be restored.
        //This will reduce command aborts and errors in this process.
        
        eReturnValues readnativemaxret = ata_Get_Native_Max_LBA(device, &hpaamacMax);

        //First compare read native max to current max
        if (readnativemaxret == SUCCESS && ((hpaamacMax - UINT64_C(1)) > currentMaxLBA))//minus 1 is due to how tDevice saves maxLBA as a SCSI value
        {
            ret = ata_Set_Max_LBA(device, 0, true);
            if (ret != SUCCESS && hpaSecurityEnabled)
            {
                //most likely command aborted because this feature is active and has locked the HPA from being changed/restored
                return DEVICE_ACCESS_DENIED;
            }
            else if (ret != SUCCESS)
            {
                //HPA and AMAC require a power cycle between each change to the max LBA.
                //this could also cover when these features are frozen.
                return POWER_CYCLE_REQUIRED;
            }
            else if (ret == SUCCESS)
            {
                currentMaxLBA = hpaamacMax - 1;
            }
            if (ret == SUCCESS && is_DCO_Supported(device, M_NULLPTR))
            {
                //check if DCO max is higher or not.
                eReturnValues dcoret = dco_Identify(device, &dcoIDData);
                if (SUCCESS == dcoret)
                {
                    if ((dcoIDData.maxLBA - UINT64_C(1)) > currentMaxLBA || ((dcoIDData.maxLBA - UINT64_C(1)) > hpaamacMax))
                    {
                        ret = dco_Restore(device);
                        if (ret != SUCCESS)
                        {
                            //DCO restore is needed, but we cannot do it until the drive has been power cycled
                            ret = POWER_CYCLE_REQUIRED;
                        }
                    }
                    else
                    {
                        ret = SUCCESS;
                    }
                }
                else
                {
                    //if we cannot read DCO identify, then we need to pass that error out
                    return dcoret;
                }
            }
        }
        else if (readnativemaxret == SUCCESS && is_DCO_Supported(device, M_NULLPTR))
        {
            eReturnValues dcoret = dco_Identify(device, &dcoIDData);
            if (SUCCESS == dcoret)
            {
                if ((dcoIDData.maxLBA - UINT64_C(1)) > currentMaxLBA || ((dcoIDData.maxLBA - UINT64_C(1)) > hpaamacMax))
                {
                    ret = dco_Restore(device);
                }
                else
                {
                    ret = SUCCESS;
                }
            }
            else
            {
                //if we cannot read DCO identify, then we need to pass that error out
                return dcoret;
            }
        }
        else if (readnativemaxret == SUCCESS && ((hpaamacMax - UINT64_C(1)) == currentMaxLBA))
        {
            ret = SUCCESS;
        }
        else
        {
            ret = readnativemaxret;
        }
    }
    return ret;
}

static uint64_t get_ATA_MaxLBA(tDevice* device)
{
    uint64_t maxLBA = 0;
    //read the max LBA from idenfity data.
    //now we need to compare read capacity data and ATA identify data.
    if (SUCCESS == ata_Identify(device, C_CAST(uint8_t*, &device->drive_info.IdentifyData.ata.Word000), 512))
    {
        uint8_t* identifyData = C_CAST(uint8_t*, &device->drive_info.IdentifyData.ata.Word000);
        if (is_ATA_Identify_Word_Valid_With_Bits_14_And_15(device->drive_info.IdentifyData.ata.Word083) && device->drive_info.IdentifyData.ata.Word083 & BIT10)
        {
            //acs4 - word 69 bit3 means extended number of user addressable sectors word supported (words 230 - 233) (Use this to get the max LBA since words 100 - 103 may only contain a value of FFFF_FFFF)
            if ((is_ATA_Identify_Word_Valid(device->drive_info.IdentifyData.ata.Word053) && device->drive_info.IdentifyData.ata.Word053 & BIT1) /* this is a validity bit for field 69 */
                && (is_ATA_Identify_Word_Valid(device->drive_info.IdentifyData.ata.Word069) && device->drive_info.IdentifyData.ata.Word069 & BIT3))
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
    uint32_t blockSize = 0;
    uint32_t physBlockSize = 0;
    uint16_t alignment = 0;
    //read capacity 10 first. If that reports FFFFFFFFh then do read capacity 16.
    //if read capacity 10 fails, retry with read capacity 16
    uint8_t* readCapBuf = C_CAST(uint8_t*, safe_calloc_aligned(READ_CAPACITY_10_LEN, sizeof(uint8_t), device->os_info.minimumAlignment));
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
            uint8_t* temp = C_CAST(uint8_t*, safe_realloc_aligned(readCapBuf, READ_CAPACITY_10_LEN, READ_CAPACITY_16_LEN, device->os_info.minimumAlignment));
            if (!temp)
            {
                safe_free_aligned(&readCapBuf);
                return maxLBA;
            }
            readCapBuf = temp;
            memset(readCapBuf, 0, READ_CAPACITY_16_LEN);
            if (SUCCESS == scsi_Read_Capacity_16(device, readCapBuf, READ_CAPACITY_16_LEN))
            {
                uint64_t tempmaxLBA = 0;
                copy_Read_Capacity_Info(&blockSize, &physBlockSize, &tempmaxLBA, &alignment, readCapBuf, true);
                //some USB drives will return success and no data, so check if this local var is 0 or not...if not, we can use this data
                if (tempmaxLBA != 0)
                {
                    maxLBA = tempmaxLBA;
                }
            }
        }
    }
    else
    {
        //try read capacity 16, if that fails we are done trying
        uint8_t* temp = C_CAST(uint8_t*, safe_realloc_aligned(readCapBuf, READ_CAPACITY_10_LEN, READ_CAPACITY_16_LEN, device->os_info.minimumAlignment));
        if (temp == M_NULLPTR)
        {
            safe_free_aligned(&readCapBuf);
            return maxLBA;
        }
        readCapBuf = temp;
        memset(readCapBuf, 0, READ_CAPACITY_16_LEN);
        if (SUCCESS == scsi_Read_Capacity_16(device, readCapBuf, READ_CAPACITY_16_LEN))
        {
            copy_Read_Capacity_Info(&blockSize, &physBlockSize, &maxLBA, &alignment, readCapBuf, true);
        }
    }
    safe_free_aligned(&readCapBuf);
    return maxLBA;
}

bool is_Max_LBA_In_Sync_With_Adapter_Or_Driver(tDevice* device, bool issueReset)
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
        DECLARE_ZERO_INIT_ARRAY(uint8_t, satVPDPage89, VPD_ATA_INFORMATION_LEN);
        if (SUCCESS == scsi_Inquiry(device, satVPDPage89, VPD_ATA_INFORMATION_LEN, ATA_INFORMATION, true, false))
        {
            //Note that the identify data in this page can be modified by the SATL in some versions of SAT, so do not trust it.
        }
        if (issueReset)
        {
            //NOTE: This is an IOCTL implemented by the low-level system files.
            //      There is no guarantee this is implemented or works at all. For example, in Windows the IOCTLs for this are obsolete and do not work on any modern hardware I can find.
            //      While testing a Broadcom controller on Linux, this does seem to fix the synchronization issue with the SG reset IOCTL.
            //      This may not work on a different controller and there is no guarantee the driver implements this IOCTL
            if (SUCCESS == os_Device_Reset(device))
            {
                delay_Seconds(1);
                //issue test unit ready after this to clear out a sense code for the device having received a bus reset
                scsi_Test_Unit_Ready(device, M_NULLPTR);
            }
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
            else if ((ataMaxLBA - 1) == scsiMaxLBA)
            {
                //possibly USB being off by one since it can use maxLBA to save info for the adapter.
                //Considering this in sync because of this special case
                inSync = true;
            }
        }
        if (!inSync && !issueReset)
        {
            //reset was not issued and method(s) to sync without a reset did not work
            //So now retry with the reset and see if that helps
            inSync = is_Max_LBA_In_Sync_With_Adapter_Or_Driver(device, true);
        }
    }
    else if (device->drive_info.drive_type == NVME_DRIVE)
    {
        //NOTE: There are not the same style of commands on nvme to change capacity as there is with SAS and SATA
        //      In NVMe there is the namespace management feature, but that is far more complicated than this command.
        //      For now just returning true for NVMe since the only real encapsulated NVMe drives will be on USB
        //      and it is extremely unlikely any namespace management commands will work over USB
        inSync = true;
    }
    return inSync;
}

bool is_Change_Identify_String_Supported(tDevice* device)
{
    if (device->drive_info.drive_type == ATA_DRIVE)
    {
        DECLARE_ZERO_INIT_ARRAY(uint8_t, idDataLogSupportedCapabilities, LEGACY_DRIVE_SEC_SIZE);
        if (SUCCESS == send_ATA_Read_Log_Ext_Cmd(device, ATA_LOG_IDENTIFY_DEVICE_DATA, ATA_ID_DATA_LOG_SUPPORTED_CAPABILITIES, idDataLogSupportedCapabilities, LEGACY_DRIVE_SEC_SIZE, 0))
        {
            uint64_t qword0 = M_BytesTo8ByteValue(idDataLogSupportedCapabilities[7], idDataLogSupportedCapabilities[6], idDataLogSupportedCapabilities[5], idDataLogSupportedCapabilities[4], idDataLogSupportedCapabilities[3], idDataLogSupportedCapabilities[2], idDataLogSupportedCapabilities[1], idDataLogSupportedCapabilities[0]);
            if (qword0 & BIT63 && M_Byte2(qword0) == ATA_ID_DATA_LOG_SUPPORTED_CAPABILITIES && M_Word0(qword0) >= 0x0001)
            {
                uint64_t supportedCapabilitiesQWord = M_BytesTo8ByteValue(idDataLogSupportedCapabilities[15], idDataLogSupportedCapabilities[14], idDataLogSupportedCapabilities[13], idDataLogSupportedCapabilities[12], idDataLogSupportedCapabilities[11], idDataLogSupportedCapabilities[10], idDataLogSupportedCapabilities[9], idDataLogSupportedCapabilities[8]);
                return (supportedCapabilitiesQWord & BIT63 && supportedCapabilitiesQWord & BIT58); //check bit63 since it should always be 1, then bit 58 for Capacity/Model Number Mapping
            }
        }
    }
    else if (device->drive_info.drive_type == SCSI_DRIVE)
    {
        uint32_t capIDVPDSizeBytes = 0;
        if (SUCCESS == get_SCSI_VPD_Page_Size(device, CAPACITY_PRODUCT_IDENTIFICATION_MAPPING, &capIDVPDSizeBytes) && capIDVPDSizeBytes > 0)
        {
            return (capIDVPDSizeBytes > 0); // TODO: should we return true if 0 bytes?
        }
    }
    return false;
}

ptrcapacityModelNumberMapping get_Capacity_Model_Number_Mapping(tDevice* device)
{
    ptrcapacityModelNumberMapping capModelMapping = M_NULLPTR;
    if (device->drive_info.drive_type == ATA_DRIVE)
    {
        uint32_t capMNLogSizeBytes = 0;
        if (SUCCESS == get_ATA_Log_Size(device, ATA_LOG_CAPACITY_MODELNUMBER_MAPPING, &capMNLogSizeBytes, true, false) && capMNLogSizeBytes > 0)
        {
            uint8_t* capMNMappingLog = C_CAST(uint8_t*, safe_calloc_aligned(capMNLogSizeBytes, sizeof(uint8_t), device->os_info.minimumAlignment));
            if (!capMNMappingLog)
            {
                return M_NULLPTR;
            }
            if (SUCCESS == get_ATA_Log(device, ATA_LOG_CAPACITY_MODELNUMBER_MAPPING, M_NULLPTR, M_NULLPTR, true, false, true, capMNMappingLog, capMNLogSizeBytes, M_NULLPTR, 0, 0))
            {
                //header is first 8bytes
                uint32_t numberOfDescriptors = M_BytesTo4ByteValue(0, capMNMappingLog[2], capMNMappingLog[1], capMNMappingLog[0]);
                uint32_t capModelMappingSz = C_CAST(uint32_t, (sizeof(capacityModelNumberMapping) - sizeof(capacityModelDescriptor)) + (sizeof(capacityModelDescriptor) * numberOfDescriptors));
                capModelMapping = C_CAST(ptrcapacityModelNumberMapping, safe_calloc(capModelMappingSz, sizeof(uint8_t)));
                if (capModelMapping != M_NULLPTR)
                {
                    capModelMapping->numberOfDescriptors = numberOfDescriptors;
                    //now loop through descriptors
                    for (uint32_t offset = 8, descriptorCounter = 0; offset < capMNLogSizeBytes && descriptorCounter < capModelMapping->numberOfDescriptors; offset += 48, ++descriptorCounter)
                    {
                        capModelMapping->descriptor[descriptorCounter].capacityMaxAddress = M_BytesTo8ByteValue(0, 0, capMNMappingLog[offset + 5], capMNMappingLog[offset + 4], capMNMappingLog[offset + 3], capMNMappingLog[offset + 2], capMNMappingLog[offset + 1], capMNMappingLog[offset + 0]);
                        uint16_t mnLimit = M_Min(MODEL_NUM_LEN, ATA_IDENTIFY_MN_LENGTH);
                        memset(capModelMapping->descriptor[descriptorCounter].modelNumber, 0, mnLimit + 1);
                        memcpy(capModelMapping->descriptor[descriptorCounter].modelNumber, &capMNMappingLog[offset + 8], mnLimit);
                        for (uint8_t iter = 0; iter < mnLimit; ++iter)
                        {
                            if (!safe_isascii(capModelMapping->descriptor[descriptorCounter].modelNumber[iter]) || !safe_isprint(capModelMapping->descriptor[descriptorCounter].modelNumber[iter]))
                            {
                                capModelMapping->descriptor[descriptorCounter].modelNumber[iter] = ' ';//replace with a space
                            }
                        }
#if !defined(__BIG_ENDIAN__)
                        byte_Swap_String_Len(capModelMapping->descriptor[descriptorCounter].modelNumber, MODEL_NUM_LEN);
#endif
                        remove_Leading_And_Trailing_Whitespace_Len(capModelMapping->descriptor[descriptorCounter].modelNumber, MODEL_NUM_LEN);
                    }
                }
            }
            safe_Free_aligned(C_CAST(void**, &capMNMappingLog));
        }
    }
    else if (device->drive_info.drive_type == SCSI_DRIVE)
    {
        uint32_t capIDVPDSizeBytes = 0;
        if (SUCCESS == get_SCSI_VPD_Page_Size(device, CAPACITY_PRODUCT_IDENTIFICATION_MAPPING, &capIDVPDSizeBytes) && capIDVPDSizeBytes > 0)
        {
            uint8_t* capProdIDMappingVPD = C_CAST(uint8_t*, safe_calloc_aligned(capIDVPDSizeBytes, sizeof(uint8_t), device->os_info.minimumAlignment));
            if (!capProdIDMappingVPD)
            {
                return M_NULLPTR;
            }
            if (SUCCESS == get_SCSI_VPD(device, CAPACITY_PRODUCT_IDENTIFICATION_MAPPING, M_NULLPTR, M_NULLPTR, true, capProdIDMappingVPD, capIDVPDSizeBytes, M_NULLPTR))
            {
                //calculate number of descriptors based on page length
                uint32_t numberOfDescriptors = M_BytesTo2ByteValue(capProdIDMappingVPD[2], capProdIDMappingVPD[3]) / 48;//Each descriptor is 48B long
                uint32_t capProdIDMappingSz = C_CAST(uint32_t, (sizeof(capacityModelNumberMapping) - sizeof(capacityModelDescriptor)) + (sizeof(capacityModelDescriptor) * numberOfDescriptors));
                capModelMapping = C_CAST(ptrcapacityModelNumberMapping, safe_calloc(capProdIDMappingSz, sizeof(uint8_t)));
                if (capModelMapping != M_NULLPTR)
                {
                    capModelMapping->numberOfDescriptors = numberOfDescriptors;
                    //loop through descriptors
                    for (uint32_t offset = 4, descriptorCounter = 0; offset < capIDVPDSizeBytes && descriptorCounter < capModelMapping->numberOfDescriptors; offset += 48, ++descriptorCounter)
                    {
                        capModelMapping->descriptor[descriptorCounter].capacityMaxAddress = M_BytesTo8ByteValue(capProdIDMappingVPD[offset + 0], capProdIDMappingVPD[offset + 1], capProdIDMappingVPD[offset + 2], capProdIDMappingVPD[offset + 3], capProdIDMappingVPD[offset + 4], capProdIDMappingVPD[offset + 5], capProdIDMappingVPD[offset + 6], capProdIDMappingVPD[offset + 7]);
                        capModelMapping->descriptor[descriptorCounter].capacityMaxAddress -= 1; //Need to -1 for SCSI so that this will match the -i report. If this is not done, then  we end up with 1 less than the value provided.
                        uint16_t mnLimit = M_Min(MODEL_NUM_LEN, 16);
                        memset(capModelMapping->descriptor[descriptorCounter].modelNumber, 0, mnLimit + 1);
                        memcpy(capModelMapping->descriptor[descriptorCounter].modelNumber, &capProdIDMappingVPD[offset + 8], mnLimit);
                        for (uint8_t iter = 0; iter < mnLimit; ++iter)
                        {
                            if (!safe_isascii(capModelMapping->descriptor[descriptorCounter].modelNumber[iter]) || !safe_isprint(capModelMapping->descriptor[descriptorCounter].modelNumber[iter]))
                            {
                                capModelMapping->descriptor[descriptorCounter].modelNumber[iter] = ' ';//replace with a space
                            }
                        }
                        remove_Leading_And_Trailing_Whitespace_Len(capModelMapping->descriptor[descriptorCounter].modelNumber, MODEL_NUM_LEN);
                    }
                }
            }
            safe_free_aligned(&capProdIDMappingVPD);
        }
    }
    return capModelMapping;
}

void delete_Capacity_Model_Number_Mapping(ptrcapacityModelNumberMapping capModelMapping)
{
    safe_free_cap_mn_map(&capModelMapping);
}

void print_Capacity_Model_Number_Mapping(ptrcapacityModelNumberMapping capModelMapping)
{
    if (capModelMapping)
    {
        printf("---Capacity model number mapping---\n");
        printf("              MaxLBA Model number\n");
        for (uint32_t descriptorCounter = 0; descriptorCounter < capModelMapping->numberOfDescriptors; descriptorCounter++)
        {
            printf("%20" PRIu64 " %s\n", capModelMapping->descriptor[descriptorCounter].capacityMaxAddress, capModelMapping->descriptor[descriptorCounter].modelNumber);
        }
    }
}
