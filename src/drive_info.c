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
#include "unit_conversion.h"
#include "time_utils.h"

#include "drive_info.h"
#include "operations.h" //this is for read-look ahead and write cache information
#include "logs.h"
#include "set_max_lba.h"
#include "smart.h"
#include "dst.h"
#include "ata_helper.h"
#include "scsi_helper.h"
#include "nvme_helper_func.h"
#include "firmware_download.h"
#include "usb_hacks.h"
#include <ctype.h>
#include "vendor/seagate/seagate_ata_types.h"
#include "vendor/seagate/seagate_scsi_types.h"

static bool add_Feature_To_Supported_List(char featuresSupported[MAX_FEATURES][MAX_FEATURE_LENGTH], uint8_t *numberOfFeaturesSupported, const char* featureString)
{
    bool success = true;
    if (featuresSupported && numberOfFeaturesSupported && featureString)
    {
        if ((*numberOfFeaturesSupported) < MAX_FEATURES)
        {
            snprintf(featuresSupported[*numberOfFeaturesSupported], MAX_FEATURE_LENGTH, "%s", featureString);
            (*numberOfFeaturesSupported) += 1;
        }
        else
        {
            success = false;
#if defined (_DEBUG)
            printf("Out of room in feature list!\n");
#endif //_DEBUG
        }
    }
    else
    {
        success = false;
#if defined (_DEBUG)
        printf("Out of room in feature list!\n");
#endif //_DEBUG
    }
    return success;
}

static bool add_Specification_To_Supported_List(char specificationsSupported[MAX_SPECS][MAX_SPEC_LENGTH], uint8_t* numberOfSpecificationsSupported, const char* specificationString)
{
    bool success = true;
    if (specificationsSupported && numberOfSpecificationsSupported && specificationString)
    {
        if ((*numberOfSpecificationsSupported) < MAX_SPECS)
        {
            snprintf(specificationsSupported[*numberOfSpecificationsSupported], MAX_SPEC_LENGTH, "%s", specificationString);
            (*numberOfSpecificationsSupported) += 1;
        }
        else
        {
            success = false;
#if defined (_DEBUG)
            printf("Out of room in specification list!\n");
#endif //_DEBUG
        }
    }
    else
    {
        success = false;
#if defined (_DEBUG)
        printf("Out of room in specification list!\n");
#endif //_DEBUG
    }
    return success;
}

//This is an internal structure that's purpose is to report capabilities out of the subfunctions processing identify data or other data so that
//additional commands to run for device discovery can be performed as needed based on these capabilities.
typedef struct _idDataCapabilitiesForDriveInfo
{
    eSeagateFamily seagateFamily;
    bool supportsIDDataLog;//used to help figure out what needs to be parsed or not out of std identify data, not already available in the log
    struct {
        bool copyOfIdentify;
        bool capacity;
        bool supportedCapabilities;
        bool currentSettings;
        bool strings;
        bool security;
        bool parallelATA;
        bool serialATA;
        bool zac2;
    }supportedIDDataPages;
    bool sctSupported;
    bool gplSupported;
    bool smartErrorLoggingSupported;
    bool smartStatusFromSCTStatusLog;
    bool tcgSupported;//for trusted send/receive commands
    bool ieee1667Supported;
    bool processedStdIDData;//set when the function that reviews the standard ID data (ECh) has already been called.
}idDataCapabilitiesForDriveInfo, *ptrIdDataCapabilitiesForDriveInfo;

static eReturnValues get_ATA_Drive_Info_From_Identify(ptrDriveInformationSAS_SATA driveInfo, ptrIdDataCapabilitiesForDriveInfo ataCapabilities, uint8_t* identify, uint32_t dataLength)
{
    eReturnValues ret = SUCCESS;
    uint16_t* wordPtr = C_CAST(uint16_t*, identify);

    if (dataLength != 512)
    {
        return BAD_PARAMETER;
    }

    ataCapabilities->processedStdIDData = true;

    //start by assuming 512B per sector. This is updated later if the drive supports a different setting.
    driveInfo->logicalSectorSize = LEGACY_DRIVE_SEC_SIZE;
    driveInfo->physicalSectorSize = LEGACY_DRIVE_SEC_SIZE;
    driveInfo->rotationRate = 0;

    //check if the really OLD Mb/s bits are set...if they are, set the speed based off of them
    //This will be changed later if other words are set.-TJE
    if (is_ATA_Identify_Word_Valid(wordPtr[0]) && M_GETBITRANGE(wordPtr[0], 10, 8) > 0)
    {
        driveInfo->interfaceSpeedInfo.speedType = INTERFACE_SPEED_ANCIENT;//ESDI bits
        driveInfo->interfaceSpeedInfo.speedIsValid = true;
        if (wordPtr[0] & BIT10)
        {
            driveInfo->interfaceSpeedInfo.ancientHistorySpeed.dataTransferGt10MbS = true;//1.25MB/s
        }
        if (wordPtr[0] & BIT9)
        {
            driveInfo->interfaceSpeedInfo.ancientHistorySpeed.dataTransferGt5MbSLte10MbS = true;//7.5Mb/s - 0.9375MB/s - used 7.5Mb/s as the middle of these values
        }
        if (wordPtr[0] & BIT8)
        {
            driveInfo->interfaceSpeedInfo.ancientHistorySpeed.dataTransferLte5MbS = true;//0.625MB/s
        }
        if (wordPtr[0] & BIT3)
        {
            driveInfo->interfaceSpeedInfo.ancientHistorySpeed.notMFMEncoded = true;
        }
    }

    //Check if CHS words are non-zero to see if the information is valid.
    if (is_ATA_Identify_Word_Valid(wordPtr[1]) && is_ATA_Identify_Word_Valid(wordPtr[3]) && is_ATA_Identify_Word_Valid(wordPtr[6]))
    {
        driveInfo->ataLegacyCHSInfo.legacyCHSValid = true;
        driveInfo->ataLegacyCHSInfo.numberOfLogicalCylinders = wordPtr[1];
        driveInfo->ataLegacyCHSInfo.numberOfLogicalHeads = M_Byte0(wordPtr[3]);
        driveInfo->ataLegacyCHSInfo.numberOfLogicalSectorsPerTrack = M_Byte0(wordPtr[6]);
        //According to ATA, word 53, bit 0 set to 1 means the words 54,-58 are valid.
        //if set to zero they MAY be valid....so just check validity on everything
    }

    //buffer type is in word 20. According to very old product manuals, if this is set to 3, then read-look ahead is supported.
    if (is_ATA_Identify_Word_Valid(wordPtr[20]))
    {
        if (wordPtr[20] == 0x0003)
        {
            driveInfo->readLookAheadSupported = true;
            //NOTE: It is not possible to determine whether this is currently enabled or not.
        }
    }
    //cache size (legacy method - from ATA 1) Word 21
    //note: Changed from multiplying by logical sector size to 512 as that is what ATA says this is increments of.
    if (is_ATA_Identify_Word_Valid(wordPtr[21]))
    {
        driveInfo->cacheSize = C_CAST(uint64_t, wordPtr[21]) * 512;
    }

    //these are words 10-19, 23-26, and 27-46
    fill_ATA_Strings_From_Identify_Data(identify, driveInfo->modelNumber, driveInfo->serialNumber, driveInfo->firmwareRevision);

    if (is_ATA_Identify_Word_Valid(wordPtr[47]) && M_Byte0(wordPtr[47]) > 0)
    {
        add_Feature_To_Supported_List(driveInfo->featuresSupported, &driveInfo->numberOfFeaturesSupported, "Read/Write Multiple");
    }

    if (is_ATA_Identify_Word_Valid_With_Bits_14_And_15(wordPtr[48]))
    {
        if (wordPtr[48] & BIT0)
        {
            ataCapabilities->tcgSupported = true;
        }
    }
    else if (is_ATA_Identify_Word_Valid(wordPtr[48]))
    {
        //NOTE: ATA 1 lists this as can or cannot perform doubleword I/O. This is listed as vendor unique as well.
        //      This is reserved in ATA-2 until it gets used later. This is PROBABLY safe to use without additional version checks.
        //      Most likely this was only used by one vendor
        if (wordPtr[48] == 0x0001)
        {
            add_Feature_To_Supported_List(driveInfo->featuresSupported, &driveInfo->numberOfFeaturesSupported, "Doubleword I/O");
        }
    }

    bool lbaModeSupported = false;
    bool dmaSupported = false;//to be used later when determining transfer speeds
    if (is_ATA_Identify_Word_Valid(wordPtr[49]))
    {
        if (wordPtr[49] & BIT9)
        {
            lbaModeSupported = true;
        }
        if (wordPtr[49] & BIT8)
        {
            dmaSupported = true;
        }
    }

    //Prefer word 64 over this if it is supported
    if (is_ATA_Identify_Word_Valid(wordPtr[51]))
    {
        uint8_t pioCycleTime = M_Byte1(wordPtr[51]);
        if (driveInfo->interfaceSpeedInfo.speedType != INTERFACE_SPEED_PARALLEL)
        {
            memset(&driveInfo->interfaceSpeedInfo, 0, sizeof(interfaceSpeed));//clear anything we've set so far
            driveInfo->interfaceSpeedInfo.speedType = INTERFACE_SPEED_PARALLEL;
            driveInfo->interfaceSpeedInfo.speedIsValid = true;
        }
        switch (pioCycleTime)
        {
        case 2://PIO-2
            driveInfo->interfaceSpeedInfo.parallelSpeed.maxSpeed = 8.3;
            driveInfo->interfaceSpeedInfo.parallelSpeed.maxModeNameValid = true;
            snprintf(driveInfo->interfaceSpeedInfo.parallelSpeed.maxModeName, PARALLEL_INTERFACE_MODE_NAME_MAX_LENGTH, "PIO-2");
            break;
        case 1://PIO-1
            driveInfo->interfaceSpeedInfo.parallelSpeed.maxSpeed = 5.2;
            driveInfo->interfaceSpeedInfo.parallelSpeed.maxModeNameValid = true;
            snprintf(driveInfo->interfaceSpeedInfo.parallelSpeed.maxModeName, PARALLEL_INTERFACE_MODE_NAME_MAX_LENGTH, "PIO-1");
            break;
        case 0://PIO-0
        default:
            //all others are reserved, treat as PIO-0 in this case
            driveInfo->interfaceSpeedInfo.parallelSpeed.maxSpeed = 3.3;
            driveInfo->interfaceSpeedInfo.parallelSpeed.maxModeNameValid = true;
            snprintf(driveInfo->interfaceSpeedInfo.parallelSpeed.maxModeName, PARALLEL_INTERFACE_MODE_NAME_MAX_LENGTH, "PIO-0");
            break;
        }
    }
    //else PIO-0 for really old backwards compatibility
    //If SN is invalid, or all ASCII zeroes, then this is likely an ESDI drive managed by an ATA compatible controller.
    //      In this case, we may not want to change the speed to say "PIO-0" since it has a different transfer rate.

    //prefer words 62/63 (DW/MW DMA) if they ae supported
    if (is_ATA_Identify_Word_Valid(wordPtr[52]))
    {
        //retired by ATA/ATAPI 4
        uint8_t dmaCycleTime = M_Byte1(wordPtr[52]);
        if (driveInfo->interfaceSpeedInfo.speedType != INTERFACE_SPEED_PARALLEL)
        {
            memset(&driveInfo->interfaceSpeedInfo, 0, sizeof(interfaceSpeed));//clear anything we've set so far
            driveInfo->interfaceSpeedInfo.speedType = INTERFACE_SPEED_PARALLEL;
            driveInfo->interfaceSpeedInfo.speedIsValid = true;
        }
        //NOTE: SWDMA may NOT be faster than PIO and it is unlikely to be used.
        switch (dmaCycleTime)
        {
        case 2://SWDMA-2
            if (driveInfo->interfaceSpeedInfo.parallelSpeed.maxSpeed < 8.3)
            {
                driveInfo->interfaceSpeedInfo.parallelSpeed.maxSpeed = 8.3;
                driveInfo->interfaceSpeedInfo.parallelSpeed.maxModeNameValid = true;
                snprintf(driveInfo->interfaceSpeedInfo.parallelSpeed.maxModeName, PARALLEL_INTERFACE_MODE_NAME_MAX_LENGTH, "SWDMA-2");
            }
            break;
        case 1://SWDMA-1
            if (driveInfo->interfaceSpeedInfo.parallelSpeed.maxSpeed < 4.2)
            {
                driveInfo->interfaceSpeedInfo.parallelSpeed.maxSpeed = 4.2;
                driveInfo->interfaceSpeedInfo.parallelSpeed.maxModeNameValid = true;
                snprintf(driveInfo->interfaceSpeedInfo.parallelSpeed.maxModeName, PARALLEL_INTERFACE_MODE_NAME_MAX_LENGTH, "SWDMA-1");
            }
            break;
        case 0://SWDMA-0
        default:
            if (driveInfo->interfaceSpeedInfo.parallelSpeed.maxSpeed < 2.1)
            {
                //all others are reserved, treat as SWDMA-0 in this case
                driveInfo->interfaceSpeedInfo.parallelSpeed.maxSpeed = 2.1;
                driveInfo->interfaceSpeedInfo.parallelSpeed.maxModeNameValid = true;
                snprintf(driveInfo->interfaceSpeedInfo.parallelSpeed.maxModeName, PARALLEL_INTERFACE_MODE_NAME_MAX_LENGTH, "SWDMA-0");
            }
            break;
        }
    }
    else if (dmaSupported)
    {
        if (driveInfo->interfaceSpeedInfo.speedType != INTERFACE_SPEED_PARALLEL)
        {
            memset(&driveInfo->interfaceSpeedInfo, 0, sizeof(interfaceSpeed));//clear anything we've set so far
            driveInfo->interfaceSpeedInfo.speedType = INTERFACE_SPEED_PARALLEL;
            driveInfo->interfaceSpeedInfo.speedIsValid = true;
        }
        if (driveInfo->interfaceSpeedInfo.parallelSpeed.maxSpeed < 2.1)
        {
            //SWDMA-0, but only if the read/write DMA commands are supported
            //this will be changed later for any drives supporting the other MWDMA/UDMA fields
            driveInfo->interfaceSpeedInfo.parallelSpeed.maxSpeed = 2.1;
            driveInfo->interfaceSpeedInfo.parallelSpeed.maxModeNameValid = true;
            snprintf(driveInfo->interfaceSpeedInfo.parallelSpeed.maxModeName, PARALLEL_INTERFACE_MODE_NAME_MAX_LENGTH, "SWDMA-0");
        }
    }

    bool words64to70Valid = false;
    bool word88Valid = false;
    if (is_ATA_Identify_Word_Valid(wordPtr[53]))
    {
        if (wordPtr[53] & BIT2)
        {
            word88Valid = true;
        }
        if (wordPtr[53] & BIT1)
        {
            words64to70Valid = true;
        }
        if ((wordPtr[53] & BIT0)
            || (is_ATA_Identify_Word_Valid(wordPtr[54])
                && is_ATA_Identify_Word_Valid(wordPtr[55])
                && is_ATA_Identify_Word_Valid(wordPtr[56])
                && is_ATA_Identify_Word_Valid(wordPtr[57])
                && is_ATA_Identify_Word_Valid(wordPtr[58])))
        {
            driveInfo->ataLegacyCHSInfo.currentInfoconfigurationValid = true;
            driveInfo->ataLegacyCHSInfo.numberOfCurrentLogicalCylinders = wordPtr[54];
            driveInfo->ataLegacyCHSInfo.numberOfCurrentLogicalHeads = M_Byte0(wordPtr[55]);
            driveInfo->ataLegacyCHSInfo.numberOfCurrentLogicalSectorsPerTrack = M_Byte0(wordPtr[56]);
            //words 57 & 58
            driveInfo->ataLegacyCHSInfo.currentCapacityInSectors = M_WordsTo4ByteValue(wordPtr[57], wordPtr[58]);
        }
    }

    if (is_ATA_Identify_Word_Valid(wordPtr[59]))
    {
        if (wordPtr[59] & BIT12)
        {
            add_Feature_To_Supported_List(driveInfo->featuresSupported, &driveInfo->numberOfFeaturesSupported, "Sanitize");
        }
    }

    //28bit max LBA...start with this and adjust to larger size later as needed
    if (lbaModeSupported || (is_ATA_Identify_Word_Valid(wordPtr[60]) || is_ATA_Identify_Word_Valid(wordPtr[61])))
    {
        lbaModeSupported = true;//workaround for some devices that may not have set the earlier LBA mode bit
        driveInfo->maxLBA = M_WordsTo4ByteValue(wordPtr[60], wordPtr[61]);
    }

    //interface speed: NOTE: for old drives, word 51 indicates highest supported  PIO mode 0-2 supported
    //                       word 52 indicates highest supported single word DMA mode 0, 1, 2 supported
    //                 See ATA-2
    if (is_ATA_Identify_Word_Valid(wordPtr[62]))
    {
        //SWDMA (obsolete since MW is so much faster...) (word 52 also holds the max supported value, but is also long obsolete...it can be checked if word 62 is not supported)
        uint8_t swdmaSupported = M_GETBITRANGE(wordPtr[62], 2, 0);
        uint8_t swdmaSelected = M_GETBITRANGE(wordPtr[62], 10, 8);
        if (driveInfo->interfaceSpeedInfo.speedType != INTERFACE_SPEED_PARALLEL)
        {
            memset(&driveInfo->interfaceSpeedInfo, 0, sizeof(interfaceSpeed));//clear anything we've set so far
            driveInfo->interfaceSpeedInfo.speedType = INTERFACE_SPEED_PARALLEL;
            driveInfo->interfaceSpeedInfo.speedIsValid = true;
        }
        if (swdmaSupported > 0 && swdmaSupported < UINT8_MAX)
        {
            int8_t counter = INT8_C(-1);
            while (swdmaSupported > 0)
            {
                swdmaSupported = swdmaSupported >> 1;
                ++counter;
            }
            switch (counter)
            {
            case 2:
                if (driveInfo->interfaceSpeedInfo.parallelSpeed.maxSpeed < 8.3)
                {
                    driveInfo->interfaceSpeedInfo.parallelSpeed.maxSpeed = 8.3;
                    driveInfo->interfaceSpeedInfo.parallelSpeed.maxModeNameValid = true;
                    snprintf(driveInfo->interfaceSpeedInfo.parallelSpeed.maxModeName, PARALLEL_INTERFACE_MODE_NAME_MAX_LENGTH, "SWDMA-2");
                }
                break;
            case 1:
                if (driveInfo->interfaceSpeedInfo.parallelSpeed.maxSpeed < 4.2)
                {
                    driveInfo->interfaceSpeedInfo.parallelSpeed.maxSpeed = 4.2;
                    driveInfo->interfaceSpeedInfo.parallelSpeed.maxModeNameValid = true;
                    snprintf(driveInfo->interfaceSpeedInfo.parallelSpeed.maxModeName, PARALLEL_INTERFACE_MODE_NAME_MAX_LENGTH, "SWDMA-1");
                }
                break;
            case 0:
                if (driveInfo->interfaceSpeedInfo.parallelSpeed.maxSpeed < 2.1)
                {
                    driveInfo->interfaceSpeedInfo.parallelSpeed.maxSpeed = 2.1;
                    driveInfo->interfaceSpeedInfo.parallelSpeed.maxModeNameValid = true;
                    snprintf(driveInfo->interfaceSpeedInfo.parallelSpeed.maxModeName, PARALLEL_INTERFACE_MODE_NAME_MAX_LENGTH, "SWDMA-0");
                }
                break;
            }
            //now check selected
            if (swdmaSelected > 0)
            {
                driveInfo->interfaceSpeedInfo.parallelSpeed.negotiatedValid = true;
                counter = -1;
                while (swdmaSelected > 0)
                {
                    swdmaSelected = swdmaSelected >> 1;
                    ++counter;
                }
                switch (counter)
                {
                case 2:
                    if (!driveInfo->interfaceSpeedInfo.parallelSpeed.negotiatedValid || driveInfo->interfaceSpeedInfo.parallelSpeed.negotiatedSpeed < 8.3)
                    {
                        driveInfo->interfaceSpeedInfo.parallelSpeed.negotiatedSpeed = 8.3;
                        driveInfo->interfaceSpeedInfo.parallelSpeed.negModeNameValid = true;
                        snprintf(driveInfo->interfaceSpeedInfo.parallelSpeed.negModeName, PARALLEL_INTERFACE_MODE_NAME_MAX_LENGTH, "SWDMA-2");
                    }
                    break;
                case 1:
                    if (!driveInfo->interfaceSpeedInfo.parallelSpeed.negotiatedValid || driveInfo->interfaceSpeedInfo.parallelSpeed.negotiatedSpeed < 4.2)
                    {
                        driveInfo->interfaceSpeedInfo.parallelSpeed.negotiatedSpeed = 4.2;
                        driveInfo->interfaceSpeedInfo.parallelSpeed.negModeNameValid = true;
                        snprintf(driveInfo->interfaceSpeedInfo.parallelSpeed.negModeName, PARALLEL_INTERFACE_MODE_NAME_MAX_LENGTH, "SWDMA-1");
                    }
                    break;
                case 0:
                    if (!driveInfo->interfaceSpeedInfo.parallelSpeed.negotiatedValid || driveInfo->interfaceSpeedInfo.parallelSpeed.negotiatedSpeed < 2.1)
                    {
                        driveInfo->interfaceSpeedInfo.parallelSpeed.negotiatedSpeed = 2.1;
                        driveInfo->interfaceSpeedInfo.parallelSpeed.negModeNameValid = true;
                        snprintf(driveInfo->interfaceSpeedInfo.parallelSpeed.negModeName, PARALLEL_INTERFACE_MODE_NAME_MAX_LENGTH, "SWDMA-0");
                    }
                    break;
                }
            }
        }
    }

    if (is_ATA_Identify_Word_Valid(wordPtr[63]))
    {
        int8_t counter = INT8_C(-1);
        //MWDMA
        uint8_t mwdmaSupported = M_GETBITRANGE(wordPtr[63], 2, 0);
        uint8_t mwdmaSelected = M_GETBITRANGE(wordPtr[63], 10, 8);
        if (driveInfo->interfaceSpeedInfo.speedType != INTERFACE_SPEED_PARALLEL)
        {
            memset(&driveInfo->interfaceSpeedInfo, 0, sizeof(interfaceSpeed));//clear anything we've set so far
            driveInfo->interfaceSpeedInfo.speedType = INTERFACE_SPEED_PARALLEL;
            driveInfo->interfaceSpeedInfo.speedIsValid = true;
        }
        if (mwdmaSupported > 0 && mwdmaSupported < UINT8_MAX)
        {
            while (mwdmaSupported > 0)
            {
                mwdmaSupported = mwdmaSupported >> 1;
                ++counter;
            }
            switch (counter)
            {
            case 2:
                if (driveInfo->interfaceSpeedInfo.parallelSpeed.maxSpeed < 16.7)
                {
                    driveInfo->interfaceSpeedInfo.parallelSpeed.maxSpeed = 16.7;
                    driveInfo->interfaceSpeedInfo.parallelSpeed.maxModeNameValid = true;
                    snprintf(driveInfo->interfaceSpeedInfo.parallelSpeed.maxModeName, PARALLEL_INTERFACE_MODE_NAME_MAX_LENGTH, "MWDMA-2");
                }
                break;
            case 1:
                if (driveInfo->interfaceSpeedInfo.parallelSpeed.maxSpeed < 13.3)
                {
                    driveInfo->interfaceSpeedInfo.parallelSpeed.maxSpeed = 13.3;
                    driveInfo->interfaceSpeedInfo.parallelSpeed.maxModeNameValid = true;
                    snprintf(driveInfo->interfaceSpeedInfo.parallelSpeed.maxModeName, PARALLEL_INTERFACE_MODE_NAME_MAX_LENGTH, "MWDMA-1");
                }
                break;
            case 0:
                if (driveInfo->interfaceSpeedInfo.parallelSpeed.maxSpeed < 4.2)
                {
                    driveInfo->interfaceSpeedInfo.parallelSpeed.maxSpeed = 4.2;
                    driveInfo->interfaceSpeedInfo.parallelSpeed.maxModeNameValid = true;
                    snprintf(driveInfo->interfaceSpeedInfo.parallelSpeed.maxModeName, PARALLEL_INTERFACE_MODE_NAME_MAX_LENGTH, "MWDMA-0");
                }
                break;
            }
            //now check selected
            if (mwdmaSelected > 0)
            {
                driveInfo->interfaceSpeedInfo.parallelSpeed.negotiatedValid = true;
                counter = -1;
                while (mwdmaSelected > 0)
                {
                    mwdmaSelected = mwdmaSelected >> 1;
                    ++counter;
                }
                switch (counter)
                {
                case 2:
                    if (!driveInfo->interfaceSpeedInfo.parallelSpeed.negotiatedValid || driveInfo->interfaceSpeedInfo.parallelSpeed.negotiatedSpeed < 16.7)
                    {
                        driveInfo->interfaceSpeedInfo.parallelSpeed.negotiatedSpeed = 16.7;
                        driveInfo->interfaceSpeedInfo.parallelSpeed.negModeNameValid = true;
                        snprintf(driveInfo->interfaceSpeedInfo.parallelSpeed.negModeName, PARALLEL_INTERFACE_MODE_NAME_MAX_LENGTH, "MWDMA-2");
                    }
                    break;
                case 1:
                    if (!driveInfo->interfaceSpeedInfo.parallelSpeed.negotiatedValid || driveInfo->interfaceSpeedInfo.parallelSpeed.negotiatedSpeed < 13.3)
                    {
                        driveInfo->interfaceSpeedInfo.parallelSpeed.negotiatedSpeed = 13.3;
                        driveInfo->interfaceSpeedInfo.parallelSpeed.negModeNameValid = true;
                        snprintf(driveInfo->interfaceSpeedInfo.parallelSpeed.negModeName, PARALLEL_INTERFACE_MODE_NAME_MAX_LENGTH, "MWDMA-1");
                    }
                    break;
                case 0:
                    if (!driveInfo->interfaceSpeedInfo.parallelSpeed.negotiatedValid || driveInfo->interfaceSpeedInfo.parallelSpeed.negotiatedSpeed < 4.2)
                    {
                        driveInfo->interfaceSpeedInfo.parallelSpeed.negotiatedSpeed = 4.2;
                        driveInfo->interfaceSpeedInfo.parallelSpeed.negModeNameValid = true;
                        snprintf(driveInfo->interfaceSpeedInfo.parallelSpeed.negModeName, PARALLEL_INTERFACE_MODE_NAME_MAX_LENGTH, "MWDMA-0");
                    }
                    break;
                }
            }
        }
    }

    bool extendedLBAFieldValid = false;
    bool deterministicTrim = false;
    bool zeroesAfterTrim = false;
    if (words64to70Valid)
    {
        if (driveInfo->interfaceSpeedInfo.speedType != INTERFACE_SPEED_PARALLEL)
        {
            memset(&driveInfo->interfaceSpeedInfo, 0, sizeof(interfaceSpeed));//clear anything we've set so far
            driveInfo->interfaceSpeedInfo.speedType = INTERFACE_SPEED_PARALLEL;
            driveInfo->interfaceSpeedInfo.speedIsValid = true;
        }
        if (is_ATA_Identify_Word_Valid(wordPtr[64]))
        {
            //PIO - from cycle time & mode3/4 support bits
            if (wordPtr[64] & BIT1)
            {
                if (driveInfo->interfaceSpeedInfo.parallelSpeed.maxSpeed < 16.7)
                {
                    driveInfo->interfaceSpeedInfo.parallelSpeed.maxSpeed = 16.7;
                    driveInfo->interfaceSpeedInfo.parallelSpeed.maxModeNameValid = true;
                    snprintf(driveInfo->interfaceSpeedInfo.parallelSpeed.maxModeName, PARALLEL_INTERFACE_MODE_NAME_MAX_LENGTH, "PIO-4");
                }
            }
            else if (wordPtr[64] & BIT0)
            {
                if (driveInfo->interfaceSpeedInfo.parallelSpeed.maxSpeed < 11.1)
                {
                    driveInfo->interfaceSpeedInfo.parallelSpeed.maxSpeed = 11.1;
                    driveInfo->interfaceSpeedInfo.parallelSpeed.maxModeNameValid = true;
                    snprintf(driveInfo->interfaceSpeedInfo.parallelSpeed.maxModeName, PARALLEL_INTERFACE_MODE_NAME_MAX_LENGTH, "PIO-3");
                }
            }
        }
        //65 = mwdma transfer cycle time per word
        //66 = manufacturers recommended mwdma cycle time
        //67 = min PIO cycle time without flow control (pio-2 to PIO-2)
        //68 = min PIO cycle time with IORDY flow control (pio-3 & pio-4)
        if (is_ATA_Identify_Word_Valid(wordPtr[68]))
        {
            //determine maximum from cycle times?
            uint16_t pioCycleTime = wordPtr[68];
            switch (pioCycleTime)
            {
            case 120://PIO4
                if (driveInfo->interfaceSpeedInfo.parallelSpeed.maxSpeed < 16.7)
                {
                    driveInfo->interfaceSpeedInfo.parallelSpeed.maxSpeed = 16.7;
                    driveInfo->interfaceSpeedInfo.parallelSpeed.maxModeNameValid = true;
                    snprintf(driveInfo->interfaceSpeedInfo.parallelSpeed.maxModeName, PARALLEL_INTERFACE_MODE_NAME_MAX_LENGTH, "PIO-4");
                }
                break;
            case 180://PIO3
                if (driveInfo->interfaceSpeedInfo.parallelSpeed.maxSpeed < 11.1)
                {
                    driveInfo->interfaceSpeedInfo.parallelSpeed.maxSpeed = 11.1;
                    driveInfo->interfaceSpeedInfo.parallelSpeed.maxModeNameValid = true;
                    snprintf(driveInfo->interfaceSpeedInfo.parallelSpeed.maxModeName, PARALLEL_INTERFACE_MODE_NAME_MAX_LENGTH, "PIO-3");
                }
                break;
            case 240://PIO2
                if (driveInfo->interfaceSpeedInfo.parallelSpeed.maxSpeed < 8.3)
                {
                    driveInfo->interfaceSpeedInfo.parallelSpeed.maxSpeed = 8.3;
                    driveInfo->interfaceSpeedInfo.parallelSpeed.maxModeNameValid = true;
                    snprintf(driveInfo->interfaceSpeedInfo.parallelSpeed.maxModeName, PARALLEL_INTERFACE_MODE_NAME_MAX_LENGTH, "PIO-2");
                }
                break;
            case 383://PIO1
                if (driveInfo->interfaceSpeedInfo.parallelSpeed.maxSpeed < 5.2)
                {
                    driveInfo->interfaceSpeedInfo.parallelSpeed.maxSpeed = 5.2;
                    driveInfo->interfaceSpeedInfo.parallelSpeed.maxModeNameValid = true;
                    snprintf(driveInfo->interfaceSpeedInfo.parallelSpeed.maxModeName, PARALLEL_INTERFACE_MODE_NAME_MAX_LENGTH, "PIO-1");
                }
                break;
            case 600://PIO0
                if (driveInfo->interfaceSpeedInfo.parallelSpeed.maxSpeed < 3.3)
                {
                    driveInfo->interfaceSpeedInfo.parallelSpeed.maxSpeed = 3.3;
                    driveInfo->interfaceSpeedInfo.parallelSpeed.maxModeNameValid = true;
                    snprintf(driveInfo->interfaceSpeedInfo.parallelSpeed.maxModeName, PARALLEL_INTERFACE_MODE_NAME_MAX_LENGTH, "PIO-0");
                }
                break;
            }
        }
        if (is_ATA_Identify_Word_Valid(wordPtr[69]))
        {
            if (wordPtr[69] & BIT15)
            {
                //CFast supported
                add_Specification_To_Supported_List(driveInfo->specificationsSupported, &driveInfo->numberOfSpecificationsSupported, "CFast");
            }
            if (wordPtr[69] & BIT14)
            {
                deterministicTrim = true;
            }
            if (wordPtr[69] & BIT8)
            {
                driveInfo->fwdlSupport.dmaModeSupported = true;
            }
            if (wordPtr[69] & BIT7)
            {
                ataCapabilities->ieee1667Supported = true;
            }
            if (wordPtr[69] & BIT6)
            {
                zeroesAfterTrim = true;
            }
            //get if it's FDE/TCG
            if (wordPtr[69] & BIT4)
            {
                //FDE
                driveInfo->encryptionSupport = ENCRYPTION_FULL_DISK;
                driveInfo->ataSecurityInformation.encryptAll = true;
            }
            if (wordPtr[69] & BIT3)
            {
                extendedLBAFieldValid = true;
            }
            if (wordPtr[69] & BIT2)
            {
                add_Feature_To_Supported_List(driveInfo->featuresSupported, &driveInfo->numberOfFeaturesSupported, "All Write Cache Non-Volatile");
            }
            //zoned capabilities (ACS4)
            driveInfo->zonedDevice = C_CAST(uint8_t, wordPtr[69] & (BIT0 | BIT1));
        }
    }

    uint8_t queueDepth = 1;//minimum queue depth for any device is 1
    if (is_ATA_Identify_Word_Valid(wordPtr[75]))
    {
        queueDepth = M_GETBITRANGE(wordPtr[75], 4, 0) + 1;
    }

    //SATA Capabilities (Words 76 & 77)
    if (is_ATA_Identify_Word_Valid_SATA(wordPtr[76]))
    {
        memset(&driveInfo->interfaceSpeedInfo, 0, sizeof(interfaceSpeed));//clear anything we've set so far
        driveInfo->interfaceSpeedInfo.speedType = INTERFACE_SPEED_SERIAL;
        driveInfo->interfaceSpeedInfo.speedIsValid = true;
        //port speed
        driveInfo->interfaceSpeedInfo.serialSpeed.numberOfPorts = 1;
        driveInfo->interfaceSpeedInfo.serialSpeed.activePortNumber = 0;
        if (wordPtr[77] & BIT12)
        {
            add_Feature_To_Supported_List(driveInfo->featuresSupported, &driveInfo->numberOfFeaturesSupported, "SATA NCQ Priority");
        }
        if (wordPtr[76] & BIT8)
        {
            DECLARE_ZERO_INIT_ARRAY(char, ncqFeatureString, MAX_FEATURE_LENGTH);
            snprintf(ncqFeatureString, MAX_FEATURE_LENGTH, "SATA NCQ [QD=%" PRIu8 "]", queueDepth);
            add_Feature_To_Supported_List(driveInfo->featuresSupported, &driveInfo->numberOfFeaturesSupported, ncqFeatureString);
        }
        //Word 76 holds bits for supporteed signalling speeds (SATA)
        if (wordPtr[76] & BIT3)
        {
            driveInfo->interfaceSpeedInfo.serialSpeed.portSpeedsMax[0] = 3;
        }
        else if (wordPtr[76] & BIT2)
        {
            driveInfo->interfaceSpeedInfo.serialSpeed.portSpeedsMax[0] = 2;
        }
        else if (wordPtr[76] & BIT1)
        {
            driveInfo->interfaceSpeedInfo.serialSpeed.portSpeedsMax[0] = 1;
        }
        else
        {
            driveInfo->interfaceSpeedInfo.serialSpeed.portSpeedsMax[0] = 0;
        }

    }

    if (is_ATA_Identify_Word_Valid_SATA(wordPtr[77]))
    {
        if (wordPtr[77] & BIT9)
        {
            add_Feature_To_Supported_List(driveInfo->featuresSupported, &driveInfo->numberOfFeaturesSupported, "SATA Out Of Band Management");
        }
        if (wordPtr[77] & BIT4)
        {
            add_Feature_To_Supported_List(driveInfo->featuresSupported, &driveInfo->numberOfFeaturesSupported, "SATA NCQ Streaming");
        }
        //Word 77 has a coded value for the negotiated speed.
        switch (M_Nibble0(wordPtr[77]) >> 1)
        {
        case 3://6.0Gb/s
            driveInfo->interfaceSpeedInfo.serialSpeed.portSpeedsNegotiated[0] = 3;
            break;
        case 2://3.0Gb/s
            driveInfo->interfaceSpeedInfo.serialSpeed.portSpeedsNegotiated[0] = 2;
            break;
        case 1://1.5Gb/s
            driveInfo->interfaceSpeedInfo.serialSpeed.portSpeedsNegotiated[0] = 1;
            break;
        case 0:
        default:
            driveInfo->interfaceSpeedInfo.serialSpeed.portSpeedsNegotiated[0] = 0;
            break;
        }
    }

    //SATA Features supported and enabled (Words 78 & 79)
    if (is_ATA_Identify_Word_Valid_SATA(wordPtr[78]) && is_ATA_Identify_Word_Valid_SATA(wordPtr[79]))
    {
        if (wordPtr[78] & BIT12)
        {
            if (wordPtr[79] & BIT10)
            {
                add_Feature_To_Supported_List(driveInfo->featuresSupported, &driveInfo->numberOfFeaturesSupported, "SATA Power Disable [Enabled]");
            }
            else
            {
                add_Feature_To_Supported_List(driveInfo->featuresSupported, &driveInfo->numberOfFeaturesSupported, "SATA Power Disable");
            }
        }
        if (wordPtr[78] & BIT11)
        {
            if (wordPtr[79] & BIT11)
            {
                add_Feature_To_Supported_List(driveInfo->featuresSupported, &driveInfo->numberOfFeaturesSupported, "SATA Rebuild Assist [Enabled]");
            }
            else
            {
                add_Feature_To_Supported_List(driveInfo->featuresSupported, &driveInfo->numberOfFeaturesSupported, "SATA Rebuild Assist");
            }
        }
        if (wordPtr[78] & BIT9)
        {
            if (wordPtr[79] & BIT9)
            {
                add_Feature_To_Supported_List(driveInfo->featuresSupported, &driveInfo->numberOfFeaturesSupported, "SATA Hybrid Information [Enabled]");
            }
            else
            {
                add_Feature_To_Supported_List(driveInfo->featuresSupported, &driveInfo->numberOfFeaturesSupported, "SATA Hybrid Information");
            }
        }
        if (wordPtr[78] & BIT8)
        {
            if (wordPtr[79] & BIT8)
            {
                add_Feature_To_Supported_List(driveInfo->featuresSupported, &driveInfo->numberOfFeaturesSupported, "SATA Device Sleep [Enabled]");
            }
            else
            {
                add_Feature_To_Supported_List(driveInfo->featuresSupported, &driveInfo->numberOfFeaturesSupported, "SATA Device Sleep");
            }
        }
        if (wordPtr[78] & BIT8)
        {
            add_Feature_To_Supported_List(driveInfo->featuresSupported, &driveInfo->numberOfFeaturesSupported, "SATA NCQ Autosense");
        }
        if (wordPtr[78] & BIT6)
        {
            if (wordPtr[79] & BIT6)
            {
                add_Feature_To_Supported_List(driveInfo->featuresSupported, &driveInfo->numberOfFeaturesSupported, "SATA Software Settings Preservation [Enabled]");
            }
            else
            {
                add_Feature_To_Supported_List(driveInfo->featuresSupported, &driveInfo->numberOfFeaturesSupported, "SATA Software Settings Preservation");
            }
        }
        if (wordPtr[78] & BIT5)
        {
            if (wordPtr[79] & BIT5)
            {
                add_Feature_To_Supported_List(driveInfo->featuresSupported, &driveInfo->numberOfFeaturesSupported, "SATA Hardware Feature Control [Enabled]");
            }
            else
            {
                add_Feature_To_Supported_List(driveInfo->featuresSupported, &driveInfo->numberOfFeaturesSupported, "SATA Hardware Feature Control");
            }
        }
        if (wordPtr[78] & BIT4)
        {
            if (wordPtr[79] & BIT4)
            {
                add_Feature_To_Supported_List(driveInfo->featuresSupported, &driveInfo->numberOfFeaturesSupported, "SATA In-Order Data Delivery [Enabled]");
            }
            else
            {
                add_Feature_To_Supported_List(driveInfo->featuresSupported, &driveInfo->numberOfFeaturesSupported, "SATA In-Order Data Delivery");
            }
        }
        if (wordPtr[78] & BIT3)
        {
            if (wordPtr[79] & BIT3)
            {
                add_Feature_To_Supported_List(driveInfo->featuresSupported, &driveInfo->numberOfFeaturesSupported, "SATA Device Initiated Power Management [Enabled]");
            }
            else
            {
                add_Feature_To_Supported_List(driveInfo->featuresSupported, &driveInfo->numberOfFeaturesSupported, "SATA Device Initiated Power Management");
            }
        }
    }

    //get which specifications are supported and the number of them added to the list (ATA Spec listed in word 80)
    uint16_t specsBits = wordPtr[80];
    if (is_ATA_Identify_Word_Valid(wordPtr[80]))
    {
        //Guessed name as this doesn't exist yet
        if (specsBits & BIT15)
        {
            add_Specification_To_Supported_List(driveInfo->specificationsSupported, &driveInfo->numberOfSpecificationsSupported, "ACS-8");
        }
        if (specsBits & BIT14)
        {
            add_Specification_To_Supported_List(driveInfo->specificationsSupported, &driveInfo->numberOfSpecificationsSupported, "ACS-7");
        }
        if (specsBits & BIT13)
        {
            add_Specification_To_Supported_List(driveInfo->specificationsSupported, &driveInfo->numberOfSpecificationsSupported, "ACS-6");
        }
        if (specsBits & BIT12)
        {
            add_Specification_To_Supported_List(driveInfo->specificationsSupported, &driveInfo->numberOfSpecificationsSupported, "ACS-5");
        }
        if (specsBits & BIT11)
        {
            add_Specification_To_Supported_List(driveInfo->specificationsSupported, &driveInfo->numberOfSpecificationsSupported, "ACS-4");
        }
        if (specsBits & BIT10)
        {
            add_Specification_To_Supported_List(driveInfo->specificationsSupported, &driveInfo->numberOfSpecificationsSupported, "ACS-3");
        }
        if (specsBits & BIT9)
        {
            add_Specification_To_Supported_List(driveInfo->specificationsSupported, &driveInfo->numberOfSpecificationsSupported, "ACS-2");
        }
        if (specsBits & BIT8)
        {
            add_Specification_To_Supported_List(driveInfo->specificationsSupported, &driveInfo->numberOfSpecificationsSupported, "ATA8-ACS");
        }
        if (specsBits & BIT7)
        {
            add_Specification_To_Supported_List(driveInfo->specificationsSupported, &driveInfo->numberOfSpecificationsSupported, "ATA/ATAPI-7");
        }
        if (specsBits & BIT6)
        {
            add_Specification_To_Supported_List(driveInfo->specificationsSupported, &driveInfo->numberOfSpecificationsSupported, "ATA/ATAPI-6");
        }
        if (specsBits & BIT5)
        {
            add_Specification_To_Supported_List(driveInfo->specificationsSupported, &driveInfo->numberOfSpecificationsSupported, "ATA/ATAPI-5");
        }
        if (specsBits & BIT4)
        {
            add_Specification_To_Supported_List(driveInfo->specificationsSupported, &driveInfo->numberOfSpecificationsSupported, "ATA/ATAPI-4");
        }
        if (specsBits & BIT3)
        {
            add_Specification_To_Supported_List(driveInfo->specificationsSupported, &driveInfo->numberOfSpecificationsSupported, "ATA-3");
        }
        if (specsBits & BIT2)
        {
            add_Specification_To_Supported_List(driveInfo->specificationsSupported, &driveInfo->numberOfSpecificationsSupported, "ATA-2");
        }
        if (specsBits & BIT1)
        {
            add_Specification_To_Supported_List(driveInfo->specificationsSupported, &driveInfo->numberOfSpecificationsSupported, "ATA-1");
        }
    }
    else
    {
        //if this was not reported, assume ATA-1
        //NOTE: May be able to check other fields to determine if a later standard is supported or not, but this is a fair assumption-TJE
        add_Specification_To_Supported_List(driveInfo->specificationsSupported, &driveInfo->numberOfSpecificationsSupported, "ATA-1 or Pre-ATA");
    }
    //Get the ATA Minor version to add to the list too.
    if (is_ATA_Identify_Word_Valid(wordPtr[81]))
    {
        switch (wordPtr[81])
        {
        case ATA_MINOR_VERSION_NOT_REPORTED:
            break;
        case ATA_MINOR_VERSION_ATA_1_PRIOR_TO_REV_4:
            add_Specification_To_Supported_List(driveInfo->specificationsSupported, &driveInfo->numberOfSpecificationsSupported, "ATA-1 (pre Revision 4)");
            break;
        case ATA_MINOR_VERSION_ATA_1_PUBLISHED:
            add_Specification_To_Supported_List(driveInfo->specificationsSupported, &driveInfo->numberOfSpecificationsSupported, "ATA-1 (Published)");
            break;
        case ATA_MINOR_VERSION_ATA_1_REV_4:
            add_Specification_To_Supported_List(driveInfo->specificationsSupported, &driveInfo->numberOfSpecificationsSupported, "ATA-1 (Revision 4)");
            break;
        case ATA_MINOR_VERSION_ATA_2_PUBLISHED:
            add_Specification_To_Supported_List(driveInfo->specificationsSupported, &driveInfo->numberOfSpecificationsSupported, "ATA-2 (Published)");
            break;
        case ATA_MINOR_VERSION_ATA_2_PRIOR_TO_REV_2K:
            add_Specification_To_Supported_List(driveInfo->specificationsSupported, &driveInfo->numberOfSpecificationsSupported, "ATA-2 (Pre Revision 2K)");
            break;
        case ATA_MINOR_VERSION_ATA_3_REV_1:
            add_Specification_To_Supported_List(driveInfo->specificationsSupported, &driveInfo->numberOfSpecificationsSupported, "ATA-3 (Revision 1)");
            break;
        case ATA_MINOR_VERSION_ATA_2_REV_2K:
            add_Specification_To_Supported_List(driveInfo->specificationsSupported, &driveInfo->numberOfSpecificationsSupported, "ATA-2 (Revision 2K)");
            break;
        case ATA_MINOR_VERSION_ATA_3_REV_0:
            add_Specification_To_Supported_List(driveInfo->specificationsSupported, &driveInfo->numberOfSpecificationsSupported, "ATA-3 (Revision 0)");
            break;
        case ATA_MINOR_VERSION_ATA_2_REV_3:
            add_Specification_To_Supported_List(driveInfo->specificationsSupported, &driveInfo->numberOfSpecificationsSupported, "ATA-2 (Revision 3)");
            break;
        case ATA_MINOR_VERSION_ATA_3_PUBLISHED:
            add_Specification_To_Supported_List(driveInfo->specificationsSupported, &driveInfo->numberOfSpecificationsSupported, "ATA-3 (Published)");
            break;
        case ATA_MINOR_VERSION_ATA_3_REV_6:
            add_Specification_To_Supported_List(driveInfo->specificationsSupported, &driveInfo->numberOfSpecificationsSupported, "ATA-3 (Revision 6)");
            break;
        case ATA_MINOR_VERSION_ATA_3_REV_7_AND_7A:
            add_Specification_To_Supported_List(driveInfo->specificationsSupported, &driveInfo->numberOfSpecificationsSupported, "ATA-3 (Revision 7 & 7A)");
            break;
        case ATA_MINOR_VERSION_ATA_ATAPI_4_REV_6:
            add_Specification_To_Supported_List(driveInfo->specificationsSupported, &driveInfo->numberOfSpecificationsSupported, "ATA/ATAPI-4 (Revision 6)");
            break;
        case ATA_MINOR_VERSION_ATA_ATAPI_4_REV_13:
            add_Specification_To_Supported_List(driveInfo->specificationsSupported, &driveInfo->numberOfSpecificationsSupported, "ATA/ATAPI-4 (Revision 13)");
            break;
        case ATA_MINOR_VERSION_ATA_ATAPI_4_REV7:
            add_Specification_To_Supported_List(driveInfo->specificationsSupported, &driveInfo->numberOfSpecificationsSupported, "ATA/ATAPI-4 (Revision 7)");
            break;
        case ATA_MINOR_VERSION_ATA_ATAPI_4_REV_18:
            add_Specification_To_Supported_List(driveInfo->specificationsSupported, &driveInfo->numberOfSpecificationsSupported, "ATA/ATAPI-4 (Revision 18)");
            break;
        case ATA_MINOR_VERSION_ATA_ATAPI_4_REV_15:
            add_Specification_To_Supported_List(driveInfo->specificationsSupported, &driveInfo->numberOfSpecificationsSupported, "ATA/ATAPI-4 (Revision 15)");
            break;
        case ATA_MINOR_VERSION_ATA_ATAPI_4_PUBLISHED:
            add_Specification_To_Supported_List(driveInfo->specificationsSupported, &driveInfo->numberOfSpecificationsSupported, "ATA/ATAPI-4 (Published)");
            break;
        case ATA_MINOR_VERSION_ATA_ATAPI_5_REV_3:
            add_Specification_To_Supported_List(driveInfo->specificationsSupported, &driveInfo->numberOfSpecificationsSupported, "ATA/ATAPI-5 (Revision 3)");
            break;
        case ATA_MINOR_VERSION_ATA_ATAPI_4_REV_14:
            add_Specification_To_Supported_List(driveInfo->specificationsSupported, &driveInfo->numberOfSpecificationsSupported, "ATA/ATAPI-4 (Revision 14)");
            break;
        case ATA_MINOR_VERSION_ATA_ATAPI_5_REV_1:
            add_Specification_To_Supported_List(driveInfo->specificationsSupported, &driveInfo->numberOfSpecificationsSupported, "ATA/ATAPI-5 (Revision 1)");
            break;
        case ATA_MINOR_VERSION_ATA_ATAPI_5_PUBLISHED:
            add_Specification_To_Supported_List(driveInfo->specificationsSupported, &driveInfo->numberOfSpecificationsSupported, "ATA/ATAPI-5 (Published)");
            break;
        case ATA_MINOR_VERSION_ATA_ATAPI_4_REV_17:
            add_Specification_To_Supported_List(driveInfo->specificationsSupported, &driveInfo->numberOfSpecificationsSupported, "ATA/ATAPI-4 (Revision 17)");
            break;
        case ATA_MINOR_VERSION_ATA_ATAPI_6_REV_0:
            add_Specification_To_Supported_List(driveInfo->specificationsSupported, &driveInfo->numberOfSpecificationsSupported, "ATA/ATAPI-6 (Revision 0)");
            break;
        case ATA_MINOR_VERSION_ATA_ATAPI_6_REV_3A:
            add_Specification_To_Supported_List(driveInfo->specificationsSupported, &driveInfo->numberOfSpecificationsSupported, "ATA/ATAPI-6 (Revision 3A)");
            break;
        case ATA_MINOR_VERSION_ATA_ATAPI_7_REV_1:
            add_Specification_To_Supported_List(driveInfo->specificationsSupported, &driveInfo->numberOfSpecificationsSupported, "ATA/ATAPI-7 (Revision 1)");
            break;
        case ATA_MINOR_VERSION_ATA_ATAPI_6_REV_2:
            add_Specification_To_Supported_List(driveInfo->specificationsSupported, &driveInfo->numberOfSpecificationsSupported, "ATA/ATAPI-6 (Revision 2)");
            break;
        case ATA_MINOR_VERSION_ATA_ATAPI_6_REV_1:
            add_Specification_To_Supported_List(driveInfo->specificationsSupported, &driveInfo->numberOfSpecificationsSupported, "ATA/ATAPI-6 (Revision 1)");
            break;
        case ATA_MINOR_VERSION_ATA_ATAPI_7_RUBLISHED:
            add_Specification_To_Supported_List(driveInfo->specificationsSupported, &driveInfo->numberOfSpecificationsSupported, "ATA/ATAPI-7 (Published)");
            break;
        case ATA_MINOR_VERSION_ATA_ATAPI_7_REV_0:
            add_Specification_To_Supported_List(driveInfo->specificationsSupported, &driveInfo->numberOfSpecificationsSupported, "ATA/ATAPI-7 (Revision 0)");
            break;
        case ATA_MINOR_VERSION_ACS3_REV_3B:
            add_Specification_To_Supported_List(driveInfo->specificationsSupported, &driveInfo->numberOfSpecificationsSupported, "ACS-3 (Revision 3B)");
            break;
        case ATA_MINOR_VERSION_ATA_ATAPI_7_REV_4A:
            add_Specification_To_Supported_List(driveInfo->specificationsSupported, &driveInfo->numberOfSpecificationsSupported, "ATA/ATAPI-7 (Revision 4A)");
            break;
        case ATA_MINOR_VERSION_ATA_ATAPI_6_PUBLISHED:
            add_Specification_To_Supported_List(driveInfo->specificationsSupported, &driveInfo->numberOfSpecificationsSupported, "ATA/ATAPI-6 (Published)");
            break;
        case ATA_MINOR_VERSION_ATA8_ACS_REV_3C:
            add_Specification_To_Supported_List(driveInfo->specificationsSupported, &driveInfo->numberOfSpecificationsSupported, "ATA8-ACS (Revision 3C)");
            break;
        case ATA_MINOR_VERSION_ATA8_ACS_REV_6:
            add_Specification_To_Supported_List(driveInfo->specificationsSupported, &driveInfo->numberOfSpecificationsSupported, "ATA8-ACS (Revision 6)");
            break;
        case ATA_MINOR_VERSION_ATA8_ACS_REV_4:
            add_Specification_To_Supported_List(driveInfo->specificationsSupported, &driveInfo->numberOfSpecificationsSupported, "ATA8-ACS (Revision 4)");
            break;
        case ATA_MINOR_VERSION_ACS5_REV_8:
            add_Specification_To_Supported_List(driveInfo->specificationsSupported, &driveInfo->numberOfSpecificationsSupported, "ATA8-ACS (Revision 8)");
            break;
        case ATA_MINOR_VERSION_ACS2_REV_2:
            add_Specification_To_Supported_List(driveInfo->specificationsSupported, &driveInfo->numberOfSpecificationsSupported, "ACS-2 (Revision 2)");
            break;
        case ATA_MINOR_VERSION_ATA8_ACS_REV_3E:
            add_Specification_To_Supported_List(driveInfo->specificationsSupported, &driveInfo->numberOfSpecificationsSupported, "ATA8-ACS (Revision 3E)");
            break;
        case ATA_MINOR_VERSION_ATA8_ACS_REV_4C:
            add_Specification_To_Supported_List(driveInfo->specificationsSupported, &driveInfo->numberOfSpecificationsSupported, "ATA8-ACS (Revision 4C)");
            break;
        case ATA_MINOR_VERSION_ATA8_ACS_REV_3F:
            add_Specification_To_Supported_List(driveInfo->specificationsSupported, &driveInfo->numberOfSpecificationsSupported, "ATA8-ACS (Revision 3F)");
            break;
        case ATA_MINOR_VERSION_ATA8_ACS_REV_3B:
            add_Specification_To_Supported_List(driveInfo->specificationsSupported, &driveInfo->numberOfSpecificationsSupported, "ATA8-ACS (Revision 3B)");
            break;
        case ATA_MINOR_VERSION_ACS4_REV_5:
            add_Specification_To_Supported_List(driveInfo->specificationsSupported, &driveInfo->numberOfSpecificationsSupported, "ACS-4 (Revision 5)");
            break;
        case ATA_MINOR_VERSION_ACS3_REV_5:
            add_Specification_To_Supported_List(driveInfo->specificationsSupported, &driveInfo->numberOfSpecificationsSupported, "ACS-3 (Revision 5)");
            break;
        case ATA_MINOR_VERSION_ACS6_REV_2:
            add_Specification_To_Supported_List(driveInfo->specificationsSupported, &driveInfo->numberOfSpecificationsSupported, "ACS-6 (Revision 2)");
            break;
        case ATA_MINOR_VERSION_ACS_2_PUBLISHED:
            add_Specification_To_Supported_List(driveInfo->specificationsSupported, &driveInfo->numberOfSpecificationsSupported, "ACS-2 (Published)");
            break;
        case ATA_MINOR_VERSION_ACS4_PUBLISHED:
            add_Specification_To_Supported_List(driveInfo->specificationsSupported, &driveInfo->numberOfSpecificationsSupported, "ACS-4 (Published)");
            break;
        case ATA_MINOR_VERSION_ATA8_ACS_REV_2D:
            add_Specification_To_Supported_List(driveInfo->specificationsSupported, &driveInfo->numberOfSpecificationsSupported, "ATA8-ACS (Revision 2D)");
            break;
        case ATA_MINOR_VERSION_ACS3_PUBLISHED:
            add_Specification_To_Supported_List(driveInfo->specificationsSupported, &driveInfo->numberOfSpecificationsSupported, "ACS-3 (Published)");
            break;
        case ATA_MINOR_VERSION_ACS2_REV_3:
            add_Specification_To_Supported_List(driveInfo->specificationsSupported, &driveInfo->numberOfSpecificationsSupported, "ACS-2 (Revision 3)");
            break;
        case ATA_MINOR_VERSION_ACS3_REV_4:
            add_Specification_To_Supported_List(driveInfo->specificationsSupported, &driveInfo->numberOfSpecificationsSupported, "ACS-3 (Revision 4)");
            break;
        case ATA_MINOR_VERSION_NOT_REPORTED_2:
            break;
        }
    }

    //words 82-87 contain fields for supported/enabled
    // words 87, 84, 83 need to validate with bits 14&15
    //Some words mirror other bits and other hold support while another holds enabled.
    //Some of these are also paired between supported and enabled.
    //The following code assumes these pairs to parse this data as best it can without making things too complicated-TJE
    if (is_ATA_Identify_Word_Valid(wordPtr[82]) && is_ATA_Identify_Word_Valid(wordPtr[85]))
    {
        if (wordPtr[82] & BIT10 || wordPtr[85] & BIT10)
        {
            add_Feature_To_Supported_List(driveInfo->featuresSupported, &driveInfo->numberOfFeaturesSupported, "HPA");
        }
        //read look ahead
        if (wordPtr[82] & BIT6)
        {
            driveInfo->readLookAheadSupported = true;
            if (wordPtr[85] & BIT6)
            {
                driveInfo->readLookAheadEnabled = true;
            }
        }
        //write cache
        if (wordPtr[82] & BIT5)
        {
            driveInfo->writeCacheSupported = true;
            if (wordPtr[85] & BIT5)
            {
                driveInfo->writeCacheEnabled = true;
            }
        }
        if (wordPtr[82] & BIT4 || wordPtr[85] & BIT4)
        {
            add_Feature_To_Supported_List(driveInfo->featuresSupported, &driveInfo->numberOfFeaturesSupported, "Packet");
        }
        if (wordPtr[82] & BIT3)
        {
            add_Feature_To_Supported_List(driveInfo->featuresSupported, &driveInfo->numberOfFeaturesSupported, "Power Management");
        }
        if (wordPtr[82] & BIT1)
        {
            if (wordPtr[85] & BIT1)
            {
                add_Feature_To_Supported_List(driveInfo->featuresSupported, &driveInfo->numberOfFeaturesSupported, "Security [Enabled]");
            }
            else
            {
                add_Feature_To_Supported_List(driveInfo->featuresSupported, &driveInfo->numberOfFeaturesSupported, "Security");
            }
        }
        if (wordPtr[82] & BIT0)
        {
            if (wordPtr[85] & BIT0)
            {
                add_Feature_To_Supported_List(driveInfo->featuresSupported, &driveInfo->numberOfFeaturesSupported, "SMART [Enabled]");
            }
            else
            {
                add_Feature_To_Supported_List(driveInfo->featuresSupported, &driveInfo->numberOfFeaturesSupported, "SMART");
            }
        }
    }

    bool words119to120Valid = false;
    if (is_ATA_Identify_Word_Valid_With_Bits_14_And_15(wordPtr[83]) && is_ATA_Identify_Word_Valid(wordPtr[86]))
    {
        if (wordPtr[86] & BIT15)
        {
            words119to120Valid = true;
        }
        if (wordPtr[83] & BIT11 || wordPtr[86] & BIT11)
        {
            add_Feature_To_Supported_List(driveInfo->featuresSupported, &driveInfo->numberOfFeaturesSupported, "DCO");
        }
        if (wordPtr[83] & BIT10 || wordPtr[86] & BIT10)
        {
            add_Feature_To_Supported_List(driveInfo->featuresSupported, &driveInfo->numberOfFeaturesSupported, "48bit Address");
        }
        if (wordPtr[83] & BIT9)
        {
            if (wordPtr[86] & BIT9)
            {
                add_Feature_To_Supported_List(driveInfo->featuresSupported, &driveInfo->numberOfFeaturesSupported, "AAM [Enabled]");
            }
            else
            {
                add_Feature_To_Supported_List(driveInfo->featuresSupported, &driveInfo->numberOfFeaturesSupported, "AAM");
            }
        }
        if (wordPtr[83] & BIT8)
        {
            if (wordPtr[86] & BIT8)
            {
                add_Feature_To_Supported_List(driveInfo->featuresSupported, &driveInfo->numberOfFeaturesSupported, "Set Max Security Extension [Enabled]");
            }
            else
            {
                add_Feature_To_Supported_List(driveInfo->featuresSupported, &driveInfo->numberOfFeaturesSupported, "Set Max Security Extension");
            }
        }
        if (wordPtr[83] & BIT5)
        {
            if (wordPtr[86] & BIT5)
            {
                add_Feature_To_Supported_List(driveInfo->featuresSupported, &driveInfo->numberOfFeaturesSupported, "PUIS [Enabled]");
            }
            else
            {
                add_Feature_To_Supported_List(driveInfo->featuresSupported, &driveInfo->numberOfFeaturesSupported, "PUIS");
            }
        }
        if (wordPtr[83] & BIT4)
        {
            if (wordPtr[86] & BIT4)
            {
                add_Feature_To_Supported_List(driveInfo->featuresSupported, &driveInfo->numberOfFeaturesSupported, "Removable Media Status Notification [Enabled]");
            }
            else
            {
                add_Feature_To_Supported_List(driveInfo->featuresSupported, &driveInfo->numberOfFeaturesSupported, "Removable Media Status Notification");
            }
        }
        if (wordPtr[83] & BIT3)
        {
            if (wordPtr[86] & BIT3)
            {
                add_Feature_To_Supported_List(driveInfo->featuresSupported, &driveInfo->numberOfFeaturesSupported, "APM [Enabled]");
            }
            else
            {
                add_Feature_To_Supported_List(driveInfo->featuresSupported, &driveInfo->numberOfFeaturesSupported, "APM");
            }
        }
        if (wordPtr[83] & BIT2)
        {
            if (wordPtr[86] & BIT2)
            {
                add_Feature_To_Supported_List(driveInfo->featuresSupported, &driveInfo->numberOfFeaturesSupported, "CFA [Enabled]");
            }
            else
            {
                add_Feature_To_Supported_List(driveInfo->featuresSupported, &driveInfo->numberOfFeaturesSupported, "CFA");
            }
        }
        if (wordPtr[83] & BIT1 || wordPtr[86] & BIT1)
        {
            DECLARE_ZERO_INIT_ARRAY(char, tcqFeatureString, MAX_FEATURE_LENGTH);
            snprintf(tcqFeatureString, MAX_FEATURE_LENGTH, "TCQ [QD=%" PRIu8 "]", queueDepth);
            add_Feature_To_Supported_List(driveInfo->featuresSupported, &driveInfo->numberOfFeaturesSupported, tcqFeatureString);
        }
        if (wordPtr[83] & BIT0 || wordPtr[86] & BIT0)
        {
            driveInfo->fwdlSupport.downloadSupported = true;
        }
    }

    bool word84Valid = false;
    bool word87Valid = false;
    if (is_ATA_Identify_Word_Valid_With_Bits_14_And_15(wordPtr[84]))
    {
        word84Valid = true;
    }
    if (is_ATA_Identify_Word_Valid_With_Bits_14_And_15(wordPtr[87]))
    {
        word87Valid = true;
    }

    if ((word84Valid && wordPtr[84] & BIT8) || (word87Valid && wordPtr[87] & BIT8))
    {
        driveInfo->worldWideNameSupported = true;
        memcpy(&driveInfo->worldWideName, &wordPtr[108], 8); //copy the 8 bytes into the world wide name
        word_Swap_64(&driveInfo->worldWideName); //byte swap to make useful
    }
    if ((word84Valid && wordPtr[84] & BIT5) || (word87Valid && wordPtr[87] & BIT5))
    {
        add_Feature_To_Supported_List(driveInfo->featuresSupported, &driveInfo->numberOfFeaturesSupported, "GPL");
        ataCapabilities->gplSupported = true;
    }
    if (word84Valid && wordPtr[84] & BIT4)
    {
        add_Feature_To_Supported_List(driveInfo->featuresSupported, &driveInfo->numberOfFeaturesSupported, "Streaming");
    }
    if ((word84Valid && wordPtr[84] & BIT3) || (word87Valid && wordPtr[87] & BIT3))
    {
        add_Feature_To_Supported_List(driveInfo->featuresSupported, &driveInfo->numberOfFeaturesSupported, "Media Card Pass-through");
    }
    if ((word84Valid && wordPtr[84] & BIT1) || (word87Valid && wordPtr[87] & BIT1))
    {
        add_Feature_To_Supported_List(driveInfo->featuresSupported, &driveInfo->numberOfFeaturesSupported, "SMART Self-Test");
    }
    if ((word84Valid && wordPtr[84] & BIT0) || (word87Valid && wordPtr[87] & BIT0))
    {
        ataCapabilities->smartErrorLoggingSupported = true;
        add_Feature_To_Supported_List(driveInfo->featuresSupported, &driveInfo->numberOfFeaturesSupported, "SMART Error Logging");
    }

    if (word88Valid && is_ATA_Identify_Word_Valid(wordPtr[88]) && driveInfo->interfaceSpeedInfo.speedType != INTERFACE_SPEED_SERIAL)
    {
        if (driveInfo->interfaceSpeedInfo.speedType != INTERFACE_SPEED_PARALLEL)
        {
            memset(&driveInfo->interfaceSpeedInfo, 0, sizeof(interfaceSpeed));//clear anything we've set so far
            driveInfo->interfaceSpeedInfo.speedType = INTERFACE_SPEED_PARALLEL;
            driveInfo->interfaceSpeedInfo.speedIsValid = true;
        }
        uint8_t supported = M_Byte0(wordPtr[88]);
        uint8_t selected = M_Byte1(wordPtr[88]);
        int8_t counter = -1;
        while (supported > 0)
        {
            supported = supported >> 1;
            ++counter;
        }
        switch (counter)
        {
        case 7://compact flash only
            driveInfo->interfaceSpeedInfo.parallelSpeed.maxSpeed = 167;
            driveInfo->interfaceSpeedInfo.parallelSpeed.maxModeNameValid = true;
            snprintf(driveInfo->interfaceSpeedInfo.parallelSpeed.maxModeName, PARALLEL_INTERFACE_MODE_NAME_MAX_LENGTH, "UDMA-7");
            break;
        case 6:
            driveInfo->interfaceSpeedInfo.parallelSpeed.maxSpeed = 133;
            driveInfo->interfaceSpeedInfo.parallelSpeed.maxModeNameValid = true;
            snprintf(driveInfo->interfaceSpeedInfo.parallelSpeed.maxModeName, PARALLEL_INTERFACE_MODE_NAME_MAX_LENGTH, "UDMA-6");
            break;
        case 5:
            driveInfo->interfaceSpeedInfo.parallelSpeed.maxSpeed = 100;
            driveInfo->interfaceSpeedInfo.parallelSpeed.maxModeNameValid = true;
            snprintf(driveInfo->interfaceSpeedInfo.parallelSpeed.maxModeName, PARALLEL_INTERFACE_MODE_NAME_MAX_LENGTH, "UDMA-5");
            break;
        case 4:
            driveInfo->interfaceSpeedInfo.parallelSpeed.maxSpeed = 66.7;
            driveInfo->interfaceSpeedInfo.parallelSpeed.maxModeNameValid = true;
            snprintf(driveInfo->interfaceSpeedInfo.parallelSpeed.maxModeName, PARALLEL_INTERFACE_MODE_NAME_MAX_LENGTH, "UDMA-4");
            break;
        case 3:
            driveInfo->interfaceSpeedInfo.parallelSpeed.maxSpeed = 44.4;
            driveInfo->interfaceSpeedInfo.parallelSpeed.maxModeNameValid = true;
            snprintf(driveInfo->interfaceSpeedInfo.parallelSpeed.maxModeName, PARALLEL_INTERFACE_MODE_NAME_MAX_LENGTH, "UDMA-3");
            break;
        case 2:
            driveInfo->interfaceSpeedInfo.parallelSpeed.maxSpeed = 33.3;
            driveInfo->interfaceSpeedInfo.parallelSpeed.maxModeNameValid = true;
            snprintf(driveInfo->interfaceSpeedInfo.parallelSpeed.maxModeName, PARALLEL_INTERFACE_MODE_NAME_MAX_LENGTH, "UDMA-2");
            break;
        case 1:
            driveInfo->interfaceSpeedInfo.parallelSpeed.maxSpeed = 25;
            driveInfo->interfaceSpeedInfo.parallelSpeed.maxModeNameValid = true;
            snprintf(driveInfo->interfaceSpeedInfo.parallelSpeed.maxModeName, PARALLEL_INTERFACE_MODE_NAME_MAX_LENGTH, "UDMA-1");
            break;
        case 0:
            driveInfo->interfaceSpeedInfo.parallelSpeed.maxSpeed = 16.7;
            driveInfo->interfaceSpeedInfo.parallelSpeed.maxModeNameValid = true;
            snprintf(driveInfo->interfaceSpeedInfo.parallelSpeed.maxModeName, PARALLEL_INTERFACE_MODE_NAME_MAX_LENGTH, "UDMA-0");
            break;
        }
        //now check selected
        if (selected > 0)
        {
            driveInfo->interfaceSpeedInfo.parallelSpeed.negotiatedValid = true;
            counter = -1;
            while (selected > 0)
            {
                selected = selected >> 1;
                ++counter;
            }
            switch (counter)
            {
            case 7://compact flash only
                driveInfo->interfaceSpeedInfo.parallelSpeed.negotiatedSpeed = 167;
                driveInfo->interfaceSpeedInfo.parallelSpeed.negModeNameValid = true;
                snprintf(driveInfo->interfaceSpeedInfo.parallelSpeed.negModeName, PARALLEL_INTERFACE_MODE_NAME_MAX_LENGTH, "UDMA-7");
                break;
            case 6:
                driveInfo->interfaceSpeedInfo.parallelSpeed.negotiatedSpeed = 133;
                driveInfo->interfaceSpeedInfo.parallelSpeed.negModeNameValid = true;
                snprintf(driveInfo->interfaceSpeedInfo.parallelSpeed.negModeName, PARALLEL_INTERFACE_MODE_NAME_MAX_LENGTH, "UDMA-6");
                break;
            case 5:
                driveInfo->interfaceSpeedInfo.parallelSpeed.negotiatedSpeed = 100;
                driveInfo->interfaceSpeedInfo.parallelSpeed.negModeNameValid = true;
                snprintf(driveInfo->interfaceSpeedInfo.parallelSpeed.negModeName, PARALLEL_INTERFACE_MODE_NAME_MAX_LENGTH, "UDMA-5");
                break;
            case 4:
                driveInfo->interfaceSpeedInfo.parallelSpeed.negotiatedSpeed = 66.7;
                driveInfo->interfaceSpeedInfo.parallelSpeed.negModeNameValid = true;
                snprintf(driveInfo->interfaceSpeedInfo.parallelSpeed.negModeName, PARALLEL_INTERFACE_MODE_NAME_MAX_LENGTH, "UDMA-4");
                break;
            case 3:
                driveInfo->interfaceSpeedInfo.parallelSpeed.negotiatedSpeed = 44.4;
                driveInfo->interfaceSpeedInfo.parallelSpeed.negModeNameValid = true;
                snprintf(driveInfo->interfaceSpeedInfo.parallelSpeed.negModeName, PARALLEL_INTERFACE_MODE_NAME_MAX_LENGTH, "UDMA-3");
                break;
            case 2:
                driveInfo->interfaceSpeedInfo.parallelSpeed.negotiatedSpeed = 33.3;
                driveInfo->interfaceSpeedInfo.parallelSpeed.negModeNameValid = true;
                snprintf(driveInfo->interfaceSpeedInfo.parallelSpeed.negModeName, PARALLEL_INTERFACE_MODE_NAME_MAX_LENGTH, "UDMA-2");
                break;
            case 1:
                driveInfo->interfaceSpeedInfo.parallelSpeed.negotiatedSpeed = 25;
                driveInfo->interfaceSpeedInfo.parallelSpeed.negModeNameValid = true;
                snprintf(driveInfo->interfaceSpeedInfo.parallelSpeed.negModeName, PARALLEL_INTERFACE_MODE_NAME_MAX_LENGTH, "UDMA-1");
                break;
            case 0:
                driveInfo->interfaceSpeedInfo.parallelSpeed.negotiatedSpeed = 16.7;
                driveInfo->interfaceSpeedInfo.parallelSpeed.negModeNameValid = true;
                snprintf(driveInfo->interfaceSpeedInfo.parallelSpeed.negModeName, PARALLEL_INTERFACE_MODE_NAME_MAX_LENGTH, "UDMA-0");
                break;
            }
        }
    }

    if (is_ATA_Identify_Word_Valid(wordPtr[89]))
    {
        if (wordPtr[89] & BIT15)
        {
            driveInfo->ataSecurityInformation.extendedTimeFormat = true;
            //bits 14:0
            driveInfo->ataSecurityInformation.securityEraseUnitTimeMinutes = (wordPtr[89] & 0x7FFF) * 2;
            if (driveInfo->ataSecurityInformation.securityEraseUnitTimeMinutes == (32767 * 2))
            {
                driveInfo->ataSecurityInformation.securityEraseUnitTimeMinutes = UINT16_MAX;
            }
        }
        else
        {
            //bits 7:0
            driveInfo->ataSecurityInformation.securityEraseUnitTimeMinutes = M_Byte0(wordPtr[89]) * 2;
            if (driveInfo->ataSecurityInformation.securityEraseUnitTimeMinutes == (255 * 2))
            {
                driveInfo->ataSecurityInformation.securityEraseUnitTimeMinutes = UINT16_MAX;
            }
        }
    }
    if (is_ATA_Identify_Word_Valid(wordPtr[90]))
    {
        if (wordPtr[90] & BIT15)
        {
            driveInfo->ataSecurityInformation.extendedTimeFormat = true;
            //bits 14:0
            driveInfo->ataSecurityInformation.enhancedSecurityEraseUnitTimeMinutes = (wordPtr[90] & 0x7FFF) * 2;
            if (driveInfo->ataSecurityInformation.enhancedSecurityEraseUnitTimeMinutes == (32767 * 2))
            {
                driveInfo->ataSecurityInformation.enhancedSecurityEraseUnitTimeMinutes = UINT16_MAX;
            }
        }
        else
        {
            //bits 7:0
            driveInfo->ataSecurityInformation.enhancedSecurityEraseUnitTimeMinutes = M_Byte0(wordPtr[90]) * 2;
            if (driveInfo->ataSecurityInformation.enhancedSecurityEraseUnitTimeMinutes == (255 * 2))
            {
                driveInfo->ataSecurityInformation.enhancedSecurityEraseUnitTimeMinutes = UINT16_MAX;
            }
        }
    }

    if (is_ATA_Identify_Word_Valid(wordPtr[92]))
    {
        driveInfo->ataSecurityInformation.masterPasswordIdentifier = wordPtr[92];
    }

    //get ATA cabling info for pata devices. SATA should clear this to zero
    //NOTE: In ATA8-APT there is table36 which goes over how to determine the cabling type.
    //      This table is mean to help a host know which speed to use at most based on results
    //      from both drive 0 and drive 1. In here we do not have this information, but we will report what
    //      this individual device is reporting.
    //      Annex A gives additional detail of a non-standard way to determine it, but it is not recommended.
    //TLDR: The drive may not detect this properly in certain cases...see ATA8-APT for details
    if (is_ATA_Identify_Word_Valid_With_Bits_14_And_15(wordPtr[93]))
    {
        if (driveInfo->interfaceSpeedInfo.speedType == INTERFACE_SPEED_PARALLEL)
        {
            driveInfo->interfaceSpeedInfo.parallelSpeed.cableInfoType = CABLING_INFO_ATA;
            driveInfo->interfaceSpeedInfo.parallelSpeed.ataCableInfo.cablingInfoValid = true;
            if (wordPtr[93] & BIT13)
            {
                driveInfo->interfaceSpeedInfo.parallelSpeed.ataCableInfo.ata80PinCableDetected = true;
            }
            if (M_GETBITRANGE(wordPtr[93], 12, 8) > 0 && wordPtr[93] & BIT8)
            {
                driveInfo->interfaceSpeedInfo.parallelSpeed.ataCableInfo.device1 = true;
                driveInfo->interfaceSpeedInfo.parallelSpeed.ataCableInfo.deviceNumberDetermined = M_GETBITRANGE(wordPtr[93], 10, 9);
            }
            else if (M_GETBITRANGE(wordPtr[93], 7, 0) > 0 && wordPtr[93] & BIT0)
            {
                driveInfo->interfaceSpeedInfo.parallelSpeed.ataCableInfo.device1 = false;
                driveInfo->interfaceSpeedInfo.parallelSpeed.ataCableInfo.deviceNumberDetermined = M_GETBITRANGE(wordPtr[93], 2, 1);
            }
        }
    }

    if (lbaModeSupported && driveInfo->maxLBA >= MAX_28BIT)
    {
        //max LBA from other words since 28bit max field is maxed out
        //check words 100-103 are valid values
        if (is_ATA_Identify_Word_Valid(wordPtr[100]) || is_ATA_Identify_Word_Valid(wordPtr[101]) || is_ATA_Identify_Word_Valid(wordPtr[102]) || is_ATA_Identify_Word_Valid(wordPtr[103]))
        {
            driveInfo->maxLBA = M_WordsTo8ByteValue(wordPtr[103], wordPtr[102], wordPtr[101], wordPtr[100]);
        }
    }

    //get the sector sizes from the identify data
    if (is_ATA_Identify_Word_Valid_With_Bits_14_And_15(wordPtr[106])) //making sure this word has valid data
    {
        //word 117 is only valid when word 106 bit 12 is set
        if ((wordPtr[106] & BIT12) == BIT12)
        {
            driveInfo->logicalSectorSize = M_WordsTo4ByteValue(wordPtr[117], wordPtr[118]);
            driveInfo->logicalSectorSize *= 2; //convert to words to bytes
        }
        else //means that logical sector size is 512bytes
        {
            driveInfo->logicalSectorSize = 512;
        }
        if ((wordPtr[106] & BIT13) == 0)
        {
            driveInfo->physicalSectorSize = driveInfo->logicalSectorSize;
        }
        else //multiple logical sectors per physical sector
        {
            uint8_t sectorSizeExponent = 0;
            //get the number of logical blocks per physical blocks
            sectorSizeExponent = wordPtr[106] & 0x000F;
            driveInfo->physicalSectorSize = C_CAST(uint32_t, driveInfo->logicalSectorSize * power_Of_Two(sectorSizeExponent));
        }
    }

    if (words119to120Valid && is_ATA_Identify_Word_Valid_With_Bits_14_And_15(wordPtr[119]) && is_ATA_Identify_Word_Valid_With_Bits_14_And_15(wordPtr[120]))
    {
        if (wordPtr[119] & BIT9)
        {
            if (wordPtr[120] & BIT9)
            {
                add_Feature_To_Supported_List(driveInfo->featuresSupported, &driveInfo->numberOfFeaturesSupported, "DSN [Enabled]");
            }
            else
            {
                add_Feature_To_Supported_List(driveInfo->featuresSupported, &driveInfo->numberOfFeaturesSupported, "DSN");
            }
        }
        if (wordPtr[119] & BIT8)
        {
            add_Feature_To_Supported_List(driveInfo->featuresSupported, &driveInfo->numberOfFeaturesSupported, "AMAC");
        }
        if (wordPtr[119] & BIT7)
        {
            if (wordPtr[120] & BIT7)
            {
                add_Feature_To_Supported_List(driveInfo->featuresSupported, &driveInfo->numberOfFeaturesSupported, "EPC [Enabled]");
            }
            else
            {
                add_Feature_To_Supported_List(driveInfo->featuresSupported, &driveInfo->numberOfFeaturesSupported, "EPC");
            }
        }
        if (wordPtr[119] & BIT6)
        {
            if (wordPtr[120] & BIT6)
            {
                add_Feature_To_Supported_List(driveInfo->featuresSupported, &driveInfo->numberOfFeaturesSupported, "Sense Data Reporting [Enabled]");
            }
            else
            {
                add_Feature_To_Supported_List(driveInfo->featuresSupported, &driveInfo->numberOfFeaturesSupported, "Sense Data Reporting");
            }
        }
        if (wordPtr[119] & BIT5)
        {
            if (wordPtr[120] & BIT5)
            {
                add_Feature_To_Supported_List(driveInfo->featuresSupported, &driveInfo->numberOfFeaturesSupported, "Free-fall Control [Enabled]");
            }
            else
            {
                add_Feature_To_Supported_List(driveInfo->featuresSupported, &driveInfo->numberOfFeaturesSupported, "Free-fall Control");
            }
        }
        if (wordPtr[119] & BIT4 || wordPtr[120] & BIT4)
        {
            driveInfo->fwdlSupport.segmentedSupported = true;
        }
        if (wordPtr[119] & BIT1)
        {
            if (wordPtr[120] & BIT1)
            {
                add_Feature_To_Supported_List(driveInfo->featuresSupported, &driveInfo->numberOfFeaturesSupported, "Write-Read-Verify [Enabled]");
            }
            else
            {
                add_Feature_To_Supported_List(driveInfo->featuresSupported, &driveInfo->numberOfFeaturesSupported, "Write-Read-Verify");
            }
        }
    }

    //ata security status
    //word 128
    if (is_ATA_Identify_Word_Valid(wordPtr[128]) && wordPtr[128] & BIT0)
    {
        driveInfo->ataSecurityInformation.securitySupported = true;
        if (wordPtr[128] & BIT1)
        {
            driveInfo->ataSecurityInformation.securityEnabled = true;
        }
        if (wordPtr[128] & BIT2)
        {
            driveInfo->ataSecurityInformation.securityLocked = true;
        }
        if (wordPtr[128] & BIT3)
        {
            driveInfo->ataSecurityInformation.securityFrozen = true;
        }
        if (wordPtr[128] & BIT4)
        {
            driveInfo->ataSecurityInformation.securityCountExpired = true;
        }
        if (wordPtr[128] & BIT5)
        {
            driveInfo->ataSecurityInformation.enhancedEraseSupported = true;
        }
        if (wordPtr[128] & BIT8)
        {
            driveInfo->ataSecurityInformation.masterPasswordCapability = true;
        }
    }

    //form factor
    if (is_ATA_Identify_Word_Valid(wordPtr[168]))
    {
        driveInfo->formFactor = M_Nibble0(wordPtr[168]);
    }
    if (is_ATA_Identify_Word_Valid(wordPtr[169]))
    {
        if (wordPtr[169] & BIT0)
        {
            //add additional info for deterministic and zeroes
            DECLARE_ZERO_INIT_ARRAY(char, trimDetails, 30);
            if (deterministicTrim || zeroesAfterTrim)
            {
                if (deterministicTrim && zeroesAfterTrim)
                {
                    snprintf(trimDetails, 30, "TRIM [Deterministic, Zeroes]");
                }
                else if (deterministicTrim)
                {
                    snprintf(trimDetails, 30, "TRIM [Deterministic]");
                }
                else if (zeroesAfterTrim)
                {
                    snprintf(trimDetails, 30, "TRIM [Zeroes]");
                }
                add_Feature_To_Supported_List(driveInfo->featuresSupported, &driveInfo->numberOfFeaturesSupported, trimDetails);
            }
            else
            {
                add_Feature_To_Supported_List(driveInfo->featuresSupported, &driveInfo->numberOfFeaturesSupported, "TRIM");
            }
        }
    }
    if (is_ATA_Identify_Word_Valid(wordPtr[206]))
    {
        if (wordPtr[206] & BIT0)
        {
            ataCapabilities->sctSupported = true;
            if (wordPtr[206] & BIT1)
            {
                add_Feature_To_Supported_List(driveInfo->featuresSupported, &driveInfo->numberOfFeaturesSupported, "SCT Read/Write Long");
            }
            if (wordPtr[206] & BIT2)
            {
                add_Feature_To_Supported_List(driveInfo->featuresSupported, &driveInfo->numberOfFeaturesSupported, "SCT Write Same");
            }
            if (wordPtr[206] & BIT3)
            {
                add_Feature_To_Supported_List(driveInfo->featuresSupported, &driveInfo->numberOfFeaturesSupported, "SCT Error Recovery Control");
            }
            if (wordPtr[206] & BIT4)
            {
                add_Feature_To_Supported_List(driveInfo->featuresSupported, &driveInfo->numberOfFeaturesSupported, "SCT Feature Control");
            }
            if (wordPtr[206] & BIT5)
            {
                add_Feature_To_Supported_List(driveInfo->featuresSupported, &driveInfo->numberOfFeaturesSupported, "SCT Data Tables");
            }
        }
    }
    //sector alignment
    if (is_ATA_Identify_Word_Valid_With_Bits_14_And_15(wordPtr[209]))
    {
        //bits 13:0 are valid for alignment. bit 15 will be 0 and bit 14 will be 1. remove bit 14 with an xor
        driveInfo->sectorAlignment = C_CAST(uint16_t, wordPtr[209] ^ BIT14);
    }
    if (is_ATA_Identify_Word_Valid(wordPtr[214]))
    {
        if (M_Byte3(wordPtr[214]) > 0)
        {
            add_Feature_To_Supported_List(driveInfo->featuresSupported, &driveInfo->numberOfFeaturesSupported, "NV Cache");
        }
        if (wordPtr[214] & BIT0)
        {
            if (wordPtr[214] & BIT1)
            {
                add_Feature_To_Supported_List(driveInfo->featuresSupported, &driveInfo->numberOfFeaturesSupported, "NV Cache Power Mode [Enabled]");
            }
            else
            {
                add_Feature_To_Supported_List(driveInfo->featuresSupported, &driveInfo->numberOfFeaturesSupported, "NV Cache Power Mode");
            }
        }
    }
    if (is_ATA_Identify_Word_Valid(wordPtr[215]) && is_ATA_Identify_Word_Valid(wordPtr[216]))
    {
        //NV Cache Size logical blocks - needs testing against different drives to make sure the value is correct
        driveInfo->hybridNANDSize = C_CAST(uint64_t, M_WordsTo4ByteValue(wordPtr[215], wordPtr[216])) * C_CAST(uint64_t, driveInfo->logicalSectorSize);
    }
    //rotation rate
    if (is_ATA_Identify_Word_Valid(wordPtr[217]))
    {
        driveInfo->rotationRate = wordPtr[217];
    }
    //Special case for SSD detection. One of these SSDs didn't set the media_type to SSD
    //but it is an SSD. So this match will catch it when this happens. It should be uncommon to find though -TJE
    if (driveInfo->rotationRate == 0 &&
        safe_strlen(driveInfo->modelNumber) > 0 && (strstr(driveInfo->modelNumber, "Seagate SSD") != M_NULLPTR) &&
        safe_strlen(driveInfo->firmwareRevision) > 0 && (strstr(driveInfo->firmwareRevision, "UHFS") != M_NULLPTR))
    {
        driveInfo->rotationRate = 0x0001;
    }
    //Transport specs supported.
    uint8_t transportType = 0;
    if (is_ATA_Identify_Word_Valid(wordPtr[222]))
    {
        specsBits = wordPtr[222];
        transportType = M_Nibble3(specsBits);//0 = parallel, 1 = serial, e = PCIe
        if (specsBits & BIT10)
        {
            if (transportType == 0x01)
            {
                add_Specification_To_Supported_List(driveInfo->specificationsSupported, &driveInfo->numberOfSpecificationsSupported, "SATA 3.5");
            }
            else
            {
                add_Specification_To_Supported_List(driveInfo->specificationsSupported, &driveInfo->numberOfSpecificationsSupported, "Reserved");
            }
        }
        if (specsBits & BIT9)
        {
            if (transportType == 0x01)
            {
                add_Specification_To_Supported_List(driveInfo->specificationsSupported, &driveInfo->numberOfSpecificationsSupported, "SATA 3.4");
            }
            else
            {
                add_Specification_To_Supported_List(driveInfo->specificationsSupported, &driveInfo->numberOfSpecificationsSupported, "Reserved");
            }
        }
        if (specsBits & BIT8)
        {
            if (transportType == 0x01)
            {
                add_Specification_To_Supported_List(driveInfo->specificationsSupported, &driveInfo->numberOfSpecificationsSupported, "SATA 3.3");
            }
            else
            {
                add_Specification_To_Supported_List(driveInfo->specificationsSupported, &driveInfo->numberOfSpecificationsSupported, "Reserved");
            }
        }
        if (specsBits & BIT7)
        {
            if (transportType == 0x01)
            {
                add_Specification_To_Supported_List(driveInfo->specificationsSupported, &driveInfo->numberOfSpecificationsSupported, "SATA 3.2");
            }
            else
            {
                add_Specification_To_Supported_List(driveInfo->specificationsSupported, &driveInfo->numberOfSpecificationsSupported, "Reserved");
            }
        }
        if (specsBits & BIT6)
        {
            if (transportType == 0x01)
            {
                add_Specification_To_Supported_List(driveInfo->specificationsSupported, &driveInfo->numberOfSpecificationsSupported, "SATA 3.1");
            }
            else
            {
                add_Specification_To_Supported_List(driveInfo->specificationsSupported, &driveInfo->numberOfSpecificationsSupported, "Reserved");
            }
        }
        if (specsBits & BIT5)
        {
            if (transportType == 0x01)
            {
                add_Specification_To_Supported_List(driveInfo->specificationsSupported, &driveInfo->numberOfSpecificationsSupported, "SATA 3.0");
            }
            else
            {
                add_Specification_To_Supported_List(driveInfo->specificationsSupported, &driveInfo->numberOfSpecificationsSupported, "Reserved");
            }
        }
        if (specsBits & BIT4)
        {
            if (transportType == 0x01)
            {
                add_Specification_To_Supported_List(driveInfo->specificationsSupported, &driveInfo->numberOfSpecificationsSupported, "SATA 2.6");
            }
            else
            {
                add_Specification_To_Supported_List(driveInfo->specificationsSupported, &driveInfo->numberOfSpecificationsSupported, "Reserved");
            }
        }
        if (specsBits & BIT3)
        {
            if (transportType == 0x01)
            {
                add_Specification_To_Supported_List(driveInfo->specificationsSupported, &driveInfo->numberOfSpecificationsSupported, "SATA 2.5");
            }
            else
            {
                add_Specification_To_Supported_List(driveInfo->specificationsSupported, &driveInfo->numberOfSpecificationsSupported, "Reserved");
            }
        }
        if (specsBits & BIT2)
        {
            if (transportType == 0x01)
            {
                add_Specification_To_Supported_List(driveInfo->specificationsSupported, &driveInfo->numberOfSpecificationsSupported, "SATA II: Extensions");
            }
            else
            {
                add_Specification_To_Supported_List(driveInfo->specificationsSupported, &driveInfo->numberOfSpecificationsSupported, "Reserved");
            }
        }
        if (specsBits & BIT1)
        {
            if (transportType == 0x01)
            {
                add_Specification_To_Supported_List(driveInfo->specificationsSupported, &driveInfo->numberOfSpecificationsSupported, "SATA 1.0a");
            }
            else if (transportType == 0)
            {
                add_Specification_To_Supported_List(driveInfo->specificationsSupported, &driveInfo->numberOfSpecificationsSupported, "ATA/ATAPI-7");
            }
            else
            {
                add_Specification_To_Supported_List(driveInfo->specificationsSupported, &driveInfo->numberOfSpecificationsSupported, "Reserved");
            }
        }
        if (specsBits & BIT0)
        {
            if (transportType == 0x01)
            {
                add_Specification_To_Supported_List(driveInfo->specificationsSupported, &driveInfo->numberOfSpecificationsSupported, "ATA8-AST");
            }
            else if (transportType == 0)
            {
                add_Specification_To_Supported_List(driveInfo->specificationsSupported, &driveInfo->numberOfSpecificationsSupported, "ATA8-APT");
            }
            else
            {
                add_Specification_To_Supported_List(driveInfo->specificationsSupported, &driveInfo->numberOfSpecificationsSupported, "Reserved");
            }
        }
    }
    if (is_ATA_Identify_Word_Valid(wordPtr[223]))
    {
        //transport minor version
        switch (wordPtr[223])
        {
        case TRANSPORT_MINOR_VERSION_ATA8_AST_D1697_VERSION_0B:
            add_Specification_To_Supported_List(driveInfo->specificationsSupported, &driveInfo->numberOfSpecificationsSupported, "ATA8-AST T13 Project D1697 Version 0b");
            break;
        case TRANSPORT_MINOR_VERSION_ATA8_AST_D1697_VERSION_1:
            add_Specification_To_Supported_List(driveInfo->specificationsSupported, &driveInfo->numberOfSpecificationsSupported, "ATA8-AST T13 Project D1697 Version 1");
            break;
        case TRANSPORT_MINOR_VERSION_NOT_REPORTED:
        case TRANSPORT_MINOR_VERSION_NOT_REPORTED2:
            break;
        }
    }

    //if word 69 bit 3 is set, then words 230-233 re valid
    if (extendedLBAFieldValid && (is_ATA_Identify_Word_Valid(wordPtr[230]) || is_ATA_Identify_Word_Valid(wordPtr[231]) || is_ATA_Identify_Word_Valid(wordPtr[232]) || is_ATA_Identify_Word_Valid(wordPtr[233])))
    {
        driveInfo->maxLBA = M_WordsTo8ByteValue(wordPtr[233], wordPtr[232], wordPtr[231], wordPtr[230]);
    }

    //adjust as reported value is one larger than last accessible LBA on the drive-TJE
    if (driveInfo->maxLBA > 0)
    {
        driveInfo->maxLBA -= 1;
    }

    if (ataCapabilities->seagateFamily == SEAGATE && is_ATA_Identify_Word_Valid(wordPtr[243]))
    {
        if (wordPtr[243] & BIT14)
        {
            //FDE
            driveInfo->encryptionSupport = ENCRYPTION_FULL_DISK;
        }
        if (wordPtr[243] & BIT12)
        {
            driveInfo->fwdlSupport.seagateDeferredPowerCycleRequired = true;
        }
    }

    if (transportType == 0xE)
    {
        memset(&driveInfo->interfaceSpeedInfo, 0, sizeof(interfaceSpeed));//clear anything we've set so far
        driveInfo->interfaceSpeedInfo.speedType = INTERFACE_SPEED_PCIE;
    }

    return ret;
}

//This function expects a complete ID data log, so the supported pages followed by all supported pages that were read.
static eReturnValues get_ATA_Drive_Info_From_ID_Data_Log(ptrDriveInformationSAS_SATA driveInfo, ptrIdDataCapabilitiesForDriveInfo ataCapabilities, uint8_t* idDataLog, uint32_t dataLength)
{
    eReturnValues ret = NOT_SUPPORTED;
    uint8_t pageNumber = idDataLog[2];
    uint16_t revision = M_BytesTo2ByteValue(idDataLog[1], idDataLog[0]);
    if (pageNumber == C_CAST(uint8_t, ATA_ID_DATA_LOG_SUPPORTED_PAGES) && revision >= ATA_ID_DATA_VERSION_1)
    {
        ret = SUCCESS;
        ataCapabilities->supportsIDDataLog = true;
        //data is valid, so figure out supported pages
        uint8_t listLen = idDataLog[ATA_ID_DATA_SUP_PG_LIST_LEN_OFFSET];
        uint32_t offset = 0;
        bool dlcSupported = false;
        bool dlcEnabled = false;
        bool cdlSupported = false;
        bool cdlEnabled = false;
        for (uint16_t iter = ATA_ID_DATA_SUP_PG_LIST_OFFSET; iter < C_CAST(uint16_t, listLen + ATA_ID_DATA_SUP_PG_LIST_OFFSET) && iter < ATA_LOG_PAGE_LEN_BYTES; ++iter)
        {
            switch (idDataLog[iter])
            {
            case ATA_ID_DATA_LOG_SUPPORTED_PAGES:
                break;
            case ATA_ID_DATA_LOG_COPY_OF_IDENTIFY_DATA:
                ataCapabilities->supportedIDDataPages.copyOfIdentify = true;
                break;
            case ATA_ID_DATA_LOG_CAPACITY:
                ataCapabilities->supportedIDDataPages.capacity = true;
                break;
            case ATA_ID_DATA_LOG_SUPPORTED_CAPABILITIES:
                ataCapabilities->supportedIDDataPages.supportedCapabilities = true;
                break;
            case ATA_ID_DATA_LOG_CURRENT_SETTINGS:
                ataCapabilities->supportedIDDataPages.currentSettings = true;
                break;
            case ATA_ID_DATA_LOG_ATA_STRINGS:
                ataCapabilities->supportedIDDataPages.strings = true;
                break;
            case ATA_ID_DATA_LOG_SECURITY:
                ataCapabilities->supportedIDDataPages.security = true;
                break;
            case ATA_ID_DATA_LOG_PARALLEL_ATA:
                ataCapabilities->supportedIDDataPages.parallelATA = true;
                break;
            case ATA_ID_DATA_LOG_SERIAL_ATA:
                ataCapabilities->supportedIDDataPages.serialATA = true;
                break;
            case ATA_ID_DATA_LOG_ZONED_DEVICE_INFORMATION:
                ataCapabilities->supportedIDDataPages.zac2 = true;
                break;
            default:
                break;
            }
        }
        offset = ATA_LOG_PAGE_LEN_BYTES * ATA_ID_DATA_LOG_CAPACITY;
        if (ataCapabilities->supportedIDDataPages.capacity && (offset + ATA_LOG_PAGE_LEN_BYTES) <= dataLength)
        {
            uint64_t qword0 = M_BytesTo8ByteValue(idDataLog[offset + 7], idDataLog[offset + 6], idDataLog[offset + 5], idDataLog[offset + 4], idDataLog[offset + 3], idDataLog[offset + 2], idDataLog[offset + 1], idDataLog[offset + 0]);
            if (qword0 & ATA_ID_DATA_QWORD_VALID_BIT && M_Byte2(qword0) == ATA_ID_DATA_LOG_CAPACITY && M_Word0(qword0) >= ATA_ID_DATA_VERSION_1)
            {
                //get the nominal buffer size
                uint64_t nominalBufferSize = M_BytesTo8ByteValue(idDataLog[offset + 39], idDataLog[offset + 38], idDataLog[offset + 37], idDataLog[offset + 36], idDataLog[offset + 35], idDataLog[offset + 34], idDataLog[offset + 33], idDataLog[offset + 32]);
                if (nominalBufferSize & ATA_ID_DATA_QWORD_VALID_BIT)
                {
                    //data is valid. Remove bit 63
                    M_CLEAR_BIT(nominalBufferSize, 63);
                    //now save this value to cache size (number of bytes)
                    driveInfo->cacheSize = nominalBufferSize;
                }
            }
        }
        offset = ATA_LOG_PAGE_LEN_BYTES * ATA_ID_DATA_LOG_SUPPORTED_CAPABILITIES;
        if (ataCapabilities->supportedIDDataPages.supportedCapabilities && (offset + ATA_LOG_PAGE_LEN_BYTES) <= dataLength)
        {
            //supported capabilities
            uint64_t qword0 = M_BytesTo8ByteValue(idDataLog[offset + 7], idDataLog[offset + 6], idDataLog[offset + 5], idDataLog[offset + 4], idDataLog[offset + 3], idDataLog[offset + 2], idDataLog[offset + 1], idDataLog[offset + 0]);
            if (qword0 & ATA_ID_DATA_QWORD_VALID_BIT && M_Byte2(qword0) == ATA_ID_DATA_LOG_SUPPORTED_CAPABILITIES && M_Word0(qword0) >= ATA_ID_DATA_VERSION_1)
            {
                uint64_t supportedCapabilitiesQWord = M_BytesTo8ByteValue(idDataLog[offset + 15], idDataLog[offset + 14], idDataLog[offset + 13], idDataLog[offset + 12], idDataLog[offset + 11], idDataLog[offset + 10], idDataLog[offset + 9], idDataLog[offset + 8]);
                if (supportedCapabilitiesQWord & ATA_ID_DATA_QWORD_VALID_BIT)
                {
                    if (supportedCapabilitiesQWord & BIT55)
                    {
                        add_Feature_To_Supported_List(driveInfo->featuresSupported, &driveInfo->numberOfFeaturesSupported, "Mutate");
                    }
                    if (supportedCapabilitiesQWord & BIT54)
                    {
                        add_Feature_To_Supported_List(driveInfo->featuresSupported, &driveInfo->numberOfFeaturesSupported, "Advanced Background Operations");
                    }
                    if (supportedCapabilitiesQWord & BIT49)
                    {
                        add_Feature_To_Supported_List(driveInfo->featuresSupported, &driveInfo->numberOfFeaturesSupported, "Set Sector Configuration");
                    }
                    if (supportedCapabilitiesQWord & BIT46)
                    {
                        dlcSupported = true;
                    }
                }
                //Download capabilities
                uint64_t downloadCapabilities = M_BytesTo8ByteValue(idDataLog[offset + 23], idDataLog[offset + 22], idDataLog[offset + 21], idDataLog[offset + 20], idDataLog[offset + 19], idDataLog[offset + 18], idDataLog[offset + 17], idDataLog[offset + 16]);
                if (downloadCapabilities & ATA_ID_DATA_QWORD_VALID_BIT)
                {
                    if (downloadCapabilities & BIT34)
                    {
                        driveInfo->fwdlSupport.deferredSupported = true;
                    }
                }
                //Utilization (IDK if we need anything from this log for this information)

                //zoned capabilities
                uint64_t zonedCapabilities = M_BytesTo8ByteValue(idDataLog[offset + 111], idDataLog[offset + 110], idDataLog[offset + 109], idDataLog[offset + 108], idDataLog[offset + 107], idDataLog[offset + 106], idDataLog[offset + 105], idDataLog[offset + 104]);
                if (zonedCapabilities & ATA_ID_DATA_QWORD_VALID_BIT)
                {
                    //we only need bits 1 & 2
                    driveInfo->zonedDevice = zonedCapabilities & 0x3;
                }

                //depopulate storage element support
                uint64_t supportedCapabilitiesQWord18 = M_BytesTo8ByteValue(idDataLog[offset + 159], idDataLog[offset + 158], idDataLog[offset + 157], idDataLog[offset + 156], idDataLog[offset + 155], idDataLog[offset + 154], idDataLog[offset + 153], idDataLog[offset + 152]);
                if (supportedCapabilitiesQWord18 & ATA_ID_DATA_QWORD_VALID_BIT)//making sure this is set for "validity"
                {
                    if (supportedCapabilitiesQWord18 & BIT1 && supportedCapabilitiesQWord18 & BIT0)//checking for both commands to be supported
                    {
                        if (supportedCapabilitiesQWord18 & BIT2)
                        {
                            add_Feature_To_Supported_List(driveInfo->featuresSupported, &driveInfo->numberOfFeaturesSupported, "Storage Element Depopulation + Restore");
                        }
                        else
                        {
                            add_Feature_To_Supported_List(driveInfo->featuresSupported, &driveInfo->numberOfFeaturesSupported, "Storage Element Depopulation");
                        }
                    }
                }

                //Command Duration Limits
                uint64_t cdlSupportQword = M_BytesTo8ByteValue(idDataLog[offset + 175], idDataLog[offset + 174], idDataLog[offset + 173], idDataLog[offset + 172], idDataLog[offset + 171], idDataLog[offset + 170], idDataLog[offset + 169], idDataLog[offset + 168]);
                if (cdlSupportQword & ATA_ID_DATA_QWORD_VALID_BIT)//making sure this is set for "validity"
                {
                    if (cdlSupportQword & BIT0)
                    {
                        cdlSupported = true;
                    }
                }
            }
        }
        offset = ATA_LOG_PAGE_LEN_BYTES * ATA_ID_DATA_LOG_CURRENT_SETTINGS;
        if (ataCapabilities->supportedIDDataPages.currentSettings && (offset + ATA_LOG_PAGE_LEN_BYTES) <= dataLength)
        {
            uint64_t qword0 = M_BytesTo8ByteValue(idDataLog[offset + 7], idDataLog[offset + 6], idDataLog[offset + 5], idDataLog[offset + 4], idDataLog[offset + 3], idDataLog[offset + 2], idDataLog[offset + 1], idDataLog[offset + 0]);
            if (qword0 & ATA_ID_DATA_QWORD_VALID_BIT && M_Byte2(qword0) == ATA_ID_DATA_LOG_CURRENT_SETTINGS && M_Word0(qword0) >= ATA_ID_DATA_VERSION_1)
            {
                uint64_t currentSettingsQWord = M_BytesTo8ByteValue(idDataLog[offset + 15], idDataLog[offset + 14], idDataLog[offset + 13], idDataLog[offset + 12], idDataLog[offset + 11], idDataLog[offset + 10], idDataLog[offset + 9], idDataLog[offset + 8]);
                if (currentSettingsQWord & ATA_ID_DATA_QWORD_VALID_BIT)
                {
                    if (currentSettingsQWord & BIT21)
                    {
                        cdlEnabled = true;
                    }
                    if (currentSettingsQWord & BIT17)
                    {
                        dlcEnabled = true;
                    }
                }
            }
        }
        if (dlcSupported)
        {
            if (dlcEnabled)
            {
                add_Feature_To_Supported_List(driveInfo->featuresSupported, &driveInfo->numberOfFeaturesSupported, "Device Life Control [Enabled]");
            }
            else
            {
                add_Feature_To_Supported_List(driveInfo->featuresSupported, &driveInfo->numberOfFeaturesSupported, "Device Life Control");
            }
        }
        if (cdlSupported)
        {
            if (cdlEnabled)
            {
                add_Feature_To_Supported_List(driveInfo->featuresSupported, &driveInfo->numberOfFeaturesSupported, "Command Duration Limits [Enabled]");
            }
            else
            {
                add_Feature_To_Supported_List(driveInfo->featuresSupported, &driveInfo->numberOfFeaturesSupported, "Command Duration Limits");
            }
        }
        /*offset = ATA_LOG_PAGE_LEN_BYTES * ATA_ID_DATA_LOG_ATA_STRINGS;
        if (ataCapabilities->supportedIDDataPages.strings && (offset + ATA_LOG_PAGE_LEN_BYTES) <= dataLength)
        {
            uint64_t qword0 = M_BytesTo8ByteValue(idDataLog[offset + 7], idDataLog[offset + 6], idDataLog[offset + 5], idDataLog[offset + 4], idDataLog[offset + 3], idDataLog[offset + 2], idDataLog[offset + 1], idDataLog[offset + 0]);
            if (qword0 & ATA_ID_DATA_QWORD_VALID_BIT && M_Byte2(qword0) == ATA_ID_DATA_LOG_ATA_STRINGS && M_Word0(qword0) >= ATA_ID_DATA_VERSION_1)
            {
            }
        }
        offset = ATA_LOG_PAGE_LEN_BYTES * ATA_ID_DATA_LOG_SECURITY;
        if (ataCapabilities->supportedIDDataPages.security && (offset + ATA_LOG_PAGE_LEN_BYTES) <= dataLength)
        {
            uint64_t qword0 = M_BytesTo8ByteValue(idDataLog[offset + 7], idDataLog[offset + 6], idDataLog[offset + 5], idDataLog[offset + 4], idDataLog[offset + 3], idDataLog[offset + 2], idDataLog[offset + 1], idDataLog[offset + 0]);
            if (qword0 & ATA_ID_DATA_QWORD_VALID_BIT && M_Byte2(qword0) == ATA_ID_DATA_LOG_SECURITY && M_Word0(qword0) >= ATA_ID_DATA_VERSION_1)
            {
            }
        }
        offset = ATA_LOG_PAGE_LEN_BYTES * ATA_ID_DATA_LOG_PARALLEL_ATA;
        if (ataCapabilities->supportedIDDataPages.parallelATA && (offset + ATA_LOG_PAGE_LEN_BYTES) <= dataLength)
        {
            uint64_t qword0 = M_BytesTo8ByteValue(idDataLog[offset + 7], idDataLog[offset + 6], idDataLog[offset + 5], idDataLog[offset + 4], idDataLog[offset + 3], idDataLog[offset + 2], idDataLog[offset + 1], idDataLog[offset + 0]);
            if (qword0 & ATA_ID_DATA_QWORD_VALID_BIT && M_Byte2(qword0) == ATA_ID_DATA_LOG_PARALLEL_ATA && M_Word0(qword0) >= ATA_ID_DATA_VERSION_1)
            {
            }
        }
        offset = ATA_LOG_PAGE_LEN_BYTES * ATA_ID_DATA_LOG_SERIAL_ATA;
        if (ataCapabilities->supportedIDDataPages.serialATA && (offset + ATA_LOG_PAGE_LEN_BYTES) <= dataLength)
        {
            uint64_t qword0 = M_BytesTo8ByteValue(idDataLog[offset + 7], idDataLog[offset + 6], idDataLog[offset + 5], idDataLog[offset + 4], idDataLog[offset + 3], idDataLog[offset + 2], idDataLog[offset + 1], idDataLog[offset + 0]);
            if (qword0 & ATA_ID_DATA_QWORD_VALID_BIT && M_Byte2(qword0) == ATA_ID_DATA_LOG_SERIAL_ATA && M_Word0(qword0) >= ATA_ID_DATA_VERSION_1)
            {
            }
        }*/
        offset = ATA_LOG_PAGE_LEN_BYTES * ATA_ID_DATA_LOG_ZONED_DEVICE_INFORMATION;
        if (ataCapabilities->supportedIDDataPages.zac2 && (offset + ATA_LOG_PAGE_LEN_BYTES) <= dataLength)
        {
            uint64_t qword0 = M_BytesTo8ByteValue(idDataLog[offset + 7], idDataLog[offset + 6], idDataLog[offset + 5], idDataLog[offset + 4], idDataLog[offset + 3], idDataLog[offset + 2], idDataLog[offset + 1], idDataLog[offset + 0]);
            if (qword0 & ATA_ID_DATA_QWORD_VALID_BIT && M_Byte2(qword0) == ATA_ID_DATA_LOG_ZONED_DEVICE_INFORMATION && M_Word0(qword0) >= ATA_ID_DATA_VERSION_1)
            {
                uint64_t zonedSettingsQWord = M_BytesTo8ByteValue(idDataLog[offset + 15], idDataLog[offset + 14], idDataLog[offset + 13], idDataLog[offset + 12], idDataLog[offset + 11], idDataLog[offset + 10], idDataLog[offset + 9], idDataLog[offset + 8]);
                uint64_t versionQWord = M_BytesTo8ByteValue(idDataLog[offset + 55], idDataLog[offset + 54], idDataLog[offset + 53], idDataLog[offset + 52], idDataLog[offset + 51], idDataLog[offset + 50], idDataLog[offset + 49], idDataLog[offset + 48]);
                uint64_t zoneActCapQWord = M_BytesTo8ByteValue(idDataLog[offset + 63], idDataLog[offset + 62], idDataLog[offset + 61], idDataLog[offset + 60], idDataLog[offset + 59], idDataLog[offset + 58], idDataLog[offset + 57], idDataLog[offset + 56]);
                if (zonedSettingsQWord & ATA_ID_DATA_QWORD_VALID_BIT)
                {
                    if (zonedSettingsQWord & BIT1)
                    {
                        add_Feature_To_Supported_List(driveInfo->featuresSupported, &driveInfo->numberOfFeaturesSupported, "Remove Element And Modify Zones");
                    }
                }
                if (versionQWord & ATA_ID_DATA_QWORD_VALID_BIT)
                {
                    switch (M_Word0(versionQWord))
                    {
                    case ZAC_MINOR_VERSION_NOT_REPORTED:
                        break;
                    case ZAC_MINOR_VERSION_ZAC_REV_5:
                        add_Specification_To_Supported_List(driveInfo->specificationsSupported, &driveInfo->numberOfSpecificationsSupported, "ZAC (Revision 5)");
                        break;
                    case ZAC_MINOR_VERSION_ZAC2_REV_15:
                        add_Specification_To_Supported_List(driveInfo->specificationsSupported, &driveInfo->numberOfSpecificationsSupported, "ZAC-2 (Revision 15)");
                        break;
                    case ZAC_MINOR_VERSION_ZAC2_REV_1B:
                        add_Specification_To_Supported_List(driveInfo->specificationsSupported, &driveInfo->numberOfSpecificationsSupported, "ZAC-2 (Revision 1B)");
                        break;
                    case ZAC_MINOR_VERSION_ZAC_REV_4:
                        add_Specification_To_Supported_List(driveInfo->specificationsSupported, &driveInfo->numberOfSpecificationsSupported, "ZAC (Revision 4)");
                        break;
                    case ZAC_MINOR_VERSION_ZAC2_REV12:
                        add_Specification_To_Supported_List(driveInfo->specificationsSupported, &driveInfo->numberOfSpecificationsSupported, "ZAC-2 (Revision 12)");
                        break;
                    case ZAC_MINOR_VERSION_ZAC_REV_1:
                        add_Specification_To_Supported_List(driveInfo->specificationsSupported, &driveInfo->numberOfSpecificationsSupported, "ZAC (Revision 1)");
                        break;
                    case ZAC_MINOR_VERSION_NOT_REPORTED_2:
                        break;
                    default:
                        //add_Specification_To_Supported_List(driveInfo->specificationsSupported, &driveInfo->numberOfSpecificationsSupported, "Unknown ZAC Minor version: %04" PRIX16, M_Word0(versionQWord));
                        break;
                    }
                }
                if (zoneActCapQWord & ATA_ID_DATA_QWORD_VALID_BIT)
                {
                    if (zoneActCapQWord & BIT0)
                    {
                        add_Feature_To_Supported_List(driveInfo->featuresSupported, &driveInfo->numberOfFeaturesSupported, "Zone Domains");
                    }
                    if (zoneActCapQWord & BIT1)
                    {
                        add_Feature_To_Supported_List(driveInfo->featuresSupported, &driveInfo->numberOfFeaturesSupported, "Zone Realms");
                    }
                }
            }
        }
        if (ataCapabilities->supportedIDDataPages.copyOfIdentify && !ataCapabilities->processedStdIDData)
        {
            offset = ATA_LOG_PAGE_LEN_BYTES * ATA_ID_DATA_LOG_COPY_OF_IDENTIFY_DATA;
            //std identify data was not already processed, so process it here for anything not covered by the above code.
            get_ATA_Drive_Info_From_Identify(driveInfo, ataCapabilities, &idDataLog[offset], ATA_LOG_PAGE_LEN_BYTES);
        }
    }
    return ret;
}

//this function expects a complete ID data log, so the supported pages followed by all supported pages that were read.
static eReturnValues get_ATA_Drive_Info_From_Device_Statistics_Log(ptrDriveInformationSAS_SATA driveInfo, ptrIdDataCapabilitiesForDriveInfo ataCapabilities, uint8_t* idDataLog, uint32_t dataLength)
{
    eReturnValues ret = NOT_SUPPORTED;
    uint8_t pageNumber = idDataLog[2];
    uint16_t revision = M_BytesTo2ByteValue(idDataLog[1], idDataLog[0]);
    M_USE_UNUSED(ataCapabilities);
    if (pageNumber == C_CAST(uint8_t, ATA_DEVICE_STATS_LOG_LIST) && revision >= ATA_DEV_STATS_VERSION_1)
    {
        bool generalStatistics = false;
        //bool freeFallStatistics = false;
        //bool rotatingMediaStatistics = false;
        //bool generalErrorStatistics = false;
        bool temperatureStatistics = false;
        //bool transportStatistics = false;
        bool solidStateStatistics = false;
        //bool zonedDeviceStatistics = false;
        uint32_t offset = 0;
        uint16_t iter = ATA_DEV_STATS_SUP_PG_LIST_OFFSET;
        uint8_t numberOfEntries = idDataLog[ATA_DEV_STATS_SUP_PG_LIST_LEN_OFFSET];
        ret = SUCCESS;
        for (iter = ATA_DEV_STATS_SUP_PG_LIST_OFFSET; iter < (numberOfEntries + ATA_DEV_STATS_SUP_PG_LIST_OFFSET) && iter < ATA_LOG_PAGE_LEN_BYTES; ++iter)
        {
            switch (idDataLog[iter])
            {
            case ATA_DEVICE_STATS_LOG_LIST:
                break;
            case ATA_DEVICE_STATS_LOG_GENERAL:
                generalStatistics = true;
                break;
            case ATA_DEVICE_STATS_LOG_FREE_FALL:
                //freeFallStatistics = true;
                break;
            case ATA_DEVICE_STATS_LOG_ROTATING_MEDIA:
                //rotatingMediaStatistics = true;
                break;
            case ATA_DEVICE_STATS_LOG_GEN_ERR:
                //generalErrorStatistics = true;
                break;
            case ATA_DEVICE_STATS_LOG_TEMP:
                temperatureStatistics = true;
                break;
            case ATA_DEVICE_STATS_LOG_TRANSPORT:
                //transportStatistics = true;
                break;
            case ATA_DEVICE_STATS_LOG_SSD:
                solidStateStatistics = true;
                break;
            case ATA_DEVICE_STATS_LOG_ZONED_DEVICE:
                //zonedDeviceStatistics = true;
            default:
                break;
            }
        }
        offset = ATA_LOG_PAGE_LEN_BYTES * ATA_DEVICE_STATS_LOG_GENERAL;
        if (generalStatistics && (offset + ATA_LOG_PAGE_LEN_BYTES) <= dataLength)
        {
            uint64_t qword0 = M_BytesTo8ByteValue(idDataLog[offset + 7], idDataLog[offset + 6], idDataLog[offset + 5], idDataLog[offset + 4], idDataLog[offset + 3], idDataLog[offset + 2], idDataLog[offset + 1], idDataLog[offset + 0]);
            if (M_Byte2(qword0) == ATA_DEVICE_STATS_LOG_GENERAL && M_Word0(qword0) >= ATA_DEV_STATS_VERSION_1)//validating we got the right page
            {
                //power on hours
                uint64_t pohQword = M_BytesTo8ByteValue(idDataLog[offset + 23], idDataLog[offset + 22], idDataLog[offset + 21], idDataLog[offset + 20], idDataLog[offset + 19], idDataLog[offset + 18], idDataLog[offset + 17], idDataLog[offset + 16]);
                if (pohQword & ATA_DEV_STATS_STATISTIC_SUPPORTED_BIT && pohQword & BIT62)
                {
                    driveInfo->powerOnMinutes = C_CAST(uint64_t, M_DoubleWord0(pohQword)) * UINT64_C(60);
                    driveInfo->powerOnMinutesValid = true;
                }
                //logical sectors written
                uint64_t lsWrittenQword = M_BytesTo8ByteValue(idDataLog[offset + 31], idDataLog[offset + 30], idDataLog[offset + 29], idDataLog[offset + 28], idDataLog[offset + 27], idDataLog[offset + 26], idDataLog[offset + 25], idDataLog[offset + 24]);
                if (lsWrittenQword & ATA_DEV_STATS_STATISTIC_SUPPORTED_BIT && lsWrittenQword & BIT62)
                {
                    driveInfo->totalLBAsWritten = lsWrittenQword & MAX_48_BIT_LBA;
                }
                //logical sectors read
                uint64_t lsReadQword = M_BytesTo8ByteValue(idDataLog[offset + 47], idDataLog[offset + 46], idDataLog[offset + 45], idDataLog[offset + 44], idDataLog[offset + 43], idDataLog[offset + 42], idDataLog[offset + 41], idDataLog[offset + 40]);
                if (lsReadQword & ATA_DEV_STATS_STATISTIC_SUPPORTED_BIT && lsReadQword & BIT62)
                {
                    driveInfo->totalLBAsRead = lsReadQword & MAX_48_BIT_LBA;
                }
                //workload utilization
                uint64_t worloadUtilization = M_BytesTo8ByteValue(idDataLog[offset + 79], idDataLog[offset + 78], idDataLog[offset + 77], idDataLog[offset + 76], idDataLog[offset + 75], idDataLog[offset + 74], idDataLog[offset + 73], idDataLog[offset + 72]);
                if (worloadUtilization & ATA_DEV_STATS_STATISTIC_SUPPORTED_BIT && worloadUtilization & BIT62)
                {
                    driveInfo->deviceReportedUtilizationRate = C_CAST(double, M_Word0(worloadUtilization)) / 1000.0;
                }
            }
        }
        //offset = ATA_LOG_PAGE_LEN_BYTES * ATA_DEVICE_STATS_LOG_FREE_FALL;
        //if (freeFallStatistics && (offset + ATA_LOG_PAGE_LEN_BYTES) <= dataLength)
        //{
        //    uint64_t qword0 = M_BytesTo8ByteValue(idDataLog[offset + 7], idDataLog[offset + 6], idDataLog[offset + 5], idDataLog[offset + 4], idDataLog[offset + 3], idDataLog[offset + 2], idDataLog[offset + 1], idDataLog[offset + 0]);
        //    if (M_Byte2(qword0) == ATA_DEVICE_STATS_LOG_FREE_FALL && M_Word0(qword0) >= ATA_DEV_STATS_VERSION_1)//validating we got the right page
        //    {
        //    }
        //}
        //offset = ATA_LOG_PAGE_LEN_BYTES * ATA_DEVICE_STATS_LOG_ROTATING_MEDIA;
        //if (rotatingMediaStatistics && (offset + ATA_LOG_PAGE_LEN_BYTES) <= dataLength)
        //{
        //    uint64_t qword0 = M_BytesTo8ByteValue(idDataLog[offset + 7], idDataLog[offset + 6], idDataLog[offset + 5], idDataLog[offset + 4], idDataLog[offset + 3], idDataLog[offset + 2], idDataLog[offset + 1], idDataLog[offset + 0]);
        //    if (M_Byte2(qword0) == ATA_DEVICE_STATS_LOG_ROTATING_MEDIA && M_Word0(qword0) >= ATA_DEV_STATS_VERSION_1)//validating we got the right page
        //    {
        //    }
        //}
        //offset = ATA_LOG_PAGE_LEN_BYTES * ATA_DEVICE_STATS_LOG_GEN_ERR;
        //if (generalErrorStatistics && (offset + ATA_LOG_PAGE_LEN_BYTES) <= dataLength)
        //{
        //    uint64_t qword0 = M_BytesTo8ByteValue(idDataLog[offset + 7], idDataLog[offset + 6], idDataLog[offset + 5], idDataLog[offset + 4], idDataLog[offset + 3], idDataLog[offset + 2], idDataLog[offset + 1], idDataLog[offset + 0]);
        //    if (M_Byte2(qword0) == ATA_DEVICE_STATS_LOG_GEN_ERR && M_Word0(qword0) >= ATA_DEV_STATS_VERSION_1)//validating we got the right page
        //    {
        //    }
        //}
        offset = ATA_LOG_PAGE_LEN_BYTES * ATA_DEVICE_STATS_LOG_TEMP;
        if (temperatureStatistics && (offset + ATA_LOG_PAGE_LEN_BYTES) <= dataLength)
        {
            uint64_t qword0 = M_BytesTo8ByteValue(idDataLog[offset + 7], idDataLog[offset + 6], idDataLog[offset + 5], idDataLog[offset + 4], idDataLog[offset + 3], idDataLog[offset + 2], idDataLog[offset + 1], idDataLog[offset + 0]);
            if (M_Byte2(qword0) == ATA_DEVICE_STATS_LOG_TEMP && M_Word0(qword0) >= ATA_DEV_STATS_VERSION_1)//validating we got the right page
            {
                //current temperature
                uint64_t currentTemp = M_BytesTo8ByteValue(idDataLog[offset + 15], idDataLog[offset + 14], idDataLog[offset + 13], idDataLog[offset + 12], idDataLog[offset + 11], idDataLog[offset + 10], idDataLog[offset + 9], idDataLog[offset + 8]);
                if (currentTemp & ATA_DEV_STATS_STATISTIC_SUPPORTED_BIT && currentTemp & BIT62)
                {
                    driveInfo->temperatureData.temperatureDataValid = true;
                    driveInfo->temperatureData.currentTemperature = M_Byte0(currentTemp);
                }
                //highest temperature
                uint64_t highestTemp = M_BytesTo8ByteValue(idDataLog[offset + 39], idDataLog[offset + 38], idDataLog[offset + 37], idDataLog[offset + 36], idDataLog[offset + 35], idDataLog[offset + 34], idDataLog[offset + 33], idDataLog[offset + 32]);
                if (highestTemp & ATA_DEV_STATS_STATISTIC_SUPPORTED_BIT && highestTemp & BIT62)
                {
                    driveInfo->temperatureData.highestTemperature = M_Byte0(highestTemp);
                    driveInfo->temperatureData.highestValid = true;
                }
                //lowest temperature
                uint64_t lowestTemp = M_BytesTo8ByteValue(idDataLog[offset + 47], idDataLog[offset + 46], idDataLog[offset + 45], idDataLog[offset + 44], idDataLog[offset + 43], idDataLog[offset + 42], idDataLog[offset + 41], idDataLog[offset + 40]);
                if (lowestTemp & ATA_DEV_STATS_STATISTIC_SUPPORTED_BIT && lowestTemp & BIT62)
                {
                    driveInfo->temperatureData.lowestTemperature = M_Byte0(lowestTemp);
                    driveInfo->temperatureData.lowestValid = true;
                }
            }
        }
        //offset = ATA_LOG_PAGE_LEN_BYTES * ATA_DEVICE_STATS_LOG_TRANSPORT;
        //if (transportStatistics && (offset + ATA_LOG_PAGE_LEN_BYTES) <= dataLength)
        //{
        //    uint64_t qword0 = M_BytesTo8ByteValue(idDataLog[offset + 7], idDataLog[offset + 6], idDataLog[offset + 5], idDataLog[offset + 4], idDataLog[offset + 3], idDataLog[offset + 2], idDataLog[offset + 1], idDataLog[offset + 0]);
        //    if (M_Byte2(qword0) == ATA_DEVICE_STATS_LOG_TRANSPORT && M_Word0(qword0) >= ATA_DEV_STATS_VERSION_1)//validating we got the right page
        //    {
        //    }
        //}
        offset = ATA_LOG_PAGE_LEN_BYTES * ATA_DEVICE_STATS_LOG_SSD;
        if (solidStateStatistics && (offset + ATA_LOG_PAGE_LEN_BYTES) <= dataLength)
        {
            uint64_t qword0 = M_BytesTo8ByteValue(idDataLog[offset + 7], idDataLog[offset + 6], idDataLog[offset + 5], idDataLog[offset + 4], idDataLog[offset + 3], idDataLog[offset + 2], idDataLog[offset + 1], idDataLog[offset + 0]);
            if (M_Byte2(qword0) == ATA_DEVICE_STATS_LOG_SSD && M_Word0(qword0) >= ATA_DEV_STATS_VERSION_1)//validating we got the right page
            {
                //percent used endurance
                uint64_t percentUsed = M_BytesTo8ByteValue(idDataLog[offset + 15], idDataLog[offset + 14], idDataLog[offset + 13], idDataLog[offset + 12], idDataLog[offset + 11], idDataLog[offset + 10], idDataLog[offset + 9], idDataLog[offset + 8]);
                if (percentUsed & ATA_DEV_STATS_STATISTIC_SUPPORTED_BIT && percentUsed & BIT62)
                {
                    driveInfo->percentEnduranceUsed = M_Byte0(percentUsed);
                }
            }
        }
        //offset = ATA_LOG_PAGE_LEN_BYTES * ATA_DEVICE_STATS_LOG_ZONED_DEVICE;
        //if (zonedDeviceStatistics && (offset + ATA_LOG_PAGE_LEN_BYTES) <= dataLength)
        //{
        //    uint64_t qword0 = M_BytesTo8ByteValue(idDataLog[offset + 7], idDataLog[offset + 6], idDataLog[offset + 5], idDataLog[offset + 4], idDataLog[offset + 3], idDataLog[offset + 2], idDataLog[offset + 1], idDataLog[offset + 0]);
        //    if (M_Byte2(qword0) == ATA_DEVICE_STATS_LOG_ZONED_DEVICE && M_Word0(qword0) >= ATA_DEV_STATS_VERSION_1)//validating we got the right page
        //    {
        //    }
        //}
    }
    return ret;
}

static eReturnValues get_ATA_Drive_Info_From_SMART_Data(ptrDriveInformationSAS_SATA driveInfo, ptrIdDataCapabilitiesForDriveInfo ataCapabilities,uint8_t *smartData, uint32_t dataLength)
{
    eReturnValues ret = BAD_PARAMETER;
    if (smartData && dataLength >= LEGACY_DRIVE_SEC_SIZE)
    {
        //get long DST time
        driveInfo->longDSTTimeMinutes = smartData[373];
        if (driveInfo->longDSTTimeMinutes == UINT8_MAX)
        {
            driveInfo->longDSTTimeMinutes = M_BytesTo2ByteValue(smartData[376], smartData[375]);
        }
        //read temperature (194), poh (9) for all, then read 241, 242, and 231 for Seagate only
        ataSMARTAttribute currentAttribute;
        uint16_t smartIter = 0;
        if (ataCapabilities->seagateFamily == SEAGATE)
        {
            //check IDD and Reman support
            bool iddSupported = false;
            bool remanSupported = false;
            if (smartData[0x1EE] & BIT0)
            {
                iddSupported = true;
            }
            if (smartData[0x1EE] & BIT1)
            {
                iddSupported = true;
            }
            if (smartData[0x1EE] & BIT2)
            {
                iddSupported = true;
            }
            if (smartData[0x1EE] & BIT3)
            {
                remanSupported = true;
            }

            //set features supported
            if (iddSupported)
            {
                add_Feature_To_Supported_List(driveInfo->featuresSupported, &driveInfo->numberOfFeaturesSupported, "Seagate In Drive Diagnostics (IDD)");
            }
            if (remanSupported)
            {
                add_Feature_To_Supported_List(driveInfo->featuresSupported, &driveInfo->numberOfFeaturesSupported, "Seagate Remanufacture");
            }
        }
        //first get the SMART attributes that we care about
        for (smartIter = 2; smartIter < 362; smartIter += 12)
        {
            currentAttribute.attributeNumber = smartData[smartIter + 0];
            currentAttribute.status = M_BytesTo2ByteValue(smartData[smartIter + 2], smartData[smartIter + 1]);
            currentAttribute.nominal = smartData[smartIter + 3];
            currentAttribute.worstEver = smartData[smartIter + 4];
            currentAttribute.rawData[0] = smartData[smartIter + 5];
            currentAttribute.rawData[1] = smartData[smartIter + 6];
            currentAttribute.rawData[2] = smartData[smartIter + 7];
            currentAttribute.rawData[3] = smartData[smartIter + 8];
            currentAttribute.rawData[4] = smartData[smartIter + 9];
            currentAttribute.rawData[5] = smartData[smartIter + 10];
            currentAttribute.rawData[6] = smartData[smartIter + 11];
            switch (currentAttribute.attributeNumber)
            {
            case 9: //POH (This attribute seems shared between vendors)
            {
                uint32_t millisecondsSinceIncrement = M_BytesTo4ByteValue(0, currentAttribute.rawData[6], currentAttribute.rawData[5], currentAttribute.rawData[4]);
                uint32_t powerOnMinutes = M_BytesTo4ByteValue(currentAttribute.rawData[3], currentAttribute.rawData[2], currentAttribute.rawData[1], currentAttribute.rawData[0]) * 60;
                powerOnMinutes += (millisecondsSinceIncrement / 60000);//convert the milliseconds to minutes, then add that to the amount of time we already know
                if (driveInfo->powerOnMinutes < powerOnMinutes)
                {
                    driveInfo->powerOnMinutes = powerOnMinutes;
                }
                driveInfo->powerOnMinutesValid = true;
            }
            break;
            case 194:
                //Temperature (This attribute seems shared between vendors)
                //NOTE: Not all vendors report this the same way!
                //Will need to handle variations on a case by case basis.
                if (!driveInfo->temperatureData.temperatureDataValid)
                {
                    driveInfo->temperatureData.temperatureDataValid = true;
                    driveInfo->temperatureData.currentTemperature = C_CAST(int16_t, M_BytesTo2ByteValue(currentAttribute.rawData[1], currentAttribute.rawData[0]));
                }
                /* TODO: This can be improved with better filters/interpretations defined per vendor to read this attribute. */
                if (ataCapabilities->seagateFamily != MAXTOR)
                {
                    if (ataCapabilities->seagateFamily == SEAGATE_VENDOR_K)
                    {
                        if (!driveInfo->temperatureData.highestValid && currentAttribute.worstEver != ATA_SMART_ATTRIBUTE_WORST_COMMON_START)//Filter out 253 as that is an unreasonable measurement, and more likely corresponds to an unreported or unsupported value
                        {
                            driveInfo->temperatureData.highestTemperature = C_CAST(int16_t, currentAttribute.worstEver);//or raw 5:4
                            driveInfo->temperatureData.highestValid = true;
                        }
                    }
                    else if (ataCapabilities->seagateFamily == SEAGATE_VENDOR_D || ataCapabilities->seagateFamily == SEAGATE_VENDOR_E)
                    {
                        if (!driveInfo->temperatureData.highestValid && currentAttribute.worstEver != ATA_SMART_ATTRIBUTE_WORST_COMMON_START)//Filter out 253 as that is an unreasonable measurement, and more likely corresponds to an unreported or unsupported value
                        {
                            driveInfo->temperatureData.highestTemperature = C_CAST(int16_t, M_BytesTo2ByteValue(currentAttribute.rawData[3], currentAttribute.rawData[2]));
                            driveInfo->temperatureData.highestValid = true;
                        }
                    }
                    else
                    {
                        if (!driveInfo->temperatureData.lowestValid && C_CAST(int16_t, M_BytesTo2ByteValue(currentAttribute.rawData[5], currentAttribute.rawData[4])) <= driveInfo->temperatureData.currentTemperature)
                        {
                            driveInfo->temperatureData.lowestTemperature = C_CAST(int16_t, M_BytesTo2ByteValue(currentAttribute.rawData[5], currentAttribute.rawData[4]));
                            driveInfo->temperatureData.lowestValid = true;
                        }
                        if (!driveInfo->temperatureData.highestValid && currentAttribute.worstEver != ATA_SMART_ATTRIBUTE_WORST_COMMON_START)//Filter out 253 as that is an unreasonable measurement, and more likely corresponds to an unreported or unsupported value
                        {
                            driveInfo->temperatureData.highestTemperature = C_CAST(int16_t, currentAttribute.worstEver);
                            driveInfo->temperatureData.highestValid = true;
                        }
                    }
                }
                break;
            case 231: //SSD Endurance
                if ((ataCapabilities->seagateFamily == SEAGATE || ataCapabilities->seagateFamily == SEAGATE_VENDOR_D || ataCapabilities->seagateFamily == SEAGATE_VENDOR_E || ataCapabilities->seagateFamily == SEAGATE_VENDOR_C || ataCapabilities->seagateFamily == SEAGATE_VENDOR_F || ataCapabilities->seagateFamily == SEAGATE_VENDOR_G || ataCapabilities->seagateFamily == SEAGATE_VENDOR_K) && driveInfo->percentEnduranceUsed < 0)
                {
                    // SCSI was implemented first, and it returns a value where 0 means 100% spares left, ATA is the opposite,
                    // so we need to subtract our number from 100
                    // On SATA drives below, we had firmware reporting in the range of 0-255 instead of 0-100. Lets check for them and normalize the value so it can be reported
                    // Note that this is only for some firmwares...we should figure out a better way to do this
                    // Here's the list of the affected SATA model numbers:
                    // ST100FM0022 (SED - rare)
                    // ST100FM0012 (SED - rare)
                    // ST200FM0012 (SED - rare)
                    // ST400FM0012 (SED - rare)
                    // ST100FM0062
                    // ST200FM0052
                    // ST400FM0052
                    if ((strcmp(driveInfo->modelNumber, "ST100FM0022") == 0 ||
                        strcmp(driveInfo->modelNumber, "ST100FM0012") == 0 ||
                        strcmp(driveInfo->modelNumber, "ST200FM0012") == 0 ||
                        strcmp(driveInfo->modelNumber, "ST400FM0012") == 0 ||
                        strcmp(driveInfo->modelNumber, "ST100FM0062") == 0 ||
                        strcmp(driveInfo->modelNumber, "ST200FM0052") == 0 ||
                        strcmp(driveInfo->modelNumber, "ST400FM0052") == 0) &&
                        strcmp(driveInfo->firmwareRevision, "0004") == 0)
                    {
                        driveInfo->percentEnduranceUsed = 100 - ((C_CAST(uint32_t, currentAttribute.nominal) * 100) / 255);
                    }
                    else
                    {
                        driveInfo->percentEnduranceUsed = 100 - currentAttribute.nominal;
                    }
                }
                break;
            case 233: //Lifetime Write to Flash (SSD)
                if (ataCapabilities->seagateFamily == SEAGATE_VENDOR_G || ataCapabilities->seagateFamily == SEAGATE_VENDOR_F)
                {
                    driveInfo->totalWritesToFlash = M_BytesTo8ByteValue(0, currentAttribute.rawData[6], currentAttribute.rawData[5], currentAttribute.rawData[4], currentAttribute.rawData[3], currentAttribute.rawData[2], currentAttribute.rawData[1], currentAttribute.rawData[0]);
                    //convert this to match what we're doing below since this is likely also in GiB written (BUT IDK BECAUSE IT ISN'T IN THE SMART SPEC!)
                    driveInfo->totalWritesToFlash = (driveInfo->totalWritesToFlash * 1024 * 1024 * 1024) / driveInfo->logicalSectorSize;
                }
                else if (ataCapabilities->seagateFamily == SEAGATE_VENDOR_K)
                {
                    driveInfo->totalWritesToFlash = M_BytesTo8ByteValue(0, currentAttribute.rawData[6], currentAttribute.rawData[5], currentAttribute.rawData[4], currentAttribute.rawData[3], currentAttribute.rawData[2], currentAttribute.rawData[1], currentAttribute.rawData[0]);
                    //units are in 32MB
                    driveInfo->totalWritesToFlash = (driveInfo->totalWritesToFlash * 1000 * 1000 * 32) / driveInfo->logicalSectorSize;
                }
                break;
            case 234: //Lifetime Write to Flash (SSD)
                if (ataCapabilities->seagateFamily == SEAGATE || (ataCapabilities->seagateFamily == SEAGATE_VENDOR_D || ataCapabilities->seagateFamily == SEAGATE_VENDOR_E || ataCapabilities->seagateFamily == SEAGATE_VENDOR_B))
                {
                    driveInfo->totalWritesToFlash = M_BytesTo8ByteValue(0, currentAttribute.rawData[6], currentAttribute.rawData[5], currentAttribute.rawData[4], currentAttribute.rawData[3], currentAttribute.rawData[2], currentAttribute.rawData[1], currentAttribute.rawData[0]);
                    //convert this to match what we're doing below since this is likely also in GiB written (BUT IDK BECAUSE IT ISN'T IN THE SMART SPEC!)
                    driveInfo->totalWritesToFlash = (driveInfo->totalWritesToFlash * 1024 * 1024 * 1024) / driveInfo->logicalSectorSize;
                }
                break;
            case 241: //Total Bytes written (SSD) Total LBAs written (HDD)
                if ((ataCapabilities->seagateFamily == SEAGATE || ataCapabilities->seagateFamily == SEAGATE_VENDOR_D || ataCapabilities->seagateFamily == SEAGATE_VENDOR_E || ataCapabilities->seagateFamily == SEAGATE_VENDOR_B || ataCapabilities->seagateFamily == SEAGATE_VENDOR_F || ataCapabilities->seagateFamily == SEAGATE_VENDOR_G || ataCapabilities->seagateFamily == SEAGATE_VENDOR_K) && driveInfo->totalLBAsWritten == 0)
                {
                    driveInfo->totalLBAsWritten = M_BytesTo8ByteValue(0, currentAttribute.rawData[6], currentAttribute.rawData[5], currentAttribute.rawData[4], currentAttribute.rawData[3], currentAttribute.rawData[2], currentAttribute.rawData[1], currentAttribute.rawData[0]);
                    if (ataCapabilities->seagateFamily == SEAGATE_VENDOR_D || ataCapabilities->seagateFamily == SEAGATE_VENDOR_E || ataCapabilities->seagateFamily == SEAGATE_VENDOR_B || ataCapabilities->seagateFamily == SEAGATE_VENDOR_F)
                    {
                        //some Seagate SSD's report this as GiB written, so convert to LBAs
                        driveInfo->totalLBAsWritten = (driveInfo->totalLBAsWritten * 1024 * 1024 * 1024) / driveInfo->logicalSectorSize;
                    }
                    else if (ataCapabilities->seagateFamily == SEAGATE_VENDOR_K)
                    {
                        //units are 32MB, so convert to LBAs
                        driveInfo->totalLBAsWritten = (driveInfo->totalLBAsWritten * 1000 * 1000 * 32) / driveInfo->logicalSectorSize;
                    }
                }
                break;
            case 242: //Total Bytes read (SSD) Total LBAs read (HDD)
                if ((ataCapabilities->seagateFamily == SEAGATE || ataCapabilities->seagateFamily == SEAGATE_VENDOR_D || ataCapabilities->seagateFamily == SEAGATE_VENDOR_E || ataCapabilities->seagateFamily == SEAGATE_VENDOR_B || ataCapabilities->seagateFamily == SEAGATE_VENDOR_F || ataCapabilities->seagateFamily == SEAGATE_VENDOR_G || ataCapabilities->seagateFamily == SEAGATE_VENDOR_K) && driveInfo->totalLBAsRead == 0)
                {
                    driveInfo->totalLBAsRead = M_BytesTo8ByteValue(0, currentAttribute.rawData[6], currentAttribute.rawData[5], currentAttribute.rawData[4], currentAttribute.rawData[3], currentAttribute.rawData[2], currentAttribute.rawData[1], currentAttribute.rawData[0]);
                    if (ataCapabilities->seagateFamily == SEAGATE_VENDOR_D || ataCapabilities->seagateFamily == SEAGATE_VENDOR_E || ataCapabilities->seagateFamily == SEAGATE_VENDOR_B || ataCapabilities->seagateFamily == SEAGATE_VENDOR_F)
                    {
                        //some Seagate SSD's report this as GiB read, so convert to LBAs
                        driveInfo->totalLBAsRead = (driveInfo->totalLBAsRead * 1024 * 1024 * 1024) / driveInfo->logicalSectorSize;
                    }
                    else if (ataCapabilities->seagateFamily == SEAGATE_VENDOR_K)
                    {
                        //units are 32MB, so convert to LBAs
                        driveInfo->totalLBAsRead = (driveInfo->totalLBAsRead * 1000 * 1000 * 32) / driveInfo->logicalSectorSize;
                    }
                }
                break;
            default:
                break;
            }
        }
    }
    return ret;
}

static eReturnValues get_Security_Features_From_Security_Protocol(tDevice *device, securityProtocolInfo* info, uint8_t *securityProtocolList, uint32_t dataLength)
{
    eReturnValues ret = BAD_PARAMETER;
    if (device && info && securityProtocolList && dataLength > 8)
    {
        uint16_t length = M_BytesTo2ByteValue(securityProtocolList[6], securityProtocolList[7]);
        uint32_t bufIter = 8;
        //Check endianness. ATA is SUPPOSED to report little endian and SCSI/NVMe report big endian, but that doesn't seem like it's always followed.
        //      So to check this, check the bytes after the length are all zero for either interpretation. 
        //      One will be right, usually the shorter of the lengths.
        //      This is necessary for any security protocol 0 (information) buffers as I've seen all kinds of weird combinations - TJE
        //For now, just take the shorter value of the two lengths given....seems true for everything I've encountered so far.
        uint16_t swappedLength = M_BytesTo2ByteValue(securityProtocolList[7], securityProtocolList[6]);
        if (swappedLength < length)
        {
            length = swappedLength;
        }
        if (length > 0)
        {
            info->securityProtocolInfoValid = true;
        }
        for (; (bufIter - 8) < length && (bufIter - 8) < dataLength; bufIter++)
        {
            switch (securityProtocolList[bufIter])
            {
            case SECURITY_PROTOCOL_INFORMATION:
                //TODO: Read FIPS compliance descriptor (spspecific = 0002h)
                //TODO: read certificates???
                break;
            case SECURITY_PROTOCOL_TCG_1:
            case SECURITY_PROTOCOL_TCG_2:
            case SECURITY_PROTOCOL_TCG_3:
            case SECURITY_PROTOCOL_TCG_4:
            case SECURITY_PROTOCOL_TCG_5:
            case SECURITY_PROTOCOL_TCG_6:
                //TODO: Level 0 discovery to look up which TCG feature(s) are being implemented.
                //      This will also be better for determining encryption support.
                info->tcg = true;
                break;
            case SECURITY_PROTOCOL_CbCS:
                info->cbcs = true;
                break;
            case SECURITY_PROTOCOL_TAPE_DATA_ENCRYPTION:
                info->tapeEncryption = true;
                break;
            case SECURITY_PROTOCOL_DATA_ENCRYPTION_CONFIGURATION:
                info->dataEncryptionConfig = true;
                break;
            case SECURITY_PROTOCOL_SA_CREATION_CAPABILITIES:
                info->saCreationCapabilities = true;
                break;
            case SECURITY_PROTOCOL_IKE_V2_SCSI:
                info->ikev2scsi = true;
                break;
            case SECURITY_PROTOCOL_SD_ASSOCIATION:
                info->sdAssociation = true;
                break;
            case SECURITY_PROTOCOL_DMTF_SECURITY_PROTOCOL_AND_DATA_MODEL:
                info->dmtfSecurity = true;
                break;
            case SECURITY_PROTOCOL_NVM_EXPRESS_RESERVED:
                info->nvmeReserved = true;
                break;
            case SECURITY_PROTOCOL_NVM_EXPRESS:
                info->nvme = true;
                break;
            case SECURITY_PROTOCOL_SCSA:
                info->scsa = true;
                break;
            case SECURITY_PROTOCOL_JEDEC_UFS:
                info->jedecUFS = true;
                break;
            case SECURITY_PROTOCOL_SDcard_TRUSTEDFLASH_SECURITY:
                info->sdTrustedFlash = true;
                break;
            case SECURITY_PROTOCOL_IEEE_1667:
                info->ieee1667 = true;
                break;
            case SECURITY_PROTOCOL_ATA_DEVICE_SERVER_PASSWORD:
            {
                info->ataDeviceServer = true;
                //read the data from this page to set ATA security information
                DECLARE_ZERO_INIT_ARRAY(uint8_t, ataSecurityInfo, 16);
                if (SUCCESS == scsi_SecurityProtocol_In(device, SECURITY_PROTOCOL_ATA_DEVICE_SERVER_PASSWORD, 0, false, 16, ataSecurityInfo))
                {

                    info->ataSecurityInfo.securityEraseUnitTimeMinutes = M_BytesTo2ByteValue(ataSecurityInfo[2], ataSecurityInfo[3]);
                    info->ataSecurityInfo.enhancedSecurityEraseUnitTimeMinutes = M_BytesTo2ByteValue(ataSecurityInfo[4], ataSecurityInfo[5]);
                    info->ataSecurityInfo.masterPasswordIdentifier = M_BytesTo2ByteValue(ataSecurityInfo[6], ataSecurityInfo[7]);
                    //check the bits now
                    if (ataSecurityInfo[8] & BIT0)
                    {
                        info->ataSecurityInfo.masterPasswordCapability = true;
                    }
                    if (ataSecurityInfo[9] & BIT5)
                    {
                        info->ataSecurityInfo.enhancedEraseSupported = true;
                    }
                    if (ataSecurityInfo[9] & BIT4)
                    {
                        info->ataSecurityInfo.securityCountExpired = true;
                    }
                    if (ataSecurityInfo[9] & BIT3)
                    {
                        info->ataSecurityInfo.securityFrozen = true;
                    }
                    if (ataSecurityInfo[9] & BIT2)
                    {
                        info->ataSecurityInfo.securityLocked = true;
                    }
                    if (ataSecurityInfo[9] & BIT1)
                    {
                        info->ataSecurityInfo.securityEnabled = true;
                    }
                    if (ataSecurityInfo[9] & BIT0)
                    {
                        info->ataSecurityInfo.securitySupported = true;
                    }
                }
            }
            break;
            default:
                break;
            }
        }
    }
    return ret;
}

eReturnValues get_ATA_Drive_Information(tDevice* device, ptrDriveInformationSAS_SATA driveInfo)
{
    eReturnValues ret = SUCCESS;
    bool smartStatusFromSCTStatusLog = false;
    idDataCapabilitiesForDriveInfo ataCap;
    memset(&ataCap, 0, sizeof(idDataCapabilitiesForDriveInfo));
    if (!driveInfo)
    {
        return BAD_PARAMETER;
    }
    memset(driveInfo, 0, sizeof(driveInformationSAS_SATA));
    memcpy(&driveInfo->adapterInformation, &device->drive_info.adapter_info, sizeof(adapterInfo));
    ataCap.seagateFamily = is_Seagate_Family(device);
    if (SUCCESS == ata_Identify(device, C_CAST(uint8_t*, &device->drive_info.IdentifyData.ata), LEGACY_DRIVE_SEC_SIZE))
    {
        get_ATA_Drive_Info_From_Identify(driveInfo, &ataCap, C_CAST(uint8_t*, &device->drive_info.IdentifyData.ata), LEGACY_DRIVE_SEC_SIZE);
    }
    driveInfo->percentEnduranceUsed = -1;//start with this to filter out this value later if necessary

    //Read Log data
    uint32_t logBufferSize = LEGACY_DRIVE_SEC_SIZE;
    uint8_t* logBuffer = C_CAST(uint8_t*, safe_calloc_aligned(logBufferSize, sizeof(uint8_t), device->os_info.minimumAlignment));
    if (!logBuffer)
    {
        return MEMORY_FAILURE;
    }

    bool gotLogDirectory = false;
    if (ataCap.gplSupported && SUCCESS == send_ATA_Read_Log_Ext_Cmd(device, ATA_LOG_DIRECTORY, 0, logBuffer, LEGACY_DRIVE_SEC_SIZE, 0))
    {
        gotLogDirectory = true;
    }
    else if (ataCap.smartErrorLoggingSupported && SUCCESS == ata_SMART_Read_Log(device, ATA_LOG_DIRECTORY, logBuffer, LEGACY_DRIVE_SEC_SIZE))
    {
        gotLogDirectory = true;
    }

    if (gotLogDirectory || ataCap.smartErrorLoggingSupported)
    {
        //check for log sizes we are interested in
        uint32_t devStatsSize = 0, idDataLogSize = 0, hybridInfoSize = 0, smartSelfTest = 0, extSelfTest = 0, hostlogging = 0, sctStatus = 0, concurrentRangesSize = 0, farmLogSize = 0;
        if (gotLogDirectory)
        {
            devStatsSize = M_BytesTo2ByteValue(logBuffer[(ATA_LOG_DEVICE_STATISTICS * 2) + 1], logBuffer[(ATA_LOG_DEVICE_STATISTICS * 2)]) * LEGACY_DRIVE_SEC_SIZE;
            idDataLogSize = M_BytesTo2ByteValue(logBuffer[(ATA_LOG_IDENTIFY_DEVICE_DATA * 2) + 1], logBuffer[(ATA_LOG_IDENTIFY_DEVICE_DATA * 2)]) * LEGACY_DRIVE_SEC_SIZE;
            hybridInfoSize = M_BytesTo2ByteValue(logBuffer[(ATA_LOG_HYBRID_INFORMATION * 2) + 1], logBuffer[(ATA_LOG_HYBRID_INFORMATION * 2)]) * LEGACY_DRIVE_SEC_SIZE;
            smartSelfTest = M_BytesTo2ByteValue(logBuffer[(ATA_LOG_SMART_SELF_TEST_LOG * 2) + 1], logBuffer[(ATA_LOG_SMART_SELF_TEST_LOG * 2)]) * LEGACY_DRIVE_SEC_SIZE;
            extSelfTest = M_BytesTo2ByteValue(logBuffer[(ATA_LOG_EXTENDED_SMART_SELF_TEST_LOG * 2) + 1], logBuffer[(ATA_LOG_EXTENDED_SMART_SELF_TEST_LOG * 2)]) * LEGACY_DRIVE_SEC_SIZE;
            sctStatus = M_BytesTo2ByteValue(logBuffer[(ATA_SCT_COMMAND_STATUS * 2) + 1], logBuffer[(ATA_SCT_COMMAND_STATUS * 2)]) * LEGACY_DRIVE_SEC_SIZE;
            hostlogging = M_BytesTo2ByteValue(logBuffer[(ATA_LOG_HOST_SPECIFIC_80H * 2) + 1], logBuffer[(ATA_LOG_HOST_SPECIFIC_80H * 2)]) * LEGACY_DRIVE_SEC_SIZE;
            concurrentRangesSize = M_BytesTo2ByteValue(logBuffer[(ATA_LOG_CONCURRENT_POSITIONING_RANGES * 2) + 1], logBuffer[(ATA_LOG_CONCURRENT_POSITIONING_RANGES * 2)]) * LEGACY_DRIVE_SEC_SIZE;
            farmLogSize = M_BytesTo2ByteValue(logBuffer[(SEAGATE_ATA_LOG_FIELD_ACCESSIBLE_RELIABILITY_METRICS * 2) + 1], logBuffer[(SEAGATE_ATA_LOG_FIELD_ACCESSIBLE_RELIABILITY_METRICS * 2)]) * LEGACY_DRIVE_SEC_SIZE;
        }
        else
        {
            //This is a case for old drives. They will only support single sector logs when this is set like this
            if (is_Self_Test_Supported(device))
            {
                smartSelfTest = ATA_LOG_PAGE_LEN_BYTES;
            }
            //The only other logs to look at in here are SMART error log (summary and comprehensive) and selective self test
            // as these are the only other single sector logs that will show up on old drives like these
        }
        if (hostlogging == (UINT32_C(16) * ATA_LOG_PAGE_LEN_BYTES))
        {
            add_Feature_To_Supported_List(driveInfo->featuresSupported, &driveInfo->numberOfFeaturesSupported, "Host Logging");
        }

        if (idDataLogSize > 0)
        {
            uint8_t* idDataLog = C_CAST(uint8_t*, safe_calloc_aligned(idDataLogSize, sizeof(uint8_t), device->os_info.minimumAlignment));
            if (idDataLog)
            {
                if (SUCCESS == get_ATA_Log(device, ATA_LOG_IDENTIFY_DEVICE_DATA, M_NULLPTR, M_NULLPTR, true, true, true, idDataLog, idDataLogSize, M_NULLPTR, 0, 0))
                {
                    //call function to fill in data from ID data log
                    get_ATA_Drive_Info_From_ID_Data_Log(driveInfo, &ataCap, idDataLog, idDataLogSize);
                }
            }
            safe_free_aligned(&idDataLog);
        }
        //read device statistics log (only some pages are needed)
        if (devStatsSize > 0)//can come from GPL or SMART
        {
            uint8_t* devStats = C_CAST(uint8_t*, safe_calloc_aligned(devStatsSize, sizeof(uint8_t), device->os_info.minimumAlignment));
            if (devStats)
            {
                if (SUCCESS == get_ATA_Log(device, ATA_LOG_DEVICE_STATISTICS, M_NULLPTR, M_NULLPTR, true, true, true, devStats, devStatsSize, M_NULLPTR, 0, 0))
                {
                    //call function to fill in data from ID data log
                    get_ATA_Drive_Info_From_Device_Statistics_Log(driveInfo, &ataCap, devStats, devStatsSize);
                }
            }
            safe_free_aligned(&devStats);
        }
        if (ataCap.gplSupported && hybridInfoSize > 0)//GPL only. Page is also only a size of 1 512B block
        {
            if (SUCCESS == send_ATA_Read_Log_Ext_Cmd(device, ATA_LOG_HYBRID_INFORMATION, 0, logBuffer, LEGACY_DRIVE_SEC_SIZE, 0))
            {
                driveInfo->hybridNANDSize = M_BytesTo8ByteValue(logBuffer[23], logBuffer[22], logBuffer[21], logBuffer[20], logBuffer[19], logBuffer[18], logBuffer[17], logBuffer[16]) * driveInfo->logicalSectorSize;
            }
        }
        if (extSelfTest > 0 || smartSelfTest > 0)
        {
            dstLogEntries dstEntries;
            memset(&dstEntries, 0, sizeof(dstLogEntries));
            if (SUCCESS == get_DST_Log_Entries(device, &dstEntries))
            {
                //setup dst info from most recent dst log entry.
                if (dstEntries.numberOfEntries > 0)
                {
                    driveInfo->dstInfo.informationValid = true;
                    driveInfo->dstInfo.powerOnHours = dstEntries.dstEntry[0].powerOnHours;
                    driveInfo->dstInfo.resultOrStatus = dstEntries.dstEntry[0].selfTestExecutionStatus;
                    driveInfo->dstInfo.testNumber = dstEntries.dstEntry[0].selfTestRun;
                    driveInfo->dstInfo.errorLBA = dstEntries.dstEntry[0].lbaOfFailure;
                }
                else
                {
                    //set to all 0's and error LBA to all Fs
                    driveInfo->dstInfo.informationValid = true;
                    driveInfo->dstInfo.powerOnHours = 0;
                    driveInfo->dstInfo.resultOrStatus = 0;
                    driveInfo->dstInfo.testNumber = 0;
                    driveInfo->dstInfo.errorLBA = UINT64_MAX;
                }
            }
        }
        if (ataCap.sctSupported && sctStatus > 0)//GPL or SMART
        {
            memset(logBuffer, 0, LEGACY_DRIVE_SEC_SIZE);
            //Read the SCT status log
            if (SUCCESS == get_ATA_Log(device, ATA_SCT_COMMAND_STATUS, M_NULLPTR, M_NULLPTR, true, true, true, logBuffer, ATA_LOG_PAGE_LEN_BYTES, M_NULLPTR, 0, 0))
            {
                uint16_t sctFormatVersion = M_BytesTo2ByteValue(logBuffer[1], logBuffer[0]);
                if (sctFormatVersion > 1)//cannot find spec for revision 1 of this log, but we'll keep this safe until I find it with this check
                {
                    if (!driveInfo->temperatureData.temperatureDataValid && logBuffer[200] != 0x80)
                    {
                        driveInfo->temperatureData.temperatureDataValid = true;
                        driveInfo->temperatureData.currentTemperature = C_CAST(int16_t, logBuffer[200]);
                    }
                    if (!driveInfo->temperatureData.highestValid && logBuffer[204] != 0x80)
                    {
                        driveInfo->temperatureData.highestTemperature = C_CAST(int16_t, logBuffer[204]);
                        driveInfo->temperatureData.highestValid = true;
                    }
                }
                if (sctFormatVersion > 2)
                {
                    //version 3 and higher report current, min, and max temperatures
                    //reading life min and max temperatures
                    if (!driveInfo->temperatureData.lowestValid && logBuffer[203] != 0x80)
                    {
                        driveInfo->temperatureData.lowestTemperature = C_CAST(int16_t, logBuffer[203]);
                        driveInfo->temperatureData.lowestValid = true;
                    }
                    uint16_t smartStatus = M_BytesTo2ByteValue(logBuffer[215], logBuffer[214]);
                    //SMART status
                    switch (smartStatus)
                    {
                    case 0xC24F:
                        smartStatusFromSCTStatusLog = true;
                        driveInfo->smartStatus = 0;
                        break;
                    case 0x2CF4:
                        smartStatusFromSCTStatusLog = true;
                        driveInfo->smartStatus = 1;
                        break;
                    default:
                        driveInfo->smartStatus = 2;
                        break;
                    }
                }
            }
        }
        if (ataCap.gplSupported && concurrentRangesSize)
        {
            memset(logBuffer, 0, logBufferSize);
            //NOTE: Only reading first 512B since this has the counter we need. Max log size is 1024 in ACS5 - TJE
            if (SUCCESS == send_ATA_Read_Log_Ext_Cmd(device, ATA_LOG_CONCURRENT_POSITIONING_RANGES, 0, logBuffer, LEGACY_DRIVE_SEC_SIZE, 0))
            {
                driveInfo->concurrentPositioningRanges = logBuffer[0];
            }
        }
        if (ataCap.gplSupported && farmLogSize)
        {
            uint8_t* farmData = C_CAST(uint8_t*, safe_calloc_aligned(16384, sizeof(uint8_t), device->os_info.minimumAlignment));
            if (farmData)
            {
                //read "page 0" or first 4KB for header/top level information and verify this is in fact the FARM log
                if (SUCCESS == send_ATA_Read_Log_Ext_Cmd(device, SEAGATE_ATA_LOG_FIELD_ACCESSIBLE_RELIABILITY_METRICS, 0, farmData, 16384, 0))
                {
                    uint64_t farmSignature = M_BytesTo8ByteValue(farmData[7], farmData[6], farmData[5], farmData[4], farmData[3], farmData[2], farmData[1], farmData[0]);
                    if (farmSignature & BIT63 && farmSignature & BIT62 && (farmSignature & UINT64_C(0x00FFFFFFFFFFFF)) == SEAGATE_FARM_LOG_SIGNATURE)
                    {
                        add_Feature_To_Supported_List(driveInfo->featuresSupported, &driveInfo->numberOfFeaturesSupported, "Field Accessible Reliability Metrics (FARM)");
                        //Now read the next page to get the DOM (and possibly other useful data)
                        if (SUCCESS == send_ATA_Read_Log_Ext_Cmd(device, SEAGATE_ATA_LOG_FIELD_ACCESSIBLE_RELIABILITY_METRICS, (UINT16_C(16384) / LEGACY_DRIVE_SEC_SIZE), farmData, 16384, 0))
                        {
                            //verify page number (first 8 bytes)
                            uint64_t farmPage1ID = M_BytesTo8ByteValue(farmData[7], farmData[6], farmData[5], farmData[4], farmData[3], farmData[2], farmData[1], farmData[0]);
                            if (farmSignature & BIT63 && farmSignature & BIT62 && (farmPage1ID & UINT64_C(0x00FFFFFFFFFFFF)) == 1)
                            {
                                uint64_t dateOfManufactureQWord = M_BytesTo8ByteValue(farmData[367], farmData[366], farmData[365], farmData[364], farmData[363], farmData[362], farmData[361], farmData[360]);
                                if (dateOfManufactureQWord & BIT63 && dateOfManufactureQWord & BIT62)//supported and valid info
                                {
                                    char domWeekStr[3] = { C_CAST(char, farmData[362]), C_CAST(char, farmData[363]), 0 };
                                    char domYearStr[3] = { C_CAST(char, farmData[360]), C_CAST(char, farmData[361]), 0 };
                                    driveInfo->dateOfManufactureValid = true;
                                    if (!get_And_Validate_Integer_Input_Uint8(domWeekStr, NULL, ALLOW_UNIT_NONE, &driveInfo->manufactureWeek))
                                    {
                                        driveInfo->dateOfManufactureValid = false;
                                    }
                                    if (!get_And_Validate_Integer_Input_Uint16(domYearStr, NULL, ALLOW_UNIT_NONE, &driveInfo->manufactureYear))
                                    {
                                        driveInfo->dateOfManufactureValid = false;
                                    }
                                    else
                                    {
                                        //year is 2 digits, but this log was not in existance until after 2018 or so
                                        driveInfo->manufactureYear += UINT16_C(2000);
                                    }
                                }
                            }
                        }
                    }
                }
                safe_free_aligned(&farmData);
            }
        }
    }
    safe_free_aligned(&logBuffer);

        DECLARE_ZERO_INIT_ARRAY(uint8_t, smartData, LEGACY_DRIVE_SEC_SIZE);
    if (SUCCESS == ata_SMART_Read_Data(device, smartData, LEGACY_DRIVE_SEC_SIZE))
    {
        get_ATA_Drive_Info_From_SMART_Data(driveInfo, &ataCap, smartData, LEGACY_DRIVE_SEC_SIZE);
    }
    //set total bytes read/written
    driveInfo->totalBytesRead = driveInfo->totalLBAsRead * driveInfo->logicalSectorSize;
    driveInfo->totalBytesWritten = driveInfo->totalLBAsWritten * driveInfo->logicalSectorSize;

    //get security protocol info
    if (ataCap.tcgSupported)
    {
        //TCG - SED drive (need to test a trusted command to see if it is being blocked or not)
        if (SUCCESS != ata_Trusted_Non_Data(device, 0, true, 0))
        {
            driveInfo->trustedCommandsBeingBlocked = true;
            if (ataCap.tcgSupported)
            {
                add_Feature_To_Supported_List(driveInfo->featuresSupported, &driveInfo->numberOfFeaturesSupported, "TCG");
            }
            if (ataCap.ieee1667Supported)
            {
                add_Feature_To_Supported_List(driveInfo->featuresSupported, &driveInfo->numberOfFeaturesSupported, "IEEE 1667");
            }
        }
        else
        {
            //Read supported security protocol list
            uint8_t* protocolList = C_CAST(uint8_t*, safe_calloc_aligned(LEGACY_DRIVE_SEC_SIZE, sizeof(uint8_t), device->os_info.minimumAlignment));
            if (protocolList)
            {
                if (SUCCESS == ata_Trusted_Receive(device, device->drive_info.ata_Options.dmaSupported, 0, 0, protocolList, LEGACY_DRIVE_SEC_SIZE))
                {
                    if (SUCCESS == get_Security_Features_From_Security_Protocol(device, &driveInfo->securityInfo, protocolList, LEGACY_DRIVE_SEC_SIZE))
                    {
                        if (driveInfo->securityInfo.tcg)
                        {
                            driveInfo->encryptionSupport = ENCRYPTION_SELF_ENCRYPTING;
                            add_Feature_To_Supported_List(driveInfo->featuresSupported, &driveInfo->numberOfFeaturesSupported, "TCG");
                        }
                        if (driveInfo->securityInfo.scsa)
                        {
                            add_Feature_To_Supported_List(driveInfo->featuresSupported, &driveInfo->numberOfFeaturesSupported, "SCSA");
                        }
                        if (driveInfo->securityInfo.ieee1667)
                        {
                            add_Feature_To_Supported_List(driveInfo->featuresSupported, &driveInfo->numberOfFeaturesSupported, "IEEE 1667");
                        }
                    }
                }
                safe_free_aligned(&protocolList);
            }
        }

    }

    //get the native maxLBA
    ata_Get_Native_Max_LBA(device, &driveInfo->nativeMaxLBA);
    if (!smartStatusFromSCTStatusLog)
    {
        //SMART status
        switch (ata_SMART_Check(device, M_NULLPTR))
        {
        case SUCCESS:
            driveInfo->smartStatus = 0;
            break;
        case FAILURE:
            driveInfo->smartStatus = 1;
            break;
        default:
            driveInfo->smartStatus = 2;
            break;
        }
    }
    if (is_Seagate_Family(device) == SEAGATE)
    {
        driveInfo->lowCurrentSpinupValid = true;
        driveInfo->lowCurrentSpinupViaSCT = is_SCT_Low_Current_Spinup_Supported(device);
        driveInfo->lowCurrentSpinupEnabled = is_Low_Current_Spin_Up_Enabled(device, driveInfo->lowCurrentSpinupViaSCT);
    }
    return ret;
}

typedef struct _scsiIdentifyInfo
{
    uint8_t version;
    uint8_t peripheralQualifier;
    uint8_t peripheralDeviceType;
    bool ccs;//scsi1, but reporting according to CCS (response format 1)
    bool protectionSupported;
    bool protectionType1Supported;
    bool protectionType2Supported;
    bool protectionType3Supported;
    bool zoneDomainsOrRealms;
}scsiIdentifyInfo, *ptrSCSIIdentifyInfo;

static eReturnValues get_SCSI_Inquiry_Data(ptrDriveInformationSAS_SATA driveInfo, ptrSCSIIdentifyInfo scsiInfo, uint8_t* inquiryData, uint32_t dataLength)
{
    eReturnValues ret = SUCCESS;
    if (driveInfo && scsiInfo && inquiryData && dataLength >= INQ_RETURN_DATA_LENGTH_SCSI2)
    {
        //now parse the data
        scsiInfo->peripheralQualifier = M_GETBITRANGE(inquiryData[0], 7, 5);
        scsiInfo->peripheralDeviceType = M_GETBITRANGE(inquiryData[0], 4, 0);
        //Vendor ID
        memcpy(&driveInfo->vendorID, &inquiryData[8], INQ_DATA_T10_VENDOR_ID_LEN);
        //MN-product identification
        memcpy(driveInfo->modelNumber, &inquiryData[16], INQ_DATA_PRODUCT_ID_LEN);
        //FWRev
        memcpy(driveInfo->firmwareRevision, &inquiryData[32], INQ_DATA_PRODUCT_REV_LEN);
        //Version (SPC version device conforms to)
        scsiInfo->version = inquiryData[2];
        uint8_t responseFormat = M_GETBITRANGE(inquiryData[3], 3, 0);
        if (responseFormat == INQ_RESPONSE_FMT_CCS)
        {
            scsiInfo->ccs = true;
        }
        switch (scsiInfo->version)
        {
        case 0:
            /*add_Specification_To_Supported_List(driveInfo->specificationsSupported, &driveInfo->numberOfSpecificationsSupported, "Does not conform to a SCSI standard");

            break;*/
            //Note: Old SCSI/SPC standards may report multiple specifications supported in this byte
            //Codes 08-0Ch, 40-44h, 48-4Ch, & 88h-8Ch are skipped in there. These were valious ways to report ISO/ECMA/ANSI standard conformance seperately for the same SCSI-x specification.
            //New standards (SPC6 or whatever is next) may use these values and they should be used.
        case 0x81:
        case 0x01:
            add_Specification_To_Supported_List(driveInfo->specificationsSupported, &driveInfo->numberOfSpecificationsSupported, "SCSI");
            scsiInfo->version = 1;
            break;
        case 0x02:
        case 0x80:
        case 0x82:
            add_Specification_To_Supported_List(driveInfo->specificationsSupported, &driveInfo->numberOfSpecificationsSupported, "SCSI-2");
            scsiInfo->version = 2;
            break;
        case 0x83:
            add_Specification_To_Supported_List(driveInfo->specificationsSupported, &driveInfo->numberOfSpecificationsSupported, "SPC");
            add_Specification_To_Supported_List(driveInfo->specificationsSupported, &driveInfo->numberOfSpecificationsSupported, "SCSI-2");
            scsiInfo->version = 3;
            break;
        case 0x84:
            add_Specification_To_Supported_List(driveInfo->specificationsSupported, &driveInfo->numberOfSpecificationsSupported, "SPC-2");
            add_Specification_To_Supported_List(driveInfo->specificationsSupported, &driveInfo->numberOfSpecificationsSupported, "SCSI-2");
            scsiInfo->version = 4;
            break;
        case 0x03:
            add_Specification_To_Supported_List(driveInfo->specificationsSupported, &driveInfo->numberOfSpecificationsSupported, "SPC");
            break;
        case 0x04:
            add_Specification_To_Supported_List(driveInfo->specificationsSupported, &driveInfo->numberOfSpecificationsSupported, "SPC-2");
            break;
        case 0x05:
            add_Specification_To_Supported_List(driveInfo->specificationsSupported, &driveInfo->numberOfSpecificationsSupported, "SPC-3");
            break;
        case 0x06:
            add_Specification_To_Supported_List(driveInfo->specificationsSupported, &driveInfo->numberOfSpecificationsSupported, "SPC-4");
            break;
        case 0x07:
            add_Specification_To_Supported_List(driveInfo->specificationsSupported, &driveInfo->numberOfSpecificationsSupported, "SPC-5");
            break;
        default:
            break;
        }
        if (responseFormat == 1)
        {
            //response format of 1 means there is compliance with the Common Command Set specification, which is partial SCSI2 support.
            add_Specification_To_Supported_List(driveInfo->specificationsSupported, &driveInfo->numberOfSpecificationsSupported, "CCS");
        }
        scsiInfo->protectionSupported = inquiryData[5] & BIT0;
        if (scsiInfo->version >= 4 && (inquiryData[4] + 4) > 57)
        {
            //Version Descriptors 1-8 (SPC2 and up)
            uint16_t versionDescriptor = 0;
            uint8_t versionIter = 0;
            for (; versionIter < INQ_MAX_VERSION_DESCRIPTORS; versionIter++)
            {
                versionDescriptor = 0;
                versionDescriptor = M_BytesTo2ByteValue(inquiryData[(versionIter * 2) + 58], inquiryData[(versionIter * 2) + 59]);
                if (versionDescriptor > 0)
                {
                    DECLARE_ZERO_INIT_ARRAY(char, versionDescriptorString, MAX_VERSION_DESCRIPTOR_STRING_LENGTH + 1);
                    decypher_SCSI_Version_Descriptors(versionDescriptor, versionDescriptorString);
                    add_Specification_To_Supported_List(driveInfo->specificationsSupported, &driveInfo->numberOfSpecificationsSupported, versionDescriptorString);
                }
            }
        }
        if (strcmp(driveInfo->vendorID, "SEAGATE ") == 0)
        {
            driveInfo->copyrightValid = true;
            memcpy(&driveInfo->copyrightInfo[0], &inquiryData[97], 48);
            driveInfo->copyrightInfo[49] = '\0';
        }
    }
    return ret;
}

static eReturnValues get_SCSI_VPD_Data(tDevice* device, ptrDriveInformationSAS_SATA driveInfo, ptrSCSIIdentifyInfo scsiInfo)
{
    eReturnValues ret = SUCCESS;
    if (device && driveInfo && scsiInfo)
    {
        //VPD pages (read list of supported pages...if we don't get anything back, we'll dummy up a list of things we are interested in trying to read...this is to work around crappy USB bridges
        uint8_t* tempBuf = C_CAST(uint8_t*, safe_calloc_aligned(LEGACY_DRIVE_SEC_SIZE * 2, sizeof(uint8_t), device->os_info.minimumAlignment));
        if (!tempBuf)
        {
            return MEMORY_FAILURE;
        }
        //some devices (external) don't support VPD pages or may have issue when trying to read them, so check if this hack is set before attempting to read them
        if ((!device->drive_info.passThroughHacks.scsiHacks.noVPDPages || device->drive_info.passThroughHacks.scsiHacks.unitSNAvailable) && (scsiInfo->version >= 2 || device->drive_info.passThroughHacks.scsiHacks.unitSNAvailable)) //VPD pages indroduced in SCSI 2...also a USB hack
        {
            bool dummyUpVPDSupport = false;
            if (!device->drive_info.passThroughHacks.scsiHacks.unitSNAvailable && SUCCESS != scsi_Inquiry(device, tempBuf, 255, 0, true, false))
            {
                //for whatever reason, this device didn't return support for the list of supported pages, so set a flag telling us to dummy up a list so that we can still attempt to issue commands to pages we do need to try and get (this is a workaround for some really stupid USB bridges)
                dummyUpVPDSupport = true;
            }
            else if (device->drive_info.passThroughHacks.scsiHacks.unitSNAvailable)
            {
                dummyUpVPDSupport = true;
            }
            if (!dummyUpVPDSupport)
            {
                if (is_Empty(tempBuf, 255))
                {
                    //this case means that the command was successful, but we got nothing but zeros....which happens on some craptastic USB bridges
                    dummyUpVPDSupport = true;
                }
            }
            if (dummyUpVPDSupport)
            {
                uint16_t offset = 4;
                //in here we will set up a fake supported VPD pages buffer so that we try to read the unit serial number page, the SAT page, and device identification page
                tempBuf[0] = C_CAST(uint8_t, scsiInfo->peripheralQualifier << 5);
                tempBuf[0] |= scsiInfo->peripheralDeviceType;
                //set page code
                tempBuf[1] = 0x00;
                if (device->drive_info.passThroughHacks.scsiHacks.unitSNAvailable)
                {
                    //This is a hack for devices that will only support this page and MAYBE the device identification VPD page.
                    //Not adding the device ID page here because it almost always contains only a name string and/or a T10 string which isn't useful for the information being gathered in here today.
                    tempBuf[offset] = UNIT_SERIAL_NUMBER;//SCSI2
                    ++offset;
                }
                else
                {
                    //now each byte will reference a supported VPD page we want to dummy up. These should be in ascending order
                    tempBuf[offset] = SUPPORTED_VPD_PAGES;//SCSI2
                    ++offset;
                    tempBuf[offset] = UNIT_SERIAL_NUMBER;//SCSI2
                    ++offset;
                    if (scsiInfo->version >= 3)//SPC
                    {
                        tempBuf[offset] = DEVICE_IDENTIFICATION;
                        ++offset;
                    }
                    if (!device->drive_info.passThroughHacks.scsiHacks.noSATVPDPage)
                    {
                        tempBuf[offset] = ATA_INFORMATION;//SAT. Going to leave this in here no matter what other version info is available since SATLs needing this dummy data may support this regardless of other version info
                        ++offset;
                    }
                    if (scsiInfo->version >= 6)//SBC3 - SPC4
                    {
                        if (scsiInfo->peripheralDeviceType == PERIPHERAL_DIRECT_ACCESS_BLOCK_DEVICE || scsiInfo->peripheralDeviceType == PERIPHERAL_SIMPLIFIED_DIRECT_ACCESS_DEVICE || scsiInfo->peripheralDeviceType == PERIPHERAL_HOST_MANAGED_ZONED_BLOCK_DEVICE)
                        {
                            tempBuf[offset] = BLOCK_DEVICE_CHARACTERISTICS;
                            ++offset;
                        }
                    }
                    //Add more pages to the dummy information as we need to. This may be useful to do in the future in case a device decides not to support a MANDATORY page or another page we care about
                }
                //set page length (n-3)
                tempBuf[2] = M_Byte1(offset - 4);//msb
                tempBuf[3] = M_Byte0(offset - 4);//lsb
            }
            //first, get the length of the supported pages
            uint16_t supportedVPDPagesLength = M_BytesTo2ByteValue(tempBuf[2], tempBuf[3]);
            uint8_t* supportedVPDPages = C_CAST(uint8_t*, safe_calloc(supportedVPDPagesLength, sizeof(uint8_t)));
            if (!supportedVPDPages)
            {
                perror("Error allocating memory for supported VPD pages!\n");
                safe_free_aligned(&tempBuf);
                return MEMORY_FAILURE;
            }
            memcpy(supportedVPDPages, &tempBuf[4], supportedVPDPagesLength);
            //now loop through and read pages as we need to, only reading the pages that we care about
            uint16_t vpdIter = 0;
            for (vpdIter = 0; vpdIter < supportedVPDPagesLength && !device->drive_info.passThroughHacks.scsiHacks.noVPDPages; vpdIter++)
            {
                switch (supportedVPDPages[vpdIter])
                {
                case UNIT_SERIAL_NUMBER:
                {
                    uint8_t unitSerialNumberPageLength = SERIAL_NUM_LEN + 4;//adding 4 bytes extra for the header
                    uint8_t* unitSerialNumber = C_CAST(uint8_t*, safe_calloc_aligned(unitSerialNumberPageLength, sizeof(uint8_t), device->os_info.minimumAlignment));
                    if (!unitSerialNumber)
                    {
                        perror("Error allocating memory to read the unit serial number");
                        continue;//continue the loop
                    }
                    if (SUCCESS == scsi_Inquiry(device, unitSerialNumber, unitSerialNumberPageLength, supportedVPDPages[vpdIter], true, false))
                    {
                        uint16_t serialNumberLength = M_BytesTo2ByteValue(unitSerialNumber[2], unitSerialNumber[3]);
                        if (serialNumberLength > 0)
                        {
                            if (strncmp(driveInfo->vendorID, "SEAGATE", safe_strlen("SEAGATE")) == 0 && serialNumberLength == 0x14)//Check SEAGATE Vendor ID And check that the length matches the SCSI commands reference manual
                            {
                                //get the SN and PCBA SN separetly. This is unique to Seagate drives at this time.
                                safe_memcpy(driveInfo->serialNumber, SERIAL_NUM_LEN, &unitSerialNumber[4], 8);
                                driveInfo->serialNumber[8] = '\0';
                                remove_Leading_And_Trailing_Whitespace_Len(driveInfo->serialNumber, 8);
                                //remaining is PCBA SN
                                safe_memcpy(driveInfo->pcbaSerialNumber, SERIAL_NUM_LEN, &unitSerialNumber[12], 12);
                                driveInfo->pcbaSerialNumber[12] = '\0';
                                remove_Leading_And_Trailing_Whitespace_Len(driveInfo->pcbaSerialNumber, 12);
                            }
                            else
                            {
                                safe_memcpy(driveInfo->serialNumber, SERIAL_NUM_LEN, &unitSerialNumber[4], M_Min(SERIAL_NUM_LEN, serialNumberLength));
                                driveInfo->serialNumber[M_Min(SERIAL_NUM_LEN, serialNumberLength)] = '\0';
                                remove_Leading_And_Trailing_Whitespace_Len(driveInfo->serialNumber, SERIAL_NUM_LEN);
                                for (uint8_t iter = 0; iter < SERIAL_NUM_LEN; ++iter)
                                {
                                    if (!safe_isprint(device->drive_info.serialNumber[iter]))
                                    {
                                        device->drive_info.serialNumber[iter] = ' ';
                                    }
                                }
                                remove_Leading_And_Trailing_Whitespace(device->drive_info.serialNumber);
                                //For Seagate and LaCie USB drives, need to remove leading or trailing zeroes.
                                if (is_Seagate_USB_Vendor_ID(driveInfo->vendorID) || is_LaCie_USB_Vendor_ID(driveInfo->vendorID))
                                {
                                    char* snPtr = driveInfo->serialNumber;
                                    const char* t10VIDPtr = driveInfo->vendorID;
                                    seagate_Serial_Number_Cleanup(t10VIDPtr, &snPtr, SERIAL_NUM_LEN + 1);
                                }
                            }
                        }
                    }
                    safe_free_aligned(&unitSerialNumber);
                    break;
                }
                case DEVICE_IDENTIFICATION:
                {
                    uint8_t* deviceIdentification = C_CAST(uint8_t*, safe_calloc_aligned(INQ_RETURN_DATA_LENGTH, sizeof(uint8_t), device->os_info.minimumAlignment));
                    if (!deviceIdentification)
                    {
                        perror("Error allocating memory to read device identification VPD page");
                        continue;
                    }
                    if (SUCCESS == scsi_Inquiry(device, deviceIdentification, INQ_RETURN_DATA_LENGTH, DEVICE_IDENTIFICATION, true, false))
                    {
                        uint16_t devIDPageLen = M_BytesTo2ByteValue(deviceIdentification[2], deviceIdentification[3]);
                        if (devIDPageLen + 4 > INQ_RETURN_DATA_LENGTH)
                        {
                            //realloc and re-read the page with the larger pagelength
                            uint8_t* temp = C_CAST(uint8_t*, safe_reallocf_aligned(C_CAST(void**, &deviceIdentification), 0, C_CAST(size_t, devIDPageLen) + 4, device->os_info.minimumAlignment));
                            if (!temp)
                            {
                                perror("Error trying to realloc for larget device identification VPD page data!\n");
                                return 101;
                            }
                            deviceIdentification = temp;
                            if (SUCCESS != scsi_Inquiry(device, deviceIdentification, devIDPageLen + 4, DEVICE_IDENTIFICATION, true, false))
                            {
                                //we had an error while trying to read the page...
                            }
                        }
                        driveInfo->interfaceSpeedInfo.serialSpeed.activePortNumber = 0xFF;//set to something invalid
                        //Below we loop through to the designator descriptors to find the WWN, and on SAS set the active port.
                        //we get the active phy from the low byte of the WWN when we find the association field set to 01b
                        uint64_t accotiatedWWN = 0;
                        uint8_t association = 0;
                        uint32_t deviceIdentificationIter = 4;
                        uint16_t pageLength = M_BytesTo2ByteValue(deviceIdentification[2], deviceIdentification[3]);
                        uint8_t designatorLength = 0;
                        uint8_t protocolIdentifier = 0;
                        uint8_t designatorType = 0;
                        for (; deviceIdentificationIter < C_CAST(uint32_t, pageLength + UINT16_C(4)); deviceIdentificationIter += designatorLength)
                        {
                            association = (deviceIdentification[deviceIdentificationIter + 1] >> 4) & 0x03;
                            designatorLength = deviceIdentification[deviceIdentificationIter + 3] + 4;
                            protocolIdentifier = M_Nibble1(deviceIdentification[deviceIdentificationIter]);
                            designatorType = M_Nibble0(deviceIdentification[deviceIdentificationIter + 1]);
                            switch (association)
                            {
                            case 0://associated with the addressed logical unit
                                if (designatorType == 0x03)
                                {
                                    driveInfo->worldWideNameSupported = true;
                                    //all NAA values other than 6 currently are 8 bytes long, so get all 8 of those bytes in.
                                    driveInfo->worldWideName = M_BytesTo8ByteValue(deviceIdentification[deviceIdentificationIter + 4], deviceIdentification[deviceIdentificationIter + 5], deviceIdentification[deviceIdentificationIter + 6],
                                        deviceIdentification[deviceIdentificationIter + 7], deviceIdentification[deviceIdentificationIter + 8], deviceIdentification[deviceIdentificationIter + 9], deviceIdentification[deviceIdentificationIter + 10], deviceIdentification[deviceIdentificationIter + 11]);
                                    //check NAA to see if it's an extended WWN
                                    uint8_t naa = M_Nibble15(driveInfo->worldWideName);
                                    if (naa == 6)
                                    {
                                        //extension is valid, so get the next 8 bytes
                                        driveInfo->worldWideNameExtensionValid = true;
                                        driveInfo->worldWideNameExtension = M_BytesTo8ByteValue(deviceIdentification[deviceIdentificationIter + 12], deviceIdentification[deviceIdentificationIter + 13], deviceIdentification[deviceIdentificationIter + 14],
                                            deviceIdentification[deviceIdentificationIter + 15], deviceIdentification[deviceIdentificationIter + 16], deviceIdentification[deviceIdentificationIter + 17], deviceIdentification[deviceIdentificationIter + 18], deviceIdentification[deviceIdentificationIter + 19]);
                                    }
                                }
                                break;
                            case 1://associated with the target port that received the command
                                if (is_Seagate_Family(device))
                                {
                                    if (protocolIdentifier == 0x06 && designatorType == 0x03)//SAS->only place that getting a port number makes sense right now since we aren't gathering port speed for other interfaces since it isn't reported.
                                    {
                                        //we know we have found the right designator, so read the WWN, and check the lowest nibble for the port number
                                        accotiatedWWN = M_BytesTo8ByteValue(deviceIdentification[deviceIdentificationIter + 4], deviceIdentification[deviceIdentificationIter + 5], deviceIdentification[deviceIdentificationIter + 6],
                                            deviceIdentification[deviceIdentificationIter + 7], deviceIdentification[deviceIdentificationIter + 8], deviceIdentification[deviceIdentificationIter + 9], deviceIdentification[deviceIdentificationIter + 10], deviceIdentification[deviceIdentificationIter + 11]);

                                        uint8_t lowNibble = M_Nibble0(accotiatedWWN);
                                        lowNibble &= 0x3;
                                        if (lowNibble == 1)
                                        {
                                            driveInfo->interfaceSpeedInfo.serialSpeed.activePortNumber = 0;
                                        }
                                        else if (lowNibble == 2)
                                        {
                                            driveInfo->interfaceSpeedInfo.serialSpeed.activePortNumber = 1;
                                        }
                                    }
                                }
                                break;
                            case 2://associated with SCSI target device that contains the addressed logical unit
                                break;
                            case 3://reserved
                            default:
                                break;
                            }
                        }
                    }
                    safe_free_aligned(&deviceIdentification);
                    break;
                }
                case EXTENDED_INQUIRY_DATA:
                {
                    uint8_t* extendedInquiryData = C_CAST(uint8_t*, safe_calloc_aligned(VPD_EXTENDED_INQUIRY_LEN, sizeof(uint8_t), device->os_info.minimumAlignment));
                    if (!extendedInquiryData)
                    {
                        perror("Error allocating memory to read extended inquiry VPD page");
                        continue;
                    }
                    if (SUCCESS == scsi_Inquiry(device, extendedInquiryData, VPD_EXTENDED_INQUIRY_LEN, EXTENDED_INQUIRY_DATA, true, false))
                    {
                        //get nvCache supported
                        driveInfo->nvCacheSupported = extendedInquiryData[6] & BIT1;
                        //get longDST time since we read this page!
                        driveInfo->longDSTTimeMinutes = M_BytesTo2ByteValue(extendedInquiryData[10], extendedInquiryData[11]);
                        //get supported protection types
                        switch (M_GETBITRANGE(extendedInquiryData[4], 5, 3))
                        {
                        case 0:
                            scsiInfo->protectionType1Supported = true;
                            break;
                        case 1:
                            scsiInfo->protectionType1Supported = true;
                            scsiInfo->protectionType2Supported = true;
                            break;
                        case 2:
                            scsiInfo->protectionType2Supported = true;
                            break;
                        case 3:
                            scsiInfo->protectionType1Supported = true;
                            scsiInfo->protectionType3Supported = true;
                            break;
                        case 4:
                            scsiInfo->protectionType3Supported = true;
                            break;
                        case 5:
                            scsiInfo->protectionType2Supported = true;
                            scsiInfo->protectionType3Supported = true;
                            break;
                        case 6:
                            //read supported lengths and protection types VPD page
                        {
                            uint16_t supportedBlockSizesAndProtectionTypesLength = 4;//reallocate in a minute when we know how much to read
                            uint8_t* supportedBlockSizesAndProtectionTypes = C_CAST(uint8_t*, safe_calloc_aligned(supportedBlockSizesAndProtectionTypesLength, sizeof(uint8_t), device->os_info.minimumAlignment));
                            if (supportedBlockSizesAndProtectionTypes)
                            {
                                if (SUCCESS == scsi_Inquiry(device, supportedBlockSizesAndProtectionTypes, supportedBlockSizesAndProtectionTypesLength, SUPPORTED_BLOCK_LENGTHS_AND_PROTECTION_TYPES, true, false))
                                {
                                    supportedBlockSizesAndProtectionTypesLength = M_BytesTo2ByteValue(supportedBlockSizesAndProtectionTypes[2], supportedBlockSizesAndProtectionTypes[3]);
                                    uint8_t* temp = C_CAST(uint8_t*, safe_reallocf_aligned(C_CAST(void**, &supportedBlockSizesAndProtectionTypes), 0, supportedBlockSizesAndProtectionTypesLength * sizeof(uint8_t), device->os_info.minimumAlignment));
                                    supportedBlockSizesAndProtectionTypes = temp;
                                    if (SUCCESS == scsi_Inquiry(device, supportedBlockSizesAndProtectionTypes, supportedBlockSizesAndProtectionTypesLength, SUPPORTED_BLOCK_LENGTHS_AND_PROTECTION_TYPES, true, false))
                                    {
                                        //loop through and find supported protection types...
                                        for (uint32_t offset = UINT16_C(4); offset < C_CAST(uint32_t, supportedBlockSizesAndProtectionTypesLength + UINT16_C(4)); offset += UINT16_C(8))
                                        {
                                            if (supportedBlockSizesAndProtectionTypes[offset + 5] & BIT1)
                                            {
                                                scsiInfo->protectionType1Supported = true;
                                            }
                                            if (supportedBlockSizesAndProtectionTypes[offset + 5] & BIT2)
                                            {
                                                scsiInfo->protectionType2Supported = true;
                                            }
                                            if (supportedBlockSizesAndProtectionTypes[offset + 5] & BIT3)
                                            {
                                                scsiInfo->protectionType3Supported = true;
                                            }
                                            if (scsiInfo->protectionType1Supported && scsiInfo->protectionType2Supported && scsiInfo->protectionType3Supported)
                                            {
                                                //all protection types supported so we can leave the loop
                                                break;
                                            }
                                        }
                                    }
                                }
                                safe_free_aligned(&supportedBlockSizesAndProtectionTypes);
                            }
                            //no else...don't care that much right now...-TJE
                        }
                        break;
                        case 7:
                            scsiInfo->protectionType1Supported = true;
                            scsiInfo->protectionType2Supported = true;
                            scsiInfo->protectionType3Supported = true;
                            break;
                        }
                    }
                    safe_free_aligned(&extendedInquiryData);
                    break;
                }
                case BLOCK_DEVICE_CHARACTERISTICS:
                {
                    uint8_t* blockDeviceCharacteristics = C_CAST(uint8_t*, safe_calloc_aligned(VPD_BLOCK_DEVICE_CHARACTERISTICS_LEN, sizeof(uint8_t), device->os_info.minimumAlignment));
                    if (!blockDeviceCharacteristics)
                    {
                        perror("Error allocating memory to read block device characteistics VPD page");
                        continue;
                    }
                    if (SUCCESS == scsi_Inquiry(device, blockDeviceCharacteristics, VPD_BLOCK_DEVICE_CHARACTERISTICS_LEN, BLOCK_DEVICE_CHARACTERISTICS, true, false))
                    {
                        driveInfo->rotationRate = M_BytesTo2ByteValue(blockDeviceCharacteristics[4], blockDeviceCharacteristics[5]);
                        driveInfo->formFactor = M_Nibble0(blockDeviceCharacteristics[7]);
                        driveInfo->zonedDevice = (blockDeviceCharacteristics[8] & (BIT4 | BIT5)) >> 4;
                    }
                    safe_free_aligned(&blockDeviceCharacteristics);
                    break;
                }
                case POWER_CONDITION:
                    //reading this information has been moved to the mode pages below. - TJE
                    //add_Feature_To_Supported_List(driveInfo->featuresSupported, &driveInfo->numberOfFeaturesSupported, "EPC");
                    break;
                case POWER_CONSUMPTION:
                    add_Feature_To_Supported_List(driveInfo->featuresSupported, &driveInfo->numberOfFeaturesSupported, "Power Consumption");
                    break;
                case LOGICAL_BLOCK_PROVISIONING:
                {
                    uint8_t* logicalBlockProvisioning = C_CAST(uint8_t*, safe_calloc_aligned(VPD_LOGICAL_BLOCK_PROVISIONING_LEN, sizeof(uint8_t), device->os_info.minimumAlignment));
                    if (!logicalBlockProvisioning)
                    {
                        perror("Error allocating memory to read logical block provisioning VPD page");
                        continue;
                    }
                    if (SUCCESS == scsi_Inquiry(device, logicalBlockProvisioning, VPD_LOGICAL_BLOCK_PROVISIONING_LEN, LOGICAL_BLOCK_PROVISIONING, true, false))
                    {
                        if (logicalBlockProvisioning[5] & BIT7)
                        {
                            DECLARE_ZERO_INIT_ARRAY(char, unmapDetails, 48);
                            uint8_t lbprz = M_GETBITRANGE(logicalBlockProvisioning[5], 4, 2);
                            if (logicalBlockProvisioning[5] & BIT1 || lbprz)
                            {
                                DECLARE_ZERO_INIT_ARRAY(char, lbprzStr, 22);
                                if (lbprz == 0)
                                {
                                    //vendor unique
                                    snprintf(lbprzStr, 22, "Vendor Pattern");
                                }
                                else if (lbprz & BIT0)
                                {
                                    snprintf(lbprzStr, 22, "Zeros");
                                }
                                else if (lbprz == 0x02)
                                {
                                    snprintf(lbprzStr, 22, "Provisioning Pattern");
                                }
                                if (logicalBlockProvisioning[5] & BIT1)
                                {
                                    snprintf(unmapDetails, 48, "UNMAP [Deterministic, %s]", lbprzStr);
                                }
                                else if (safe_strlen(lbprzStr))
                                {
                                    snprintf(unmapDetails, 48, "UNMAP [%s]", lbprzStr);
                                }
                                add_Feature_To_Supported_List(driveInfo->featuresSupported, &driveInfo->numberOfFeaturesSupported, unmapDetails);
                            }
                            else
                            {
                                add_Feature_To_Supported_List(driveInfo->featuresSupported, &driveInfo->numberOfFeaturesSupported, "UNMAP");
                            }
                        }
                    }
                    safe_free_aligned(&logicalBlockProvisioning);
                    break;
                }
                case BLOCK_LIMITS:
                {
                    uint8_t* blockLimits = C_CAST(uint8_t*, safe_calloc_aligned(VPD_BLOCK_LIMITS_LEN, sizeof(uint8_t), device->os_info.minimumAlignment));
                    if (!blockLimits)
                    {
                        perror("Error allocating memory to read logical block provisioning VPD page");
                        continue;
                    }
                    if (SUCCESS == scsi_Inquiry(device, blockLimits, VPD_BLOCK_LIMITS_LEN, BLOCK_LIMITS, true, false))
                    {
                        uint64_t writeSameLength = M_BytesTo8ByteValue(blockLimits[36], blockLimits[37], blockLimits[38], blockLimits[39], blockLimits[40], blockLimits[41], blockLimits[42], blockLimits[43]);
                        uint32_t maxAtomicLen = M_BytesTo4ByteValue(blockLimits[44], blockLimits[45], blockLimits[46], blockLimits[47]);
                        uint32_t atomicAlign = M_BytesTo4ByteValue(blockLimits[48], blockLimits[49], blockLimits[50], blockLimits[51]);
                        uint32_t atomicXferLenGran = M_BytesTo4ByteValue(blockLimits[52], blockLimits[53], blockLimits[54], blockLimits[55]);
                        uint32_t maxAtomicLenWAtomicBoundary = M_BytesTo4ByteValue(blockLimits[56], blockLimits[57], blockLimits[58], blockLimits[59]);
                        uint32_t maxAtomicBoundarySize = M_BytesTo4ByteValue(blockLimits[60], blockLimits[61], blockLimits[62], blockLimits[63]);
                        if (writeSameLength > 0)
                        {
                            add_Feature_To_Supported_List(driveInfo->featuresSupported, &driveInfo->numberOfFeaturesSupported, "Write Same");
                        }
                        if (maxAtomicLen > 0 || atomicAlign > 0 || atomicXferLenGran > 0 || maxAtomicLenWAtomicBoundary > 0 || maxAtomicBoundarySize > 0)
                        {
                            add_Feature_To_Supported_List(driveInfo->featuresSupported, &driveInfo->numberOfFeaturesSupported, "Atomic Writes");
                        }
                    }
                    safe_free_aligned(&blockLimits);
                    break;
                }
                case ATA_INFORMATION:
                {
                    uint8_t* ataInformation = C_CAST(uint8_t*, safe_calloc_aligned(VPD_ATA_INFORMATION_LEN, sizeof(uint8_t), device->os_info.minimumAlignment));
                    if (!ataInformation)
                    {
                        perror("Error allocating memory to read ATA Information VPD page");
                        continue;
                    }
                    if (SUCCESS == scsi_Inquiry(device, ataInformation, VPD_ATA_INFORMATION_LEN, ATA_INFORMATION, true, false))
                    {
                        add_Feature_To_Supported_List(driveInfo->featuresSupported, &driveInfo->numberOfFeaturesSupported, "SAT");
                        memcpy(driveInfo->satVendorID, &ataInformation[8], 8);
                        memcpy(driveInfo->satProductID, &ataInformation[16], 16);
                        memcpy(driveInfo->satProductRevision, &ataInformation[32], 4);

                    }
                    safe_free_aligned(&ataInformation);
                    break;
                }
                case CONCURRENT_POSITIONING_RANGES:
                {
                    uint32_t concurrentRangesLength = (15 * 32) + 64;//max of 15 ranges at 32 bytes each, plus 64 bytes that show ahead as a "header"
                    uint8_t* concurrentRangesData = C_CAST(uint8_t*, safe_calloc_aligned(concurrentRangesLength, sizeof(uint8_t), device->os_info.minimumAlignment));
                    if (!concurrentRangesData)
                    {
                        perror("Error allocating memory to read concurrent positioning ranges VPD page");
                        continue;
                    }
                    if (SUCCESS == scsi_Inquiry(device, concurrentRangesData, concurrentRangesLength, CONCURRENT_POSITIONING_RANGES, true, false))
                    {
                        //calculate how many ranges are being reported by the device.
                        driveInfo->concurrentPositioningRanges = C_CAST(uint8_t, (M_BytesTo2ByteValue(concurrentRangesData[2], concurrentRangesData[3]) - 60) / 32);//-60 since page length doesn't include first 4 bytes and descriptors start at offset 64. Each descriptor is 32B long
                    }
                    safe_free_aligned(&concurrentRangesData);
                }
                break;
                case ZONED_BLOCK_DEVICE_CHARACTERISTICS:
                {
                    uint8_t* zbdCharacteristics = C_CAST(uint8_t*, safe_calloc_aligned(VPD_ZONED_BLOCK_DEVICE_CHARACTERISTICS_LEN, sizeof(uint8_t), device->os_info.minimumAlignment));
                    if (!zbdCharacteristics)
                    {
                        perror("Error allocating memory to read zoned block device characteristics VPD page");
                        continue;
                    }
                    if (SUCCESS == scsi_Inquiry(device, zbdCharacteristics, VPD_ZONED_BLOCK_DEVICE_CHARACTERISTICS_LEN, ZONED_BLOCK_DEVICE_CHARACTERISTICS, true, false))
                    {
                        switch (M_Nibble1(zbdCharacteristics[4]))
                        {
                        case 0:
                            break;
                        case 1://host aware
                            break;
                        case 2://domains and realms
                            //add to features supported
                            scsiInfo->zoneDomainsOrRealms = true;
                            break;
                        default:
                            break;
                        }
                    }
                    safe_free_aligned(&zbdCharacteristics);
                }
                    break;
                default:
                    break;
                }
            }
            safe_free(&supportedVPDPages);
        }
        else
        {
            //SCSI(1)/SASI/CCS don't have VPD pages. Try getting the SN from here (and that's all you get!)
            memcpy(driveInfo->serialNumber, &device->drive_info.scsiVpdData.inquiryData[36], SERIAL_NUM_LEN);
            device->drive_info.serialNumber[SERIAL_NUM_LEN] = '\0';
        }
        safe_free_aligned(&tempBuf);
    }
    return ret;
}

static eReturnValues get_SCSI_Log_Data(tDevice* device, ptrDriveInformationSAS_SATA driveInfo, ptrSCSIIdentifyInfo scsiInfo)
{
    eReturnValues ret = SUCCESS;
    if (device && driveInfo && scsiInfo)
    {
        bool smartStatusRead = false;
        if (scsiInfo->version >= 2 && scsiInfo->peripheralDeviceType != PERIPHERAL_SIMPLIFIED_DIRECT_ACCESS_DEVICE && !device->drive_info.passThroughHacks.scsiHacks.noLogPages)//SCSI2 introduced log pages
        {
            bool dummyUpLogPages = false;
            bool subpagesSupported = true;
            //Check log pages for data->start with list of pages and subpages
            uint8_t* scsiLogBuf = C_CAST(uint8_t*, safe_calloc_aligned(512, sizeof(uint8_t), device->os_info.minimumAlignment));
            if (scsiLogBuf)
            {
                if (!device->drive_info.passThroughHacks.scsiHacks.noLogSubPages && SUCCESS != scsi_Log_Sense_Cmd(device, false, LPC_CUMULATIVE_VALUES, LP_SUPPORTED_LOG_PAGES_AND_SUBPAGES, 0xFF, 0, scsiLogBuf, 512))
                {
                    //either device doesn't support logs, or it just doesn't support subpages, so let's try reading the list of supported pages (no subpages) before saying we need to dummy up the list
                    if (SUCCESS != scsi_Log_Sense_Cmd(device, false, LPC_CUMULATIVE_VALUES, LP_SUPPORTED_LOG_PAGES, 0, 0, scsiLogBuf, 512))
                    {
                        dummyUpLogPages = true;
                    }
                    else
                    {
                        subpagesSupported = false;
                    }
                }
                else if (device->drive_info.passThroughHacks.scsiHacks.noLogSubPages)
                {
                    //device doesn't support subpages, so read the list of pages without subpages before continuing.
                    if (SUCCESS != scsi_Log_Sense_Cmd(device, false, LPC_CUMULATIVE_VALUES, LP_SUPPORTED_LOG_PAGES, 0, 0, scsiLogBuf, 512))
                    {
                        dummyUpLogPages = true;
                    }
                    subpagesSupported = false;
                }
                if (device->drive_info.passThroughHacks.scsiHacks.noLogPages)
                {
                    //trying to read the list of supported pages can trigger this to show up due to invalid operation code
                    //when this happens, just return to save the time and effort.
                    safe_free_aligned(&scsiLogBuf);
                    driveInfo->smartStatus = 2;
                    return NOT_SUPPORTED;
                }
                if (!dummyUpLogPages)
                {
                    //memcmp to make sure we weren't given zeros
                    if (is_Empty(scsiLogBuf, 512))
                    {
                        dummyUpLogPages = true;
                    }
                }
                //this is really a work-around for USB drives since some DO support pages, but the don't actually list them (same as the VPD pages above). Most USB drives don't work though - TJE
                if (dummyUpLogPages)
                {
                    uint16_t offset = 4;
                    uint8_t increment = 1;//change to 2 for subpages (spc4 added subpages)
                    if (scsiInfo->version >= 6)
                    {
                        subpagesSupported = true;
                        increment = 2;
                    }
                    memset(scsiLogBuf, 0, LEGACY_DRIVE_SEC_SIZE);
                    scsiLogBuf[0] = 0;
                    scsiLogBuf[1] = 0;

                    //descriptors Need to be added based on support for subpages or not!
                    scsiLogBuf[offset] = LP_SUPPORTED_LOG_PAGES;//just to be correct/accurate
                    if (subpagesSupported)
                    {
                        scsiLogBuf[offset + 1] = 0;//subpage
                        offset += increment;
                        scsiLogBuf[offset] = LP_SUPPORTED_LOG_PAGES_AND_SUBPAGES;//just to be correct/accurate
                        scsiLogBuf[offset + 1] = 0xFF;//supported subpages
                    }
                    offset += increment;
                    scsiLogBuf[offset] = LP_WRITE_ERROR_COUNTERS;//not likely available on USB
                    offset += increment;
                    scsiLogBuf[offset] = LP_READ_ERROR_COUNTERS;//not likely available on USB
                    offset += increment;

                    //if SBC3! (sequential access device page, so also check peripheral device type)
                    if (scsiInfo->peripheralDeviceType == PERIPHERAL_DIRECT_ACCESS_BLOCK_DEVICE || scsiInfo->peripheralDeviceType == PERIPHERAL_HOST_MANAGED_ZONED_BLOCK_DEVICE)
                    {
                        if (scsiInfo->version >= 6) //SBC3 is to be used in conjunction with SPC4. We may need to drop this one level later, but this should be ok
                        {
                            scsiLogBuf[offset] = LP_LOGICAL_BLOCK_PROVISIONING;//SBC3
                            offset += increment;
                        }
                    }

                    if (scsiInfo->version >= 4)//SPC2
                    {
                        scsiLogBuf[offset] = LP_TEMPERATURE;//not likely available on USB
                        offset += increment;
                    }

                    if (subpagesSupported && scsiInfo->version >= 7)
                    {
                        scsiLogBuf[offset] = LP_ENVIRONMENTAL_REPORTING;//not likely available on USB
                        scsiLogBuf[offset + 1] = 0x01;//subpage (page number is same as temperature)
                        offset += increment;
                    }
                    if (scsiInfo->version >= 4)//SPC2
                    {
                        scsiLogBuf[offset] = LP_START_STOP_CYCLE_COUNTER;//just to be correct, we're not reading this today
                        offset += increment;
                    }
                    if (scsiInfo->version >= 7)//SBC4?
                    {
                        scsiLogBuf[offset] = LP_UTILIZATION;//not likely available on USB
                        scsiLogBuf[offset + 1] = 0x01;//subpage
                        offset += increment;
                    }
                    if (scsiInfo->version >= 4)//SPC2
                    {
                        scsiLogBuf[offset] = LP_APPLICATION_CLIENT;
                        offset += increment;
                        scsiLogBuf[offset] = LP_SELF_TEST_RESULTS;
                        offset += increment;
                    }

                    if (scsiInfo->peripheralDeviceType == PERIPHERAL_DIRECT_ACCESS_BLOCK_DEVICE || scsiInfo->peripheralDeviceType == PERIPHERAL_HOST_MANAGED_ZONED_BLOCK_DEVICE)
                    {
                        if (scsiInfo->version >= 6) //SBC3 is to be used in conjunction with SPC4. We may need to drop this one level later, but this should be ok
                        {
                            scsiLogBuf[offset] = LP_SOLID_STATE_MEDIA;//not likely available on USB
                            offset += increment;
                        }
                    }

                    if (scsiInfo->peripheralDeviceType == PERIPHERAL_DIRECT_ACCESS_BLOCK_DEVICE || scsiInfo->peripheralDeviceType == PERIPHERAL_HOST_MANAGED_ZONED_BLOCK_DEVICE)
                    {
                        if (scsiInfo->version >= 6) //SBC3 is to be used in conjunction with SPC4. We may need to drop this one level later, but this should be ok
                        {
                            scsiLogBuf[offset] = LP_BACKGROUND_SCAN_RESULTS;//not likely available on USB
                            offset += increment;
                        }
                    }

                    if (scsiInfo->version >= 6)
                    {
                        scsiLogBuf[offset] = LP_GENERAL_STATISTICS_AND_PERFORMANCE;//not likely available on USB
                        offset += increment;
                    }

                    if (scsiInfo->peripheralDeviceType == PERIPHERAL_DIRECT_ACCESS_BLOCK_DEVICE || scsiInfo->peripheralDeviceType == PERIPHERAL_HOST_MANAGED_ZONED_BLOCK_DEVICE)
                    {
                        if (scsiInfo->version >= 5) //SBC2 is to be used in conjunction with SPC3. We may need to drop this one level later, but this should be ok
                        {
                            scsiLogBuf[offset] = LP_INFORMATION_EXCEPTIONS;
                            offset += increment;
                        }
                    }

                    //page length
                    scsiLogBuf[2] = M_Byte1(offset - 4);
                    scsiLogBuf[3] = M_Byte0(offset - 4);
                }
                //loop through log pages and read them:
                uint16_t logPageIter = LOG_PAGE_HEADER_LENGTH;//log page descriptors start on offset 4 and are 2 bytes long each
                uint16_t supportedPagesLength = M_BytesTo2ByteValue(scsiLogBuf[2], scsiLogBuf[3]);
                uint8_t incrementAmount = subpagesSupported ? 2 : 1;
                for (; logPageIter < M_Min(supportedPagesLength + LOG_PAGE_HEADER_LENGTH, LEGACY_DRIVE_SEC_SIZE) && !device->drive_info.passThroughHacks.scsiHacks.noLogPages; logPageIter += incrementAmount)
                {
                    uint8_t pageCode = scsiLogBuf[logPageIter] & 0x3F;//outer switch statement
                    uint8_t subpageCode = 0;
                    if (subpagesSupported)
                    {
                        subpageCode = scsiLogBuf[logPageIter + 1];//inner switch statement
                    }
                    switch (pageCode)
                    {
                    case LP_WRITE_ERROR_COUNTERS:
                        if (subpageCode == 0)
                        {
                            //we need parameter code 5h (total bytes processed)
                            //assume we only need to read 16 bytes to get this value
                            uint8_t* writeErrorData = C_CAST(uint8_t*, safe_calloc_aligned(16, sizeof(uint8_t), device->os_info.minimumAlignment));
                            if (!writeErrorData)
                            {
                                break;
                            }
                            if (SUCCESS == scsi_Log_Sense_Cmd(device, false, LPC_CUMULATIVE_VALUES, pageCode, subpageCode, 0x0005, writeErrorData, 16))
                            {
                                //check the length before we start trying to read the number of bytes in.
                                if (M_BytesTo2ByteValue(writeErrorData[4], writeErrorData[5]) == 0x0005)
                                {
                                    uint8_t paramLength = writeErrorData[7];
                                    switch (paramLength)
                                    {
                                    case 1://single byte
                                        driveInfo->totalBytesWritten = writeErrorData[8];
                                        break;
                                    case 2://word
                                        driveInfo->totalBytesWritten = M_BytesTo2ByteValue(writeErrorData[8], writeErrorData[9]);
                                        break;
                                    case 4://double word
                                        driveInfo->totalBytesWritten = M_BytesTo4ByteValue(writeErrorData[8], writeErrorData[9], writeErrorData[10], writeErrorData[11]);
                                        break;
                                    case 8://quad word
                                        driveInfo->totalBytesWritten = M_BytesTo8ByteValue(writeErrorData[8], writeErrorData[9], writeErrorData[10], writeErrorData[11], writeErrorData[12], writeErrorData[13], writeErrorData[14], writeErrorData[15]);
                                        break;
                                    default://don't bother trying to read the data since it's in a more complicated format to read than we care to handle in this code right now
                                        break;
                                    }
                                    //now convert this to LBAs based on the logical sector size
                                    if (driveInfo->logicalSectorSize)
                                    {
                                        driveInfo->totalLBAsWritten = driveInfo->totalBytesWritten / driveInfo->logicalSectorSize;
                                    }
                                }
                            }
                            safe_free_aligned(&writeErrorData);
                        }
                        break;
                    case LP_READ_ERROR_COUNTERS:
                        if (subpageCode == 0)
                        {
                            //we need parameter code 5h (total bytes processed)
                            //assume we only need to read 16 bytes to get this value
                            uint8_t* readErrorData = C_CAST(uint8_t*, safe_calloc_aligned(16, sizeof(uint8_t), device->os_info.minimumAlignment));
                            if (!readErrorData)
                            {
                                break;
                            }
                            if (SUCCESS == scsi_Log_Sense_Cmd(device, false, LPC_CUMULATIVE_VALUES, pageCode, subpageCode, 0x0005, readErrorData, 16))
                            {
                                if (M_BytesTo2ByteValue(readErrorData[4], readErrorData[5]) == 0x0005)
                                {
                                    //check the length before we start trying to read the number of bytes in.
                                    uint8_t paramLength = readErrorData[7];
                                    switch (paramLength)
                                    {
                                    case 1://single byte
                                        driveInfo->totalBytesRead = readErrorData[8];
                                        break;
                                    case 2://word
                                        driveInfo->totalBytesRead = M_BytesTo2ByteValue(readErrorData[8], readErrorData[9]);
                                        break;
                                    case 4://double word
                                        driveInfo->totalBytesRead = M_BytesTo4ByteValue(readErrorData[8], readErrorData[9], readErrorData[10], readErrorData[11]);
                                        break;
                                    case 8://quad word
                                        driveInfo->totalBytesRead = M_BytesTo8ByteValue(readErrorData[8], readErrorData[9], readErrorData[10], readErrorData[11], readErrorData[12], readErrorData[13], readErrorData[14], readErrorData[15]);
                                        break;
                                    default://don't bother trying to read the data since it's in a more complicated format to read than we care to handle in this code right now
                                        break;
                                    }
                                    //now convert this to LBAs based on the logical sector size
                                    if (driveInfo->logicalSectorSize)
                                    {
                                        driveInfo->totalLBAsRead = driveInfo->totalBytesRead / driveInfo->logicalSectorSize;
                                    }
                                }
                            }
                            safe_free_aligned(&readErrorData);
                        }
                        break;
                    case LP_LOGICAL_BLOCK_PROVISIONING:
                        /*if (subpageCode == 0)
                        {

                        }*/
                        break;
                    case LP_TEMPERATURE://also environmental reporting
                        switch (subpageCode)
                        {
                        case 0://temperature
                        {
                            uint8_t* temperatureData = C_CAST(uint8_t*, safe_calloc_aligned(10, sizeof(uint8_t), device->os_info.minimumAlignment));
                            if (!temperatureData)
                            {
                                break;
                            }
                            if (SUCCESS == scsi_Log_Sense_Cmd(device, false, LPC_CUMULATIVE_VALUES, pageCode, subpageCode, 0, temperatureData, 10))
                            {
                                driveInfo->temperatureData.temperatureDataValid = true;
                                driveInfo->temperatureData.currentTemperature = temperatureData[9];
                            }
                            safe_free_aligned(&temperatureData);
                        }
                        break;
                        case 1://environmental reporting
                        {
                            uint8_t* environmentReporting = C_CAST(uint8_t*, safe_calloc_aligned(16, sizeof(uint8_t), device->os_info.minimumAlignment));
                            if (!environmentReporting)
                            {
                                break;
                            }
                            //get temperature data first
                            if (SUCCESS == scsi_Log_Sense_Cmd(device, false, LPC_CUMULATIVE_VALUES, pageCode, subpageCode, 0, environmentReporting, 16))
                            {
                                driveInfo->temperatureData.temperatureDataValid = true;
                                driveInfo->temperatureData.currentTemperature = C_CAST(int8_t, environmentReporting[9]);
                                driveInfo->temperatureData.highestTemperature = C_CAST(int8_t, environmentReporting[10]);
                                driveInfo->temperatureData.lowestTemperature = C_CAST(int8_t, environmentReporting[11]);
                                driveInfo->temperatureData.highestValid = true;
                                driveInfo->temperatureData.lowestValid = true;
                            }
                            //now get humidity data if available
                            if (SUCCESS == scsi_Log_Sense_Cmd(device, false, LPC_CUMULATIVE_VALUES, pageCode, subpageCode, 0x0100, environmentReporting, 16))
                            {
                                driveInfo->humidityData.humidityDataValid = true;
                                driveInfo->humidityData.currentHumidity = environmentReporting[9];
                                driveInfo->humidityData.highestHumidity = environmentReporting[10];
                                driveInfo->humidityData.lowestHumidity = environmentReporting[11];
                                driveInfo->humidityData.highestValid = true;
                                driveInfo->humidityData.lowestValid = true;
                            }
                            safe_free_aligned(&environmentReporting);
                        }
                        break;
                        default:
                            break;
                        }
                        break;
                    case LP_UTILIZATION://also start-stop cycle counter
                        switch (subpageCode)
                        {
                        case 0x00://start-stop cycle count
                        {
                            uint8_t* startStopCounterLog = C_CAST(uint8_t*, safe_calloc_aligned(14, sizeof(uint8_t), device->os_info.minimumAlignment));
                            if (!startStopCounterLog)
                            {
                                break;
                            }
                            if (SUCCESS == scsi_Log_Sense_Cmd(device, false, LPC_CUMULATIVE_VALUES, pageCode, subpageCode, 0x0001, startStopCounterLog, 14))
                            {
                                //check that we have the correct page
                                if (!(startStopCounterLog[0] & BIT6) && M_GETBITRANGE(startStopCounterLog[0], 5, 0) == LP_START_STOP_CYCLE_COUNTER && startStopCounterLog[1] == 0)
                                {
                                    //check the parameter code in case a device/translator with this page is responding but did not give this parameter code
                                    if (M_BytesTo2ByteValue(startStopCounterLog[4], startStopCounterLog[5]) == 0x0001)
                                    {
                                        //DOM found
                                        char domWeekStr[3] = { C_CAST(char, startStopCounterLog[12]), C_CAST(char, startStopCounterLog[13]), 0 };
                                        char domYearStr[5] = { C_CAST(char, startStopCounterLog[8]), C_CAST(char, startStopCounterLog[9]), C_CAST(char, startStopCounterLog[10]), C_CAST(char, startStopCounterLog[11]), 0 };
                                        driveInfo->dateOfManufactureValid = true;
                                        if (!get_And_Validate_Integer_Input_Uint8(domWeekStr, NULL, ALLOW_UNIT_NONE, &driveInfo->manufactureWeek))
                                        {
                                            driveInfo->dateOfManufactureValid = false;
                                        }
                                        if (!get_And_Validate_Integer_Input_Uint16(domYearStr, NULL, ALLOW_UNIT_NONE, &driveInfo->manufactureYear))
                                        {
                                            driveInfo->dateOfManufactureValid = false;
                                        }
                                    }
                                }
                            }
                            safe_free_aligned(&startStopCounterLog);
                        }
                        break;
                        case 0x01://utilization
                        {
                            uint8_t* utilizationData = C_CAST(uint8_t*, safe_calloc_aligned(10, sizeof(uint8_t), device->os_info.minimumAlignment));
                            if (!utilizationData)
                            {
                                break;
                            }
                            if (SUCCESS == scsi_Log_Sense_Cmd(device, false, LPC_CUMULATIVE_VALUES, pageCode, subpageCode, 0, utilizationData, 10))
                            {
                                //bytes 9 & 10
                                driveInfo->deviceReportedUtilizationRate = C_CAST(double, M_BytesTo2ByteValue(utilizationData[8], utilizationData[9])) / 1000.0;
                            }
                            safe_free_aligned(&utilizationData);
                        }
                        break;
                        default:
                            break;
                        }
                        break;
                    case LP_APPLICATION_CLIENT:
                        switch (subpageCode)
                        {
                        case 0x00://application client
                        {
                            uint8_t* applicationClient = C_CAST(uint8_t*, safe_calloc_aligned(4, sizeof(uint8_t), device->os_info.minimumAlignment));
                            if (!applicationClient)
                            {
                                break;
                            }
                            if (SUCCESS == scsi_Log_Sense_Cmd(device, false, LPC_CUMULATIVE_VALUES, pageCode, subpageCode, 0, applicationClient, 4))
                            {
                                //add "Application Client Logging" to supported features :)
                                add_Feature_To_Supported_List(driveInfo->featuresSupported, &driveInfo->numberOfFeaturesSupported, "Application Client Logging");
                            }
                            safe_free_aligned(&applicationClient);
                        }
                        break;
                        default:
                            break;
                        }
                        break;
                    case LP_SELF_TEST_RESULTS:
                        if (subpageCode == 0)
                        {
                            uint8_t* selfTestResults = C_CAST(uint8_t*, safe_calloc_aligned(LP_SELF_TEST_RESULTS_LEN, sizeof(uint8_t), device->os_info.minimumAlignment));
                            if (!selfTestResults)
                            {
                                break;
                            }
                            if (SUCCESS == scsi_Log_Sense_Cmd(device, false, LPC_CUMULATIVE_VALUES, pageCode, subpageCode, 0, selfTestResults, LP_SELF_TEST_RESULTS_LEN))
                            {
                                uint8_t parameterOffset = 4;
                                add_Feature_To_Supported_List(driveInfo->featuresSupported, &driveInfo->numberOfFeaturesSupported, "Self Test");
                                //get the last DST information (parameter code 1)
                                driveInfo->dstInfo.informationValid = true;
                                driveInfo->dstInfo.resultOrStatus = M_Nibble0(selfTestResults[parameterOffset + 4]);
                                driveInfo->dstInfo.testNumber = M_Nibble1(selfTestResults[parameterOffset + 4]) >> 1;
                                driveInfo->dstInfo.powerOnHours = M_BytesTo2ByteValue(selfTestResults[parameterOffset + 6], selfTestResults[parameterOffset + 7]);
                                driveInfo->dstInfo.errorLBA = M_BytesTo8ByteValue(selfTestResults[parameterOffset + 8], selfTestResults[parameterOffset + 9], selfTestResults[parameterOffset + 10], selfTestResults[parameterOffset + 11], selfTestResults[parameterOffset + 12], selfTestResults[parameterOffset + 13], selfTestResults[parameterOffset + 14], selfTestResults[parameterOffset + 15]);
                            }
                            safe_free_aligned(&selfTestResults);
                        }
                        break;
                    case LP_SOLID_STATE_MEDIA:
                        if (subpageCode == 0)
                        {
                            //need parameter 0001h
                            uint8_t* ssdEnduranceData = C_CAST(uint8_t*, safe_calloc_aligned(12, sizeof(uint8_t), device->os_info.minimumAlignment));
                            if (!ssdEnduranceData)
                            {
                                break;
                            }
                            if (SUCCESS == scsi_Log_Sense_Cmd(device, false, LPC_CUMULATIVE_VALUES, pageCode, subpageCode, 0x0001, ssdEnduranceData, 12))
                            {
                                //bytes 7 of parameter 1 (or byte 12)
                                driveInfo->percentEnduranceUsed = C_CAST(double, ssdEnduranceData[11]);
                            }
                            safe_free_aligned(&ssdEnduranceData);
                        }
                        break;
                    case LP_BACKGROUND_SCAN_RESULTS:
                        if (subpageCode == 0)
                        {
                            //reading power on minutes from here
                            uint8_t* backgroundScanResults = C_CAST(uint8_t*, safe_calloc_aligned(19, sizeof(uint8_t), device->os_info.minimumAlignment));
                            if (!backgroundScanResults)
                            {
                                break;
                            }
                            if (SUCCESS == scsi_Log_Sense_Cmd(device, false, LPC_CUMULATIVE_VALUES, pageCode, subpageCode, 0, backgroundScanResults, 19))
                            {
                                //bytes 8 to 11
                                driveInfo->powerOnMinutes = M_BytesTo4ByteValue(backgroundScanResults[8], backgroundScanResults[9], backgroundScanResults[10], backgroundScanResults[11]);
                                driveInfo->powerOnMinutesValid = true;
                            }
                            safe_free_aligned(&backgroundScanResults);
                        }
                        break;
                    case LP_GENERAL_STATISTICS_AND_PERFORMANCE:
                        if (subpageCode == 0)
                        {
                            //parameter code 1 is what we're interested in for this one
                            uint8_t* generalStatsAndPerformance = C_CAST(uint8_t*, safe_calloc_aligned(72, sizeof(uint8_t), device->os_info.minimumAlignment));
                            if (!generalStatsAndPerformance)
                            {
                                break;
                            }
                            if (SUCCESS == scsi_Log_Sense_Cmd(device, false, LPC_CUMULATIVE_VALUES, pageCode, subpageCode, 0x0001, generalStatsAndPerformance, 72))
                            {
                                //total lbas written (number of logical blocks received)
                                driveInfo->totalLBAsWritten = M_BytesTo8ByteValue(generalStatsAndPerformance[24], generalStatsAndPerformance[25], generalStatsAndPerformance[26], generalStatsAndPerformance[27], generalStatsAndPerformance[28], generalStatsAndPerformance[29], generalStatsAndPerformance[30], generalStatsAndPerformance[31]);
                                //convert to bytes written
                                driveInfo->totalBytesWritten = driveInfo->totalLBAsWritten * driveInfo->logicalSectorSize;
                                //total lbas read (number of logical blocks transmitted)
                                driveInfo->totalLBAsRead = M_BytesTo8ByteValue(generalStatsAndPerformance[32], generalStatsAndPerformance[33], generalStatsAndPerformance[34], generalStatsAndPerformance[35], generalStatsAndPerformance[36], generalStatsAndPerformance[37], generalStatsAndPerformance[38], generalStatsAndPerformance[39]);
                                //convert to bytes written
                                driveInfo->totalBytesRead = driveInfo->totalLBAsRead * driveInfo->logicalSectorSize;
                            }
                            safe_free_aligned(&generalStatsAndPerformance);
                        }
                        break;
                    case LP_INFORMATION_EXCEPTIONS:
                        if (subpageCode == 0)
                        {
                            uint8_t* informationExceptions = C_CAST(uint8_t*, safe_calloc_aligned(11, sizeof(uint8_t), device->os_info.minimumAlignment));
                            if (!informationExceptions)
                            {
                                break;
                            }
                            if (SUCCESS == scsi_Log_Sense_Cmd(device, false, LPC_CUMULATIVE_VALUES, pageCode, subpageCode, 0, informationExceptions, 11))
                            {
                                smartStatusRead = true;
                                if (informationExceptions[8] == 0)//if the ASC is 0, then no trip
                                {
                                    driveInfo->smartStatus = 0;
                                }
                                else//we have a trip condition...don't care what the specific trip is though
                                {
                                    driveInfo->smartStatus = 1;
                                }
                                if (!driveInfo->temperatureData.temperatureDataValid && informationExceptions[10] > 0)
                                {
                                    //temperature log page was not read, neither was environmental reporting, but we do have
                                    //a current temperature reading here, so use it
                                    driveInfo->temperatureData.temperatureDataValid = true;
                                    driveInfo->temperatureData.currentTemperature = informationExceptions[10];
                                }
                            }
                            else
                            {
                                driveInfo->smartStatus = 2;
                            }
                            safe_free_aligned(&informationExceptions);
                        }
                        break;
                    case SEAGATE_LP_FARM:
                        if (subpageCode == SEAGATE_FARM_SP_CURRENT)
                        {
                            //Currently only reading first parameter to check if this is the farm log.
                            //TODO: Expand this to read more info out of FARM
                            uint8_t* farmData = C_CAST(uint8_t*, safe_calloc_aligned(76, sizeof(uint8_t), device->os_info.minimumAlignment));
                            if (!farmData)
                            {
                                break;
                            }
                            if (SUCCESS == scsi_Log_Sense_Cmd(device, false, LPC_CUMULATIVE_VALUES, pageCode, subpageCode, 0, farmData, 76))
                            {
                                uint64_t farmSignature = M_BytesTo8ByteValue(farmData[4], farmData[5], farmData[6], farmData[7], farmData[8], farmData[9], farmData[10], farmData[11]);
                                if (farmSignature & BIT63 && farmSignature & BIT62 && (farmSignature & UINT64_C(0x00FFFFFFFFFFFF)) == SEAGATE_FARM_LOG_SIGNATURE)
                                {
                                    add_Feature_To_Supported_List(driveInfo->featuresSupported, &driveInfo->numberOfFeaturesSupported, "Field Accessible Reliability Metrics (FARM)");
                                }
                                //NOTE: If for any reason DOM was not already read from standard page, can read it here too
                            }
                            safe_free_aligned(&farmData);
                        }
                        break;
                    case 0x3C://Vendor specific page. we're checking this page on Seagate drives for an enhanced usage indicator on SSDs (PPM value)
                        if (is_Seagate_Family(device) == SEAGATE || is_Seagate_Family(device) == SEAGATE_VENDOR_A)
                        {
                            uint8_t* ssdUsage = C_CAST(uint8_t*, safe_calloc_aligned(12, sizeof(uint8_t), device->os_info.minimumAlignment));
                            if (!ssdUsage)
                            {
                                break;
                            }
                            if (SUCCESS == scsi_Log_Sense_Cmd(device, false, LPC_CUMULATIVE_VALUES, pageCode, 0, 0x8004, ssdUsage, 12))
                            {
                                driveInfo->percentEnduranceUsed = (C_CAST(double, M_BytesTo4ByteValue(ssdUsage[8], ssdUsage[9], ssdUsage[10], ssdUsage[11])) / 1000000.00) * 100.00;
                            }
                            safe_free_aligned(&ssdUsage);
                        }
                        break;
                    default:
                        break;
                    }
                }
                safe_free_aligned(&scsiLogBuf);
            }
        }
        if (!smartStatusRead)
        {
            //we didn't read the informational exceptions log page, so we need to set this to SMART status unknown
            driveInfo->smartStatus = 2;
        }
    }
    return ret;
}

static eReturnValues get_SCSI_Read_Capacity_Data(tDevice* device, ptrDriveInformationSAS_SATA driveInfo, ptrSCSIIdentifyInfo scsiInfo)
{
    eReturnValues ret = SUCCESS;
    if (device && driveInfo && scsiInfo)
    {
        uint8_t protectionTypeEnabled = 0;//default to type 0
    //read capacity data - try read capacity 10 first, then do a read capacity 16. This is to work around some USB bridges passing the command and returning no data.
        uint8_t* readCapBuf = C_CAST(uint8_t*, safe_calloc_aligned(READ_CAPACITY_10_LEN, sizeof(uint8_t), device->os_info.minimumAlignment));
        if (!readCapBuf)
        {
            return MEMORY_FAILURE;
        }
        switch (scsiInfo->peripheralDeviceType)
        {
        case PERIPHERAL_DIRECT_ACCESS_BLOCK_DEVICE:
        case PERIPHERAL_HOST_MANAGED_ZONED_BLOCK_DEVICE:
        case PERIPHERAL_SEQUENTIAL_ACCESS_BLOCK_DEVICE:
        case PERIPHERAL_SIMPLIFIED_DIRECT_ACCESS_DEVICE:
            if (SUCCESS == scsi_Read_Capacity_10(device, readCapBuf, READ_CAPACITY_10_LEN))
            {
                copy_Read_Capacity_Info(&driveInfo->logicalSectorSize, &driveInfo->physicalSectorSize, &driveInfo->maxLBA, &driveInfo->sectorAlignment, readCapBuf, false);
                if (scsiInfo->version > 3)//SPC2 and higher can reference SBC2 and higher which introduced read capacity 16
                {
                    //try a read capacity 16 anyways and see if the data from that was valid or not since that will give us a physical sector size whereas readcap10 data will not
                    uint8_t* temp = C_CAST(uint8_t*, safe_realloc_aligned(readCapBuf, READ_CAPACITY_10_LEN, READ_CAPACITY_16_LEN * sizeof(uint8_t), device->os_info.minimumAlignment));
                    if (!temp)
                    {
                        safe_free_aligned(&readCapBuf);
                        return MEMORY_FAILURE;
                    }
                    readCapBuf = temp;
                    memset(readCapBuf, 0, READ_CAPACITY_16_LEN);
                    if (SUCCESS == scsi_Read_Capacity_16(device, readCapBuf, READ_CAPACITY_16_LEN))
                    {
                        uint32_t logicalBlockSize = 0;
                        uint32_t physicalBlockSize = 0;
                        uint64_t maxLBA = 0;
                        uint16_t sectorAlignment = 0;
                        copy_Read_Capacity_Info(&logicalBlockSize, &physicalBlockSize, &maxLBA, &sectorAlignment, readCapBuf, true);
                        //some USB drives will return success and no data, so check if this local var is 0 or not...if not, we can use this data
                        if (maxLBA != 0)
                        {
                            driveInfo->logicalSectorSize = logicalBlockSize;
                            driveInfo->physicalSectorSize = physicalBlockSize;
                            driveInfo->maxLBA = maxLBA;
                            driveInfo->sectorAlignment = sectorAlignment;
                        }
                        if (scsiInfo->protectionSupported && readCapBuf[12] & BIT0)//protection enabled
                        {
                            switch (M_GETBITRANGE(readCapBuf[12], 3, 1))
                            {
                            case 0:
                                protectionTypeEnabled = 1;
                                break;
                            case 1:
                                protectionTypeEnabled = 2;
                                break;
                            case 2:
                                protectionTypeEnabled = 3;
                                break;
                            default:
                                break;
                            }
                        }
                    }
                    //check for format corrupt
                    uint8_t senseKey = 0;
                    uint8_t asc = 0;
                    uint8_t ascq = 0;
                    uint8_t fru = 0;
                    get_Sense_Key_ASC_ASCQ_FRU(device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, &senseKey, &asc, &ascq, &fru);
                    if (senseKey == SENSE_KEY_MEDIUM_ERROR && asc == 0x31 && ascq == 0)
                    {
                        if (!driveInfo->isFormatCorrupt)
                        {
                            add_Feature_To_Supported_List(driveInfo->featuresSupported, &driveInfo->numberOfFeaturesSupported, "Format Corrupt - not all features identifiable.");
                        }
                        driveInfo->isFormatCorrupt = true;
                    }
                }
            }
            else
            {
                //check for format corrupt first
                uint8_t senseKey = 0;
                uint8_t asc = 0;
                uint8_t ascq = 0;
                uint8_t fru = 0;
                get_Sense_Key_ASC_ASCQ_FRU(device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, &senseKey, &asc, &ascq, &fru);
                if (senseKey == SENSE_KEY_MEDIUM_ERROR && asc == 0x31 && ascq == 0)
                {
                    if (!driveInfo->isFormatCorrupt)
                    {
                        add_Feature_To_Supported_List(driveInfo->featuresSupported, &driveInfo->numberOfFeaturesSupported, "Format Corrupt - not all features identifiable.");
                    }
                    driveInfo->isFormatCorrupt = true;
                }

                //try read capacity 16, if that fails we are done trying
                uint8_t* temp = C_CAST(uint8_t*, safe_realloc_aligned(readCapBuf, READ_CAPACITY_10_LEN, READ_CAPACITY_16_LEN * sizeof(uint8_t), device->os_info.minimumAlignment));
                if (temp == M_NULLPTR)
                {
                    safe_free_aligned(&readCapBuf);
                    return MEMORY_FAILURE;
                }
                readCapBuf = temp;
                memset(readCapBuf, 0, READ_CAPACITY_16_LEN);
                if (SUCCESS == scsi_Read_Capacity_16(device, readCapBuf, READ_CAPACITY_16_LEN))
                {
                    copy_Read_Capacity_Info(&driveInfo->logicalSectorSize, &driveInfo->physicalSectorSize, &driveInfo->maxLBA, &driveInfo->sectorAlignment, readCapBuf, true);
                    if (scsiInfo->protectionSupported && readCapBuf[12] & BIT0)//protection enabled
                    {
                        switch (M_GETBITRANGE(readCapBuf[12], 3, 1))
                        {
                        case 0:
                            protectionTypeEnabled = 1;
                            break;
                        case 1:
                            protectionTypeEnabled = 2;
                            break;
                        case 2:
                            protectionTypeEnabled = 3;
                            break;
                        default:
                            break;
                        }
                    }
                }
                //check for format corrupt first
                senseKey = 0;
                asc = 0;
                ascq = 0;
                fru = 0;
                get_Sense_Key_ASC_ASCQ_FRU(device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, &senseKey, &asc, &ascq, &fru);
                if (senseKey == SENSE_KEY_MEDIUM_ERROR && asc == 0x31 && ascq == 0)
                {
                    if (!driveInfo->isFormatCorrupt)
                    {
                        add_Feature_To_Supported_List(driveInfo->featuresSupported, &driveInfo->numberOfFeaturesSupported, "Format Corrupt - not all features identifiable.");
                    }
                    driveInfo->isFormatCorrupt = true;
                }
            }
            break;
        default:
            break;
        }
        safe_free_aligned(&readCapBuf);
        if (scsiInfo->protectionSupported)
        {
            //set protection types supported up here.
            if (scsiInfo->protectionType1Supported)
            {
                if (protectionTypeEnabled == 1)
                {
                    add_Feature_To_Supported_List(driveInfo->featuresSupported, &driveInfo->numberOfFeaturesSupported, "Protection Type 1 [Enabled]");
                }
                else
                {
                    add_Feature_To_Supported_List(driveInfo->featuresSupported, &driveInfo->numberOfFeaturesSupported, "Protection Type 1");
                }

            }
            if (scsiInfo->protectionType2Supported)
            {
                if (protectionTypeEnabled == 2)
                {
                    add_Feature_To_Supported_List(driveInfo->featuresSupported, &driveInfo->numberOfFeaturesSupported, "Protection Type 2 [Enabled]");
                }
                else
                {
                    add_Feature_To_Supported_List(driveInfo->featuresSupported, &driveInfo->numberOfFeaturesSupported, "Protection Type 2");
                }

            }
            if (scsiInfo->protectionType3Supported)
            {
                if (protectionTypeEnabled == 3)
                {
                    add_Feature_To_Supported_List(driveInfo->featuresSupported, &driveInfo->numberOfFeaturesSupported, "Protection Type 3 [Enabled]");
                }
                else
                {
                    add_Feature_To_Supported_List(driveInfo->featuresSupported, &driveInfo->numberOfFeaturesSupported, "Protection Type 3");
                }

            }
        }
    }
    return ret;
}

static eReturnValues get_SCSI_Mode_Data(tDevice* device, ptrDriveInformationSAS_SATA driveInfo, ptrSCSIIdentifyInfo scsiInfo)
{
    eReturnValues ret = SUCCESS;
    if (device && driveInfo && scsiInfo)
    {
        if (!device->drive_info.passThroughHacks.scsiHacks.noModePages && (scsiInfo->version >= 2 || scsiInfo->ccs))
        {
            uint16_t numberOfPages = 0;
            uint16_t offset = 0;
            //create a list of mode pages (and any subpages) we care about reading and go through that list reading each one
            DECLARE_ZERO_INIT_ARRAY(uint8_t, listOfModePagesAndSubpages, 512);//allow 10 entries in the list...update the loop condition below if this is adjusted
            //format for page list is first byte = page, 2nd byte = subpage, then increment and look at the next page
            listOfModePagesAndSubpages[offset] = MP_READ_WRITE_ERROR_RECOVERY;//AWRE, ARRE
            offset += 2;
            if (device->drive_info.interface_type != USB_INTERFACE && device->drive_info.interface_type != IEEE_1394_INTERFACE && device->drive_info.interface_type != MMC_INTERFACE && device->drive_info.interface_type != SD_INTERFACE)
            {
                if (driveInfo->rotationRate == 0 && (scsiInfo->peripheralDeviceType == PERIPHERAL_DIRECT_ACCESS_BLOCK_DEVICE || scsiInfo->peripheralDeviceType == PERIPHERAL_HOST_MANAGED_ZONED_BLOCK_DEVICE || scsiInfo->peripheralDeviceType == PERIPHERAL_SIMPLIFIED_DIRECT_ACCESS_DEVICE))
                {
                    listOfModePagesAndSubpages[offset] = MP_RIGID_DISK_GEOMETRY;//To get medium rotation rate if we didn't already get it. This page is long obsolete
                    offset += 2;
                }
            }
            listOfModePagesAndSubpages[offset] = MP_CACHING;//WCE, DRA, NV_DIS?
            offset += 2;
            if (scsiInfo->version >= SCSI_VERSION_SPC_2)//control mode page didn't get long DST info until SPC2
            {
                listOfModePagesAndSubpages[offset] = MP_CONTROL;//Long DST Time
                offset += 2;
            }
            if (!device->drive_info.passThroughHacks.scsiHacks.noModeSubPages && scsiInfo->version >= SCSI_VERSION_SPC_3)//SPC3 added subpage codes
            {
                listOfModePagesAndSubpages[offset] = MP_CONTROL;//DLC
                listOfModePagesAndSubpages[offset + 1] = 0x01;
                offset += 2;
                if (device->drive_info.interface_type != USB_INTERFACE && device->drive_info.interface_type != IEEE_1394_INTERFACE && device->drive_info.interface_type != MMC_INTERFACE && device->drive_info.interface_type != SD_INTERFACE)
                {
                    //Command Duration Limits
                    if (scsiInfo->version >= SCSI_VERSION_SPC_5)
                    {
                        listOfModePagesAndSubpages[offset] = MP_CONTROL;
                        listOfModePagesAndSubpages[offset + 1] = 0x03;
                        offset += 2;
                    }
                    //Command Duration Limits T2
                    if (scsiInfo->version >= SCSI_VERSION_SPC_6)
                    {
                        listOfModePagesAndSubpages[offset] = MP_CONTROL;
                        listOfModePagesAndSubpages[offset + 1] = 0x07;
                        offset += 2;
                    }

                    //IO Advice hints is in SBC4
                    listOfModePagesAndSubpages[offset] = MP_CONTROL;//IO Advice Hints (can we read this page or not basically)
                    listOfModePagesAndSubpages[offset + 1] = 0x05;
                    offset += 2;
                }
                //From SAT spec
                listOfModePagesAndSubpages[offset] = MP_CONTROL;//PATA control (can PATA transfer speeds be changed)
                listOfModePagesAndSubpages[offset + 1] = 0xF1;
                offset += 2;
                //TODO: SAT version check for SAT-5
                listOfModePagesAndSubpages[offset] = MP_CONTROL;//feature control (SAT CDL)
                listOfModePagesAndSubpages[offset + 1] = 0xF2;
                offset += 2;
            }
            if (device->drive_info.interface_type != USB_INTERFACE && device->drive_info.interface_type != IEEE_1394_INTERFACE && device->drive_info.interface_type != MMC_INTERFACE && device->drive_info.interface_type != SD_INTERFACE)
            {
                if (scsiInfo->version >= SCSI_VERSION_SPC_2)//SPC2 added this page
                {
                    listOfModePagesAndSubpages[offset] = MP_PROTOCOL_SPECIFIC_PORT;//get interface type
                    listOfModePagesAndSubpages[offset + 1] = 0;
                    offset += 2;
                }
                if (!device->drive_info.passThroughHacks.scsiHacks.noModeSubPages && scsiInfo->version >= 5)//SPC3 added subpage codes
                {
                    listOfModePagesAndSubpages[offset] = MP_PROTOCOL_SPECIFIC_PORT;//get SAS phy speed
                    listOfModePagesAndSubpages[offset + 1] = 1;
                    offset += 2;
                }
            }
            if (scsiInfo->version >= SCSI_VERSION_SPC)//SPC added this page
            {
                listOfModePagesAndSubpages[offset] = MP_POWER_CONDTION;//EPC and older standby/idle timers
                listOfModePagesAndSubpages[offset + 1] = 0;
                offset += 2;
            }
            if (!device->drive_info.passThroughHacks.scsiHacks.noModeSubPages && scsiInfo->version >= SCSI_VERSION_SPC_3)//SPC3 added subpage codes
            {
                //ATA Advanced Power Management page from SAT2
                listOfModePagesAndSubpages[offset] = MP_POWER_CONDTION;//ATA APM
                listOfModePagesAndSubpages[offset + 1] = 0xF1;//reading this for the ATA APM settings (check if supported really)
                offset += 2;
            }
            if (scsiInfo->version >= SCSI_VERSION_SPC)//Added in SPC
            {
                listOfModePagesAndSubpages[offset] = MP_INFORMATION_EXCEPTIONS_CONTROL;//SMART/informational exceptions & MRIE value. Dexcept? Warnings?
                listOfModePagesAndSubpages[offset + 1] = 0;
                offset += 2;
            }
            if (device->drive_info.interface_type != USB_INTERFACE && device->drive_info.interface_type != IEEE_1394_INTERFACE && device->drive_info.interface_type != MMC_INTERFACE && device->drive_info.interface_type != SD_INTERFACE)
            {
                if (!device->drive_info.passThroughHacks.scsiHacks.noModeSubPages && scsiInfo->version >= SCSI_VERSION_SPC_3)//SPC3 added subpage codes
                {
                    listOfModePagesAndSubpages[offset] = MP_BACKGROUND_CONTROL;//EN_BMS, EN_PS
                    listOfModePagesAndSubpages[offset + 1] = 0x01;
                    offset += 2;
                }
            }
            numberOfPages = offset / 2;
            uint16_t modeIter = 0;
            uint8_t protocolIdentifier = 0;
            for (uint16_t pageCounter = 0; modeIter < offset && pageCounter < numberOfPages && !device->drive_info.passThroughHacks.scsiHacks.noModePages; modeIter += 2, ++pageCounter)
            {
                uint8_t pageCode = listOfModePagesAndSubpages[modeIter];
                uint8_t subPageCode = listOfModePagesAndSubpages[modeIter + 1];
                switch (pageCode)
                {
                case MP_READ_WRITE_ERROR_RECOVERY:
                    switch (subPageCode)
                    {
                    case 0:
                        //check if AWRE and ARRE are supported or can be changed before checking if they are enabled or not.
                    {
                        char* awreString = M_NULLPTR;
                        char* arreString = M_NULLPTR;
                        uint32_t awreStringLength = 0;
                        uint32_t arreStringLength = 0;
                        DECLARE_ZERO_INIT_ARRAY(uint8_t, readWriteErrorRecovery, 12 + SCSI_MODE_PAGE_MIN_HEADER_LENGTH);//need to include header length in this
                        bool pageRead = false;
                        bool defaultsRead = false;
                        bool sixByte = false;
                        uint16_t headerLength = 0;
                        if (SUCCESS == get_SCSI_Mode_Page(device, MPC_DEFAULT_VALUES, pageCode, subPageCode, M_NULLPTR, M_NULLPTR, true, readWriteErrorRecovery, 12 + SCSI_MODE_PAGE_MIN_HEADER_LENGTH, M_NULLPTR, &sixByte))
                        {
                            uint16_t blockDescLen = 0;
                            defaultsRead = true;
                            if (sixByte)
                            {
                                headerLength = MODE_PARAMETER_HEADER_6_LEN;
                                blockDescLen = readWriteErrorRecovery[2];
                            }
                            else
                            {
                                headerLength = MODE_PARAMETER_HEADER_10_LEN;
                                blockDescLen = M_BytesTo2ByteValue(readWriteErrorRecovery[6], readWriteErrorRecovery[7]);
                            }
                            headerLength += blockDescLen;
                        }
                        if (defaultsRead)
                        {
                            //awre
                            if (readWriteErrorRecovery[headerLength + 2] & BIT7)
                            {
                                if (!awreString)
                                {
                                    awreStringLength = 30;
                                    awreString = C_CAST(char*, safe_calloc(awreStringLength, sizeof(char)));
                                }
                                else
                                {
                                    awreStringLength = 30;
                                    char* temp = C_CAST(char*, safe_realloc(awreString, awreStringLength * sizeof(char)));
                                    if (temp)
                                    {
                                        awreString = temp;
                                        memset(awreString, 0, awreStringLength);
                                    }
                                }
                                if (awreString && awreStringLength >= 30)
                                {
                                    snprintf(awreString, awreStringLength, "Automatic Write Reassignment");
                                }
                            }
                            //arre
                            if (readWriteErrorRecovery[headerLength + 2] & BIT6)
                            {
                                if (!arreString)
                                {
                                    arreStringLength = 30;
                                    arreString = C_CAST(char*, safe_calloc(arreStringLength, sizeof(char)));
                                }
                                else
                                {
                                    arreStringLength = 30;
                                    char* temp = C_CAST(char*, safe_realloc(arreString, arreStringLength * sizeof(char)));
                                    if (temp)
                                    {
                                        arreString = temp;
                                        memset(arreString, 0, arreStringLength);
                                    }
                                }
                                if (arreString && arreStringLength >= 30)
                                {
                                    snprintf(arreString, arreStringLength, "Automatic Read Reassignment");
                                }
                            }
                        }
                        if (SUCCESS == get_SCSI_Mode_Page(device, MPC_CURRENT_VALUES, pageCode, subPageCode, M_NULLPTR, M_NULLPTR, true, readWriteErrorRecovery, 12 + SCSI_MODE_PAGE_MIN_HEADER_LENGTH, M_NULLPTR, &sixByte))
                        {
                            uint16_t blockDescLen = 0;
                            pageRead = true;
                            if (sixByte)
                            {
                                headerLength = MODE_PARAMETER_HEADER_6_LEN;
                                blockDescLen = readWriteErrorRecovery[2];
                                if (readWriteErrorRecovery[2] & BIT7)
                                {
                                    driveInfo->isWriteProtected = true;
                                }
                            }
                            else
                            {
                                headerLength = MODE_PARAMETER_HEADER_10_LEN;
                                blockDescLen = M_BytesTo2ByteValue(readWriteErrorRecovery[6], readWriteErrorRecovery[7]);
                                if (readWriteErrorRecovery[3] & BIT7)
                                {
                                    driveInfo->isWriteProtected = true;
                                }
                            }
                            headerLength += blockDescLen;
                        }
                        if (pageRead)
                        {
                            //awre
                            if (readWriteErrorRecovery[headerLength + 2] & BIT7)
                            {
                                if (!awreString)
                                {
                                    awreStringLength = 40;
                                    awreString = C_CAST(char*, safe_calloc(awreStringLength, sizeof(char)));
                                }
                                else
                                {
                                    awreStringLength = 40;
                                    char* temp = C_CAST(char*, safe_realloc(awreString, awreStringLength * sizeof(char)));
                                    if (temp)
                                    {
                                        awreString = temp;
                                        memset(awreString, 0, awreStringLength);
                                    }
                                }
                                if (awreString && awreStringLength >= 40)
                                {
                                    snprintf(awreString, awreStringLength, "Automatic Write Reassignment [Enabled]");
                                }
                            }
                            //arre
                            if (readWriteErrorRecovery[headerLength + 2] & BIT6)
                            {
                                if (!arreString)
                                {
                                    arreStringLength = 40;
                                    arreString = C_CAST(char*, safe_calloc(arreStringLength, sizeof(char)));
                                }
                                else
                                {
                                    arreStringLength = 40;
                                    char* temp = C_CAST(char*, safe_realloc(arreString, arreStringLength * sizeof(char)));
                                    if (temp)
                                    {
                                        arreString = temp;
                                        memset(arreString, 0, arreStringLength);
                                    }
                                }
                                if (arreString && arreStringLength >= 40)
                                {
                                    snprintf(arreString, arreStringLength, "Automatic Read Reassignment [Enabled]");
                                }
                            }
                        }
                        if (awreString)
                        {
                            add_Feature_To_Supported_List(driveInfo->featuresSupported, &driveInfo->numberOfFeaturesSupported, awreString);

                        }
                        if (arreString)
                        {
                            add_Feature_To_Supported_List(driveInfo->featuresSupported, &driveInfo->numberOfFeaturesSupported, arreString);

                        }
                        safe_free(&awreString);
                        safe_free(&arreString);
                    }
                    break;
                    default:
                        break;
                    }
                    break;
                case MP_RIGID_DISK_GEOMETRY:
                    switch (subPageCode)
                    {
                    case 0:
                        if (driveInfo->rotationRate == 0)
                        {
                            DECLARE_ZERO_INIT_ARRAY(uint8_t, rigidGeometry, 24 + SCSI_MODE_PAGE_MIN_HEADER_LENGTH);
                            bool pageRead = false;
                            bool sixByte = false;
                            uint16_t headerLength = 0;
                            if (SUCCESS == get_SCSI_Mode_Page(device, MPC_CURRENT_VALUES, pageCode, subPageCode, M_NULLPTR, M_NULLPTR, true, rigidGeometry, 24 + SCSI_MODE_PAGE_MIN_HEADER_LENGTH, M_NULLPTR, &sixByte))
                            {
                                uint16_t blockDescLen = 0;
                                pageRead = true;
                                if (sixByte)
                                {
                                    headerLength = MODE_PARAMETER_HEADER_6_LEN;
                                    blockDescLen = rigidGeometry[2];
                                    if (rigidGeometry[2] & BIT7)
                                    {
                                        driveInfo->isWriteProtected = true;
                                    }
                                }
                                else
                                {
                                    headerLength = MODE_PARAMETER_HEADER_10_LEN;
                                    blockDescLen = M_BytesTo2ByteValue(rigidGeometry[6], rigidGeometry[7]);
                                    if (rigidGeometry[3] & BIT7)
                                    {
                                        driveInfo->isWriteProtected = true;
                                    }
                                }
                                headerLength += blockDescLen;
                            }
                            if (pageRead)
                            {
                                driveInfo->rotationRate = M_BytesTo2ByteValue(rigidGeometry[headerLength + 20], rigidGeometry[headerLength + 21]);
                            }
                        }
                        break;
                    default:
                        break;
                    }
                    break;
                case MP_CACHING:
                    switch (subPageCode)
                    {
                    case 0:
                    {
                        DECLARE_ZERO_INIT_ARRAY(uint8_t, cachingPage, 20 + SCSI_MODE_PAGE_MIN_HEADER_LENGTH);//need to include header length in this
                        bool pageRead = false;
                        bool sixByte = false;
                        uint16_t headerLength = 0;
                        if (SUCCESS == get_SCSI_Mode_Page(device, MPC_CURRENT_VALUES, pageCode, subPageCode, M_NULLPTR, M_NULLPTR, true, cachingPage, 20 + SCSI_MODE_PAGE_MIN_HEADER_LENGTH, M_NULLPTR, &sixByte))
                        {
                            uint16_t blockDescLen = 0;
                            pageRead = true;
                            if (sixByte)
                            {
                                headerLength = MODE_PARAMETER_HEADER_6_LEN;
                                blockDescLen = cachingPage[2];
                                if (cachingPage[2] & BIT7)
                                {
                                    driveInfo->isWriteProtected = true;
                                }
                            }
                            else
                            {
                                headerLength = MODE_PARAMETER_HEADER_10_LEN;
                                blockDescLen = M_BytesTo2ByteValue(cachingPage[6], cachingPage[7]);
                                if (cachingPage[3] & BIT7)
                                {
                                    driveInfo->isWriteProtected = true;
                                }
                            }
                            headerLength += blockDescLen;
                        }
                        if (pageRead)
                        {
                            //NV_DIS
                            driveInfo->nvCacheEnabled = !M_ToBool(cachingPage[headerLength + 13] & BIT0);//bit being set means disabled the cache, being set to 0 means cache is enabled.

                            //WCE
                            driveInfo->writeCacheEnabled = M_ToBool(cachingPage[headerLength + 2] & BIT2);
                            if (driveInfo->writeCacheEnabled)
                            {
                                driveInfo->writeCacheSupported = true;
                            }
                            //DRA
                            driveInfo->readLookAheadEnabled = !M_ToBool(cachingPage[headerLength + 12] & BIT5);
                            if (driveInfo->readLookAheadEnabled)
                            {
                                driveInfo->readLookAheadSupported = true;
                            }
                            //check for supported if it's not already set
                            if (!driveInfo->writeCacheSupported || !driveInfo->readLookAheadSupported)
                            {
                                //we didn't get is supported from above, so check the changable page
                                memset(cachingPage, 0, 20 + SCSI_MODE_PAGE_MIN_HEADER_LENGTH);
                                pageRead = false;//reset to false before reading the changable values page
                                if (SUCCESS == get_SCSI_Mode_Page(device, MPC_CHANGABLE_VALUES, pageCode, subPageCode, M_NULLPTR, M_NULLPTR, true, cachingPage, 20 + SCSI_MODE_PAGE_MIN_HEADER_LENGTH, M_NULLPTR, &sixByte))
                                {
                                    uint16_t blockDescLen = 0;
                                    pageRead = true;
                                    if (sixByte)
                                    {
                                        headerLength = MODE_PARAMETER_HEADER_6_LEN;
                                        blockDescLen = cachingPage[2];
                                        if (cachingPage[2] & BIT7)
                                        {
                                            driveInfo->isWriteProtected = true;
                                        }
                                    }
                                    else
                                    {
                                        headerLength = MODE_PARAMETER_HEADER_10_LEN;
                                        blockDescLen = M_BytesTo2ByteValue(cachingPage[6], cachingPage[7]);
                                        if (cachingPage[3] & BIT7)
                                        {
                                            driveInfo->isWriteProtected = true;
                                        }
                                    }
                                    headerLength += blockDescLen;
                                }
                                if (pageRead)
                                {
                                    //changable dictates if a bit can be changed or not. So unlike above where it is indicating state, this indicates if it is supported or can be changed at all from the current state
                                    driveInfo->writeCacheSupported = M_ToBool(cachingPage[headerLength + 2] & BIT2);
                                    driveInfo->readLookAheadSupported = M_ToBool(cachingPage[headerLength + 12] & BIT5);
                                }
                            }
                        }
                    }
                    break;
                    default:
                        break;
                    }
                    break;
                case MP_CONTROL:
                    switch (subPageCode)
                    {
                    case 0://control mode page. No subpage
                    {
                        DECLARE_ZERO_INIT_ARRAY(uint8_t, controlPage, MP_CONTROL_LEN + SCSI_MODE_PAGE_MIN_HEADER_LENGTH);//need to include header length in this
                        bool pageRead = false;
                        bool sixByte = false;
                        uint16_t headerLength = 0;
                        if (SUCCESS == get_SCSI_Mode_Page(device, MPC_CURRENT_VALUES, pageCode, subPageCode, M_NULLPTR, M_NULLPTR, true, controlPage, MP_CONTROL_LEN + SCSI_MODE_PAGE_MIN_HEADER_LENGTH, M_NULLPTR, &sixByte))
                        {
                            uint16_t blockDescLen = 0;
                            pageRead = true;
                            if (sixByte)
                            {
                                headerLength = MODE_PARAMETER_HEADER_6_LEN;
                                blockDescLen = controlPage[2];
                                if (controlPage[2] & BIT7)
                                {
                                    driveInfo->isWriteProtected = true;
                                }
                            }
                            else
                            {
                                headerLength = MODE_PARAMETER_HEADER_10_LEN;
                                blockDescLen = M_BytesTo2ByteValue(controlPage[6], controlPage[7]);
                                if (controlPage[3] & BIT7)
                                {
                                    driveInfo->isWriteProtected = true;
                                }
                            }
                            headerLength += blockDescLen;
                        }
                        if (pageRead)
                        {
                            //check the page code and page length
                            if (M_GETBITRANGE(controlPage[headerLength + 0], 5, 0) == MP_CONTROL)
                            {
                                //check length since the page needs to be long enough for this data. Earlier specs this page was shorter
                                if (controlPage[headerLength + 1] == 0x0A)
                                {
                                    if (driveInfo->longDSTTimeMinutes == 0)//checking for zero since we may have already gotten this from the Extended Inquiry VPD page
                                    {
                                        driveInfo->longDSTTimeMinutes = ((C_CAST(uint64_t, M_BytesTo2ByteValue(controlPage[headerLength + 10], controlPage[headerLength + 11])) + UINT64_C(60)) - UINT64_C(1)) / UINT64_C(60);//rounding up to nearest minute
                                    }
                                }
                            }
                        }
                    }
                    break;
                    case 1://controlExtension
                    {
                        //check if DLC is supported or can be changed before checking if they are enabled or not.
                        char* dlcString = M_NULLPTR;
                        uint32_t dlcStringLength = 0;
                        DECLARE_ZERO_INIT_ARRAY(uint8_t, controlExtensionPage, MP_CONTROL_EXTENSION_LEN + SCSI_MODE_PAGE_MIN_HEADER_LENGTH);//need to include header length in this
                        bool pageRead = false;
                        bool defaultsRead = false;
                        bool sixByte = false;
                        uint16_t headerLength = 0;
                        if (SUCCESS == get_SCSI_Mode_Page(device, MPC_DEFAULT_VALUES, pageCode, subPageCode, M_NULLPTR, M_NULLPTR, true, controlExtensionPage, MP_CONTROL_EXTENSION_LEN + SCSI_MODE_PAGE_MIN_HEADER_LENGTH, M_NULLPTR, &sixByte))
                        {
                            uint16_t blockDescLen = 0;
                            defaultsRead = true;
                            if (sixByte)
                            {
                                headerLength = MODE_PARAMETER_HEADER_6_LEN;
                                blockDescLen = controlExtensionPage[2];
                            }
                            else
                            {
                                headerLength = MODE_PARAMETER_HEADER_10_LEN;
                                blockDescLen = M_BytesTo2ByteValue(controlExtensionPage[6], controlExtensionPage[7]);
                            }
                            headerLength += blockDescLen;
                        }
                        if (defaultsRead)
                        {
                            //dlc
                            if (controlExtensionPage[headerLength + 4] & BIT3)
                            {
                                if (!dlcString)
                                {
                                    dlcStringLength = 50;
                                    dlcString = C_CAST(char*, safe_calloc(dlcStringLength, sizeof(char)));
                                }
                                else
                                {
                                    dlcStringLength = 50;
                                    char* temp = C_CAST(char*, safe_realloc(dlcString, dlcStringLength * sizeof(char)));
                                    if (temp)
                                    {
                                        dlcString = temp;
                                        memset(dlcString, 0, 50);
                                    }
                                }
                                if (dlcString && dlcStringLength >= 50)
                                {
                                    snprintf(dlcString, dlcStringLength, "Device Life Control");
                                }
                            }
                        }
                        if (SUCCESS == get_SCSI_Mode_Page(device, MPC_CURRENT_VALUES, pageCode, subPageCode, M_NULLPTR, M_NULLPTR, true, controlExtensionPage, MP_CONTROL_EXTENSION_LEN + SCSI_MODE_PAGE_MIN_HEADER_LENGTH, M_NULLPTR, &sixByte))
                        {
                            uint16_t blockDescLen = 0;
                            pageRead = true;
                            if (sixByte)
                            {
                                headerLength = MODE_PARAMETER_HEADER_6_LEN;
                                blockDescLen = controlExtensionPage[2];
                                if (controlExtensionPage[2] & BIT7)
                                {
                                    driveInfo->isWriteProtected = true;
                                }
                            }
                            else
                            {
                                headerLength = MODE_PARAMETER_HEADER_10_LEN;
                                blockDescLen = M_BytesTo2ByteValue(controlExtensionPage[6], controlExtensionPage[7]);
                                if (controlExtensionPage[3] & BIT7)
                                {
                                    driveInfo->isWriteProtected = true;
                                }
                            }
                            headerLength += blockDescLen;
                        }
                        if (pageRead)
                        {
                            //dlc
                            if (controlExtensionPage[headerLength + 4] & BIT3)
                            {
                                if (!dlcString)
                                {
                                    dlcStringLength = 50;
                                    dlcString = C_CAST(char*, safe_calloc(dlcStringLength, sizeof(char)));
                                }
                                else
                                {
                                    dlcStringLength = 50;
                                    char* temp = C_CAST(char*, safe_realloc(dlcString, dlcStringLength * sizeof(char)));
                                    if (temp)
                                    {
                                        dlcString = temp;
                                        memset(dlcString, 0, 50);
                                    }
                                }
                                if (dlcString && dlcStringLength >= 50)
                                {
                                    snprintf(dlcString, dlcStringLength, "Device Life Control [Enabled]");
                                }
                            }
                        }
                        if (dlcString)
                        {
                            add_Feature_To_Supported_List(driveInfo->featuresSupported, &driveInfo->numberOfFeaturesSupported, dlcString);
                        }
                        safe_free(&dlcString);
                    }
                    break;
                    case 0x03://CDL A
                    //case 0x04://CDL B
                    {
                        DECLARE_ZERO_INIT_ARRAY(uint8_t, cdl, 36 + SCSI_MODE_PAGE_MIN_HEADER_LENGTH);//need to include header length in this
                        bool sixByte = false;
                        //uint16_t headerLength = 0;
                        if (SUCCESS == get_SCSI_Mode_Page(device, MPC_CURRENT_VALUES, pageCode, subPageCode, M_NULLPTR, M_NULLPTR, true, cdl, 36 + SCSI_MODE_PAGE_MIN_HEADER_LENGTH, M_NULLPTR, &sixByte))
                        {
                            add_Feature_To_Supported_List(driveInfo->featuresSupported, &driveInfo->numberOfFeaturesSupported, "Command Duration Limits");
                        }
                    }
                    break;
                    case 0x05://IO Advice Hints
                    {
                        DECLARE_ZERO_INIT_ARRAY(uint8_t, ioAdviceHints, 1040 + SCSI_MODE_PAGE_MIN_HEADER_LENGTH);//need to include header length in this
                        bool pageRead = false;
                        bool sixByte = false;
                        uint16_t headerLength = 0;
                        if (SUCCESS == get_SCSI_Mode_Page(device, MPC_CURRENT_VALUES, pageCode, subPageCode, M_NULLPTR, M_NULLPTR, true, ioAdviceHints, 1040 + SCSI_MODE_PAGE_MIN_HEADER_LENGTH, M_NULLPTR, &sixByte))
                        {
                            uint16_t blockDescLen = 0;
                            pageRead = true;
                            if (sixByte)
                            {
                                headerLength = MODE_PARAMETER_HEADER_6_LEN;
                                blockDescLen = ioAdviceHints[2];
                                if (ioAdviceHints[2] & BIT7)
                                {
                                    driveInfo->isWriteProtected = true;
                                }
                            }
                            else
                            {
                                headerLength = MODE_PARAMETER_HEADER_10_LEN;
                                blockDescLen = M_BytesTo2ByteValue(ioAdviceHints[6], ioAdviceHints[7]);
                                if (ioAdviceHints[3] & BIT7)
                                {
                                    driveInfo->isWriteProtected = true;
                                }
                            }
                            headerLength += blockDescLen;
                        }
                        if (pageRead)
                        {
                            //check if any of the Hints valid bits are set so we know it is enabled. TODO: add checking for the cache enabled bit?
                            bool valid = false;
                            for (uint32_t iter = headerLength + 15; iter < C_CAST(uint32_t, (UINT16_C(1040) + headerLength)); iter += 16)
                            {
                                uint8_t hintsMode = (ioAdviceHints[0] & 0xC0) >> 6;
                                if (hintsMode == 0)
                                {
                                    valid = true;
                                    break;//we found at least one, so get out of the loop.
                                }
                            }
                            if (valid)
                            {
                                add_Feature_To_Supported_List(driveInfo->featuresSupported, &driveInfo->numberOfFeaturesSupported, "IO Advice Hints [Enabled]");
                            }
                            else
                            {
                                add_Feature_To_Supported_List(driveInfo->featuresSupported, &driveInfo->numberOfFeaturesSupported, "IO Advice Hints");

                            }
                        }
                    }
                    break;
                    case 0x07://CDL T2 A
                    //case 0x08://CDL T2 B
                    {
                        DECLARE_ZERO_INIT_ARRAY(uint8_t, cdl, 232 + SCSI_MODE_PAGE_MIN_HEADER_LENGTH);//need to include header length in this
                        bool sixByte = false;
                        //uint16_t headerLength = 0;
                        if (SUCCESS == get_SCSI_Mode_Page(device, MPC_CURRENT_VALUES, pageCode, subPageCode, M_NULLPTR, M_NULLPTR, true, cdl, 232 + SCSI_MODE_PAGE_MIN_HEADER_LENGTH, M_NULLPTR, &sixByte))
                        {
                            add_Feature_To_Supported_List(driveInfo->featuresSupported, &driveInfo->numberOfFeaturesSupported, "Command Duration Limits T2");
                        }
                    }
                    break;
                    case 0xF1://PATA control
                        //if we can read this page, then the device supports PATA Control
                    {
                        DECLARE_ZERO_INIT_ARRAY(uint8_t, pataControl, 8 + SCSI_MODE_PAGE_MIN_HEADER_LENGTH);//need to include header length in this
                        //bool pageRead = false, 
                        bool sixByte = false;
                        //uint16_t headerLength = 0;
                        if (SUCCESS == get_SCSI_Mode_Page(device, MPC_CURRENT_VALUES, pageCode, subPageCode, M_NULLPTR, M_NULLPTR, true, pataControl, 8 + SCSI_MODE_PAGE_MIN_HEADER_LENGTH, M_NULLPTR, &sixByte))
                        {
                            // uint16_t blockDescLen = 0;
                            // pageRead = true;
                            // if (sixByte)
                            // {
                            //     headerLength = MODE_PARAMETER_HEADER_6_LEN;
                            //     blockDescLen = pataControl[2];
                            //     if (pataControl[2] & BIT7)
                            //     {
                            //         driveInfo->isWriteProtected = true;
                            //     }
                            // }
                            // else
                            // {
                            //     headerLength = MODE_PARAMETER_HEADER_10_LEN;
                            //     blockDescLen = M_BytesTo2ByteValue(pataControl[6], pataControl[7]);
                            //     if (pataControl[3] & BIT7)
                            //     {
                            //         driveInfo->isWriteProtected = true;
                            //     }
                            // }
                            // headerLength += blockDescLen;
                            add_Feature_To_Supported_List(driveInfo->featuresSupported, &driveInfo->numberOfFeaturesSupported, "PATA Control");
                        }
                    }
                    break;
                    case 0xF2://ATA feature control (CDL)
                    {
                        DECLARE_ZERO_INIT_ARRAY(uint8_t, ataFeatureControl, 16 + SCSI_MODE_PAGE_MIN_HEADER_LENGTH);//need to include header length in this
                        //bool pageRead = false;
                        bool sixByte = false;
                        uint16_t headerLength = 0;
                        if (SUCCESS == get_SCSI_Mode_Page(device, MPC_CURRENT_VALUES, pageCode, subPageCode, M_NULLPTR, M_NULLPTR, true, ataFeatureControl, 16 + SCSI_MODE_PAGE_MIN_HEADER_LENGTH, M_NULLPTR, &sixByte))
                        {
                            uint16_t blockDescLen = 0;
                            //pageRead = true;
                            if (sixByte)
                            {
                                headerLength = MODE_PARAMETER_HEADER_6_LEN;
                                blockDescLen = ataFeatureControl[2];
                                if (ataFeatureControl[2] & BIT7)
                                {
                                    driveInfo->isWriteProtected = true;
                                }
                            }
                            else
                            {
                                headerLength = MODE_PARAMETER_HEADER_10_LEN;
                                blockDescLen = M_BytesTo2ByteValue(ataFeatureControl[6], ataFeatureControl[7]);
                                if (ataFeatureControl[3] & BIT7)
                                {
                                    driveInfo->isWriteProtected = true;
                                }
                            }
                            headerLength += blockDescLen;
                            if (M_GETBITRANGE(ataFeatureControl[headerLength + 4], 2, 0))
                            {
                                add_Feature_To_Supported_List(driveInfo->featuresSupported, &driveInfo->numberOfFeaturesSupported, "SATA Command Duration Limit [Enabled]");
                            }
                        }
                    }
                    break;
                    default:
                        break;
                    }
                    break;
                case MP_PROTOCOL_SPECIFIC_PORT:
                    switch (subPageCode)
                    {
                    case 0x00://Protocol specific port (Use this to get whether SAS or FC or SCSI, etc)
                    {
                        DECLARE_ZERO_INIT_ARRAY(uint8_t, protocolSpecificPort, LEGACY_DRIVE_SEC_SIZE + SCSI_MODE_PAGE_MIN_HEADER_LENGTH);//need to include header length in this
                        bool pageRead = false;
                        bool sixByte = false;
                        uint16_t headerLength = 0;
                        if (SUCCESS == get_SCSI_Mode_Page(device, MPC_CURRENT_VALUES, pageCode, subPageCode, M_NULLPTR, M_NULLPTR, true, protocolSpecificPort, LEGACY_DRIVE_SEC_SIZE + SCSI_MODE_PAGE_MIN_HEADER_LENGTH, M_NULLPTR, &sixByte))
                        {
                            uint16_t blockDescLen = 0;
                            pageRead = true;
                            if (sixByte)
                            {
                                headerLength = MODE_PARAMETER_HEADER_6_LEN;
                                blockDescLen = protocolSpecificPort[2];
                                if (protocolSpecificPort[2] & BIT7)
                                {
                                    driveInfo->isWriteProtected = true;
                                }
                            }
                            else
                            {
                                headerLength = MODE_PARAMETER_HEADER_10_LEN;
                                blockDescLen = M_BytesTo2ByteValue(protocolSpecificPort[6], protocolSpecificPort[7]);
                                if (protocolSpecificPort[3] & BIT7)
                                {
                                    driveInfo->isWriteProtected = true;
                                }
                            }
                            headerLength += blockDescLen;
                        }
                        if (pageRead)
                        {
                            protocolIdentifier = M_Nibble0(protocolSpecificPort[headerLength + 2]);
                        }
                    }
                    break;
                    case 0x01://Phy control and discover mode page (SAS)
                    {
                        DECLARE_ZERO_INIT_ARRAY(uint8_t, protocolSpecificPort, LEGACY_DRIVE_SEC_SIZE + SCSI_MODE_PAGE_MIN_HEADER_LENGTH);//need to include header length in this
                        bool pageRead = false;
                        bool sixByte = false;
                        uint16_t headerLength = 0;
                        if (SUCCESS == get_SCSI_Mode_Page(device, MPC_CURRENT_VALUES, pageCode, subPageCode, M_NULLPTR, M_NULLPTR, true, protocolSpecificPort, LEGACY_DRIVE_SEC_SIZE + SCSI_MODE_PAGE_MIN_HEADER_LENGTH, M_NULLPTR, &sixByte))
                        {
                            uint16_t blockDescLen = 0;
                            pageRead = true;
                            if (sixByte)
                            {
                                headerLength = MODE_PARAMETER_HEADER_6_LEN;
                                blockDescLen = protocolSpecificPort[2];
                                if (protocolSpecificPort[2] & BIT7)
                                {
                                    driveInfo->isWriteProtected = true;
                                }
                            }
                            else
                            {
                                headerLength = MODE_PARAMETER_HEADER_10_LEN;
                                blockDescLen = M_BytesTo2ByteValue(protocolSpecificPort[6], protocolSpecificPort[7]);
                                if (protocolSpecificPort[3] & BIT7)
                                {
                                    driveInfo->isWriteProtected = true;
                                }
                            }
                            headerLength += blockDescLen;
                        }
                        if (pageRead)
                        {
                            protocolIdentifier = M_Nibble0(protocolSpecificPort[headerLength + 5]);
                            switch (protocolIdentifier)
                            {
                            case 0x0://Fiber Channel
                                driveInfo->interfaceSpeedInfo.speedIsValid = true;
                                driveInfo->interfaceSpeedInfo.speedType = INTERFACE_SPEED_FIBRE;
                                break;
                            case 0x1://parallel scsi
                            case 0x2://serial storage architecture scsi-3 protocol
                            case 0x3://IEEE 1394
                            case 0x4://RDMA 
                            case 0x5://iSCSI
                                break;
                            case 0x6:
                            {
                                driveInfo->interfaceSpeedInfo.speedIsValid = true;
                                driveInfo->interfaceSpeedInfo.speedType = INTERFACE_SPEED_SERIAL;
                                uint16_t phyDescriptorIter = headerLength + 8;
                                uint16_t phyPageLen = M_BytesTo2ByteValue(protocolSpecificPort[headerLength + 2], protocolSpecificPort[headerLength + 3]);
                                driveInfo->interfaceSpeedInfo.serialSpeed.numberOfPorts = protocolSpecificPort[headerLength + 7];
                                uint8_t phyCount = 0;
                                //now we need to go through the descriptors for each phy
                                for (; phyDescriptorIter < C_CAST(uint16_t, M_Min(C_CAST(uint16_t, phyPageLen + headerLength), C_CAST(uint16_t, LEGACY_DRIVE_SEC_SIZE + headerLength))) && phyCount < C_CAST(uint8_t, MAX_PORTS); phyDescriptorIter += 48, phyCount++)
                                {
                                    //uint8_t phyIdentifier = modePages[phyDescriptorIter + 1];
                                    switch (M_Nibble0(protocolSpecificPort[phyDescriptorIter + 5]))
                                    {
                                    case 0x8://1.5 Gb/s
                                        driveInfo->interfaceSpeedInfo.serialSpeed.portSpeedsNegotiated[phyCount] = 1;
                                        break;
                                    case 0x9://3.0 Gb/s
                                        driveInfo->interfaceSpeedInfo.serialSpeed.portSpeedsNegotiated[phyCount] = 2;
                                        break;
                                    case 0xA://6.0 Gb/s
                                        driveInfo->interfaceSpeedInfo.serialSpeed.portSpeedsNegotiated[phyCount] = 3;
                                        break;
                                    case 0xB://12.0 Gb/s
                                        driveInfo->interfaceSpeedInfo.serialSpeed.portSpeedsNegotiated[phyCount] = 4;
                                        break;
                                    case 0xC:
                                        driveInfo->interfaceSpeedInfo.serialSpeed.portSpeedsNegotiated[phyCount] = 5;
                                        break;
                                    case 0xD:
                                        driveInfo->interfaceSpeedInfo.serialSpeed.portSpeedsNegotiated[phyCount] = 6;
                                        break;
                                    case 0xE:
                                        driveInfo->interfaceSpeedInfo.serialSpeed.portSpeedsNegotiated[phyCount] = 7;
                                        break;
                                    case 0xF:
                                        driveInfo->interfaceSpeedInfo.serialSpeed.portSpeedsNegotiated[phyCount] = 8;
                                        break;
                                    default:
                                        driveInfo->interfaceSpeedInfo.serialSpeed.portSpeedsNegotiated[phyCount] = 0;
                                        break;
                                    }
                                    switch (M_Nibble0(protocolSpecificPort[phyDescriptorIter + 33]))
                                    {
                                    case 0x8://1.5 Gb/s
                                        driveInfo->interfaceSpeedInfo.serialSpeed.portSpeedsMax[phyCount] = 1;
                                        break;
                                    case 0x9://3.0 Gb/s
                                        driveInfo->interfaceSpeedInfo.serialSpeed.portSpeedsMax[phyCount] = 2;
                                        break;
                                    case 0xA://6.0 Gb/s
                                        driveInfo->interfaceSpeedInfo.serialSpeed.portSpeedsMax[phyCount] = 3;
                                        break;
                                    case 0xB://12.0 Gb/s
                                        driveInfo->interfaceSpeedInfo.serialSpeed.portSpeedsMax[phyCount] = 4;
                                        break;
                                    case 0xC://22.5 Gb/s
                                        driveInfo->interfaceSpeedInfo.serialSpeed.portSpeedsMax[phyCount] = 5;
                                        break;
                                    case 0xD:
                                        driveInfo->interfaceSpeedInfo.serialSpeed.portSpeedsMax[phyCount] = 6;
                                        break;
                                    case 0xE:
                                        driveInfo->interfaceSpeedInfo.serialSpeed.portSpeedsMax[phyCount] = 7;
                                        break;
                                    case 0xF:
                                        driveInfo->interfaceSpeedInfo.serialSpeed.portSpeedsMax[phyCount] = 8;
                                        break;
                                    default:
                                        driveInfo->interfaceSpeedInfo.serialSpeed.portSpeedsMax[phyCount] = 0;
                                        break;
                                    }
                                }
                            }
                            break;
                            case 0x7://automation/drive interface transport
                            case 0x8://AT Attachement interface
                            case 0x9://UAS
                            case 0xA://SCSI over PCI Express
                            case 0xB://PCI Express protocols
                            case 0xF://No specific protocol
                            default://reserved
                                break;
                            }
                        }
                    }
                    break;
                    case 0x03://Negotiated Settings (Parallel SCSI)
                    {
                        DECLARE_ZERO_INIT_ARRAY(uint8_t, protocolSpecificPort, LEGACY_DRIVE_SEC_SIZE + SCSI_MODE_PAGE_MIN_HEADER_LENGTH);//need to include header length in this
                        bool pageRead = false;
                        bool sixByte = false;
                        uint16_t headerLength = 0;
                        if (SUCCESS == get_SCSI_Mode_Page(device, MPC_CURRENT_VALUES, pageCode, subPageCode, M_NULLPTR, M_NULLPTR, true, protocolSpecificPort, LEGACY_DRIVE_SEC_SIZE + SCSI_MODE_PAGE_MIN_HEADER_LENGTH, M_NULLPTR, &sixByte))
                        {
                            uint16_t blockDescLen = 0;
                            pageRead = true;
                            if (sixByte)
                            {
                                headerLength = MODE_PARAMETER_HEADER_6_LEN;
                                blockDescLen = protocolSpecificPort[2];
                                if (protocolSpecificPort[2] & BIT7)
                                {
                                    driveInfo->isWriteProtected = true;
                                }
                            }
                            else
                            {
                                headerLength = MODE_PARAMETER_HEADER_10_LEN;
                                blockDescLen = M_BytesTo2ByteValue(protocolSpecificPort[6], protocolSpecificPort[7]);
                                if (protocolSpecificPort[3] & BIT7)
                                {
                                    driveInfo->isWriteProtected = true;
                                }
                            }
                            headerLength += blockDescLen;
                        }
                        if (pageRead)
                        {
                            protocolIdentifier = M_Nibble0(protocolSpecificPort[headerLength + 5]);
                            switch (protocolIdentifier)
                            {
                            case 0x0://Fiber Channel
                                break;
                            case 0x1://parallel scsi
                            {
                                //get the negotiated speed
                                uint16_t scalingMultiplier = 0;
                                uint8_t transferPeriodFactor = protocolSpecificPort[headerLength + 6];
                                uint8_t transferWidthExponent = protocolSpecificPort[headerLength + 9];
                                switch (transferPeriodFactor)
                                {
                                case 0x07:
                                    scalingMultiplier = 320;
                                    break;
                                case 0x08:
                                    scalingMultiplier = 160;
                                    break;
                                case 0x09:
                                    scalingMultiplier = 80;
                                    break;
                                case 0x0A:
                                    scalingMultiplier = 40;
                                    break;
                                case 0x0B:
                                    scalingMultiplier = 40;
                                    break;
                                case 0x0C:
                                    scalingMultiplier = 20;
                                    break;
                                default:
                                    //need to do an if here...
                                    if (transferPeriodFactor >= 0x0D && transferPeriodFactor <= 0x18)
                                    {
                                        scalingMultiplier = 20;
                                    }
                                    else if (transferPeriodFactor >= 0x19 && transferPeriodFactor <= 0x31)
                                    {
                                        scalingMultiplier = 10;
                                    }
                                    else if (transferPeriodFactor >= 0x32 /* && transferPeriodFactor <= 0xFF */)
                                    {
                                        scalingMultiplier = 5;
                                    }
                                    break;
                                }
                                if (scalingMultiplier > 0)
                                {
                                    driveInfo->interfaceSpeedInfo.speedType = INTERFACE_SPEED_PARALLEL;
                                    driveInfo->interfaceSpeedInfo.speedIsValid = true;
                                    driveInfo->interfaceSpeedInfo.parallelSpeed.negotiatedValid = true;
                                    driveInfo->interfaceSpeedInfo.parallelSpeed.negotiatedSpeed = C_CAST(double, scalingMultiplier) * (C_CAST(double, transferWidthExponent) + UINT32_C(1));
                                    snprintf(driveInfo->interfaceSpeedInfo.parallelSpeed.negModeName, PARALLEL_INTERFACE_MODE_NAME_MAX_LENGTH, "FAST-%" PRIu16 "", scalingMultiplier);
                                    driveInfo->interfaceSpeedInfo.parallelSpeed.negModeNameValid = true;
                                }
                            }
                            break;
                            case 0x2://serial storage architecture scsi-3 protocol
                            case 0x3://IEEE 1394
                            case 0x4://RDMA 
                            case 0x5://iSCSI
                            case 0x6:
                            default:
                                break;
                            }
                        }
                    }
                    break;
                    case 0x04://Report Transfer Capabilities (Parallel SCSI)
                    {
                        DECLARE_ZERO_INIT_ARRAY(uint8_t, protocolSpecificPort, LEGACY_DRIVE_SEC_SIZE + SCSI_MODE_PAGE_MIN_HEADER_LENGTH);//need to include header length in this
                        bool pageRead = false;
                        bool sixByte = false;
                        uint16_t headerLength = 0;
                        if (SUCCESS == get_SCSI_Mode_Page(device, MPC_CURRENT_VALUES, pageCode, subPageCode, M_NULLPTR, M_NULLPTR, true, protocolSpecificPort, LEGACY_DRIVE_SEC_SIZE + SCSI_MODE_PAGE_MIN_HEADER_LENGTH, M_NULLPTR, &sixByte))
                        {
                            uint16_t blockDescLen = 0;
                            pageRead = true;
                            if (sixByte)
                            {
                                headerLength = MODE_PARAMETER_HEADER_6_LEN;
                                blockDescLen = protocolSpecificPort[2];
                                if (protocolSpecificPort[2] & BIT7)
                                {
                                    driveInfo->isWriteProtected = true;
                                }
                            }
                            else
                            {
                                headerLength = MODE_PARAMETER_HEADER_10_LEN;
                                blockDescLen = M_BytesTo2ByteValue(protocolSpecificPort[6], protocolSpecificPort[7]);
                                if (protocolSpecificPort[3] & BIT7)
                                {
                                    driveInfo->isWriteProtected = true;
                                }
                            }
                            headerLength += blockDescLen;
                        }
                        if (pageRead)
                        {
                            protocolIdentifier = M_Nibble0(protocolSpecificPort[headerLength + 5]);
                            switch (protocolIdentifier)
                            {
                            case 0x0://Fiber Channel
                                break;
                            case 0x1://parallel scsi
                            {
                                //get the max speed
                                uint16_t scalingMultiplier = 0;
                                uint8_t transferPeriodFactor = protocolSpecificPort[headerLength + 6];
                                uint8_t transferWidthExponent = protocolSpecificPort[headerLength + 9];
                                switch (transferPeriodFactor)
                                {
                                case 0x07:
                                    scalingMultiplier = 320;
                                    break;
                                case 0x08:
                                    scalingMultiplier = 160;
                                    break;
                                case 0x09:
                                    scalingMultiplier = 80;
                                    break;
                                case 0x0A:
                                    scalingMultiplier = 40;
                                    break;
                                case 0x0B:
                                    scalingMultiplier = 40;
                                    break;
                                case 0x0C:
                                    scalingMultiplier = 20;
                                    break;
                                default:
                                    //need to do an if here...
                                    if (transferPeriodFactor >= 0x0D && transferPeriodFactor <= 0x18)
                                    {
                                        scalingMultiplier = 20;
                                    }
                                    else if (transferPeriodFactor >= 0x19 && transferPeriodFactor <= 0x31)
                                    {
                                        scalingMultiplier = 10;
                                    }
                                    else if (transferPeriodFactor >= 0x32 /* && transferPeriodFactor <= 0xFF */)
                                    {
                                        scalingMultiplier = 5;
                                    }
                                    break;
                                }
                                if (scalingMultiplier > 0)
                                {
                                    driveInfo->interfaceSpeedInfo.speedType = INTERFACE_SPEED_PARALLEL;
                                    driveInfo->interfaceSpeedInfo.speedIsValid = true;
                                    driveInfo->interfaceSpeedInfo.parallelSpeed.maxSpeed = C_CAST(double, scalingMultiplier) * (C_CAST(double, transferWidthExponent) + UINT32_C(1));
                                    snprintf(driveInfo->interfaceSpeedInfo.parallelSpeed.maxModeName, PARALLEL_INTERFACE_MODE_NAME_MAX_LENGTH, "FAST-%" PRIu16 "", scalingMultiplier);
                                    driveInfo->interfaceSpeedInfo.parallelSpeed.maxModeNameValid = true;
                                }
                            }
                            break;
                            case 0x2://serial storage architecture scsi-3 protocol
                            case 0x3://IEEE 1394
                            case 0x4://RDMA 
                            case 0x5://iSCSI
                            case 0x6:
                            default:
                                break;
                            }
                        }
                    }
                    break;
                    default:
                        break;
                    }
                    break;
                case MP_POWER_CONDTION:
                    switch (subPageCode)
                    {
                    case 0x00://EPC
                    {
                        char* epcFeatureString = M_NULLPTR;
                        uint32_t epcFeatureStringLength = 0;
                        //read the default values to check if it's supported...then try the current page...
                        bool defaultsRead = false;
                        bool sixByte = false;
                        DECLARE_ZERO_INIT_ARRAY(uint8_t, powerConditions, 40 + SCSI_MODE_PAGE_MIN_HEADER_LENGTH);//need to include header length in this
                        bool pageRead = false;
                        uint16_t headerLength = 0;
                        if (SUCCESS == get_SCSI_Mode_Page(device, MPC_DEFAULT_VALUES, pageCode, subPageCode, M_NULLPTR, M_NULLPTR, true, powerConditions, 40 + SCSI_MODE_PAGE_MIN_HEADER_LENGTH, M_NULLPTR, &sixByte))
                        {
                            uint16_t blockDescLen = 0;
                            defaultsRead = true;
                            if (sixByte)
                            {
                                headerLength = MODE_PARAMETER_HEADER_6_LEN;
                                blockDescLen = powerConditions[2];
                            }
                            else
                            {
                                headerLength = MODE_PARAMETER_HEADER_10_LEN;
                                blockDescLen = M_BytesTo2ByteValue(powerConditions[6], powerConditions[7]);
                            }
                            headerLength += blockDescLen;
                        }
                        if (defaultsRead)
                        {
                            if (powerConditions[1 + headerLength] > 0x0A)
                            {
                                if (!epcFeatureString)
                                {
                                    epcFeatureStringLength = 4;
                                    epcFeatureString = C_CAST(char*, safe_calloc(epcFeatureStringLength, sizeof(char)));
                                }
                                else
                                {
                                    epcFeatureStringLength = 4;
                                    char* temp = C_CAST(char*, safe_realloc(epcFeatureString, epcFeatureStringLength * sizeof(char)));
                                    if (temp)
                                    {
                                        epcFeatureString = temp;
                                        memset(epcFeatureString, 0, epcFeatureStringLength);
                                    }
                                }
                                if (epcFeatureString && epcFeatureStringLength >= 4)
                                {
                                    snprintf(epcFeatureString, epcFeatureStringLength, "EPC");
                                }
                            }
                            else
                            {
                                if (!epcFeatureString)
                                {
                                    epcFeatureStringLength = 17;
                                    epcFeatureString = C_CAST(char*, safe_calloc(epcFeatureStringLength, sizeof(char)));
                                }
                                else
                                {
                                    epcFeatureStringLength = 17;
                                    char* temp = C_CAST(char*, safe_realloc(epcFeatureString, epcFeatureStringLength * sizeof(char)));
                                    if (temp)
                                    {
                                        epcFeatureString = temp;
                                        memset(epcFeatureString, 0, epcFeatureStringLength);
                                    }
                                }
                                if (epcFeatureString && epcFeatureStringLength >= 17)
                                {
                                    snprintf(epcFeatureString, epcFeatureStringLength, "Power Conditions");
                                }
                            }
                        }
                        //Now read the current page to see if it's more than just supported :)
                        if (SUCCESS == get_SCSI_Mode_Page(device, MPC_CURRENT_VALUES, pageCode, subPageCode, M_NULLPTR, M_NULLPTR, true, powerConditions, 40 + SCSI_MODE_PAGE_MIN_HEADER_LENGTH, M_NULLPTR, &sixByte))
                        {
                            uint16_t blockDescLen = 0;
                            pageRead = true;
                            if (sixByte)
                            {
                                headerLength = MODE_PARAMETER_HEADER_6_LEN;
                                blockDescLen = powerConditions[2];
                                if (powerConditions[2] & BIT7)
                                {
                                    driveInfo->isWriteProtected = true;
                                }
                            }
                            else
                            {
                                headerLength = MODE_PARAMETER_HEADER_10_LEN;
                                blockDescLen = M_BytesTo2ByteValue(powerConditions[6], powerConditions[7]);
                                if (powerConditions[3] & BIT7)
                                {
                                    driveInfo->isWriteProtected = true;
                                }
                            }
                            headerLength += blockDescLen;
                        }
                        if (pageRead)
                        {
                            if (powerConditions[1 + headerLength] > 0x0A &&
                                (powerConditions[2 + headerLength] & BIT0 ||
                                    powerConditions[3 + headerLength] & BIT0 ||
                                    powerConditions[3 + headerLength] & BIT1 ||
                                    powerConditions[3 + headerLength] & BIT2 ||
                                    powerConditions[3 + headerLength] & BIT3
                                    )
                                )
                            {
                                if (!epcFeatureString)
                                {
                                    epcFeatureStringLength = 14;
                                    epcFeatureString = C_CAST(char*, safe_calloc(epcFeatureStringLength, sizeof(char)));
                                }
                                else
                                {
                                    epcFeatureStringLength = 14;
                                    char* temp = C_CAST(char*, safe_realloc(epcFeatureString, epcFeatureStringLength * sizeof(char)));
                                    if (temp)
                                    {
                                        epcFeatureString = temp;
                                        memset(epcFeatureString, 0, epcFeatureStringLength);
                                    }
                                }
                                if (epcFeatureString && epcFeatureStringLength >= 14)
                                {
                                    snprintf(epcFeatureString, epcFeatureStringLength, "EPC [Enabled]");
                                }
                            }
                            else if (powerConditions[3 + headerLength] & BIT0 || powerConditions[3 + headerLength] & BIT1)
                            {
                                if (!epcFeatureString)
                                {
                                    epcFeatureStringLength = 27;
                                    epcFeatureString = C_CAST(char*, safe_calloc(epcFeatureStringLength, sizeof(char)));
                                }
                                else
                                {
                                    epcFeatureStringLength = 27;
                                    char* temp = C_CAST(char*, safe_realloc(epcFeatureString, epcFeatureStringLength * sizeof(char)));
                                    if (temp)
                                    {
                                        epcFeatureString = temp;
                                        memset(epcFeatureString, 0, 27);
                                    }
                                }
                                if (epcFeatureString && epcFeatureStringLength >= 27)
                                {
                                    snprintf(epcFeatureString, epcFeatureStringLength, "Power Conditions [Enabled]");
                                }
                            }
                        }
                        if (epcFeatureString)
                        {
                            add_Feature_To_Supported_List(driveInfo->featuresSupported, &driveInfo->numberOfFeaturesSupported, epcFeatureString);
                        }
                        safe_free(&epcFeatureString);
                    }
                    break;
                    case 0xF1://ata power conditions
                    {
                        DECLARE_ZERO_INIT_ARRAY(uint8_t, ataPowerConditions, 16 + SCSI_MODE_PAGE_MIN_HEADER_LENGTH);//need to include header length in this
                        bool pageRead = false;
                        bool sixByte = false;
                        uint16_t headerLength = 0;
                        if (SUCCESS == get_SCSI_Mode_Page(device, MPC_CURRENT_VALUES, pageCode, subPageCode, M_NULLPTR, M_NULLPTR, true, ataPowerConditions, 16 + SCSI_MODE_PAGE_MIN_HEADER_LENGTH, M_NULLPTR, &sixByte))
                        {
                            uint16_t blockDescLen = 0;
                            pageRead = true;
                            if (sixByte)
                            {
                                headerLength = MODE_PARAMETER_HEADER_6_LEN;
                                blockDescLen = ataPowerConditions[2];
                                if (ataPowerConditions[2] & BIT7)
                                {
                                    driveInfo->isWriteProtected = true;
                                }
                            }
                            else
                            {
                                headerLength = MODE_PARAMETER_HEADER_10_LEN;
                                blockDescLen = M_BytesTo2ByteValue(ataPowerConditions[6], ataPowerConditions[7]);
                                if (ataPowerConditions[3] & BIT7)
                                {
                                    driveInfo->isWriteProtected = true;
                                }
                            }
                            headerLength += blockDescLen;
                        }
                        if (pageRead)
                        {
                            if (ataPowerConditions[headerLength + 0x05] & BIT0)
                            {
                                add_Feature_To_Supported_List(driveInfo->featuresSupported, &driveInfo->numberOfFeaturesSupported, "APM [Enabled]");
                            }
                            else
                            {
                                add_Feature_To_Supported_List(driveInfo->featuresSupported, &driveInfo->numberOfFeaturesSupported, "APM");
                            }
                        }
                    }
                    break;
                    default:
                        break;
                    }
                    break;
                case MP_INFORMATION_EXCEPTIONS_CONTROL:
                    switch (subPageCode)
                    {
                    case 0:
                    {
                        DECLARE_ZERO_INIT_ARRAY(uint8_t, informationalExceptions, 12 + SCSI_MODE_PAGE_MIN_HEADER_LENGTH);//need to include header length in this
                        bool pageRead = false;
                        bool sixByte = false;
                        uint16_t headerLength = 0;
                        if (SUCCESS == get_SCSI_Mode_Page(device, MPC_CURRENT_VALUES, pageCode, subPageCode, M_NULLPTR, M_NULLPTR, true, informationalExceptions, 12 + SCSI_MODE_PAGE_MIN_HEADER_LENGTH, M_NULLPTR, &sixByte))
                        {
                            uint16_t blockDescLen = 0;
                            pageRead = true;
                            if (sixByte)
                            {
                                headerLength = MODE_PARAMETER_HEADER_6_LEN;
                                blockDescLen = informationalExceptions[2];
                                if (informationalExceptions[2] & BIT7)
                                {
                                    driveInfo->isWriteProtected = true;
                                }
                            }
                            else
                            {
                                headerLength = MODE_PARAMETER_HEADER_10_LEN;
                                blockDescLen = M_BytesTo2ByteValue(informationalExceptions[6], informationalExceptions[7]);
                                if (informationalExceptions[3] & BIT7)
                                {
                                    driveInfo->isWriteProtected = true;
                                }
                            }
                            headerLength += blockDescLen;
                        }
                        if (pageRead)
                        {
                            DECLARE_ZERO_INIT_ARRAY(char, temp, MAX_FEATURE_LENGTH);
                            snprintf(temp, MAX_FEATURE_LENGTH, "Informational Exceptions [Mode %" PRIu8 "]", M_Nibble0(informationalExceptions[headerLength + 3]));
                            add_Feature_To_Supported_List(driveInfo->featuresSupported, &driveInfo->numberOfFeaturesSupported, temp);

                        }
                    }
                    break;
                    case 0x01://Background control
                    {
                        //check if DLC is supported or can be changed before checking if they are enabled or not.
                        char* bmsString = M_NULLPTR;
                        char* bmsPSString = M_NULLPTR;
                        uint32_t bmsStringLength = 0;
                        uint32_t bmsPSStringLength = 0;
                        DECLARE_ZERO_INIT_ARRAY(uint8_t, backgroundControl, 16 + SCSI_MODE_PAGE_MIN_HEADER_LENGTH);//need to include header length in this
                        bool pageRead = false;
                        bool defaultsRead = false;
                        bool sixByte = false;
                        uint16_t headerLength = 0;
                        if (SUCCESS == get_SCSI_Mode_Page(device, MPC_DEFAULT_VALUES, pageCode, subPageCode, M_NULLPTR, M_NULLPTR, true, backgroundControl, 16 + SCSI_MODE_PAGE_MIN_HEADER_LENGTH, M_NULLPTR, &sixByte))
                        {
                            uint16_t blockDescLen = 0;
                            defaultsRead = true;
                            if (sixByte)
                            {
                                headerLength = MODE_PARAMETER_HEADER_6_LEN;
                                blockDescLen = backgroundControl[2];
                            }
                            else
                            {
                                headerLength = MODE_PARAMETER_HEADER_10_LEN;
                                blockDescLen = M_BytesTo2ByteValue(backgroundControl[6], backgroundControl[7]);
                            }
                            headerLength += blockDescLen;
                        }
                        if (defaultsRead)
                        {
                            //bms
                            if (backgroundControl[headerLength + 4] & BIT0)
                            {
                                if (!bmsString)
                                {
                                    bmsStringLength = 50;
                                    bmsString = C_CAST(char*, safe_calloc(bmsStringLength, sizeof(char)));
                                }
                                else
                                {
                                    bmsStringLength = 50;
                                    char* temp = C_CAST(char*, safe_realloc(bmsString, bmsStringLength * sizeof(char)));
                                    if (temp)
                                    {
                                        bmsString = temp;
                                        memset(bmsString, 0, bmsStringLength);
                                    }
                                }
                                if (bmsString && bmsStringLength >= 50)
                                {
                                    snprintf(bmsString, bmsStringLength, "Background Media Scan");
                                }
                            }
                            //bms-ps
                            if (backgroundControl[headerLength + 5] & BIT0)
                            {
                                if (!bmsPSString)
                                {
                                    bmsPSStringLength = 50;
                                    bmsPSString = C_CAST(char*, safe_calloc(bmsPSStringLength, sizeof(char)));
                                }
                                else
                                {
                                    bmsPSStringLength = 50;
                                    char* temp = C_CAST(char*, safe_realloc(bmsPSString, bmsPSStringLength * sizeof(char)));
                                    if (temp)
                                    {
                                        bmsPSString = temp;
                                        memset(bmsPSString, 0, bmsPSStringLength);
                                    }
                                }
                                if (bmsPSString && bmsPSStringLength >= 50)
                                {
                                    snprintf(bmsPSString, bmsPSStringLength, "Background Pre-Scan");
                                }
                            }
                        }
                        if (SUCCESS == get_SCSI_Mode_Page(device, MPC_CURRENT_VALUES, pageCode, subPageCode, M_NULLPTR, M_NULLPTR, true, backgroundControl, 16 + SCSI_MODE_PAGE_MIN_HEADER_LENGTH, M_NULLPTR, &sixByte))
                        {
                            uint16_t blockDescLen = 0;
                            pageRead = true;
                            if (sixByte)
                            {
                                headerLength = MODE_PARAMETER_HEADER_6_LEN;
                                blockDescLen = backgroundControl[2];
                                if (backgroundControl[2] & BIT7)
                                {
                                    driveInfo->isWriteProtected = true;
                                }
                            }
                            else
                            {
                                headerLength = MODE_PARAMETER_HEADER_10_LEN;
                                blockDescLen = M_BytesTo2ByteValue(backgroundControl[6], backgroundControl[7]);
                                if (backgroundControl[3] & BIT7)
                                {
                                    driveInfo->isWriteProtected = true;
                                }
                            }
                            headerLength += blockDescLen;
                        }
                        if (pageRead)
                        {
                            //bms
                            if (backgroundControl[headerLength + 4] & BIT0)
                            {
                                if (!bmsString)
                                {
                                    bmsStringLength = 50;
                                    bmsString = C_CAST(char*, safe_calloc(bmsStringLength, sizeof(char)));
                                }
                                else
                                {
                                    bmsStringLength = 50;
                                    char* temp = C_CAST(char*, safe_realloc(bmsString, bmsStringLength * sizeof(char)));
                                    if (temp)
                                    {
                                        bmsString = temp;
                                        memset(bmsString, 0, bmsStringLength);
                                    }
                                }
                                if (bmsString && bmsStringLength >= 50)
                                {
                                    snprintf(bmsString, bmsStringLength, "Background Media Scan [Enabled]");
                                }
                            }
                            //bms-ps
                            if (backgroundControl[headerLength + 5] & BIT0)
                            {
                                if (!bmsPSString)
                                {
                                    bmsPSStringLength = 50;
                                    bmsPSString = C_CAST(char*, safe_calloc(bmsPSStringLength, sizeof(char)));
                                }
                                else
                                {
                                    bmsPSStringLength = 50;
                                    char* temp = C_CAST(char*, safe_realloc(bmsPSString, bmsPSStringLength * sizeof(char)));
                                    if (temp)
                                    {
                                        bmsPSString = temp;
                                        memset(bmsPSString, 0, bmsPSStringLength);
                                    }
                                }
                                if (bmsPSString && bmsPSStringLength >= 50)
                                {
                                    snprintf(bmsPSString, bmsPSStringLength, "Background Pre-Scan [Enabled]");
                                }
                            }
                        }
                        if (bmsString)
                        {
                            add_Feature_To_Supported_List(driveInfo->featuresSupported, &driveInfo->numberOfFeaturesSupported, bmsString);
                        }
                        if (bmsPSString)
                        {
                            add_Feature_To_Supported_List(driveInfo->featuresSupported, &driveInfo->numberOfFeaturesSupported, bmsPSString);
                        }
                        safe_free(&bmsString);
                        safe_free(&bmsPSString);
                    }
                    break;
                    default:
                        break;
                    }
                    break;
                default:
                    break;
                }
            }
        }
    }
    return ret;
}

//which diag pages are suppored to add to features list
static eReturnValues get_SCSI_Diagnostic_Data(tDevice* device, ptrDriveInformationSAS_SATA driveInfo, ptrSCSIIdentifyInfo scsiInfo)
{
    eReturnValues ret = SUCCESS;
    if (device && driveInfo && scsiInfo)
    {
        //skip diag pages on USB/IEEE1394 as it is extremly unlikely these requests will be handled properly and unlikely that any standard diag pages will be supported.-TJE
        if (device->drive_info.interface_type != USB_INTERFACE && device->drive_info.interface_type != IEEE_1394_INTERFACE && device->drive_info.interface_type != MMC_INTERFACE && device->drive_info.interface_type != SD_INTERFACE)
        {
            //Read supported Diagnostic parameters and check for rebuild assist. (need SCSI2 and higher since before that, this is all vendor unique)
            uint16_t supportedDiagsLength = UINT16_C(512);
            uint8_t* supportedDiagnostics = C_CAST(uint8_t*, safe_calloc_aligned(supportedDiagsLength, sizeof(uint8_t), device->os_info.minimumAlignment));
            if (supportedDiagnostics)
            {
                bool gotDiagData = false;
                if (scsiInfo->version >= 3)//PCV bit and page code fields introduced in SPC specification
                {
                    //try this request first. If it fails, then most likely we need to use the SCSI 2 method below instead
                    if (SUCCESS == scsi_Receive_Diagnostic_Results(device, true, DIAG_PAGE_SUPPORTED_PAGES, supportedDiagsLength, supportedDiagnostics, 15))
                    {
                        if (supportedDiagnostics[0] == DIAG_PAGE_SUPPORTED_PAGES && supportedDiagnostics[1] == 0) //validate returned page data
                        {
                            gotDiagData = true;
                        }
                    }
                }
                if (!gotDiagData)
                {
                    //old backwards compatible way to request the diag data is to send a send diagnostic with all zeroes to request the supported pages in the subsequent receive
                    memset(supportedDiagnostics, 0, supportedDiagsLength);
                    if (scsiInfo->version >= 2 && SUCCESS == scsi_Send_Diagnostic(device, 0, 1, 0, 0, 0, 4, supportedDiagnostics, 4, 15))
                    {
                        if (SUCCESS == scsi_Receive_Diagnostic_Results(device, false, 0, supportedDiagsLength, supportedDiagnostics, 15))
                        {
                            gotDiagData = true;
                        }
                    }
                }
                if (gotDiagData)
                {
                    //confirm the page code in case old devices did not respond correctly due to not supporting the page format bit.
                    //confirm the list of supported pages by checking the page code and byte 1 are both set to zero
                    if (supportedDiagnostics[0] == DIAG_PAGE_SUPPORTED_PAGES && supportedDiagnostics[1] == 0)
                    {
                        uint16_t pageLength = M_BytesTo2ByteValue(supportedDiagnostics[2], supportedDiagnostics[3]);
                        for (uint32_t iter = UINT16_C(4); iter < C_CAST(uint32_t, pageLength + UINT16_C(4)) && iter < supportedDiagsLength; ++iter)
                        {
                            switch (supportedDiagnostics[iter])
                            {
                                //Add more diagnostic pages in here if we want to check them for supported features.
                            case DIAG_PAGE_TRANSLATE_ADDRESS:
                                add_Feature_To_Supported_List(driveInfo->featuresSupported, &driveInfo->numberOfFeaturesSupported, "Translate Address");
                                break;
                            case DIAG_PAGE_REBUILD_ASSIST:
                                add_Feature_To_Supported_List(driveInfo->featuresSupported, &driveInfo->numberOfFeaturesSupported, "Rebuild Assist");
                                break;
                            case 0x90:
                                if (is_Seagate_Family(device) == SEAGATE)
                                {
                                    add_Feature_To_Supported_List(driveInfo->featuresSupported, &driveInfo->numberOfFeaturesSupported, "Seagate Remanufacture");
                                    break;
                                }
                                break;
                            case 0x98:
                                if (is_Seagate_Family(device) == SEAGATE)
                                {
                                    add_Feature_To_Supported_List(driveInfo->featuresSupported, &driveInfo->numberOfFeaturesSupported, "Seagate In Drive Diagnostics (IDD)");
                                    break;
                                }
                                break;
                            default:
                                break;
                            }
                        }
                    }
                }
                safe_free_aligned(&supportedDiagnostics);
            }
        }
    }
    return ret;
}

//report supported operation codes to figure out additional features.
static eReturnValues get_SCSI_Report_Op_Codes_Data(tDevice* device, ptrDriveInformationSAS_SATA driveInfo, ptrSCSIIdentifyInfo scsiInfo)
{
    eReturnValues ret = SUCCESS;
    if (device && driveInfo && scsiInfo)
    {
        if (!device->drive_info.passThroughHacks.scsiHacks.noReportSupportedOperations)//mostly for USB devices to prevent sending commands that don't usually work in the first place.
        {
            uint8_t* supportedCommands = C_CAST(uint8_t*, safe_calloc_aligned(36, sizeof(uint8_t), device->os_info.minimumAlignment));
            //allocating 36 bytes for 4 byte header + max length of 32B for the CDB
            if (supportedCommands)
            {
                //Most SAT devices won't report all at once, so try asking for individual commands that are supported
                //one at a time instead of asking for everything all at once.
                //Format unit
                bool formatSupported = false;
                bool fastFormatSupported = false;
                if (scsiInfo->version >= 5 && SUCCESS == scsi_Report_Supported_Operation_Codes(device, false, REPORT_OPERATION_CODE, SCSI_FORMAT_UNIT_CMD, 0, 10, supportedCommands))
                {
                    switch (supportedCommands[1] & 0x07)
                    {
                    case 0: //not available right now...so not supported
                    case 1://not supported
                        break;
                    case 3://supported according to spec
                    case 5://supported in vendor specific mannor in same format as case 3
                        formatSupported = true;
                        //now check for fast format support
                        if (!(supportedCommands[7] == 0xFF && supportedCommands[8] == 0xFF))//if both these bytes are FFh, then the drive conforms to SCSI2 where this was the "interleave" field
                        {
                            if (supportedCommands[8] & 0x03)//checks that fast format bits are available for use.
                            {
                                fastFormatSupported = true;
                            }
                        }
                        break;
                    default:
                        break;
                    }
                }
                else if (scsiInfo->version >= 3 && scsiInfo->version < 5 && SUCCESS == scsi_Inquiry(device, supportedCommands, 12, SCSI_FORMAT_UNIT_CMD, false, true))
                {
                    switch (supportedCommands[1] & 0x07)
                    {
                    case 0: //not available right now...so not supported
                    case 1://not supported
                        break;
                    case 3://supported according to spec
                    case 5://supported in vendor specific mannor in same format as case 3
                        formatSupported = true;
                        break;
                    default:
                        break;
                    }
                }
                else
                {
                    //failed to check command support (for one reason or another)
                    //for CMD DT, just return not supported.
                    //for report op codes, set hacks flag and return
                    if (scsiInfo->version >= 5)
                    {
                        device->drive_info.passThroughHacks.scsiHacks.noReportSupportedOperations = true;
                    }
                    safe_free_aligned(&supportedCommands);
                    return NOT_SUPPORTED;
                }
                //check for format corrupt
                uint8_t senseKey = 0;
                uint8_t asc = 0;
                uint8_t ascq = 0;
                uint8_t fru = 0;
                get_Sense_Key_ASC_ASCQ_FRU(device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, &senseKey, &asc, &ascq, &fru);
                if (senseKey == SENSE_KEY_MEDIUM_ERROR && asc == 0x31 && ascq == 0)
                {
                    if (!driveInfo->isFormatCorrupt)
                    {
                        add_Feature_To_Supported_List(driveInfo->featuresSupported, &driveInfo->numberOfFeaturesSupported, "Format Corrupt - not all features identifiable.");
                    }
                }
                if (formatSupported)
                {
                    add_Feature_To_Supported_List(driveInfo->featuresSupported, &driveInfo->numberOfFeaturesSupported, "Format Unit");
                }
                if (fastFormatSupported)
                {
                    add_Feature_To_Supported_List(driveInfo->featuresSupported, &driveInfo->numberOfFeaturesSupported, "Fast Format");
                }
                memset(supportedCommands, 0, 36);
                if (scsiInfo->version >= 5 && SUCCESS == scsi_Report_Supported_Operation_Codes(device, false, REPORT_OPERATION_CODE, SCSI_FORMAT_WITH_PRESET_CMD, 0, 14, supportedCommands))
                {
                    switch (supportedCommands[1] & 0x07)
                    {
                    case 0: //not available right now...so not supported
                    case 1://not supported
                        break;
                    case 3://supported according to spec
                    case 5://supported in vendor specific mannor in same format as case 3
                        add_Feature_To_Supported_List(driveInfo->featuresSupported, &driveInfo->numberOfFeaturesSupported, "Format With Preset");
                        break;
                    default:
                        break;
                    }
                }
                memset(supportedCommands, 0, 36);
                //Sanitize (need to check each service action to make sure at least one is supported.
                bool sanitizeSupported = false;
                if (scsiInfo->version >= 5 && SUCCESS == scsi_Report_Supported_Operation_Codes(device, false, REPORT_OPERATION_CODE_AND_SERVICE_ACTION, SANITIZE_CMD, SCSI_SANITIZE_OVERWRITE, 14, supportedCommands))
                {
                    switch (supportedCommands[1] & 0x07)
                    {
                    case 0: //not available right now...so not supported
                    case 1://not supported
                        break;
                    case 3://supported according to spec
                    case 5://supported in vendor specific manor in same format as case 3
                        sanitizeSupported = true;
                        break;
                    default:
                        break;
                    }
                }
                else if (scsiInfo->version >= 5 && SUCCESS == scsi_Report_Supported_Operation_Codes(device, false, REPORT_OPERATION_CODE_AND_SERVICE_ACTION, SANITIZE_CMD, SCSI_SANITIZE_BLOCK_ERASE, 14, supportedCommands))
                {
                    switch (supportedCommands[1] & 0x07)
                    {
                    case 0: //not available right now...so not supported
                    case 1://not supported
                        break;
                    case 3://supported according to spec
                    case 5://supported in vendor specific manor in same format as case 3
                        sanitizeSupported = true;
                        break;
                    default:
                        break;
                    }
                }
                else if (scsiInfo->version >= 5 && SUCCESS == scsi_Report_Supported_Operation_Codes(device, false, REPORT_OPERATION_CODE_AND_SERVICE_ACTION, SANITIZE_CMD, SCSI_SANITIZE_CRYPTOGRAPHIC_ERASE, 14, supportedCommands))
                {
                    switch (supportedCommands[1] & 0x07)
                    {
                    case 0: //not available right now...so not supported
                    case 1://not supported
                        break;
                    case 3://supported according to spec
                    case 5://supported in vendor specific manor in same format as case 3
                        sanitizeSupported = true;
                        break;
                    default:
                        break;
                    }
                }
                else if (scsiInfo->version >= 5 && SUCCESS == scsi_Report_Supported_Operation_Codes(device, false, REPORT_OPERATION_CODE_AND_SERVICE_ACTION, SANITIZE_CMD, SCSI_SANITIZE_EXIT_FAILURE_MODE, 14, supportedCommands))
                {
                    switch (supportedCommands[1] & 0x07)
                    {
                    case 0: //not available right now...so not supported
                    case 1://not supported
                        break;
                    case 3://supported according to spec
                    case 5://supported in vendor specific manor in same format as case 3
                        sanitizeSupported = true;
                        break;
                    default:
                        break;
                    }
                }
                if (sanitizeSupported)
                {
                    add_Feature_To_Supported_List(driveInfo->featuresSupported, &driveInfo->numberOfFeaturesSupported, "Sanitize");
                }
                //storage element depopulation
                bool getElementStatusSupported = false;
                bool removeAndTruncateSupported = false;
                bool restoreElementsSupported = false;
                if (scsiInfo->version >= 5 && SUCCESS == scsi_Report_Supported_Operation_Codes(device, false, REPORT_OPERATION_CODE_AND_SERVICE_ACTION, 0x9E, 0x17, 20, supportedCommands))
                {
                    switch (supportedCommands[1] & 0x07)
                    {
                    case 0: //not available right now...so not supported
                    case 1://not supported
                        break;
                    case 3://supported according to spec
                    case 5://supported in vendor specific manor in same format as case 3
                        getElementStatusSupported = true;
                        break;
                    default:
                        break;
                    }
                }
                if (scsiInfo->version >= 5 && SUCCESS == scsi_Report_Supported_Operation_Codes(device, false, REPORT_OPERATION_CODE_AND_SERVICE_ACTION, 0x9E, 0x18, 20, supportedCommands))
                {
                    switch (supportedCommands[1] & 0x07)
                    {
                    case 0: //not available right now...so not supported
                    case 1://not supported
                        break;
                    case 3://supported according to spec
                    case 5://supported in vendor specific manor in same format as case 3
                        removeAndTruncateSupported = true;
                        break;
                    default:
                        break;
                    }
                }
                if (scsiInfo->version >= 5 && SUCCESS == scsi_Report_Supported_Operation_Codes(device, false, REPORT_OPERATION_CODE_AND_SERVICE_ACTION, 0x9E, 0x19, 20, supportedCommands))
                {
                    switch (supportedCommands[1] & 0x07)
                    {
                    case 0: //not available right now...so not supported
                    case 1://not supported
                        break;
                    case 3://supported according to spec
                    case 5://supported in vendor specific manor in same format as case 3
                        restoreElementsSupported = true;
                        break;
                    default:
                        break;
                    }
                }
                if (removeAndTruncateSupported && getElementStatusSupported)
                {
                    if (restoreElementsSupported)
                    {
                        add_Feature_To_Supported_List(driveInfo->featuresSupported, &driveInfo->numberOfFeaturesSupported, "Storage Element Depopulation + Restore");
                    }
                    else
                    {
                        add_Feature_To_Supported_List(driveInfo->featuresSupported, &driveInfo->numberOfFeaturesSupported, "Storage Element Depopulation");
                    }
                }
                //Add checking that this is zbd first?
                if (scsiInfo->version >= 5 && scsiInfo->peripheralDeviceType == PERIPHERAL_HOST_MANAGED_ZONED_BLOCK_DEVICE && SUCCESS == scsi_Report_Supported_Operation_Codes(device, false, REPORT_OPERATION_CODE_AND_SERVICE_ACTION, 0x9E, 0x1A, 20, supportedCommands))
                {
                    switch (supportedCommands[1] & 0x07)
                    {
                    case 0: //not available right now...so not supported
                    case 1://not supported
                        break;
                    case 3://supported according to spec
                    case 5://supported in vendor specific manor in same format as case 3
                        add_Feature_To_Supported_List(driveInfo->featuresSupported, &driveInfo->numberOfFeaturesSupported, "Remove Element and Modify Zones");
                        break;
                    default:
                        break;
                    }
                }
                if (scsiInfo->zoneDomainsOrRealms && scsiInfo->peripheralDeviceType == PERIPHERAL_HOST_MANAGED_ZONED_BLOCK_DEVICE)
                {
                    if (scsiInfo->version >= 5 && SUCCESS == scsi_Report_Supported_Operation_Codes(device, false, REPORT_OPERATION_CODE_AND_SERVICE_ACTION, 0x95, 0x07, 20, supportedCommands))
                    {
                        switch (supportedCommands[1] & 0x07)
                        {
                        case 0: //not available right now...so not supported
                        case 1://not supported
                            break;
                        case 3://supported according to spec
                        case 5://supported in vendor specific manor in same format as case 3
                            add_Feature_To_Supported_List(driveInfo->featuresSupported, &driveInfo->numberOfFeaturesSupported, "Zone Domains");
                            break;
                        default:
                            break;
                        }
                    }

                    if (scsiInfo->version >= 5 && SUCCESS == scsi_Report_Supported_Operation_Codes(device, false, REPORT_OPERATION_CODE_AND_SERVICE_ACTION, 0x95, 0x06, 20, supportedCommands))
                    {
                        switch (supportedCommands[1] & 0x07)
                        {
                        case 0: //not available right now...so not supported
                        case 1://not supported
                            break;
                        case 3://supported according to spec
                        case 5://supported in vendor specific manor in same format as case 3
                            add_Feature_To_Supported_List(driveInfo->featuresSupported, &driveInfo->numberOfFeaturesSupported, "Zone Realms");
                            break;
                        default:
                            break;
                        }
                    }
                }

                if (!driveInfo->securityInfo.securityProtocolInfoValid)
                {
                    //Check security protocol in case the earlier attempt did not work to detect when the OS/driver/HBA are blocking these commands
                    if (scsiInfo->version >= 6 && SUCCESS == scsi_Report_Supported_Operation_Codes(device, false, REPORT_OPERATION_CODE, 0xA2, 0, 16, supportedCommands))
                    {
                        switch (supportedCommands[1] & 0x07)
                        {
                        case 0: //not available right now...so not supported
                        case 1://not supported
                            break;
                        case 3://supported according to spec
                        case 5://supported in vendor specific manor in same format as case 3
                            driveInfo->trustedCommandsBeingBlocked = true;
                            break;
                        default:
                            break;
                        }
                    }
                }

                //check write buffer (firmware download) call info firmware download.h for this information.
                supportedDLModes supportedDLModes;
                memset(&supportedDLModes, 0, sizeof(supportedDLModes));
                supportedDLModes.size = sizeof(supportedDLModes);
                supportedDLModes.version = SUPPORTED_FWDL_MODES_VERSION;
                //change the device type to scsi before we enter here! Doing this so that --satinfo is correct!
                eDriveType tempDevType = device->drive_info.drive_type;
                device->drive_info.drive_type = SCSI_DRIVE;
                if (SUCCESS == get_Supported_FWDL_Modes(device, &supportedDLModes))
                {
                    driveInfo->fwdlSupport.downloadSupported = supportedDLModes.downloadMicrocodeSupported;
                    driveInfo->fwdlSupport.segmentedSupported = supportedDLModes.segmented;
                    driveInfo->fwdlSupport.deferredSupported = supportedDLModes.deferred;
                    driveInfo->fwdlSupport.dmaModeSupported = supportedDLModes.firmwareDownloadDMACommandSupported;
                    driveInfo->fwdlSupport.seagateDeferredPowerCycleRequired = supportedDLModes.seagateDeferredPowerCycleActivate;
                }
                device->drive_info.drive_type = tempDevType;
                //ATA Passthrough commands
                if (scsiInfo->version >= 5 && SUCCESS == scsi_Report_Supported_Operation_Codes(device, false, REPORT_OPERATION_CODE, ATA_PASS_THROUGH_12, 0, 16, supportedCommands))
                {
                    switch (supportedCommands[1] & 0x07)
                    {
                    case 0: //not available right now...so not supported
                    case 1://not supported
                        break;
                    case 3://supported according to spec
                    case 5://supported in vendor specific mannor in same format as case 3
                        //TODO: make sure this isn't the "blank" command being supported by a MMC device.
                        add_Feature_To_Supported_List(driveInfo->featuresSupported, &driveInfo->numberOfFeaturesSupported, "ATA Pass-Through 12");
                        break;
                    default:
                        break;
                    }
                }
                if (scsiInfo->version >= 5 && SUCCESS == scsi_Report_Supported_Operation_Codes(device, false, REPORT_OPERATION_CODE, ATA_PASS_THROUGH_16, 0, 20, supportedCommands))
                {
                    switch (supportedCommands[1] & 0x07)
                    {
                    case 0: //not available right now...so not supported
                    case 1://not supported
                        break;
                    case 3://supported according to spec
                    case 5://supported in vendor specific mannor in same format as case 3
                        add_Feature_To_Supported_List(driveInfo->featuresSupported, &driveInfo->numberOfFeaturesSupported, "ATA Pass-Through 16");
                        break;
                    default:
                        break;
                    }
                }
                if (scsiInfo->version >= 5 && SUCCESS == scsi_Report_Supported_Operation_Codes(device, false, REPORT_OPERATION_CODE_AND_SERVICE_ACTION, 0x7F, 0x1FF0, 36, supportedCommands))
                {
                    switch (supportedCommands[1] & 0x07)
                    {
                    case 0: //not available right now...so not supported
                    case 1://not supported
                        break;
                    case 3://supported according to spec
                    case 5://supported in vendor specific mannor in same format as case 3
                        add_Feature_To_Supported_List(driveInfo->featuresSupported, &driveInfo->numberOfFeaturesSupported, "ATA Pass-Through 32");
                        break;
                    default:
                        break;
                    }
                }
                safe_free_aligned(&supportedCommands);
            }
        }
    }
    return ret;
}

eReturnValues get_SCSI_Drive_Information(tDevice* device, ptrDriveInformationSAS_SATA driveInfo)
{
    eReturnValues ret = SUCCESS;
    if (!driveInfo)
    {
        return BAD_PARAMETER;
    }
    memset(driveInfo, 0, sizeof(driveInformationSAS_SATA));
    scsiIdentifyInfo scsiInfo;
    memset(&scsiInfo, 0, sizeof(scsiIdentifyInfo));
    //start with standard inquiry data
    uint8_t* inquiryData = C_CAST(uint8_t*, safe_calloc_aligned(255, sizeof(uint8_t), device->os_info.minimumAlignment));
    if (inquiryData)
    {
        if (SUCCESS == scsi_Inquiry(device, inquiryData, 255, 0, false, false))
        {
            get_SCSI_Inquiry_Data(driveInfo, &scsiInfo, inquiryData, 255);
        }
        safe_free_aligned(&inquiryData);
    }
    memcpy(&driveInfo->adapterInformation, &device->drive_info.adapter_info, sizeof(adapterInfo));

    //TODO: add checking peripheral device type as well to make sure it's only direct access and zoned block devices?
    if ((device->drive_info.interface_type == SCSI_INTERFACE || device->drive_info.interface_type == RAID_INTERFACE) && (device->drive_info.drive_type != ATA_DRIVE && device->drive_info.drive_type != NVME_DRIVE))
    {
        //send report luns to see how many luns are attached. This SHOULD be the way to detect multi-actuator drives for now. This could change in the future.
        //TODO: Find a better way to remove the large check above which may not work out well in some cases, but should reduce false detection on USB among other interfaces
        driveInfo->lunCount = get_LUN_Count(device);
    }

    get_SCSI_VPD_Data(device, driveInfo, &scsiInfo);

    get_SCSI_Read_Capacity_Data(device, driveInfo, &scsiInfo);

    if (scsiInfo.version == 6 && (device->drive_info.passThroughHacks.scsiHacks.securityProtocolSupported || SUCCESS == scsi_SecurityProtocol_In(device, SECURITY_PROTOCOL_INFORMATION, 0, false, 0, M_NULLPTR))) //security protocol commands introduced in SPC4. TODO: may need to drop to SPC3 for some devices. Need to investigate
    {
        //Check for TCG support - try sending a security protocol in command to get the list of security protocols (check for security protocol EFh? We can do that for ATA Security information)
        uint8_t* securityProtocols = C_CAST(uint8_t*, safe_calloc_aligned(512, sizeof(uint8_t), device->os_info.minimumAlignment));
        if (securityProtocols)
        {
            if (SUCCESS == scsi_SecurityProtocol_In(device, SECURITY_PROTOCOL_INFORMATION, 0, false, 512, securityProtocols))
            {
                if (SUCCESS == get_Security_Features_From_Security_Protocol(device, &driveInfo->securityInfo, securityProtocols, 512))
                {
                    if (driveInfo->securityInfo.tcg)
                    {
                        driveInfo->encryptionSupport = ENCRYPTION_SELF_ENCRYPTING;
                        add_Feature_To_Supported_List(driveInfo->featuresSupported, &driveInfo->numberOfFeaturesSupported, "TCG");
                    }
                    if (driveInfo->securityInfo.cbcs)
                    {
                        add_Feature_To_Supported_List(driveInfo->featuresSupported, &driveInfo->numberOfFeaturesSupported, "CbCS");
                    }
                    if (driveInfo->securityInfo.tapeEncryption)
                    {
                        add_Feature_To_Supported_List(driveInfo->featuresSupported, &driveInfo->numberOfFeaturesSupported, "Tape Encryption");
                    }
                    if (driveInfo->securityInfo.dataEncryptionConfig)
                    {
                        add_Feature_To_Supported_List(driveInfo->featuresSupported, &driveInfo->numberOfFeaturesSupported, "Data Encryption Configuration");
                    }
                    if (driveInfo->securityInfo.saCreationCapabilities)
                    {
                        add_Feature_To_Supported_List(driveInfo->featuresSupported, &driveInfo->numberOfFeaturesSupported, "SA Creation Capabilities");
                    }
                    if (driveInfo->securityInfo.ikev2scsi)
                    {
                        add_Feature_To_Supported_List(driveInfo->featuresSupported, &driveInfo->numberOfFeaturesSupported, "IKE V2 SCSI");
                    }
                    if (driveInfo->securityInfo.sdAssociation)
                    {
                        add_Feature_To_Supported_List(driveInfo->featuresSupported, &driveInfo->numberOfFeaturesSupported, "SD Association");
                    }
                    if (driveInfo->securityInfo.dmtfSecurity)
                    {
                        add_Feature_To_Supported_List(driveInfo->featuresSupported, &driveInfo->numberOfFeaturesSupported, "DMTF Security");
                    }
                    if (driveInfo->securityInfo.nvmeReserved)
                    {
                        add_Feature_To_Supported_List(driveInfo->featuresSupported, &driveInfo->numberOfFeaturesSupported, "NVMe Reserved");
                    }
                    if (driveInfo->securityInfo.nvme)
                    {
                        add_Feature_To_Supported_List(driveInfo->featuresSupported, &driveInfo->numberOfFeaturesSupported, "NVMe RPMB");
                    }
                    if (driveInfo->securityInfo.scsa)
                    {
                        add_Feature_To_Supported_List(driveInfo->featuresSupported, &driveInfo->numberOfFeaturesSupported, "SCSA");
                    }
                    if (driveInfo->securityInfo.jedecUFS)
                    {
                        add_Feature_To_Supported_List(driveInfo->featuresSupported, &driveInfo->numberOfFeaturesSupported, "JEDEC UFS");
                    }
                    if (driveInfo->securityInfo.sdTrustedFlash)
                    {
                        add_Feature_To_Supported_List(driveInfo->featuresSupported, &driveInfo->numberOfFeaturesSupported, "SD Trusted Flash");
                    }
                    if (driveInfo->securityInfo.ieee1667)
                    {
                        add_Feature_To_Supported_List(driveInfo->featuresSupported, &driveInfo->numberOfFeaturesSupported, "IEEE 1667");
                    }
                    if (driveInfo->securityInfo.ataDeviceServer)
                    {
                        add_Feature_To_Supported_List(driveInfo->featuresSupported, &driveInfo->numberOfFeaturesSupported, "ATA Security");
                        memcpy(&driveInfo->ataSecurityInformation, &driveInfo->securityInfo.ataSecurityInfo, sizeof(ataSecurityStatus));
                    }
                }
            }
            safe_free_aligned(&securityProtocols);
        }
    }
    driveInfo->percentEnduranceUsed = -1;//set to this to filter out later

    if (scsiInfo.version == 2)
    {
        //Check for persistent reservation support
        if (SUCCESS == scsi_Persistent_Reserve_In(device, SCSI_PERSISTENT_RESERVE_IN_READ_KEYS, 0, M_NULLPTR))
        {
            add_Feature_To_Supported_List(driveInfo->featuresSupported, &driveInfo->numberOfFeaturesSupported, "Persistent Reservations");
        }
    }

    get_SCSI_Log_Data(device, driveInfo, &scsiInfo);

    get_SCSI_Mode_Data(device, driveInfo, &scsiInfo);

    if (!driveInfo->interfaceSpeedInfo.speedIsValid)
    {
        //these old standards didn't report it, but we can reasonably guess the speed
        if (scsiInfo.version == 1)
        {
            driveInfo->interfaceSpeedInfo.speedIsValid = true;
            driveInfo->interfaceSpeedInfo.speedType = INTERFACE_SPEED_PARALLEL;
            driveInfo->interfaceSpeedInfo.parallelSpeed.maxSpeed = 5.0;
            snprintf(driveInfo->interfaceSpeedInfo.parallelSpeed.maxModeName, PARALLEL_INTERFACE_MODE_NAME_MAX_LENGTH, "FAST-5");
            driveInfo->interfaceSpeedInfo.parallelSpeed.maxModeNameValid = true;
        }
        else if (scsiInfo.version >= 2 && scsiInfo.version <= 4)
        {
            driveInfo->interfaceSpeedInfo.speedIsValid = true;
            driveInfo->interfaceSpeedInfo.speedType = INTERFACE_SPEED_PARALLEL;
            driveInfo->interfaceSpeedInfo.parallelSpeed.maxSpeed = 10.0;
            snprintf(driveInfo->interfaceSpeedInfo.parallelSpeed.maxModeName, PARALLEL_INTERFACE_MODE_NAME_MAX_LENGTH, "FAST-10");
            driveInfo->interfaceSpeedInfo.parallelSpeed.maxModeNameValid = true;
        }
    }

    get_SCSI_Diagnostic_Data(device, driveInfo, &scsiInfo);

    get_SCSI_Report_Op_Codes_Data(device, driveInfo, &scsiInfo);

    driveInfo->lowCurrentSpinupValid = false;
    return ret;
}

//currently using the bitfields in here, other commands are sometimes run to read additional information
//may need to reorganize more in the future to eliminate needing to pass in tDevice -TJE
static eReturnValues get_NVMe_Controller_Identify_Data(tDevice *device, ptrDriveInformationNVMe driveInfo, uint8_t* nvmeIdentifyData, uint32_t identifyDataLength)
{
    eReturnValues ret = SUCCESS;
    if (!device || !driveInfo || !nvmeIdentifyData || identifyDataLength != NVME_IDENTIFY_DATA_LEN)
    {
        return BAD_PARAMETER;
    }
    //MN
    memcpy(driveInfo->controllerData.modelNumber, &nvmeIdentifyData[24], 40);
    remove_Leading_And_Trailing_Whitespace(driveInfo->controllerData.modelNumber);
    //SN
    memcpy(driveInfo->controllerData.serialNumber, &nvmeIdentifyData[4], 20);
    remove_Leading_And_Trailing_Whitespace(driveInfo->controllerData.serialNumber);
    //FW
    memcpy(driveInfo->controllerData.firmwareRevision, &nvmeIdentifyData[64], 8);
    remove_Leading_And_Trailing_Whitespace(driveInfo->controllerData.firmwareRevision);
    //vid
    driveInfo->controllerData.pciVendorID = M_BytesTo2ByteValue(nvmeIdentifyData[1], nvmeIdentifyData[0]);
    //ssvid
    driveInfo->controllerData.pciSubsystemVendorID = M_BytesTo2ByteValue(nvmeIdentifyData[3], nvmeIdentifyData[2]);
    //IEEE OUI
    driveInfo->controllerData.ieeeOUI = M_BytesTo4ByteValue(0, nvmeIdentifyData[75], nvmeIdentifyData[74], nvmeIdentifyData[73]);
    //controller ID
    driveInfo->controllerData.controllerID = M_BytesTo2ByteValue(nvmeIdentifyData[79], nvmeIdentifyData[78]);
    //version
    driveInfo->controllerData.majorVersion = M_BytesTo2ByteValue(nvmeIdentifyData[83], nvmeIdentifyData[82]);
    driveInfo->controllerData.minorVersion = nvmeIdentifyData[81];
    driveInfo->controllerData.tertiaryVersion = nvmeIdentifyData[80];
    driveInfo->controllerData.numberOfPowerStatesSupported = nvmeIdentifyData[263] + 1;
    if (nvmeIdentifyData[96] & BIT0)
    {
        driveInfo->controllerData.hostIdentifierSupported = true;
        //host identifier is supported
        nvmeFeaturesCmdOpt getHostIdentifier;
        memset(&getHostIdentifier, 0, sizeof(nvmeFeaturesCmdOpt));
        getHostIdentifier.fid = 0x81;
        getHostIdentifier.sel = 0;//current data
        DECLARE_ZERO_INIT_ARRAY(uint8_t, hostIdentifier, 16);
        getHostIdentifier.dataPtr = hostIdentifier;
        getHostIdentifier.dataLength = 16;
        if (SUCCESS == nvme_Get_Features(device, &getHostIdentifier))
        {
            memcpy(&driveInfo->controllerData.hostIdentifier, hostIdentifier, 16);
            if (getHostIdentifier.featSetGetValue & BIT0)
            {
                driveInfo->controllerData.hostIdentifierIs128Bits = true;
            }
        }
    }
    //fguid (This field is big endian)
    driveInfo->controllerData.fguid[0] = nvmeIdentifyData[112];
    driveInfo->controllerData.fguid[1] = nvmeIdentifyData[113];
    driveInfo->controllerData.fguid[2] = nvmeIdentifyData[114];
    driveInfo->controllerData.fguid[3] = nvmeIdentifyData[115];
    driveInfo->controllerData.fguid[4] = nvmeIdentifyData[116];
    driveInfo->controllerData.fguid[5] = nvmeIdentifyData[117];
    driveInfo->controllerData.fguid[6] = nvmeIdentifyData[118];
    driveInfo->controllerData.fguid[7] = nvmeIdentifyData[119];
    driveInfo->controllerData.fguid[8] = nvmeIdentifyData[120];
    driveInfo->controllerData.fguid[9] = nvmeIdentifyData[121];
    driveInfo->controllerData.fguid[10] = nvmeIdentifyData[122];
    driveInfo->controllerData.fguid[11] = nvmeIdentifyData[123];
    driveInfo->controllerData.fguid[12] = nvmeIdentifyData[124];
    driveInfo->controllerData.fguid[13] = nvmeIdentifyData[125];
    driveInfo->controllerData.fguid[14] = nvmeIdentifyData[126];
    driveInfo->controllerData.fguid[15] = nvmeIdentifyData[127];
    //warning composite temperature
    driveInfo->controllerData.warningCompositeTemperatureThreshold = M_BytesTo2ByteValue(nvmeIdentifyData[267], nvmeIdentifyData[266]);
    //critical composite temperature
    driveInfo->controllerData.criticalCompositeTemperatureThreshold = M_BytesTo2ByteValue(nvmeIdentifyData[269], nvmeIdentifyData[268]);
    //total nvm capacity
    for (uint8_t i = 0; i < 16; ++i)
    {
        driveInfo->controllerData.totalNVMCapacity[i] = nvmeIdentifyData[295 + i];
    }
    driveInfo->controllerData.totalNVMCapacityD = convert_128bit_to_double(&driveInfo->controllerData.totalNVMCapacity[0]);
    //unallocated nvm capacity
    for (uint8_t i = 0; i < 16; ++i)
    {
        driveInfo->controllerData.unallocatedNVMCapacity[i] = nvmeIdentifyData[296 + i];
    }
    driveInfo->controllerData.unallocatedNVMCapacityD = convert_128bit_to_double(&driveInfo->controllerData.unallocatedNVMCapacity[0]);
    //DST info
    if (nvmeIdentifyData[256] & BIT4)//DST command is supported
    {
        //set Long DST Time before reading the log
        driveInfo->controllerData.longDSTTimeMinutes = M_BytesTo2ByteValue(nvmeIdentifyData[317], nvmeIdentifyData[316]);
        //Read the NVMe DST log
        DECLARE_ZERO_INIT_ARRAY(uint8_t, nvmeDSTLog, 564);
        nvmeGetLogPageCmdOpts dstLogOpts;
        memset(&dstLogOpts, 0, sizeof(nvmeGetLogPageCmdOpts));
        dstLogOpts.addr = nvmeDSTLog;
        dstLogOpts.dataLen = 564;
        dstLogOpts.lid = 6;
        dstLogOpts.nsid = NVME_ALL_NAMESPACES;//controller data
        if (SUCCESS == nvme_Get_Log_Page(device, &dstLogOpts))
        {
            driveInfo->dstInfo.informationValid = true;
            //Bytes 31:4 hold the latest DST run information
            uint32_t latestDSTOffset = 4;
            uint8_t status = M_Nibble0(nvmeDSTLog[latestDSTOffset + 0]);
            if (status != 0x0F)//a status of F means this is an unused entry
            {
                driveInfo->dstInfo.resultOrStatus = status;
                driveInfo->dstInfo.testNumber = M_Nibble1(nvmeDSTLog[latestDSTOffset + 0]);
                driveInfo->dstInfo.powerOnHours = M_BytesTo8ByteValue(nvmeDSTLog[latestDSTOffset + 11], nvmeDSTLog[latestDSTOffset + 10], nvmeDSTLog[latestDSTOffset + 9], nvmeDSTLog[latestDSTOffset + 8], nvmeDSTLog[latestDSTOffset + 7], nvmeDSTLog[latestDSTOffset + 6], nvmeDSTLog[latestDSTOffset + 5], nvmeDSTLog[latestDSTOffset + 4]);
                if (nvmeDSTLog[latestDSTOffset + 2] & BIT1)
                {
                    driveInfo->dstInfo.errorLBA = M_BytesTo8ByteValue(nvmeDSTLog[latestDSTOffset + 23], nvmeDSTLog[latestDSTOffset + 22], nvmeDSTLog[latestDSTOffset + 12], nvmeDSTLog[latestDSTOffset + 20], nvmeDSTLog[latestDSTOffset + 19], nvmeDSTLog[latestDSTOffset + 18], nvmeDSTLog[latestDSTOffset + 17], nvmeDSTLog[latestDSTOffset + 16]);
                }
                else
                {
                    driveInfo->dstInfo.errorLBA = UINT64_MAX;
                }
            }
        }
    }
    //Sanitize
    if (nvmeIdentifyData[328] & BIT0)//Sanitize supported
    {
        add_Feature_To_Supported_List(driveInfo->controllerData.controllerFeaturesSupported, &driveInfo->controllerData.numberOfControllerFeatures, "Sanitize");
    }
    //max namespaces
    driveInfo->controllerData.maxNumberOfNamespaces = M_BytesTo4ByteValue(nvmeIdentifyData[519], nvmeIdentifyData[518], nvmeIdentifyData[517], nvmeIdentifyData[516]);
    //volatile write cache
    if (nvmeIdentifyData[525] & BIT0)
    {
        driveInfo->controllerData.volatileWriteCacheSupported = true;
        nvmeFeaturesCmdOpt getWriteCache;
        memset(&getWriteCache, 0, sizeof(nvmeFeaturesCmdOpt));
        getWriteCache.fid = 0x06;
        getWriteCache.sel = 0;//current data
        if (SUCCESS == nvme_Get_Features(device, &getWriteCache))
        {
            if (getWriteCache.featSetGetValue & BIT0)
            {
                driveInfo->controllerData.volatileWriteCacheEnabled = true;
            }
            else
            {
                driveInfo->controllerData.volatileWriteCacheEnabled = false;
            }
        }
        else
        {
            driveInfo->controllerData.volatileWriteCacheSupported = false;
        }
    }
    //nvm subsystem qualified name
    memcpy(driveInfo->controllerData.nvmSubsystemNVMeQualifiedName, &nvmeIdentifyData[768], 256);
    //firmware slots
    driveInfo->controllerData.numberOfFirmwareSlots = M_GETBITRANGE(nvmeIdentifyData[260], 3, 1);
    //Add in other controller "Features" as needed
    if (nvmeIdentifyData[256] & BIT0)
    {
        //Supports security send/receive. Check for TCG and other security protocols
        DECLARE_ZERO_INIT_ARRAY(uint8_t, supportedSecurityProtocols, LEGACY_DRIVE_SEC_SIZE);
        if (SUCCESS == nvme_Security_Receive(device, SECURITY_PROTOCOL_INFORMATION, 0, 0, supportedSecurityProtocols, 512))
        {
            if (SUCCESS == get_Security_Features_From_Security_Protocol(device, &driveInfo->securityInfo, supportedSecurityProtocols, 512))
            {
                if (driveInfo->securityInfo.tcg)
                {
                    driveInfo->controllerData.encryptionSupport = ENCRYPTION_SELF_ENCRYPTING;
                    add_Feature_To_Supported_List(driveInfo->controllerData.controllerFeaturesSupported, &driveInfo->controllerData.numberOfControllerFeatures, "TCG");
                }
                if (driveInfo->securityInfo.cbcs)
                {
                    add_Feature_To_Supported_List(driveInfo->controllerData.controllerFeaturesSupported, &driveInfo->controllerData.numberOfControllerFeatures, "CbCS");
                }
                if (driveInfo->securityInfo.tapeEncryption)
                {
                    add_Feature_To_Supported_List(driveInfo->controllerData.controllerFeaturesSupported, &driveInfo->controllerData.numberOfControllerFeatures, "Tape Encryption");
                }
                if (driveInfo->securityInfo.dataEncryptionConfig)
                {
                    add_Feature_To_Supported_List(driveInfo->controllerData.controllerFeaturesSupported, &driveInfo->controllerData.numberOfControllerFeatures, "Data Encryption Configuration");
                }
                if (driveInfo->securityInfo.saCreationCapabilities)
                {
                    add_Feature_To_Supported_List(driveInfo->controllerData.controllerFeaturesSupported, &driveInfo->controllerData.numberOfControllerFeatures, "SA Creation Capabilities");
                }
                if (driveInfo->securityInfo.ikev2scsi)
                {
                    add_Feature_To_Supported_List(driveInfo->controllerData.controllerFeaturesSupported, &driveInfo->controllerData.numberOfControllerFeatures, "IKE V2 SCSI");
                }
                if (driveInfo->securityInfo.sdAssociation)
                {
                    add_Feature_To_Supported_List(driveInfo->controllerData.controllerFeaturesSupported, &driveInfo->controllerData.numberOfControllerFeatures, "SD Association");
                }
                if (driveInfo->securityInfo.dmtfSecurity)
                {
                    add_Feature_To_Supported_List(driveInfo->controllerData.controllerFeaturesSupported, &driveInfo->controllerData.numberOfControllerFeatures, "DMTF Security");
                }
                if (driveInfo->securityInfo.nvmeReserved)
                {
                    add_Feature_To_Supported_List(driveInfo->controllerData.controllerFeaturesSupported, &driveInfo->controllerData.numberOfControllerFeatures, "NVMe Reserved");
                }
                if (driveInfo->securityInfo.nvme)
                {
                    add_Feature_To_Supported_List(driveInfo->controllerData.controllerFeaturesSupported, &driveInfo->controllerData.numberOfControllerFeatures, "NVMe RPMB");
                }
                if (driveInfo->securityInfo.scsa)
                {
                    add_Feature_To_Supported_List(driveInfo->controllerData.controllerFeaturesSupported, &driveInfo->controllerData.numberOfControllerFeatures, "SCSA");
                }
                if (driveInfo->securityInfo.jedecUFS)
                {
                    add_Feature_To_Supported_List(driveInfo->controllerData.controllerFeaturesSupported, &driveInfo->controllerData.numberOfControllerFeatures, "JEDEC UFS");
                }
                if (driveInfo->securityInfo.sdTrustedFlash)
                {
                    add_Feature_To_Supported_List(driveInfo->controllerData.controllerFeaturesSupported, &driveInfo->controllerData.numberOfControllerFeatures, "SD Trusted Flash");
                }
                if (driveInfo->securityInfo.ieee1667)
                {
                    add_Feature_To_Supported_List(driveInfo->controllerData.controllerFeaturesSupported, &driveInfo->controllerData.numberOfControllerFeatures, "IEEE 1667");
                }
                if (driveInfo->securityInfo.ataDeviceServer)
                {
                    add_Feature_To_Supported_List(driveInfo->controllerData.controllerFeaturesSupported, &driveInfo->controllerData.numberOfControllerFeatures, "ATA Security");
                }
            }
        }
        else
        {
            //NOTE: For whatever reason the security commands did not complete despite the drive supporting them to read the list of supported protocols
            //set the "blocked commaands" flag
            // This is not currently enabled as it has not been observed in any system yet like it has for ATA and SCSI
            //driveInfo->trustedCommandsBeingBlocked = true;
        }
    }
    if (nvmeIdentifyData[256] & BIT1)
    {
        add_Feature_To_Supported_List(driveInfo->controllerData.controllerFeaturesSupported, &driveInfo->controllerData.numberOfControllerFeatures, "Format NVM");
    }
    if (nvmeIdentifyData[256] & BIT2)
    {
        add_Feature_To_Supported_List(driveInfo->controllerData.controllerFeaturesSupported, &driveInfo->controllerData.numberOfControllerFeatures, "Firmware Update");
    }
    if (nvmeIdentifyData[256] & BIT3)
    {
        add_Feature_To_Supported_List(driveInfo->controllerData.controllerFeaturesSupported, &driveInfo->controllerData.numberOfControllerFeatures, "Namespace Management");
    }
    if (nvmeIdentifyData[256] & BIT4)
    {
        add_Feature_To_Supported_List(driveInfo->controllerData.controllerFeaturesSupported, &driveInfo->controllerData.numberOfControllerFeatures, "Device Self Test");
    }
    if (nvmeIdentifyData[256] & BIT7)
    {
        add_Feature_To_Supported_List(driveInfo->controllerData.controllerFeaturesSupported, &driveInfo->controllerData.numberOfControllerFeatures, "Virtualization Management");
    }
    if (nvmeIdentifyData[257] & BIT1)
    {
        add_Feature_To_Supported_List(driveInfo->controllerData.controllerFeaturesSupported, &driveInfo->controllerData.numberOfControllerFeatures, "Doorbell Buffer Config");
    }

    //Before we memset the identify data, add some namespace features
    if (nvmeIdentifyData[520] & BIT1)
    {
        add_Feature_To_Supported_List(driveInfo->namespaceData.namespaceFeaturesSupported, &driveInfo->namespaceData.numberOfNamespaceFeatures, "Write Uncorrectable");
    }
    if (nvmeIdentifyData[520] & BIT2)
    {
        add_Feature_To_Supported_List(driveInfo->namespaceData.namespaceFeaturesSupported, &driveInfo->namespaceData.numberOfNamespaceFeatures, "Dataset Management");
    }
    if (nvmeIdentifyData[520] & BIT3)
    {
        add_Feature_To_Supported_List(driveInfo->namespaceData.namespaceFeaturesSupported, &driveInfo->namespaceData.numberOfNamespaceFeatures, "Write Zeros");
    }
    if (nvmeIdentifyData[520] & BIT5)
    {
        add_Feature_To_Supported_List(driveInfo->namespaceData.namespaceFeaturesSupported, &driveInfo->namespaceData.numberOfNamespaceFeatures, "Persistent Reservations");
    }
    return ret;
}

static eReturnValues get_NVMe_Namespace_Identify_Data(ptrDriveInformationNVMe driveInfo, uint8_t* nvmeIdentifyData, uint32_t identifyDataLength)
{
    eReturnValues ret = SUCCESS;
    if (!driveInfo || !nvmeIdentifyData || identifyDataLength != NVME_IDENTIFY_DATA_LEN)
    {
        return BAD_PARAMETER;
    }
    driveInfo->namespaceData.valid = true;
    driveInfo->namespaceData.namespaceSize = M_BytesTo8ByteValue(nvmeIdentifyData[7], nvmeIdentifyData[6], nvmeIdentifyData[5], nvmeIdentifyData[4], nvmeIdentifyData[3], nvmeIdentifyData[2], nvmeIdentifyData[1], nvmeIdentifyData[0]) - 1;//spec says this is 0 to (n-1)!
    driveInfo->namespaceData.namespaceCapacity = M_BytesTo8ByteValue(nvmeIdentifyData[15], nvmeIdentifyData[14], nvmeIdentifyData[13], nvmeIdentifyData[12], nvmeIdentifyData[11], nvmeIdentifyData[10], nvmeIdentifyData[9], nvmeIdentifyData[8]);
    driveInfo->namespaceData.namespaceUtilization = M_BytesTo8ByteValue(nvmeIdentifyData[23], nvmeIdentifyData[22], nvmeIdentifyData[21], nvmeIdentifyData[20], nvmeIdentifyData[19], nvmeIdentifyData[18], nvmeIdentifyData[17], nvmeIdentifyData[16]);
    //lba size & relative performance
    uint8_t numLBAFormats = nvmeIdentifyData[25];
    uint8_t lbaFormatIdentifier = M_Nibble0(nvmeIdentifyData[26]);
    if (numLBAFormats > 16)
    {
        lbaFormatIdentifier |= (M_GETBITRANGE(nvmeIdentifyData[26], 6, 5)) << 4;
    }
    //lba formats start at byte 128, and are 4 bytes in size each
    uint32_t lbaFormatOffset = UINT32_C(128) + (C_CAST(uint32_t, lbaFormatIdentifier) * UINT32_C(4));
    uint32_t lbaFormatData = M_BytesTo4ByteValue(nvmeIdentifyData[lbaFormatOffset + 3], nvmeIdentifyData[lbaFormatOffset + 2], nvmeIdentifyData[lbaFormatOffset + 1], nvmeIdentifyData[lbaFormatOffset + 0]);
    driveInfo->namespaceData.formattedLBASizeBytes = C_CAST(uint32_t, power_Of_Two(M_GETBITRANGE(lbaFormatData, 23, 16)));
    driveInfo->namespaceData.relativeFormatPerformance = M_GETBITRANGE(lbaFormatData, 25, 24);
    //nvm capacity
    for (uint8_t i = 0; i < 16; ++i)
    {
        driveInfo->namespaceData.nvmCapacity[i] = nvmeIdentifyData[48 + i];
    }
    driveInfo->namespaceData.nvmCapacityD = convert_128bit_to_double(&driveInfo->namespaceData.nvmCapacity[0]);
    //NGUID
    driveInfo->namespaceData.namespaceGloballyUniqueIdentifier[0] = nvmeIdentifyData[104];
    driveInfo->namespaceData.namespaceGloballyUniqueIdentifier[1] = nvmeIdentifyData[105];
    driveInfo->namespaceData.namespaceGloballyUniqueIdentifier[2] = nvmeIdentifyData[106];
    driveInfo->namespaceData.namespaceGloballyUniqueIdentifier[3] = nvmeIdentifyData[107];
    driveInfo->namespaceData.namespaceGloballyUniqueIdentifier[4] = nvmeIdentifyData[108];
    driveInfo->namespaceData.namespaceGloballyUniqueIdentifier[5] = nvmeIdentifyData[109];
    driveInfo->namespaceData.namespaceGloballyUniqueIdentifier[6] = nvmeIdentifyData[110];
    driveInfo->namespaceData.namespaceGloballyUniqueIdentifier[7] = nvmeIdentifyData[111];
    driveInfo->namespaceData.namespaceGloballyUniqueIdentifier[8] = nvmeIdentifyData[112];
    driveInfo->namespaceData.namespaceGloballyUniqueIdentifier[9] = nvmeIdentifyData[113];
    driveInfo->namespaceData.namespaceGloballyUniqueIdentifier[10] = nvmeIdentifyData[114];
    driveInfo->namespaceData.namespaceGloballyUniqueIdentifier[11] = nvmeIdentifyData[115];
    driveInfo->namespaceData.namespaceGloballyUniqueIdentifier[12] = nvmeIdentifyData[116];
    driveInfo->namespaceData.namespaceGloballyUniqueIdentifier[13] = nvmeIdentifyData[117];
    driveInfo->namespaceData.namespaceGloballyUniqueIdentifier[14] = nvmeIdentifyData[118];
    driveInfo->namespaceData.namespaceGloballyUniqueIdentifier[15] = nvmeIdentifyData[119];
    //EUI64
    driveInfo->namespaceData.ieeeExtendedUniqueIdentifier = M_BytesTo8ByteValue(nvmeIdentifyData[120], nvmeIdentifyData[121], nvmeIdentifyData[122], nvmeIdentifyData[123], nvmeIdentifyData[124], nvmeIdentifyData[125], nvmeIdentifyData[126], nvmeIdentifyData[127]);
    //Namespace "features"
    uint8_t protectionEnabled = M_GETBITRANGE(nvmeIdentifyData[29], 2, 0);
    if (nvmeIdentifyData[28] & BIT0)
    {
        if (protectionEnabled == 1)
        {
            add_Feature_To_Supported_List(driveInfo->namespaceData.namespaceFeaturesSupported, &driveInfo->namespaceData.numberOfNamespaceFeatures, "Protection Type 1 [Enabled]");
        }
        else
        {
            add_Feature_To_Supported_List(driveInfo->namespaceData.namespaceFeaturesSupported, &driveInfo->namespaceData.numberOfNamespaceFeatures, "Protection Type 1");
        }
    }
    if (nvmeIdentifyData[28] & BIT1)
    {
        if (protectionEnabled == 2)
        {
            add_Feature_To_Supported_List(driveInfo->namespaceData.namespaceFeaturesSupported, &driveInfo->namespaceData.numberOfNamespaceFeatures, "Protection Type 2 [Enabled]");
        }
        else
        {
            add_Feature_To_Supported_List(driveInfo->namespaceData.namespaceFeaturesSupported, &driveInfo->namespaceData.numberOfNamespaceFeatures, "Protection Type 2");
        }
    }
    if (nvmeIdentifyData[28] & BIT2)
    {
        if (protectionEnabled == 3)
        {
            add_Feature_To_Supported_List(driveInfo->namespaceData.namespaceFeaturesSupported, &driveInfo->namespaceData.numberOfNamespaceFeatures, "Protection Type 3 [Enabled]");
        }
        else
        {
            add_Feature_To_Supported_List(driveInfo->namespaceData.namespaceFeaturesSupported, &driveInfo->namespaceData.numberOfNamespaceFeatures, "Protection Type 3");
        }
    }
    if (nvmeIdentifyData[30] & BIT0)
    {
        add_Feature_To_Supported_List(driveInfo->namespaceData.namespaceFeaturesSupported, &driveInfo->namespaceData.numberOfNamespaceFeatures, "Namespace Sharing");
    }
    return ret;
}

//TODO: Move code in controller data reading DST log to here
static eReturnValues get_NVMe_Log_Data(tDevice* device, ptrDriveInformationNVMe driveInfo)
{
    eReturnValues ret = SUCCESS;
    if (!device || !driveInfo)
    {
        return BAD_PARAMETER;
    }
    //Data from SMART log page
    DECLARE_ZERO_INIT_ARRAY(uint8_t, nvmeSMARTData, 512);
    nvmeGetLogPageCmdOpts smartLogOpts;
    memset(&smartLogOpts, 0, sizeof(nvmeGetLogPageCmdOpts));
    smartLogOpts.addr = nvmeSMARTData;
    smartLogOpts.dataLen = 512;
    smartLogOpts.lid = 2;
    smartLogOpts.nsid = NVME_ALL_NAMESPACES;//controller data
    if (SUCCESS == nvme_Get_Log_Page(device, &smartLogOpts))
    {
        driveInfo->smartData.valid = true;
        if (nvmeSMARTData[0] == 0)
        {
            driveInfo->smartData.smartStatus = 0;
        }
        else
        {
            driveInfo->smartData.smartStatus = 1;
        }
        if (nvmeSMARTData[0] & BIT3)
        {
            driveInfo->smartData.mediumIsReadOnly = true;
        }
        driveInfo->smartData.compositeTemperatureKelvin = M_BytesTo2ByteValue(nvmeSMARTData[2], nvmeSMARTData[1]);
        driveInfo->smartData.availableSpacePercent = nvmeSMARTData[3];
        driveInfo->smartData.availableSpaceThresholdPercent = nvmeSMARTData[4];
        driveInfo->smartData.percentageUsed = nvmeSMARTData[5];
        //data units read/written
        for (uint8_t i = 0; i < 16; ++i)
        {
            driveInfo->smartData.dataUnitsRead[i] = nvmeSMARTData[32 + i];
            driveInfo->smartData.dataUnitsWritten[i] = nvmeSMARTData[48 + i];
            driveInfo->smartData.powerOnHours[i] = nvmeSMARTData[128 + i];
        }
        driveInfo->smartData.dataUnitsReadD = convert_128bit_to_double(driveInfo->smartData.dataUnitsRead);
        driveInfo->smartData.dataUnitsWrittenD = convert_128bit_to_double(driveInfo->smartData.dataUnitsWritten);
        driveInfo->smartData.powerOnHoursD = convert_128bit_to_double(driveInfo->smartData.powerOnHours);
    }
    else
    {
        driveInfo->smartData.smartStatus = 2;
    }
    return ret;
}

eReturnValues get_NVMe_Drive_Information(tDevice* device, ptrDriveInformationNVMe driveInfo)
{
    eReturnValues ret = NOT_SUPPORTED;
    if (!driveInfo)
    {
        return BAD_PARAMETER;
    }
    memset(driveInfo, 0, sizeof(driveInformationNVMe));
    //changing ret to success since we have passthrough available
    ret = SUCCESS;
    uint8_t* nvmeIdentifyData = C_CAST(uint8_t*, safe_calloc_aligned(NVME_IDENTIFY_DATA_LEN, sizeof(uint8_t), device->os_info.minimumAlignment));
    if (!nvmeIdentifyData)
    {
        return MEMORY_FAILURE;
    }
    if (SUCCESS == nvme_Identify(device, nvmeIdentifyData, 0, NVME_IDENTIFY_CTRL))
    {
        get_NVMe_Controller_Identify_Data(device, driveInfo, nvmeIdentifyData, NVME_IDENTIFY_DATA_LEN);
    }
    memset(nvmeIdentifyData, 0, NVME_IDENTIFY_DATA_LEN);
    if (SUCCESS == nvme_Identify(device, nvmeIdentifyData, device->drive_info.namespaceID, NVME_IDENTIFY_NS))
    {
        get_NVMe_Namespace_Identify_Data(driveInfo, nvmeIdentifyData, NVME_IDENTIFY_DATA_LEN);
    }
    safe_free_aligned(&nvmeIdentifyData);
    get_NVMe_Log_Data(device, driveInfo);
    return ret;
}


//This is for use with ATA or SCSI drives where we only want to show the applicable information for each drive type. NOT RECOMMENDED ON EXTERNAL USB/IEEE1394 PRODUCTS!
void print_Device_Information(ptrDriveInformation driveInfo)
{
    switch (driveInfo->infoType)
    {
    case DRIVE_INFO_SAS_SATA:
        print_SAS_Sata_Device_Information(&driveInfo->sasSata);
        break;
    case DRIVE_INFO_NVME:
        print_NVMe_Device_Information(&driveInfo->nvme);
        break;
    default:
        break;
    }
}

void print_NVMe_Device_Information(ptrDriveInformationNVMe driveInfo)
{
    printf("NVMe Controller Information:\n");
    printf("\tModel Number: %s\n", driveInfo->controllerData.modelNumber);
    printf("\tSerial Number: %s\n", driveInfo->controllerData.serialNumber);
    printf("\tFirmware Revision: %s\n", driveInfo->controllerData.firmwareRevision);
    printf("\tIEEE OUI: ");
    if (driveInfo->controllerData.ieeeOUI > 0)
    {
        printf("%06" PRIX32 "\n", driveInfo->controllerData.ieeeOUI);
    }
    else
    {
        printf("Not Supported\n");
    }
    printf("\tPCI Vendor ID: %04" PRIX16 "\n", driveInfo->controllerData.pciVendorID);
    printf("\tPCI Subsystem Vendor ID: %04" PRIX16 "\n", driveInfo->controllerData.pciSubsystemVendorID);
    printf("\tController ID: ");
    if (driveInfo->controllerData.controllerID > 0)
    {
        printf("%04" PRIX16 "\n", driveInfo->controllerData.controllerID);
    }
    else
    {
        printf("Not Supported\n");
    }
    printf("\tNVMe Version: ");
    if (driveInfo->controllerData.majorVersion > 0 || driveInfo->controllerData.minorVersion > 0 || driveInfo->controllerData.tertiaryVersion > 0)
    {
        printf("%" PRIu16 ".%" PRIu8 ".%" PRIu8 "\n", driveInfo->controllerData.majorVersion, driveInfo->controllerData.minorVersion, driveInfo->controllerData.tertiaryVersion);
    }
    else
    {
        printf("Not reported (NVMe 1.1 or older)\n");
    }
    if (driveInfo->controllerData.hostIdentifierSupported)
    {
        //TODO: Print out the host identifier
    }
    printf("\tFGUID: ");
    DECLARE_ZERO_INIT_ARRAY(uint8_t, zero128Bit, 16);
    if (memcmp(zero128Bit, driveInfo->controllerData.fguid, 16))
    {
        for (uint8_t i = 0; i < 16; ++i)
        {
            printf("%02" PRIX8, driveInfo->controllerData.fguid[i]);
        }
        printf("\n");
    }
    else
    {
        printf("Not Supported\n");
    }
    if (driveInfo->controllerData.totalNVMCapacityD > 0)
    {
        //convert this to an "easy" unit instead of tons and tons of bytes
        DECLARE_ZERO_INIT_ARRAY(char, mTotalCapUnits, UNIT_STRING_LENGTH);
        DECLARE_ZERO_INIT_ARRAY(char, totalCapUnits, UNIT_STRING_LENGTH);
        char* mTotalCapUnit = &mTotalCapUnits[0], *totalCapUnit = &totalCapUnits[0];
        double mTotalCapacity = driveInfo->controllerData.totalNVMCapacityD;
        double totalCapacity = mTotalCapacity;
        metric_Unit_Convert(&mTotalCapacity, &mTotalCapUnit);
        capacity_Unit_Convert(&totalCapacity, &totalCapUnit);
        printf("\tTotal NVM Capacity (%s/%s): %0.02f/%0.02f\n", mTotalCapUnit, totalCapUnit, mTotalCapacity, totalCapacity);
        if (driveInfo->controllerData.unallocatedNVMCapacityD > 0)
        {
            DECLARE_ZERO_INIT_ARRAY(char, mUnCapUnits, UNIT_STRING_LENGTH);
            DECLARE_ZERO_INIT_ARRAY(char, unCapUnits, UNIT_STRING_LENGTH);
            char* mUnCapUnit = &mUnCapUnits[0], *unCapUnit = &unCapUnits[0];
            double mUnCapacity = driveInfo->controllerData.unallocatedNVMCapacityD;
            double unCapacity = mUnCapacity;
            metric_Unit_Convert(&mUnCapacity, &mUnCapUnit);
            capacity_Unit_Convert(&unCapacity, &unCapUnit);
            printf("\tUnallocated NVM Capacity (%s/%s): %0.02f/%0.02f\n", mUnCapUnit, unCapUnit, mUnCapacity, unCapacity);
        }
    }
    printf("\tWrite Cache: ");
    if (driveInfo->controllerData.volatileWriteCacheSupported)
    {
        if (driveInfo->controllerData.volatileWriteCacheEnabled)
        {
            printf("Enabled\n");
        }
        else
        {
            printf("Disabled\n");
        }
    }
    else
    {
        printf("Not Supported\n");
    }
    printf("\tMaximum Number Of Namespaces: %" PRIu32 "\n", driveInfo->controllerData.maxNumberOfNamespaces);
    printf("\tNumber of supported power states: %" PRIu8 "\n", driveInfo->controllerData.numberOfPowerStatesSupported + 1);
    //Putting SMART & DST data here so that it isn't confused with the namespace data below - TJE
    if (driveInfo->smartData.valid)
    {
        printf("\tRead-Only Medium: ");
        if (driveInfo->smartData.mediumIsReadOnly)
        {
            printf("True\n");
        }
        else
        {
            printf("False\n");
        }
        printf("\tSMART Status: ");
        switch (driveInfo->smartData.smartStatus)
        {
        case 0:
            printf("Good\n");
            break;
        case 1:
            printf("Bad\n");
            break;
        case 2:
        default:
            printf("Unknown\n");
            break;
        }
        //kelvin_To_Celsius(&driveInfo->smartData.compositeTemperatureKelvin);
        printf("\tComposite Temperature (K): %" PRIu16 "\n", driveInfo->smartData.compositeTemperatureKelvin);
        printf("\tPercent Used (%%): %" PRIu8 "\n", driveInfo->smartData.percentageUsed);
        printf("\tAvailable Spare (%%): %" PRIu8 "\n", driveInfo->smartData.availableSpacePercent);
        uint16_t days = 0;
        uint8_t years = 0;
        uint8_t hours = 0;
        uint8_t minutes = 0;
        uint8_t seconds = 0;
        convert_Seconds_To_Displayable_Time_Double(driveInfo->smartData.powerOnHoursD * 3600.0, &years, &days, &hours, &minutes, &seconds);
        printf("\tPower On Time: ");
        print_Time_To_Screen(&years, &days, &hours, &minutes, &seconds);
        printf("\n");
        printf("\tPower On Hours (hours): %0.00f\n", driveInfo->smartData.powerOnHoursD);

        //Last DST information
        printf("\tLast DST information:\n");
        if (driveInfo->dstInfo.informationValid)
        {
            if (driveInfo->smartData.powerOnHoursD - C_CAST(double, (driveInfo->dstInfo.powerOnHours)) < driveInfo->smartData.powerOnHoursD)
            {
                double timeSinceLastDST = C_CAST(double, driveInfo->smartData.powerOnHoursD) - C_CAST(double, driveInfo->dstInfo.powerOnHours);
                printf("\t\tTime since last DST (hours): ");
                if (timeSinceLastDST >= 0)
                {
                    printf("%0.02f\n", timeSinceLastDST);
                }
                else
                {
                    printf("Indeterminate\n");
                }
                printf("\t\tDST Status/Result: 0x%" PRIX8 "\n", driveInfo->dstInfo.resultOrStatus);
                printf("\t\tDST Test run: 0x%" PRIX8 "\n", driveInfo->dstInfo.testNumber);
                if (driveInfo->dstInfo.resultOrStatus != 0 && driveInfo->dstInfo.resultOrStatus != 0xF && driveInfo->dstInfo.errorLBA != UINT64_MAX)
                {
                    //Show the Error LBA
                    printf("\t\tError occurred at LBA: %" PRIu64 "\n", driveInfo->dstInfo.errorLBA);
                }
            }
            else
            {
                printf("\t\tDST has never been run\n");
            }
        }
        else
        {
            printf("\t\tNot supported\n");
        }
        //Long DST time
        printf("\tLong Drive Self Test Time: ");
        if (driveInfo->controllerData.longDSTTimeMinutes > 0)
        {
            //print as hours:minutes
            years = 0;
            days = 0;
            hours = 0;
            minutes = 0;
            seconds = 0;
            convert_Seconds_To_Displayable_Time(driveInfo->controllerData.longDSTTimeMinutes * 60, &years, &days, &hours, &minutes, &seconds);
            print_Time_To_Screen(&years, &days, &hours, &minutes, &seconds);
        }
        else
        {
            printf("Not Supported");
        }
        printf("\n");

        //Workload Rate (Annualized)
        printf("\tAnnualized Workload Rate (TB/yr): ");
#ifndef MINUTES_IN_1_YEAR
#define MINUTES_IN_1_YEAR 525600.0
#endif // !MINUTES_IN_1_YEAR
        double totalTerabytesRead = (driveInfo->smartData.dataUnitsReadD * 512.0 * 1000.0) / 1000000000000.0;
        double totalTerabytesWritten = (driveInfo->smartData.dataUnitsWrittenD * 512.0 * 1000.0) / 1000000000000.0;
        double calculatedUsage = C_CAST(double, totalTerabytesRead + totalTerabytesWritten) * C_CAST(double, MINUTES_IN_1_YEAR / C_CAST(double, driveInfo->smartData.powerOnHoursD) * 60.0);
        printf("%0.02f\n", calculatedUsage);
        //Total Bytes Read
        printf("\tTotal Bytes Read ");
        double totalBytesRead = driveInfo->smartData.dataUnitsReadD * 512.0 * 1000.0;
        DECLARE_ZERO_INIT_ARRAY(char, unitReadString, UNIT_STRING_LENGTH);
        char* unitRead = &unitReadString[0];
        metric_Unit_Convert(&totalBytesRead, &unitRead);
        printf("(%s): %0.02f\n", unitRead, totalBytesRead);
        //Total Bytes Written
        printf("\tTotal Bytes Written ");
        double totalBytesWritten = driveInfo->smartData.dataUnitsWrittenD * 512.0 * 1000.0;
        DECLARE_ZERO_INIT_ARRAY(char, unitWrittenString, UNIT_STRING_LENGTH);
        char* unitWritten = &unitWrittenString[0];
        metric_Unit_Convert(&totalBytesWritten, &unitWritten);
        printf("(%s): %0.02f\n", unitWritten, totalBytesWritten);

    }
    //Encryption Support
    printf("\tEncryption Support: ");
    switch (driveInfo->controllerData.encryptionSupport)
    {
    case ENCRYPTION_SELF_ENCRYPTING:
        printf("Self Encrypting\n");
        /*if (driveInfo->trustedCommandsBeingBlocked)
        {
            printf("\t\tWARNING: OS is blocking TCG commands over passthrough. Please enable it before running any TCG commands\n");
        }*/
        break;
    case ENCRYPTION_FULL_DISK:
        printf("Full Disk Encryption\n");
        break;
    case ENCRYPTION_NONE:
    default:
        printf("Not Supported\n");
        break;
    }
    //number of firmware slots
    printf("\tNumber of Firmware Slots: %" PRIu8 "\n", driveInfo->controllerData.numberOfFirmwareSlots);
    //Print out Controller features! (admin commands, etc)
    printf("\tController Features:\n");
    for (uint16_t featureIter = 0; featureIter < driveInfo->controllerData.numberOfControllerFeatures; ++featureIter)
    {
        printf("\t\t%s\n", driveInfo->controllerData.controllerFeaturesSupported[featureIter]);
    }

    printf("\nNVMe Namespace Information:\n");
    if (driveInfo->namespaceData.valid)
    {
        //Namespace size
        DECLARE_ZERO_INIT_ARRAY(char, mSizeUnits, UNIT_STRING_LENGTH);
        DECLARE_ZERO_INIT_ARRAY(char, sizeUnits, UNIT_STRING_LENGTH);
        char* mSizeUnit = &mSizeUnits[0];
        char* sizeUnit = &sizeUnits[0];
        double nvmMSize = C_CAST(double, driveInfo->namespaceData.namespaceSize * driveInfo->namespaceData.formattedLBASizeBytes);
        double nvmSize = nvmMSize;
        metric_Unit_Convert(&nvmMSize, &mSizeUnit);
        capacity_Unit_Convert(&nvmSize, &sizeUnit);
        printf("\tNamespace Size (%s/%s): %0.02f/%0.02f\n", mSizeUnit, sizeUnit, nvmMSize, nvmSize);
        printf("\tNamespace Size (LBAs): %" PRIu64 "\n", driveInfo->namespaceData.namespaceSize);

        //namespace capacity
        DECLARE_ZERO_INIT_ARRAY(char, mCapUnits, UNIT_STRING_LENGTH);
        DECLARE_ZERO_INIT_ARRAY(char, capUnits, UNIT_STRING_LENGTH);
        char* mCapUnit = &mCapUnits[0];
        char* capUnit = &capUnits[0];
        double nvmMCap = C_CAST(double, driveInfo->namespaceData.namespaceCapacity * driveInfo->namespaceData.formattedLBASizeBytes);
        double nvmCap = nvmMCap;
        metric_Unit_Convert(&nvmMCap, &mCapUnit);
        capacity_Unit_Convert(&nvmCap, &capUnit);
        printf("\tNamespace Capacity (%s/%s): %0.02f/%0.02f\n", mCapUnit, capUnit, nvmMCap, nvmCap);
        printf("\tNamespace Capacity (LBAs): %" PRIu64 "\n", driveInfo->namespaceData.namespaceCapacity);

        //namespace utilization
        DECLARE_ZERO_INIT_ARRAY(char, mUtilizationUnits, UNIT_STRING_LENGTH);
        DECLARE_ZERO_INIT_ARRAY(char, utilizationUnits, UNIT_STRING_LENGTH);
        char* mUtilizationUnit = &mUtilizationUnits[0];
        char* utilizationUnit = &utilizationUnits[0];
        double nvmMUtilization = C_CAST(double, driveInfo->namespaceData.namespaceUtilization * driveInfo->namespaceData.formattedLBASizeBytes);
        double nvmUtilization = nvmMUtilization;
        metric_Unit_Convert(&nvmMUtilization, &mUtilizationUnit);
        capacity_Unit_Convert(&nvmUtilization, &utilizationUnit);
        printf("\tNamespace Utilization (%s/%s): %0.02f/%0.02f\n", mUtilizationUnit, utilizationUnit, nvmMUtilization, nvmUtilization);
        printf("\tNamespace Utilization (LBAs): %" PRIu64 "\n", driveInfo->namespaceData.namespaceUtilization);

        //Formatted LBA Size
        printf("\tLogical Block Size (B): %" PRIu32 "\n", driveInfo->namespaceData.formattedLBASizeBytes);

        //relative performance
        printf("\tLogical Block Size Relative Performance: ");
        switch (driveInfo->namespaceData.relativeFormatPerformance)
        {
        case 0:
            printf("Best Performance\n");
            break;
        case 1:
            printf("Better Performance\n");
            break;
        case 2:
            printf("Good Performance\n");
            break;
        case 3:
            printf("Degraded Performance\n");
            break;
        default://this case shouldn't ever happen...just reducing a warning - TJE
            printf("Unknown Performance\n");
            break;
        }
        if (driveInfo->namespaceData.nvmCapacityD > 0)
        {
            memset(mCapUnits, 0, UNIT_STRING_LENGTH * sizeof(char));
            memset(capUnits, 0, UNIT_STRING_LENGTH * sizeof(char));
            double mCapacity = driveInfo->namespaceData.nvmCapacityD;
            double capacity = mCapacity;
            metric_Unit_Convert(&mCapacity, &mCapUnit);
            capacity_Unit_Convert(&capacity, &capUnit);
            printf("\tNVM Capacity (%s/%s): %0.02f/%0.02f\n", mCapUnit, capUnit, mCapacity, capacity);
        }
        printf("\tNGUID: ");
        if (memcmp(zero128Bit, driveInfo->namespaceData.namespaceGloballyUniqueIdentifier, 16))
        {
            for (uint8_t i = 0; i < 16; ++i)
            {
                printf("%02" PRIX8, driveInfo->controllerData.fguid[i]);
            }
            printf("\n");
        }
        else
        {
            printf("Not Supported\n");
        }
        printf("\tEUI64: ");
        if (driveInfo->namespaceData.ieeeExtendedUniqueIdentifier != 0)
        {
            printf("%016" PRIX64 "\n", driveInfo->namespaceData.ieeeExtendedUniqueIdentifier);
        }
        else
        {
            printf("Not Supported\n");
        }
        //Namespace features.
        printf("\tNamespace Features:\n");
        for (uint16_t featureIter = 0; featureIter < driveInfo->namespaceData.numberOfNamespaceFeatures; ++featureIter)
        {
            printf("\t\t%s\n", driveInfo->namespaceData.namespaceFeaturesSupported[featureIter]);
        }
    }
    else
    {
        printf("\tERROR: Could not get namespace data!\n");
    }
    printf("\n");
}

void print_SAS_Sata_Device_Information(ptrDriveInformationSAS_SATA driveInfo)
{
    double mCapacity = 0;
    double capacity = 0;
    DECLARE_ZERO_INIT_ARRAY(char, mCapUnits, UNIT_STRING_LENGTH);
    DECLARE_ZERO_INIT_ARRAY(char, capUnits, UNIT_STRING_LENGTH);
    char* mCapUnit = &mCapUnits[0];
    char* capUnit = &capUnits[0];
    if (safe_strlen(driveInfo->vendorID))
    {
        printf("\tVendor ID: %s\n", driveInfo->vendorID);
    }
    printf("\tModel Number: %s\n", driveInfo->modelNumber);
    printf("\tSerial Number: %s\n", driveInfo->serialNumber);
    if (safe_strlen(driveInfo->pcbaSerialNumber))
    {
        printf("\tPCBA Serial Number: %s\n", driveInfo->pcbaSerialNumber);
    }
    printf("\tFirmware Revision: %s\n", driveInfo->firmwareRevision);
    if (safe_strlen(driveInfo->satVendorID))
    {
        printf("\tSAT Vendor ID: %s\n", driveInfo->satVendorID);
    }
    if (safe_strlen(driveInfo->satProductID))
    {
        printf("\tSAT Product ID: %s\n", driveInfo->satProductID);
    }
    if (safe_strlen(driveInfo->satProductRevision))
    {
        printf("\tSAT Product Rev: %s\n", driveInfo->satProductRevision);
    }
    printf("\tWorld Wide Name: ");
    if (driveInfo->worldWideNameSupported)
    {
        printf("%016" PRIX64 "", driveInfo->worldWideName);
        if (driveInfo->worldWideNameExtensionValid)
        {
            printf("%016" PRIX64 "", driveInfo->worldWideNameExtension);
        }
    }
    else
    {
        printf("Not Supported");
    }
    printf("\n");
    if (driveInfo->dateOfManufactureValid)
    {
        printf("\tDate Of Manufacture: Week %" PRIu8 ", %" PRIu16 "\n", driveInfo->manufactureWeek, driveInfo->manufactureYear);
    }
    if (driveInfo->copyrightValid && safe_strlen(driveInfo->copyrightInfo))
    {
        printf("\tCopyright: %s\n", driveInfo->copyrightInfo);
    }
    //Drive capacity
    mCapacity = C_CAST(double, driveInfo->maxLBA * driveInfo->logicalSectorSize);
    if (driveInfo->maxLBA == 0 && driveInfo->ataLegacyCHSInfo.legacyCHSValid)
    {
        if (driveInfo->ataLegacyCHSInfo.currentCapacityInSectors > 0)
        {
            mCapacity = C_CAST(double, C_CAST(uint64_t, driveInfo->ataLegacyCHSInfo.currentCapacityInSectors) * C_CAST(uint64_t, driveInfo->logicalSectorSize));
        }
        else
        {
            mCapacity = C_CAST(double, (C_CAST(uint64_t, driveInfo->ataLegacyCHSInfo.numberOfLogicalCylinders) * C_CAST(uint64_t, driveInfo->ataLegacyCHSInfo.numberOfLogicalHeads) * C_CAST(uint64_t, driveInfo->ataLegacyCHSInfo.numberOfLogicalSectorsPerTrack)) * C_CAST(uint64_t, driveInfo->logicalSectorSize));
        }
    }
    capacity = mCapacity;
    metric_Unit_Convert(&mCapacity, &mCapUnit);
    capacity_Unit_Convert(&capacity, &capUnit);
    printf("\tDrive Capacity (%s/%s): %0.02f/%0.02f\n", mCapUnit, capUnit, mCapacity, capacity);
    if (!(driveInfo->nativeMaxLBA == 0 || driveInfo->nativeMaxLBA == UINT64_MAX))
    {
        mCapacity = C_CAST(double, C_CAST(uint64_t, driveInfo->nativeMaxLBA) * C_CAST(uint64_t, driveInfo->logicalSectorSize));
        capacity = mCapacity;
        metric_Unit_Convert(&mCapacity, &mCapUnit);
        capacity_Unit_Convert(&capacity, &capUnit);
        printf("\tNative Drive Capacity (%s/%s): %0.02f/%0.02f\n", mCapUnit, capUnit, mCapacity, capacity);
    }
    printf("\tTemperature Data:\n");
    if (driveInfo->temperatureData.temperatureDataValid)
    {
        printf("\t\tCurrent Temperature (C): %" PRId16 "\n", driveInfo->temperatureData.currentTemperature);
    }
    else
    {
        printf("\t\tCurrent Temperature (C): Not Reported\n");
    }
    //Highest Temperature
    if (driveInfo->temperatureData.highestValid)
    {
        printf("\t\tHighest Temperature (C): %" PRId16 "\n", driveInfo->temperatureData.highestTemperature);
    }
    else
    {
        printf("\t\tHighest Temperature (C): Not Reported\n");
    }
    //Lowest Temperature
    if (driveInfo->temperatureData.lowestValid)
    {
        printf("\t\tLowest Temperature (C): %" PRId16 "\n", driveInfo->temperatureData.lowestTemperature);
    }
    else
    {
        printf("\t\tLowest Temperature (C): Not Reported\n");
    }
    if (driveInfo->humidityData.humidityDataValid)
    {
        //Humidity Data
        printf("\tHumidity Data:\n");
        if (driveInfo->humidityData.humidityDataValid)
        {
            if (driveInfo->humidityData.currentHumidity == UINT8_MAX)
            {
                printf("\t\tCurrent Humidity (%%): Invalid Reading\n");
            }
            else
            {
                printf("\t\tCurrent Humidity (%%): %" PRIu8 "\n", driveInfo->humidityData.currentHumidity);
            }
        }
        else
        {
            printf("\t\tCurrent Humidity (%%): Not Reported\n");
        }
        if (driveInfo->humidityData.highestValid)
        {
            if (driveInfo->humidityData.currentHumidity == UINT8_MAX)
            {
                printf("\t\tHighest Humidity (%%): Invalid Reading\n");
            }
            else
            {
                printf("\t\tHighest Humidity (%%): %" PRIu8 "\n", driveInfo->humidityData.highestHumidity);
            }
        }
        else
        {
            printf("\t\tHighest Humidity (%%): Not Reported\n");
        }
        if (driveInfo->humidityData.lowestValid)
        {
            if (driveInfo->humidityData.currentHumidity == UINT8_MAX)
            {
                printf("\t\tLowest Humidity (%%): Invalid Reading\n");
            }
            else
            {
                printf("\t\tLowest Humidity (%%): %" PRIu8 "\n", driveInfo->humidityData.lowestHumidity);
            }
        }
        else
        {
            printf("\t\tLowest Humidity (%%): Not Reported\n");
        }
    }
    //Power On Time
    printf("\tPower On Time: ");
    if (driveInfo->powerOnMinutesValid)
    {
        uint16_t days = 0;
        uint8_t years = 0;
        uint8_t hours = 0;
        uint8_t minutes = 0;
        uint8_t seconds = 0;
        convert_Seconds_To_Displayable_Time(driveInfo->powerOnMinutes * UINT64_C(60), &years, &days, &hours, &minutes, &seconds);
        print_Time_To_Screen(&years, &days, &hours, &minutes, &seconds);
    }
    else
    {
        printf("Not Reported");
    }
    printf("\n");
    printf("\tPower On Hours: ");
    if (driveInfo->powerOnMinutesValid)
    {
        //convert to a double to display as xx.xx
        double powerOnHours = C_CAST(double, driveInfo->powerOnMinutes) / 60.00;
        printf("%0.02f", powerOnHours);
    }
    else
    {
        printf("Not Reported");
    }
    printf("\n");
    if (driveInfo->ataLegacyCHSInfo.legacyCHSValid && driveInfo->maxLBA == 0)
    {
        printf("\tDefault CHS: %" PRIu16 " | %" PRIu8 " | %" PRIu8 "\n", driveInfo->ataLegacyCHSInfo.numberOfLogicalCylinders, driveInfo->ataLegacyCHSInfo.numberOfLogicalHeads, driveInfo->ataLegacyCHSInfo.numberOfLogicalSectorsPerTrack);
        printf("\tCurrent CHS: %" PRIu16 " | %" PRIu8 " | %" PRIu8 "\n", driveInfo->ataLegacyCHSInfo.numberOfCurrentLogicalCylinders, driveInfo->ataLegacyCHSInfo.numberOfCurrentLogicalHeads, driveInfo->ataLegacyCHSInfo.numberOfCurrentLogicalSectorsPerTrack);
        uint32_t simMaxLBA = 0;
        if (driveInfo->ataLegacyCHSInfo.currentInfoconfigurationValid && driveInfo->ataLegacyCHSInfo.numberOfCurrentLogicalCylinders > 0 && driveInfo->ataLegacyCHSInfo.numberOfCurrentLogicalHeads > 0 && driveInfo->ataLegacyCHSInfo.numberOfCurrentLogicalSectorsPerTrack > 0)
        {
            simMaxLBA = C_CAST(uint32_t, driveInfo->ataLegacyCHSInfo.numberOfCurrentLogicalCylinders) * C_CAST(uint32_t, driveInfo->ataLegacyCHSInfo.numberOfCurrentLogicalHeads) * C_CAST(uint32_t, driveInfo->ataLegacyCHSInfo.numberOfCurrentLogicalSectorsPerTrack);
        }
        else
        {
            simMaxLBA = C_CAST(uint32_t, driveInfo->ataLegacyCHSInfo.numberOfLogicalCylinders) * C_CAST(uint32_t, driveInfo->ataLegacyCHSInfo.numberOfLogicalHeads) * C_CAST(uint32_t, driveInfo->ataLegacyCHSInfo.numberOfLogicalSectorsPerTrack);
        }
        printf("\tSimulated MaxLBA: %" PRIu32 "\n", simMaxLBA);
    }
    else
    {
        //MaxLBA
        printf("\tMaxLBA: %" PRIu64 "\n", driveInfo->maxLBA);
        //Native Max LBA
        printf("\tNative MaxLBA: ");
        if (driveInfo->nativeMaxLBA == 0 || driveInfo->nativeMaxLBA == UINT64_MAX)
        {
            printf("Not Reported\n");
        }
        else
        {
            printf("%" PRIu64 "\n", driveInfo->nativeMaxLBA);
        }
    }
    if (driveInfo->isFormatCorrupt)
    {
        //Logical Sector Size
        printf("\tLogical Sector Size (B): Format Corrupt\n");
        //Physical Sector Size
        printf("\tPhysical Sector Size (B): Format Corrupt\n");
        //Sector Alignment
        printf("\tSector Alignment: Format Corrupt\n");
    }
    else
    {
        //Logical Sector Size
        printf("\tLogical Sector Size (B): %" PRIu32 "\n", driveInfo->logicalSectorSize);
        //Physical Sector Size
        printf("\tPhysical Sector Size (B): %" PRIu32 "\n", driveInfo->physicalSectorSize);
        //Sector Alignment
        printf("\tSector Alignment: %" PRIu16 "\n", driveInfo->sectorAlignment);
    }
    //Rotation Rate
    printf("\tRotation Rate (RPM): ");
    if (driveInfo->rotationRate == 0)
    {
        printf("Not Reported\n");
    }
    else if (driveInfo->rotationRate == 0x0001)
    {
        printf("SSD\n");
    }
    else
    {
        printf("%" PRIu16 "\n", driveInfo->rotationRate);
    }
    if (driveInfo->isWriteProtected)
    {
        printf("\tMedium is write protected!\n");
    }
    //Form Factor
    printf("\tForm Factor: ");
    switch (driveInfo->formFactor)
    {
    case 1:
        printf("5.25\"\n");
        break;
    case 2:
        printf("3.5\"\n");
        break;
    case 3:
        printf("2.5\"\n");
        break;
    case 4:
        printf("1.8\"\n");
        break;
    case 5:
        printf("Less than 1.8\"\n");
        break;
    case 6:
        printf("mSATA\n");
        break;
    case 7:
        printf("M.2\n");
        break;
    case 8:
        printf("MicroSSD\n");
        break;
    case 9:
        printf("CFast\n");
        break;
    case 0:
    default:
        printf("Not Reported\n");
        break;
    }
    //Last DST information
    printf("\tLast DST information:\n");
    if (driveInfo->dstInfo.informationValid && driveInfo->powerOnMinutesValid)
    {
        if (driveInfo->powerOnMinutes - (driveInfo->dstInfo.powerOnHours * 60) != driveInfo->powerOnMinutes)
        {
            double timeSinceLastDST = (C_CAST(double, driveInfo->powerOnMinutes) / 60.0) - C_CAST(double, driveInfo->dstInfo.powerOnHours);
            printf("\t\tTime since last DST (hours): ");
            if (timeSinceLastDST >= 0)
            {
                printf("%0.02f\n", timeSinceLastDST);
            }
            else
            {
                printf("Indeterminate\n");
            }
            printf("\t\tDST Status/Result: 0x%" PRIX8 "\n", driveInfo->dstInfo.resultOrStatus);
            printf("\t\tDST Test run: 0x%" PRIX8 "\n", driveInfo->dstInfo.testNumber);
            if (driveInfo->dstInfo.resultOrStatus != 0 && driveInfo->dstInfo.resultOrStatus != 0xF && driveInfo->dstInfo.errorLBA != UINT64_MAX)
            {
                //Show the Error LBA
                printf("\t\tError occurred at LBA: %" PRIu64 "\n", driveInfo->dstInfo.errorLBA);
            }
        }
        else
        {
            printf("\t\tDST has never been run\n");
        }
    }
    else
    {
        printf("\t\tNot supported\n");
    }
    //Long DST time
    printf("\tLong Drive Self Test Time: ");
    if (driveInfo->longDSTTimeMinutes > 0)
    {
        //print as hours:minutes
        uint16_t days = 0;
        uint8_t years = 0;
        uint8_t hours = 0;
        uint8_t minutes = 0;
        uint8_t seconds = 0;
        convert_Seconds_To_Displayable_Time(driveInfo->longDSTTimeMinutes * 60, &years, &days, &hours, &minutes, &seconds);
        print_Time_To_Screen(&years, &days, &hours, &minutes, &seconds);
    }
    else
    {
        printf("Not Supported");
    }
    printf("\n");
    //Interface Speed
    printf("\tInterface speed:\n");
    if (driveInfo->interfaceSpeedInfo.speedIsValid)
    {
        if (driveInfo->interfaceSpeedInfo.speedType == INTERFACE_SPEED_SERIAL)
        {
            if (driveInfo->interfaceSpeedInfo.serialSpeed.numberOfPorts > 0)
            {
                if (driveInfo->interfaceSpeedInfo.serialSpeed.numberOfPorts == 1)
                {
                    printf("\t\tMax Speed (Gb/s): ");
                    switch (driveInfo->interfaceSpeedInfo.serialSpeed.portSpeedsMax[0])
                    {
                    case 5:
                        printf("22.5");
                        break;
                    case 4:
                        printf("12.0");
                        break;
                    case 3:
                        printf("6.0");
                        break;
                    case 2:
                        printf("3.0");
                        break;
                    case 1:
                        printf("1.5");
                        break;
                    case 0:
                        printf("Not Reported");
                        break;
                    default:
                        printf("Unknown");
                        break;
                    }
                    printf("\n");
                    printf("\t\tNegotiated Speed (Gb/s): ");
                    switch (driveInfo->interfaceSpeedInfo.serialSpeed.portSpeedsNegotiated[0])
                    {
                    case 5:
                        printf("22.5");
                        break;
                    case 4:
                        printf("12.0");
                        break;
                    case 3:
                        printf("6.0");
                        break;
                    case 2:
                        printf("3.0");
                        break;
                    case 1:
                        printf("1.5");
                        break;
                    case 0:
                        printf("Not Reported");
                        break;
                    default:
                        printf("Unknown");
                        break;
                    }
                    printf("\n");
                }
                else
                {
                    for (uint8_t portIter = 0; portIter < driveInfo->interfaceSpeedInfo.serialSpeed.numberOfPorts && portIter < MAX_PORTS; portIter++)
                    {
                        printf("\t\tPort %" PRIu8 "", portIter);
                        if (driveInfo->interfaceSpeedInfo.serialSpeed.activePortNumber == portIter && driveInfo->interfaceSpeedInfo.serialSpeed.activePortNumber != UINT8_MAX)
                        {
                            printf(" (Current Port)");
                        }
                        printf("\n");
                        //Max Speed
                        printf("\t\t\tMax Speed (GB/s): ");
                        switch (driveInfo->interfaceSpeedInfo.serialSpeed.portSpeedsMax[portIter])
                        {
                        case 5:
                            printf("22.5");
                            break;
                        case 4:
                            printf("12.0");
                            break;
                        case 3:
                            printf("6.0");
                            break;
                        case 2:
                            printf("3.0");
                            break;
                        case 1:
                            printf("1.5");
                            break;
                        case 0:
                            printf("Not Reported");
                            break;
                        default:
                            printf("Unknown");
                            break;
                        }
                        printf("\n");
                        //Negotiated speed
                        printf("\t\t\tNegotiated Speed (Gb/s): ");
                        switch (driveInfo->interfaceSpeedInfo.serialSpeed.portSpeedsNegotiated[portIter])
                        {
                        case 5:
                            printf("22.5");
                            break;
                        case 4:
                            printf("12.0");
                            break;
                        case 3:
                            printf("6.0");
                            break;
                        case 2:
                            printf("3.0");
                            break;
                        case 1:
                            printf("1.5");
                            break;
                        case 0:
                            printf("Not Reported");
                            break;
                        default:
                            printf("Unknown");
                            break;
                        }
                        printf("\n");
                    }
                }
            }
            else
            {
                printf("\t\tNot Reported\n");
            }
        }
        else if (driveInfo->interfaceSpeedInfo.speedType == INTERFACE_SPEED_PARALLEL)
        {
            printf("\t\tMax Speed (MB/s): %0.02f", driveInfo->interfaceSpeedInfo.parallelSpeed.maxSpeed);
            if (driveInfo->interfaceSpeedInfo.parallelSpeed.maxModeNameValid)
            {
                printf(" (%s)", driveInfo->interfaceSpeedInfo.parallelSpeed.maxModeName);
            }
            printf("\n");
            printf("\t\tNegotiated Speed (MB/s): ");
            if (driveInfo->interfaceSpeedInfo.parallelSpeed.negotiatedValid)
            {
                printf("%0.02f", driveInfo->interfaceSpeedInfo.parallelSpeed.negotiatedSpeed);
                if (driveInfo->interfaceSpeedInfo.parallelSpeed.negModeNameValid)
                {
                    printf(" (%s)", driveInfo->interfaceSpeedInfo.parallelSpeed.negModeName);
                }
                printf("\n");
            }
            else
            {
                printf("Not Reported\n");
            }
            if (driveInfo->interfaceSpeedInfo.parallelSpeed.cableInfoType == CABLING_INFO_ATA && driveInfo->interfaceSpeedInfo.parallelSpeed.ataCableInfo.cablingInfoValid)
            {
                printf("\t\tCabling Detected: ");
                if (driveInfo->interfaceSpeedInfo.parallelSpeed.ataCableInfo.ata80PinCableDetected)
                {
                    printf("80-pin Cable\n");
                }
                else
                {
                    printf("40-pin Cable\n");
                }
                printf("\t\tDevice Number: %" PRIu8 "\n", driveInfo->interfaceSpeedInfo.parallelSpeed.ataCableInfo.device1 ? UINT8_C(1) : UINT8_C(0));
                printf("\t\tDevice Set by: ");
                switch (driveInfo->interfaceSpeedInfo.parallelSpeed.ataCableInfo.deviceNumberDetermined)
                {
                case 1:
                    printf("Jumper\n");
                    break;
                case 2:
                    printf("Cable Select\n");
                    break;
                default:
                    printf("Unknown\n");
                    break;
                }
            }
        }
        else if (driveInfo->interfaceSpeedInfo.speedType == INTERFACE_SPEED_ANCIENT)
        {
            if (driveInfo->interfaceSpeedInfo.ancientHistorySpeed.dataTransferGt10MbS)
            {
                printf("\t\t>10Mb/s\n");
            }
            else if (driveInfo->interfaceSpeedInfo.ancientHistorySpeed.dataTransferGt5MbSLte10MbS)
            {
                printf("\t\t>5Mb/s & <10Mb/s\n");
            }
            else if (driveInfo->interfaceSpeedInfo.ancientHistorySpeed.dataTransferLte5MbS)
            {
                printf("\t\t<5Mb/s\n");
            }
            else
            {
                printf("\t\tNot Reported\n");
            }
        }
        else
        {
            printf("\t\tNot Reported\n");
        }
    }
    else
    {
        printf("\t\tNot Reported\n");
    }
    //Workload Rate (Annualized)
    printf("\tAnnualized Workload Rate (TB/yr): ");
    if (driveInfo->totalBytesRead > 0 || driveInfo->totalBytesWritten > 0)
    {
        if (driveInfo->powerOnMinutesValid)
        {
#ifndef MINUTES_IN_1_YEAR
#define MINUTES_IN_1_YEAR 525600.0
#endif // !MINUTES_IN_1_YEAR
            double totalTerabytesRead = C_CAST(double, driveInfo->totalBytesRead) / 1000000000000.0;
            double totalTerabytesWritten = C_CAST(double, driveInfo->totalBytesWritten) / 1000000000000.0;
            double calculatedUsage = C_CAST(double, totalTerabytesRead + totalTerabytesWritten) * C_CAST(double, MINUTES_IN_1_YEAR / C_CAST(double, driveInfo->powerOnMinutes));
            printf("%0.02f\n", calculatedUsage);
        }
        else
        {
            printf("0.00\n");
        }
    }
    else
    {
        printf("Not Reported\n");
    }
    //Total Bytes Read
    printf("\tTotal Bytes Read ");
    if (driveInfo->totalBytesRead > 0)
    {
        double totalBytesRead = C_CAST(double, driveInfo->totalBytesRead);
        char unitString[4] = { '\0' };
        char* unit = &unitString[0];
        metric_Unit_Convert(&totalBytesRead, &unit);
        printf("(%s): %0.02f\n", unit, totalBytesRead);
    }
    else
    {
        printf("(B): Not Reported\n");
    }
    //Total Bytes Written
    printf("\tTotal Bytes Written ");
    if (driveInfo->totalBytesWritten > 0)
    {
        double totalBytesWritten = C_CAST(double, driveInfo->totalBytesWritten);
        char unitString[4] = { '\0' };
        char* unit = &unitString[0];
        metric_Unit_Convert(&totalBytesWritten, &unit);
        printf("(%s): %0.02f\n", unit, totalBytesWritten);
    }
    else
    {
        printf("(B): Not Reported\n");
    }
    //Drive reported Utilization
    if (driveInfo->deviceReportedUtilizationRate > 0)
    {
        printf("\tDrive Reported Utilization (%%): ");
        printf("%0.04f", driveInfo->deviceReportedUtilizationRate);
    }
    //Encryption Support
    printf("\tEncryption Support: ");
    switch (driveInfo->encryptionSupport)
    {
    case ENCRYPTION_SELF_ENCRYPTING:
        printf("Self Encrypting\n");
        break;
    case ENCRYPTION_FULL_DISK:
        printf("Full Disk Encryption\n");
        break;
    case ENCRYPTION_NONE:
    default:
        printf("Not Supported\n");
        break;
    }
    if (driveInfo->trustedCommandsBeingBlocked)
    {
        printf("\t\tWARNING: OS/driver/HBA is blocking TCG commands over passthrough. Please enable it before running any TCG commands\n");
    }
    //Cache Size -- convert to MB
    if (driveInfo->cacheSize > 0)
    {
        double cacheSize = C_CAST(double, driveInfo->cacheSize);
        DECLARE_ZERO_INIT_ARRAY(char, cacheUnit, UNIT_STRING_LENGTH);
        char* cachUnitPtr = &cacheUnit[0];
        capacity_Unit_Convert(&cacheSize, &cachUnitPtr);
        printf("\tCache Size (%s): %0.02f\n", cacheUnit, cacheSize);
    }
    else
    {
        printf("\tCache Size (MiB): Not Reported\n");
    }
    //Hybrid NAND Cache Size -- convert to GB
    if (driveInfo->hybridNANDSize > 0)
    {
        double cacheSize = C_CAST(double, driveInfo->hybridNANDSize);
        DECLARE_ZERO_INIT_ARRAY(char, cacheUnit, UNIT_STRING_LENGTH);
        char* cachUnitPtr = &cacheUnit[0];
        capacity_Unit_Convert(&cacheSize, &cachUnitPtr);
        printf("\tHybrid NAND Cache Size (%s): %0.02f\n", cacheUnit, cacheSize);
    }
    //Percent Endurance Used
    if (driveInfo->rotationRate == 0x0001)
    {
        if (driveInfo->percentEnduranceUsed >= 0)
        {
            printf("\tPercentage Used Endurance Indicator (%%): %0.05f\n", driveInfo->percentEnduranceUsed);
        }
        else
        {
            printf("\tPercentage Used Endurance Indicator (%%): Not Reported\n");
        }
    }
    //Write Amplification
    if (driveInfo->rotationRate == 0x0001 && driveInfo->totalWritesToFlash > 0)
    {
        if (driveInfo->totalLBAsWritten > 0)
        {
            printf("\tWrite Amplification (%%): %0.02f\n", C_CAST(double, driveInfo->totalWritesToFlash) / C_CAST(double, driveInfo->totalLBAsWritten));
        }
        else
        {
            printf("\tWrite Amplification (%%): 0\n");
        }
    }
    //Read look ahead
    if (driveInfo->readLookAheadSupported)
    {
        if (driveInfo->readLookAheadEnabled)
        {
            printf("\tRead Look-Ahead: Enabled\n");
        }
        else
        {
            printf("\tRead Look-Ahead: Disabled\n");
        }
    }
    else
    {
        printf("\tRead Look-Ahead: Not Supported\n");
    }
    //NVCache (!NV_DIS bit from caching MP)
    if (driveInfo->nvCacheSupported)
    {
        printf("\tNon-Volatile Cache: ");
        if (driveInfo->nvCacheEnabled)
        {
            printf("Enabled\n");
        }
        else
        {
            printf("Disabled\n");
        }
    }
    //Write Cache
    if (driveInfo->writeCacheSupported)
    {
        if (driveInfo->writeCacheEnabled)
        {
            printf("\tWrite Cache: Enabled\n");
        }
        else
        {
            printf("\tWrite Cache: Disabled\n");
        }
    }
    else
    {
        printf("\tWrite Cache: Not Supported\n");
    }
    if (driveInfo->lowCurrentSpinupValid)
    {
        if (driveInfo->lowCurrentSpinupViaSCT)//to handle differences in reporting between 2.5" products and others
        {
            printf("\tLow Current Spinup: ");
            switch (driveInfo->lowCurrentSpinupEnabled)
            {
            case SEAGATE_LOW_CURRENT_SPINUP_STATE_LOW:
                printf("Enabled\n");
                break;
            case SEAGATE_LOW_CURRENT_SPINUP_STATE_DEFAULT:
                printf("Disabled\n");
                break;
            case SEAGATE_LOW_CURRENT_SPINUP_STATE_ULTRA_LOW:
                printf("Ultra Low Enabled\n");
                break;
            default:
                printf("Unknown/Invalid state: %" PRIX16 "\n", C_CAST(uint16_t, driveInfo->lowCurrentSpinupEnabled));
                break;
            }
        }
        else
        {
            if (driveInfo->lowCurrentSpinupEnabled > 0)
            {
                printf("\tLow Current Spinup: Enabled\n");
            }
            else
            {
                printf("\tLow Current Spinup: Disabled\n");
            }
        }
    }
    //SMART Status
    printf("\tSMART Status: ");
    switch (driveInfo->smartStatus)
    {
    case 0://good
        printf("Good\n");
        break;
    case 1://bad
        printf("Tripped\n");
        break;
    default://unknown
        printf("Unknown or Not Supported\n");
        break;
    }
    //ATA Security Infomation
    printf("\tATA Security Information: ");
    if (driveInfo->ataSecurityInformation.securitySupported)
    {
        printf("Supported");
        if (driveInfo->ataSecurityInformation.securityEnabled)
        {
            printf(", Enabled");
        }
        if (driveInfo->ataSecurityInformation.securityLocked)
        {
            printf(", Locked");
        }
        if (driveInfo->ataSecurityInformation.securityFrozen)
        {
            printf(", Frozen");
        }
        if (driveInfo->ataSecurityInformation.securityCountExpired)
        {
            printf(", Password Count Expired");
        }
        printf("\n");
    }
    else
    {
        printf("Not Supported\n");
    }
    //Zoned Device Type
    if (driveInfo->zonedDevice > 0)
    {
        printf("\tZoned Device Type: ");
        switch (driveInfo->zonedDevice)
        {
        case 0x1://host aware
            printf("Host Aware\n");
            break;
        case 0x2://host managed
            printf("Device Managed\n");
            break;
        case 0x3://reserved
            printf("Reserved\n");
            break;
        default:
            printf("Not a Zoned Device\n");
            break;
        }
    }
    printf("\tFirmware Download Support: ");
    if (driveInfo->fwdlSupport.downloadSupported)
    {
        printf("Full");//changed to "Full" from "Immediate" since this makes more sense...-TJE
        if (driveInfo->fwdlSupport.segmentedSupported)
        {
            printf(", Segmented");
            if (driveInfo->fwdlSupport.seagateDeferredPowerCycleRequired)
            {
                printf(" as Deferred - Power Cycle Activation Only");
            }
        }
        if (driveInfo->fwdlSupport.deferredSupported)
        {
            printf(", Deferred");
        }
        if (driveInfo->fwdlSupport.dmaModeSupported)
        {
            printf(", DMA");
        }
    }
    else
    {
        printf("Not Supported");
    }
    printf("\n");
    if (driveInfo->lunCount > 0)
    {
        printf("\tNumber of Logical Units: %" PRIu8 "\n", driveInfo->lunCount);
    }
    if (driveInfo->concurrentPositioningRanges > 0)
    {
        printf("\tNumber of Concurrent Ranges: %" PRIu8 "\n", driveInfo->concurrentPositioningRanges);
    }
    //Specifications Supported
    printf("\tSpecifications Supported:\n");
    if (driveInfo->numberOfSpecificationsSupported > 0)
    {
        uint8_t specificationsIter = 0;
        for (specificationsIter = 0; specificationsIter < driveInfo->numberOfSpecificationsSupported && specificationsIter < MAX_SPECS; specificationsIter++)
        {
            printf("\t\t%s\n", driveInfo->specificationsSupported[specificationsIter]);
        }
    }
    else
    {
        printf("\t\tNone reported by device.\n");
    }
    //Features Supported
    printf("\tFeatures Supported:\n");
    if (driveInfo->numberOfFeaturesSupported > 0)
    {
        uint8_t featuresIter = 0;
        for (featuresIter = 0; featuresIter < driveInfo->numberOfFeaturesSupported && featuresIter < MAX_FEATURES; featuresIter++)
        {
            printf("\t\t%s\n", driveInfo->featuresSupported[featuresIter]);
        }
    }
    else
    {
        printf("\t\tNone reported or an error occurred while trying to determine\n\t\tthe features.\n");
    }
    //Adapter information
    printf("\tAdapter Information:\n");
    printf("\t\tAdapter Type: ");
    switch (driveInfo->adapterInformation.infoType)
    {
    case ADAPTER_INFO_USB:
        printf("USB\n");
        break;
    case ADAPTER_INFO_PCI:
        printf("PCI\n");
        break;
    case ADAPTER_INFO_IEEE1394:
        printf("IEEE1394\n");
        break;
    default:
        printf("Unknown\n");
        break;
    }
    printf("\t\tVendor ID: ");
    if (driveInfo->adapterInformation.vendorIDValid)
    {
        printf("%04" PRIX32 "h\n", driveInfo->adapterInformation.vendorID);
    }
    else
    {
        printf("Not available.\n");
    }
    printf("\t\tProduct ID: ");
    if (driveInfo->adapterInformation.productIDValid)
    {
        printf("%04" PRIX32 "h\n", driveInfo->adapterInformation.productID);
    }
    else
    {
        printf("Not available.\n");
    }
    printf("\t\tRevision: ");
    if (driveInfo->adapterInformation.revisionValid)
    {
        printf("%04" PRIX32 "h\n", driveInfo->adapterInformation.revision);
    }
    else
    {
        printf("Not available.\n");
    }
    if (driveInfo->adapterInformation.specifierIDValid)//IEEE1394 only, so it will only print when we get this set to true for now - TJE
    {
        printf("\t\tSpecifier ID: ");
        printf("%04" PRIX32 "h\n", driveInfo->adapterInformation.specifierID);
    }
    if (driveInfo->lunCount > 1)
    {
        printf("This device has multiple actuators. Some commands/features may affect more than one actuator.\n");
    }
    return;
}

//This exists so we can print out SCSI reported and ATA reported information for comparison purposes. (SAT test/check) NOT FOR USE WITH A SAS DRIVE
void print_Parent_And_Child_Information(ptrDriveInformation translatorDriveInfo, ptrDriveInformation driveInfo)
{
    if (translatorDriveInfo && translatorDriveInfo->infoType == DRIVE_INFO_SAS_SATA)
    {
        printf("SCSI Translator Reported Information:\n");
        print_Device_Information(translatorDriveInfo);
    }
    else
    {
        printf("SCSI Translator Information Not Available.\n\n");
    }
    if (driveInfo && driveInfo->infoType == DRIVE_INFO_SAS_SATA)
    {
        printf("ATA Reported Information:\n");
        print_Device_Information(driveInfo);
    }
    else if (driveInfo && driveInfo->infoType == DRIVE_INFO_NVME)
    {
        printf("NVMe Reported Information:\n");
        print_Device_Information(driveInfo);
    }
    else if (driveInfo)
    {
        printf("Unknown device Information type:\n");
        print_Device_Information(driveInfo);
    }
    else
    {
        printf("Drive Information not available.\n\n");
    }
}

//This function ONLY exists because we need to show a mix of SCSI and ATA information on USB.
void generate_External_Drive_Information(ptrDriveInformationSAS_SATA externalDriveInfo, ptrDriveInformationSAS_SATA scsiDriveInfo, ptrDriveInformationSAS_SATA ataDriveInfo)
{
    if (externalDriveInfo && scsiDriveInfo && ataDriveInfo)
    {
        //take data from each of the inputs, and plug it into a new one, then call the standard print function
        memcpy(externalDriveInfo, ataDriveInfo, sizeof(driveInformationSAS_SATA));
        //we have a copy of the ata info, now just change the stuff we want to show from scsi info
        memset(externalDriveInfo->vendorID, 0, 8);
        memcpy(externalDriveInfo->vendorID, scsiDriveInfo->vendorID, 8);
        memset(externalDriveInfo->modelNumber, 0, MODEL_NUM_LEN);
        memcpy(externalDriveInfo->modelNumber, scsiDriveInfo->modelNumber, safe_strlen(scsiDriveInfo->modelNumber));
        memset(externalDriveInfo->serialNumber, 0, SERIAL_NUM_LEN);
        memcpy(externalDriveInfo->serialNumber, scsiDriveInfo->serialNumber, safe_strlen(scsiDriveInfo->serialNumber));
        memset(externalDriveInfo->firmwareRevision, 0, FW_REV_LEN);
        memcpy(externalDriveInfo->firmwareRevision, scsiDriveInfo->firmwareRevision, safe_strlen(scsiDriveInfo->firmwareRevision));
        externalDriveInfo->maxLBA = scsiDriveInfo->maxLBA;
        externalDriveInfo->nativeMaxLBA = scsiDriveInfo->nativeMaxLBA;
        externalDriveInfo->logicalSectorSize = scsiDriveInfo->logicalSectorSize;
        externalDriveInfo->physicalSectorSize = scsiDriveInfo->physicalSectorSize;
        externalDriveInfo->sectorAlignment = scsiDriveInfo->sectorAlignment;
        externalDriveInfo->zonedDevice = scsiDriveInfo->zonedDevice;

        //Generally we rely on the ATA reported information to be correct
        //But this is being done for some newer products that are...strange...so we need to do 
        //some additional checks to figure out whether the SCSI drive info is telling us something that
        //is not reported in the ATA drive info.
        if (externalDriveInfo->rotationRate == 0 && scsiDriveInfo->rotationRate > 0)
        {
            externalDriveInfo->rotationRate = scsiDriveInfo->rotationRate;
        }
        if (externalDriveInfo->formFactor == 0 && scsiDriveInfo->formFactor > 0)
        {
            externalDriveInfo->formFactor = scsiDriveInfo->formFactor;
        }
        if (!externalDriveInfo->worldWideNameSupported && scsiDriveInfo->worldWideNameSupported)
        {
            externalDriveInfo->worldWideNameSupported = scsiDriveInfo->worldWideNameSupported;
            externalDriveInfo->worldWideName = scsiDriveInfo->worldWideName;
            externalDriveInfo->worldWideNameExtensionValid = scsiDriveInfo->worldWideNameExtensionValid;
            externalDriveInfo->worldWideNameExtension = scsiDriveInfo->worldWideNameExtension;
        }
        //copy specifications supported into the external drive info.
        uint16_t extSpecNumber = externalDriveInfo->numberOfSpecificationsSupported;
        uint16_t scsiSpecNumber = 0;
        for (; extSpecNumber < MAX_SPECS && scsiSpecNumber < scsiDriveInfo->numberOfSpecificationsSupported; ++extSpecNumber, ++scsiSpecNumber)
        {
            memcpy(&externalDriveInfo->specificationsSupported[extSpecNumber], &scsiDriveInfo->specificationsSupported[scsiSpecNumber], MAX_SPEC_LENGTH);
            ++(externalDriveInfo->numberOfSpecificationsSupported);
        }

    }
    return;
}

void generate_External_NVMe_Drive_Information(ptrDriveInformationSAS_SATA externalDriveInfo, ptrDriveInformationSAS_SATA scsiDriveInfo, ptrDriveInformationNVMe nvmeDriveInfo)
{
    //for the most part, keep all the SCSI information.
    //After that take the POH, temperature, DST information, workload, and combine the features.
    //Also add the NVMe spec version to the output as well.
    if (externalDriveInfo && scsiDriveInfo && nvmeDriveInfo)
    {
        //take data from each of the inputs, and plug it into a new one, then call the standard print function
        memcpy(externalDriveInfo, scsiDriveInfo, sizeof(driveInformationSAS_SATA));
        //Keep the SCSI information, minus a few things to copy from NVMe if the NVMe info needed was read properly
        if (nvmeDriveInfo->smartData.valid)
        {
            //Power on hours
            externalDriveInfo->powerOnMinutes = C_CAST(uint64_t, nvmeDriveInfo->smartData.powerOnHoursD * 60);
            externalDriveInfo->powerOnMinutesValid = true;
            //Temperature (SCSI is in Celsius!)
            externalDriveInfo->temperatureData.currentTemperature = C_CAST(int16_t, nvmeDriveInfo->smartData.compositeTemperatureKelvin) - INT16_C(273);
            externalDriveInfo->temperatureData.temperatureDataValid = true;
            //Workload (reads, writes)
            externalDriveInfo->totalBytesRead = C_CAST(uint64_t, nvmeDriveInfo->smartData.dataUnitsReadD * 512 * 1000);//this is a count of 512B units, so converting to bytes
            externalDriveInfo->totalLBAsRead = C_CAST(uint64_t, nvmeDriveInfo->smartData.dataUnitsReadD * 512 * 1000 / nvmeDriveInfo->namespaceData.formattedLBASizeBytes);
            externalDriveInfo->totalBytesWritten = C_CAST(uint64_t, nvmeDriveInfo->smartData.dataUnitsWrittenD * 512 * 1000); //this is a count of 512B units, so converting to bytes
            externalDriveInfo->totalLBAsWritten = C_CAST(uint64_t, nvmeDriveInfo->smartData.dataUnitsWrittenD * 512 * 1000 / nvmeDriveInfo->namespaceData.formattedLBASizeBytes);
            externalDriveInfo->percentEnduranceUsed = nvmeDriveInfo->smartData.percentageUsed;
            externalDriveInfo->smartStatus = nvmeDriveInfo->smartData.smartStatus;
        }

        memcpy(&externalDriveInfo->dstInfo, &nvmeDriveInfo->dstInfo, sizeof(lastDSTInformation));
        externalDriveInfo->longDSTTimeMinutes = nvmeDriveInfo->controllerData.longDSTTimeMinutes;

        if (!externalDriveInfo->writeCacheSupported)
        {
            externalDriveInfo->writeCacheSupported = nvmeDriveInfo->controllerData.volatileWriteCacheSupported;
            externalDriveInfo->writeCacheEnabled = nvmeDriveInfo->controllerData.volatileWriteCacheEnabled;
        }

        //copy specifications supported into the external drive info.
        uint16_t extSpecNumber = externalDriveInfo->numberOfSpecificationsSupported;
        if (nvmeDriveInfo->controllerData.majorVersion > 0 || nvmeDriveInfo->controllerData.minorVersion > 0 || nvmeDriveInfo->controllerData.tertiaryVersion > 0)
        {
            snprintf(externalDriveInfo->specificationsSupported[extSpecNumber], MAX_SPEC_LENGTH, "NVMe %" PRIu16 ".%" PRIu8 ".%" PRIu8 "\n", nvmeDriveInfo->controllerData.majorVersion, nvmeDriveInfo->controllerData.minorVersion, nvmeDriveInfo->controllerData.tertiaryVersion);
        }
        else
        {
            snprintf(externalDriveInfo->specificationsSupported[extSpecNumber], MAX_SPEC_LENGTH, "NVMe 1.1 or older\n");
        }
        ++(externalDriveInfo->numberOfSpecificationsSupported);

        //now copy over features such as sanitize and dst
        //copy specifications supported into the external drive info.
        uint16_t extFeatNumber = externalDriveInfo->numberOfFeaturesSupported;
        uint16_t nvmeFeatNumber = 0;
        //controller features
        for (; extFeatNumber < MAX_FEATURES && nvmeFeatNumber < nvmeDriveInfo->controllerData.numberOfControllerFeatures; ++extFeatNumber, ++nvmeFeatNumber)
        {
            memcpy(&externalDriveInfo->featuresSupported[extFeatNumber], &nvmeDriveInfo->controllerData.controllerFeaturesSupported[nvmeFeatNumber], MAX_FEATURE_LENGTH);
            if (strcmp(nvmeDriveInfo->controllerData.controllerFeaturesSupported[nvmeFeatNumber], "Firmware Update") == 0)
            {
                //this is not the best way to handle this, but will keep capabilities listed more consistent with NVMe
                externalDriveInfo->fwdlSupport.downloadSupported = true;
                externalDriveInfo->fwdlSupport.deferredSupported = true;
            }
            ++(externalDriveInfo->numberOfFeaturesSupported);
        }
        if (nvmeDriveInfo->namespaceData.valid)
        {
            //namespace features
            nvmeFeatNumber = 0;
            for (; extFeatNumber < MAX_FEATURES && nvmeFeatNumber < nvmeDriveInfo->namespaceData.numberOfNamespaceFeatures; ++extFeatNumber, ++nvmeFeatNumber)
            {
                memcpy(&externalDriveInfo->featuresSupported[extFeatNumber], &nvmeDriveInfo->namespaceData.namespaceFeaturesSupported[nvmeFeatNumber], MAX_FEATURE_LENGTH);
                ++(externalDriveInfo->numberOfFeaturesSupported);
            }
        }
    }
    return;
}


eReturnValues print_Drive_Information(tDevice* device, bool showChildInformation)
{
    eReturnValues ret = SUCCESS;
    ptrDriveInformation ataDriveInfo = M_NULLPTR;
    ptrDriveInformation scsiDriveInfo = M_NULLPTR;
    ptrDriveInformation usbDriveInfo = M_NULLPTR;
    ptrDriveInformation nvmeDriveInfo = M_NULLPTR;
#if defined (DEBUG_DRIVE_INFO_TIME)
    seatimer_t ataTime, scsiTime, nvmeTime;
    memset(&ataTime, 0, sizeof(seatimer_t));
    memset(&scsiTime, 0, sizeof(seatimer_t));
    memset(&nvmeTime, 0, sizeof(seatimer_t));
#endif //DEBUG_DRIVE_INFO_TIME
    //Always allocate scsiDrive info since it will always be available no matter the drive type we are talking to!
    scsiDriveInfo = C_CAST(ptrDriveInformation, safe_calloc(1, sizeof(driveInformation)));
    if (device->drive_info.drive_type == ATA_DRIVE || (device->drive_info.passThroughHacks.ataPTHacks.possilbyEmulatedNVMe && device->drive_info.drive_type != NVME_DRIVE))
    {
#if defined (DEBUG_DRIVE_INFO_TIME)
        start_Timer(&ataTime);
#endif //DEBUG_DRIVE_INFO_TIME
        //allocate ataDriveInfo since this is an ATA drive
        ataDriveInfo = C_CAST(ptrDriveInformation, safe_calloc(1, sizeof(driveInformation)));
        if (ataDriveInfo)
        {
            ataDriveInfo->infoType = DRIVE_INFO_SAS_SATA;
            ret = get_ATA_Drive_Information(device, &ataDriveInfo->sasSata);
        }
#if defined (DEBUG_DRIVE_INFO_TIME)
        stop_Timer(&ataTime);
#endif //DEBUG_DRIVE_INFO_TIME
    }
    else if (device->drive_info.drive_type == NVME_DRIVE)
    {
#if defined (DEBUG_DRIVE_INFO_TIME)
        start_Timer(&nvmeTime);
#endif //DEBUG_DRIVE_INFO_TIME
        //allocate nvmeDriveInfo since this is an NVMe drive
        nvmeDriveInfo = C_CAST(ptrDriveInformation, safe_calloc(1, sizeof(driveInformation)));
        if (nvmeDriveInfo)
        {
            nvmeDriveInfo->infoType = DRIVE_INFO_NVME;
            ret = get_NVMe_Drive_Information(device, &nvmeDriveInfo->nvme);
        }
#if defined (DEBUG_DRIVE_INFO_TIME)
        stop_Timer(&nvmeTime);
#endif //DEBUG_DRIVE_INFO_TIME
    }
    if (scsiDriveInfo)
    {
#if defined (DEBUG_DRIVE_INFO_TIME)
        start_Timer(&scsiTime);
#endif //DEBUG_DRIVE_INFO_TIME
        //now that we have software translation always get the scsi data.
        scsiDriveInfo->infoType = DRIVE_INFO_SAS_SATA;
        ret = get_SCSI_Drive_Information(device, &scsiDriveInfo->sasSata);
#if defined (DEBUG_DRIVE_INFO_TIME)
        stop_Timer(&scsiTime);
#endif //DEBUG_DRIVE_INFO_TIME
    }
#if defined (DEBUG_DRIVE_INFO_TIME)
    printf("Discovery Times:\n");
    uint8_t hours = 0;
    uint8_t minutes = 0;
    uint8_t seconds = 0;
    uint64_t ataSeconds = 0;
    uint64_t nvmeSeconds = 0;
    if (device->drive_info.drive_type == ATA_DRIVE || device->drive_info.passThroughHacks.ataPTHacks.possilbyEmulatedNVMe)
    {
        ataSeconds = get_Seconds(ataTime);
        convert_Seconds_To_Displayable_Time(ataSeconds, M_NULLPTR, M_NULLPTR, &hours, &minutes, &seconds);
        printf("ATA: ");
        print_Time_To_Screen(M_NULLPTR, M_NULLPTR, &hours, &minutes, &seconds);
        printf("\n");
    }
    else if (device->drive_info.drive_type == NVME_DRIVE)
    {
        nvmeSeconds = get_Seconds(nvmeTime);
        convert_Seconds_To_Displayable_Time(nvmeSeconds, M_NULLPTR, M_NULLPTR, &hours, &minutes, &seconds);
        printf("NVMe: ");
        print_Time_To_Screen(M_NULLPTR, M_NULLPTR, &hours, &minutes, &seconds);
        printf("\n");
    }
    uint64_t scsiSeconds = get_Seconds(scsiTime);
    convert_Seconds_To_Displayable_Time(scsiSeconds, M_NULLPTR, M_NULLPTR, &hours, &minutes, &seconds);
    printf("SCSI: ");
    print_Time_To_Screen(M_NULLPTR, M_NULLPTR, &hours, &minutes, &seconds);
    printf("\n");
    printf("Total: ");
    scsiSeconds += ataSeconds + nvmeSeconds;
    convert_Seconds_To_Displayable_Time(scsiSeconds, M_NULLPTR, M_NULLPTR, &hours, &minutes, &seconds);
    print_Time_To_Screen(M_NULLPTR, M_NULLPTR, &hours, &minutes, &seconds);
    printf("\n");
#endif //DEBUG_DRIVE_INFO_TIME

    if (ret == SUCCESS && (ataDriveInfo || scsiDriveInfo || usbDriveInfo || nvmeDriveInfo))
    {
        //call the print functions appropriately
        if (showChildInformation && (device->drive_info.drive_type != SCSI_DRIVE || device->drive_info.passThroughHacks.ataPTHacks.possilbyEmulatedNVMe) && scsiDriveInfo && (ataDriveInfo || nvmeDriveInfo))
        {
            if ((device->drive_info.drive_type == ATA_DRIVE || device->drive_info.passThroughHacks.ataPTHacks.possilbyEmulatedNVMe) && ataDriveInfo)
            {
                print_Parent_And_Child_Information(scsiDriveInfo, ataDriveInfo);
            }
            else if (device->drive_info.drive_type == NVME_DRIVE && nvmeDriveInfo)
            {
                print_Parent_And_Child_Information(scsiDriveInfo, nvmeDriveInfo);
            }
        }
        else
        {
            //ONLY call the external function when we are able to get some passthrough information back as well
            if ((device->drive_info.interface_type == USB_INTERFACE || device->drive_info.interface_type == IEEE_1394_INTERFACE) && ataDriveInfo && scsiDriveInfo && device->drive_info.drive_type == ATA_DRIVE)
            {
                usbDriveInfo = C_CAST(ptrDriveInformation, safe_calloc(1, sizeof(driveInformation)));
                if (usbDriveInfo)
                {
                    usbDriveInfo->infoType = DRIVE_INFO_SAS_SATA;
                    generate_External_Drive_Information(&usbDriveInfo->sasSata, &scsiDriveInfo->sasSata, &ataDriveInfo->sasSata);
                    print_Device_Information(usbDriveInfo);
                }
                else
                {
                    ret = MEMORY_FAILURE;
                    printf("Error allocating memory for USB - ATA drive info\n");
                }
            }
            else if (device->drive_info.interface_type == USB_INTERFACE && device->drive_info.drive_type == NVME_DRIVE && nvmeDriveInfo && scsiDriveInfo)
            {
                usbDriveInfo = C_CAST(ptrDriveInformation, safe_calloc(1, sizeof(driveInformation)));
                if (usbDriveInfo)
                {
                    usbDriveInfo->infoType = DRIVE_INFO_SAS_SATA;
                    generate_External_NVMe_Drive_Information(&usbDriveInfo->sasSata, &scsiDriveInfo->sasSata, &nvmeDriveInfo->nvme);
                    print_Device_Information(usbDriveInfo);
                }
                else
                {
                    ret = MEMORY_FAILURE;
                    printf("Error allocating memory for USB - NVMe drive info\n");
                }
            }
            else//ata or scsi
            {
                if (device->drive_info.drive_type == ATA_DRIVE && ataDriveInfo)
                {
                    print_Device_Information(ataDriveInfo);
                }
                else if (device->drive_info.drive_type == NVME_DRIVE && nvmeDriveInfo)
                {
                    print_Device_Information(nvmeDriveInfo);
                    //print_Nvme_Ctrl_Information(device);
                }
                else if (scsiDriveInfo)
                {
                    print_Device_Information(scsiDriveInfo);
                }
                else
                {
                    printf("Error allocating memory to get device information.\n");
                }
            }
        }
    }
    safe_free_drive_info(&ataDriveInfo);
    safe_free_drive_info(&scsiDriveInfo);
    safe_free_drive_info(&usbDriveInfo);
    safe_free_drive_info(&nvmeDriveInfo);
    return ret;
}

const char* print_drive_type(tDevice* device)
{
    if (device != M_NULLPTR)
    {
        if (device->drive_info.drive_type == ATA_DRIVE)
        {
            return "ATA";
        }
        else if (device->drive_info.drive_type == SCSI_DRIVE)
        {
            return "SCSI";
        }
        else if (device->drive_info.drive_type == NVME_DRIVE)
        {
            return "NVMe";
        }
        else if (device->drive_info.drive_type == RAID_DRIVE)
        {
            return "RAID";
        }
        else if (device->drive_info.drive_type == ATAPI_DRIVE)
        {
            return "ATAPI";
        }
        else if (device->drive_info.drive_type == FLASH_DRIVE)
        {
            return "FLASH";
        }
        else if (device->drive_info.drive_type == LEGACY_TAPE_DRIVE)
        {
            return "TAPE";
        }
        else
        {
            return "UNKNOWN";
        }
    }
    else
    {
        return "Invalid device structure pointer";
    }
}
