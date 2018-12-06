//
// Do NOT modify or remove this copyright and license
//
// Copyright (c) 2012 - 2018 Seagate Technology LLC and/or its Affiliates, All Rights Reserved
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
#include "format_unit.h"
#include "dst.h"

int get_Ready_LED_State(tDevice *device, bool *readyLEDOnOff)
{
    int ret = UNKNOWN;
    if (device->drive_info.drive_type == SCSI_DRIVE)
    {
        uint8_t *modeSense = (uint8_t*)calloc(24, sizeof(uint8_t));
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
        safe_Free(modeSense);
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
        uint8_t *modeSelect = (uint8_t*)calloc(24, sizeof(uint8_t));
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
            ret = scsi_Mode_Select_10(device, 24, true, true, modeSelect, 24);
        }
        safe_Free(modeSelect);
    }
    else //ata cannot control ready LED since it is managed by the host, not the drive (drive just reads a signal to change operation as per ATA spec). Not sure if other device types support this change or not at this time.
    {
        ret = NOT_SUPPORTED;
    }
    return ret;
}

int scsi_Set_Read_Look_Ahead(tDevice *device, bool readLookAheadEnableDisable)
{
    int ret = UNKNOWN;
    //on SAS we change this through a mode page
    uint8_t *cachingModePage = (uint8_t*)calloc(MP_CACHING_LEN + MODE_PARAMETER_HEADER_10_LEN, sizeof(uint8_t));
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
        ret = scsi_Mode_Select_10(device, MP_CACHING_LEN + MODE_PARAMETER_HEADER_10_LEN, true, true, cachingModePage, MP_CACHING_LEN + MODE_PARAMETER_HEADER_10_LEN);
    }
    safe_Free(cachingModePage);
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
    uint8_t *cachingModePage = (uint8_t*)calloc(MP_CACHING_LEN + MODE_PARAMETER_HEADER_10_LEN, sizeof(uint8_t));
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
        ret = scsi_Mode_Select_10(device, MP_CACHING_LEN + MODE_PARAMETER_HEADER_10_LEN, true, true, cachingModePage, MP_CACHING_LEN + MODE_PARAMETER_HEADER_10_LEN);
    }
    safe_Free(cachingModePage);
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
	uint8_t *cachingModePage = (uint8_t*)calloc(MP_CACHING_LEN + MODE_PARAMETER_HEADER_10_LEN, sizeof(uint8_t));
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
	safe_Free(cachingModePage);
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

bool scsi_Is_Read_Look_Ahead_Enabled(tDevice *device)
{
    bool enabled = false;
    //on SAS we change this through a mode page
    uint8_t *cachingModePage = (uint8_t*)calloc(MP_CACHING_LEN + MODE_PARAMETER_HEADER_10_LEN, sizeof(uint8_t));
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
    safe_Free(cachingModePage);
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
		break;
#endif
	case SCSI_DRIVE:
		return scsi_Is_Write_Cache_Supported(device);
		break;
	case ATA_DRIVE:
		return ata_Is_Write_Cache_Supported(device);
		break;
	default:
		break;
	}
	return false;
}

bool scsi_Is_Write_Cache_Supported(tDevice *device)
{
	bool supported = false;
	//on SAS we change this through a mode page
	uint8_t *cachingModePage = (uint8_t*)calloc(MP_CACHING_LEN + MODE_PARAMETER_HEADER_10_LEN, sizeof(uint8_t));
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
	safe_Free(cachingModePage);
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
        break;
#endif
    case SCSI_DRIVE:
        return scsi_Is_Write_Cache_Enabled(device);
        break;
    case ATA_DRIVE:
        return ata_Is_Write_Cache_Enabled(device);
        break;
    default:
        break;
    }
    return false;
}

bool scsi_Is_Write_Cache_Enabled(tDevice *device)
{
    bool enabled = false;
    //on SAS we change this through a mode page
    uint8_t *cachingModePage = (uint8_t*)calloc(MP_CACHING_LEN + MODE_PARAMETER_HEADER_10_LEN, sizeof(uint8_t));
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
    safe_Free(cachingModePage);
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
    bool isWriteSameSupported = is_Write_Same_Supported(device, 0, (uint32_t)device->drive_info.deviceMaxLba, &maxNumberOfLogicalBlocksPerCommand);
    bool isTrimUnmapSupported = is_Trim_Or_Unmap_Supported(device, NULL, NULL);
    bool isFormatUnitSupported = is_Format_Unit_Supported(device, NULL);
    eraseMethod * currentErase = (eraseMethod*)eraseMethodList;
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

    //next trim/unmap
    if (isTrimUnmapSupported)
    {
        currentErase->eraseIdentifier = ERASE_TRIM_UNMAP;
        snprintf(currentErase->eraseName, MAX_ERASE_NAME_LENGTH, "TRIM/UNMAP Erase");
        //snprintf(currentErase->eraseWarning, MAX_ERASE_WARNING_LENGTH, "");
        currentErase->warningValid = false;
        currentErase->eraseWeight = 3;
        ++currentErase;
    }

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
        uint32_t longDSTTimeMinutes = (uint32_t)(((uint32_t)hours * 60) + (uint32_t)minutes);
        *overwriteEraseTimeEstimateMinutes = M_Max(*overwriteEraseTimeEstimateMinutes, longDSTTimeMinutes);
        if (*overwriteEraseTimeEstimateMinutes == 0)
        {
            //This drive doesn't support anything that gives us time estimates, so let's make a guess.
            //TODO: Make this guess better by reading the drive capabilities and interface speed to determine a more accurate estimate.
            uint32_t megabytesPerSecond = is_SSD(device) ? 450 : 150;//assume 450 MB/s on SSD and 150 MB/s on HDD
            *overwriteEraseTimeEstimateMinutes = (uint32_t)(((device->drive_info.deviceMaxLba * device->drive_info.deviceBlockSize) / (megabytesPerSecond * 1.049e+6)) / 60);
        }
    }

    return ret;
}

void print_Supported_Erase_Methods(tDevice *device, eraseMethod const eraseMethodList[MAX_SUPPORTED_ERASE_METHODS], uint32_t *overwriteEraseTimeEstimateMinutes)
{
    uint8_t counter = 0;
    bool cryptoSupported = false;
    bool trimUnmapSupported = false;
    bool sanitizeBlockEraseSupported = false;
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
        case ERASE_TRIM_UNMAP:
            trimUnmapSupported = true;
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
        convert_Seconds_To_Displayable_Time((uint64_t)(*overwriteEraseTimeEstimateMinutes * 60), NULL, &days, &hours, &minutes, &seconds);
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
        if (trimUnmapSupported || sanitizeBlockEraseSupported)
        {
            printf("Trim/Unmap & blockerase should also complete in under a minute.\n");
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
        controlModePage[byteOffset] |= BIT2;//set the bit to 1
    }
    else
    {
        controlModePage[byteOffset] &= (uint8_t)(~BIT2);//set the bit to 0
    }
    //write the change to the drive
    if (mode6ByteCmd)
    {
        ret = scsi_Mode_Select_6(device, MODE_PARAMETER_HEADER_6_LEN + 12, true, saveParameters, controlModePage, MODE_PARAMETER_HEADER_6_LEN + 12);
    }
    else
    {
        ret = scsi_Mode_Select_10(device, MODE_PARAMETER_HEADER_10_LEN + 12, true, saveParameters, controlModePage, MODE_PARAMETER_HEADER_10_LEN + 12);
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
	scsiStatus returnedStatus = { 0 };
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

bool check_Duplicate_Drive(tDevice *deviceList, uint32_t deviceIdx)
{
	bool duplicateDrive = false;
	uint32_t deviceIter;

	for (deviceIter = 0; deviceIter < (deviceIdx - 1); deviceIter++)
	{
		duplicateDrive = (strncmp((deviceList + deviceIter)->drive_info.serialNumber,
			(deviceList + deviceIdx)->drive_info.serialNumber,
			strlen((deviceList + deviceIter)->drive_info.serialNumber)) == 0);

		if (duplicateDrive == true)
			return duplicateDrive;
	}

	return duplicateDrive;
}

void remove_Duplicate_Drives(tDevice *deviceList, volatile uint32_t * numberOfDevices, removeDuplicateDriveType rmvDevFlag)
{
	volatile uint32_t i, j;
	bool sameSlNo = false;

	for (i = 0; i < *numberOfDevices; i++)
	{
		for (j = 0; j < *numberOfDevices; j++)
		{
#ifdef _DEBUG
			printf("%s --> For drive i : %d and j : %d \n", __FUNCTION__, i, j);
#endif

			sameSlNo = false;
			if (i == j)
			{
				continue;
			}
			
			sameSlNo =	(strncmp((deviceList + i)->drive_info.serialNumber,
						(deviceList + j)->drive_info.serialNumber,
						strlen((deviceList + i)->drive_info.serialNumber)) == 0);

			if (sameSlNo)
			{
#ifdef _DEBUG
				printf("We have same serial no \n");
#endif
#if defined (_WIN32)
				/* We are supporting csmi only - for now */
				if (rmvDevFlag.csmi != 0)
				{
					if (is_CSMI_Device(deviceList + i))
					{
						remove_Drive(deviceList, i, numberOfDevices);
						*numberOfDevices -= 1;
						i--;
						if (j > i)
						{
							j--;
						}
					}

					if (is_CSMI_Device(deviceList + j))
					{
						remove_Drive(deviceList, j, numberOfDevices);
						*numberOfDevices -= 1;
						j--;
						if (i > j)
						{
							i--;
						}
					}
				}
				
#endif
			}
		}
	}
}

void remove_Drive(tDevice *deviceList, uint32_t driveToRemoveIdx, uint32_t * numberOfDevices)
{
	uint32_t i;

#ifdef _DEBUG
	printf("Removing Drive with index : %d \n", driveToRemoveIdx);
#endif

	if ((deviceList + driveToRemoveIdx)->raid_device != NULL)
	{
		free((deviceList + driveToRemoveIdx)->raid_device);
	}

	for (i = driveToRemoveIdx; i < *numberOfDevices - 1; i++)
	{
		memcpy((deviceList + driveToRemoveIdx), (deviceList + driveToRemoveIdx + 1), sizeof(tDevice));
	}

	memset((deviceList + i), 0, sizeof(tDevice));
}

bool is_CSMI_Device(tDevice *device)
{
	bool csmiDevice = true;

#ifdef _DEBUG
	printf("friendly name : %s interface_type : %d raid_device : %x \n", 
		device->os_info.friendlyName, device->drive_info.interface_type, device->raid_device);
#endif

	csmiDevice = csmiDevice && (strncmp(device->os_info.friendlyName, "SCSI", 4) == 0);
	csmiDevice = csmiDevice && (device->drive_info.interface_type == RAID_INTERFACE);
	csmiDevice = csmiDevice && (device->raid_device != NULL);

#ifdef _DEBUG
	if (csmiDevice)
	{
		printf("This is a CSMI drive \n");
	}
	else
	{
		printf("This is not a CSMI drive \n");
	}
#endif
	return csmiDevice;
}

#if !defined (DISABLE_NVME_PASSTHROUGH)

int clr_Pcie_Correctable_Errs(tDevice *device)
{
    const char *desc = "Clear Seagate PCIe Correctable counters for the given device ";
    const char *save = "specifies that the controller shall save the attribute";
    int err;
    void *buf = NULL;

    struct config {
        int   save;
    };

    struct config cfg = {
        .save         = 0,
    };
    err = nvme_set_feature( device, 0, 0xE1, 0xCB, cfg.save, 0, buf);
	if (err < 0) {
        perror("set-feature");
        return errno;
    }

    return err;

}

int nvme_set_feature(tDevice *device, unsigned int nsid,unsigned char fid, unsigned int value, bool save, unsigned int  data_len, void *data)
{
	unsigned int cdw10 = fid | (save ? 1 << 31 : 0);

	return pci_Correctble_Err( device, 0x09, nsid, cdw10, value, data_len, data);
}

#endif