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

#include "zoned_operations.h"

int get_Number_Of_Zones(tDevice *device, eZoneReportingOptions reportingOptions, uint64_t startingLBA, uint32_t *numberOfMatchingZones)
{
    int ret = SUCCESS;
    if (!numberOfMatchingZones)
    {
        return BAD_PARAMETER;
    }
    uint8_t reportZones[LEGACY_DRIVE_SEC_SIZE] = { 0 };
    uint32_t zoneListLength = 0;
    if (device->drive_info.drive_type == ATA_DRIVE)
    {
        ret = ata_Report_Zones_Ext(device, reportingOptions, false, 1, startingLBA, reportZones, LEGACY_DRIVE_SEC_SIZE);
        zoneListLength = M_BytesTo4ByteValue(reportZones[3], reportZones[2], reportZones[1], reportZones[0]);
    }
    else if (device->drive_info.drive_type == SCSI_DRIVE)
    {
        ret = scsi_Report_Zones(device, reportingOptions, false, LEGACY_DRIVE_SEC_SIZE, startingLBA, reportZones);
        zoneListLength = M_BytesTo4ByteValue(reportZones[0], reportZones[1], reportZones[2], reportZones[3]);
    }
    else
    {
        return NOT_SUPPORTED;
    }
    if (ret != SUCCESS)
    {
        return ret;
    }
    *numberOfMatchingZones = zoneListLength / 64;
    return SUCCESS;
}

int get_Zone_Descriptors(tDevice *device, eZoneReportingOptions reportingOptions, uint64_t startingLBA, uint32_t numberOfZoneDescriptors, ptrZoneDescriptor zoneDescriptors)
{
    int ret = SUCCESS;
    uint8_t *reportZones = NULL;
    uint32_t sectorCount = get_Sector_Count_For_512B_Based_XFers(device);
    uint32_t dataBytesToRequest = (((numberOfZoneDescriptors * 64 + 64) + 511) / LEGACY_DRIVE_SEC_SIZE) * LEGACY_DRIVE_SEC_SIZE;//rounds to nearest 512B
    if (!zoneDescriptors || numberOfZoneDescriptors == 0)
    {
        return BAD_PARAMETER;
    }
    reportZones = (uint8_t*)calloc(LEGACY_DRIVE_SEC_SIZE * sectorCount * sizeof(uint8_t), sizeof(uint8_t));
    if (!reportZones)
    {
        return MEMORY_FAILURE;
    }
    //need to break this into chunks to pull.
    uint64_t nextZoneLBA = startingLBA;
    uint64_t zoneMaxLBA = device->drive_info.deviceMaxLba;//start with this...change later.
    uint32_t zoneIter = 0;
    for (uint32_t pullIter = 0; pullIter < dataBytesToRequest; pullIter += (LEGACY_DRIVE_SEC_SIZE * 64))
    {
        uint32_t localListLength = 0;
        if ((pullIter + (sectorCount * LEGACY_DRIVE_SEC_SIZE)) > dataBytesToRequest)
        {
            sectorCount = (dataBytesToRequest - pullIter) / LEGACY_DRIVE_SEC_SIZE;
        }
        if (device->drive_info.drive_type == ATA_DRIVE)
        {
            ret = ata_Report_Zones_Ext(device, reportingOptions, true, M_Min(dataBytesToRequest / LEGACY_DRIVE_SEC_SIZE, sectorCount), nextZoneLBA, reportZones, (LEGACY_DRIVE_SEC_SIZE * sectorCount));
            localListLength = M_BytesTo4ByteValue(reportZones[3], reportZones[2], reportZones[1], reportZones[0]);
            zoneMaxLBA = M_BytesTo8ByteValue(reportZones[15], reportZones[14], reportZones[13], reportZones[12], reportZones[11], reportZones[10], reportZones[9], reportZones[8]);
        }
        else if (device->drive_info.drive_type == SCSI_DRIVE)
        {
            ret = scsi_Report_Zones(device, reportingOptions, true, (LEGACY_DRIVE_SEC_SIZE * sectorCount), nextZoneLBA, reportZones);
            localListLength = M_BytesTo4ByteValue(reportZones[0], reportZones[1], reportZones[2], reportZones[3]);
            zoneMaxLBA = M_BytesTo8ByteValue(reportZones[8], reportZones[9], reportZones[10], reportZones[11], reportZones[12], reportZones[13], reportZones[14], reportZones[15]);
        }
        else
        {
            safe_Free(reportZones);
            return NOT_SUPPORTED;
        }
        if (ret != SUCCESS)
        {
            return ret;
        }
        //fill in the returned zones.
        for (uint32_t byteIter = 64; zoneIter < numberOfZoneDescriptors && byteIter < localListLength && byteIter < (LEGACY_DRIVE_SEC_SIZE * sectorCount); ++zoneIter, byteIter += 64)
        {
            zoneDescriptors[zoneIter].descriptorValid = true;
            zoneDescriptors[zoneIter].zoneType = (eZoneType)M_Nibble0(reportZones[byteIter + 0]);
            zoneDescriptors[zoneIter].zoneCondition = (eZoneCondition)M_Nibble1(reportZones[byteIter + 1]);
            zoneDescriptors[zoneIter].nonseqBit = reportZones[byteIter + 1] & BIT1;
            zoneDescriptors[zoneIter].resetBit = reportZones[byteIter + 1] & BIT0;
            //zone length
            zoneDescriptors[zoneIter].zoneLength = M_BytesTo8ByteValue(reportZones[byteIter + 8], reportZones[byteIter + 9], reportZones[byteIter + 10], reportZones[byteIter + 11], reportZones[byteIter + 12], reportZones[byteIter + 13], reportZones[byteIter + 14], reportZones[byteIter + 15]);
            //zone start lba
            zoneDescriptors[zoneIter].zoneStartingLBA = M_BytesTo8ByteValue(reportZones[byteIter + 16], reportZones[byteIter + 17], reportZones[byteIter + 18], reportZones[byteIter + 19], reportZones[byteIter + 20], reportZones[byteIter + 21], reportZones[byteIter + 22], reportZones[byteIter + 23]);
            //write pointer lba
            zoneDescriptors[zoneIter].writePointerLBA = M_BytesTo8ByteValue(reportZones[byteIter + 24], reportZones[byteIter + 25], reportZones[byteIter + 26], reportZones[byteIter + 27], reportZones[byteIter + 28], reportZones[byteIter + 29], reportZones[byteIter + 30], reportZones[byteIter + 31]);
            //byte swap for ATA because of endianness differences
            if (device->drive_info.drive_type == ATA_DRIVE)
            {
                //byte swap the zone length, start lba, and write pointer lba. All other fields match SCSI
                byte_Swap_64(&zoneDescriptors[zoneIter].zoneLength);
                byte_Swap_64(&zoneDescriptors[zoneIter].zoneStartingLBA);
                byte_Swap_64(&zoneDescriptors[zoneIter].writePointerLBA);
            }
        }
        //now that we are here, go to the last zone and get the LBA and length to figure out what to request next.
        nextZoneLBA = zoneDescriptors[zoneIter - 1].zoneStartingLBA + zoneDescriptors[zoneIter - 1].zoneLength;//plus 1 to go into the next zone.
        if (nextZoneLBA > zoneMaxLBA)
        {
            break;
        }
    }
    safe_Free(reportZones);
    return SUCCESS;
}

void print_Zone_Descriptor(zoneDescriptor zoneDescriptor)
{
    if (zoneDescriptor.descriptorValid)
    {
        char zoneTypeString[27] = { 0 };
        switch (zoneDescriptor.zoneType)
        {
        case ZONE_TYPE_CONVENTIONAL:
            sprintf(zoneTypeString, "Conventional");
            break;
        case ZONE_TYPE_SEQUENTIAL_WRITE_REQUIRED:
            sprintf(zoneTypeString, "Sequential Write Required");
            break;
        case ZONE_TYPE_SEQUENTIAL_WRITE_PREFERRED:
            sprintf(zoneTypeString, "Sequential Write Preferred");
            break;
        case ZONE_TYPE_RESERVED:
        default:
            sprintf(zoneTypeString, "Reserved");
            break;
        }
        char zoneCondition[18] = { 0 };
        switch (zoneDescriptor.zoneCondition)
        {
        case ZONE_CONDITION_NOT_WRITE_POINTER:
            sprintf(zoneCondition, "Not Write Pointer");
            break;
        case ZONE_CONDITION_EMPTY:
            sprintf(zoneCondition, "Empty");
            break;
        case ZONE_CONDITION_IMLICITLY_OPENED:
            sprintf(zoneCondition, "Implicitly Opened");
            break;
        case ZONE_CONDITION_EXPLICITYLE_OPENED:
            sprintf(zoneCondition, "Explicitly Opened");
            break;
        case ZONE_CONDITION_CLOSED:
            sprintf(zoneCondition, "Closed");
            break;
        case ZONE_CONDITION_READ_ONLY:
            sprintf(zoneCondition, "Read Only");
            break;
        case ZONE_CONDITION_FULL:
            sprintf(zoneCondition, "Full");
            break;
        case ZONE_CONDITION_OFFLINE:
            sprintf(zoneCondition, "Offline");
            break;
        default:
            sprintf(zoneCondition, "Reserved");
            break;
        }
        //TODO: add showing reset bit and nonseq bit?
        printf("%-26s  %-17s  %-20"PRIu64"  %-20"PRIu64"  %-20"PRIu64"\n", zoneTypeString, zoneCondition, zoneDescriptor.zoneStartingLBA, zoneDescriptor.zoneLength, zoneDescriptor.writePointerLBA);
    }
}


void print_Zone_Descriptors(eZoneReportingOptions reportingOptions, uint32_t numberOfZoneDescriptors, ptrZoneDescriptor zoneDescriptors)
{
    char showingZones[40] = { 0 };
    switch (reportingOptions)
    {
    case ZONE_REPORT_LIST_ALL_ZONES:
        sprintf(showingZones, "All Zones");
        break;
    case ZONE_REPORT_LIST_EMPTY_ZONES:
        sprintf(showingZones, "Empty Zones");
        break;
    case ZONE_REPORT_LIST_IMPLICIT_OPEN_ZONES:
        sprintf(showingZones, "Implicitly Open Zones");
        break;
    case ZONE_REPORT_LIST_EXPLICIT_OPEN_ZONES:
        sprintf(showingZones, "Explicitly Open Zones");
        break;
    case ZONE_REPORT_LIST_CLOSED_ZONES:
        sprintf(showingZones, "Closed Zones");
        break;
    case ZONE_REPORT_LIST_FULL_ZONES:
        sprintf(showingZones, "Full Zones");
        break;
    case ZONE_REPORT_LIST_READ_ONLY_ZONES:
        sprintf(showingZones, "Read Only Zones");
        break;
    case ZONE_REPORT_LIST_OFFLINE_ZONES:
        sprintf(showingZones, "Offline Zones");
        break;
    case ZONE_REPORT_LIST_ZONES_WITH_RESET_SET_TO_ONE:
        sprintf(showingZones, "Reset Recommended Zones");
        break;
    case ZONE_REPORT_LIST_ZONES_WITH_NON_SEQ_SET_TO_ONE:
        sprintf(showingZones, "Non-Sequential Resource Active Zones");
        break;
    case ZONE_REPORT_LIST_ALL_ZONES_THAT_ARE_NOT_WRITE_POINTERS:
        sprintf(showingZones, "Not Write Pointer Zones");
        break;
    default:
        sprintf(showingZones, "Unknown/Reserved Zones");
        break;
    }
    if (!zoneDescriptors)
    {
        perror("bad pointer to zoneDescriptors");
        return;
    }
    printf("\n===%s===\n", showingZones);

    //TODO: add showing reset bit and nonseq bit?
    printf("%-26s  %-17s  %-20s  %-20s  %-20s\n", "Zone Type", "Zone Condition", "Start LBA", "Length", "Write Pointer LBA");
    for (uint32_t zoneIter = 0; zoneIter < numberOfZoneDescriptors; ++zoneIter)
    {
        print_Zone_Descriptor(zoneDescriptors[zoneIter]);
    }
    return;
}