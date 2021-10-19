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
// \file depopulate.h
// \brief This file defines the functions for depopulating physical/storage elements on a drive (Remanufacture)

#include "depopulate.h"
#include "seagate_operations.h" //Including this so we can read the Seagate vendos specific version stuff and mask it to look like ACS4/SBC4
#include "platform_helper.h"

bool is_Depopulation_Feature_Supported(tDevice *device, uint64_t *depopulationTime)
{
    bool supported = false;
    if (device->drive_info.drive_type == ATA_DRIVE)
    {
        //support is listed in the ID Data log, supported capabilities page
        uint8_t supportedCapabilities[LEGACY_DRIVE_SEC_SIZE] = { 0 };
        if (SUCCESS == send_ATA_Read_Log_Ext_Cmd(device, ATA_LOG_IDENTIFY_DEVICE_DATA, ATA_ID_DATA_LOG_SUPPORTED_CAPABILITIES, supportedCapabilities, LEGACY_DRIVE_SEC_SIZE, 0))
        {
            uint64_t supportedCapabilitiesQWord0 = M_BytesTo8ByteValue(supportedCapabilities[7], supportedCapabilities[6], supportedCapabilities[5], supportedCapabilities[4], supportedCapabilities[3], supportedCapabilities[2], supportedCapabilities[1], supportedCapabilities[0]);
            if (supportedCapabilitiesQWord0 & BIT63 && M_GETBITRANGE(supportedCapabilitiesQWord0, 23, 16) == 0x03)//make sure required bits/fields are there...checking for bit63 to be 1 and page number to be 3
            {
                uint64_t supportedCapabilitiesQWord18 = M_BytesTo8ByteValue(supportedCapabilities[159], supportedCapabilities[158], supportedCapabilities[157], supportedCapabilities[156], supportedCapabilities[155], supportedCapabilities[154], supportedCapabilities[153], supportedCapabilities[152]);
                if (supportedCapabilitiesQWord18 & BIT63)//making sure this is set for "validity"
                {
                    if (supportedCapabilitiesQWord18 & BIT1 && supportedCapabilitiesQWord18 & BIT0)//checking for both commands to be supported
                    {
                        supported = true;
                    }
                }
                //get depopulation execution time
                if (depopulationTime)
                {
                    uint64_t supportedCapabilitiesQWord19 = M_BytesTo8ByteValue(supportedCapabilities[167], supportedCapabilities[166], supportedCapabilities[165], supportedCapabilities[164], supportedCapabilities[163], supportedCapabilities[162], supportedCapabilities[161], supportedCapabilities[160]);
                    if (supportedCapabilitiesQWord19 & BIT63)//check for validity
                    {
                        *depopulationTime = supportedCapabilitiesQWord19 & UINT64_C(0x7FFFFFFFFFFFFFFF);
                    }
                    else
                    {
                        *depopulationTime = UINT64_MAX;//so we can set the timeout on the command or say "time not reported"
                    }
                }
            }
        }
    }
    else if (device->drive_info.drive_type == SCSI_DRIVE)
    {
        //send some report supported operation code commands to figure it out
        uint8_t reportOpCodes[20] = { 0 };
        bool getElementStatusSupported = false;
        bool removeAndTruncateSupported = false;
        if (SUCCESS == scsi_Report_Supported_Operation_Codes(device, false, REPORT_OPERATION_CODE_AND_SERVICE_ACTION, 0x9E, 0x17, 20, reportOpCodes))
        {
            switch (reportOpCodes[1] & 0x07)
            {
            case 0: //not available right now...so not supported
            case 1://not supported
                break;
            case 3://supported according to spec
            case 5://supported in vendor specific mannor in same format as case 3
                getElementStatusSupported = true;
                break;
            default:
                break;
            }
        }
        if (SUCCESS == scsi_Report_Supported_Operation_Codes(device, false, REPORT_OPERATION_CODE_AND_SERVICE_ACTION, 0x9E, 0x18, 20, reportOpCodes))
        {
            switch (reportOpCodes[1] & 0x07)
            {
            case 0: //not available right now...so not supported
            case 1://not supported
                break;
            case 3://supported according to spec
            case 5://supported in vendor specific mannor in same format as case 3
                removeAndTruncateSupported = true;
                break;
            default:
                break;
            }
        }
        if (removeAndTruncateSupported && getElementStatusSupported)
        {
            supported = true;
            if (depopulationTime)
            {
                *depopulationTime = UINT64_MAX;
                uint8_t blockDeviceCharacteristics[VPD_BLOCK_DEVICE_CHARACTERISTICS_LEN] = { 0 };
                if (SUCCESS == scsi_Inquiry(device, blockDeviceCharacteristics, VPD_BLOCK_DEVICE_CHARACTERISTICS_LEN, BLOCK_DEVICE_CHARACTERISTICS, true, false))
                {
                    *depopulationTime = M_BytesTo4ByteValue(blockDeviceCharacteristics[12], blockDeviceCharacteristics[13], blockDeviceCharacteristics[14], blockDeviceCharacteristics[15]);
                }
            }
        }
    }
    return supported;
}

int get_Number_Of_Descriptors(tDevice *device, uint32_t *numberOfDescriptors)
{
    int ret = NOT_SUPPORTED;
    if (!numberOfDescriptors)
    {
        return BAD_PARAMETER;
    }
    uint8_t getPhysicalElementCount[LEGACY_DRIVE_SEC_SIZE] = { 0 };
    if (device->drive_info.drive_type == ATA_DRIVE)
    {
        if (SUCCESS == ata_Get_Physical_Element_Status(device, 0, 0, 0, getPhysicalElementCount, LEGACY_DRIVE_SEC_SIZE))
        {
            *numberOfDescriptors = M_BytesTo4ByteValue(getPhysicalElementCount[3], getPhysicalElementCount[2], getPhysicalElementCount[1], getPhysicalElementCount[0]);
            ret = SUCCESS;
        }
    }
    else if (device->drive_info.drive_type == SCSI_DRIVE)
    {
        if (SUCCESS == scsi_Get_Physical_Element_Status(device, 0, LEGACY_DRIVE_SEC_SIZE, 0, 0, getPhysicalElementCount))
        {
            *numberOfDescriptors = M_BytesTo4ByteValue(getPhysicalElementCount[0], getPhysicalElementCount[1], getPhysicalElementCount[2], getPhysicalElementCount[3]);
            ret = SUCCESS;
        }
    }
    return ret;
}

int get_Physical_Element_Descriptors(tDevice *device, uint32_t numberOfElementsExpected, ptrPhysicalElement elementList)
{
    //NOTE: Seagate legacy method uses head numbers starting at zero, but STD spec starts at 1. Add 1 to anything from Seagate legacy method
    int ret = NOT_SUPPORTED;
    if (!elementList)
    {
        return BAD_PARAMETER;
    }
    //This should be a number of 512B blocks based on how many physical elements are supported by the drive.
    //NOTE: If this ever starts requesting a LOT of data, then this may need to be broken into multiple commands. - TJE
    uint32_t getPhysicalElementsDataSize = (numberOfElementsExpected * 32 /*bytes per descriptor*/) + 32 /*bytes for data header*/;
    //now round that to the nearest 512B sector
    getPhysicalElementsDataSize = ((getPhysicalElementsDataSize + LEGACY_DRIVE_SEC_SIZE - 1) / LEGACY_DRIVE_SEC_SIZE) * LEGACY_DRIVE_SEC_SIZE;
    uint8_t *getPhysicalElements = C_CAST(uint8_t*, calloc_aligned(getPhysicalElementsDataSize, sizeof(uint8_t), device->os_info.minimumAlignment));
    if (getPhysicalElements)
    {
        uint32_t numberOfDescriptorsReturned = 0;
        if (device->drive_info.drive_type == ATA_DRIVE)
        {
            if (SUCCESS == ata_Get_Physical_Element_Status(device, 0, 0, 0, getPhysicalElements, getPhysicalElementsDataSize))
            {
                ret = SUCCESS;
                //Fill in the struct here since ATA is little endian
                numberOfDescriptorsReturned = M_BytesTo4ByteValue(getPhysicalElements[7], getPhysicalElements[6], getPhysicalElements[5], getPhysicalElements[4]);
                if (numberOfElementsExpected != numberOfDescriptorsReturned)
                {
                    ///uhh...we need to choose the loop condition
                }
            }
        }
        else if (device->drive_info.drive_type == SCSI_DRIVE)
        {
            if (SUCCESS == scsi_Get_Physical_Element_Status(device, 0, getPhysicalElementsDataSize, 0, 0, getPhysicalElements))
            {
                ret = SUCCESS;
                //Fill in the struct here since SCSI is big endian
                numberOfDescriptorsReturned = M_BytesTo4ByteValue(getPhysicalElements[4], getPhysicalElements[5], getPhysicalElements[6], getPhysicalElements[7]);
                if (numberOfElementsExpected != numberOfDescriptorsReturned)
                {
                    ///uhh...we need to choose the loop condition
                }
            }
        }
        if (ret == SUCCESS)
        {
            for (uint32_t elementIter = 0, offset = 32; elementIter < numberOfDescriptorsReturned && offset < getPhysicalElementsDataSize; ++elementIter, offset += 32)
            {
                //byte swap when we need it (ATA vs SCSI)
                elementList[elementIter].elementIdentifier = M_BytesTo4ByteValue(getPhysicalElements[offset + 4], getPhysicalElements[offset + 5], getPhysicalElements[offset + 6], getPhysicalElements[offset + 7]);
                if (device->drive_info.drive_type == ATA_DRIVE)
                {
                    byte_Swap_32(&elementList[elementIter].elementIdentifier);
                }
                elementList[elementIter].restorationAllowed = M_ToBool(getPhysicalElements[offset + 13] & BIT0);
                elementList[elementIter].elementType = getPhysicalElements[offset + 14];
                elementList[elementIter].elementHealth = getPhysicalElements[offset + 15];
                //byte swap when we need it (ATA vs SCSI)
                elementList[elementIter].associatedCapacity = M_BytesTo8ByteValue(getPhysicalElements[offset + 16], getPhysicalElements[offset + 17], getPhysicalElements[offset + 18], getPhysicalElements[offset + 19], getPhysicalElements[offset + 20], getPhysicalElements[offset + 21], getPhysicalElements[offset + 22], getPhysicalElements[offset + 23]);
                if (device->drive_info.drive_type == ATA_DRIVE)
                {
                    byte_Swap_64(&elementList[elementIter].associatedCapacity);
                }
            }
        }
        safe_Free_aligned(getPhysicalElements)
    }
    else
    {
        ret = MEMORY_FAILURE;
    }    
    return ret;
}

void show_Physical_Element_Descriptors(uint32_t numberOfElements, ptrPhysicalElement elementList, uint64_t depopulateTime)
{
    //print out the list of descriptors
    printf("\nElement Types:\n");
    printf("\t P - physical element\n");
    printf("\t S - storage element\n");

    printf("\nApproximate time to depopulate: ");
    if (depopulateTime > 0 && depopulateTime < UINT64_MAX)
    {
        uint8_t days = 0, hours = 0, minutes = 0, seconds = 0;
        convert_Seconds_To_Displayable_Time(depopulateTime, NULL, &days, &hours, &minutes, &seconds);
        print_Time_To_Screen(NULL, &days, &hours, &minutes, &seconds);
        printf("\n");
    }
    else
    {
        printf("Not reported.\n");
    }
    //TODO: add another column for rebuild allowed
    printf("\nElement #\tType\tHealth\tStatus\t\tAssociated MaxLBA\tRebuild Allowed\n");
    printf("----------------------------------------------------------------------------------\n");
    for (uint32_t elementIter = 0; elementIter < numberOfElements; ++elementIter)
    {
#define PHYSICAL_ELEMENT_STATUS_STRING_MAX_LENGTH 23
        char statusString[PHYSICAL_ELEMENT_STATUS_STRING_MAX_LENGTH] = { 0 };
#define PHYSICAL_ELEMENT_CAPACITY_STRING_MAX_LENGTH 21
        char capacityString[PHYSICAL_ELEMENT_CAPACITY_STRING_MAX_LENGTH] = { 0 };
        char elementType = 'P';//physical element
#define PHYSICAL_ELEMENT_REBUILD_ALLOWED_STRING_MAX_LENGTH 4
        char rebuildAllowed[PHYSICAL_ELEMENT_REBUILD_ALLOWED_STRING_MAX_LENGTH] = { 0 };
        if (/* elementList[elementIter].elementHealth >= 0 && */ elementList[elementIter].elementHealth <= 0x63)
        {
            snprintf(statusString, PHYSICAL_ELEMENT_STATUS_STRING_MAX_LENGTH, "In Limit");
        }
        else if (elementList[elementIter].elementHealth == 0x64)
        {
            snprintf(statusString, PHYSICAL_ELEMENT_STATUS_STRING_MAX_LENGTH, "At Limit");
        }
        else if (elementList[elementIter].elementHealth >= 0x65 && elementList[elementIter].elementHealth <= 0xCF)
        {
            snprintf(statusString, PHYSICAL_ELEMENT_STATUS_STRING_MAX_LENGTH, "Over Limit");
        }
        else if (elementList[elementIter].elementHealth == 0xFB)
        {
            snprintf(statusString, PHYSICAL_ELEMENT_STATUS_STRING_MAX_LENGTH, "Repopulate Error");
        }
        else if (elementList[elementIter].elementHealth == 0xFC)
        {
            snprintf(statusString, PHYSICAL_ELEMENT_STATUS_STRING_MAX_LENGTH, "Repopulate in progress");
        }
        else if (elementList[elementIter].elementHealth == 0xFD)
        {
            snprintf(statusString, PHYSICAL_ELEMENT_STATUS_STRING_MAX_LENGTH, "Depopulate Error");
        }
        else if (elementList[elementIter].elementHealth == 0xFE)
        {
            snprintf(statusString, PHYSICAL_ELEMENT_STATUS_STRING_MAX_LENGTH, "Depopulate in progress");
        }
        else if (elementList[elementIter].elementHealth == 0xFF)
        {
            snprintf(statusString, PHYSICAL_ELEMENT_STATUS_STRING_MAX_LENGTH, "Depopulated");
        }
        else
        {
            snprintf(statusString, PHYSICAL_ELEMENT_STATUS_STRING_MAX_LENGTH, "Reserved");
        }
        if (elementList[elementIter].associatedCapacity == UINT64_MAX)
        {
            //Drive doesn't report this
            snprintf(capacityString, PHYSICAL_ELEMENT_CAPACITY_STRING_MAX_LENGTH,  "N/A");
        }
        else
        {
            snprintf(capacityString, PHYSICAL_ELEMENT_CAPACITY_STRING_MAX_LENGTH,  "%" PRIu64, elementList[elementIter].associatedCapacity);
        }
        if (elementList[elementIter].elementType == 1)
        {
            elementType = 'S';
        }
        if (elementList[elementIter].restorationAllowed)
        {
            snprintf(rebuildAllowed, PHYSICAL_ELEMENT_REBUILD_ALLOWED_STRING_MAX_LENGTH, "Yes");
        }
        else
        {
            snprintf(rebuildAllowed, PHYSICAL_ELEMENT_REBUILD_ALLOWED_STRING_MAX_LENGTH, "No");
        }
        printf("%9" PRIu32 "\t%c  \t%3" PRIu8 " \t%-23s\t%s\t%s\n", elementList[elementIter].elementIdentifier, elementType, elementList[elementIter].elementHealth, statusString, capacityString, rebuildAllowed);
    }
    printf("\nNOTE: At least one element must be able to be rebuilt to repopulate and rebuild.\n");
    return;
}

//TODO: This definition belongs in opensea-transport cmds.h/.c
int depopulate_Physical_Element(tDevice *device, uint32_t elementDescriptorID, uint64_t requestedMaxLBA)
{
    int ret = NOT_SUPPORTED;
    os_Lock_Device(device);
    if (device->drive_info.drive_type == ATA_DRIVE)
    {
        ret = ata_Remove_Element_And_Truncate(device, elementDescriptorID, requestedMaxLBA);
    }
    else if (device->drive_info.drive_type == SCSI_DRIVE)
    {
        ret = scsi_Remove_And_Truncate(device, requestedMaxLBA, elementDescriptorID);
    }
    os_Unlock_Device(device);
    return ret;
}

//NOTE: This may NOT give percentage. This will happen on ATA drives, but you can check that it is still running or not. - TJE
//On ATA drives, if in progress, the progress variable will get set to 255 since it is not possible to determine actual progress
int get_Depopulate_Progress(tDevice *device, eDepopStatus *depopStatus, double *progress)
{
    int ret = NOT_SUPPORTED;
    if (!depopStatus)
    {
        return BAD_PARAMETER;
    }
    *depopStatus = DEPOP_NOT_IN_PROGRESS;
    if (device->drive_info.drive_type == ATA_DRIVE)
    {
        bool workaroundIncompleteSense = false;
        uint8_t senseKey = 0, asc = 0, ascq = 0;
        if (SUCCESS == ata_Request_Sense_Data(device, &senseKey, &asc, &ascq))
        {
            if (senseKey == SENSE_KEY_NOT_READY && asc == 0x04 && ascq == 0x24)//depop in progress
            {
                ret = SUCCESS;
                if (progress)
                {
                    *progress = 255.0;
                }
                *depopStatus = DEPOP_IN_PROGRESS;
            }
            else if (senseKey == SENSE_KEY_NOT_READY && asc == 0x04 && ascq == 0x25)//repop in progress
            {
                ret = SUCCESS;
                if (progress)
                {
                    *progress = 255.0;
                }
                *depopStatus = DEPOP_REPOP_IN_PROGRESS;
            }
            else if (senseKey == SENSE_KEY_NOT_READY && asc == 0x04 && ascq == 0x1E)//microcode activation required
            {
                ret = SUCCESS;
                if (progress)
                {
                    *progress = 0.0;
                }
                *depopStatus = DEPOP_MICROCODE_NEEDS_ACTIVATION;
            }
            else if (senseKey == SENSE_KEY_ILLEGAL_REQUEST && asc == 0x24 && ascq == 0x00) //invalid field in CDB
            {
                ret = SUCCESS;
                if (progress)
                {
                    *progress = 0.0;
                }
                *depopStatus = DEPOP_INVALID_FIELD;
            }
            else if (senseKey == SENSE_KEY_MEDIUM_ERROR && asc == 0x31 && ascq == 0x04)//depop failed
            {
                if (progress)
                {
                    *progress = 0.0;
                }
                *depopStatus = DEPOP_FAILED;
            }
            else if (senseKey == SENSE_KEY_MEDIUM_ERROR && asc == 0x31 && ascq == 0x05)//repop failed
            {
                if (progress)
                {
                    *progress = 0.0;
                }
                *depopStatus = DEPOP_REPOP_FAILED;
            }
            else
            {
                workaroundIncompleteSense = true;
            }
        }
        else
        {
            workaroundIncompleteSense = true;
        }
        if (workaroundIncompleteSense)
        {
            //Send the get physical element status command and check if any say depopulation/repopulation in progress or had an error.
            //read physical element status to see if any of the specified element number matches any that were found
            uint32_t numberOfDescriptors = 0;
            int getDescirptors = get_Number_Of_Descriptors(device, &numberOfDescriptors);
            if (SUCCESS == getDescirptors && numberOfDescriptors > 0)
            {
                ptrPhysicalElement elementList = C_CAST(ptrPhysicalElement, malloc(numberOfDescriptors * sizeof(physicalElement)));
                if (elementList)
                {
                    memset(elementList, 0, numberOfDescriptors * sizeof(physicalElement));
                    if (SUCCESS == get_Physical_Element_Descriptors(device, numberOfDescriptors, elementList))
                    {
                        //loop through and check associatedCapacity and elementIdentifiers
                        bool foundStatus = false;
                        ret = SUCCESS;
                        for (uint32_t elementID = 0; !foundStatus && elementID < numberOfDescriptors; ++elementID)
                        {
                            switch (elementList[elementID].elementHealth)
                            {
                            case 0xFB://repop error
                                *depopStatus = DEPOP_REPOP_FAILED;
                                if (progress)
                                {
                                    *progress = 0.0;
                                }
                                foundStatus = true;
                                break;
                            case 0xFC://repop in progress
                                *depopStatus = DEPOP_REPOP_IN_PROGRESS;
                                if (progress)
                                {
                                    *progress = 255.0;
                                }
                                foundStatus = true;
                                break;
                            case 0xFD://depop error
                                *depopStatus = DEPOP_FAILED;
                                if (progress)
                                {
                                    *progress = 0.0;
                                }
                                foundStatus = true;
                                break;
                            case 0xFE://depop in progress
                                *depopStatus = DEPOP_IN_PROGRESS;
                                if (progress)
                                {
                                    *progress = 255.0;
                                }
                                foundStatus = true;
                                break;
                            case 0xFF://depop completed successfully
                            default:
                                break;
                            }
                        }
                    }
                    else
                    {
                        ret = NOT_SUPPORTED;
                    }
                    safe_Free(elementList)
                }
            }
            else
            {
                ret = getDescirptors;
            }
        }
    }
    else if (device->drive_info.drive_type == SCSI_DRIVE)
    {
        uint8_t senseData[SPC3_SENSE_LEN] = { 0 };
        if (SUCCESS == scsi_Request_Sense_Cmd(device, true, senseData, SPC3_SENSE_LEN))
        {
            senseDataFields senseFields;
            memset(&senseFields, 0, sizeof(senseDataFields));
            ret = SUCCESS;
            get_Sense_Data_Fields(senseData, SPC3_SENSE_LEN, &senseFields);
            //now that we've read all the fields, check for known sense data and fill in progress if any.
            if (senseFields.scsiStatusCodes.senseKey == SENSE_KEY_NOT_READY && senseFields.scsiStatusCodes.asc == 0x04 && senseFields.scsiStatusCodes.ascq == 0x24)//depop in progress
            {
                ret = SUCCESS;
                if (progress && senseFields.senseKeySpecificInformation.senseKeySpecificValid && senseFields.senseKeySpecificInformation.type == SENSE_KEY_SPECIFIC_PROGRESS_INDICATION)
                {
                    *progress = (senseFields.senseKeySpecificInformation.progress.progressIndication * 100.0) / 65536.0;
                }
                else if (progress)
                {
                    *progress = 255.0;
                }
                *depopStatus = DEPOP_IN_PROGRESS;
            }
            else if (senseFields.scsiStatusCodes.senseKey == SENSE_KEY_NOT_READY && senseFields.scsiStatusCodes.asc == 0x04 && senseFields.scsiStatusCodes.ascq == 0x25)//repop in progress
            {
                ret = SUCCESS;
                if (progress && senseFields.senseKeySpecificInformation.senseKeySpecificValid && senseFields.senseKeySpecificInformation.type == SENSE_KEY_SPECIFIC_PROGRESS_INDICATION)
                {
                    *progress = (senseFields.senseKeySpecificInformation.progress.progressIndication * 100.0) / 65536.0;
                }
                else if (progress)
                {
                    *progress = 255.0;
                }
                *depopStatus = DEPOP_REPOP_IN_PROGRESS;
            }
            else if (senseFields.scsiStatusCodes.senseKey == SENSE_KEY_NOT_READY && senseFields.scsiStatusCodes.asc == 0x04 && senseFields.scsiStatusCodes.ascq == 0x1E)//microcode activation required
            {
                ret = SUCCESS;
                if (progress)
                {
                    *progress = 0.0;
                }
                *depopStatus = DEPOP_MICROCODE_NEEDS_ACTIVATION;
            }
            else if (senseFields.scsiStatusCodes.senseKey == SENSE_KEY_ILLEGAL_REQUEST && senseFields.scsiStatusCodes.asc == 0x24 && senseFields.scsiStatusCodes.ascq == 0x00) //invalid field in CDB
            {
                ret = SUCCESS;
                if (progress)
                {
                    *progress = 0.0;
                }
                *depopStatus = DEPOP_INVALID_FIELD;
            }
            else if (senseFields.scsiStatusCodes.senseKey == SENSE_KEY_MEDIUM_ERROR && senseFields.scsiStatusCodes.asc == 0x31 && senseFields.scsiStatusCodes.ascq == 0x04)//depop failed
            {
                if (progress)
                {
                    *progress = 0.0;
                }
                *depopStatus = DEPOP_FAILED;
            }
            else if (senseFields.scsiStatusCodes.senseKey == SENSE_KEY_MEDIUM_ERROR && senseFields.scsiStatusCodes.asc == 0x31 && senseFields.scsiStatusCodes.ascq == 0x05)//repop failed
            {
                if (progress)
                {
                    *progress = 0.0;
                }
                *depopStatus = DEPOP_REPOP_FAILED;
            }
            else
            {
                //nothing to report.
            }
        }
        else
        {
            //TODO: If this failed, there is likely a bigger problem! But we can try getting physical element status
            ret = FAILURE;
        }
    }
    return ret;
}

int show_Depop_Repop_Progress(tDevice *device)
{
    int ret = NOT_SUPPORTED;
    eDepopStatus depopStatus = DEPOP_NOT_IN_PROGRESS;
    double progress = 0.0;
    if (SUCCESS == get_Depopulate_Progress(device, &depopStatus, &progress))
    {
        ret = SUCCESS;
        switch (depopStatus)
        {
        case DEPOP_NOT_IN_PROGRESS:
            printf("Depopulation/repopulation is not in progress.\n");
            break;
        case DEPOP_IN_PROGRESS:
            printf("Depopulation in progress: ");
            if (progress > 100)
            {
                printf("Progress indication not available.\n");
            }
            else
            {
                printf("%0.02f%%\n", progress);
            }
            ret = IN_PROGRESS;
            break;
        case DEPOP_REPOP_IN_PROGRESS:
            printf("Repopulation in progress: ");
            if (progress > 100)
            {
                printf("Progress indication not available.\n");
            }
            else
            {
                printf("%0.02f%%\n", progress);
            }
            ret = IN_PROGRESS;
            break;
        case DEPOP_FAILED:
            printf("Depopulation failed.\n");
            break;
        case DEPOP_REPOP_FAILED:
            printf("Repopulation failed.\n");
            break;
        case DEPOP_MICROCODE_NEEDS_ACTIVATION:
            printf("Depopulation/repopulation requires microcode activation before it can be run.\n");
            break;
        default:
            printf("Unknown depopulation/repopulation status. The feature may not be supported, or is not running.\n");
            break;
        }
    }
    else
    {
        ret = FAILURE;
        printf("A failure was encountered when checking for progress on depopulation/repopulation.\n");
    }
    return ret;
}

int perform_Depopulate_Physical_Element(tDevice *device, uint32_t elementDescriptorID, uint64_t requestedMaxLBA, bool pollForProgress)
{
    int ret = NOT_SUPPORTED;
    uint64_t depopTime = 0;
    if (is_Depopulation_Feature_Supported(device, &depopTime))
    {
        if (device->deviceVerbosity >= VERBOSITY_DEFAULT)
        {
            if (depopTime == UINT64_MAX || depopTime == 0)
            {
                printf("Starting depopulation. Approximate time until completion is not available.\n");
            }
            else
            {
                uint8_t days = 0, hours = 0, minutes = 0, seconds = 0;
                convert_Seconds_To_Displayable_Time(depopTime, NULL, &days, &hours, &minutes, &seconds);
                printf("Starting depopulation. Approximate time until completion: ");
                print_Time_To_Screen(NULL, &days, &hours, &minutes, &seconds);
                printf("\n");
            }
            printf("Do not remove power or attempt other access as interrupting it may make\n");
            printf("the drive unusable or require performing this command again!!\n");
        }
        ret = depopulate_Physical_Element(device, elementDescriptorID, requestedMaxLBA);
        if (ret != SUCCESS)
        {
            bool determineInvalidElementOrMaxLBA = false;
            if (device->drive_info.drive_type == SCSI_DRIVE)
            {
                //On SAS, we'll have sense data, on ATA we can attempt to request sense, but some systems/controllers do this for us and make this impossible to retrieve...so we need to work around this
                uint8_t senseKey = 0, asc = 0, ascq = 0, fru = 0;
                //First check what sense data we already have...
                get_Sense_Key_ASC_ASCQ_FRU(device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, &senseKey, &asc, &ascq, &fru);
                //if this matches known cases, we're good to go...otherwise if ATA try requesting sense data ext command.
                if (senseKey == SENSE_KEY_NOT_READY && asc == 0x04 && ascq == 0x1E)//microcode activation required
                {
                    if (device->deviceVerbosity >= VERBOSITY_DEFAULT)
                    {
                        printf("Depopulation cannot be started. Microcode must be activated first.\n");
                    }
                    ret = FAILURE;
                }
                else if (senseKey == SENSE_KEY_ILLEGAL_REQUEST && asc == 0x24 && ascq == 0x00)
                {
                    ret = FAILURE;
                    determineInvalidElementOrMaxLBA = true;
                }
            }
            else if (device->drive_info.drive_type == ATA_DRIVE)
            {
                bool workaroundIncompleteSense = false;
                uint8_t senseKey = 0, asc = 0, ascq = 0;
                if (SUCCESS == ata_Request_Sense_Data(device, &senseKey, &asc, &ascq))
                {
                    if (senseKey == SENSE_KEY_NOT_READY && asc == 0x04 && ascq == 0x1E)//microcode activation required
                    {
                        if (device->deviceVerbosity >= VERBOSITY_DEFAULT)
                        {
                            printf("Depopulation cannot be started. Microcode must be activated first.\n");
                        }
                        ret = FAILURE;
                    }
                    else if (senseKey == SENSE_KEY_ILLEGAL_REQUEST && asc == 0x24 && ascq == 0x00)
                    {
                        ret = FAILURE;
                        determineInvalidElementOrMaxLBA = true;
                    }
                    else
                    {
                        workaroundIncompleteSense = true;
                    }
                }
                else
                {
                    workaroundIncompleteSense = true;
                }
                if (workaroundIncompleteSense)
                {
                    bool reasonFound = false;
                    //This means that something about the command was not liked...first check if microcode needs activation
                    uint8_t currentSettings[LEGACY_DRIVE_SEC_SIZE] = { 0 };
                    if (SUCCESS == send_ATA_Read_Log_Ext_Cmd(device, ATA_LOG_IDENTIFY_DEVICE_DATA, ATA_ID_DATA_LOG_CURRENT_SETTINGS, currentSettings, LEGACY_DRIVE_SEC_SIZE, 0))
                    {
                        uint64_t currentSettingsHeader = M_BytesTo8ByteValue(currentSettings[7], currentSettings[6], currentSettings[5], currentSettings[4], currentSettings[3], currentSettings[2], currentSettings[1], currentSettings[0]);
                        if (currentSettingsHeader & BIT63 && M_Byte2(currentSettingsHeader) == ATA_ID_DATA_LOG_CURRENT_SETTINGS && M_Word0(currentSettingsHeader) >= 0x0001)
                        {
                            //valid data
                            uint64_t currentSettingsQWord = M_BytesTo8ByteValue(currentSettings[15], currentSettings[14], currentSettings[13], currentSettings[12], currentSettings[11], currentSettings[10], currentSettings[9], currentSettings[8]);
                            if (currentSettingsQWord & BIT63 && currentSettingsQWord & BIT19)
                            {
                                if (device->deviceVerbosity >= VERBOSITY_DEFAULT)
                                {
                                    printf("Depopulation cannot be started. Microcode must be activated first.\n");
                                }
                                ret = FAILURE;
                                reasonFound = true;
                            }
                        }
                    }
                    if (!reasonFound)
                    {
                        determineInvalidElementOrMaxLBA = true;
                    }
                }
            }
            if (determineInvalidElementOrMaxLBA)
            {
                bool invalidElement = false;
                bool invalidMaxLBA = false;
                ret = FAILURE;
                //read physical element status to see if any of the specified element number matches any that were found
                uint32_t numberOfDescriptors = 0;
                get_Number_Of_Descriptors(device, &numberOfDescriptors);
                if (numberOfDescriptors > 0)
                {
                    ptrPhysicalElement elementList = C_CAST(ptrPhysicalElement, malloc(numberOfDescriptors * sizeof(physicalElement)));
                    if (elementList)
                    {
                        memset(elementList, 0, numberOfDescriptors * sizeof(physicalElement));
                        if (SUCCESS == get_Physical_Element_Descriptors(device, numberOfDescriptors, elementList))
                        {
                            //loop through and check associatedCapacity and elementIdentifiers
                            bool foundDescriptor = false;
                            for (uint32_t elementID = 0; elementID < numberOfDescriptors; ++elementID)
                            {
                                if (elementList[elementID].elementIdentifier == elementDescriptorID)
                                {
                                    //found the descriptor, so it's not a issue of not finding it
                                    foundDescriptor = true;
                                    //check associated maxLBA
                                    if (elementList[elementID].associatedCapacity != UINT64_MAX && requestedMaxLBA != 0 && requestedMaxLBA > elementList[elementID].associatedCapacity)
                                    {
                                        //tried requesting a new capacity greater than what the device can support...so this will trigger an error
                                        invalidMaxLBA = true;
                                    }
                                }
                            }
                            if (!foundDescriptor)
                            {
                                invalidElement = true;
                            }
                        }
                        safe_Free(elementList)
                    }
                }
                if (device->deviceVerbosity >= VERBOSITY_DEFAULT)
                {
                    if (invalidElement)
                    {
                        printf("Depopulation failed due to invalid element specified\n");
                    }
                    else if (invalidMaxLBA)
                    {
                        printf("Depopulation failed due to invalid new max LBA specified\n");
                    }
                    else
                    {
                        printf("Depopulation failed with invalid field. Invalid element specified, or invalid new max LBA or some other error\n");
                    }
                }
            }
            else
            {
                if (device->deviceVerbosity >= VERBOSITY_DEFAULT)
                {
                    printf("An unknown error was encountered when attempting to depopulate elements.\n");
                }
                ret = FAILURE;
            }
        }
        else
        {
            if (pollForProgress)
            {
                //SCSI and ATA will be handled differently.
                //SCSI can report progress percentage in sense data. ATA does not do this.
                //Furthermore, request sense data from ATA may or may not work or get the information we want
                eDepopStatus depopStatus = DEPOP_NOT_IN_PROGRESS;//start with this until we start polling
                double progress = 0.0;
                uint16_t delayTime = 15;
                int progressCheck = SUCCESS;
                if (device->deviceVerbosity >= VERBOSITY_DEFAULT)
                {
                    printf("\n");
                }
                do
                {
                    delay_Seconds(delayTime);
                    progressCheck = get_Depopulate_Progress(device, &depopStatus, &progress);
                    if (depopStatus == DEPOP_IN_PROGRESS)
                    {
                        if (progress > 100.0)
                        {
                            if (device->deviceVerbosity >= VERBOSITY_DEFAULT)
                            {
                                printf("\rDepopulation is progress, but progress indication is not available.");
                            }
                        }
                        else
                        {
                            if (device->deviceVerbosity >= VERBOSITY_DEFAULT)
                            {
                                printf("\rDepopulation progress: %0.02f%%", progress);
                            }
                        }
                    }
                } while (depopStatus == DEPOP_IN_PROGRESS && progressCheck == SUCCESS);
                switch (depopStatus)
                {
                case DEPOP_NOT_IN_PROGRESS:
                    ret = SUCCESS;
                    break;
                case DEPOP_FAILED:
                case DEPOP_REPOP_FAILED:
                    ret = FAILURE;
                    break;
                case DEPOP_INVALID_FIELD:
                case DEPOP_MICROCODE_NEEDS_ACTIVATION:
                default:
                    ret = UNKNOWN;
                    break;
                }
            }
        }
    }
    return ret;
}

bool is_Repopulate_Feature_Supported(tDevice *device, uint64_t *depopulationTime)
{
    bool supported = false;
    if (device->drive_info.drive_type == ATA_DRIVE)
    {
        //support is listed in the ID Data log, supported capabilities page
        uint8_t supportedCapabilities[LEGACY_DRIVE_SEC_SIZE] = { 0 };
        if (SUCCESS == send_ATA_Read_Log_Ext_Cmd(device, ATA_LOG_IDENTIFY_DEVICE_DATA, ATA_ID_DATA_LOG_SUPPORTED_CAPABILITIES, supportedCapabilities, LEGACY_DRIVE_SEC_SIZE, 0))
        {
            uint64_t supportedCapabilitiesQWord0 = M_BytesTo8ByteValue(supportedCapabilities[7], supportedCapabilities[6], supportedCapabilities[5], supportedCapabilities[4], supportedCapabilities[3], supportedCapabilities[2], supportedCapabilities[1], supportedCapabilities[0]);
            if (supportedCapabilitiesQWord0 & BIT63 && M_GETBITRANGE(supportedCapabilitiesQWord0, 23, 16) == 0x03)//make sure required bits/fields are there...checking for bit63 to be 1 and page number to be 3
            {
                uint64_t supportedCapabilitiesQWord18 = M_BytesTo8ByteValue(supportedCapabilities[159], supportedCapabilities[158], supportedCapabilities[157], supportedCapabilities[156], supportedCapabilities[155], supportedCapabilities[154], supportedCapabilities[153], supportedCapabilities[152]);
                if (supportedCapabilitiesQWord18 & BIT63)//making sure this is set for "validity"
                {
                    if (supportedCapabilitiesQWord18 & BIT2)
                    {
                        supported = true;
                    }
                }
                //get depopulation execution time
                if (depopulationTime)
                {
                    uint64_t supportedCapabilitiesQWord19 = M_BytesTo8ByteValue(supportedCapabilities[167], supportedCapabilities[166], supportedCapabilities[165], supportedCapabilities[164], supportedCapabilities[163], supportedCapabilities[162], supportedCapabilities[161], supportedCapabilities[160]);
                    if (supportedCapabilitiesQWord19 & BIT63)//check for validity
                    {
                        *depopulationTime = supportedCapabilitiesQWord19 & UINT64_C(0x7FFFFFFFFFFFFFFF);
                    }
                    else
                    {
                        *depopulationTime = UINT64_MAX;//so we can set the timeout on the command or say "time not reported"
                    }
                }
            }
        }
    }
    else if (device->drive_info.drive_type == SCSI_DRIVE)
    {
        //send some report supported operation code commands to figure it out
        uint8_t reportOpCodes[20] = { 0 };
        if (SUCCESS == scsi_Report_Supported_Operation_Codes(device, false, REPORT_OPERATION_CODE_AND_SERVICE_ACTION, 0x9E, 0x19, 20, reportOpCodes))
        {
            switch (reportOpCodes[1] & 0x07)
            {
            case 0: //not available right now...so not supported
            case 1://not supported
                break;
            case 3://supported according to spec
            case 5://supported in vendor specific mannor in same format as case 3
                supported = true;
                break;
            default:
                break;
            }
        }
        if (supported)
        {
            supported = true;
            if (depopulationTime)
            {
                *depopulationTime = UINT64_MAX;
                uint8_t blockDeviceCharacteristics[VPD_BLOCK_DEVICE_CHARACTERISTICS_LEN] = { 0 };
                if (SUCCESS == scsi_Inquiry(device, blockDeviceCharacteristics, VPD_BLOCK_DEVICE_CHARACTERISTICS_LEN, BLOCK_DEVICE_CHARACTERISTICS, true, false))
                {
                    *depopulationTime = M_BytesTo4ByteValue(blockDeviceCharacteristics[12], blockDeviceCharacteristics[13], blockDeviceCharacteristics[14], blockDeviceCharacteristics[15]);
                }
            }
        }
    }
    return supported;
}

int repopulate_Elements(tDevice *device)
{
    int ret = NOT_SUPPORTED;
    os_Lock_Device(device);
    if (device->drive_info.drive_type == ATA_DRIVE)
    {
        ret = ata_Restore_Elements_And_Rebuild(device);
    }
    else if (device->drive_info.drive_type == SCSI_DRIVE)
    {
        ret = scsi_Restore_Elements_And_Rebuild(device);
    }
    os_Unlock_Device(device);
    return ret;
}

int perform_Repopulate_Physical_Element(tDevice *device, bool pollForProgress)
{
    int ret = NOT_SUPPORTED;
    uint64_t depopTime = 0;
    if (is_Repopulate_Feature_Supported(device, &depopTime))
    {
        if (device->deviceVerbosity >= VERBOSITY_DEFAULT)
        {
            if (depopTime == UINT64_MAX || depopTime == 0)
            {
                printf("Starting repopulation. Approximate time until completion is not available.\n");
            }
            else
            {
                uint8_t days = 0, hours = 0, minutes = 0, seconds = 0;
                convert_Seconds_To_Displayable_Time(depopTime, NULL, &days, &hours, &minutes, &seconds);
                printf("Starting repopulation. Approximate time until completion: ");
                print_Time_To_Screen(NULL, &days, &hours, &minutes, &seconds);
                printf("\n");
            }
            printf("Do not remove power or attempt other access as interrupting it may make\n");
            printf("the drive unusable or require performing this command again!!\n");
        }
        ret = repopulate_Elements(device);
        if (ret != SUCCESS)
        {
            if (device->drive_info.drive_type == SCSI_DRIVE)
            {
                //On SAS, we'll have sense data, on ATA we can attempt to request sense, but some systems/controllers do this for us and make this impossible to retrieve...so we need to work around this
                uint8_t senseKey = 0, asc = 0, ascq = 0, fru = 0;
                //First check what sense data we already have...
                get_Sense_Key_ASC_ASCQ_FRU(device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, &senseKey, &asc, &ascq, &fru);
                //if this matches known cases, we're good to go...otherwise if ATA try requesting sense data ext command.
                if (senseKey == SENSE_KEY_NOT_READY && asc == 0x04 && ascq == 0x1E)//microcode activation required
                {
                    if (device->deviceVerbosity >= VERBOSITY_DEFAULT)
                    {
                        printf("Repopulation cannot be started. Microcode must be activated first.\n");
                    }
                    ret = FAILURE;
                }
                else if (senseKey == SENSE_KEY_ILLEGAL_REQUEST && asc == 0x2C && ascq == 0x00)//command sequence error means all depopulated elements do not allow restoration (at least one needs to support this)
                {
                    ret = FAILURE;
                }
            }
            else if (device->drive_info.drive_type == ATA_DRIVE)
            {
                bool workaroundIncompleteSense = false;
                uint8_t senseKey = 0, asc = 0, ascq = 0;
                if (SUCCESS == ata_Request_Sense_Data(device, &senseKey, &asc, &ascq))
                {
                    if (senseKey == SENSE_KEY_NOT_READY && asc == 0x04 && ascq == 0x1E)//microcode activation required
                    {
                        if (device->deviceVerbosity >= VERBOSITY_DEFAULT)
                        {
                            printf("Repopulation cannot be started. Microcode must be activated first.\n");
                        }
                        ret = FAILURE;
                    }
                    else if (senseKey == SENSE_KEY_ILLEGAL_REQUEST && asc == 0x2C && ascq == 0x00)
                    {
                        ret = FAILURE;
                    }
                    else
                    {
                        workaroundIncompleteSense = true;
                    }
                }
                else
                {
                    workaroundIncompleteSense = true;
                }
                if (workaroundIncompleteSense)
                {
                    bool reasonFound = false;
                    //This means that something about the command was not liked...first check if microcode needs activation
                    uint8_t currentSettings[LEGACY_DRIVE_SEC_SIZE] = { 0 };
                    if (SUCCESS == send_ATA_Read_Log_Ext_Cmd(device, ATA_LOG_IDENTIFY_DEVICE_DATA, ATA_ID_DATA_LOG_CURRENT_SETTINGS, currentSettings, LEGACY_DRIVE_SEC_SIZE, 0))
                    {
                        uint64_t currentSettingsHeader = M_BytesTo8ByteValue(currentSettings[7], currentSettings[6], currentSettings[5], currentSettings[4], currentSettings[3], currentSettings[2], currentSettings[1], currentSettings[0]);
                        if (currentSettingsHeader & BIT63 && M_Byte2(currentSettingsHeader) == ATA_ID_DATA_LOG_CURRENT_SETTINGS && M_Word0(currentSettingsHeader) >= 0x0001)
                        {
                            //valid data
                            uint64_t currentSettingsQWord = M_BytesTo8ByteValue(currentSettings[15], currentSettings[14], currentSettings[13], currentSettings[12], currentSettings[11], currentSettings[10], currentSettings[9], currentSettings[8]);
                            if (currentSettingsQWord & BIT63 && currentSettingsQWord & BIT19)
                            {
                                if (device->deviceVerbosity >= VERBOSITY_DEFAULT)
                                {
                                    printf("Depopulation cannot be started. Microcode must be activated first.\n");
                                }
                                ret = FAILURE;
                                reasonFound = true;
                            }
                        }
                    }
                    if (!reasonFound)
                    {
                        //read physical element status to see if any of the specified element number matches any that were found
                        uint32_t repopulatableElements = 0;
                        uint32_t currentlyDepopulatedElements = 0;
                        uint32_t numberOfDescriptors = 0;
                        get_Number_Of_Descriptors(device, &numberOfDescriptors);
                        if (numberOfDescriptors > 0)
                        {
                            ptrPhysicalElement elementList = C_CAST(ptrPhysicalElement, malloc(numberOfDescriptors * sizeof(physicalElement)));
                            if (elementList)
                            {
                                memset(elementList, 0, numberOfDescriptors * sizeof(physicalElement));
                                if (SUCCESS == get_Physical_Element_Descriptors(device, numberOfDescriptors, elementList))
                                {
                                    //figure out if any depopulated elements support being repopulated

                                    for (uint32_t elementID = 0; elementID < numberOfDescriptors; ++elementID)
                                    {
                                        if (elementList[elementID].elementHealth == 0xFF)//depopulated successfully
                                        {
                                            ++currentlyDepopulatedElements;
                                            if (elementList[elementID].restorationAllowed)
                                            {
                                                ++repopulatableElements;
                                            }
                                        }
                                    }
                                }
                                safe_Free(elementList)
                            }
                        }
                        if (currentlyDepopulatedElements > 0)
                        {
                            if (repopulatableElements > 0)
                            {
                                if (device->deviceVerbosity >= VERBOSITY_DEFAULT)
                                {
                                    printf("Unknown error when trying to repopulate elements.\n");
                                }
                                ret = FAILURE;
                            }
                            else
                            {
                                if (device->deviceVerbosity >= VERBOSITY_DEFAULT)
                                {
                                    printf("Repopulation of elements is not supported as currently depopulated elements do not\n");
                                    printf("support being repopulated.\n");
                                }
                                ret = FAILURE;
                            }
                        }
                        else
                        {
                            if (device->deviceVerbosity >= VERBOSITY_DEFAULT)
                            {
                                printf("Unknown error when trying to repopulate elements.\n");
                            }
                            ret = FAILURE;
                        }
                    }
                }
            }
        }
        else
        {
            if (pollForProgress)
            {
                //SCSI and ATA will be handled differently.
                //SCSI can report progress percentage in sense data. ATA does not do this.
                //Furthermore, request sense data from ATA may or may not work or get the information we want
                eDepopStatus depopStatus = DEPOP_NOT_IN_PROGRESS;//start with this until we start polling
                double progress = 0.0;
                uint16_t delayTime = 15;
                int progressCheck = SUCCESS;
                if (device->deviceVerbosity >= VERBOSITY_DEFAULT)
                {
                    printf("\n");
                }
                do
                {
                    delay_Seconds(delayTime);
                    progressCheck = get_Depopulate_Progress(device, &depopStatus, &progress);
                    if (depopStatus == DEPOP_REPOP_IN_PROGRESS)
                    {
                        if (progress > 100.0)
                        {
                            if (device->deviceVerbosity >= VERBOSITY_DEFAULT)
                            {
                                printf("\rRepopulation is progress, but progress indication is not available.");
                            }
                        }
                        else
                        {
                            if (device->deviceVerbosity >= VERBOSITY_DEFAULT)
                            {
                                printf("\rRepopulation progress: %0.02f%%", progress);
                            }
                        }
                    }
                } while (depopStatus == DEPOP_REPOP_IN_PROGRESS && progressCheck == SUCCESS);
                switch (depopStatus)
                {
                case DEPOP_NOT_IN_PROGRESS:
                    ret = SUCCESS;
                    break;
                case DEPOP_FAILED:
                case DEPOP_REPOP_FAILED:
                    ret = FAILURE;
                    break;
                case DEPOP_INVALID_FIELD:
                case DEPOP_MICROCODE_NEEDS_ACTIVATION:
                default:
                    ret = UNKNOWN;
                    break;
                }
            }
        }
    }
    return ret;
}
