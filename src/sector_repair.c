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
// \file sector_repair.c


#include "sector_repair.h"
#include "cmds.h"

int repair_LBA(tDevice *device, ptrErrorLBA LBA, bool forcePassthroughCommand, bool automaticWriteReallocationEnabled, bool automaticReadReallocationEnabled)
{
    int ret = UNKNOWN;
    uint16_t logicalPerPhysical = device->drive_info.devicePhyBlockSize / device->drive_info.deviceBlockSize;
    uint32_t dataSize = device->drive_info.deviceBlockSize * logicalPerPhysical;
    uint8_t *dataBuf = (uint8_t*)calloc(dataSize, sizeof(uint8_t));
    if (!dataBuf)
    {
        return MEMORY_FAILURE;
    }
    LBA->errorAddress = align_LBA(device, LBA->errorAddress);
    LBA->repairStatus = NOT_REPAIRED;
    if (VERBOSITY_QUIET < device->deviceVerbosity)
    {
        printf("\n\tAttempting repair on LBA %"PRIu64" (aligned)", LBA->errorAddress);
    }
    if (forcePassthroughCommand && (device->drive_info.drive_type == ATA_DRIVE || device->drive_info.drive_type == ATAPI_DRIVE))
    {
        if (device->drive_info.interface_type != IDE_INTERFACE)
        {
            //need to use child drive info for write
            uint8_t *temp = NULL;
            logicalPerPhysical = device->drive_info.bridge_info.childDevicePhyBlockSize / device->drive_info.bridge_info.childDeviceBlockSize;
            dataSize = device->drive_info.bridge_info.childDeviceBlockSize * logicalPerPhysical;
            temp = (uint8_t*)realloc(dataBuf, dataSize * sizeof(uint8_t));
            if (!temp)
            {
                return MEMORY_FAILURE;
            }
            dataBuf = temp;
            memset(dataBuf, 0, dataSize);
        }
        ret = ata_Write(device, LBA->errorAddress, false, dataBuf, dataSize);
        if (ret == SUCCESS)
        {
            ret = ata_Flush_Cache_Command(device);
            if (ret == SUCCESS)
            {
                ret = ata_Read_Verify(device, LBA->errorAddress, logicalPerPhysical);
            }
        }
    }
    else
    {
        int readReallocation = FAILURE;//assume failure
        if (automaticReadReallocationEnabled)
        {
            //Attempt a read reallocation to preserve the user's data
            ret = read_LBA(device, LBA->errorAddress, false, dataBuf, dataSize);
            readReallocation = ret;
            if (ret == SUCCESS)
            {
                ret = verify_LBA(device, LBA->errorAddress, logicalPerPhysical);
            }
        }
        if (automaticWriteReallocationEnabled && readReallocation != SUCCESS)
        {
            ret = write_LBA(device, LBA->errorAddress, false, dataBuf, dataSize);
            if (ret == SUCCESS)
            {
                ret = flush_Cache(device);
                if (ret == SUCCESS)
                {
                    ret = verify_LBA(device, LBA->errorAddress, logicalPerPhysical);
                }
            }
        }
        else if (!automaticWriteReallocationEnabled && readReallocation != SUCCESS)
        {
            //write reallocation is not allowed, but try to write it anyways...it may reduce a reallocation.
            //If a write succeeds, we're done and don't need to reallocate.
            //If a write fails, then send the reassign blocks command since the sector needs reassignment
            ret = write_LBA(device, LBA->errorAddress, false, dataBuf, dataSize);
            if (ret == SUCCESS)
            {
                ret = flush_Cache(device);
                if (ret == SUCCESS)
                {
                    ret = verify_LBA(device, LBA->errorAddress, logicalPerPhysical);
                }
            }
            if (ret != SUCCESS && device->drive_info.drive_type != NVME_DRIVE)//make sure the write and verify did actually work! NOTE: NVMe does not have a reassign command or translation for it in translation spec
            {
                //need to use the reallocate command (SCSI...ATA interfaces should attempt translating it through SAT)
                bool longLBA = false;
                uint8_t increment = 4;
                if (LBA->errorAddress + (logicalPerPhysical - 1) > UINT32_MAX)
                {
                    longLBA = true;
                    increment = 8;
                }
                uint32_t reassignListLength = logicalPerPhysical * increment + 4;//+4 is parameter header
                //set up the header
                dataBuf[2] = M_Byte1(logicalPerPhysical * increment);
                dataBuf[3] = M_Byte0(logicalPerPhysical * increment);
                uint64_t reassignLBA = LBA->errorAddress;
                uint32_t offset = 4;
                uint32_t iter = 0;
                //create the list of LBAs. 1 for 1 logical per physical, 8 for 8 logical per physical
                for (iter = 0, offset = 4; iter < logicalPerPhysical; ++iter, offset += increment, ++reassignLBA)
                {
                    if (longLBA)
                    {
                        dataBuf[offset + 0] = M_Byte0(reassignLBA);
                        dataBuf[offset + 1] = M_Byte1(reassignLBA);
                        dataBuf[offset + 2] = M_Byte2(reassignLBA);
                        dataBuf[offset + 3] = M_Byte3(reassignLBA);
                        dataBuf[offset + 4] = M_Byte4(reassignLBA);
                        dataBuf[offset + 5] = M_Byte5(reassignLBA);
                        dataBuf[offset + 6] = M_Byte6(reassignLBA);
                        dataBuf[offset + 7] = M_Byte7(reassignLBA);
                    }
                    else
                    {
                        dataBuf[offset + 0] = M_Byte0(reassignLBA);
                        dataBuf[offset + 1] = M_Byte1(reassignLBA);
                        dataBuf[offset + 2] = M_Byte2(reassignLBA);
                        dataBuf[offset + 3] = M_Byte3(reassignLBA);
                    }
                }
                bool done = false;
                uint8_t counter = 0;
                do
                {
                    //always using short list since we are doing single reallocations at a time...not using enough data to need a long list.
                    ret = scsi_Reassign_Blocks(device, longLBA, false, reassignListLength, dataBuf);
                    //Need to check and make sure that we didn't get a check condition
                    senseDataFields senseFields;
                    memset(&senseFields, 0, sizeof(senseDataFields));
                    get_Sense_Data_Fields(device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, &senseFields);
                    if (senseFields.validStructure)
                    {
                        bool updateList = false;
                        uint64_t commandSpecificLba = UINT64_MAX;
                        uint64_t informationLba = UINT64_MAX;
                        if (senseFields.scsiStatusCodes.format != 0 && senseFields.scsiStatusCodes.senseKey != SENSE_KEY_ILLEGAL_REQUEST && 
                            senseFields.scsiStatusCodes.senseKey != SENSE_KEY_HARDWARE_ERROR)
                        {
                            done = false;
                            // check the command-specific information for a valid LBA.
                            
                            if (senseFields.fixedFormat)
                            {
                                commandSpecificLba = senseFields.fixedCommandSpecificInformation;
                            }
                            else
                            {
                                commandSpecificLba = senseFields.descriptorCommandSpecificInformation;
                            }
                            // if we have a valid LBA, then we need to remove all LBAs prior to that one and reissue the command.
                            if (commandSpecificLba < device->drive_info.deviceMaxLba)
                            {
                                updateList = true;
                            }
                            else
                            {
                                commandSpecificLba = UINT64_MAX;
                                done = true;
                            }
                        }
                        else
                        {
                            done = true;
                        }
                        if (senseFields.scsiStatusCodes.senseKey == SENSE_KEY_MEDIUM_ERROR)
                        {
                            done = false;
                            //check information field for a valid LBA.
                            if (senseFields.fixedFormat)
                            {
                                informationLba = senseFields.fixedInformation;
                            }
                            else
                            {
                                informationLba = senseFields.descriptorInformation;
                            }
                            //if valid, add it to the list and reissue the command
                            if (informationLba < device->drive_info.deviceMaxLba)
                            {
                                updateList = true;
                            }
                            else
                            {
                                informationLba = UINT64_MAX;
                                done = true;
                            }
                        }
                        else
                        {
                            done = true;
                        }
                        if (updateList)
                        {
                            //we got at least one update to do to the list. 
                            //Check both the LBAs we saved above since it's not clear if both conditions can happen at the same time or not. 
                            //Most likely only one or the other though...
                            //TODO: update the list based on what we got above.
                            if (commandSpecificLba != UINT64_MAX)
                            {
                                //update the list to remove LBAs before this one
                                reassignLBA = LBA->errorAddress;
                                for (iter = 0, offset = 4; iter < (uint32_t)logicalPerPhysical; ++iter, ++reassignLBA)
                                {
                                    if (commandSpecificLba <= reassignLBA)
                                    {
                                        if (longLBA)
                                        {
                                            dataBuf[offset + 0] = M_Byte0(reassignLBA);
                                            dataBuf[offset + 1] = M_Byte1(reassignLBA);
                                            dataBuf[offset + 2] = M_Byte2(reassignLBA);
                                            dataBuf[offset + 3] = M_Byte3(reassignLBA);
                                            dataBuf[offset + 4] = M_Byte4(reassignLBA);
                                            dataBuf[offset + 5] = M_Byte5(reassignLBA);
                                            dataBuf[offset + 6] = M_Byte6(reassignLBA);
                                            dataBuf[offset + 7] = M_Byte7(reassignLBA);
                                        }
                                        else
                                        {
                                            dataBuf[offset + 0] = M_Byte0(reassignLBA);
                                            dataBuf[offset + 1] = M_Byte1(reassignLBA);
                                            dataBuf[offset + 2] = M_Byte2(reassignLBA);
                                            dataBuf[offset + 3] = M_Byte3(reassignLBA);
                                        }
                                        offset += increment;
                                    }
                                }
                                //update list length
                                reassignListLength = offset - 4;//minus 4 to get just length of list minus the parameter header
                                if (longLBA)
                                {
                                    dataBuf[0] = M_Byte3(reassignListLength);
                                    dataBuf[1] = M_Byte2(reassignListLength);
                                }
                                dataBuf[2] = M_Byte1(reassignListLength);
                                dataBuf[3] = M_Byte0(reassignListLength);
                                reassignListLength += 4;//add the 4 back in now before we come back around and reissue the command
                            }
                            if (informationLba != UINT64_MAX)
                            {
                                //add this LBA to the list to be reassigned
                                reassignLBA = LBA->errorAddress;
                                bool infoLBAAdded = false;
                                for (iter = 0, offset = 4; iter < ((uint32_t)logicalPerPhysical + 1); ++iter, offset += increment)
                                {
                                    uint64_t listLBA = reassignLBA;
                                    if (!infoLBAAdded && informationLba < reassignLBA)
                                    {
                                        listLBA = informationLba;
                                        infoLBAAdded = true;
                                    }
                                    else
                                    {
                                        ++reassignLBA;
                                    }
                                    if (longLBA)
                                    {
                                        dataBuf[offset + 0] = M_Byte0(listLBA);
                                        dataBuf[offset + 1] = M_Byte1(listLBA);
                                        dataBuf[offset + 2] = M_Byte2(listLBA);
                                        dataBuf[offset + 3] = M_Byte3(listLBA);
                                        dataBuf[offset + 4] = M_Byte4(listLBA);
                                        dataBuf[offset + 5] = M_Byte5(listLBA);
                                        dataBuf[offset + 6] = M_Byte6(listLBA);
                                        dataBuf[offset + 7] = M_Byte7(listLBA);
                                    }
                                    else
                                    {
                                        dataBuf[offset + 0] = M_Byte0(listLBA);
                                        dataBuf[offset + 1] = M_Byte1(listLBA);
                                        dataBuf[offset + 2] = M_Byte2(listLBA);
                                        dataBuf[offset + 3] = M_Byte3(listLBA);
                                    }
                                }
                                //update list length
                                reassignListLength = offset - 4;//minus 4 to get just length of list minus the parameter header
                                if (longLBA)
                                {
                                    dataBuf[0] = M_Byte3(reassignListLength);
                                    dataBuf[1] = M_Byte2(reassignListLength);
                                }
                                dataBuf[2] = M_Byte1(reassignListLength);
                                dataBuf[3] = M_Byte0(reassignListLength);
                                reassignListLength += 4;//add the 4 back in now before we come back around and reissue the command
                            }
                        }
                    }
                    else
                    {
                        done = true;
                    }
                    ++counter;
                } while (!done && counter < 5);
                if (ret == SUCCESS)
                {
                    ret = verify_LBA(device, LBA->errorAddress, logicalPerPhysical);
                }
            }
        }
    }
    safe_Free(dataBuf);
    switch (ret)
    {
    case SUCCESS:
        LBA->repairStatus = REPAIRED;
        break;
    case FAILURE:
        LBA->repairStatus = REPAIR_FAILED;
        break;
    case PERMISSION_DENIED:
        LBA->repairStatus = UNABLE_TO_REPAIR_ACCESS_DENIED;
        break;
    default:
        LBA->repairStatus = NOT_REPAIRED;
        break;
    }
    if (VERBOSITY_QUIET < device->deviceVerbosity)
    {
        printf("...");
        if (ret == SUCCESS)
        {
            printf("repaired!\n");
        }
        else if (ret == PERMISSION_DENIED)
        {
            printf("access denied!\n");
        }
        else
        {
            printf("failed!\n");
        }
    }
    bool emulationActive = is_Sector_Size_Emulation_Active(device);
    if (ret == PERMISSION_DENIED && !forcePassthroughCommand && device->drive_info.interface_type != IDE_INTERFACE && device->drive_info.drive_type == ATA_DRIVE && !emulationActive)
    {
        //We are going to call this function recursively to try it again forcing ATA passthrough
        if (VERBOSITY_QUIET < device->deviceVerbosity)
        {
            printf("\tAttempting SAT ATA Pass-through command for repair...\n");
        }
        ret = repair_LBA(device, LBA, true, automaticWriteReallocationEnabled, automaticReadReallocationEnabled);
    }
    return ret;
}

void print_LBA_Error_List(ptrErrorLBA const LBAs, uint16_t numberOfErrors)
{
    //need to print out a list of the LBAs and their status
    printf("                            Bad LBAs                            \n");
    printf("Defect Number          Defect LBA                            Repair Status\n");
    uint64_t errorIter = 1;
    bool showAccessDeniedNote = false;
    for (errorIter = 1; errorIter <= numberOfErrors; errorIter++)
    {
        char* repairString = NULL;
        switch (LBAs[errorIter - 1].repairStatus)
        {
        case REPAIRED:
            repairString = "Repaired";
            break;
        case REPAIR_FAILED:
            repairString = "Repair Failed";
            break;
        case REPAIR_NOT_REQUIRED:
            repairString = "Repair Not Required";
            break;
        case UNABLE_TO_REPAIR_ACCESS_DENIED:
            showAccessDeniedNote = true;
            repairString = "Access Denied";
            break;
        case NOT_REPAIRED:
        default:
            repairString = "Not Repaired";
            break;
        }
        printf("%5"PRIu64"                  %-20"PRIu64"       %19s\n", errorIter, LBAs[errorIter - 1].errorAddress, repairString);
    }
    if (showAccessDeniedNote)
    {
        printf("\nNOTE: Some LBAs could not be repaired because access to them was denied.\n");
        printf("This may happen when a secondary drive with a file system installed on\n");
        printf("it is recognized by the current host OS, but the current host doesn't have\n");
        printf("permission to change the contents of the second drive.\n\n");
    }
}

int get_Automatic_Reallocation_Support(tDevice *device, bool *automaticWriteReallocationEnabled, bool *automaticReadReallocationEnabled)
{
    int ret = NOT_SUPPORTED;
    if (automaticReadReallocationEnabled)
    {
        *automaticReadReallocationEnabled = false;
    }
    if (automaticWriteReallocationEnabled)
    {
        *automaticWriteReallocationEnabled = false;
    }
    if (device->drive_info.drive_type == ATA_DRIVE) //this should also catch USB drives
    {
        //ATA always supports automatic write reallocation.
        //ATA does not support automatic read reallocation.
        if (automaticReadReallocationEnabled)
        {
            *automaticReadReallocationEnabled = false;
        }
        if (automaticWriteReallocationEnabled)
        {
            *automaticWriteReallocationEnabled = true;
        }
        ret = SUCCESS;
    }
#if !defined (DISABLE_NVME_PASSTHROUGH)
    else if (device->drive_info.drive_type == NVME_DRIVE)
    {
        if (automaticReadReallocationEnabled)
        {
            *automaticReadReallocationEnabled = true;
        }
        if (automaticWriteReallocationEnabled)
        {
            *automaticWriteReallocationEnabled = true;
        }
        ret = SUCCESS;
    }
#endif
    else
    {
        //Assume it's SCSI and read the read-write error recovery mode page
        bool readPage = false;
        uint8_t headerLength = MODE_PARAMETER_HEADER_10_LEN;
        uint8_t readWriteErrorRecoveryMP[MP_READ_WRITE_ERROR_RECOVERY_LEN + MODE_PARAMETER_HEADER_10_LEN] = { 0 };
        if (SUCCESS == scsi_Mode_Sense_10(device, MP_READ_WRITE_ERROR_RECOVERY, MP_READ_WRITE_ERROR_RECOVERY_LEN + MODE_PARAMETER_HEADER_10_LEN, 0, true, false, MPC_CURRENT_VALUES, readWriteErrorRecoveryMP))
        {
            readPage = true;
        }
        else if (SUCCESS == scsi_Mode_Sense_6(device, MP_READ_WRITE_ERROR_RECOVERY, MP_READ_WRITE_ERROR_RECOVERY_LEN + MODE_PARAMETER_HEADER_6_LEN, 0, true, MPC_CURRENT_VALUES, readWriteErrorRecoveryMP))
        {
            readPage = true;
            headerLength = MODE_PARAMETER_HEADER_6_LEN;
        }
        if (readPage)
        {
            if (M_GETBITRANGE(readWriteErrorRecoveryMP[headerLength + 0], 5, 0) == MP_READ_WRITE_ERROR_RECOVERY && readWriteErrorRecoveryMP[headerLength + 1] == 0x0A)
            {
                ret = SUCCESS;
                //we have the right page, so we can get the bits
                if (automaticReadReallocationEnabled)
                {
                    if (readWriteErrorRecoveryMP[headerLength + 2] & BIT7)
                    {
                        *automaticReadReallocationEnabled = true;
                    }
                }
                if (automaticWriteReallocationEnabled)
                {
                    if (readWriteErrorRecoveryMP[headerLength + 2] & BIT6)
                    {
                        *automaticWriteReallocationEnabled = true;
                    }
                }
            }
        }
    }
    return ret;
}

int errorLBACompare(const void *a, const void *b)
{
    ptrErrorLBA lba1 = (ptrErrorLBA)a;
    ptrErrorLBA lba2 = (ptrErrorLBA)b;
    if (lba1->errorAddress < lba2->errorAddress)
    {
        return -1;
    }
    if (lba1->errorAddress == lba2->errorAddress)
    {
        return 0;
    }
    if (lba1->errorAddress > lba2->errorAddress)
    {
        return 1;
    }
    return 0;
}

void sort_Error_LBA_List(ptrErrorLBA LBAList, uint32_t *numberOfLBAsInTheList)
{
    if (!LBAList || !numberOfLBAsInTheList)
    {
        return;
    }
    //TODO: Implement a faster iterative merge sort that removes duplicates on the fly. Right now this should be ok. - TJE
    //http://stackoverflow.com/questions/18924792/sort-and-remove-duplicates-from-int-array-in-c
    if (*numberOfLBAsInTheList > 1)
    {
        uint32_t duplicatesDetected = 0;
        //Sort the list.
        qsort(LBAList, *numberOfLBAsInTheList, sizeof(errorLBA), errorLBACompare);
        //Remove duplicates and update the number of items in the list (local var only). This should be easy since we've already sorted the list
        uint64_t tempLBA = LBAList[0].errorAddress;
        for (uint32_t iter = 1; iter < *numberOfLBAsInTheList - 1; ++iter)
        {
            if (LBAList[iter].errorAddress == tempLBA)
            {
                ++duplicatesDetected;
                LBAList[iter].errorAddress = UINT64_MAX;
            }
            else
            {
                tempLBA = LBAList[iter].errorAddress;
            }
        }
        if (duplicatesDetected > 0)
        {
            //Sort the list one more time.
            qsort(LBAList, *numberOfLBAsInTheList, sizeof(errorLBA), errorLBACompare);
            //set number of LBAs in the list
            (*numberOfLBAsInTheList) -= duplicatesDetected;
        }
    }
}

bool is_LBA_Already_In_The_List(ptrErrorLBA LBAList, uint32_t numberOfLBAsInTheList, uint64_t lba)
{
    bool inList = false;
    if (!LBAList)
    {
        return inList;
    }
    for (uint32_t begin = 0, end = numberOfLBAsInTheList; begin < numberOfLBAsInTheList && end > 0; ++begin, --end)
    {
        if (lba == LBAList[begin].errorAddress || lba == LBAList[end].errorAddress)
        {
            inList = true;
            break;
        }
    }
    return inList;
}

uint32_t find_LBA_Entry_In_List(ptrErrorLBA LBAList, uint32_t numberOfLBAsInTheList, uint64_t lba)
{
    uint32_t index = UINT32_MAX;//something invalid
    if (!LBAList)
    {
        return index;
    }
    for (uint32_t begin = 0, end = numberOfLBAsInTheList; begin < numberOfLBAsInTheList && end > 0; ++begin, --end)
    {
        if (lba == LBAList[begin].errorAddress)
        {
            index = begin;
            break;
        }
        else if (lba == LBAList[end].errorAddress)
        {
            index = end;
            break;
        }
    }
    return index;
}