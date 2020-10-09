//
// Do NOT modify or remove this copyright and license
//
// Copyright (c) 2012 - 2020 Seagate Technology LLC and/or its Affiliates, All Rights Reserved
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
    uint8_t *getPhysicalElements = (uint8_t*)calloc_aligned(getPhysicalElementsDataSize, sizeof(uint8_t), device->os_info.minimumAlignment);
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
                elementList[elementIter].restorationAllowed = getPhysicalElements[offset + 13] & BIT0 > 0 ? true : false;
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
        safe_Free_aligned(getPhysicalElements);
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
    printf("-------------------------------------------------------------------\n");
    for (uint32_t elementIter = 0; elementIter < numberOfElements; ++elementIter)
    {
        char statusString[23] = { 0 };
        char capacityString[21] = { 0 };
        char elementType = 'P';//physical element
        char rebuildAllowed[4] = { 0 };
        if (/* elementList[elementIter].elementHealth >= 0 && */ elementList[elementIter].elementHealth <= 0x63)
        {
            sprintf(statusString, "In Limit");
        }
        else if (elementList[elementIter].elementHealth == 0x64)
        {
            sprintf(statusString, "At Limit");
        }
        else if (elementList[elementIter].elementHealth >= 0x65 && elementList[elementIter].elementHealth <= 0xCF)
        {
            sprintf(statusString, "Over Limit");
        }
        else if (elementList[elementIter].elementHealth == 0xFD)
        {
            sprintf(statusString, "Depopulate Error");
        }
        else if (elementList[elementIter].elementHealth == 0xFE)
        {
            sprintf(statusString, "Depopulate in progress");
        }
        else if (elementList[elementIter].elementHealth == 0xFF)
        {
            sprintf(statusString, "Depopulated");
        }
        else
        {
            sprintf(statusString, "Reserved");
        }
        if (elementList[elementIter].associatedCapacity == UINT64_MAX)
        {
            //Drive doesn't report this
            sprintf(capacityString, "N/A");
        }
        else
        {
            sprintf(capacityString, "%" PRIu64, elementList[elementIter].associatedCapacity);
        }
        if (elementList[elementIter].elementType == 1)
        {
            elementType = 'S';
        }
        if (elementList[elementIter].restorationAllowed)
        {
            sprintf(rebuildAllowed, "Yes");
        }
        else
        {
            sprintf(rebuildAllowed, "No");
        }
        printf("%9" PRIu32 "\t%c  \t%3" PRIu8 " \t%-23s\t%s\t%s\n", elementList[elementIter].elementIdentifier, elementType, elementList[elementIter].elementHealth, statusString, capacityString, rebuildAllowed);
    }
    printf("\nNOTE: At least one element must be able to be rebuilt to repopulate and rebuild.\n");
    return;
}

int depopulate_Physical_Element(tDevice *device, uint32_t elementDescriptorID, uint64_t requestedMaxLBA)
{
    int ret = NOT_SUPPORTED;
    if (device->drive_info.drive_type == ATA_DRIVE)
    {
        ret = ata_Remove_Element_And_Truncate(device, elementDescriptorID, requestedMaxLBA);
    }
    else if (device->drive_info.drive_type == SCSI_DRIVE)
    {
        ret = scsi_Remove_And_Truncate(device, requestedMaxLBA, elementDescriptorID);
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
                    if (supportedCapabilitiesQWord18 & BIT3)
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
    if (device->drive_info.drive_type == ATA_DRIVE)
    {
        ret = ata_Restore_Elements_And_Rebuild(device);
    }
    else if (device->drive_info.drive_type == SCSI_DRIVE)
    {
        ret = scsi_Restore_Elements_And_Rebuild(device);
    }
    return ret;
}