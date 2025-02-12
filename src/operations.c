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
// \file operations.c   Implementation for generic ATA/SCSI functions
//                     The intention of the file is to be generic & not OS specific

#include "bit_manip.h"
#include "code_attributes.h"
#include "common_types.h"
#include "error_translation.h"
#include "io_utils.h"
#include "math_utils.h"
#include "memory_safety.h"
#include "precision_timer.h"
#include "string_utils.h"
#include "time_utils.h"
#include "type_conversion.h"
#include "unit_conversion.h"

#include "ata_helper_func.h"
#include "nvme_helper_func.h"
#include "operations.h"
#include "operations_Common.h"
#include "scsi_helper_func.h"

// headers below are for determining quickest erase
#include "ata_Security.h"
#include "dst.h"
#include "format.h"
#include "logs.h" //for SCSI mode pages
#include "sanitize.h"
#include "trim_unmap.h"
#include "writesame.h"

eReturnValues get_Ready_LED_State(tDevice* device, bool* readyLEDOnOff)
{
    eReturnValues ret = UNKNOWN;
    if (device->drive_info.drive_type == SCSI_DRIVE)
    {
        uint8_t* modeSense =
            M_REINTERPRET_CAST(uint8_t*, safe_calloc_aligned(24, sizeof(uint8_t), device->os_info.minimumAlignment));
        if (modeSense == M_NULLPTR)
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
        safe_free_aligned(&modeSense);
    }
    else // ata cannot control ready LED since it is managed by the host, not the drive (drive just reads a signal to
         // change operation as per ATA spec). Not sure if other device types support this change or not at this time.
    {
        ret = NOT_SUPPORTED;
    }
    return ret;
}

eReturnValues change_Ready_LED(tDevice* device, bool readyLEDDefault, bool readyLEDOnOff)
{
    eReturnValues ret = UNKNOWN;
    if (device->drive_info.drive_type == SCSI_DRIVE)
    {
        uint8_t* modeSelect =
            M_REINTERPRET_CAST(uint8_t*, safe_calloc_aligned(24, sizeof(uint8_t), device->os_info.minimumAlignment));
        if (modeSelect == M_NULLPTR)
        {
            perror("calloc failure!");
            return MEMORY_FAILURE;
        }
        if (readyLEDDefault)
        {
            // we need to read the default AND current page this way we only touch 1 bit on the page
            if (SUCCESS == scsi_Mode_Sense_10(device, 0x19, 24, 0, true, false, MPC_DEFAULT_VALUES, modeSelect))
            {
                if (modeSelect[2 + MODE_PARAMETER_HEADER_10_LEN] & BIT4)
                {
                    readyLEDOnOff = true; // set to true so that we turn the bit on
                }
            }
            safe_memset(modeSelect, 24, 0, 24);
        }
        if (SUCCESS == scsi_Mode_Sense_10(device, 0x19, 24, 0, true, false, MPC_CURRENT_VALUES, modeSelect))
        {
            if (readyLEDOnOff) // set the bit to 1
            {
                modeSelect[2 + MODE_PARAMETER_HEADER_10_LEN] |= BIT4;
            }
            else // set the bit to 0 if it isn't already 0
            {
                if (modeSelect[2 + MODE_PARAMETER_HEADER_10_LEN] & BIT4)
                {
                    modeSelect[2 + MODE_PARAMETER_HEADER_10_LEN] ^= BIT4;
                }
            }
            // mode data length
            modeSelect[0] = 0;
            modeSelect[1] = 0x10; // 16
            // medium type
            modeSelect[2] = 0;
            // device specific
            modeSelect[3] = 0;
            // reserved and LongLBA bit
            modeSelect[4] = RESERVED;
            // reserved
            modeSelect[5] = RESERVED;
            // block desciptor length
            modeSelect[6] = 0;
            modeSelect[7] = 0;
            // send the mode select command
            ret = scsi_Mode_Select_10(device, 24, true, true, false, modeSelect, 24);
        }
        safe_free_aligned(&modeSelect);
    }
    else // ata cannot control ready LED since it is managed by the host, not the drive (drive just reads a signal to
         // change operation as per ATA spec). Not sure if other device types support this change or not at this time.
    {
        ret = NOT_SUPPORTED;
    }
    return ret;
}

// SBC spec. Caching Mode Page NV_DIS
eReturnValues scsi_Set_NV_DIS(tDevice* device, bool nv_disEnableDisable)
{
    eReturnValues ret = UNKNOWN;

    if (device->drive_info.drive_type != SCSI_DRIVE)
    {
        return NOT_SUPPORTED;
    }
    // on SAS we change this through a mode page
    uint8_t* cachingModePage =
        M_REINTERPRET_CAST(uint8_t*, safe_calloc_aligned(MP_CACHING_LEN + MODE_PARAMETER_HEADER_10_LEN, sizeof(uint8_t),
                                                         device->os_info.minimumAlignment));
    if (cachingModePage == M_NULLPTR)
    {
        perror("calloc failure!");
        return MEMORY_FAILURE;
    }
    // first read the current settings
    ret = scsi_Mode_Sense_10(device, MP_CACHING, MP_CACHING_LEN + MODE_PARAMETER_HEADER_10_LEN, 0, true, false,
                             MPC_CURRENT_VALUES, cachingModePage);
    if (ret == SUCCESS)
    {
        // set up the mode parameter header
        // mode data length
        cachingModePage[0] =
            M_Byte1(MP_CACHING_LEN + (MODE_PARAMETER_HEADER_10_LEN - 2)); // add 6 to omit the length bytes
        cachingModePage[1] = M_Byte0(MP_CACHING_LEN + (MODE_PARAMETER_HEADER_10_LEN - 2));
        // medium type
        cachingModePage[2] = 0;
        // device specific
        cachingModePage[3] = 0;
        // reserved and LongLBA bit
        cachingModePage[4] = RESERVED;
        // reserved
        cachingModePage[5] = RESERVED;
        // block desciptor length
        cachingModePage[6] = 0;
        cachingModePage[7] = 0;
        // now go change the bit to what we need it to, then send a mode select command
        if (!nv_disEnableDisable)
        {
            // Disable the NV Cache (set the NV_DIS to one)
            cachingModePage[MODE_PARAMETER_HEADER_10_LEN + 12] |= BIT0;
        }
        else
        {
            // Enable the NV Cache (set the NV_DIS to zero)
            // turn the bit off if it is already set
            if (cachingModePage[MODE_PARAMETER_HEADER_10_LEN + 12] & BIT0)
            {
                cachingModePage[MODE_PARAMETER_HEADER_10_LEN + 12] ^= BIT0;
            }
        }
        // send the mode select command
        ret = scsi_Mode_Select_10(device, MP_CACHING_LEN + MODE_PARAMETER_HEADER_10_LEN, true, true, false,
                                  cachingModePage, MP_CACHING_LEN + MODE_PARAMETER_HEADER_10_LEN);
    }
    safe_free_aligned(&cachingModePage);
    return ret;
}

eReturnValues scsi_Set_Read_Look_Ahead(tDevice* device, bool readLookAheadEnableDisable)
{
    eReturnValues ret = UNKNOWN;
    // on SAS we change this through a mode page
    uint8_t* cachingModePage =
        M_REINTERPRET_CAST(uint8_t*, safe_calloc_aligned(MP_CACHING_LEN + MODE_PARAMETER_HEADER_10_LEN, sizeof(uint8_t),
                                                         device->os_info.minimumAlignment));
    if (cachingModePage == M_NULLPTR)
    {
        perror("calloc failure!");
        return MEMORY_FAILURE;
    }
    // first read the current settings
    ret = scsi_Mode_Sense_10(device, MP_CACHING, MP_CACHING_LEN + MODE_PARAMETER_HEADER_10_LEN, 0, true, false,
                             MPC_CURRENT_VALUES, cachingModePage);
    if (ret == SUCCESS)
    {
        // set up the mode parameter header
        // mode data length
        cachingModePage[0] = M_Byte1(MP_CACHING_LEN + 6);
        cachingModePage[1] = M_Byte0(MP_CACHING_LEN + 6);
        // medium type
        cachingModePage[2] = 0;
        // device specific
        cachingModePage[3] = 0;
        // reserved and LongLBA bit
        cachingModePage[4] = RESERVED;
        // reserved
        cachingModePage[5] = RESERVED;
        // block desciptor length
        cachingModePage[6] = 0;
        cachingModePage[7] = 0;
        // now go change the bit to what we need it to, then send a mode select command
        if (readLookAheadEnableDisable == false)
        {
            cachingModePage[MODE_PARAMETER_HEADER_10_LEN + 12] |= BIT5;
        }
        else
        {
            // turn the bit off if it is already set
            if (cachingModePage[MODE_PARAMETER_HEADER_10_LEN + 12] & BIT5)
            {
                cachingModePage[MODE_PARAMETER_HEADER_10_LEN + 12] ^= BIT5;
            }
        }
        // send the mode select command
        ret = scsi_Mode_Select_10(device, MP_CACHING_LEN + MODE_PARAMETER_HEADER_10_LEN, true, true, false,
                                  cachingModePage, MP_CACHING_LEN + MODE_PARAMETER_HEADER_10_LEN);
    }
    safe_free_aligned(&cachingModePage);
    return ret;
}

eReturnValues ata_Set_Read_Look_Ahead(tDevice* device, bool readLookAheadEnableDisable)
{
    eReturnValues ret = UNKNOWN;
    // on ata, we just send a set features command to change this
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

eReturnValues set_Read_Look_Ahead(tDevice* device, bool readLookAheadEnableDisable)
{
    eReturnValues ret = UNKNOWN;
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

eReturnValues scsi_Set_Write_Cache(tDevice* device, bool writeCacheEnableDisable)
{
    eReturnValues ret = UNKNOWN;
    // on SAS we change this through a mode page
    uint8_t* cachingModePage =
        M_REINTERPRET_CAST(uint8_t*, safe_calloc_aligned(MP_CACHING_LEN + MODE_PARAMETER_HEADER_10_LEN, sizeof(uint8_t),
                                                         device->os_info.minimumAlignment));
    if (cachingModePage == M_NULLPTR)
    {
        perror("calloc failure!");
        return MEMORY_FAILURE;
    }
    // first read the current settings
    ret = scsi_Mode_Sense_10(device, MP_CACHING, MP_CACHING_LEN + MODE_PARAMETER_HEADER_10_LEN, 0, true, false,
                             MPC_CURRENT_VALUES, cachingModePage);
    if (ret == SUCCESS)
    {
        // set up the mode parameter header
        // mode data length
        cachingModePage[0] = M_Byte1(MP_CACHING_LEN + 6);
        cachingModePage[1] = M_Byte0(MP_CACHING_LEN + 6);
        // medium type
        cachingModePage[2] = 0;
        // device specific
        cachingModePage[3] = 0;
        // reserved and LongLBA bit
        cachingModePage[4] = RESERVED;
        // reserved
        cachingModePage[5] = RESERVED;
        // block desciptor length
        cachingModePage[6] = 0;
        cachingModePage[7] = 0;
        // now go change the bit to what we need it to, then send a mode select command
        if (writeCacheEnableDisable == true)
        {
            cachingModePage[MODE_PARAMETER_HEADER_10_LEN + 2] |= BIT2;
        }
        else
        {
            // turn the bit off if it is already set
            if (cachingModePage[MODE_PARAMETER_HEADER_10_LEN + 2] & BIT2)
            {
                cachingModePage[MODE_PARAMETER_HEADER_10_LEN + 2] ^= BIT2;
            }
        }
        // send the mode select command
        ret = scsi_Mode_Select_10(device, MP_CACHING_LEN + MODE_PARAMETER_HEADER_10_LEN, true, true, false,
                                  cachingModePage, MP_CACHING_LEN + MODE_PARAMETER_HEADER_10_LEN);
    }
    safe_free_aligned(&cachingModePage);
    return ret;
}

eReturnValues ata_Set_Write_Cache(tDevice* device, bool writeCacheEnableDisable)
{
    eReturnValues ret = UNKNOWN;
    // on ata, we just send a set features command to change this
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

eReturnValues nvme_Set_Write_Cache(tDevice* device, bool writeCacheEnableDisable)
{
    eReturnValues ret = NOT_SUPPORTED;
    if (device->drive_info.IdentifyData.nvme.ctrl.vwc &
        BIT0) // This bit must be set to 1 to control whether write caching is enabled or disabled.
    {
        nvmeFeaturesCmdOpt featuresOptions;
        safe_memset(&featuresOptions, sizeof(nvmeFeaturesCmdOpt), 0, sizeof(nvmeFeaturesCmdOpt));
        if (writeCacheEnableDisable)
        {
            featuresOptions.featSetGetValue = BIT0;
        }
        featuresOptions.fid = NVME_FEAT_VOLATILE_WC_;
        // featuresOptions.sv //TODO: take extra "volatile" parameter? The drive may or may not support saving this
        // accross power cycles (see spec). - TJE
        ret = nvme_Set_Features(device, &featuresOptions);
    }
    return ret;
}

eReturnValues set_Write_Cache(tDevice* device, bool writeCacheEnableDisable)
{
    eReturnValues ret = UNKNOWN;
    switch (device->drive_info.drive_type)
    {
    case NVME_DRIVE:
        ret = nvme_Set_Write_Cache(device, writeCacheEnableDisable);
        break;
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

bool is_Read_Look_Ahead_Supported(tDevice* device)
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

// NOTE: this uses the RCD bit. Old drives do not support this bit. Checking the changable values to detect support
// before trying to change it.
bool scsi_Is_Read_Look_Ahead_Supported(tDevice* device)
{
    bool supported = false;
    // on SAS we change this through a mode page
    uint8_t* cachingModePage =
        M_REINTERPRET_CAST(uint8_t*, safe_calloc_aligned(MP_CACHING_LEN + MODE_PARAMETER_HEADER_10_LEN, sizeof(uint8_t),
                                                         device->os_info.minimumAlignment));
    if (cachingModePage == M_NULLPTR)
    {
        perror("calloc failure!");
        return false;
    }
    // if changable, then it is supported
    if (SUCCESS == scsi_Mode_Sense_10(device, MP_CACHING, MP_CACHING_LEN + MODE_PARAMETER_HEADER_10_LEN, 0, true, false,
                                      MPC_CHANGABLE_VALUES, cachingModePage))
    {
        // check the offset to see if the bit is set.
        if (cachingModePage[MODE_PARAMETER_HEADER_10_LEN + 12] & BIT5)
        {
            supported = true;
        }
    }
    safe_memset(cachingModePage, MP_CACHING_LEN + MODE_PARAMETER_HEADER_10_LEN, 0,
                MP_CACHING_LEN + MODE_PARAMETER_HEADER_10_LEN);
    // check default to see if it is enabled and just cannot be disabled (unlikely)
    if (!supported && SUCCESS == scsi_Mode_Sense_10(device, MP_CACHING, MP_CACHING_LEN + MODE_PARAMETER_HEADER_10_LEN,
                                                    0, true, false, MPC_DEFAULT_VALUES, cachingModePage))
    {
        // check the offset to see if the bit is set.
        if (!(cachingModePage[MODE_PARAMETER_HEADER_10_LEN + 12] & BIT5))
        {
            supported = true; // if it is enabled by default, then it's supported
        }
    }
    safe_free_aligned(&cachingModePage);
    return supported;
}

bool ata_Is_Read_Look_Ahead_Supported(tDevice* device)
{
    bool supported = false;
    if (is_ATA_Identify_Word_Valid(le16_to_host(device->drive_info.IdentifyData.ata.Word082)) &&
        le16_to_host(device->drive_info.IdentifyData.ata.Word082) & BIT6)
    {
        supported = true;
    }
    return supported;
}

bool is_NV_Cache_Enabled(tDevice* device)
{
    if (device->drive_info.drive_type == SCSI_DRIVE)
    {
        return !scsi_is_NV_DIS_Bit_Set(device); // since this function returns when the bit is set to 1 (meaning cache
                                                // disabled), then we need to flip that bit for this use.
    }
    // Not sure if ATA or NVMe support this.
    return false;
}

bool is_Read_Look_Ahead_Enabled(tDevice* device)
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

// SPC3 added this page, but the NV_DIS bit is on the caching mode page.
bool scsi_Is_NV_Cache_Supported(tDevice* device)
{
    bool supported = false;
    // check the extended inquiry data for the NV_SUP bit
    DECLARE_ZERO_INIT_ARRAY(uint8_t, extInq, VPD_EXTENDED_INQUIRY_LEN);
    if (SUCCESS == scsi_Inquiry(device, extInq, VPD_EXTENDED_INQUIRY_LEN, EXTENDED_INQUIRY_DATA, true, false))
    {
        if (extInq[6] & BIT1)
        {
            supported = true;
        }
    }
    return supported;
}

bool is_NV_Cache_Supported(tDevice* device)
{
    if (device->drive_info.drive_type == SCSI_DRIVE)
    {
        return scsi_Is_NV_Cache_Supported(device);
    }
    return false;
}

bool scsi_is_NV_DIS_Bit_Set(tDevice* device)
{
    bool enabled = false;
    // on SAS we change this through a mode page
    uint8_t* cachingModePage =
        M_REINTERPRET_CAST(uint8_t*, safe_calloc_aligned(MP_CACHING_LEN + MODE_PARAMETER_HEADER_10_LEN, sizeof(uint8_t),
                                                         device->os_info.minimumAlignment));
    if (cachingModePage == M_NULLPTR)
    {
        perror("calloc failure!");
        return false;
    }
    // first read the current settings
    if (SUCCESS == scsi_Mode_Sense_10(device, MP_CACHING, MP_CACHING_LEN + MODE_PARAMETER_HEADER_10_LEN, 0, true, false,
                                      MPC_CURRENT_VALUES, cachingModePage))
    {
        // check the offset to see if the bit is set.
        if (cachingModePage[MODE_PARAMETER_HEADER_10_LEN + 12] & BIT0)
        {
            // This means that the NV cache is disabled when this bit is set to 1
            enabled = true;
        }
        else
        {
            enabled = false;
        }
    }
    safe_free_aligned(&cachingModePage);
    return enabled;
}

bool scsi_Is_Read_Look_Ahead_Enabled(tDevice* device)
{
    bool enabled = false;
    // on SAS we change this through a mode page
    uint8_t* cachingModePage =
        M_REINTERPRET_CAST(uint8_t*, safe_calloc_aligned(MP_CACHING_LEN + MODE_PARAMETER_HEADER_10_LEN, sizeof(uint8_t),
                                                         device->os_info.minimumAlignment));
    if (cachingModePage == M_NULLPTR)
    {
        perror("calloc failure!");
        return false;
    }
    // first read the current settings
    if (SUCCESS == scsi_Mode_Sense_10(device, MP_CACHING, MP_CACHING_LEN + MODE_PARAMETER_HEADER_10_LEN, 0, true, false,
                                      MPC_CURRENT_VALUES, cachingModePage))
    {
        // check the offset to see if the bit is set.
        if (cachingModePage[MODE_PARAMETER_HEADER_10_LEN + 12] & BIT5)
        {
            enabled = false;
        }
        else
        {
            enabled = true;
        }
    }
    safe_free_aligned(&cachingModePage);
    return enabled;
}

bool ata_Is_Read_Look_Ahead_Enabled(tDevice* device)
{
    bool enabled = false;
    if (is_ATA_Identify_Word_Valid(le16_to_host(device->drive_info.IdentifyData.ata.Word085)) &&
        le16_to_host(device->drive_info.IdentifyData.ata.Word085) & BIT6)
    {
        enabled = true;
    }
    return enabled;
}

bool nvme_Is_Write_Cache_Supported(tDevice* device)
{
    bool supported = false;
    if (device->drive_info.IdentifyData.nvme.ctrl.vwc &
        BIT0) // This bit must be set to 1 to control whether write caching is enabled or disabled.
    {
        supported = true;
    }
    return supported;
}

bool is_Write_Cache_Supported(tDevice* device)
{
    switch (device->drive_info.drive_type)
    {
    case NVME_DRIVE:
        return nvme_Is_Write_Cache_Supported(device);
    case SCSI_DRIVE:
        return scsi_Is_Write_Cache_Supported(device);
    case ATA_DRIVE:
        return ata_Is_Write_Cache_Supported(device);
    default:
        break;
    }
    return false;
}

bool scsi_Is_Write_Cache_Supported(tDevice* device)
{
    bool supported = false;
    // on SAS we change this through a mode page
    uint8_t* cachingModePage =
        M_REINTERPRET_CAST(uint8_t*, safe_calloc_aligned(MP_CACHING_LEN + MODE_PARAMETER_HEADER_10_LEN, sizeof(uint8_t),
                                                         device->os_info.minimumAlignment));
    if (cachingModePage == M_NULLPTR)
    {
        perror("calloc failure!");
        return false;
    }
    // if changable, then it is supported
    if (SUCCESS == scsi_Mode_Sense_10(device, MP_CACHING, MP_CACHING_LEN + MODE_PARAMETER_HEADER_10_LEN, 0, true, false,
                                      MPC_CHANGABLE_VALUES, cachingModePage))
    {
        // check the offset to see if the bit is set.
        if (cachingModePage[MODE_PARAMETER_HEADER_10_LEN + 2] & BIT2)
        {
            supported = true;
        }
    }
    safe_memset(cachingModePage, MP_CACHING_LEN + MODE_PARAMETER_HEADER_10_LEN, 0,
                MP_CACHING_LEN + MODE_PARAMETER_HEADER_10_LEN);
    // check default to see if it is enabled and just cannot be disabled (unlikely)
    if (!supported && SUCCESS == scsi_Mode_Sense_10(device, MP_CACHING, MP_CACHING_LEN + MODE_PARAMETER_HEADER_10_LEN,
                                                    0, true, false, MPC_DEFAULT_VALUES, cachingModePage))
    {
        // check the offset to see if the bit is set.
        if (cachingModePage[MODE_PARAMETER_HEADER_10_LEN + 2] & BIT2)
        {
            supported = true; // if it is enabled by default, then it's supported
        }
    }
    safe_free_aligned(&cachingModePage);
    return supported;
}

bool ata_Is_Write_Cache_Supported(tDevice* device)
{
    bool supported = false;
    if (is_ATA_Identify_Word_Valid(le16_to_host(device->drive_info.IdentifyData.ata.Word082)) &&
        le16_to_host(device->drive_info.IdentifyData.ata.Word082) & BIT5)
    {
        supported = true;
    }
    return supported;
}

bool nvme_Is_Write_Cache_Enabled(tDevice* device)
{
    bool enabled = false;
    if (device->drive_info.IdentifyData.nvme.ctrl.vwc &
        BIT0) // This bit must be set to 1 to control whether write caching is enabled or disabled.
    {
        // get the feature identifier
        nvmeFeaturesCmdOpt featuresOptions;
        safe_memset(&featuresOptions, sizeof(nvmeFeaturesCmdOpt), 0, sizeof(nvmeFeaturesCmdOpt));
        featuresOptions.fid = NVME_FEAT_VOLATILE_WC_;
        featuresOptions.sel = 0; // getting current settings
        if (SUCCESS == nvme_Get_Features(device, &featuresOptions))
        {
            enabled = featuresOptions.featSetGetValue & BIT0;
        }
    }
    return enabled;
}

bool is_Write_Cache_Enabled(tDevice* device)
{
    switch (device->drive_info.drive_type)
    {
    case NVME_DRIVE:
        return nvme_Is_Write_Cache_Enabled(device);
    case SCSI_DRIVE:
        return scsi_Is_Write_Cache_Enabled(device);
    case ATA_DRIVE:
        return ata_Is_Write_Cache_Enabled(device);
    default:
        break;
    }
    return false;
}

bool scsi_Is_Write_Cache_Enabled(tDevice* device)
{
    bool enabled = false;
    // on SAS we change this through a mode page
    uint8_t* cachingModePage =
        M_REINTERPRET_CAST(uint8_t*, safe_calloc_aligned(MP_CACHING_LEN + MODE_PARAMETER_HEADER_10_LEN, sizeof(uint8_t),
                                                         device->os_info.minimumAlignment));
    if (cachingModePage == M_NULLPTR)
    {
        perror("calloc failure!");
        return false;
    }
    // first read the current settings
    if (SUCCESS == scsi_Mode_Sense_10(device, MP_CACHING, MP_CACHING_LEN + MODE_PARAMETER_HEADER_10_LEN, 0, true, false,
                                      MPC_CURRENT_VALUES, cachingModePage))
    {
        // check the offset to see if the bit is set.
        if (cachingModePage[MODE_PARAMETER_HEADER_10_LEN + 2] & BIT2)
        {
            enabled = true;
        }
        else
        {
            enabled = false;
        }
    }
    safe_free_aligned(&cachingModePage);
    return enabled;
}

bool ata_Is_Write_Cache_Enabled(tDevice* device)
{
    bool enabled = false;
    if (is_ATA_Identify_Word_Valid(le16_to_host(device->drive_info.IdentifyData.ata.Word085)) &&
        le16_to_host(device->drive_info.IdentifyData.ata.Word085) & BIT5)
    {
        enabled = true;
    }
    return enabled;
}

eReturnValues is_Write_After_Erase_Required(tDevice* device, ptrWriteAfterErase writeReq)
{
    eReturnValues ret = NOT_SUPPORTED;
    DISABLE_NONNULL_COMPARE
    if (device->drive_info.drive_type == SCSI_DRIVE && !device->drive_info.passThroughHacks.scsiHacks.noVPDPages)
    {
        ret = SUCCESS;
        if (writeReq == M_NULLPTR)
        {
            return BAD_PARAMETER;
        }
        // read the block device characteristics VPD page
        DECLARE_ZERO_INIT_ARRAY(uint8_t, blockCharacteristics, VPD_BLOCK_DEVICE_CHARACTERISTICS_LEN);
        if (SUCCESS == scsi_Inquiry(device, blockCharacteristics, VPD_BLOCK_DEVICE_CHARACTERISTICS_LEN,
                                    BLOCK_DEVICE_CHARACTERISTICS, true, false))
        {
            writeReq->blockErase  = C_CAST(eWriteAfterEraseReq, get_bit_range_uint8(blockCharacteristics[7], 7, 6));
            writeReq->cryptoErase = C_CAST(eWriteAfterEraseReq, get_bit_range_uint8(blockCharacteristics[7], 5, 4));
            if ((writeReq->cryptoErase <= WAEREQ_READ_COMPLETES_GOOD_STATUS ||
                 writeReq->blockErase <= WAEREQ_READ_COMPLETES_GOOD_STATUS) &&
                device->drive_info.currentProtectionType > 0)
            {
                // A device formatted with protection may require an erase.
                // So we need to check if the device supports logical block provisioning management.
                // If it does, we are done, but otherwise we need to set a flag for may require an overwrite.
                // Devices that support logical block provisioning will not require an overwrite because they
                // automatically unmap at the end of crypto or block erase which resets the PI bytes and does not cause
                // a read conflict. -TJE NOTE: It is possible for a vendor unique behavior on other devices to allow
                // reading after these, but we have no way of detecting that -TJE In SBC, a device supporting this shall
                // support the logical block provisioning VPD page...so just try requesting that first.
                bool needPIWriteAfterErase = true;
                DECLARE_ZERO_INIT_ARRAY(uint8_t, logicalBlockProvisioning, VPD_LOGICAL_BLOCK_PROVISIONING_LEN);
                if (SUCCESS != scsi_Inquiry(device, logicalBlockProvisioning, VPD_LOGICAL_BLOCK_PROVISIONING_LEN,
                                            LOGICAL_BLOCK_PROVISIONING, true, false))
                {
                    needPIWriteAfterErase = false;
                }
                else
                {
                    // check if lbpu, lbpws, or lbpws10 are set since this can indicate support for provisioning. If
                    // none are set, provisioning is not supported.
                    if (get_bit_range_uint8(logicalBlockProvisioning[5], 7, 5) > 0)
                    {
                        needPIWriteAfterErase = false;
                    }
                }
                if (needPIWriteAfterErase)
                {
                    if (writeReq->cryptoErase != WAEREQ_NOT_SPECIFIED)
                    {
                        // only change when this is set to some other value because that can help to set this only when
                        // crypto is supported
                        writeReq->cryptoErase = WAEREQ_PI_FORMATTED_MAY_REQUIRE_OVERWRITE;
                    }
                    if (writeReq->blockErase != WAEREQ_NOT_SPECIFIED)
                    {
                        // only change when this is set to some other value because that can help to set this only when
                        // block is supported
                        writeReq->blockErase = WAEREQ_PI_FORMATTED_MAY_REQUIRE_OVERWRITE;
                    }
                }
            }
        }
    }
    else if (writeReq != M_NULLPTR)
    {
        writeReq->cryptoErase = WAEREQ_NOT_SPECIFIED;
        writeReq->blockErase  = WAEREQ_NOT_SPECIFIED;
    }
    RESTORE_NONNULL_COMPARE
    return ret;
}

// erase weights are hard coded right now....-TJE
eReturnValues get_Supported_Erase_Methods(tDevice*    device,
                                          eraseMethod eraseMethodList[MAX_SUPPORTED_ERASE_METHODS],
                                          uint32_t*   overwriteEraseTimeEstimateMinutes)
{
    eReturnValues             ret = SUCCESS;
    ataSecurityStatus         ataSecurityInfo;
    sanitizeFeaturesSupported sanitizeInfo;
    nvmeFormatSupport         nvmeFormatInfo;
    writeAfterErase           writeAfterEraseRequirements;
    uint64_t                  maxNumberOfLogicalBlocksPerCommand = UINT64_C(0);
    bool                      formatUnitAdded                    = false;
    bool                      nvmFormatAdded                     = false;
    bool isWriteSameSupported  = is_Write_Same_Supported(device, 0, C_CAST(uint32_t, device->drive_info.deviceMaxLba),
                                                         &maxNumberOfLogicalBlocksPerCommand);
    bool isFormatUnitSupported = is_Format_Unit_Supported(device, M_NULLPTR);
    eraseMethod* currentErase  = C_CAST(eraseMethod*, eraseMethodList);
    if (currentErase == M_NULLPTR)
    {
        return BAD_PARAMETER;
    }
    if (overwriteEraseTimeEstimateMinutes != M_NULLPTR)
    {
        *overwriteEraseTimeEstimateMinutes = UINT32_C(0); // start off with zero
    }
    safe_memset(&sanitizeInfo, sizeof(sanitizeFeaturesSupported), 0, sizeof(sanitizeFeaturesSupported));
    safe_memset(&ataSecurityInfo, sizeof(ataSecurityStatus), 0, sizeof(ataSecurityStatus));
    safe_memset(&nvmeFormatInfo, sizeof(nvmeFormatSupport), 0, sizeof(nvmeFormatSupport));
    safe_memset(&writeAfterEraseRequirements, sizeof(writeAfterErase), 0, sizeof(writeAfterErase));
    // first make sure the list is initialized to all 1's (to help sorting later)
    safe_memset(currentErase, sizeof(eraseMethod) * MAX_SUPPORTED_ERASE_METHODS, 0xFF,
                sizeof(eraseMethod) * MAX_SUPPORTED_ERASE_METHODS);

    get_Sanitize_Device_Features(device, &sanitizeInfo);

    get_ATA_Security_Info(device, &ataSecurityInfo, sat_ATA_Security_Protocol_Supported(device));

    get_NVMe_Format_Support(device, &nvmeFormatInfo);

    is_Write_After_Erase_Required(device, &writeAfterEraseRequirements);

    // fastest will be sanitize crypto
    if (sanitizeInfo.crypto)
    {
        DECLARE_ZERO_INIT_ARRAY(char, sanitizeWarning, MAX_ERASE_WARNING_LENGTH);
        if (writeAfterEraseRequirements.cryptoErase >= WAEREQ_MEDIUM_ERROR_OTHER_ASC)
        {
            if (writeAfterEraseRequirements.cryptoErase == WAEREQ_PI_FORMATTED_MAY_REQUIRE_OVERWRITE)
            {
                snprintf_err_handle(sanitizeWarning, MAX_ERASE_WARNING_LENGTH,
                                    "PI formatting may require write after crypto erase.");
            }
            else
            {
                snprintf_err_handle(sanitizeWarning, MAX_ERASE_WARNING_LENGTH,
                                    "Cannot be stopped, even with a power cycle. Write after crypto erase required.");
            }
        }
        else
        {
            snprintf_err_handle(sanitizeWarning, MAX_ERASE_WARNING_LENGTH,
                                "Cannot be stopped, even with a power cycle.");
        }
        currentErase->eraseIdentifier = ERASE_SANITIZE_CRYPTO;
        snprintf_err_handle(currentErase->eraseName, MAX_ERASE_NAME_LENGTH, "Sanitize Crypto Erase");
        snprintf_err_handle(currentErase->eraseWarning, MAX_ERASE_WARNING_LENGTH, "%s", sanitizeWarning);
        currentErase->warningValid      = true;
        currentErase->eraseWeight       = 0;
        currentErase->sanitizationLevel = ERASE_SANITIZATION_PURGE;
        ++currentErase;
    }

    // next sanitize block erase
    if (sanitizeInfo.blockErase)
    {
        DECLARE_ZERO_INIT_ARRAY(char, sanitizeWarning, MAX_ERASE_WARNING_LENGTH);
        if (writeAfterEraseRequirements.blockErase >= WAEREQ_MEDIUM_ERROR_OTHER_ASC)
        {
            if (writeAfterEraseRequirements.blockErase == WAEREQ_PI_FORMATTED_MAY_REQUIRE_OVERWRITE)
            {
                snprintf_err_handle(sanitizeWarning, MAX_ERASE_WARNING_LENGTH,
                                    "PI formatting may require write after block erase.");
            }
            else
            {
                snprintf_err_handle(sanitizeWarning, MAX_ERASE_WARNING_LENGTH,
                                    "Cannot be stopped, even with a power cycle. Write after block erase required.");
            }
        }
        else
        {
            snprintf_err_handle(sanitizeWarning, MAX_ERASE_WARNING_LENGTH,
                                "Cannot be stopped, even with a power cycle.");
        }
        currentErase->eraseIdentifier = ERASE_SANITIZE_BLOCK;
        snprintf_err_handle(currentErase->eraseName, MAX_ERASE_NAME_LENGTH, "Sanitize Block Erase");
        snprintf_err_handle(currentErase->eraseWarning, MAX_ERASE_WARNING_LENGTH, "%s", sanitizeWarning);
        currentErase->warningValid      = true;
        currentErase->eraseWeight       = 1;
        currentErase->sanitizationLevel = ERASE_SANITIZATION_PURGE;
        ++currentErase;
    }

    // format on SAS SSD will take only a couple seconds since it basically does a unmap operation
    if (isFormatUnitSupported && device->drive_info.drive_type == SCSI_DRIVE && !formatUnitAdded && is_SSD(device))
    {
        currentErase->eraseIdentifier = ERASE_FORMAT_UNIT;
        snprintf_err_handle(currentErase->eraseName, MAX_ERASE_NAME_LENGTH, "Format Unit");
        snprintf_err_handle(currentErase->eraseWarning, MAX_ERASE_WARNING_LENGTH,
                            "If interrupted, must be restarted from the beginning.");
        currentErase->warningValid      = true;
        currentErase->eraseWeight       = 2;
        currentErase->sanitizationLevel = ERASE_SANITIZATION_CLEAR; // While an SSD may do a block erase, there is no
                                                                    // guarantee it is the same as Sanitize.-TJE
        ++currentErase;
        formatUnitAdded = true;
    }

    if (device->drive_info.drive_type == NVME_DRIVE && nvmeFormatInfo.formatCommandSupported)
    {
        // next up for NVMe is to list the format with user and crypto erase support.
        if (nvmeFormatInfo.cryptographicEraseSupported)
        {
            currentErase->eraseIdentifier = ERASE_NVM_FORMAT_CRYPTO_SECURE_ERASE;
            snprintf_err_handle(currentErase->eraseName, MAX_ERASE_NAME_LENGTH, "NVM Format: Crypto Erase");
            currentErase->eraseWeight       = 0;
            currentErase->warningValid      = false;
            currentErase->sanitizationLevel = ERASE_SANITIZATION_POSSIBLE_PURGE;
            // NOTE: We can create a list of known devices capable of meeting purge and a list of those know to meet
            // clear to set this more clearly
            ++currentErase;
        }
        // if NOT an NVM HDD, user erase should be next since it will most likely be as fast as a sanitize block erase
        if (is_SSD(device))
        {
            currentErase->eraseIdentifier = ERASE_NVM_FORMAT_USER_SECURE_ERASE;
            snprintf_err_handle(currentErase->eraseName, MAX_ERASE_NAME_LENGTH, "NVM Format: User Data Erase");
            currentErase->eraseWeight  = 1;
            currentErase->warningValid = false;
            currentErase->sanitizationLevel =
                ERASE_SANITIZATION_CLEAR; // only crypto erase is a possible purge in IEEE2883-2022
            // NOTE: We can create a list of known devices capable of meeting purge and a list of those know to meet
            // clear to set this more clearly
            ++currentErase;
            nvmFormatAdded = true;
        }
    }

    // trim/unmap/deallocate are not allowed since they are "hints" that those LBAs are not needed rather than
    // guaranteed erasure of those blocks

    // This weight value is reserved for TCG revert (this is placed in another library)

    bool enhancedEraseAddedToList = false;
    // maybe enhanced ata security erase (check time...if it's set to 2 minutes, then that is the lowest possible time
    // and likely a TCG drive doing a crypto erase)
    if (ataSecurityInfo.enhancedEraseSupported && ataSecurityInfo.enhancedSecurityEraseUnitTimeMinutes == 2)
    {
        enhancedEraseAddedToList      = true;
        currentErase->eraseIdentifier = ERASE_ATA_SECURITY_ENHANCED;
        snprintf_err_handle(currentErase->eraseName, MAX_ERASE_NAME_LENGTH, "ATA Enhanced Security Erase");
        if (ataSecurityInfo.securityEnabled)
        {
            snprintf_err_handle(currentErase->eraseWarning, MAX_ERASE_WARNING_LENGTH,
                                "Use the password set in the BIOS/UEFI or disable it from BIOS/UEFI.");
        }
        else
        {
            snprintf_err_handle(currentErase->eraseWarning, MAX_ERASE_WARNING_LENGTH,
                                "Requires setting device password. Password cleared upon success.");
        }
        currentErase->warningValid      = true;
        currentErase->eraseWeight       = 5;
        currentErase->sanitizationLevel = ERASE_SANITIZATION_PURGE;
        ++currentErase;
    }

    // this weight value is reserved for TCG revertSP (this is placed in another library)

    // sanitize overwrite
    if (sanitizeInfo.overwrite)
    {
        currentErase->eraseIdentifier = ERASE_SANITIZE_OVERWRITE;
        snprintf_err_handle(currentErase->eraseName, MAX_ERASE_NAME_LENGTH, "Sanitize Overwrite Erase");
        snprintf_err_handle(currentErase->eraseWarning, MAX_ERASE_WARNING_LENGTH,
                            "Cannot be stopped, even with a power cycle.");
        currentErase->warningValid      = true;
        currentErase->eraseWeight       = 7;
        currentErase->sanitizationLevel = ERASE_SANITIZATION_PURGE;
        ++currentErase;
    }

    // format unit (I put this above write same since on SAS, we cannot get progress indication from write same)
    if (isFormatUnitSupported && device->drive_info.drive_type == SCSI_DRIVE && !formatUnitAdded)
    {
        currentErase->eraseIdentifier = ERASE_FORMAT_UNIT;
        snprintf_err_handle(currentErase->eraseName, MAX_ERASE_NAME_LENGTH, "Format Unit");
        snprintf_err_handle(currentErase->eraseWarning, MAX_ERASE_WARNING_LENGTH,
                            "If interupted, must be restarted from the beginning.");
        currentErase->warningValid = true;
        currentErase->eraseWeight  = 8;
        currentErase->sanitizationLevel =
            ERASE_SANITIZATION_CLEAR; // If security initialize is supported in format it can be a purge. Seagate drives
                                      // do not support this bit, other vendors might.
        ++currentErase;
        formatUnitAdded = true;
    }

    if (device->drive_info.drive_type == NVME_DRIVE && nvmeFormatInfo.formatCommandSupported && !nvmFormatAdded)
    {
        currentErase->eraseIdentifier = ERASE_NVM_FORMAT_USER_SECURE_ERASE;
        snprintf_err_handle(currentErase->eraseName, MAX_ERASE_NAME_LENGTH, "NVM Format: User Data Erase");
        currentErase->eraseWeight = 8; // assuming that this will do a full drive overwrite format which will be slow
        // NOTE: If crypto is supported, a request for user secure erase may run a crypto erase, but no way to know for
        // sure-TJE
        currentErase->warningValid      = false;
        currentErase->sanitizationLevel = ERASE_SANITIZATION_CLEAR;
        // Note: We can create a list of known devices capable of meeting purge and a list of those know to meet clear
        // to set this more clearly
        ++currentErase;
        nvmFormatAdded = true;
    }

    // write same
    if (isWriteSameSupported)
    {
        currentErase->eraseIdentifier = ERASE_WRITE_SAME;
        snprintf_err_handle(currentErase->eraseName, MAX_ERASE_NAME_LENGTH, "Write Same Erase");
        snprintf_err_handle(currentErase->eraseWarning, MAX_ERASE_WARNING_LENGTH,
                            "Host may abort erase with disc access.");
        currentErase->warningValid      = true;
        currentErase->eraseWeight       = 9;
        currentErase->sanitizationLevel = ERASE_SANITIZATION_CLEAR;
        ++currentErase;
    }

    // ata security - normal &| enhanced (check times)
    if (ataSecurityInfo.securitySupported)
    {
        if (!enhancedEraseAddedToList && ataSecurityInfo.enhancedEraseSupported &&
            ataSecurityInfo.enhancedSecurityEraseUnitTimeMinutes <= ataSecurityInfo.securityEraseUnitTimeMinutes)
        {
            enhancedEraseAddedToList = true;
            // add enhanced erase
            enhancedEraseAddedToList      = true;
            currentErase->eraseIdentifier = ERASE_ATA_SECURITY_ENHANCED;
            snprintf_err_handle(currentErase->eraseName, MAX_ERASE_NAME_LENGTH, "ATA Enhanced Security Erase");
            if (ataSecurityInfo.securityEnabled)
            {
                snprintf_err_handle(currentErase->eraseWarning, MAX_ERASE_WARNING_LENGTH,
                                    "Use the password set in the BIOS/UEFI or disable it from BIOS/UEFI.");
            }
            else
            {
                snprintf_err_handle(currentErase->eraseWarning, MAX_ERASE_WARNING_LENGTH,
                                    "Requires setting device password. Password cleared upon success.");
            }
            currentErase->warningValid      = true;
            currentErase->eraseWeight       = 10;
            currentErase->sanitizationLevel = ERASE_SANITIZATION_PURGE;
            ++currentErase;
        }
        // add normal erase
        currentErase->eraseIdentifier = ERASE_ATA_SECURITY_NORMAL;
        snprintf_err_handle(currentErase->eraseName, MAX_ERASE_NAME_LENGTH, "ATA Security Erase");
        snprintf_err_handle(currentErase->eraseWarning, MAX_ERASE_WARNING_LENGTH,
                            "Requires setting device password. Password cleared upon success.");
        currentErase->warningValid      = true;
        currentErase->eraseWeight       = 11;
        currentErase->sanitizationLevel = ERASE_SANITIZATION_CLEAR;
        ++currentErase;

        // if enhanced erase has not been added, but is supported, add it here (since it's longer than short)
        if (!enhancedEraseAddedToList && ataSecurityInfo.enhancedEraseSupported &&
            ataSecurityInfo.enhancedSecurityEraseUnitTimeMinutes >= ataSecurityInfo.securityEraseUnitTimeMinutes)
        {
            enhancedEraseAddedToList = true;
            // add enhanced erase
            enhancedEraseAddedToList      = true;
            currentErase->eraseIdentifier = ERASE_ATA_SECURITY_ENHANCED;
            snprintf_err_handle(currentErase->eraseName, MAX_ERASE_NAME_LENGTH, "ATA Enhanced Security Erase");
            snprintf_err_handle(currentErase->eraseWarning, MAX_ERASE_WARNING_LENGTH,
                                "Requires setting device password. Password cleared upon success.");
            currentErase->warningValid      = true;
            currentErase->eraseWeight       = 12;
            currentErase->sanitizationLevel = ERASE_SANITIZATION_PURGE;
            ++currentErase;
        }

        // save the time estimate from NORMAL ATA Security erase if it isn't all F's
        // Using normal for the estimate since it will always be an overwrite of the disk (no crypto) and because we
        // cannot access reassigned sectors during a host overwrite, so this is the closest we'll get.-TJE
        if (overwriteEraseTimeEstimateMinutes && *overwriteEraseTimeEstimateMinutes == 0 &&
            ataSecurityInfo.securitySupported)
        {
            if (ataSecurityInfo.securityEraseUnitTimeMinutes != UINT16_MAX)
            {
                *overwriteEraseTimeEstimateMinutes = ataSecurityInfo.securityEraseUnitTimeMinutes;
            }
        }
    }

    // overwrite (always available and always the slowest)
    currentErase->eraseIdentifier = ERASE_OVERWRITE;
    snprintf_err_handle(currentErase->eraseName, MAX_ERASE_NAME_LENGTH, "Overwrite Erase");
    // snprintf_err_handle(currentErase->eraseWarning, MAX_ERASE_WARNING_LENGTH, "");
    currentErase->warningValid      = false;
    currentErase->eraseWeight       = 13;
    currentErase->sanitizationLevel = ERASE_SANITIZATION_CLEAR;
    ++currentErase;

    if (overwriteEraseTimeEstimateMinutes != M_NULLPTR) // make sure the incoming value is zero in case time was set by
                                                        // something above here (like ata security erase)
    {
        uint8_t hours   = UINT8_C(0);
        uint8_t minutes = UINT8_C(0);
        // let's set a time estimate!
        // base it off of the long DST time...as that is probably the closest match we'll get since that does access
        // every LBA
        get_Long_DST_Time(device, &hours, &minutes);
        uint32_t longDSTTimeMinutes =
            C_CAST(uint32_t, (C_CAST(uint32_t, hours) * UINT32_C(60)) + C_CAST(uint32_t, minutes));
        *overwriteEraseTimeEstimateMinutes = M_Max(*overwriteEraseTimeEstimateMinutes, longDSTTimeMinutes);
        if (*overwriteEraseTimeEstimateMinutes == 0)
        {
            // This drive doesn't support anything that gives us time estimates, so let's make a guess.
            // TODO: Make this guess better by reading the drive capabilities and interface speed to determine a more
            // accurate estimate.
            uint32_t megabytesPerSecond = is_SSD(device) ? 450 : 150; // assume 450 MB/s on SSD and 150 MB/s on HDD
            *overwriteEraseTimeEstimateMinutes = C_CAST(
                uint32_t, (C_CAST(double, (device->drive_info.deviceMaxLba * device->drive_info.deviceBlockSize)) /
                           (megabytesPerSecond * 1.049e+6)) /
                              60.0);
        }
    }

    return ret;
}

void print_Supported_Erase_Methods(tDevice*          device,
                                   eraseMethod const eraseMethodList[MAX_SUPPORTED_ERASE_METHODS],
                                   const uint32_t*   overwriteEraseTimeEstimateMinutes)
{
    uint8_t counter                     = UINT8_C(0);
    bool    cryptoSupported             = false;
    bool    sanitizeBlockEraseSupported = false;
    M_USE_UNUSED(device);
    printf("Data sanitization capabilities:\n");
    printf("\tRecommendation - Restore the MaxLBA of the device prior to any erase in\n");
    printf("\t                 order to allow the drive to erase all user addressable\n");
    printf("\t                 sectors. For ATA devices this means restoring \n");
    printf("\t                 HPA + DCO / AMAC to restore the maxLBA.\n");
    printf("\t                 Restoring the MaxLBA also allows full verification of\n");
    printf("\t                 all user addressable space on the device without a\n");
    printf("\t                 limitation from a lower maxLBA.\n");
    printf("\tClear - Logical techniques are applied to all addressable storage\n");
    printf("\t        locations, protecting against simple, non-invasive data\n");
    printf("\t        recovery techniques.\n");
    printf("\tClear, Possible Purge - Cryptographic erase is a purge if the vendor\n");
    printf("\t        implementation meets the requirements in IEEE 2883-2022.\n");
    printf("\tPurge - Logical techniques that target user data, overprovisioning,\n");
    printf("\t        unused space, and bad blocks rendering data recovery infeasible\n");
    printf("\t        even with state-of-the-art laboratory techniques.\n");
    printf("\nErase Methods supported by this drive (listed fastest to slowest):\n");
    while (counter < MAX_SUPPORTED_ERASE_METHODS)
    {
#define ERASE_SANITIZATION_CAPABILITIES_STR_LEN (24)
        DECLARE_ZERO_INIT_ARRAY(char, eraseDataCapabilities, ERASE_SANITIZATION_CAPABILITIES_STR_LEN);
        switch (eraseMethodList[counter].eraseIdentifier)
        {
        case ERASE_MAX_VALUE:
            ++counter;
            continue;
        case ERASE_SANITIZE_CRYPTO:
        case ERASE_TCG_REVERT_SP:
        case ERASE_TCG_REVERT:
        case ERASE_NVM_FORMAT_CRYPTO_SECURE_ERASE:
            cryptoSupported = true;
            break;
        case ERASE_SANITIZE_BLOCK:
            sanitizeBlockEraseSupported = true;
            break;
        case ERASE_NOT_SUPPORTED:
        case ERASE_OVERWRITE:
        case ERASE_WRITE_SAME:
        case ERASE_ATA_SECURITY_NORMAL:
        case ERASE_ATA_SECURITY_ENHANCED:
        case ERASE_OBSOLETE:
        case ERASE_FORMAT_UNIT:
        case ERASE_NVM_FORMAT_USER_SECURE_ERASE:
        case ERASE_SANITIZE_OVERWRITE:
            break;
        }
        switch (eraseMethodList[counter].sanitizationLevel)
        {
        case ERASE_SANITIZATION_UNKNOWN:
            snprintf_err_handle(eraseDataCapabilities, ERASE_SANITIZATION_CAPABILITIES_STR_LEN, "Unknown");
            break;
        case ERASE_SANITIZATION_CLEAR:
            snprintf_err_handle(eraseDataCapabilities, ERASE_SANITIZATION_CAPABILITIES_STR_LEN, "Clear");
            break;
        case ERASE_SANITIZATION_POSSIBLE_PURGE:
            snprintf_err_handle(eraseDataCapabilities, ERASE_SANITIZATION_CAPABILITIES_STR_LEN,
                                "Clear, Possible Purge");
            break;
        case ERASE_SANITIZATION_PURGE:
            snprintf_err_handle(eraseDataCapabilities, ERASE_SANITIZATION_CAPABILITIES_STR_LEN, "Purge");
            break;
        }
        if (eraseMethodList[counter].warningValid)
        {
            printf("%2" PRIu8 " %-*s (%s)\n\tNOTE: %-*s\n", counter + 1, MAX_ERASE_NAME_LENGTH,
                   eraseMethodList[counter].eraseName, eraseDataCapabilities, MAX_ERASE_WARNING_LENGTH,
                   eraseMethodList[counter].eraseWarning);
        }
        else
        {
            printf("%2" PRIu8 " %-*s (%s)\n\n", counter + 1, MAX_ERASE_NAME_LENGTH, eraseMethodList[counter].eraseName,
                   eraseDataCapabilities);
        }
        ++counter;
    }
    if (overwriteEraseTimeEstimateMinutes != M_NULLPTR)
    {
        uint16_t days    = UINT16_C(0);
        uint8_t  hours   = UINT8_C(0);
        uint8_t  minutes = UINT8_C(0);
        uint8_t  seconds = UINT8_C(0);
        convert_Seconds_To_Displayable_Time(C_CAST(uint64_t, *overwriteEraseTimeEstimateMinutes) * UINT64_C(60),
                                            M_NULLPTR, &days, &hours, &minutes, &seconds);
        // Example output:
        // The minimum time to overwrite erase this drive is approximately x days y hours z minutes.
        // The actual time may take longer. Cryptographic erase completes in seconds. Trim/Unmap & blockerase should
        // also complete in under a minute
        printf("The minimum time to overwrite erase this drive is approximately:\n\t");
        print_Time_To_Screen(M_NULLPTR, &days, &hours, &minutes, &seconds);
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
}

eReturnValues set_Sense_Data_Format(tDevice* device, bool defaultSetting, bool descriptorFormat, bool saveParameters)
{
    eReturnValues ret = NOT_SUPPORTED;
    // Change D_Sense for Control Mode page
    DECLARE_ZERO_INIT_ARRAY(uint8_t, controlModePage, MODE_PARAMETER_HEADER_10_LEN + 12);
    bool mode6ByteCmd = false;
    if (SUCCESS == scsi_Mode_Sense_10(device, MP_CONTROL, MODE_PARAMETER_HEADER_10_LEN + 12, 0, true, false,
                                      MPC_CURRENT_VALUES, controlModePage))
    {
        mode6ByteCmd = false;
    }
    else if (SUCCESS == scsi_Mode_Sense_6(device, MP_CONTROL, MODE_PARAMETER_HEADER_6_LEN + 12, 0, true,
                                          MPC_CURRENT_VALUES, controlModePage))
    {
        mode6ByteCmd = true;
    }
    else
    {
        return NOT_SUPPORTED;
    }
    // Should there be an interface check here? We should allow this anywhere since SAT might be being used and would
    // also be affected by this bit
    if (defaultSetting)
    {
        // read the default setting for this bit
        DECLARE_ZERO_INIT_ARRAY(uint8_t, controlModePageDefaults, MODE_PARAMETER_HEADER_10_LEN + 12);
        if (mode6ByteCmd && SUCCESS == scsi_Mode_Sense_6(device, MP_CONTROL, MODE_PARAMETER_HEADER_6_LEN + 12, 0, true,
                                                         MPC_DEFAULT_VALUES, controlModePageDefaults))
        {
            // figure out what D_Sense is set to, then change it in the current settings
            if (controlModePage[MODE_PARAMETER_HEADER_6_LEN + 2] & BIT2)
            {
                descriptorFormat = true;
            }
            else
            {
                descriptorFormat = false;
            }
        }
        else if (!mode6ByteCmd &&
                 SUCCESS == scsi_Mode_Sense_10(device, MP_CONTROL, MODE_PARAMETER_HEADER_10_LEN + 12, 0, true, false,
                                               MPC_DEFAULT_VALUES, controlModePageDefaults))
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
    uint8_t byteOffset = UINT8_C(0);
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
        M_SET_BIT8(controlModePage[byteOffset], 2);
    }
    else
    {
        M_CLEAR_BIT8(controlModePage[byteOffset], 2);
    }
    // write the change to the drive
    if (mode6ByteCmd)
    {
        ret = scsi_Mode_Select_6(device, MODE_PARAMETER_HEADER_6_LEN + 12, true, saveParameters, false, controlModePage,
                                 MODE_PARAMETER_HEADER_6_LEN + 12);
    }
    else
    {
        ret = scsi_Mode_Select_10(device, MODE_PARAMETER_HEADER_10_LEN + 12, true, saveParameters, false,
                                  controlModePage, MODE_PARAMETER_HEADER_10_LEN + 12);
    }
    return ret;
}

eReturnValues get_Current_Free_Fall_Control_Sensitivity(tDevice* device, uint16_t* sensitivity)
{
    eReturnValues ret = NOT_SUPPORTED;
    DISABLE_NONNULL_COMPARE
    if (sensitivity == M_NULLPTR)
    {
        return BAD_PARAMETER;
    }
    RESTORE_NONNULL_COMPARE
    if (device->drive_info.drive_type == ATA_DRIVE)
    {
        if (is_ATA_Identify_Word_Valid(le16_to_host(device->drive_info.IdentifyData.ata.Word086)) &&
            le16_to_host(device->drive_info.IdentifyData.ata.Word086) & BIT15)
        {
            if (is_ATA_Identify_Word_Valid_With_Bits_14_And_15(
                    le16_to_host(device->drive_info.IdentifyData.ata.Word119)) &&
                le16_to_host(device->drive_info.IdentifyData.ata.Word119) & BIT5) // supported
            {
                ret          = SUCCESS;
                *sensitivity = UINT16_MAX; // this can be used to filter out invalid value, a.k.a. feature is not
                                           // enabled, but is supported.
                if (is_ATA_Identify_Word_Valid_With_Bits_14_And_15(
                        le16_to_host(device->drive_info.IdentifyData.ata.Word120)) &&
                    le16_to_host(device->drive_info.IdentifyData.ata.Word120) & BIT5) // enabled
                {
                    if (is_ATA_Identify_Word_Valid(le16_to_host(device->drive_info.IdentifyData.ata.Word053)))
                    {
                        // Word 53, bits 15:8
                        *sensitivity = M_Byte1(device->drive_info.IdentifyData.ata.Word053);
                    }
                }
            }
        }
    }
    return ret;
}

eReturnValues set_Free_Fall_Control_Sensitivity(tDevice* device, uint8_t sensitivity)
{
    eReturnValues ret = NOT_SUPPORTED;
    if (device->drive_info.drive_type == ATA_DRIVE)
    {
        if (is_ATA_Identify_Word_Valid(le16_to_host(device->drive_info.IdentifyData.ata.Word086)) &&
            le16_to_host(device->drive_info.IdentifyData.ata.Word086) & BIT15)
        {
            if (is_ATA_Identify_Word_Valid_With_Bits_14_And_15(
                    le16_to_host(device->drive_info.IdentifyData.ata.Word119)) &&
                le16_to_host(device->drive_info.IdentifyData.ata.Word119) & BIT5) // supported
            {
                ret = ata_Set_Features(device, SF_ENABLE_FREE_FALL_CONTROL_FEATURE, sensitivity, 0, 0, 0);
            }
        }
    }
    return ret;
}

eReturnValues disable_Free_Fall_Control_Feature(tDevice* device)
{
    eReturnValues ret = NOT_SUPPORTED;
    if (device->drive_info.drive_type == ATA_DRIVE)
    {
        if (is_ATA_Identify_Word_Valid(le16_to_host(device->drive_info.IdentifyData.ata.Word086)) &&
            le16_to_host(device->drive_info.IdentifyData.ata.Word086) & BIT15)
        {
            if (is_ATA_Identify_Word_Valid_With_Bits_14_And_15(
                    le16_to_host(device->drive_info.IdentifyData.ata.Word119)) &&
                le16_to_host(device->drive_info.IdentifyData.ata.Word119) & BIT5) // supported
            {
                ret = ata_Set_Features(device, SF_DISABLE_FREE_FALL_CONTROL_FEATURE, 0, 0, 0, 0);
            }
        }
    }
    return ret;
}

void show_Test_Unit_Ready_Status(tDevice* device)
{
    scsiStatus returnedStatus;
    safe_memset(&returnedStatus, sizeof(scsiStatus), 0, sizeof(scsiStatus));
    eReturnValues ret = scsi_Test_Unit_Ready(device, &returnedStatus);
    if ((ret == SUCCESS) && (returnedStatus.senseKey == SENSE_KEY_NO_ERROR))
    {
        printf("READY\n");
    }
    else
    {
        eVerbosityLevels tempVerbosity = device->deviceVerbosity;
        printf("NOT READY\n");
        device->deviceVerbosity =
            VERBOSITY_COMMAND_NAMES; // the function below will print out a sense data translation, but only it we are
                                     // at this verbosity or higher which is why it's set before this call.
        check_Sense_Key_ASC_ASCQ_And_FRU(device, returnedStatus.senseKey, returnedStatus.asc, returnedStatus.ascq,
                                         returnedStatus.fru);
        device->deviceVerbosity = tempVerbosity; // restore it back to what it was now that this is done.
    }
}

eReturnValues enable_Disable_AAM_Feature(tDevice* device, bool enable)
{
    eReturnValues ret = NOT_SUPPORTED;
    if (device->drive_info.drive_type == ATA_DRIVE)
    {
        // check the identify bits to make sure APM is supported.
        if (is_ATA_Identify_Word_Valid(le16_to_host(device->drive_info.IdentifyData.ata.Word083)) &&
            le16_to_host(device->drive_info.IdentifyData.ata.Word083) & BIT9)
        {
            if (enable)
            {
                // set value to the vendor recommended value reported in identify data when requesting an enable
                // operation
                uint8_t enableValue = UINT8_C(128);
                if (is_ATA_Identify_Word_Valid(le16_to_host(device->drive_info.IdentifyData.ata.Word094)))
                {
                    enableValue = M_Byte1(device->drive_info.IdentifyData.ata.Word094);
                }
                ret = ata_Set_Features(device, SF_ENABLE_AUTOMATIC_ACOUSTIC_MANAGEMENT_FEATURE, enableValue, 0, 0, 0);
            }
            else
            {
                // subcommand C2
                ret = ata_Set_Features(device, SF_DISABLE_AUTOMATIC_ACOUSTIC_MANAGEMENT, 0, 0, 0, 0);
                if (ret != SUCCESS)
                {
                    // the disable AAM feature is not available on all devices according to ATA spec.
                    ret = NOT_SUPPORTED;
                }
            }
        }
    }
    return ret;
}
// AAM Levels:
//  0 - vendor specific
//  1-7Fh = These are labelled as "Retired" in every spec I can find, so no idea what these even mean. - TJE
//  80h = minimum acoustic emanation
//  81h - FDh = intermediate acoustic management levels
//  FEh = maximum performance.
eReturnValues set_AAM_Level(tDevice* device, uint8_t aamLevel)
{
    eReturnValues ret = NOT_SUPPORTED;
    if (device->drive_info.drive_type == ATA_DRIVE)
    {
        // check the identify bits to make sure APM is supported.
        if (is_ATA_Identify_Word_Valid(le16_to_host(device->drive_info.IdentifyData.ata.Word083)) &&
            le16_to_host(device->drive_info.IdentifyData.ata.Word083) & BIT9)
        {
            // subcommand 42 with the aamLevel in the count field
            ret = ata_Set_Features(device, SF_ENABLE_AUTOMATIC_ACOUSTIC_MANAGEMENT_FEATURE, aamLevel, 0, 0, 0);
        }
    }
    return ret;
}

eReturnValues get_AAM_Level(tDevice* device, uint8_t* aamLevel)
{
    eReturnValues ret = NOT_SUPPORTED;
    if (device->drive_info.drive_type == ATA_DRIVE)
    {
        // check the identify bits to make sure AAM is supported.
        if (is_ATA_Identify_Word_Valid(le16_to_host(device->drive_info.IdentifyData.ata.Word083)) &&
            le16_to_host(device->drive_info.IdentifyData.ata.Word083) &
                BIT9) // word 86 says "enabled". We may or may not want to check for that.
        {
            // get it from identify device word 94
            ret = SUCCESS;
            if (is_ATA_Identify_Word_Valid(le16_to_host(device->drive_info.IdentifyData.ata.Word094)))
            {
                *aamLevel = M_Byte0(device->drive_info.IdentifyData.ata.Word094);
            }
            else
            {
                *aamLevel = UINT8_MAX; // invalid value since the identify word was invalid
            }
        }
    }
    return ret;
}

bool scsi_MP_Reset_To_Defaults_Supported(tDevice* device)
{
    bool supported = false;
    if (device->drive_info.scsiVersion >= SCSI_VERSION_SCSI2) // VPD added in SCSI2
    {
        DECLARE_ZERO_INIT_ARRAY(uint8_t, extendedInquiryData, VPD_EXTENDED_INQUIRY_LEN);
        if (SUCCESS ==
            scsi_Inquiry(device, extendedInquiryData, VPD_EXTENDED_INQUIRY_LEN, EXTENDED_INQUIRY_DATA, true, false))
        {
            if (extendedInquiryData[1] == EXTENDED_INQUIRY_DATA)
            {
                supported = extendedInquiryData[8] & BIT3;
            }
        }
    }
    return supported;
}

eReturnValues scsi_Update_Mode_Page(tDevice* device, uint8_t modePage, uint8_t subpage, eSCSI_MP_UPDATE_MODE updateMode)
{
    eReturnValues        ret            = NOT_SUPPORTED;
    uint32_t             modePageLength = UINT32_C(0);
    eScsiModePageControl mpc            = MPC_DEFAULT_VALUES;
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
    if (modePage == MP_RETURN_ALL_PAGES ||
        subpage == MP_SP_ALL_SUBPAGES) // if asking for all mode pages, all mode pages and subpages, or all subpages of
                                       // a specific page, we need to handle it in here.
    {
        // if resetting all pages, check if the RTD bit is supported to simplify the process...-TJE
        if (mpc == MPC_DEFAULT_VALUES && modePage == MP_RETURN_ALL_PAGES && subpage == MP_SP_ALL_SUBPAGES &&
            scsi_MP_Reset_To_Defaults_Supported(device))
        {
            // requesting to reset all mode pages. Send the mode select command with the RTD bit set.
            ret              = scsi_Mode_Select_10(device, 0, true, true, true, M_NULLPTR, 0);
            uint8_t senseKey = UINT8_C(0);
            uint8_t asc      = UINT8_C(0);
            uint8_t ascq     = UINT8_C(0);
            uint8_t fru      = UINT8_C(0);
            get_Sense_Key_ASC_ASCQ_FRU(device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, &senseKey, &asc, &ascq,
                                       &fru);
            if (senseKey == SENSE_KEY_ILLEGAL_REQUEST && asc == 0x20 &&
                ascq == 0x00) // checking for invalid operation code
            {
                // retry with 6 byte command since 10 byte op code was not recognizd.
                ret = scsi_Mode_Select_6(device, 0, true, true, true, M_NULLPTR, 0);
            }
        }
        else
        {
            if (SUCCESS == get_SCSI_Mode_Page_Size(device, MPC_CURRENT_VALUES, modePage, subpage, &modePageLength))
            {
                uint8_t* modeData = C_CAST(
                    uint8_t*, safe_calloc_aligned(modePageLength, sizeof(uint8_t), device->os_info.minimumAlignment));
                if (modeData == M_NULLPTR)
                {
                    return MEMORY_FAILURE;
                }
                // now read all the data
                bool used6ByteCmd = false;
                if (SUCCESS == get_SCSI_Mode_Page(device, mpc, modePage, subpage, M_NULLPTR, M_NULLPTR, true, modeData,
                                                  modePageLength, M_NULLPTR, &used6ByteCmd))
                {
                    // now we need to loop through each page, and send it to the drive as a new mode select command.
                    uint32_t offset                = UINT32_C(0);
                    uint16_t blockDescriptorLength = UINT16_C(0);
                    uint16_t modeDataLen           = UINT16_C(0);

                    get_SBC_Mode_Header_Blk_Desc_Fields(used6ByteCmd, modeData, modePageLength, &modeDataLen, M_NULLPTR,
                                                        M_NULLPTR, M_NULLPTR, &blockDescriptorLength, M_NULLPTR,
                                                        M_NULLPTR);
                    if (used6ByteCmd)
                    {
                        offset = MODE_PARAMETER_HEADER_6_LEN;
                    }
                    else
                    {
                        offset = MODE_PARAMETER_HEADER_10_LEN;
                    }
                    offset += blockDescriptorLength;

                    uint16_t currentPageLength = UINT16_C(0);
                    uint16_t counter           = UINT16_C(0);
                    uint16_t failedModeSelects = UINT16_C(0);
                    for (; offset < modePageLength && modeDataLen > 0; offset += currentPageLength, ++counter)
                    {
                        uint8_t* currentPageToSet       = M_NULLPTR;
                        uint16_t currentPageToSetLength = used6ByteCmd
                                                              ? MODE_PARAMETER_HEADER_6_LEN + blockDescriptorLength
                                                              : MODE_PARAMETER_HEADER_10_LEN + blockDescriptorLength;
                        uint8_t  currentPage            = get_bit_range_uint8(modeData[offset + 0], 5, 0);
                        uint8_t  currentSubPage         = UINT8_C(0);
                        uint16_t currentPageOffset      = UINT16_C(0);
                        if (modeData[offset] & BIT6)
                        {
                            // subpage format
                            currentSubPage    = modeData[offset + 1];
                            currentPageLength = M_BytesTo2ByteValue(modeData[offset + 2], modeData[offset + 3]) +
                                                4; // add 4 bytes for page code, subpage code, & page length bytes
                        }
                        else
                        {
                            currentPageLength =
                                modeData[offset + 1] + 2; // add 2 bytes for the page code and page length bytes
                        }
                        currentPageToSetLength += currentPageLength;
                        currentPageToSet =
                            M_REINTERPRET_CAST(uint8_t*, safe_calloc_aligned(currentPageToSetLength, sizeof(uint8_t),
                                                                             device->os_info.minimumAlignment));
                        if (currentPageToSet == M_NULLPTR)
                        {
                            safe_free_aligned(&modeData);
                            return MEMORY_FAILURE;
                        }
                        if (used6ByteCmd)
                        {
                            // copy header and block descriptors (if any)
                            currentPageOffset = MODE_PARAMETER_HEADER_6_LEN + blockDescriptorLength;
                            safe_memcpy(currentPageToSet, currentPageToSetLength, &modeData[0],
                                        MODE_PARAMETER_HEADER_6_LEN + blockDescriptorLength);
                            // now zero out the reserved bytes for the mode select command
                            currentPageToSet[0] = 0; // mode data length is reserved for mode select commands
                            // leave medium type alone
                            // leave device specific parameter alone???
                            // leave block descriptor length alone in case we got some.
                        }
                        else
                        {
                            // copy header and block descriptors (if any)
                            currentPageOffset = MODE_PARAMETER_HEADER_10_LEN + blockDescriptorLength;
                            safe_memcpy(currentPageToSet, currentPageToSetLength, &modeData[0],
                                        MODE_PARAMETER_HEADER_10_LEN + blockDescriptorLength);
                            // now zero out the reserved bytes for the mode select command
                            currentPageToSet[0] = 0; // mode data length is reserved for mode select commands
                            currentPageToSet[1] = 0;
                            // leave medium type alone
                            // leave device specific parameter alone???
                            // leave block descriptor length alone in case we got some.
                        }
                        // now we need to copy the default data over now, then send it to the drive.
                        safe_memcpy(&currentPageToSet[currentPageOffset], currentPageToSetLength - currentPageOffset,
                                    &modeData[offset], currentPageLength);
                        bool pageFormat =
                            currentPage == 0 ? false : true;        // set to false when reading vendor unique page zero
                        bool savable = modeData[offset + 0] & BIT7; // use this to save pages. This bit says whether the
                                                                    // page/settings can be saved or not.
                        if (used6ByteCmd)
                        {
                            if (SUCCESS != scsi_Mode_Select_6(device, C_CAST(uint8_t, currentPageToSetLength),
                                                              pageFormat, savable, false, currentPageToSet,
                                                              currentPageToSetLength))
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
                            if (SUCCESS != scsi_Mode_Select_10(device, currentPageToSetLength, pageFormat, savable,
                                                               false, currentPageToSet, currentPageToSetLength))
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
                        safe_free_aligned(&currentPageToSet);
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
                safe_free_aligned(&modeData);
            }
            else
            {
                // mode page not supported most likely
            }
        }
    }
    else
    {
        // individual page...easy peasy
        if (SUCCESS == get_SCSI_Mode_Page_Size(device, MPC_CURRENT_VALUES, modePage, subpage, &modePageLength))
        {
            uint8_t* modeData = C_CAST(
                uint8_t*, safe_calloc_aligned(modePageLength, sizeof(uint8_t), device->os_info.minimumAlignment));
            if (modeData == M_NULLPTR)
            {
                return MEMORY_FAILURE;
            }
            // now read all the data
            bool used6ByteCmd = false;
            if (SUCCESS == get_SCSI_Mode_Page(device, mpc, modePage, subpage, M_NULLPTR, M_NULLPTR, true, modeData,
                                              modePageLength, M_NULLPTR, &used6ByteCmd))
            {
                uint16_t offset                = UINT16_C(0);
                uint16_t blockDescriptorLength = UINT16_C(0);
                uint16_t modeDataLen           = UINT16_C(0);
                get_SBC_Mode_Header_Blk_Desc_Fields(used6ByteCmd, modeData, modePageLength, &modeDataLen, M_NULLPTR,
                                                    M_NULLPTR, M_NULLPTR, &blockDescriptorLength, M_NULLPTR, M_NULLPTR);
                if (used6ByteCmd)
                {
                    offset = MODE_PARAMETER_HEADER_6_LEN;
                    // now zero out the reserved bytes for the mode select command
                    modeData[0] = 0; // mode data length is reserved for mode select commands
                    // leave medium type alone
                    // leave device specific parameter alone???
                    // leave block descriptor length alone in case we got some.
                }
                else
                {
                    offset = MODE_PARAMETER_HEADER_10_LEN;
                    // now zero out the reserved bytes for the mode select command
                    modeData[0] = 0; // mode data length is reserved for mode select commands
                    modeData[1] = 0;
                    // leave medium type alone
                    // leave device specific parameter alone???
                    // leave block descriptor length alone in case we got some.
                }
                offset += blockDescriptorLength;
                // now send the mode select command
                bool pageFormat = modePage == 0 ? false : true; // set to false when reading vendor unique page zero
                bool savable =
                    modeData[offset + 0] &
                    BIT7; // use this to save pages. This bit says whether the page/settings can be saved or not.
                if (used6ByteCmd)
                {
                    if (SUCCESS != scsi_Mode_Select_6(device, C_CAST(uint8_t, modePageLength), pageFormat, savable,
                                                      false, modeData, modePageLength))
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
                    if (SUCCESS != scsi_Mode_Select_10(device, C_CAST(uint16_t, modePageLength), pageFormat, savable,
                                                       false, modeData, modePageLength))
                    {
                        ret = FAILURE;
                    }
                    else
                    {
                        ret = SUCCESS;
                    }
                }
            }
            safe_free_aligned(&modeData);
        }
        else
        {
            // most likely not a supported page
        }
    }
    return ret;
}

// NOTE: This rely's on NOT having the mode page header in the passed in buffer, just the raw mode page itself!
eReturnValues scsi_Set_Mode_Page(tDevice* device, uint8_t* modePageData, uint16_t modeDataLength, bool saveChanges)
{
    eReturnValues ret = NOT_SUPPORTED;
    DISABLE_NONNULL_COMPARE
    if (modePageData == M_NULLPTR || modeDataLength == UINT16_C(0))
    {
        return BAD_PARAMETER;
    }
    RESTORE_NONNULL_COMPARE
    uint32_t modePageLength = UINT32_C(0);
    uint8_t  modePage       = get_bit_range_uint8(modePageData[0], 5, 0);
    uint8_t  subpage        = UINT8_C(0);
    if (modePageData[0] & BIT6)
    {
        // subpage format
        subpage = modePageData[1];
    }
    // even though we have the data we want to send, we must ALWAYS request the page first, then modify the data and
    // send it back.
    if (SUCCESS == get_SCSI_Mode_Page_Size(device, MPC_CURRENT_VALUES, modePage, subpage, &modePageLength))
    {
        uint8_t* modeData = M_REINTERPRET_CAST(
            uint8_t*, safe_calloc_aligned(modePageLength, sizeof(uint8_t), device->os_info.minimumAlignment));
        if (modeData == M_NULLPTR)
        {
            return MEMORY_FAILURE;
        }
        // now read all the data
        bool used6ByteCmd = false;
        if (SUCCESS == get_SCSI_Mode_Page(device, MPC_CURRENT_VALUES, modePage, subpage, M_NULLPTR, M_NULLPTR, true,
                                          modeData, modePageLength, M_NULLPTR, &used6ByteCmd))
        {
            uint16_t offset                = UINT16_C(0);
            uint16_t blockDescriptorLength = UINT16_C(0);
            get_SBC_Mode_Header_Blk_Desc_Fields(used6ByteCmd, modeData, modePageLength, M_NULLPTR, M_NULLPTR, M_NULLPTR,
                                                M_NULLPTR, &blockDescriptorLength, M_NULLPTR, M_NULLPTR);
            if (used6ByteCmd)
            {
                offset = MODE_PARAMETER_HEADER_6_LEN;
                // now zero out the reserved bytes for the mode select command
                modeData[0] = RESERVED; // mode data length is reserved for mode select commands
                // leave medium type alone
                // leave device specific parameter alone???
                // leave block descriptor length alone in case we got some.
            }
            else
            {
                offset = MODE_PARAMETER_HEADER_10_LEN;
                // now zero out the reserved bytes for the mode select command
                modeData[0] = RESERVED; // mode data length is reserved for mode select commands
                modeData[1] = RESERVED;
                // leave medium type alone
                // leave device specific parameter alone???
                // leave block descriptor length alone in case we got some.
            }
            offset += blockDescriptorLength;
            // copy the incoming buffer (which is ONLY mode page data)
            safe_memcpy(&modeData[offset], modePageLength - offset, modePageData, modeDataLength);
            // now send the mode select command
            bool pageFormat = modePage == 0 ? false : true; // set to false when reading vendor unique page zero
            // bool savable = modeData[offset + 0] & BIT7;// use this to save pages. This bit says whether the
            // page/settings can be saved or not.
            if (used6ByteCmd)
            {
                if (SUCCESS != scsi_Mode_Select_6(device, C_CAST(uint8_t, modePageLength), pageFormat, saveChanges,
                                                  false, modeData, modePageLength))
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
                if (SUCCESS != scsi_Mode_Select_10(device, C_CAST(uint16_t, modePageLength), pageFormat, saveChanges,
                                                   false, modeData, modePageLength))
                {
                    ret = FAILURE;
                }
                else
                {
                    ret = SUCCESS;
                }
            }
        }
        safe_free_aligned(&modeData);
    }
    else
    {
        // most likely not a supported page
    }
    return ret;
}

#define SCSI_MODE_PAGE_NAME_MAX_LENGTH 40
// Note: this doesn't take into account some pages being device type specific.
//       Reviewing all the various SCSI standards for different device types would be necessary in order to make this
//       100% correct and complete. We are currently focussed on block and zoned block devices, however some
//       pages are being looked up for other device types as well.
M_NONNULL_PARAM_LIST(4)
M_PARAM_WO(4) static void get_SCSI_MP_Name(uint8_t scsiDeviceType, uint8_t modePage, uint8_t subpage, char* mpName)
{
    scsiDeviceType = get_bit_range_uint8(scsiDeviceType, 4, 0); // strip off the qualifier if it was passed
    switch (modePage)
    {
    case 0x00: // vendor unique
        break;
    case 0x01:
        switch (subpage)
        {
        case 0x00: // read-write error recovery
            snprintf_err_handle(mpName, SCSI_MODE_PAGE_NAME_MAX_LENGTH, "Read-Write Error Recovery");
            break;
        default:
            // unknown
            break;
        }
        break;
    case 0x02:
        switch (subpage)
        {
        case 0x00: // disconnect-reconnect
            snprintf_err_handle(mpName, SCSI_MODE_PAGE_NAME_MAX_LENGTH, "Disconnect-Reconnect");
            break;
        default:
            // unknown
            break;
        }
        break;
    case 0x03:
        switch (subpage)
        {
        case 0x00: // Format Device (block devie) or MRW CD-RW (cd/dvd)
            switch (scsiDeviceType)
            {
            case PERIPHERAL_DIRECT_ACCESS_BLOCK_DEVICE:
            case PERIPHERAL_HOST_MANAGED_ZONED_BLOCK_DEVICE:
                snprintf_err_handle(mpName, SCSI_MODE_PAGE_NAME_MAX_LENGTH, "Format Device");
                break;
            case PERIPHERAL_CD_DVD_DEVICE:
                snprintf_err_handle(mpName, SCSI_MODE_PAGE_NAME_MAX_LENGTH, "MRW CD-RW");
                break;
            default:
                break;
            }
            break;
        default:
            // unknown
            break;
        }
        break;
    case 0x04:
        switch (subpage)
        {
        case 0x00: // Rigid Disk Geometry
            snprintf_err_handle(mpName, SCSI_MODE_PAGE_NAME_MAX_LENGTH, "Rigid Disk Geometry");
            break;
        default:
            // unknown
            break;
        }
        break;
    case 0x05:
        switch (subpage)
        {
        case 0x00: // flexible disk
            snprintf_err_handle(mpName, SCSI_MODE_PAGE_NAME_MAX_LENGTH, "Flexible Disk");
            break;
        default:
            // unknown
            break;
        }
        break;
    case 0x06:
        switch (subpage)
        {
        case 0x00: // optical memory (SBC) OR RBC device parameters
            switch (scsiDeviceType)
            {
            case PERIPHERAL_OPTICAL_MEMORY_DEVICE:
                snprintf_err_handle(mpName, SCSI_MODE_PAGE_NAME_MAX_LENGTH, "Optical Memory");
                break;
            case PERIPHERAL_SIMPLIFIED_DIRECT_ACCESS_DEVICE:
                snprintf_err_handle(mpName, SCSI_MODE_PAGE_NAME_MAX_LENGTH, "RBC Device Parameters");
                break;
            default:
                break;
            }
            break;
        default:
            // unknown
            break;
        }
        break;
    case 0x07:
        switch (subpage)
        {
        case 0x00: // verify error recovery
            snprintf_err_handle(mpName, SCSI_MODE_PAGE_NAME_MAX_LENGTH, "Verify Error Recovery");
            break;
        default:
            // unknown
            break;
        }
        break;
    case 0x08:
        switch (subpage)
        {
        case 0x00: // Caching
            snprintf_err_handle(mpName, SCSI_MODE_PAGE_NAME_MAX_LENGTH, "Caching");
            break;
        default:
            // unknown
            break;
        }
        break;
    case 0x09:
        switch (subpage)
        {
        case 0x00: // peripheral device
            snprintf_err_handle(mpName, SCSI_MODE_PAGE_NAME_MAX_LENGTH, "Peripheral Device");
            break;
        default:
            // unknown
            break;
        }
        break;
    case 0x0A:
        switch (subpage)
        {
        case 0x00: // control
            snprintf_err_handle(mpName, SCSI_MODE_PAGE_NAME_MAX_LENGTH, "Control");
            break;
        case 0x01: // control extension
            snprintf_err_handle(mpName, SCSI_MODE_PAGE_NAME_MAX_LENGTH, "Control Extension");
            break;
        case 0x02: // application tag
            snprintf_err_handle(mpName, SCSI_MODE_PAGE_NAME_MAX_LENGTH, "Application Tag");
            break;
        case 0x03: // command duration limit A
            snprintf_err_handle(mpName, SCSI_MODE_PAGE_NAME_MAX_LENGTH, "Command Duration Limit A");
            break;
        case 0x04: // command duration limit B
            snprintf_err_handle(mpName, SCSI_MODE_PAGE_NAME_MAX_LENGTH, "Command Duration Limit B");
            break;
        case 0x05: // IO Advice Hints Grouping
            snprintf_err_handle(mpName, SCSI_MODE_PAGE_NAME_MAX_LENGTH, "IO Advice Hints Grouping");
            break;
        case 0x06: // Background Operation Control
            snprintf_err_handle(mpName, SCSI_MODE_PAGE_NAME_MAX_LENGTH, "Background Operation Control");
            break;
        case 0xF0: // Control Data Protection
            snprintf_err_handle(mpName, SCSI_MODE_PAGE_NAME_MAX_LENGTH, "Control Data Protection");
            break;
        case 0xF1: // PATA Control
            snprintf_err_handle(mpName, SCSI_MODE_PAGE_NAME_MAX_LENGTH, "PATA Control");
            break;
        default:
            // unknown
            break;
        }
        break;
    case 0x0B:
        switch (subpage)
        {
        case 0x00: // Medium Types Supported
            snprintf_err_handle(mpName, SCSI_MODE_PAGE_NAME_MAX_LENGTH, "Medium Types Supported");
            break;
        default:
            // unknown
            break;
        }
        break;
    case 0x0C:
        switch (subpage)
        {
        case 0x00: // notch and partition
            snprintf_err_handle(mpName, SCSI_MODE_PAGE_NAME_MAX_LENGTH, "Notch And Partition");
            break;
        default:
            // unknown
            break;
        }
        break;
    case 0x0D:
        switch (subpage)
        {
        case 0x00: // Power Condition (direct accessblock device) or CD Device Parameters
            switch (scsiDeviceType)
            {
            case PERIPHERAL_HOST_MANAGED_ZONED_BLOCK_DEVICE:
            case PERIPHERAL_DIRECT_ACCESS_BLOCK_DEVICE:
                snprintf_err_handle(mpName, SCSI_MODE_PAGE_NAME_MAX_LENGTH, "Power Condition");
                break;
            case PERIPHERAL_CD_DVD_DEVICE:
                snprintf_err_handle(mpName, SCSI_MODE_PAGE_NAME_MAX_LENGTH, "CD Device Parameters");
                break;
            default:
                break;
            }
            break;
        default:
            // unknown
            break;
        }
        break;
    case 0x0E:
        switch (subpage)
        {
        case 0x00: // CD Audio Control
            snprintf_err_handle(mpName, SCSI_MODE_PAGE_NAME_MAX_LENGTH, "CD Audio Control");
            break;
        case 0x01: // Target Device
            snprintf_err_handle(mpName, SCSI_MODE_PAGE_NAME_MAX_LENGTH, "Target Device");
            break;
        case 0x02: // DT Device Primary Port
            snprintf_err_handle(mpName, SCSI_MODE_PAGE_NAME_MAX_LENGTH, "DT Devuce Primary Port");
            break;
        case 0x03: // Logical Unit
            snprintf_err_handle(mpName, SCSI_MODE_PAGE_NAME_MAX_LENGTH, "Logical Unit");
            break;
        case 0x04: // Target Device Serial Number
            snprintf_err_handle(mpName, SCSI_MODE_PAGE_NAME_MAX_LENGTH, "Target Device Serial Number");
            break;
        default:
            // unknown
            break;
        }
        break;
    case 0x0F:
        switch (subpage)
        {
        case 0x00: // Data Compression
            snprintf_err_handle(mpName, SCSI_MODE_PAGE_NAME_MAX_LENGTH, "Data Compression");
            break;
        default:
            // unknown
            break;
        }
        break;
    case 0x10:
        switch (subpage)
        {
        case 0x00: // XOR Control (direct access) OR Device configuration (tape)
            switch (scsiDeviceType)
            {
            case PERIPHERAL_HOST_MANAGED_ZONED_BLOCK_DEVICE:
            case PERIPHERAL_DIRECT_ACCESS_BLOCK_DEVICE:
                snprintf_err_handle(mpName, SCSI_MODE_PAGE_NAME_MAX_LENGTH, "XOR Control");
                break;
            case PERIPHERAL_SEQUENTIAL_ACCESS_BLOCK_DEVICE:
                snprintf_err_handle(mpName, SCSI_MODE_PAGE_NAME_MAX_LENGTH, "Device Configuration");
                break;
            default:
                break;
            }
            break;
        case 0x01: // Device Configuration Extension
            snprintf_err_handle(mpName, SCSI_MODE_PAGE_NAME_MAX_LENGTH, "Device Configuration Extension");
            break;
        default:
            // unknown
            break;
        }
        break;
    case 0x11:
        switch (subpage)
        {
        case 0x00: // Medium Partition (1)
            snprintf_err_handle(mpName, SCSI_MODE_PAGE_NAME_MAX_LENGTH, "Medium Partition (1)");
            break;
        default:
            // unknown
            break;
        }
        break;
        // 12h and 13h are in the SPC5 annex, but not named...skipping
    case 0x14:
        switch (subpage)
        {
        case 0x00: // enclosure services management
            snprintf_err_handle(mpName, SCSI_MODE_PAGE_NAME_MAX_LENGTH, "Enclosure Services Management");
            break;
        default:
            // unknown
            break;
        }
        break;
    case 0x15: // Extended
        // all subpages
        snprintf_err_handle(mpName, SCSI_MODE_PAGE_NAME_MAX_LENGTH, "Extended - %" PRIu8, subpage);
        break;
    case 0x16: // Extended Device-Type specific
        // all subpages
        snprintf_err_handle(mpName, SCSI_MODE_PAGE_NAME_MAX_LENGTH, "Extended Device Type Specific - %" PRIu8, subpage);
        break;
        // 17h is in spec, but not named
    case 0x18: // protocol specific logical unit
        snprintf_err_handle(mpName, SCSI_MODE_PAGE_NAME_MAX_LENGTH, "Protocol Specific Logical Unit - %" PRIu8,
                            subpage);
        break;
    case 0x19: // protocol specific port
        snprintf_err_handle(mpName, SCSI_MODE_PAGE_NAME_MAX_LENGTH, "Protocol Specific Port - %" PRIu8, subpage);
        break;
    case 0x1A:
        switch (subpage)
        {
        case 0x00: // Power Condition
            snprintf_err_handle(mpName, SCSI_MODE_PAGE_NAME_MAX_LENGTH, "Power Condition");
            break;
        case 0x01: // Power Consumption
            snprintf_err_handle(mpName, SCSI_MODE_PAGE_NAME_MAX_LENGTH, "Power Consumption");
            break;
        case 0xF1: // ATA Power Condition
            snprintf_err_handle(mpName, SCSI_MODE_PAGE_NAME_MAX_LENGTH, "ATA Power Condition");
            break;
        default:
            // unknown
            break;
        }
        break;
    case 0x1B:
        switch (subpage)
        {
        case 0x00: // LUN Mapping
            snprintf_err_handle(mpName, SCSI_MODE_PAGE_NAME_MAX_LENGTH, "LUN Mapping");
            break;
        default:
            // unknown
            break;
        }
        break;
    case 0x1C:
        switch (subpage)
        {
        case 0x00: // Varies on name depending on device type. All related to failure reporting though!!!
            switch (scsiDeviceType)
            {
            case PERIPHERAL_HOST_MANAGED_ZONED_BLOCK_DEVICE:
            case PERIPHERAL_DIRECT_ACCESS_BLOCK_DEVICE:
            case PERIPHERAL_OPTICAL_MEMORY_DEVICE:
                snprintf_err_handle(mpName, SCSI_MODE_PAGE_NAME_MAX_LENGTH, "Informational Exceptions Control");
                break;
            case PERIPHERAL_CD_DVD_DEVICE:
                snprintf_err_handle(mpName, SCSI_MODE_PAGE_NAME_MAX_LENGTH, "Fault/Failure Reporting");
                break;
            case PERIPHERAL_SEQUENTIAL_ACCESS_BLOCK_DEVICE:
            case PERIPHERAL_AUTOMATION_DRIVE_INTERFACE:
            case PERIPHERAL_MEDIUM_CHANGER_DEVICE:
                snprintf_err_handle(mpName, SCSI_MODE_PAGE_NAME_MAX_LENGTH, "Informational Exceptions Control (Tape)");
                break;
            default:
                break;
            }
            break;
        case 0x01: // background control
            snprintf_err_handle(mpName, SCSI_MODE_PAGE_NAME_MAX_LENGTH, "Background Control");
            break;
        case 0x02: // logical block provisioning
            snprintf_err_handle(mpName, SCSI_MODE_PAGE_NAME_MAX_LENGTH, "Logical Block Provisioning");
            break;
        default:
            // unknown
            break;
        }
        break;
    case 0x1D:
        switch (subpage)
        {
        case 0x00: // varies depending on device type
            switch (scsiDeviceType)
            {
            case PERIPHERAL_CD_DVD_DEVICE:
                snprintf_err_handle(mpName, SCSI_MODE_PAGE_NAME_MAX_LENGTH, "C/DVD Time-Out And Protect");
                break;
            case PERIPHERAL_SEQUENTIAL_ACCESS_BLOCK_DEVICE:
                snprintf_err_handle(mpName, SCSI_MODE_PAGE_NAME_MAX_LENGTH, "Medium Configuration");
                break;
            case PERIPHERAL_MEDIUM_CHANGER_DEVICE:
                snprintf_err_handle(mpName, SCSI_MODE_PAGE_NAME_MAX_LENGTH, "Element Address Assignments");
                break;
            default:
                break;
            }
            break;
        default:
            // unknown
            break;
        }
        break;
    case 0x1E:
        switch (subpage)
        {
        case 0x00: // transport geometry parameters
            snprintf_err_handle(mpName, SCSI_MODE_PAGE_NAME_MAX_LENGTH, "Transport Geometry Parameters");
            break;
        default:
            // unknown
            break;
        }
        break;
    case 0x1F:
        switch (subpage)
        {
        case 0x00: // device capabilities
            snprintf_err_handle(mpName, SCSI_MODE_PAGE_NAME_MAX_LENGTH, "Device Capabilities");
            break;
        default:
            // unknown
            break;
        }
        break;
    case 0x2A:
        switch (subpage)
        {
        case 0x00: // CD Capabilities and Mechanical Status - CD-DVD
            switch (scsiDeviceType)
            {
            case PERIPHERAL_CD_DVD_DEVICE:
                snprintf_err_handle(mpName, SCSI_MODE_PAGE_NAME_MAX_LENGTH, "CD Capabilities and Mechanical Status");
                break;
            default:
                break;
            }
            break;
        default:
            // unknown
            break;
        }
        break;
    default:
        // unknown
        break;
    }
}

// this should only have the mode data. NO block descriptors or mode page header (4 or 8 bytes before the mode page
// starts)
M_NONNULL_IF_NONZERO_PARAM(2, 3)
M_PARAM_RO_SIZE(2, 3)
static void print_Mode_Page(uint8_t              scsiPeripheralDeviceType,
                            uint8_t*             modeData,
                            uint32_t             modeDataLen,
                            eScsiModePageControl mpc,
                            bool                 outputWithPrintDataBuffer)
{
    if (modeData != M_NULLPTR && modeDataLen > UINT32_C(2))
    {
        uint8_t  pageNumber = get_bit_range_uint8(modeData[0], 5, 0);
        uint8_t  subpage    = UINT8_C(0);
        uint16_t pageLength = modeData[1] + UINT16_C(2); // page 0 format
        if (modeData[0] & BIT6)
        {
            subpage    = modeData[1];
            pageLength = M_BytesTo2ByteValue(modeData[2], modeData[3]) + UINT16_C(4);
        }
        int equalsLengthToPrint = C_CAST(
            int,
            (M_Min(C_CAST(uint32_t, pageLength), modeDataLen) * UINT32_C(3)) -
                UINT32_C(
                    1)); // printf for variable width fields requires an int, so this shuold always calculate to a
                         // non-negative value, so it should be safe to case to int...plus it should be small...mode
                         // pages are not that long that they come close to signed 32bit max values.-TJE
        // print the header
        if (outputWithPrintDataBuffer)
        {
            equalsLengthToPrint = 1;
            switch (mpc)
            {
            case MPC_CURRENT_VALUES:
                equalsLengthToPrint += C_CAST(int, safe_strlen(" Current Values"));
                break;
            case MPC_CHANGABLE_VALUES:
                equalsLengthToPrint += C_CAST(int, safe_strlen(" Changable Values"));
                break;
            case MPC_DEFAULT_VALUES:
                equalsLengthToPrint += C_CAST(int, safe_strlen(" Default Values"));
                break;
            case MPC_SAVED_VALUES:
                equalsLengthToPrint += C_CAST(int, safe_strlen(" Saved Values"));
                if (subpage > 0)
                {
                    ++equalsLengthToPrint;
                }
                break;
            default: // this shouldn't happen...
                equalsLengthToPrint = 16;
                break;
            }
        }
        // before going further, check if we have a page name to lookup and printout to adjust the size for
        DECLARE_ZERO_INIT_ARRAY(char, pageName, SCSI_MODE_PAGE_NAME_MAX_LENGTH);
        get_SCSI_MP_Name(scsiPeripheralDeviceType, pageNumber, subpage, pageName);
        if (equalsLengthToPrint <
            (C_CAST(int, safe_strlen(pageName)) + 6)) // name will go too far over the end, need to enlarge
        {
            // the equals length should be enlarged for this!!!
            equalsLengthToPrint = C_CAST(int, safe_strlen(pageName)) + 6;
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
            equalsLengthToPrint += 2; // for space at beginning and end
        }
        printf("\n%.*s\n", equalsLengthToPrint,
               "=================================================================================="); // 80 characters
                                                                                                      // max...
        printf(" Page %" PRIX8 "h", pageNumber);
        if (subpage != 0)
        {
            printf(" - %" PRIX8 "h", subpage);
        }
        if (safe_strlen(pageName) > 0)
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
        default: // this shouldn't happen...
            break;
        }
        printf("\n%.*s\n", equalsLengthToPrint,
               "=================================================================================="); // 80 characters
                                                                                                      // max...
        // print out the raw data bytes sent to this function
        if (outputWithPrintDataBuffer)
        {
            print_Data_Buffer(modeData, M_Min(M_STATIC_CAST(uint32_t, pageLength), modeDataLen), false);
        }
        else
        {
            for (uint32_t iter = UINT32_C(0); iter < M_Min(M_STATIC_CAST(uint32_t, pageLength), modeDataLen); ++iter)
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
    else if (modeData != M_NULLPTR)
    {
        // page not supported
        uint8_t pageNumber = get_bit_range_uint8(modeData[0], 5, 0);
        uint8_t subpage    = UINT8_C(0);
        if (modeData[0] & BIT6)
        {
            subpage = modeData[1];
        }
        int equalsLengthToPrint = 1;
        // print the header
        switch (mpc)
        {
        case MPC_CURRENT_VALUES:
            equalsLengthToPrint += C_CAST(int, safe_strlen(" Current Values"));
            break;
        case MPC_CHANGABLE_VALUES:
            equalsLengthToPrint += C_CAST(int, safe_strlen(" Changable Values"));
            break;
        case MPC_DEFAULT_VALUES:
            equalsLengthToPrint += C_CAST(int, safe_strlen(" Default Values"));
            break;
        case MPC_SAVED_VALUES:
            equalsLengthToPrint += C_CAST(int, safe_strlen(" Saved Values"));
            if (subpage > 0)
            {
                ++equalsLengthToPrint;
            }
            break;
        default: // this shouldn't happen...
            equalsLengthToPrint = 16;
            break;
        }
        printf("\n%.*s\n", equalsLengthToPrint,
               "=================================================================================="); // 80 characters
                                                                                                      // max...
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
        default: // this shouldn't happen...
            break;
        }
        printf("\n%.*s\n", equalsLengthToPrint,
               "=================================================================================="); // 80 characters
                                                                                                      // max...
        printf("Not Supported.\n");
    }
}

// shows a single mode page for the selected control(current, saved, changable, default)
void show_SCSI_Mode_Page(tDevice*             device,
                         uint8_t              modePage,
                         uint8_t              subpage,
                         eScsiModePageControl mpc,
                         bool                 bufferFormatOutput)
{
    uint32_t modePageLength = UINT32_C(0);
    if (modePage == MP_RETURN_ALL_PAGES ||
        subpage == MP_SP_ALL_SUBPAGES) // if asking for all mode pages, all mode pages and subpages, or all subpages of
                                       // a specific page, we need to handle it in here.
    {
        if (SUCCESS == get_SCSI_Mode_Page_Size(device, mpc, modePage, subpage, &modePageLength))
        {
            uint8_t* modeData = C_CAST(
                uint8_t*, safe_calloc_aligned(modePageLength, sizeof(uint8_t), device->os_info.minimumAlignment));
            if (modeData == M_NULLPTR)
            {
                return;
            }
            // now read all the data
            bool used6ByteCmd = false;
            if (SUCCESS == get_SCSI_Mode_Page(device, mpc, modePage, subpage, M_NULLPTR, M_NULLPTR, true, modeData,
                                              modePageLength, M_NULLPTR, &used6ByteCmd))
            {
                // Loop through each page returned in the buffer and print it to the screen
                uint32_t offset                = UINT32_C(0);
                uint16_t blockDescriptorLength = UINT16_C(0);
                uint16_t modeDataLen           = UINT16_C(0);
                get_SBC_Mode_Header_Blk_Desc_Fields(used6ByteCmd, modeData, modePageLength, &modeDataLen, M_NULLPTR,
                                                    M_NULLPTR, M_NULLPTR, &blockDescriptorLength, M_NULLPTR, M_NULLPTR);
                if (!used6ByteCmd)
                {
                    // got 10 byte command data
                    offset = MODE_PARAMETER_HEADER_10_LEN;
                }
                else
                {
                    // got 6 byte command data.
                    offset = MODE_PARAMETER_HEADER_6_LEN;
                }
                offset += blockDescriptorLength;
                uint16_t currentPageLength = UINT16_C(0);
                uint16_t counter           = UINT16_C(0);
                for (; offset < modePageLength && modeDataLen > 0; offset += currentPageLength, ++counter)
                {
                    if (modeData[offset] & BIT6)
                    {
                        // subpage format
                        currentPageLength = M_BytesTo2ByteValue(modeData[offset + 2], modeData[offset + 3]) +
                                            4; // add 4 bytes for page code, subpage code, & page length bytes
                    }
                    else
                    {
                        currentPageLength =
                            modeData[offset + 1] + 2; // add 2 bytes for the page code and page length bytes
                    }
                    // now print the page out!
                    print_Mode_Page(device->drive_info.scsiVpdData.inquiryData[0], &modeData[offset], currentPageLength,
                                    mpc, bufferFormatOutput);
                }
            }
            safe_free_aligned(&modeData);
        }
        else
        {
            // not supported (SATL most likely)
            DECLARE_ZERO_INIT_ARRAY(uint8_t, modeData, 2);
            modeData[0] = modePage;
            modeData[1] = subpage;
            print_Mode_Page(device->drive_info.scsiVpdData.inquiryData[0], modeData, 2, mpc, bufferFormatOutput);
        }
    }
    else
    {
        // single page...easy
        if (SUCCESS == get_SCSI_Mode_Page_Size(device, mpc, modePage, subpage, &modePageLength))
        {
            uint8_t* modeData = C_CAST(
                uint8_t*, safe_calloc_aligned(modePageLength, sizeof(uint8_t), device->os_info.minimumAlignment));
            if (modeData == M_NULLPTR)
            {
                return;
            }
            // now read all the data
            bool used6ByteCmd = false;
            if (SUCCESS == get_SCSI_Mode_Page(device, mpc, modePage, subpage, M_NULLPTR, M_NULLPTR, true, modeData,
                                              modePageLength, M_NULLPTR, &used6ByteCmd))
            {
                uint16_t blockDescriptorLength = UINT16_C(0);
                uint16_t modeDataLen           = UINT16_C(0);
                get_SBC_Mode_Header_Blk_Desc_Fields(used6ByteCmd, modeData, modePageLength, &modeDataLen, M_NULLPTR,
                                                    M_NULLPTR, M_NULLPTR, &blockDescriptorLength, M_NULLPTR, M_NULLPTR);
                if (modeDataLen > 0)
                {
                    uint8_t headerLen = MODE_PARAMETER_HEADER_10_LEN;
                    if (used6ByteCmd)
                    {
                        headerLen = MODE_PARAMETER_HEADER_6_LEN;
                    }
                    print_Mode_Page(device->drive_info.scsiVpdData.inquiryData[0],
                                    &modeData[headerLen + blockDescriptorLength],
                                    modePageLength - headerLen - blockDescriptorLength, mpc, bufferFormatOutput);
                }
                else
                {
                    printf("No mode page data was returned.\n");
                }
            }
            safe_free_aligned(&modeData);
        }
        else
        {
            // not supported (SATL most likely)
            DECLARE_ZERO_INIT_ARRAY(uint8_t, modeData, 2);
            modeData[0] = modePage;
            modeData[1] = subpage;
            print_Mode_Page(device->drive_info.scsiVpdData.inquiryData[0], modeData, 2, mpc, bufferFormatOutput);
        }
    }
}

// shows all mpc values for a given page.
// should we return an error when asking for all mode pages since that output will otherwise be really messy???
void show_SCSI_Mode_Page_All(tDevice* device, uint8_t modePage, uint8_t subpage, bool bufferFormatOutput)
{
    eScsiModePageControl mpc = MPC_CURRENT_VALUES; // will be incremented through a loop
    for (; mpc <= MPC_SAVED_VALUES; ++mpc)
    {
        show_SCSI_Mode_Page(device, modePage, subpage, mpc, bufferFormatOutput);
    }
}

// if yes, a page and subpage can be provided when doing a log page reset
static bool reset_Specific_Log_Page_Supported(tDevice* device)
{
    bool supported = false;
    if (device->drive_info.scsiVersion >= SCSI_VERSION_SPC_3)
    {
        scsiOperationCodeInfoRequest logSenseSupReq;
        safe_memset(&logSenseSupReq, sizeof(scsiOperationCodeInfoRequest), 0, sizeof(scsiOperationCodeInfoRequest));
        logSenseSupReq.operationCode      = LOG_SELECT_CMD;
        logSenseSupReq.serviceActionValid = false;
        eSCSICmdSupport logSenseSupport   = is_SCSI_Operation_Code_Supported(device, &logSenseSupReq);
        if (logSenseSupport == SCSI_CMD_SUPPORT_SUPPORTED_TO_SCSI_STANDARD)
        {
            if (logSenseSupReq.cdbUsageDataLength > 0)
            {
                if (get_bit_range_uint8(logSenseSupReq.cdbUsageData[2], 5, 0) > 0 && logSenseSupReq.cdbUsageData[3] > 0)
                {
                    supported = true;
                }
            }
        }
    }
    return supported;
}

eReturnValues reset_SCSI_Log_Page(tDevice*            device,
                                  eScsiLogPageControl pageControl,
                                  uint8_t             logPage,
                                  uint8_t             logSubPage,
                                  bool                saveChanges)
{
    eReturnValues ret = NOT_SUPPORTED;
    if (logPage || logSubPage)
    {
        if (!reset_Specific_Log_Page_Supported(device))
        {
            return BAD_PARAMETER; // cannot reset a specific page on this device
        }
    }
    ret = scsi_Log_Select_Cmd(device, true, saveChanges, C_CAST(uint8_t, pageControl), logPage, logSubPage, 0,
                              M_NULLPTR, 0);

    return ret;
}

// doing this in SCSI way for now...should handle nvme separately at some point since a namespace is similar to a lun
uint8_t get_LUN_Count(tDevice* device)
{
    uint8_t lunCount = UINT8_C(1); // assume 1 since we are talking over a lun right now. - TJE
    if (device->drive_info.interface_type != USB_INTERFACE && device->drive_info.interface_type != IEEE_1394_INTERFACE)
    {
        DECLARE_ZERO_INIT_ARRAY(uint8_t, luns, 4);
        uint8_t selectReport = UINT8_C(0x02); // or 0????
        if (SUCCESS == scsi_Report_Luns(device, selectReport, 4, luns))
        {
            uint32_t lunListLength = M_BytesTo4ByteValue(luns[0], luns[1], luns[2], luns[3]);
            lunCount               = C_CAST(uint8_t, lunListLength / UINT32_C(8));
        }
    }
    return lunCount;
}

eMLU get_MLU_Value_For_SCSI_Operation(tDevice* device, uint8_t operationCode, uint16_t serviceAction)
{
    eMLU                         mlu = MLU_NOT_REPORTED;
    scsiOperationCodeInfoRequest mluSupReq;
    safe_memset(&mluSupReq, sizeof(scsiOperationCodeInfoRequest), 0, sizeof(scsiOperationCodeInfoRequest));
    mluSupReq.operationCode      = operationCode;
    mluSupReq.serviceActionValid = false;
    if (serviceAction != 0)
    {
        mluSupReq.serviceActionValid = true;
        mluSupReq.serviceAction      = serviceAction;
    }
    eSCSICmdSupport logSenseSupport = is_SCSI_Operation_Code_Supported(device, &mluSupReq);
    if (logSenseSupport == SCSI_CMD_SUPPORT_SUPPORTED_TO_SCSI_STANDARD)
    {
        mlu = M_STATIC_CAST(eMLU, mluSupReq.multipleLogicalUnits);
    }
    return mlu;
}

bool scsi_Mode_Pages_Shared_By_Multiple_Logical_Units(tDevice* device, uint8_t modePage, uint8_t subPage)
{
    bool     mlus                 = false;
    uint32_t modePagePolicyLength = UINT32_C(4);
    uint8_t* vpdModePagePolicy    = M_REINTERPRET_CAST(
        uint8_t*, safe_calloc_aligned(modePagePolicyLength, sizeof(uint8_t), device->os_info.minimumAlignment));
    if (vpdModePagePolicy != M_NULLPTR)
    {
        if (SUCCESS == scsi_Inquiry(device, vpdModePagePolicy, modePagePolicyLength, MODE_PAGE_POLICY, true, false))
        {
            modePagePolicyLength = M_BytesTo2ByteValue(vpdModePagePolicy[2], vpdModePagePolicy[3]) + 4;
            safe_free_aligned(&vpdModePagePolicy);
            vpdModePagePolicy = C_CAST(
                uint8_t*, safe_calloc_aligned(modePagePolicyLength, sizeof(uint8_t), device->os_info.minimumAlignment));
            if (vpdModePagePolicy != M_NULLPTR)
            {
                if (SUCCESS ==
                    scsi_Inquiry(device, vpdModePagePolicy, modePagePolicyLength, MODE_PAGE_POLICY, true, false))
                {
                    modePagePolicyLength = M_BytesTo2ByteValue(vpdModePagePolicy[2], vpdModePagePolicy[3]) + 4;
                    // Now loop through and find the requested page
                    for (uint32_t vpdMPOffset = UINT32_C(4);
                         vpdMPOffset < UINT16_MAX && vpdMPOffset < modePagePolicyLength; vpdMPOffset += 4)
                    {
                        // NOLINTBEGIN(bugprone-branch-clone)
                        // disabling clang-tidy for readability
                        if (modePage == get_bit_range_uint8(vpdModePagePolicy[vpdMPOffset], 5, 0) &&
                            subPage == vpdModePagePolicy[vpdMPOffset + 1])
                        {
                            mlus = M_ToBool(vpdModePagePolicy[vpdMPOffset + 2] & BIT7);
                            break;
                        }
                        else if (get_bit_range_uint8(vpdModePagePolicy[vpdMPOffset], 5, 0) == 0x3F && subPage == 0 &&
                                 vpdModePagePolicy[vpdMPOffset + 1] == 0)
                        {
                            // This is the "report all mode pages", no subpages to indicate that the mlus applies to all
                            // mode pages on the device.
                            mlus = M_ToBool(vpdModePagePolicy[vpdMPOffset + 2] & BIT7);
                            break;
                        }
                        else if (get_bit_range_uint8(vpdModePagePolicy[vpdMPOffset], 5, 0) == 0x3F &&
                                 vpdModePagePolicy[vpdMPOffset + 1] == 0xFF)
                        {
                            // This is the "report all mode pages and subpages", to indicate that the mlus applies to
                            // all mode pages and subpages on the device.
                            mlus = M_ToBool(vpdModePagePolicy[vpdMPOffset + 2] & BIT7);
                            break;
                        }
                        // NOLINTEND(bugprone-branch-clone)
                    }
                }
            }
        }
        safe_free_aligned(&vpdModePagePolicy);
    }
    return mlus;
}

#define CONCURRENT_RANGES_VERSION_V1 1

typedef struct s_concurrentRangeDescriptionV1
{
    uint8_t  rangeNumber;
    uint8_t  numberOfStorageElements;
    uint64_t lowestLBA;
    uint64_t numberOfLBAs;
} concurrentRangeDescriptionV1;

typedef struct s_concurrentRangesV1
{
    size_t                       size;
    uint32_t                     version;
    uint8_t                      numberOfRanges;
    concurrentRangeDescriptionV1 range[15]; // maximum of 15 concurrent ranges per ACS5
} concurrentRangesV1, *ptrConcurrentRangesV1;

eReturnValues get_Concurrent_Positioning_Ranges(tDevice* device, ptrConcurrentRanges ranges)
{
    eReturnValues ret = NOT_SUPPORTED;
    DISABLE_NONNULL_COMPARE
    if (ranges != M_NULLPTR && ranges->size >= sizeof(concurrentRangesV1) &&
        ranges->version >= CONCURRENT_RANGES_VERSION_V1)
    {
        if (device->drive_info.drive_type == ATA_DRIVE)
        {
            uint32_t concurrentLogSizeBytes =
                0; // NOTE: spec currently says this is at most 1024 bytes, but may be as low as 512
            if (SUCCESS == get_ATA_Log_Size(device, ATA_LOG_CONCURRENT_POSITIONING_RANGES, &concurrentLogSizeBytes,
                                            true, false) &&
                concurrentLogSizeBytes > 0)
            {
                uint8_t* concurrentRangeLog =
                    M_REINTERPRET_CAST(uint8_t*, safe_calloc_aligned(concurrentLogSizeBytes, sizeof(uint8_t),
                                                                     device->os_info.minimumAlignment));
                if (concurrentRangeLog == M_NULLPTR)
                {
                    return MEMORY_FAILURE;
                }
                if (SUCCESS == get_ATA_Log(device, ATA_LOG_CONCURRENT_POSITIONING_RANGES, M_NULLPTR, M_NULLPTR, true,
                                           false, true, concurrentRangeLog, concurrentLogSizeBytes, M_NULLPTR, 0, 0))
                {
                    ret = SUCCESS;
                    // header is first 64bytes
                    ranges->numberOfRanges = concurrentRangeLog[0];
                    // now loop through descriptors
                    for (uint32_t offset = UINT32_C(64), rangeCounter = UINT32_C(0);
                         offset < concurrentLogSizeBytes && rangeCounter < ranges->numberOfRanges &&
                         rangeCounter < UINT32_C(15);
                         offset += UINT32_C(32), ++rangeCounter)
                    {
                        ranges->range[rangeCounter].rangeNumber             = concurrentRangeLog[offset + 0];
                        ranges->range[rangeCounter].numberOfStorageElements = concurrentRangeLog[offset + 1];
                        // ATA QWords are little endian!
                        ranges->range[rangeCounter].lowestLBA =
                            M_BytesTo8ByteValue(0, 0, concurrentRangeLog[offset + 13], concurrentRangeLog[offset + 12],
                                                concurrentRangeLog[offset + 11], concurrentRangeLog[offset + 10],
                                                concurrentRangeLog[offset + 9], concurrentRangeLog[offset + 8]);
                        ranges->range[rangeCounter].numberOfLBAs =
                            M_BytesTo8ByteValue(concurrentRangeLog[offset + 23], concurrentRangeLog[offset + 22],
                                                concurrentRangeLog[offset + 21], concurrentRangeLog[offset + 20],
                                                concurrentRangeLog[offset + 19], concurrentRangeLog[offset + 18],
                                                concurrentRangeLog[offset + 17], concurrentRangeLog[offset + 16]);
                    }
                }
                safe_free_aligned(&concurrentRangeLog);
            }
        }
        else if (device->drive_info.drive_type == SCSI_DRIVE)
        {
            uint32_t concurrentLogSizeBytes = UINT32_C(0);
            if (SUCCESS == get_SCSI_VPD_Page_Size(device, CONCURRENT_POSITIONING_RANGES, &concurrentLogSizeBytes) &&
                concurrentLogSizeBytes > UINT32_C(0))
            {
                uint8_t* concurrentRangeVPD =
                    M_REINTERPRET_CAST(uint8_t*, safe_calloc_aligned(concurrentLogSizeBytes, sizeof(uint8_t),
                                                                     device->os_info.minimumAlignment));
                if (concurrentRangeVPD == M_NULLPTR)
                {
                    return MEMORY_FAILURE;
                }
                if (SUCCESS == get_SCSI_VPD(device, CONCURRENT_POSITIONING_RANGES, M_NULLPTR, M_NULLPTR, true,
                                            concurrentRangeVPD, concurrentLogSizeBytes, M_NULLPTR))
                {
                    ret = SUCCESS;
                    // calculate number of ranges based on page length
                    ranges->numberOfRanges =
                        C_CAST(uint8_t, (M_BytesTo2ByteValue(concurrentRangeVPD[2], concurrentRangeVPD[3]) - 60) /
                                            32); //-60 since page length doesn't include first 4 bytes and descriptors
                                                 // start at offset 64. Each descriptor is 32B long
                    // loop through descriptors
                    for (uint32_t offset = UINT32_C(64), rangeCounter = UINT32_C(0);
                         offset < concurrentLogSizeBytes && rangeCounter < ranges->numberOfRanges &&
                         rangeCounter < UINT32_C(15);
                         offset += UINT32_C(32), ++rangeCounter)
                    {
                        ranges->range[rangeCounter].rangeNumber             = concurrentRangeVPD[offset + 0];
                        ranges->range[rangeCounter].numberOfStorageElements = concurrentRangeVPD[offset + 1];
                        // SCSI is big endian
                        ranges->range[rangeCounter].lowestLBA =
                            M_BytesTo8ByteValue(concurrentRangeVPD[offset + 8], concurrentRangeVPD[offset + 9],
                                                concurrentRangeVPD[offset + 10], concurrentRangeVPD[offset + 11],
                                                concurrentRangeVPD[offset + 12], concurrentRangeVPD[offset + 13],
                                                concurrentRangeVPD[offset + 14], concurrentRangeVPD[offset + 15]);
                        ranges->range[rangeCounter].numberOfLBAs =
                            M_BytesTo8ByteValue(concurrentRangeVPD[offset + 16], concurrentRangeVPD[offset + 17],
                                                concurrentRangeVPD[offset + 18], concurrentRangeVPD[offset + 19],
                                                concurrentRangeVPD[offset + 20], concurrentRangeVPD[offset + 21],
                                                concurrentRangeVPD[offset + 22], concurrentRangeVPD[offset + 23]);
                    }
                }
                safe_free_aligned(&concurrentRangeVPD);
            }
        }
    }
    else
    {
        ret = BAD_PARAMETER;
    }
    RESTORE_NONNULL_COMPARE
    return ret;
}

void print_Concurrent_Positioning_Ranges(ptrConcurrentRanges ranges)
{
    DISABLE_NONNULL_COMPARE
    if (ranges != M_NULLPTR && ranges->size >= sizeof(concurrentRangesV1) &&
        ranges->version >= CONCURRENT_RANGES_VERSION_V1)
    {
        printf("====Concurrent Positioning Ranges====\n");
        printf("\nRange#\t#Elements\t          Lowest LBA     \t   # of LBAs      \n");
        for (uint8_t rangeCounter = UINT8_C(0); rangeCounter < ranges->numberOfRanges && rangeCounter < 15;
             ++rangeCounter)
        {
            if (ranges->range[rangeCounter].numberOfStorageElements)
            {
                printf("  %2" PRIu8 "  \t   %3" PRIu8 "   \t%20" PRIu64 "\t%20" PRIu64 "\n",
                       ranges->range[rangeCounter].rangeNumber, ranges->range[rangeCounter].numberOfStorageElements,
                       ranges->range[rangeCounter].lowestLBA, ranges->range[rangeCounter].numberOfLBAs);
            }
            else
            {
                printf("  %2" PRIu8 "  \t   N/A   \t%20" PRIu64 "\t%20" PRIu64 "\n",
                       ranges->range[rangeCounter].rangeNumber, ranges->range[rangeCounter].lowestLBA,
                       ranges->range[rangeCounter].numberOfLBAs);
            }
        }
    }
    else
    {
        printf("ERROR: Incompatible concurrent ranges data structure. Cannot print the data.\n");
    }
    RESTORE_NONNULL_COMPARE
}

eReturnValues get_Write_Read_Verify_Info(tDevice* device, ptrWRVInfo info)
{
    eReturnValues ret = NOT_SUPPORTED;
    DISABLE_NONNULL_COMPARE
    if (device == M_NULLPTR || info == M_NULLPTR)
    {
        return BAD_PARAMETER;
    }
    RESTORE_NONNULL_COMPARE
    if (device->drive_info.drive_type == ATA_DRIVE)
    {
        ret = SUCCESS;
        // check identify data
        info->bytesBeingVerified = 0; // start by setting to zero
        if (is_ATA_Identify_Word_Valid(le16_to_host(device->drive_info.IdentifyData.ata.Word086)) &&
            le16_to_host(device->drive_info.IdentifyData.ata.Word086) & BIT15) // words 119, 120 valid
        {
            if (is_ATA_Identify_Word_Valid_With_Bits_14_And_15(
                    le16_to_host(device->drive_info.IdentifyData.ata.Word119)) &&
                le16_to_host(device->drive_info.IdentifyData.ata.Word119) & BIT1) // supported
            {
                info->supported = true;
                if (is_ATA_Identify_Word_Valid_With_Bits_14_And_15(
                        le16_to_host(device->drive_info.IdentifyData.ata.Word120)) &&
                    le16_to_host(device->drive_info.IdentifyData.ata.Word120) & BIT1)
                {
                    info->enabled = true;
                }
                if (is_ATA_Identify_Word_Valid(le16_to_host(device->drive_info.IdentifyData.ata.Word220)))
                {
                    info->currentWRVMode = M_Byte0(device->drive_info.IdentifyData.ata.Word220);
                }
                // filling in remaining data because it is possible some drives will report valid data for these other
                // modes even if not enabled.
                if (info->currentWRVMode == ATA_WRV_MODE_USER &&
                    (is_ATA_Identify_Word_Valid(le16_to_host(device->drive_info.IdentifyData.ata.Word211)) &&
                     is_ATA_Identify_Word_Valid(le16_to_host(device->drive_info.IdentifyData.ata.Word210))))
                {
                    info->wrv3sectorCount =
                        M_WordsTo4ByteValue(le16_to_host(device->drive_info.IdentifyData.ata.Word211),
                                            device->drive_info.IdentifyData.ata.Word210);
                }
                if (info->currentWRVMode == ATA_WRV_MODE_VENDOR &&
                    (is_ATA_Identify_Word_Valid(le16_to_host(device->drive_info.IdentifyData.ata.Word213)) &&
                     is_ATA_Identify_Word_Valid(le16_to_host(device->drive_info.IdentifyData.ata.Word212))))
                {
                    info->wrv2sectorCount =
                        M_WordsTo4ByteValue(le16_to_host(device->drive_info.IdentifyData.ata.Word213),
                                            device->drive_info.IdentifyData.ata.Word212);
                }

                if (info->enabled)
                {
                    switch (info->currentWRVMode)
                    {
                    case ATA_WRV_MODE_ALL:
                        info->bytesBeingVerified = UINT64_MAX;
                        break;
                    case ATA_WRV_MODE_65536:
                        info->bytesBeingVerified = UINT64_C(65536) * device->drive_info.deviceBlockSize;
                        break;
                    case ATA_WRV_MODE_VENDOR:
                        info->bytesBeingVerified =
                            C_CAST(uint64_t, info->wrv2sectorCount) * device->drive_info.deviceBlockSize;
                        break;
                    case ATA_WRV_MODE_USER:
                        info->bytesBeingVerified =
                            C_CAST(uint64_t, info->wrv3sectorCount) * device->drive_info.deviceBlockSize;
                        break;
                    default: // handle any case not currently defined in the specifications.
                        info->bytesBeingVerified = 0;
                        break;
                    }
                }
            }
        }
    }
    return ret;
}

void print_Write_Read_Verify_Info(ptrWRVInfo info)
{
    DISABLE_NONNULL_COMPARE
    if (info != M_NULLPTR)
    {
        printf("\n=====Write-Read-Verify=====\n");
        if (info->supported)
        {
            if (info->enabled)
            {
                DECLARE_ZERO_INIT_ARRAY(char, capUnitarry, UNIT_STRING_LENGTH);
                DECLARE_ZERO_INIT_ARRAY(char, metUnitarry, UNIT_STRING_LENGTH);
                char*  capUnit = &capUnitarry[0];
                char*  metUnit = &metUnitarry[0];
                double capD = C_CAST(double, info->bytesBeingVerified), metD = C_CAST(double, info->bytesBeingVerified);
                printf("Enabled\n");
                printf("Mode: ");
                if (info->bytesBeingVerified > 0 && info->bytesBeingVerified != UINT64_MAX)
                {
                    capacity_Unit_Convert(&capD, &capUnit);
                    metric_Unit_Convert(&metD, &metUnit);
                }
                else
                {
                    snprintf_err_handle(capUnit, UNIT_STRING_LENGTH, "B");
                    snprintf_err_handle(metUnit, UNIT_STRING_LENGTH, "B");
                }
                switch (info->currentWRVMode)
                {
                case ATA_WRV_MODE_ALL:
                    printf("0\nVerifying: All Sectors\n");
                    break;
                case ATA_WRV_MODE_65536:
                    printf("1\nVerifying: First 65536 written sectors.\n");
                    printf("Verify Capacity (%s/%s): %0.02f/%0.02f\n", capUnit, metUnit, capD, metD);
                    break;
                case ATA_WRV_MODE_VENDOR:
                    printf("2\nVerifying: First %" PRIu32 " written sectors.\n", info->wrv2sectorCount);
                    printf("Verify Capacity (%s/%s): %0.02f/%0.02f\n", capUnit, metUnit, capD, metD);
                    break;
                case ATA_WRV_MODE_USER:
                    printf("3\nVerifying: First %" PRIu32 " written sectors.\n", info->wrv3sectorCount);
                    printf("Verify Capacity (%s/%s): %0.02f/%0.02f\n", capUnit, metUnit, capD, metD);
                    break;
                }
            }
            else
            {
                printf("Supported, but not Enabled\n");
            }
        }
        else
        {
            printf("Not Supported\n");
        }
    }
    RESTORE_NONNULL_COMPARE
}

eReturnValues disable_Write_Read_Verify(tDevice* device)
{
    eReturnValues ret = NOT_SUPPORTED;
    if (device->drive_info.drive_type == ATA_DRIVE)
    {
        ret = ata_Set_Features(device, SF_DISABLE_WRITE_READ_VERIFY_FEATURE, 0, 0, 0, 0);
    }
    return ret;
}

eReturnValues set_Write_Read_Verify(tDevice* device, bool all, bool vendorSpecific, uint32_t wrvSectorCount)
{
    eReturnValues ret = NOT_SUPPORTED;
    if (device->drive_info.drive_type == ATA_DRIVE)
    {
        if (all && vendorSpecific)
        {
            return BAD_PARAMETER;
        }
        else if (all)
        {
            ret = ata_Set_Features(device, SF_ENABLE_WRITE_READ_VERIFY_FEATURE, 0, ATA_WRV_MODE_ALL, 0, 0);
        }
        else if (vendorSpecific)
        {
            ret = ata_Set_Features(device, SF_ENABLE_WRITE_READ_VERIFY_FEATURE, 0, ATA_WRV_MODE_VENDOR, 0, 0);
        }
        else if (wrvSectorCount == 65536) // Detecting this very specific number to make this mode since it is in the
                                          // spec and this is probably what would be wanted in this case.
        {
            ret = ata_Set_Features(device, SF_ENABLE_WRITE_READ_VERIFY_FEATURE, 0, ATA_WRV_MODE_65536, 0, 0);
        }
        else
        {
            // divide the incoming value by 1024 to set the count register.
            // Before doing the devision, round up to nearest. If this would overflow, set count to maximum
            uint8_t count = UINT8_C(0);
            if (wrvSectorCount >= MAX_WRV_USER_SECTORS)
            {
                count = UINT8_MAX; // FFh
            }
            else
            {
                // this math is rounding up.
                // If someone selected a value not evenly divisible by 1024, they likely want at LEAST that many
                // sectors being verified rather than not enough, so rounding up here makes the most sense -TJE
                count = C_CAST(uint8_t, ((wrvSectorCount + (WRV_USER_MULTIPLIER - 1)) / WRV_USER_MULTIPLIER));
            }
            ret = ata_Set_Features(device, SF_ENABLE_WRITE_READ_VERIFY_FEATURE, count, ATA_WRV_MODE_USER, 0, 0);
        }
    }
    return ret;
}

eOSFeatureSupported is_Block_Sanitize_Operation_Supported(tDevice* device)
{
    eOSFeatureSupported featureSupported = OS_FEATURE_UNKNOWN;

    if (device->drive_info.drive_type == NVME_DRIVE) // If NVMe drive
    {
        if (device->drive_info.interface_type == USB_INTERFACE) // If USB_INTERFACE
        {
            if (device->drive_info.passThroughHacks.passthroughType == NVME_PASSTHROUGH_JMICRON ||
                device->drive_info.passThroughHacks.passthroughType ==
                    NVME_PASSTHROUGH_ASMEDIA) // JMICRON or ASMEDIA, than supported
            {
                featureSupported = OS_FEATURE_SUPPORTED;
            }
            else if (device->drive_info.passThroughHacks.ataPTHacks
                         .ata28BitOnly) // Only supported when can send 48bit SAT commands
            {
                featureSupported = OS_FEATURE_ADAPTER_BLOCKS;
            }
        }
        else // Non USB_INTERFACE
        {
#if defined(_WIN32)
            if (is_Windows_PE()) // If Windows PE, than supported
            {
                featureSupported = OS_FEATURE_SUPPORTED;
            }
            else
            {
                if (device->os_info.fileSystemInfo.fileSystemInfoValid &&
                    device->os_info.fileSystemInfo.isSystemDisk) // If boot drive than not supported
                    featureSupported = OS_FEATURE_OS_BLOCKS;
                else if (!is_Windows_10_Version_1903_Or_Higher()) // If not 1903 and higher, if not, than not supported
                {
                    featureSupported = OS_FEATURE_OS_BLOCKS;
                }
                else
                {
                    featureSupported = OS_FEATURE_SUPPORTED;
                }
            }
#else
            if (device->os_info.fileSystemInfo.fileSystemInfoValid &&
                device->os_info.fileSystemInfo.isSystemDisk) // If boot drive than not supported
                featureSupported = OS_FEATURE_OS_BLOCKS;
            else
                featureSupported = OS_FEATURE_SUPPORTED;
#endif
        }
    }
    else // If SATA/SAS drive
    {
#if defined(_WIN32)
        if (is_Windows_PE()) // If Windows_PE, than supported
        {
            featureSupported = OS_FEATURE_SUPPORTED;
        }
        else if (is_Windows_8_Or_Higher()) // If Windows_8_or_higher
        {
            if ((device->drive_info.drive_type == ATA_DRIVE && device->drive_info.interface_type == IDE_INTERFACE) ||
                (device->drive_info.drive_type == SCSI_DRIVE && device->drive_info.interface_type == SCSI_INTERFACE))
            {
                featureSupported = OS_FEATURE_INTERFACE_BLOCKS;
            }
        }
#else
        featureSupported = OS_FEATURE_SUPPORTED;
#endif
    }

    return featureSupported;
}

eOSFeatureSupported is_Crypto_Sanitize_Operation_Supported(tDevice* device)
{
    eOSFeatureSupported featureSupported = OS_FEATURE_UNKNOWN;

    if (device->drive_info.drive_type == NVME_DRIVE) // If NVMe drive
    {
        if (device->drive_info.interface_type == USB_INTERFACE) // If USB_INTERFACE
        {
            if (device->drive_info.passThroughHacks.passthroughType == NVME_PASSTHROUGH_JMICRON ||
                device->drive_info.passThroughHacks.passthroughType ==
                    NVME_PASSTHROUGH_ASMEDIA) // JMICRON or ASMEDIA, than supported
            {
                featureSupported = OS_FEATURE_SUPPORTED;
            }
            else if (device->drive_info.passThroughHacks.ataPTHacks
                         .ata28BitOnly) // Only supported when can send 48bit SAT commands
            {
                featureSupported = OS_FEATURE_ADAPTER_BLOCKS;
            }
        }
        else // Non USB_INTERFACE
        {
#if defined(_WIN32)
            if (is_Windows_PE()) // If Windows PE, than supported
            {
                featureSupported = OS_FEATURE_SUPPORTED;
            }
            else
            {
                if (device->os_info.fileSystemInfo.fileSystemInfoValid &&
                    device->os_info.fileSystemInfo.isSystemDisk) // If boot drive than not supported
                    featureSupported = OS_FEATURE_OS_BLOCKS;
                else if (!is_Windows_10_Version_1903_Or_Higher()) // If not 1903 and higher, if not, than not supported
                {
                    featureSupported = OS_FEATURE_OS_BLOCKS;
                }
                else
                {
                    featureSupported = OS_FEATURE_SUPPORTED;
                }
            }
#else
            if (device->os_info.fileSystemInfo.fileSystemInfoValid &&
                device->os_info.fileSystemInfo.isSystemDisk) // If boot drive than not supported
                featureSupported = OS_FEATURE_OS_BLOCKS;
            else
                featureSupported = OS_FEATURE_SUPPORTED;
#endif
        }
    }
    else // If SATA/SAS drive
    {
#if defined(_WIN32)
        if (is_Windows_PE()) // If Windows_PE, than supported
        {
            featureSupported = OS_FEATURE_SUPPORTED;
        }
        else if (is_Windows_8_Or_Higher()) // If Windows_8_or_higher
        {
            if ((device->drive_info.drive_type == ATA_DRIVE && device->drive_info.interface_type == IDE_INTERFACE) ||
                (device->drive_info.drive_type == SCSI_DRIVE && device->drive_info.interface_type == SCSI_INTERFACE))
            {
                featureSupported = OS_FEATURE_INTERFACE_BLOCKS;
            }
        }
#else
        featureSupported = OS_FEATURE_SUPPORTED;
#endif
    }

    return featureSupported;
}

eOSFeatureSupported is_Overwrite_Sanitize_Operation_Supported(tDevice* device)
{
    eOSFeatureSupported featureSupported = OS_FEATURE_UNKNOWN;

    if (device->drive_info.drive_type == NVME_DRIVE) // If NVMe drive
    {
        if (device->drive_info.interface_type == USB_INTERFACE) // If USB_INTERFACE
        {
            if (device->drive_info.passThroughHacks.passthroughType == NVME_PASSTHROUGH_JMICRON ||
                device->drive_info.passThroughHacks.passthroughType ==
                    NVME_PASSTHROUGH_ASMEDIA) // JMICRON or ASMEDIA, than supported
            {
                featureSupported = OS_FEATURE_SUPPORTED;
            }
            else if (device->drive_info.passThroughHacks.ataPTHacks
                         .ata28BitOnly) // Only supported when can send 48bit SAT commands
            {
                featureSupported = OS_FEATURE_ADAPTER_BLOCKS;
            }
        }
        else // Non USB_INTERFACE
        {
#if defined(_WIN32)
            if (is_Windows_PE()) // If Windows PE, than supported
            {
                featureSupported = OS_FEATURE_SUPPORTED;
            }
#else
            if (device->os_info.fileSystemInfo.fileSystemInfoValid &&
                device->os_info.fileSystemInfo.isSystemDisk) // If boot drive than not supported
                featureSupported = OS_FEATURE_OS_BLOCKS;
            else
                featureSupported = OS_FEATURE_SUPPORTED;
#endif
        }
    }
    else // If SATA/SAS drive
    {
#if defined(_WIN32)
        if (is_Windows_PE()) // If Windows_PE, than supported
        {
            featureSupported = OS_FEATURE_SUPPORTED;
        }
        else if (is_Windows_8_Or_Higher()) // If Windows_8_or_higher
        {
            if ((device->drive_info.drive_type == ATA_DRIVE && device->drive_info.interface_type == IDE_INTERFACE) ||
                (device->drive_info.drive_type == SCSI_DRIVE && device->drive_info.interface_type == SCSI_INTERFACE))
            {
                featureSupported = OS_FEATURE_INTERFACE_BLOCKS;
            }
        }
#else
        featureSupported = OS_FEATURE_SUPPORTED;
#endif
    }

    return featureSupported;
}

eOSFeatureSupported is_NVMe_Format_Operation_Supported(tDevice* device)
{
    eOSFeatureSupported featureSupported = OS_FEATURE_UNKNOWN;

    if (device->drive_info.drive_type == NVME_DRIVE) // If NVMe drive
    {
        if (device->drive_info.interface_type == USB_INTERFACE) // If USB_INTERFACE
        {
            if (device->drive_info.passThroughHacks.passthroughType == NVME_PASSTHROUGH_JMICRON ||
                device->drive_info.passThroughHacks.passthroughType ==
                    NVME_PASSTHROUGH_ASMEDIA) // JMICRON or ASMEDIA, than supported
            {
                featureSupported = OS_FEATURE_SUPPORTED;
            }
        }
        else // Non USB_INTERFACE
        {
#if defined(_WIN32)
            if (is_Windows_PE()) // If Windows PE, than supported
            {
                featureSupported = OS_FEATURE_SUPPORTED;
            }
#else
            featureSupported = OS_FEATURE_SUPPORTED;
#endif
        }
    }

    return featureSupported;
}

eOSFeatureSupported is_SCSI_Format_Unit_Operation_Supported(tDevice* device)
{
    eOSFeatureSupported featureSupported = OS_FEATURE_UNKNOWN;

    if (device->drive_info.interface_type == USB_INTERFACE) // If USB_INTERFACE
    {
        // Some devices may support the most basic version of this command,
        // but it is better to just disable it since it likely won't do what we want
        featureSupported = OS_FEATURE_INTERFACE_BLOCKS;
    }
    else if (device->drive_info.interface_type == SCSI_INTERFACE &&
             device->drive_info.drive_type == ATA_DRIVE) // If SATA drive on a SCSI_INTERFACE
    {
        // It MAY be supported, but it will most likely just return without running anything.
        // It is recommended to disable it in this case because it doesn't really do anything useful.
        featureSupported = OS_FEATURE_INTERFACE_BLOCKS;
    }
    else
    {
        featureSupported = OS_FEATURE_SUPPORTED;
    }

    return featureSupported;
}

eOSFeatureSupported is_SMART_Check_Operation_Supported(M_ATTR_UNUSED tDevice* device)
{
    eOSFeatureSupported featureSupported = OS_FEATURE_SUPPORTED;

#if defined(_WIN32)
    if (device->os_info.ioType == WIN_IOCTL_BASIC)
        featureSupported = OS_FEATURE_OS_BLOCKS;
    else if (device->drive_info.drive_type == NVME_DRIVE && device->drive_info.interface_type == SCSI_INTERFACE &&
             strcmp(device->drive_info.T10_vendor_ident, "NVMe") ==
                 0) // SCSI Vendor ID is set to NVMe, the Interface is SCSI_INTERFACE, drive is NVMe, then not supported
        featureSupported = OS_FEATURE_OS_BLOCKS;
#endif

    return featureSupported;
}

eOSFeatureSupported is_DST_Operation_Supported(tDevice* device)
{
    eOSFeatureSupported featureSupported = OS_FEATURE_UNKNOWN;

    if (device->drive_info.drive_type == NVME_DRIVE) // If NVMe drive
    {
        if (device->drive_info.interface_type == USB_INTERFACE) // If USB_INTERFACE
        {
            if (device->drive_info.passThroughHacks.passthroughType == NVME_PASSTHROUGH_JMICRON ||
                device->drive_info.passThroughHacks.passthroughType ==
                    NVME_PASSTHROUGH_ASMEDIA) // JMICRON or ASMEDIA, than supported
            {
                featureSupported = OS_FEATURE_SUPPORTED;
#if !defined(_WIN32)
                if ((device->drive_info.passThroughHacks.passthroughType == NVME_PASSTHROUGH_JMICRON &&
                     device->drive_info.adapter_info.vendorID ==
                         0x0BC2)) // For linux, if JMICRON and 0BC2h vendor, then not supported
                {
                    featureSupported = OS_FEATURE_ADAPTER_BLOCKS;
                }
#endif
            }
        }
        else if (device->drive_info.interface_type == SCSI_INTERFACE &&
                 (strcmp(device->drive_info.T10_vendor_ident, "NVMe") ==
                  0)) // SCSI Vendor ID is set to NVMe, the Interface is SCSI_INTERFACE, drive is NVMe, then not
                      // supported
        {
            featureSupported = OS_FEATURE_OS_BLOCKS;
        }
        else
        {
            featureSupported = OS_FEATURE_SUPPORTED;
        }
    }
    else // If SATA/SAS drive
    {
#if defined(_WIN32)
        if (device->os_info.ioType == WIN_IOCTL_BASIC)
            featureSupported = OS_FEATURE_OS_BLOCKS;
        else
            featureSupported = OS_FEATURE_SUPPORTED;
#else
        featureSupported = OS_FEATURE_SUPPORTED;
#endif
    }

#if defined(_WIN32)
    if (!is_Windows_PE() && !is_Windows_10_Version_1903_Or_Higher())
    {
        featureSupported = OS_FEATURE_OS_BLOCKS;
    }
#endif

    return featureSupported;
}

eOSFeatureSupported is_ATA_Secure_Erase_Operation_Supported(M_ATTR_UNUSED tDevice* device)
{
    eOSFeatureSupported featureSupported = OS_FEATURE_UNKNOWN;

#if defined(_WIN32)
    if (device->os_info.ioType == WIN_IOCTL_BASIC || device->os_info.ioType == WIN_IOCTL_SMART_ONLY ||
        device->os_info.ioType == WIN_IOCTL_SMART_AND_IDE) // Not supported for WIN_IOCTL_BASIC or WIN_IOCTL_SMART_ONLY
                                                           // or WIN_IOCTL_SMART_AND_IDE
        featureSupported = OS_FEATURE_OS_BLOCKS;
    else if (!is_Windows_PE() && !is_Windows_8_Or_Higher() &&
             (device->drive_info.interface_type == USB_INTERFACE ||
              device->drive_info.interface_type ==
                  SCSI_INTERFACE)) // Non PE windows which are older than 8 will not support for USB or SCSI interface
        featureSupported = OS_FEATURE_OS_BLOCKS;
    else
        featureSupported = OS_FEATURE_SUPPORTED;
#else
    featureSupported = OS_FEATURE_SUPPORTED;
#endif

    return featureSupported;
}
