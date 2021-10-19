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
// \file operations.c   Implementation for generic ATA/SCSI functions
//                     The intention of the file is to be generic & not OS specific

#include "operations_Common.h"
#include "operations.h"
#include "ata_helper_func.h"
#include "scsi_helper_func.h"
#include "nvme_helper_func.h"

//headers below are for determining quickest erase
#include "sanitize.h"
#include "writesame.h"
#include "ata_Security.h"
#include "trim_unmap.h"
#include "format.h"
#include "dst.h"
#include "logs.h"//for SCSI mode pages

int get_Ready_LED_State(tDevice *device, bool *readyLEDOnOff)
{
    int ret = UNKNOWN;
    if (device->drive_info.drive_type == SCSI_DRIVE)
    {
        uint8_t *modeSense = C_CAST(uint8_t*, calloc_aligned(24, sizeof(uint8_t), device->os_info.minimumAlignment));
        if (!modeSense)
        {
            perror("calloc failure!");
            return MEMORY_FAILURE;
        }
        if (SUCCESS == scsi_Mode_Sense_10(device, 0x19, 24, 0, true, false, MPC_CURRENT_VALUES, modeSense))
        {
            ret = SUCCESS;
            if (modeSense[2 + MODE_PARAMETER_HEADER_10_LEN] & BIT4)
            {
                *readyLEDOnOff = true;
            }
            else
            {
                *readyLEDOnOff = false;
            }
        }
        else
        {
            ret = FAILURE;
        }
        safe_Free_aligned(modeSense)
    }
    else //ata cannot control ready LED since it is managed by the host, not the drive (drive just reads a signal to change operation as per ATA spec). Not sure if other device types support this change or not at this time.
    {
        ret = NOT_SUPPORTED;
    }
    return ret;
}

int change_Ready_LED(tDevice *device, bool readyLEDDefault, bool readyLEDOnOff)
{
    int ret = UNKNOWN;
    if (device->drive_info.drive_type == SCSI_DRIVE)
    {
        uint8_t *modeSelect = C_CAST(uint8_t*, calloc_aligned(24, sizeof(uint8_t), device->os_info.minimumAlignment));
        if (!modeSelect)
        {
            perror("calloc failure!");
            return MEMORY_FAILURE;
        }
        if (readyLEDDefault)
        {
            //we need to read the default AND current page this way we only touch 1 bit on the page
            if (SUCCESS == scsi_Mode_Sense_10(device, 0x19, 24, 0, true, false, MPC_DEFAULT_VALUES, modeSelect))
            {
                if (modeSelect[2 + MODE_PARAMETER_HEADER_10_LEN] & BIT4)
                {
                    readyLEDOnOff = true;//set to true so that we turn the bit on
                }
            }
            memset(modeSelect, 0, 24);
        }
        if (SUCCESS == scsi_Mode_Sense_10(device, 0x19, 24, 0, true, false, MPC_CURRENT_VALUES, modeSelect))
        {
            if (readyLEDOnOff)//set the bit to 1
            {
                modeSelect[2 + MODE_PARAMETER_HEADER_10_LEN] |= BIT4;
            }
            else//set the bit to 0 if it isn't already 0
            {
                if (modeSelect[2 + MODE_PARAMETER_HEADER_10_LEN] & BIT4)
                {
                    modeSelect[2 + MODE_PARAMETER_HEADER_10_LEN] ^= BIT4;
                }
            }
            //mode data length
            modeSelect[0] = 0;
            modeSelect[1] = 0x10;//16
            //medium type
            modeSelect[2] = 0;
            //device specific
            modeSelect[3] = 0;
            //reserved and LongLBA bit
            modeSelect[4] = RESERVED;
            //reserved
            modeSelect[5] = RESERVED;
            //block desciptor length
            modeSelect[6] = 0;
            modeSelect[7] = 0;
            //send the mode select command
            ret = scsi_Mode_Select_10(device, 24, true, true, false, modeSelect, 24);
        }
        safe_Free_aligned(modeSelect)
    }
    else //ata cannot control ready LED since it is managed by the host, not the drive (drive just reads a signal to change operation as per ATA spec). Not sure if other device types support this change or not at this time.
    {
        ret = NOT_SUPPORTED;
    }
    return ret;
}

//SBC spec. Caching Mode Page NV_DIS
int scsi_Set_NV_DIS(tDevice *device, bool nv_disEnableDisable)
{
    int ret = UNKNOWN;

    if (device->drive_info.drive_type != SCSI_DRIVE)
    {
        return NOT_SUPPORTED;
    }
    //on SAS we change this through a mode page
    uint8_t *cachingModePage = C_CAST(uint8_t*, calloc_aligned(MP_CACHING_LEN + MODE_PARAMETER_HEADER_10_LEN, sizeof(uint8_t), device->os_info.minimumAlignment));
    if (cachingModePage == NULL)
    {
        perror("calloc failure!");
        return MEMORY_FAILURE;
    }
    //first read the current settings
    ret = scsi_Mode_Sense_10(device, MP_CACHING, MP_CACHING_LEN + MODE_PARAMETER_HEADER_10_LEN, 0, true, false, MPC_CURRENT_VALUES, cachingModePage);
    if (ret == SUCCESS)
    {
        //set up the mode parameter header
        //mode data length
        cachingModePage[0] = M_Byte1(MP_CACHING_LEN + (MODE_PARAMETER_HEADER_10_LEN - 2));//add 6 to omit the length bytes
        cachingModePage[1] = M_Byte0(MP_CACHING_LEN + (MODE_PARAMETER_HEADER_10_LEN - 2));
        //medium type
        cachingModePage[2] = 0;
        //device specific
        cachingModePage[3] = 0;
        //reserved and LongLBA bit
        cachingModePage[4] = RESERVED;
        //reserved
        cachingModePage[5] = RESERVED;
        //block desciptor length
        cachingModePage[6] = 0;
        cachingModePage[7] = 0;
        //now go change the bit to what we need it to, then send a mode select command
        if (!nv_disEnableDisable)
        {
            //Disable the NV Cache (set the NV_DIS to one)
            cachingModePage[MODE_PARAMETER_HEADER_10_LEN + 12] |= BIT0;
        }
        else
        {
            //Enable the NV Cache (set the NV_DIS to zero)
            //turn the bit off if it is already set
            if (cachingModePage[MODE_PARAMETER_HEADER_10_LEN + 12] & BIT0)
            {
                cachingModePage[MODE_PARAMETER_HEADER_10_LEN + 12] ^= BIT0;
            }
        }
        //send the mode select command
        ret = scsi_Mode_Select_10(device, MP_CACHING_LEN + MODE_PARAMETER_HEADER_10_LEN, true, true, false, cachingModePage, MP_CACHING_LEN + MODE_PARAMETER_HEADER_10_LEN);
    }
    safe_Free_aligned(cachingModePage)
    return ret;
}

int scsi_Set_Read_Look_Ahead(tDevice *device, bool readLookAheadEnableDisable)
{
    int ret = UNKNOWN;
    //on SAS we change this through a mode page
    uint8_t *cachingModePage = C_CAST(uint8_t*, calloc_aligned(MP_CACHING_LEN + MODE_PARAMETER_HEADER_10_LEN, sizeof(uint8_t), device->os_info.minimumAlignment));
    if (cachingModePage == NULL)
    {
        perror("calloc failure!");
        return MEMORY_FAILURE;
    }
    //first read the current settings
    ret = scsi_Mode_Sense_10(device, MP_CACHING, MP_CACHING_LEN + MODE_PARAMETER_HEADER_10_LEN, 0, true, false, MPC_CURRENT_VALUES, cachingModePage);
    if (ret == SUCCESS)
    {
        //set up the mode parameter header
        //mode data length
        cachingModePage[0] = M_Byte1(MP_CACHING_LEN + 6);
        cachingModePage[1] = M_Byte0(MP_CACHING_LEN + 6);
        //medium type
        cachingModePage[2] = 0;
        //device specific
        cachingModePage[3] = 0;
        //reserved and LongLBA bit
        cachingModePage[4] = RESERVED;
        //reserved
        cachingModePage[5] = RESERVED;
        //block desciptor length
        cachingModePage[6] = 0;
        cachingModePage[7] = 0;
        //now go change the bit to what we need it to, then send a mode select command
        if (readLookAheadEnableDisable == false)
        {
            cachingModePage[MODE_PARAMETER_HEADER_10_LEN + 12] |= BIT5;
        }
        else
        {
            //turn the bit off if it is already set
            if (cachingModePage[MODE_PARAMETER_HEADER_10_LEN + 12] & BIT5)
            {
                cachingModePage[MODE_PARAMETER_HEADER_10_LEN + 12] ^= BIT5;
            }
        }
        //send the mode select command
        ret = scsi_Mode_Select_10(device, MP_CACHING_LEN + MODE_PARAMETER_HEADER_10_LEN, true, true, false, cachingModePage, MP_CACHING_LEN + MODE_PARAMETER_HEADER_10_LEN);
    }
    safe_Free_aligned(cachingModePage)
    return ret;
}

int ata_Set_Read_Look_Ahead(tDevice *device, bool readLookAheadEnableDisable)
{
    int ret = UNKNOWN;
    //on ata, we just send a set features command to change this
    if (readLookAheadEnableDisable == true)
    {
        ret = ata_Set_Features(device, SF_ENABLE_READ_LOOK_AHEAD_FEATURE, 0, 0, 0, 0);
    }
    else
    {
        ret = ata_Set_Features(device, SF_DISABLE_READ_LOOK_AHEAD_FEATURE, 0, 0, 0, 0);
    }
    return ret;
}

int set_Read_Look_Ahead(tDevice *device, bool readLookAheadEnableDisable)
{
    int ret = UNKNOWN;
    if (device->drive_info.drive_type == SCSI_DRIVE)
    {
        ret = scsi_Set_Read_Look_Ahead(device, readLookAheadEnableDisable);
    }
    else if (device->drive_info.drive_type == ATA_DRIVE)
    {
        ret = ata_Set_Read_Look_Ahead(device, readLookAheadEnableDisable);
    }
    else
    {
        ret = NOT_SUPPORTED;
    }
    return ret;
}

int scsi_Set_Write_Cache(tDevice *device, bool writeCacheEnableDisable)
{
    int ret = UNKNOWN;
    //on SAS we change this through a mode page
    uint8_t *cachingModePage = C_CAST(uint8_t*, calloc_aligned(MP_CACHING_LEN + MODE_PARAMETER_HEADER_10_LEN, sizeof(uint8_t), device->os_info.minimumAlignment));
    if (cachingModePage == NULL)
    {
        perror("calloc failure!");
        return MEMORY_FAILURE;
    }
    //first read the current settings
    ret = scsi_Mode_Sense_10(device, MP_CACHING, MP_CACHING_LEN + MODE_PARAMETER_HEADER_10_LEN, 0, true, false, MPC_CURRENT_VALUES, cachingModePage);
    if (ret == SUCCESS)
    {
        //set up the mode parameter header
        //mode data length
        cachingModePage[0] = M_Byte1(MP_CACHING_LEN + 6);
        cachingModePage[1] = M_Byte0(MP_CACHING_LEN + 6);
        //medium type
        cachingModePage[2] = 0;
        //device specific
        cachingModePage[3] = 0;
        //reserved and LongLBA bit
        cachingModePage[4] = RESERVED;
        //reserved
        cachingModePage[5] = RESERVED;
        //block desciptor length
        cachingModePage[6] = 0;
        cachingModePage[7] = 0;
        //now go change the bit to what we need it to, then send a mode select command
        if (writeCacheEnableDisable == true)
        {
            cachingModePage[MODE_PARAMETER_HEADER_10_LEN + 2] |= BIT2;
        }
        else
        {
            //turn the bit off if it is already set
            if (cachingModePage[MODE_PARAMETER_HEADER_10_LEN + 2] & BIT2)
            {
                cachingModePage[MODE_PARAMETER_HEADER_10_LEN + 2] ^= BIT2;
            }
        }
        //send the mode select command
        ret = scsi_Mode_Select_10(device, MP_CACHING_LEN + MODE_PARAMETER_HEADER_10_LEN, true, true, false, cachingModePage, MP_CACHING_LEN + MODE_PARAMETER_HEADER_10_LEN);
    }
    safe_Free_aligned(cachingModePage)
    return ret;
}

int ata_Set_Write_Cache(tDevice *device, bool writeCacheEnableDisable)
{
    int ret = UNKNOWN;
    //on ata, we just send a set features command to change this
    if (writeCacheEnableDisable == true)
    {
        ret = ata_Set_Features(device, SF_ENABLE_VOLITILE_WRITE_CACHE, 0, 0, 0, 0);
    }
    else
    {
        ret = ata_Set_Features(device, SF_DISABLE_VOLITILE_WRITE_CACHE, 0, 0, 0, 0);
    }
    return ret;
}

#if !defined(DISABLE_NVME_PASSTHROUGH)
int nvme_Set_Write_Cache(tDevice *device, bool writeCacheEnableDisable)
{
    int ret = NOT_SUPPORTED;
    if (device->drive_info.IdentifyData.nvme.ctrl.vwc & BIT0)//This bit must be set to 1 to control whether write caching is enabled or disabled.
    {
        nvmeFeaturesCmdOpt featuresOptions;
        memset(&featuresOptions, 0, sizeof(nvmeFeaturesCmdOpt));
        if (writeCacheEnableDisable)
        {
            featuresOptions.featSetGetValue = BIT0;
        }
        featuresOptions.fid = NVME_FEAT_VOLATILE_WC_;
        //featuresOptions.sv //TODO: take extra "volatile" parameter? The drive may or may not support saving this accross power cycles (see spec). - TJE
        ret = nvme_Set_Features(device, &featuresOptions);
    }
    return ret;
}
#endif

int set_Write_Cache(tDevice *device, bool writeCacheEnableDisable)
{
    int ret = UNKNOWN;
    switch (device->drive_info.drive_type)
    {
    case NVME_DRIVE:
#if !defined (DISABLE_NVME_PASSTHROUGH)
        ret = nvme_Set_Write_Cache(device, writeCacheEnableDisable);
        break;
#endif
    case SCSI_DRIVE:
        ret = scsi_Set_Write_Cache(device, writeCacheEnableDisable);
        break;
    case ATA_DRIVE:
        ret = ata_Set_Write_Cache(device, writeCacheEnableDisable);
        break;
    default:
        ret = NOT_SUPPORTED;
    }
    return ret;
}

bool is_Read_Look_Ahead_Supported(tDevice *device)
{
    if (device->drive_info.drive_type == SCSI_DRIVE)
    {
        return scsi_Is_Read_Look_Ahead_Supported(device);
    }
    else if (device->drive_info.drive_type == ATA_DRIVE)
    {
        return ata_Is_Read_Look_Ahead_Supported(device);
    }
    return false;
}
//TODO: this uses the RCD bit. Old drives don't have this. Do something to detect this on legacy products later
bool scsi_Is_Read_Look_Ahead_Supported(tDevice *device)
{
    bool supported = false;
    //on SAS we change this through a mode page
    uint8_t *cachingModePage = C_CAST(uint8_t*, calloc_aligned(MP_CACHING_LEN + MODE_PARAMETER_HEADER_10_LEN, sizeof(uint8_t), device->os_info.minimumAlignment));
    if (cachingModePage == NULL)
    {
        perror("calloc failure!");
        return false;
    }
    //if changable, then it is supported
    if (SUCCESS == scsi_Mode_Sense_10(device, MP_CACHING, MP_CACHING_LEN + MODE_PARAMETER_HEADER_10_LEN, 0, true, false, MPC_CHANGABLE_VALUES, cachingModePage))
    {
        //check the offset to see if the bit is set.
        if (cachingModePage[MODE_PARAMETER_HEADER_10_LEN + 12] & BIT5)
        {
            supported = true;
        }
    }
    memset(cachingModePage, 0, MP_CACHING_LEN + MODE_PARAMETER_HEADER_10_LEN);
    //check default to see if it is enabled and just cannot be disabled (unlikely)
    if (!supported && SUCCESS == scsi_Mode_Sense_10(device, MP_CACHING, MP_CACHING_LEN + MODE_PARAMETER_HEADER_10_LEN, 0, true, false, MPC_DEFAULT_VALUES, cachingModePage))
    {
        //check the offset to see if the bit is set.
        if (!(cachingModePage[MODE_PARAMETER_HEADER_10_LEN + 12] & BIT5))
        {
            supported = true;//if it is enabled by default, then it's supported
        }
    }
    safe_Free_aligned(cachingModePage)
    return supported;
}

bool ata_Is_Read_Look_Ahead_Supported(tDevice *device)
{
    bool supported = false;
    if (device->drive_info.IdentifyData.ata.Word082 & BIT6)
    {
        supported = true;
    }
    return supported;
}

bool is_NV_Cache_Enabled(tDevice *device)
{
    if (device->drive_info.drive_type == SCSI_DRIVE)
    {
        return !scsi_is_NV_DIS_Bit_Set(device);//since this function returns when the bit is set to 1 (meaning cache disabled), then we need to flip that bit for this use.
    }
    //Not sure if ATA or NVMe support this. 
    return false;
}

bool is_Read_Look_Ahead_Enabled(tDevice *device)
{
    if (device->drive_info.drive_type == SCSI_DRIVE)
    {
        return scsi_Is_Read_Look_Ahead_Enabled(device);
    }
    else if (device->drive_info.drive_type == ATA_DRIVE)
    {
        return ata_Is_Read_Look_Ahead_Enabled(device);
    }
    return false;
}

//TODO: SPC3 added this page, but the NV_DIS bit is on the caching mode page.
//      We may want to add extra logic to see if the NV_DIS bit is set to 1 on the caching mode page.
bool scsi_Is_NV_Cache_Supported(tDevice *device)
{
    bool supported = false;
    //check the extended inquiry data for the NV_SUP bit
    uint8_t extInq[VPD_EXTENDED_INQUIRY_LEN] = { 0 };
    if (SUCCESS == scsi_Inquiry(device, extInq, VPD_EXTENDED_INQUIRY_LEN, EXTENDED_INQUIRY_DATA, true, false))
    {
        if (extInq[6] & BIT1)
        {
            supported = true;
        }
    }
    return supported;
}

bool is_NV_Cache_Supported(tDevice *device)
{
    if (device->drive_info.drive_type == SCSI_DRIVE)
    {
        return scsi_Is_NV_Cache_Supported(device);
    }
    return false;
}

bool scsi_is_NV_DIS_Bit_Set(tDevice *device)
{
    bool enabled = false;
    //on SAS we change this through a mode page
    uint8_t *cachingModePage = C_CAST(uint8_t*, calloc_aligned(MP_CACHING_LEN + MODE_PARAMETER_HEADER_10_LEN, sizeof(uint8_t), device->os_info.minimumAlignment));
    if (cachingModePage == NULL)
    {
        perror("calloc failure!");
        return false;
    }
    //first read the current settings
    if(SUCCESS == scsi_Mode_Sense_10(device, MP_CACHING, MP_CACHING_LEN + MODE_PARAMETER_HEADER_10_LEN, 0, true, false, MPC_CURRENT_VALUES, cachingModePage))
    {
        //check the offset to see if the bit is set.
        if (cachingModePage[MODE_PARAMETER_HEADER_10_LEN + 12] & BIT0)
        {
            //This means that the NV cache is disabled when this bit is set to 1
            enabled = true;
        }
        else
        {
            enabled = false;
        }
    }
    safe_Free_aligned(cachingModePage)
    return enabled;
}

bool scsi_Is_Read_Look_Ahead_Enabled(tDevice *device)
{
    bool enabled = false;
    //on SAS we change this through a mode page
    uint8_t *cachingModePage = C_CAST(uint8_t*, calloc_aligned(MP_CACHING_LEN + MODE_PARAMETER_HEADER_10_LEN, sizeof(uint8_t), device->os_info.minimumAlignment));
    if (cachingModePage == NULL)
    {
        perror("calloc failure!");
        return false;
    }
    //first read the current settings
    if(SUCCESS == scsi_Mode_Sense_10(device, MP_CACHING, MP_CACHING_LEN + MODE_PARAMETER_HEADER_10_LEN, 0, true, false, MPC_CURRENT_VALUES, cachingModePage))
    {
        //check the offset to see if the bit is set.
        if (cachingModePage[MODE_PARAMETER_HEADER_10_LEN + 12] & BIT5)
        {
            enabled = false;
        }
        else
        {
            enabled = true;
        }
    }
    safe_Free_aligned(cachingModePage)
    return enabled;
}

bool ata_Is_Read_Look_Ahead_Enabled(tDevice *device)
{
    bool enabled = false;
    if (device->drive_info.IdentifyData.ata.Word085 & BIT6)
    {
        enabled = true;
    }
    return enabled;
}

#if !defined (DISABLE_NVME_PASSTHROUGH)
bool nvme_Is_Write_Cache_Supported(tDevice *device)
{
    bool supported = false;
    if (device->drive_info.IdentifyData.nvme.ctrl.vwc & BIT0)//This bit must be set to 1 to control whether write caching is enabled or disabled.
    {
        supported = true;
    }
    return supported;
}
#endif

bool is_Write_Cache_Supported(tDevice *device)
{
    switch (device->drive_info.drive_type)
    {
    case NVME_DRIVE:
#if !defined (DISABLE_NVME_PASSTHROUGH)
        return nvme_Is_Write_Cache_Supported(device);
#endif
    case SCSI_DRIVE:
        return scsi_Is_Write_Cache_Supported(device);
    case ATA_DRIVE:
        return ata_Is_Write_Cache_Supported(device);
    default:
        break;
    }
    return false;
}

bool scsi_Is_Write_Cache_Supported(tDevice *device)
{
    bool supported = false;
    //on SAS we change this through a mode page
    uint8_t *cachingModePage = C_CAST(uint8_t*, calloc_aligned(MP_CACHING_LEN + MODE_PARAMETER_HEADER_10_LEN, sizeof(uint8_t), device->os_info.minimumAlignment));
    if (cachingModePage == NULL)
    {
        perror("calloc failure!");
        return false;
    }
    //if changable, then it is supported
    if (SUCCESS == scsi_Mode_Sense_10(device, MP_CACHING, MP_CACHING_LEN + MODE_PARAMETER_HEADER_10_LEN, 0, true, false, MPC_CHANGABLE_VALUES, cachingModePage))
    {
        //check the offset to see if the bit is set.
        if (cachingModePage[MODE_PARAMETER_HEADER_10_LEN + 2] & BIT2)
        {
            supported = true;
        }
    }
    memset(cachingModePage, 0, MP_CACHING_LEN + MODE_PARAMETER_HEADER_10_LEN);
    //check default to see if it is enabled and just cannot be disabled (unlikely)
    if (!supported && SUCCESS == scsi_Mode_Sense_10(device, MP_CACHING, MP_CACHING_LEN + MODE_PARAMETER_HEADER_10_LEN, 0, true, false, MPC_DEFAULT_VALUES, cachingModePage))
    {
        //check the offset to see if the bit is set.
        if (cachingModePage[MODE_PARAMETER_HEADER_10_LEN + 2] & BIT2)
        {
            supported = true;//if it is enabled by default, then it's supported
        }
    }
    safe_Free_aligned(cachingModePage)
    return supported;
}

bool ata_Is_Write_Cache_Supported(tDevice *device)
{
    bool supported = false;
    if (device->drive_info.IdentifyData.ata.Word082 & BIT5)
    {
        supported = true;
    }
    return supported;
}

#if !defined (DISABLE_NVME_PASSTHROUGH)
bool nvme_Is_Write_Cache_Enabled(tDevice *device)
{
    bool enabled = false;
    if (device->drive_info.IdentifyData.nvme.ctrl.vwc & BIT0)//This bit must be set to 1 to control whether write caching is enabled or disabled.
    {
        //get the feature identifier
        nvmeFeaturesCmdOpt featuresOptions;
        memset(&featuresOptions, 0, sizeof(nvmeFeaturesCmdOpt));
        featuresOptions.fid = NVME_FEAT_VOLATILE_WC_;
        featuresOptions.sel = 0;//getting current settings
        if (SUCCESS == nvme_Get_Features(device, &featuresOptions))
        {
            enabled = featuresOptions.featSetGetValue & BIT0;
        }
    }
    return enabled;
}
#endif

bool is_Write_Cache_Enabled(tDevice *device)
{
    switch (device->drive_info.drive_type)
    {
    case NVME_DRIVE:
#if !defined (DISABLE_NVME_PASSTHROUGH)
        return nvme_Is_Write_Cache_Enabled(device);
#endif
    case SCSI_DRIVE:
        return scsi_Is_Write_Cache_Enabled(device);
    case ATA_DRIVE:
        return ata_Is_Write_Cache_Enabled(device);
    default:
        break;
    }
    return false;
}

bool scsi_Is_Write_Cache_Enabled(tDevice *device)
{
    bool enabled = false;
    //on SAS we change this through a mode page
    uint8_t *cachingModePage = C_CAST(uint8_t*, calloc_aligned(MP_CACHING_LEN + MODE_PARAMETER_HEADER_10_LEN, sizeof(uint8_t), device->os_info.minimumAlignment));
    if (cachingModePage == NULL)
    {
        perror("calloc failure!");
        return false;
    }
    //first read the current settings
    if (SUCCESS == scsi_Mode_Sense_10(device, MP_CACHING, MP_CACHING_LEN + MODE_PARAMETER_HEADER_10_LEN, 0, true, false, MPC_CURRENT_VALUES, cachingModePage))
    {
        //check the offset to see if the bit is set.
        if (cachingModePage[MODE_PARAMETER_HEADER_10_LEN + 2] & BIT2)
        {
            enabled = true;
        }
        else
        {
            enabled = false;
        }
    }
    safe_Free_aligned(cachingModePage)
    return enabled;
}

bool ata_Is_Write_Cache_Enabled(tDevice *device)
{
    bool enabled = false;
    if (device->drive_info.IdentifyData.ata.Word085 & BIT5)
    {
        enabled = true;
    }
    return enabled;
}

//erase weights are hard coded right now....-TJE
int get_Supported_Erase_Methods(tDevice *device, eraseMethod const eraseMethodList[MAX_SUPPORTED_ERASE_METHODS], uint32_t *overwriteEraseTimeEstimateMinutes)
{
    int ret = SUCCESS;
    ataSecurityStatus ataSecurityInfo;
    sanitizeFeaturesSupported sanitizeInfo;
    uint64_t maxNumberOfLogicalBlocksPerCommand = 0;
    bool formatUnitAdded = false;
    bool isWriteSameSupported = is_Write_Same_Supported(device, 0, C_CAST(uint32_t, device->drive_info.deviceMaxLba), &maxNumberOfLogicalBlocksPerCommand);
    bool isFormatUnitSupported = is_Format_Unit_Supported(device, NULL);
    eraseMethod * currentErase = C_CAST(eraseMethod*, eraseMethodList);
    if (!currentErase)
    {
        return BAD_PARAMETER;
    }
    if (overwriteEraseTimeEstimateMinutes)
    {
        *overwriteEraseTimeEstimateMinutes = 0;//start off with zero
    }
    memset(&sanitizeInfo, 0, sizeof(sanitizeFeaturesSupported));
    memset(&ataSecurityInfo, 0, sizeof(ataSecurityStatus));
    //first make sure the list is initialized to all 1's (to help sorting later)
    memset(currentErase, 0xFF, sizeof(eraseMethod) * MAX_SUPPORTED_ERASE_METHODS);

    get_Sanitize_Device_Features(device, &sanitizeInfo);

    get_ATA_Security_Info(device, &ataSecurityInfo, sat_ATA_Security_Protocol_Supported(device));

    //fastest will be sanitize crypto
    if (sanitizeInfo.crypto)
    {
        currentErase->eraseIdentifier = ERASE_SANITIZE_CRYPTO;
        snprintf(currentErase->eraseName, MAX_ERASE_NAME_LENGTH, "Sanitize Crypto Erase");
        snprintf(currentErase->eraseWarning, MAX_ERASE_WARNING_LENGTH, "Cannot be stopped, even with a power cycle.");
        currentErase->warningValid = true;
        currentErase->eraseWeight = 0;
        ++currentErase;
    }

    //next sanitize block erase
    if (sanitizeInfo.blockErase)
    {
        currentErase->eraseIdentifier = ERASE_SANITIZE_BLOCK;
        snprintf(currentErase->eraseName, MAX_ERASE_NAME_LENGTH, "Sanitize Block Erase");
        snprintf(currentErase->eraseWarning, MAX_ERASE_WARNING_LENGTH, "Cannot be stopped, even with a power cycle.");
        currentErase->warningValid = true;
        currentErase->eraseWeight = 1;
        ++currentErase;
    }

    //format on SAS SSD will take only a couple seconds since it basically does a unmap operation
    if (isFormatUnitSupported && device->drive_info.drive_type == SCSI_DRIVE && !formatUnitAdded && is_SSD(device))
    {
        currentErase->eraseIdentifier = ERASE_FORMAT_UNIT;
        snprintf(currentErase->eraseName, MAX_ERASE_NAME_LENGTH, "Format Unit");
        snprintf(currentErase->eraseWarning, MAX_ERASE_WARNING_LENGTH, "If interrupted, must be restarted from the beginning.");
        currentErase->warningValid = true;
        currentErase->eraseWeight = 2;
        ++currentErase;
        formatUnitAdded = true;
    }

    //trim/unmap/deallocate are not allowed since they are "hints" rather than guaranteed erasure.

    //This weight value is reserved for TCG revert (this is placed in another library)

    bool enhancedEraseAddedToList = false;
    //maybe enhanced ata security erase (check time...if it's set to 2 minutes, then that is the lowest possible time and likely a TCG drive doing a crypto erase)
    if (ataSecurityInfo.enhancedEraseSupported && ataSecurityInfo.enhancedSecurityEraseUnitTimeMinutes == 2 && !ataSecurityInfo.securityEnabled)
    {
        enhancedEraseAddedToList = true;
        currentErase->eraseIdentifier = ERASE_ATA_SECURITY_ENHANCED;
        snprintf(currentErase->eraseName, MAX_ERASE_NAME_LENGTH, "ATA Enhanced Security Erase");
        snprintf(currentErase->eraseWarning, MAX_ERASE_WARNING_LENGTH, "Requires setting device password. Password cleared upon success.");
        currentErase->warningValid = true;
        currentErase->eraseWeight = 5;
        ++currentErase;
    }

    //this weight value is reserved for TCG revertSP (this is placed in another library)

    //sanitize overwrite
    if (sanitizeInfo.overwrite)
    {
        currentErase->eraseIdentifier = ERASE_SANITIZE_OVERWRITE;
        snprintf(currentErase->eraseName, MAX_ERASE_NAME_LENGTH, "Sanitize Overwrite Erase");
        snprintf(currentErase->eraseWarning, MAX_ERASE_WARNING_LENGTH, "Cannot be stopped, even with a power cycle.");
        currentErase->warningValid = true;
        currentErase->eraseWeight = 7;
        ++currentErase;
    }

    //format unit (I put this above write same since on SAS, we cannot get progress indication from write same)
    if (isFormatUnitSupported && device->drive_info.drive_type == SCSI_DRIVE && !formatUnitAdded)
    {
        currentErase->eraseIdentifier = ERASE_FORMAT_UNIT;
        snprintf(currentErase->eraseName, MAX_ERASE_NAME_LENGTH, "Format Unit");
        snprintf(currentErase->eraseWarning, MAX_ERASE_WARNING_LENGTH, "If interupted, must be restarted from the beginning.");
        currentErase->warningValid = true;
        currentErase->eraseWeight = 8;
        ++currentErase;
        formatUnitAdded = true;
    }

    //write same
    if (isWriteSameSupported)
    {
        currentErase->eraseIdentifier = ERASE_WRITE_SAME;
        snprintf(currentErase->eraseName, MAX_ERASE_NAME_LENGTH, "Write Same Erase");
        snprintf(currentErase->eraseWarning, MAX_ERASE_WARNING_LENGTH, "Host may abort erase with disc access.");
        currentErase->warningValid = true;
        currentErase->eraseWeight = 9;
        ++currentErase;
    }

    //ata security - normal &| enhanced (check times)
    if (!ataSecurityInfo.securityEnabled && ataSecurityInfo.securitySupported)
    {
        if (!enhancedEraseAddedToList && ataSecurityInfo.enhancedEraseSupported && ataSecurityInfo.enhancedSecurityEraseUnitTimeMinutes <= ataSecurityInfo.securityEraseUnitTimeMinutes)
        {
            enhancedEraseAddedToList = true;
            //add enhanced erase
            enhancedEraseAddedToList = true;
            currentErase->eraseIdentifier = ERASE_ATA_SECURITY_ENHANCED;
            snprintf(currentErase->eraseName, MAX_ERASE_NAME_LENGTH, "ATA Enhanced Security Erase");
            snprintf(currentErase->eraseWarning, MAX_ERASE_WARNING_LENGTH, "Requires setting device password. Password cleared upon success.");
            currentErase->warningValid = true;
            currentErase->eraseWeight = 10;
            ++currentErase;
        }
        //add normal erase
        currentErase->eraseIdentifier = ERASE_ATA_SECURITY_NORMAL;
        snprintf(currentErase->eraseName, MAX_ERASE_NAME_LENGTH, "ATA Security Erase");
        snprintf(currentErase->eraseWarning, MAX_ERASE_WARNING_LENGTH, "Requires setting device password. Password cleared upon success.");
        currentErase->warningValid = true;
        currentErase->eraseWeight = 11;
        ++currentErase;

        //if enhanced erase has not been added, but is supported, add it here (since it's longer than short)
        if (!enhancedEraseAddedToList && ataSecurityInfo.enhancedEraseSupported && ataSecurityInfo.enhancedSecurityEraseUnitTimeMinutes >= ataSecurityInfo.securityEraseUnitTimeMinutes)
        {
            enhancedEraseAddedToList = true;
            //add enhanced erase
            enhancedEraseAddedToList = true;
            currentErase->eraseIdentifier = ERASE_ATA_SECURITY_ENHANCED;
            snprintf(currentErase->eraseName, MAX_ERASE_NAME_LENGTH, "ATA Enhanced Security Erase");
            snprintf(currentErase->eraseWarning, MAX_ERASE_WARNING_LENGTH, "Requires setting device password. Password cleared upon success.");
            currentErase->warningValid = true;
            currentErase->eraseWeight = 12;
            ++currentErase;
        }

        //save the time estimate from NORMAL ATA Security erase if it isn't all F's
        //Using normal for the estimate since it will always be an overwrite of the disk (no crypto) and because we cannot access reassigned sectors during a host overwrite, so this is the closest we'll get.-TJE
        if (overwriteEraseTimeEstimateMinutes && *overwriteEraseTimeEstimateMinutes == 0 && ataSecurityInfo.securitySupported)
        {
            if (ataSecurityInfo.securityEraseUnitTimeMinutes != UINT16_MAX)
            {
                *overwriteEraseTimeEstimateMinutes = ataSecurityInfo.securityEraseUnitTimeMinutes;
            }
        }
    }

    //overwrite (always available and always the slowest)
    currentErase->eraseIdentifier = ERASE_OVERWRITE;
    snprintf(currentErase->eraseName, MAX_ERASE_NAME_LENGTH, "Overwrite Erase");
    //snprintf(currentErase->eraseWarning, MAX_ERASE_WARNING_LENGTH, "");
    currentErase->warningValid = false;
    currentErase->eraseWeight = 13;
    ++currentErase;

    if (overwriteEraseTimeEstimateMinutes)//make sure the incoming value is zero in case time was set by something above here (like ata security erase)
    {
        uint8_t hours = 0, minutes = 0;
        //let's set a time estimate!
        //base it off of the long DST time...as that is probably the closest match we'll get since that does access every LBA
        get_Long_DST_Time(device, &hours, &minutes);
        uint32_t longDSTTimeMinutes = C_CAST(uint32_t, (C_CAST(uint32_t, hours) * UINT32_C(60)) + C_CAST(uint32_t, minutes));
        *overwriteEraseTimeEstimateMinutes = M_Max(*overwriteEraseTimeEstimateMinutes, longDSTTimeMinutes);
        if (*overwriteEraseTimeEstimateMinutes == 0)
        {
            //This drive doesn't support anything that gives us time estimates, so let's make a guess.
            //TODO: Make this guess better by reading the drive capabilities and interface speed to determine a more accurate estimate.
            uint32_t megabytesPerSecond = is_SSD(device) ? 450 : 150;//assume 450 MB/s on SSD and 150 MB/s on HDD
            *overwriteEraseTimeEstimateMinutes = C_CAST(uint32_t, ((device->drive_info.deviceMaxLba * device->drive_info.deviceBlockSize) / (megabytesPerSecond * 1.049e+6)) / 60);
        }
    }

    return ret;
}

void print_Supported_Erase_Methods(tDevice *device, eraseMethod const eraseMethodList[MAX_SUPPORTED_ERASE_METHODS], uint32_t *overwriteEraseTimeEstimateMinutes)
{
    uint8_t counter = 0;
    bool cryptoSupported = false;
    bool sanitizeBlockEraseSupported = false;
    M_USE_UNUSED(device);
    printf("Erase Methods supported by this drive (listed fastest to slowest):\n");
    while (counter < MAX_SUPPORTED_ERASE_METHODS)
    {
        switch (eraseMethodList[counter].eraseIdentifier)
        {
        case ERASE_MAX_VALUE:
            ++counter;
            continue;
        case ERASE_SANITIZE_CRYPTO:
        case ERASE_TCG_REVERT_SP:
        case ERASE_TCG_REVERT:
            cryptoSupported = true;
            break;
        case ERASE_SANITIZE_BLOCK:
            sanitizeBlockEraseSupported = true;
            break;
        default:
            break;
        }
        if (eraseMethodList[counter].warningValid)
        {
            printf("%2"PRIu8" %-*s\n\tNOTE: %-*s\n", counter + 1, MAX_ERASE_NAME_LENGTH, eraseMethodList[counter].eraseName, MAX_ERASE_WARNING_LENGTH, eraseMethodList[counter].eraseWarning);
        }
        else
        {
            printf("%2"PRIu8" %-*s\n\n", counter + 1, MAX_ERASE_NAME_LENGTH, eraseMethodList[counter].eraseName);
        }
        ++counter;
    }
    if (overwriteEraseTimeEstimateMinutes)
    {
        uint8_t days = 0, hours = 0, minutes = 0, seconds = 0;
        convert_Seconds_To_Displayable_Time(C_CAST(uint64_t, *overwriteEraseTimeEstimateMinutes * 60), NULL, &days, &hours, &minutes, &seconds);
        //Example output: 
        //The minimum time to overwrite erase this drive is approximately x days y hours z minutes. 
        //The actual time may take longer. Cryptographic erase completes in seconds. Trim/Unmap & blockerase should also complete in under a minute
        printf("The minimum time to overwrite erase this drive is approximately:\n\t");
        print_Time_To_Screen(NULL, &days, &hours, &minutes, &seconds);
        printf("\n");
        printf("The actual time to erase may take longer.\n");
        if (cryptoSupported)
        {
            printf("Cryptographic erase completes in seconds.\n");
        }
        if (sanitizeBlockEraseSupported)
        {
            printf("Blockerase should also complete in under a minute.\n");
        }
        printf("\n");
    }
    return;
}

int enable_Disable_PUIS_Feature(tDevice *device, bool enable)
{
    int ret = NOT_SUPPORTED;
    if(device->drive_info.drive_type == ATA_DRIVE)
    {
    
        //check the identify bits to make sure PUIS is supported.
        if(device->drive_info.IdentifyData.ata.Word083 & BIT5)
        {
            if(enable)
            {
                ret = ata_Set_Features(device, SF_ENABLE_PUIS_FEATURE, 0, 0, 0, 0);
            }
            else
            {
                ret = ata_Set_Features(device, SF_DISABLE_PUIS_FEATURE, 0, 0, 0, 0);
            }
        }
    }
    return ret;
}

int set_Sense_Data_Format(tDevice *device, bool defaultSetting, bool descriptorFormat, bool saveParameters)
{
    int ret = NOT_SUPPORTED;
    //Change D_Sense for Control Mode page
    uint8_t controlModePage[MODE_PARAMETER_HEADER_10_LEN + 12] = { 0 };
    bool mode6ByteCmd = false;
    if (SUCCESS == scsi_Mode_Sense_10(device, MP_CONTROL, MODE_PARAMETER_HEADER_10_LEN + 12, 0, true, false, MPC_CURRENT_VALUES, controlModePage))
    {
        mode6ByteCmd = false;
    }
    else if (SUCCESS == scsi_Mode_Sense_6(device, MP_CONTROL, MODE_PARAMETER_HEADER_6_LEN + 12, 0, true, MPC_CURRENT_VALUES, controlModePage))
    {
        mode6ByteCmd = true;
    }
    else
    {
        return NOT_SUPPORTED;
    }
    //Should there be an interface check here? We should allow this anywhere since SAT might be being used and would also be affected by this bit
    if (defaultSetting)
    {
        //read the default setting for this bit
        uint8_t controlModePageDefaults[MODE_PARAMETER_HEADER_10_LEN + 12] = { 0 };
        if (mode6ByteCmd && SUCCESS == scsi_Mode_Sense_6(device, MP_CONTROL, MODE_PARAMETER_HEADER_6_LEN + 12, 0, true, MPC_DEFAULT_VALUES, controlModePageDefaults))
        {
            //figure out what D_Sense is set to, then change it in the current settings
            if (controlModePage[MODE_PARAMETER_HEADER_6_LEN + 2] & BIT2)
            {
                descriptorFormat = true;
            }
            else
            {
                descriptorFormat = false;
            }
        }
        else if (!mode6ByteCmd && SUCCESS == scsi_Mode_Sense_10(device, MP_CONTROL, MODE_PARAMETER_HEADER_10_LEN + 12, 0, true, false, MPC_DEFAULT_VALUES, controlModePageDefaults))
        {
            if (controlModePage[MODE_PARAMETER_HEADER_10_LEN + 2] & BIT2)
            {
                descriptorFormat = true;
            }
            else
            {
                descriptorFormat = false;
            }
        }
        else
        {
            return NOT_SUPPORTED;
        }
    }
    uint8_t byteOffset = 0;
    if (mode6ByteCmd)
    {
        byteOffset = MODE_PARAMETER_HEADER_6_LEN + 2;
    }
    else
    {
        byteOffset = MODE_PARAMETER_HEADER_10_LEN + 2;
    }
    if (descriptorFormat)
    {
        M_SET_BIT(controlModePage[byteOffset], 2);
    }
    else
    {
        M_CLEAR_BIT(controlModePage[byteOffset], 2);
    }
    //write the change to the drive
    if (mode6ByteCmd)
    {
        ret = scsi_Mode_Select_6(device, MODE_PARAMETER_HEADER_6_LEN + 12, true, saveParameters, false, controlModePage, MODE_PARAMETER_HEADER_6_LEN + 12);
    }
    else
    {
        ret = scsi_Mode_Select_10(device, MODE_PARAMETER_HEADER_10_LEN + 12, true, saveParameters, false, controlModePage, MODE_PARAMETER_HEADER_10_LEN + 12);
    }
    return ret;
}

int get_Current_Free_Fall_Control_Sensitivity(tDevice * device, uint16_t *sensitivity)
{
    int ret = NOT_SUPPORTED;
    if (!sensitivity)
    {
        return BAD_PARAMETER;
    }
    if (device->drive_info.drive_type == ATA_DRIVE)
    {
        if (device->drive_info.IdentifyData.ata.Word119 & BIT5)//supported
        {
            *sensitivity = UINT16_MAX;//this can be used to filter out invalid value, a.k.a. feature is not enabled, but is supported.
            if (device->drive_info.IdentifyData.ata.Word120 & BIT5)//enabled
            {
                //Word 53, bits 15:8
                *sensitivity = M_Byte1(device->drive_info.IdentifyData.ata.Word053);
            }
        }
    }
    return ret;
}

int set_Free_Fall_Control_Sensitivity(tDevice *device, uint8_t sensitivity)
{
    int ret = NOT_SUPPORTED;
    if (device->drive_info.drive_type == ATA_DRIVE)
    {
        if (device->drive_info.IdentifyData.ata.Word119 & BIT5)//supported
        {
            ret = ata_Set_Features(device, SF_ENABLE_FREE_FALL_CONTROL_FEATURE, sensitivity, 0, 0, 0);
        }
    }
    return ret;
}

int disable_Free_Fall_Control_Feature(tDevice *device)
{
    int ret = NOT_SUPPORTED;
    if (device->drive_info.drive_type == ATA_DRIVE)
    {
        if (device->drive_info.IdentifyData.ata.Word119 & BIT5)//supported //TODO: Check if it's enabled first as well? Do this if this command is aborting when already disabled, otherwise this should be ok
        {
            ret = ata_Set_Features(device, SF_DISABLE_FREE_FALL_CONTROL_FEATURE, 0, 0, 0, 0);
        }
    }
    return ret;
}

void show_Test_Unit_Ready_Status(tDevice *device)
{
    scsiStatus returnedStatus;
	memset(&returnedStatus, 0, sizeof(scsiStatus));
    int ret = scsi_Test_Unit_Ready(device, &returnedStatus);
    if ((ret == SUCCESS) && (returnedStatus.senseKey == SENSE_KEY_NO_ERROR))
    {
        printf("READY\n");
    }
    else
    {
        eVerbosityLevels tempVerbosity = device->deviceVerbosity;
        printf("NOT READY\n");
        device->deviceVerbosity = VERBOSITY_COMMAND_NAMES;//the function below will print out a sense data translation, but only it we are at this verbosity or higher which is why it's set before this call.
        check_Sense_Key_ASC_ASCQ_And_FRU(device, returnedStatus.senseKey, returnedStatus.asc, returnedStatus.ascq, returnedStatus.fru);
        device->deviceVerbosity = tempVerbosity;//restore it back to what it was now that this is done.
    }
}

int enable_Disable_AAM_Feature(tDevice *device, bool enable)
{
    int ret = NOT_SUPPORTED;
    if (device->drive_info.drive_type == ATA_DRIVE)
    {
        //check the identify bits to make sure APM is supported.
        if (device->drive_info.IdentifyData.ata.Word083 & BIT9)
        {
            if (enable)
            {
                //set value to the vendor recommended value reported in identify data when requesting an enable operation
                //TODO: Should we set max performance instead by default?
                ret = ata_Set_Features(device, SF_ENABLE_AUTOMATIC_ACOUSTIC_MANAGEMENT_FEATURE, M_Byte1(device->drive_info.IdentifyData.ata.Word094), 0, 0, 0);
            }
            else
            {
                //subcommand C2
                ret = ata_Set_Features(device, SF_DISABLE_AUTOMATIC_ACOUSTIC_MANAGEMENT, 0, 0, 0, 0);
                if (ret != SUCCESS)
                {
                    //the disable AAM feature is not available on all devices according to ATA spec.
                    ret = NOT_SUPPORTED;
                }
            }
        }
    }
    return ret;
}
//AAM Levels:
// 0 - vendor specific
// 1-7Fh = These are labelled as "Retired" in every spec I can find, so no idea what these even mean. - TJE
// 80h = minimum acoustic emanation
// 81h - FDh = intermediate acoustic management levels
// FEh = maximum performance.
int set_AAM_Level(tDevice *device, uint8_t apmLevel)
{
    int ret = NOT_SUPPORTED;
    if (device->drive_info.drive_type == ATA_DRIVE)
    {
        //check the identify bits to make sure APM is supported.
        if (device->drive_info.IdentifyData.ata.Word083 & BIT9)
        {
            //subcommand 42 with the aamLevel in the count field
            ret = ata_Set_Features(device, SF_ENABLE_AUTOMATIC_ACOUSTIC_MANAGEMENT_FEATURE, apmLevel, 0, 0, 0);
        }
    }
    return ret;
}

int get_AAM_Level(tDevice *device, uint8_t *aamLevel)
{
    int ret = NOT_SUPPORTED;
    if (device->drive_info.drive_type == ATA_DRIVE)
    {
        //check the identify bits to make sure AAM is supported.
        if (device->drive_info.IdentifyData.ata.Word083 & BIT9)//word 86 says "enabled". We may or may not want to check for that.
        {
            //get it from identify device word 94
            ret = SUCCESS;
            *aamLevel = M_Byte0(device->drive_info.IdentifyData.ata.Word094);
        }
    }
    return ret;
}


bool scsi_MP_Reset_To_Defaults_Supported(tDevice *device)
{
    bool supported = false;
    if (device->drive_info.scsiVersion >= SCSI_VERSION_SCSI2)//VPD added in SCSI2
    {
        uint8_t extendedInquiryData[VPD_EXTENDED_INQUIRY_LEN] = { 0 };
        if (SUCCESS == scsi_Inquiry(device, extendedInquiryData, VPD_EXTENDED_INQUIRY_LEN, EXTENDED_INQUIRY_DATA, true, false))
        {
            if (extendedInquiryData[1] == EXTENDED_INQUIRY_DATA)
            {
                supported = extendedInquiryData[8] & BIT3;
            }
        }
    }
    return supported;
}


int scsi_Update_Mode_Page(tDevice *device, uint8_t modePage, uint8_t subpage, eSCSI_MP_UPDATE_MODE updateMode)
{
    int ret = NOT_SUPPORTED;
    uint32_t modePageLength = 0;
    eScsiModePageControl mpc = MPC_DEFAULT_VALUES;
    switch (updateMode)
    {
    case UPDATE_SCSI_MP_RESTORE_TO_SAVED:
        mpc = MPC_SAVED_VALUES;
        break;
    case UPDATE_SCSI_MP_SAVE_CURRENT:
        mpc = MPC_CURRENT_VALUES;
        break;
    case UPDATE_SCSI_MP_RESET_TO_DEFAULT:
    default:
        mpc = MPC_DEFAULT_VALUES;
        break;
    }
    if (modePage == MP_RETURN_ALL_PAGES || subpage == MP_SP_ALL_SUBPAGES)//if asking for all mode pages, all mode pages and subpages, or all subpages of a specific page, we need to handle it in here.
    {
        //if resetting all pages, check if the RTD bit is supported to simplify the process...-TJE
        if (mpc == MPC_DEFAULT_VALUES && modePage == MP_RETURN_ALL_PAGES && subpage == MP_SP_ALL_SUBPAGES && scsi_MP_Reset_To_Defaults_Supported(device))
        {
            //requesting to reset all mode pages. Send the mode select command with the RTD bit set.
            ret = scsi_Mode_Select_10(device, 0, true, true, true, NULL, 0);
            uint8_t senseKey = 0, asc = 0, ascq = 0, fru = 0;
            get_Sense_Key_ASC_ASCQ_FRU(device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, &senseKey, &asc, &ascq, &fru);
            if (senseKey == SENSE_KEY_ILLEGAL_REQUEST && asc == 0x20 && ascq == 0x00)//checking for invalid operation code
            {
                //retry with 6 byte command since 10 byte op code was not recognizd.
                ret = scsi_Mode_Select_6(device, 0, true, true, true, NULL, 0);
            }
        }
        else
        {
            if (SUCCESS == get_SCSI_Mode_Page_Size(device, MPC_CURRENT_VALUES, modePage, subpage, &modePageLength))
            {
                uint8_t *modeData = C_CAST(uint8_t*, calloc_aligned(modePageLength, sizeof(uint8_t), device->os_info.minimumAlignment));
                if (!modeData)
                {
                    return MEMORY_FAILURE;
                }
                //now read all the data
                bool used6ByteCmd = false;
                if (SUCCESS == get_SCSI_Mode_Page(device, mpc, modePage, subpage, NULL, NULL, true, modeData, modePageLength, NULL, &used6ByteCmd))
                {
                    //now we need to loop through each page, and send it to the drive as a new mode select command.
                    uint32_t offset = 0;
                    uint16_t blockDescriptorLength = 0;
                    if (!used6ByteCmd)
                    {
                        //got 10 byte command data
                        blockDescriptorLength = M_BytesTo2ByteValue(modeData[6], modeData[7]);
                        offset = MODE_PARAMETER_HEADER_10_LEN + blockDescriptorLength;
                    }
                    else
                    {
                        //got 6 byte command data.
                        blockDescriptorLength = modeData[3];
                        offset = MODE_PARAMETER_HEADER_6_LEN + blockDescriptorLength;
                    }
                    uint16_t currentPageLength = 0;
                    uint16_t counter = 0, failedModeSelects = 0;
                    for (; offset < modePageLength; offset += currentPageLength, ++counter)
                    {
                        uint8_t* currentPageToSet = NULL;
                        uint16_t currentPageToSetLength = used6ByteCmd ? MODE_PARAMETER_HEADER_6_LEN + blockDescriptorLength : MODE_PARAMETER_HEADER_10_LEN + blockDescriptorLength;
                        uint8_t currentPage = M_GETBITRANGE(modeData[offset + 0], 5, 0);
                        uint8_t currentSubPage = 0;
                        uint16_t currentPageOffset = 0;
                        if (modeData[offset] & BIT6)
                        {
                            //subpage format
                            currentSubPage = modeData[offset + 1];
                            currentPageLength = M_BytesTo2ByteValue(modeData[offset + 2], modeData[offset + 3]) + 4;//add 4 bytes for page code, subpage code, & page length bytes
                        }
                        else
                        {
                            currentPageLength = modeData[offset + 1] + 2;//add 2 bytes for the page code and page length bytes
                        }
                        currentPageToSetLength += currentPageLength;
                        currentPageToSet = C_CAST(uint8_t*, calloc_aligned(currentPageToSetLength, sizeof(uint8_t), device->os_info.minimumAlignment));
                        if (!currentPageToSet)
                        {
                            safe_Free_aligned(modeData)
                            return MEMORY_FAILURE;
                        }
                        if (used6ByteCmd)
                        {
                            //copy header and block descriptors (if any)
                            currentPageOffset = MODE_PARAMETER_HEADER_6_LEN + blockDescriptorLength;
                            memcpy(currentPageToSet, &modeData[0], MODE_PARAMETER_HEADER_6_LEN + blockDescriptorLength);
                            //now zero out the reserved bytes for the mode select command
                            currentPageToSet[0] = 0;//mode data length is reserved for mode select commands
                            //leave medium type alone
                            //leave device specific parameter alone???
                            //leave block descriptor length alone in case we got some.
                        }
                        else
                        {
                            //copy header and block descriptors (if any)
                            currentPageOffset = MODE_PARAMETER_HEADER_10_LEN + blockDescriptorLength;
                            memcpy(currentPageToSet, &modeData[0], MODE_PARAMETER_HEADER_10_LEN + blockDescriptorLength);
                            //now zero out the reserved bytes for the mode select command
                            currentPageToSet[0] = 0;//mode data length is reserved for mode select commands
                            currentPageToSet[1] = 0;
                            //leave medium type alone
                            //leave device specific parameter alone???
                            //leave block descriptor length alone in case we got some.
                        }
                        //now we need to copy the default data over now, then send it to the drive.
                        memcpy(&currentPageToSet[currentPageOffset], &modeData[offset], currentPageLength);
                        bool pageFormat = currentPage == 0 ? false : true;//set to false when reading vendor unique page zero
                        bool savable = modeData[offset + 0] & BIT7;// use this to save pages. This bit says whether the page/settings can be saved or not.
                        if (used6ByteCmd)
                        {
                            if (SUCCESS != scsi_Mode_Select_6(device, C_CAST(uint8_t, currentPageToSetLength), pageFormat, savable, false, currentPageToSet, currentPageToSetLength))
                            {
                                ++failedModeSelects;
                                printf("WARNING! Unable to reset page %" PRIX8 "h", currentPage);
                                if (currentSubPage != 0)
                                {
                                    printf(" - %" PRIX8 "h", currentSubPage);
                                }
                                else
                                {
                                    printf("\n");
                                }
                            }
                            else
                            {
                                ret = SUCCESS;
                            }
                        }
                        else
                        {
                            if (SUCCESS != scsi_Mode_Select_10(device, currentPageToSetLength, pageFormat, savable, false, currentPageToSet, currentPageToSetLength))
                            {
                                ++failedModeSelects;
                                printf("WARNING! Unable to reset page %" PRIX8 "h", currentPage);
                                if (currentSubPage != 0)
                                {
                                    printf(" - %" PRIX8 "h", currentSubPage);
                                }
                                else
                                {
                                    printf("\n");
                                }
                            }
                            else
                            {
                                ret = SUCCESS;
                            }
                        }
                        safe_Free_aligned(currentPageToSet)
                    }
                    if (counter > 0 && counter == failedModeSelects)
                    {
                        ret = FAILURE;
                    }
                }
                else
                {
                    ret = FAILURE;
                }
                safe_Free_aligned(modeData)
            }
            else
            {
                //mode page not supported most likely
            }
        }
    }
    else
    {
        //individual page...easy peasy
        if (SUCCESS == get_SCSI_Mode_Page_Size(device, MPC_CURRENT_VALUES, modePage, subpage, &modePageLength))
        {
            uint8_t *modeData = C_CAST(uint8_t*, calloc_aligned(modePageLength, sizeof(uint8_t), device->os_info.minimumAlignment));
            if (!modeData)
            {
                return MEMORY_FAILURE;
            }
            //now read all the data
            bool used6ByteCmd = false;
            if (SUCCESS == get_SCSI_Mode_Page(device, mpc, modePage, subpage, NULL, NULL, true, modeData, modePageLength, NULL, &used6ByteCmd))
            {
                uint16_t offset = 0;
                uint16_t blockDescriptorLength = 0;
                if (used6ByteCmd)
                {
                    blockDescriptorLength = modeData[3];
                    offset = MODE_PARAMETER_HEADER_6_LEN + blockDescriptorLength;
                    //now zero out the reserved bytes for the mode select command
                    modeData[0] = 0;//mode data length is reserved for mode select commands
                    //leave medium type alone
                    //leave device specific parameter alone???
                    //leave block descriptor length alone in case we got some.
                }
                else
                {
                    blockDescriptorLength = M_BytesTo2ByteValue(modeData[6], modeData[7]);
                    offset = MODE_PARAMETER_HEADER_10_LEN + blockDescriptorLength;
                    //now zero out the reserved bytes for the mode select command
                    modeData[0] = 0;//mode data length is reserved for mode select commands
                    modeData[1] = 0;
                    //leave medium type alone
                    //leave device specific parameter alone???
                    //leave block descriptor length alone in case we got some.
                }
                //now send the mode select command
                bool pageFormat = modePage == 0 ? false : true;//set to false when reading vendor unique page zero
                bool savable = modeData[offset + 0] & BIT7;// use this to save pages. This bit says whether the page/settings can be saved or not.
                if (used6ByteCmd)
                {
                    if (SUCCESS != scsi_Mode_Select_6(device, C_CAST(uint8_t, modePageLength), pageFormat, savable, false, modeData, modePageLength))
                    {
                        ret = FAILURE;
                    }
                    else
                    {
                        ret = SUCCESS;
                    }
                }
                else
                {
                    if (SUCCESS != scsi_Mode_Select_10(device, C_CAST(uint16_t, modePageLength), pageFormat, savable, false, modeData, modePageLength))
                    {
                        ret = FAILURE;
                    }
                    else
                    {
                        ret = SUCCESS;
                    }
                }
            }
            safe_Free_aligned(modeData)
        }
        else
        {
            //most likely not a supported page
        }
    }
    return ret;
}

//TODO: should we have another parameter to disable saving the page if they just want to make a temporary change?
//If this is done. Do we want to just send the command, or do we want to turn off saving if the page isn't savable?
//NOTE: This rely's on NOT having the mode page header in the passed in buffer, just the raw mode page itself!
int scsi_Set_Mode_Page(tDevice *device, uint8_t* modePageData, uint16_t modeDataLength, bool saveChanges)
{
    int ret = NOT_SUPPORTED;
    if (!modePageData || modeDataLength == 0)
    {
        return BAD_PARAMETER;
    }
    uint32_t modePageLength = 0;
    uint8_t modePage = M_GETBITRANGE(modePageData[0], 5, 0);
    uint8_t subpage = 0;
    if (modePageData[0] & BIT6)
    {
        //subpage format
        subpage = modePageData[1];
    }
    //even though we have the data we want to send, we must ALWAYS request the page first, then modify the data and send it back.
    if (SUCCESS == get_SCSI_Mode_Page_Size(device, MPC_CURRENT_VALUES, modePage, subpage, &modePageLength))
    {
        uint8_t *modeData = C_CAST(uint8_t*, calloc_aligned(modePageLength, sizeof(uint8_t), device->os_info.minimumAlignment));
        if (!modeData)
        {
            return MEMORY_FAILURE;
        }
        //now read all the data
        bool used6ByteCmd = false;
        if (SUCCESS == get_SCSI_Mode_Page(device, MPC_CURRENT_VALUES, modePage, subpage, NULL, NULL, true, modeData, modePageLength, NULL, &used6ByteCmd))
        {
            uint16_t offset = 0;
            uint16_t blockDescriptorLength = 0;
            if (used6ByteCmd)
            {
                blockDescriptorLength = modeData[3];
                offset = MODE_PARAMETER_HEADER_6_LEN + blockDescriptorLength;
                //now zero out the reserved bytes for the mode select command
                modeData[0] = 0;//mode data length is reserved for mode select commands
                //leave medium type alone
                //leave device specific parameter alone???
                //leave block descriptor length alone in case we got some.
            }
            else
            {
                blockDescriptorLength = M_BytesTo2ByteValue(modeData[6], modeData[7]);
                offset = MODE_PARAMETER_HEADER_10_LEN + blockDescriptorLength;
                //now zero out the reserved bytes for the mode select command
                modeData[0] = 0;//mode data length is reserved for mode select commands
                modeData[1] = 0;
                //leave medium type alone
                //leave device specific parameter alone???
                //leave block descriptor length alone in case we got some.
            }
            //copy the incoming buffer (which is ONLY mode page data)
            memcpy(&modeData[offset], modePageData, M_Min(modeDataLength, modePageLength));
            //now send the mode select command
            bool pageFormat = modePage == 0 ? false : true;//set to false when reading vendor unique page zero
            //bool savable = modeData[offset + 0] & BIT7;// use this to save pages. This bit says whether the page/settings can be saved or not.
            if (used6ByteCmd)
            {
                if (SUCCESS != scsi_Mode_Select_6(device, C_CAST(uint8_t, modePageLength), pageFormat, saveChanges, false, modeData, modePageLength))
                {
                    ret = FAILURE;
                }
                else
                {
                    ret = SUCCESS;
                }
            }
            else
            {
                if (SUCCESS != scsi_Mode_Select_10(device, C_CAST(uint16_t, modePageLength), pageFormat, saveChanges, false, modeData, modePageLength))
                {
                    ret = FAILURE;
                }
                else
                {
                    ret = SUCCESS;
                }
            }
        }
        safe_Free_aligned(modeData)
    }
    else
    {
        //most likely not a supported page
    }
    return ret;
}

#define SCSI_MODE_PAGE_NAME_MAX_LENGTH 40
//TODO: this doesn't take into account some pages being device type specific. It does try for page 1Ch an 1Dh
void get_SCSI_MP_Name(uint8_t scsiDeviceType, uint8_t modePage, uint8_t subpage, char *mpName)
{
    scsiDeviceType = M_GETBITRANGE(scsiDeviceType, 4, 0);//strip off the qualifier if it was passed
    switch (modePage)
    {
    case 0x00://vendor unique
        break;
    case 0x01:
        switch (subpage)
        {
        case 0x00://read-write error recovery
            snprintf(mpName, SCSI_MODE_PAGE_NAME_MAX_LENGTH, "Read-Write Error Recovery");
            break;
        default:
            //unknown
            break;
        }
        break;
    case 0x02:
        switch (subpage)
        {
        case 0x00://disconnect-reconnect
            snprintf(mpName, SCSI_MODE_PAGE_NAME_MAX_LENGTH, "Disconnect-Reconnect");
            break;
        default:
            //unknown
            break;
        }
        break;
    case 0x03:
        switch (subpage)
        {
        case 0x00://Format Device (block devie) or MRW CD-RW (cd/dvd)
            switch (scsiDeviceType)
            {
            case PERIPHERAL_DIRECT_ACCESS_BLOCK_DEVICE:
            case PERIPHERAL_HOST_MANAGED_ZONED_BLOCK_DEVICE:
                snprintf(mpName, SCSI_MODE_PAGE_NAME_MAX_LENGTH, "Format Device");
                break;
            case PERIPHERAL_CD_DVD_DEVICE:
                snprintf(mpName, SCSI_MODE_PAGE_NAME_MAX_LENGTH, "MRW CD-RW");
                break;
            default:
                break;
            }
            break;
        default:
            //unknown
            break;
        }
        break;
    case 0x04:
        switch (subpage)
        {
        case 0x00://Rigid Disk Geometry
            snprintf(mpName, SCSI_MODE_PAGE_NAME_MAX_LENGTH, "Rigid Disk Geometry");
            break;
        default:
            //unknown
            break;
        }
        break;
    case 0x05:
        switch (subpage)
        {
        case 0x00://flexible disk
            snprintf(mpName, SCSI_MODE_PAGE_NAME_MAX_LENGTH, "Flexible Disk");
            break;
        default:
            //unknown
            break;
        }
        break;
    case 0x06:
        switch (subpage)
        {
        case 0x00://optical memory (SBC) OR RBC device parameters
            switch (scsiDeviceType)
            {
            case PERIPHERAL_OPTICAL_MEMORY_DEVICE:
                snprintf(mpName, SCSI_MODE_PAGE_NAME_MAX_LENGTH, "Optical Memory");
                break;
            case PERIPHERAL_SIMPLIFIED_DIRECT_ACCESS_DEVICE:
                snprintf(mpName, SCSI_MODE_PAGE_NAME_MAX_LENGTH, "RBC Device Parameters");
                break;
            default:
                break;
            }
            break;
        default:
            //unknown
            break;
        }
        break;
    case 0x07:
        switch (subpage)
        {
        case 0x00://verify error recovery
            snprintf(mpName, SCSI_MODE_PAGE_NAME_MAX_LENGTH, "Verify Error Recovery");
            break;
        default:
            //unknown
            break;
        }
        break;
    case 0x08:
        switch (subpage)
        {
        case 0x00://Caching
            snprintf(mpName, SCSI_MODE_PAGE_NAME_MAX_LENGTH, "Caching");
            break;
        default:
            //unknown
            break;
        }
        break;
    case 0x09:
        switch (subpage)
        {
        case 0x00://peripheral device
            snprintf(mpName, SCSI_MODE_PAGE_NAME_MAX_LENGTH, "Peripheral Device");
            break;
        default:
            //unknown
            break;
        }
        break;
    case 0x0A:
        switch (subpage)
        {
        case 0x00://control
            snprintf(mpName, SCSI_MODE_PAGE_NAME_MAX_LENGTH, "Control");
            break;
        case 0x01://control extension
            snprintf(mpName, SCSI_MODE_PAGE_NAME_MAX_LENGTH, "Control Extension");
            break;
        case 0x02://application tag
            snprintf(mpName, SCSI_MODE_PAGE_NAME_MAX_LENGTH, "Application Tag");
            break;
        case 0x03://command duration limit A
            snprintf(mpName, SCSI_MODE_PAGE_NAME_MAX_LENGTH, "Command Duration Limit A");
            break;
        case 0x04://command duration limit B
            snprintf(mpName, SCSI_MODE_PAGE_NAME_MAX_LENGTH, "Command Duration Limit B");
            break;
        case 0x05://IO Advice Hints Grouping
            snprintf(mpName, SCSI_MODE_PAGE_NAME_MAX_LENGTH, "IO Advice Hints Grouping");
            break;
        case 0x06://Background Operation Control
            snprintf(mpName, SCSI_MODE_PAGE_NAME_MAX_LENGTH, "Background Operation Control");
            break;
        case 0xF0://Control Data Protection
            snprintf(mpName, SCSI_MODE_PAGE_NAME_MAX_LENGTH, "Control Data Protection");
            break;
        case 0xF1://PATA Control
            snprintf(mpName, SCSI_MODE_PAGE_NAME_MAX_LENGTH, "PATA Control");
            break;
        default:
            //unknown
            break;
        }
        break;
    case 0x0B:
        switch (subpage)
        {
        case 0x00://Medium Types Supported
            snprintf(mpName, SCSI_MODE_PAGE_NAME_MAX_LENGTH, "Medium Types Supported");
            break;
        default:
            //unknown
            break;
        }
        break;
    case 0x0C:
        switch (subpage)
        {
        case 0x00://notch and partition
            snprintf(mpName, SCSI_MODE_PAGE_NAME_MAX_LENGTH, "Notch And Partition");
            break;
        default:
            //unknown
            break;
        }
        break;
    case 0x0D:
        switch (subpage)
        {
        case 0x00://Power Condition (direct accessblock device) or CD Device Parameters
            switch (scsiDeviceType)
            {
            case PERIPHERAL_HOST_MANAGED_ZONED_BLOCK_DEVICE:
            case PERIPHERAL_DIRECT_ACCESS_BLOCK_DEVICE:
                snprintf(mpName, SCSI_MODE_PAGE_NAME_MAX_LENGTH, "Power Condition");
                break;
            case PERIPHERAL_CD_DVD_DEVICE:
                snprintf(mpName, SCSI_MODE_PAGE_NAME_MAX_LENGTH, "CD Device Parameters");
                break;
            default:
                break;
            }
            break;
        default:
            //unknown
            break;
        }
        break;
    case 0x0E:
        switch (subpage)
        {
        case 0x00://CD Audio Control
            snprintf(mpName, SCSI_MODE_PAGE_NAME_MAX_LENGTH, "CD Audio Control");
            break;
        case 0x01://Target Device
            snprintf(mpName, SCSI_MODE_PAGE_NAME_MAX_LENGTH, "Target Device");
            break;
        case 0x02://DT Device Primary Port
            snprintf(mpName, SCSI_MODE_PAGE_NAME_MAX_LENGTH, "DT Devuce Primary Port");
            break;
        case 0x03://Logical Unit
            snprintf(mpName, SCSI_MODE_PAGE_NAME_MAX_LENGTH, "Logical Unit");
            break;
        case 0x04://Target Device Serial Number
            snprintf(mpName, SCSI_MODE_PAGE_NAME_MAX_LENGTH, "Target Device Serial Number");
            break;
        default:
            //unknown
            break;
        }
        break;
    case 0x0F:
        switch (subpage)
        {
        case 0x00://Data Compression
            snprintf(mpName, SCSI_MODE_PAGE_NAME_MAX_LENGTH, "Data Compression");
            break;
        default:
            //unknown
            break;
        }
        break;
    case 0x10:
        switch (subpage)
        {
        case 0x00://XOR Control (direct access) OR Device configuration (tape)
            switch (scsiDeviceType)
            {
            case PERIPHERAL_HOST_MANAGED_ZONED_BLOCK_DEVICE:
            case PERIPHERAL_DIRECT_ACCESS_BLOCK_DEVICE:
                snprintf(mpName, SCSI_MODE_PAGE_NAME_MAX_LENGTH, "XOR Control");
                break;
            case PERIPHERAL_SEQUENTIAL_ACCESS_BLOCK_DEVICE:
                snprintf(mpName, SCSI_MODE_PAGE_NAME_MAX_LENGTH, "Device Configuration");
                break;
            default:
                break;
            }
            break;
        case 0x01://Device Configuration Extension
            snprintf(mpName, SCSI_MODE_PAGE_NAME_MAX_LENGTH, "Device Configuration Extension");
            break;
        default:
            //unknown
            break;
        }
        break;
    case 0x11:
        switch (subpage)
        {
        case 0x00://Medium Partition (1)
            snprintf(mpName, SCSI_MODE_PAGE_NAME_MAX_LENGTH, "Medium Partition (1)");
            break;
        default:
            //unknown
            break;
        }
        break;
        //12h and 13h are in the SPC5 annex, but not named...skipping
    case 0x14:
        switch (subpage)
        {
        case 0x00://enclosure services management
            snprintf(mpName, SCSI_MODE_PAGE_NAME_MAX_LENGTH, "Enclosure Services Management");
            break;
        default:
            //unknown
            break;
        }
        break;
    case 0x15://Extended
        //all subpages
        snprintf(mpName, SCSI_MODE_PAGE_NAME_MAX_LENGTH, "Extended - %" PRIu8, subpage);
        break;
    case 0x16://Extended Device-Type specific
        //all subpages
        snprintf(mpName, SCSI_MODE_PAGE_NAME_MAX_LENGTH, "Extended Device Type Specific - %" PRIu8, subpage);
        break;
        //17h is in spec, but not named
    case 0x18://protocol specific logical unit
        snprintf(mpName, SCSI_MODE_PAGE_NAME_MAX_LENGTH, "Protocol Specific Logical Unit - %" PRIu8, subpage); 
        break;
    case 0x19://protocol specific port
        snprintf(mpName, SCSI_MODE_PAGE_NAME_MAX_LENGTH, "Protocol Specific Port - %" PRIu8, subpage);
        break;
    case 0x1A:
        switch (subpage)
        {
        case 0x00://Power Condition
            snprintf(mpName, SCSI_MODE_PAGE_NAME_MAX_LENGTH, "Power Condition");
            break;
        case 0x01://Power Consumption
            snprintf(mpName, SCSI_MODE_PAGE_NAME_MAX_LENGTH, "Power Consumption");
            break;
        case 0xF1://ATA Power Condition
            snprintf(mpName, SCSI_MODE_PAGE_NAME_MAX_LENGTH, "ATA Power Condition");
            break;
        default:
            //unknown
            break;
        }
        break;
    case 0x1B:
        switch (subpage)
        {
        case 0x00://LUN Mapping
            snprintf(mpName, SCSI_MODE_PAGE_NAME_MAX_LENGTH, "LUN Mapping");
            break;
        default:
            //unknown
            break;
        }
        break;
    case 0x1C:
        switch (subpage)
        {
        case 0x00://Varies on name depending on device type. All related to failure reporting though!!!
            switch (scsiDeviceType)
            {
            case PERIPHERAL_HOST_MANAGED_ZONED_BLOCK_DEVICE:
            case PERIPHERAL_DIRECT_ACCESS_BLOCK_DEVICE:
            case PERIPHERAL_OPTICAL_MEMORY_DEVICE:
                snprintf(mpName, SCSI_MODE_PAGE_NAME_MAX_LENGTH, "Informational Exceptions Control");
                break;
            case PERIPHERAL_CD_DVD_DEVICE:
                snprintf(mpName, SCSI_MODE_PAGE_NAME_MAX_LENGTH, "Fault/Failure Reporting");
                break;
            case PERIPHERAL_SEQUENTIAL_ACCESS_BLOCK_DEVICE:
            case PERIPHERAL_AUTOMATION_DRIVE_INTERFACE:
            case PERIPHERAL_MEDIUM_CHANGER_DEVICE:
                snprintf(mpName, SCSI_MODE_PAGE_NAME_MAX_LENGTH, "Informational Exceptions Control (Tape)");
                break;
            default:
                break;
            }
            break;
        case 0x01://background control
            snprintf(mpName, SCSI_MODE_PAGE_NAME_MAX_LENGTH, "Background Control");
            break;
        case 0x02://logical block provisioning
            snprintf(mpName, SCSI_MODE_PAGE_NAME_MAX_LENGTH, "Logical Block Provisioning");
            break;
        default:
            //unknown
            break;
        }
        break;
    case 0x1D:
        switch (subpage)
        {
        case 0x00://varies depending on device type
            switch (scsiDeviceType)
            {
            case PERIPHERAL_CD_DVD_DEVICE:
                snprintf(mpName, SCSI_MODE_PAGE_NAME_MAX_LENGTH, "C/DVD Time-Out And Protect");
                break;
            case PERIPHERAL_SEQUENTIAL_ACCESS_BLOCK_DEVICE:
                snprintf(mpName, SCSI_MODE_PAGE_NAME_MAX_LENGTH, "Medium Configuration");
                break;
            case PERIPHERAL_MEDIUM_CHANGER_DEVICE:
                snprintf(mpName, SCSI_MODE_PAGE_NAME_MAX_LENGTH, "Element Address Assignments");
                break;
            default:
                break;
            }
            break;
        default:
            //unknown
            break;
        }
        break;
    case 0x1E:
        switch (subpage)
        {
        case 0x00://transport geometry parameters
            snprintf(mpName, SCSI_MODE_PAGE_NAME_MAX_LENGTH, "Transport Geometry Parameters");
            break;
        default:
            //unknown
            break;
        }
        break;
    case 0x1F:
        switch (subpage)
        {
        case 0x00://device capabilities
            snprintf(mpName, SCSI_MODE_PAGE_NAME_MAX_LENGTH, "Device Capabilities");
            break;
        default:
            //unknown
            break;
        }
        break;
    case 0x2A:
        switch (subpage)
        {
        case 0x00://CD Capabilities and Mechanical Status - CD-DVD
            switch (scsiDeviceType)
            {
            case PERIPHERAL_CD_DVD_DEVICE:
                snprintf(mpName, SCSI_MODE_PAGE_NAME_MAX_LENGTH, "CD Capabilities and Mechanical Status");
                break;
            default:
                break;
            }
            break;
        default:
            //unknown
            break;
        }
        break;
    default:
        //unknown
        break;
    }
}

//this should only have the mode data. NO block descriptors or mode page header (4 or 8 bytes before the mode page starts)
void print_Mode_Page(uint8_t scsiPeripheralDeviceType, uint8_t* modeData, uint32_t modeDataLen, eScsiModePageControl mpc, bool outputWithPrintDataBuffer)
{
    if (modeData && modeDataLen > 2)
    {
        uint8_t pageNumber = M_GETBITRANGE(modeData[0], 5, 0);
        uint8_t subpage = 0;
        uint16_t pageLength = modeData[1] + 2;//page 0 format
        if (modeData[0] & BIT6)
        {
            subpage = modeData[1];
            pageLength = M_BytesTo2ByteValue(modeData[2], modeData[3]) + 4;
        }
        int equalsLengthToPrint = (M_Min(pageLength, modeDataLen) * 3) - 1;
        //print the header
        if (outputWithPrintDataBuffer)
        {
            equalsLengthToPrint = 1;
            switch (mpc)
            {
            case MPC_CURRENT_VALUES:
                equalsLengthToPrint += C_CAST(int, strlen(" Current Values"));
                break;
            case MPC_CHANGABLE_VALUES:
                equalsLengthToPrint += C_CAST(int, strlen(" Changable Values"));
                break;
            case MPC_DEFAULT_VALUES:
                equalsLengthToPrint += C_CAST(int, strlen(" Default Values"));
                break;
            case MPC_SAVED_VALUES:
                equalsLengthToPrint += C_CAST(int, strlen(" Saved Values"));
                if (subpage > 0)
                {
                    ++equalsLengthToPrint;
                }
                break;
            default://this shouldn't happen...
                equalsLengthToPrint = 16;
                break;
            }
        }
        //before going further, check if we have a page name to lookup and printout to adjust the size for
        char pageName[SCSI_MODE_PAGE_NAME_MAX_LENGTH] = { 0 };
        get_SCSI_MP_Name(scsiPeripheralDeviceType, pageNumber, subpage, pageName);
        if (equalsLengthToPrint < (C_CAST(int, strlen(pageName)) + 6)) //name will go too far over the end, need to enlarge
        {
            //the equals length should be enlarged for this!!!
            equalsLengthToPrint = C_CAST(int, strlen(pageName)) + 6;
            if (pageNumber >= 0x10)
            {
                equalsLengthToPrint += 3;
            }
            else
            {
                equalsLengthToPrint += 2;
            }
            if (subpage > 0)
            {
                equalsLengthToPrint += 3;
                if (subpage >= 0x10)
                {
                    equalsLengthToPrint += 3;
                }
                else
                {
                    equalsLengthToPrint += 2;
                }
            }
            equalsLengthToPrint += 2;//for space at beginning and end
        }
        printf("\n%.*s\n", equalsLengthToPrint, "==================================================================================");//80 characters max...
        printf(" Page %" PRIX8 "h", pageNumber);
        if (subpage != 0)
        {
            printf(" - %" PRIX8 "h", subpage);
        }
        if (strlen(pageName) > 0)
        {
            printf(" %s", pageName);
        }
        printf("\n");
        switch (mpc)
        {
        case MPC_CURRENT_VALUES:
            printf(" Current Values");
            break;
        case MPC_CHANGABLE_VALUES:
            printf(" Changable Values");
            break;
        case MPC_DEFAULT_VALUES:
            printf(" Default Values");
            break;
        case MPC_SAVED_VALUES:
            printf(" Saved Values");
            break;
        default://this shouldn't happen...
            break;
        }
        printf("\n%.*s\n", equalsLengthToPrint, "==================================================================================");//80 characters max...
        //print out the raw data bytes sent to this function
        if (outputWithPrintDataBuffer)
        {
            print_Data_Buffer(modeData, M_Min(pageLength, modeDataLen), false);
        }
        else
        {
            //TODO: Do we want another variable to track when we get to 80 characters wide and print a newline and indent the next line??? - Not needed yet since we don't have a mode page that large
            for (uint16_t iter = 0; iter < M_Min(pageLength, modeDataLen); ++iter)
            {
                printf("%02" PRIX8, modeData[iter]);
                if ((uint32_t)(iter + UINT16_C(1)) < M_Min(pageLength, modeDataLen))
                {
                    printf(" ");
                }
            }
        }
        printf("\n");
    }
    else if(modeData)
    {
        //page not supported
        uint8_t pageNumber = M_GETBITRANGE(modeData[0], 5, 0);
        uint8_t subpage = 0;
        if (modeData[0] & BIT6)
        {
            subpage = modeData[1];
        }
        int equalsLengthToPrint = 1;
        //print the header
        switch (mpc)
        {
        case MPC_CURRENT_VALUES:
            equalsLengthToPrint += C_CAST(int, strlen(" Current Values"));
            break;
        case MPC_CHANGABLE_VALUES:
            equalsLengthToPrint += C_CAST(int, strlen(" Changable Values"));
            break;
        case MPC_DEFAULT_VALUES:
            equalsLengthToPrint += C_CAST(int, strlen(" Default Values"));
            break;
        case MPC_SAVED_VALUES:
            equalsLengthToPrint += C_CAST(int, strlen(" Saved Values"));
            if (subpage > 0)
            {
                ++equalsLengthToPrint;
            }
            break;
        default://this shouldn't happen...
            equalsLengthToPrint = 16;
            break;
        }
        printf("\n%.*s\n", equalsLengthToPrint, "==================================================================================");//80 characters max...
        printf(" Page %" PRIX8 "h", pageNumber);
        if (subpage != 0)
        {
            printf(" - %" PRIX8 "h", subpage);
        }
        printf("\n");
        switch (mpc)
        {
        case MPC_CURRENT_VALUES:
            printf(" Current Values");
            break;
        case MPC_CHANGABLE_VALUES:
            printf(" Changable Values");
            break;
        case MPC_DEFAULT_VALUES:
            printf(" Default Values");
            break;
        case MPC_SAVED_VALUES:
            printf(" Saved Values");
            break;
        default://this shouldn't happen...
            break;
        }
        printf("\n%.*s\n", equalsLengthToPrint, "==================================================================================");//80 characters max...
        printf("Not Supported.\n");
    }
}

//shows a single mode page for the selected control(current, saved, changable, default)
void show_SCSI_Mode_Page(tDevice * device, uint8_t modePage, uint8_t subpage, eScsiModePageControl mpc, bool bufferFormatOutput)
{
    uint32_t modePageLength = 0;
    if (modePage == MP_RETURN_ALL_PAGES || subpage == MP_SP_ALL_SUBPAGES)//if asking for all mode pages, all mode pages and subpages, or all subpages of a specific page, we need to handle it in here.
    {
        if (SUCCESS == get_SCSI_Mode_Page_Size(device, mpc, modePage, subpage, &modePageLength))
        {
            uint8_t *modeData = C_CAST(uint8_t*, calloc_aligned(modePageLength, sizeof(uint8_t), device->os_info.minimumAlignment));
            if (!modeData)
            {
                return;
            }
            //now read all the data
            bool used6ByteCmd = false;
            if (SUCCESS == get_SCSI_Mode_Page(device, mpc, modePage, subpage, NULL, NULL, true, modeData, modePageLength, NULL, &used6ByteCmd))
            {
                //Loop through each page returned in the buffer and print it to the screen
                uint32_t offset = 0;
                uint16_t blockDescriptorLength = 0;
                if (!used6ByteCmd)
                {
                    //got 10 byte command data
                    blockDescriptorLength = M_BytesTo2ByteValue(modeData[6], modeData[7]);
                    offset = MODE_PARAMETER_HEADER_10_LEN + blockDescriptorLength;
                }
                else
                {
                    //got 6 byte command data.
                    blockDescriptorLength = modeData[3];
                    offset = MODE_PARAMETER_HEADER_6_LEN + blockDescriptorLength;
                }
                uint16_t currentPageLength = 0;
                uint16_t counter = 0;
                for (; offset < modePageLength; offset += currentPageLength, ++counter)
                {
                    if (modeData[offset] & BIT6)
                    {
                        //subpage format
                        currentPageLength = M_BytesTo2ByteValue(modeData[offset + 2], modeData[offset + 3]) + 4;//add 4 bytes for page code, subpage code, & page length bytes
                    }
                    else
                    {
                        currentPageLength = modeData[offset + 1] + 2;//add 2 bytes for the page code and page length bytes
                    }
                    //now print the page out!
                    print_Mode_Page(device->drive_info.scsiVpdData.inquiryData[0], &modeData[offset], currentPageLength, mpc, bufferFormatOutput);
                }
            }
            safe_Free_aligned(modeData)
        }
        else
        {
            //not supported (SATL most likely)
            uint8_t modeData[2] = { 0 };
            modeData[0] = modePage;
            modeData[1] = subpage;
            print_Mode_Page(device->drive_info.scsiVpdData.inquiryData[0], modeData, 2, mpc, bufferFormatOutput);
        }
    }
    else
    {
        //single page...easy
        if (SUCCESS == get_SCSI_Mode_Page_Size(device, mpc, modePage, subpage, &modePageLength))
        {
            uint8_t *modeData = C_CAST(uint8_t*, calloc_aligned(modePageLength, sizeof(uint8_t), device->os_info.minimumAlignment));
            if (!modeData)
            {
                return;
            }
            //now read all the data
            bool used6ByteCmd = false;
            if (SUCCESS == get_SCSI_Mode_Page(device, mpc, modePage, subpage, NULL, NULL, true, modeData, modePageLength, NULL, &used6ByteCmd))
            {
                if (used6ByteCmd)
                {
                    print_Mode_Page(device->drive_info.scsiVpdData.inquiryData[0], &modeData[MODE_PARAMETER_HEADER_6_LEN + modeData[3]/*block descripto length in case one was returned*/], modePageLength - MODE_PARAMETER_HEADER_10_LEN - modeData[3], mpc, bufferFormatOutput);
                }
                else
                {
                    uint16_t blockDescriptorLength = M_BytesTo2ByteValue(modeData[6], modeData[7]);
                    print_Mode_Page(device->drive_info.scsiVpdData.inquiryData[0], &modeData[MODE_PARAMETER_HEADER_10_LEN + blockDescriptorLength], modePageLength - MODE_PARAMETER_HEADER_10_LEN - blockDescriptorLength, mpc, bufferFormatOutput);
                }
            }
            safe_Free_aligned(modeData)
        }
        else
        {
            //not supported (SATL most likely)
            uint8_t modeData[2] = { 0 };
            modeData[0] = modePage;
            modeData[1] = subpage;
            print_Mode_Page(device->drive_info.scsiVpdData.inquiryData[0], modeData, 2, mpc, bufferFormatOutput);
        }
    }
}

//shows all mpc values for a given page.
//should we return an error when asking for all mode pages since that output will otherwise be really messy???
void show_SCSI_Mode_Page_All(tDevice * device, uint8_t modePage, uint8_t subpage, bool bufferFormatOutput)
{
    //if (modePage == MP_RETURN_ALL_PAGES || subpage == MP_SP_ALL_SUBPAGES)
    //{
    //    //TODO: custom function or other code to handle input of modepage == 0x3F || subpage == 0xFF and keep the output clean?
    //}
    //else
    {
        //TODO: loop through and print a page out for each MPC value.
        eScsiModePageControl mpc = MPC_CURRENT_VALUES;//will be incremented through a loop
        for (; mpc <= MPC_SAVED_VALUES; ++mpc)
        {
            show_SCSI_Mode_Page(device, modePage, subpage, mpc, bufferFormatOutput);
        }
    }
}

//if yes, a page and subpage can be provided when doing a log page reset
bool reset_Specific_Log_Page_Supported(tDevice *device)
{
    bool supported = false;
    if (device->drive_info.scsiVersion >= SCSI_VERSION_SPC_3)
    {
        uint8_t supportData[14] = { 0 };
        if (SUCCESS == scsi_Report_Supported_Operation_Codes(device, false, 1, LOG_SELECT_CMD, 0, 14, supportData))
        {
            uint8_t support = M_GETBITRANGE(supportData[1], 2, 0);
            uint16_t cdbSize = M_BytesTo2ByteValue(supportData[2], supportData[3]);
            uint8_t offset = 4;
            switch (support)
            {
            case 0x03://supports in conformance with a scsi standard
                //check the CDB usage data to see if we can set a page code or subpage code
                if (cdbSize > 0)
                {
                    if (M_GETBITRANGE(supportData[offset + 2], 5, 0) > 0 && supportData[offset + 3] > 0)
                    {
                        supported = true;
                    }
                }
                break;
            default:
                break;
            }
        }
    }
    return supported;
}

int reset_SCSI_Log_Page(tDevice * device, eScsiLogPageControl pageControl, uint8_t logPage, uint8_t logSubPage, bool saveChanges)
{
    int ret = NOT_SUPPORTED;
    if (logPage || logSubPage)
    {
        if (!reset_Specific_Log_Page_Supported(device))
        {
            return BAD_PARAMETER;//cannot reset a specific page on this device
        }
    }
    ret = scsi_Log_Select_Cmd(device, true, saveChanges, pageControl, logPage, logSubPage, 0, NULL, 0);

    return ret;
}

//doing this in SCSI way for now...should handle nvme separately at some point since a namespace is similar to a lun
uint8_t get_LUN_Count(tDevice *device)
{
    uint8_t lunCount = 1;//assume 1 since we are talking over a lun right now. - TJE
    if (device->drive_info.interface_type != USB_INTERFACE && device->drive_info.interface_type != IEEE_1394_INTERFACE)
    {
        uint8_t luns[4] = { 0 };
        uint8_t selectReport = 0x02;//or 0????
        if (SUCCESS == scsi_Report_Luns(device, selectReport, 4, luns))
        {
            uint32_t lunListLength = M_BytesTo4ByteValue(luns[0], luns[1], luns[2], luns[3]);
            lunCount = C_CAST(uint8_t, lunListLength / UINT32_C(8));
        }
    }
    return lunCount;
}

eMLU get_MLU_Value_For_SCSI_Operation(tDevice *device, uint8_t operationCode, uint16_t serviceAction)
{
    eMLU mlu = MLU_NOT_REPORTED;
    uint8_t reportOp[4] = { 0 };
    uint8_t reportingOptions = 1;
    if (serviceAction > 0)
    {
        reportingOptions = 2;
    }
    if (SUCCESS == scsi_Report_Supported_Operation_Codes(device, false, reportingOptions, operationCode, serviceAction, 4, reportOp))
    {
        switch (M_GETBITRANGE(reportOp[1], 2, 0))
        {
        case 3:
            mlu = C_CAST(eMLU, M_GETBITRANGE(reportOp[1], 6, 5));
            break;
        default:
            break;
        }
    }
    return mlu;
}

bool scsi_Mode_Pages_Shared_By_Multiple_Logical_Units(tDevice *device, uint8_t modePage, uint8_t subPage)
{
    bool mlus = false;
    uint32_t modePagePolicyLength = 4;
    uint8_t *vpdModePagePolicy = C_CAST(uint8_t*, calloc_aligned(modePagePolicyLength, sizeof(uint8_t), device->os_info.minimumAlignment));
    if (vpdModePagePolicy)
    {
        if (SUCCESS == scsi_Inquiry(device, vpdModePagePolicy, modePagePolicyLength, MODE_PAGE_POLICY, true, false))
        {
            modePagePolicyLength = M_BytesTo2ByteValue(vpdModePagePolicy[2], vpdModePagePolicy[3]) + 4;
            safe_Free_aligned(vpdModePagePolicy)
            vpdModePagePolicy = C_CAST(uint8_t*, calloc_aligned(modePagePolicyLength, sizeof(uint8_t), device->os_info.minimumAlignment));
            if (vpdModePagePolicy)
            {
                if (SUCCESS == scsi_Inquiry(device, vpdModePagePolicy, modePagePolicyLength, MODE_PAGE_POLICY, true, false))
                {
                    modePagePolicyLength = M_BytesTo2ByteValue(vpdModePagePolicy[2], vpdModePagePolicy[3]) + 4;
                    //Now loop through and find the requested page
                    for (uint32_t vpdMPOffset = 4; vpdMPOffset < UINT16_MAX && vpdMPOffset < modePagePolicyLength; vpdMPOffset += 4)
                    {
                        if (modePage == M_GETBITRANGE(vpdModePagePolicy[vpdMPOffset], 5, 0) && subPage == vpdModePagePolicy[vpdMPOffset + 1])
                        {
                            mlus = M_ToBool(vpdModePagePolicy[vpdMPOffset + 2] & BIT7);
                            break;
                        }
                        else if (M_GETBITRANGE(vpdModePagePolicy[vpdMPOffset], 5, 0) == 0x3F && subPage == 0 && vpdModePagePolicy[vpdMPOffset + 1] == 0)
                        {
                            //This is the "report all mode pages", no subpages to indicate that the mlus applies to all mode pages on the device.
                            mlus = M_ToBool(vpdModePagePolicy[vpdMPOffset + 2] & BIT7);
                            break;
                        }
                        else if (M_GETBITRANGE(vpdModePagePolicy[vpdMPOffset], 5, 0) == 0x3F && vpdModePagePolicy[vpdMPOffset + 1] == 0xFF)
                        {
                            //This is the "report all mode pages and subpages", to indicate that the mlus applies to all mode pages and subpages on the device.
                            mlus = M_ToBool(vpdModePagePolicy[vpdMPOffset + 2] & BIT7);
                            break;
                        }
                    }
                }
            }
        }
        safe_Free_aligned(vpdModePagePolicy)
    }
    return mlus;
}

#define CONCURRENT_RANGES_VERSION_V1 1

typedef struct _concurrentRangeDescriptionV1
{
    uint8_t rangeNumber;
    uint8_t numberOfStorageElements;
    uint64_t lowestLBA;
    uint64_t numberOfLBAs;
}concurrentRangeDescriptionV1;

typedef struct _concurrentRangesV1
{
    size_t size;
    uint32_t version;
    uint8_t numberOfRanges;
    concurrentRangeDescriptionV1 range[15];//maximum of 15 concurrent ranges per ACS5
}concurrentRangesV1, *ptrConcurrentRangesV1;

int get_Concurrent_Positioning_Ranges(tDevice *device, ptrConcurrentRanges ranges)
{
    int ret = NOT_SUPPORTED;
    if (ranges && ranges->size >= sizeof(concurrentRangesV1) && ranges->version >= CONCURRENT_RANGES_VERSION_V1)
    {
        if (device->drive_info.drive_type == ATA_DRIVE)
        {
            uint32_t concurrentLogSizeBytes = 0;//NOTE: spec currently says this is at most 1024 bytes, but may be as low as 512
            if (SUCCESS == get_ATA_Log_Size(device, ATA_LOG_CONCURRENT_POSITIONING_RANGES, &concurrentLogSizeBytes, true, false) && concurrentLogSizeBytes > 0)
            {
                uint8_t *concurrentRangeLog = C_CAST(uint8_t*, calloc_aligned(concurrentLogSizeBytes, sizeof(uint8_t), device->os_info.minimumAlignment));
                if (!concurrentRangeLog)
                {
                    return MEMORY_FAILURE;
                }
                if (SUCCESS == get_ATA_Log(device, ATA_LOG_CONCURRENT_POSITIONING_RANGES, NULL, NULL, true, false, true, concurrentRangeLog, concurrentLogSizeBytes, NULL, 0, 0))
                {
                    ret = SUCCESS;
                    //header is first 64bytes
                    ranges->numberOfRanges = concurrentRangeLog[0];
                    //now loop through descriptors
                    for (uint32_t offset = 64, rangeCounter = 0; offset < concurrentLogSizeBytes && rangeCounter < ranges->numberOfRanges && rangeCounter < 15; offset += 32, ++rangeCounter)
                    {
                        ranges->range[rangeCounter].rangeNumber = concurrentRangeLog[offset + 0];
                        ranges->range[rangeCounter].numberOfStorageElements = concurrentRangeLog[offset + 1];
                        //ATA QWords are little endian!
                        ranges->range[rangeCounter].lowestLBA = M_BytesTo8ByteValue(0, 0, concurrentRangeLog[offset + 13], concurrentRangeLog[offset + 12], concurrentRangeLog[offset + 11], concurrentRangeLog[offset + 10], concurrentRangeLog[offset + 9], concurrentRangeLog[offset + 8]);
                        ranges->range[rangeCounter].numberOfLBAs = M_BytesTo8ByteValue(concurrentRangeLog[offset + 23], concurrentRangeLog[offset + 22], concurrentRangeLog[offset + 21], concurrentRangeLog[offset + 20], concurrentRangeLog[offset + 19], concurrentRangeLog[offset + 18], concurrentRangeLog[offset + 17], concurrentRangeLog[offset + 16]);
                    }
                }
                safe_Free_aligned(concurrentRangeLog)
            }
        }
        else if (device->drive_info.drive_type == SCSI_DRIVE)
        {
            uint32_t concurrentLogSizeBytes = 0;
            if (SUCCESS == get_SCSI_VPD_Page_Size(device, CONCURRENT_POSITIONING_RANGES, &concurrentLogSizeBytes) && concurrentLogSizeBytes > 0)
            {
                uint8_t *concurrentRangeVPD = C_CAST(uint8_t*, calloc_aligned(concurrentLogSizeBytes, sizeof(uint8_t), device->os_info.minimumAlignment));
                if (!concurrentRangeVPD)
                {
                    return MEMORY_FAILURE;
                }
                if (SUCCESS == get_SCSI_VPD(device, CONCURRENT_POSITIONING_RANGES, NULL, NULL, true, concurrentRangeVPD, concurrentLogSizeBytes, NULL))
                {
                    ret = SUCCESS;
                    //calculate number of ranges based on page length
                    ranges->numberOfRanges = (M_BytesTo2ByteValue(concurrentRangeVPD[2], concurrentRangeVPD[3]) - 60) / 32;//-60 since page length doesn't include first 4 bytes and descriptors start at offset 64. Each descriptor is 32B long
                    //loop through descriptors
                    for (uint32_t offset = 64, rangeCounter = 0; offset < concurrentLogSizeBytes && rangeCounter < ranges->numberOfRanges && rangeCounter < 15; offset += 32, ++rangeCounter)
                    {
                        ranges->range[rangeCounter].rangeNumber = concurrentRangeVPD[offset + 0];
                        ranges->range[rangeCounter].numberOfStorageElements = concurrentRangeVPD[offset + 1];
                        //SCSI is big endian
                        ranges->range[rangeCounter].lowestLBA = M_BytesTo8ByteValue(concurrentRangeVPD[offset + 8], concurrentRangeVPD[offset + 9], concurrentRangeVPD[offset + 10], concurrentRangeVPD[offset + 11], concurrentRangeVPD[offset + 12], concurrentRangeVPD[offset + 13], concurrentRangeVPD[offset + 14], concurrentRangeVPD[offset + 15]);
                        ranges->range[rangeCounter].numberOfLBAs = M_BytesTo8ByteValue(concurrentRangeVPD[offset + 16], concurrentRangeVPD[offset + 17], concurrentRangeVPD[offset + 18], concurrentRangeVPD[offset + 19], concurrentRangeVPD[offset + 20], concurrentRangeVPD[offset + 21], concurrentRangeVPD[offset + 22], concurrentRangeVPD[offset + 23]);
                    }
                }
                safe_Free_aligned(concurrentRangeVPD)
            }
        }
    }
    else
    {
        ret = BAD_PARAMETER;
    }
    return ret;
}

void print_Concurrent_Positioning_Ranges(ptrConcurrentRanges ranges)
{
    if (ranges && ranges->size >= sizeof(concurrentRangesV1) && ranges->version >= CONCURRENT_RANGES_VERSION_V1)
    {
        printf("====Concurrent Positioning Ranges====\n");
        printf("\nRange#\t#Elements\t          Lowest LBA     \t   # of LBAs      \n");
        for (uint8_t rangeCounter = 0; rangeCounter < ranges->numberOfRanges && rangeCounter < 15; ++rangeCounter)
        {
            if (ranges->range[rangeCounter].numberOfStorageElements)
            {
                printf("  %2" PRIu8 "  \t   %3" PRIu8 "   \t%20" PRIu64 "\t%20" PRIu64"\n", ranges->range[rangeCounter].rangeNumber, ranges->range[rangeCounter].numberOfStorageElements, ranges->range[rangeCounter].lowestLBA, ranges->range[rangeCounter].numberOfLBAs);
            }
            else
            {
                printf("  %2" PRIu8 "  \t   N/A   \t%20" PRIu64 "\t%20" PRIu64"\n", ranges->range[rangeCounter].rangeNumber, ranges->range[rangeCounter].lowestLBA, ranges->range[rangeCounter].numberOfLBAs);
            }
        }
    }
    else
    {
        printf("ERROR: Incompatible concurrent ranges data structure. Cannot print the data.\n");
    }
}
