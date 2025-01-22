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
// \file sector_repair.c

#include "bit_manip.h"
#include "code_attributes.h"
#include "common_types.h"
#include "error_translation.h"
#include "io_utils.h"
#include "math_utils.h"
#include "memory_safety.h"
#include "precision_timer.h"
#include "sort_and_search.h"
#include "string_utils.h"
#include "type_conversion.h"

#include "cmds.h"
#include "sector_repair.h"

eReturnValues repair_LBA(tDevice*    device,
                         ptrErrorLBA LBA,
                         bool        forcePassthroughCommand,
                         bool        automaticWriteReallocationEnabled,
                         bool        automaticReadReallocationEnabled)
{
    eReturnValues ret = UNKNOWN;
    uint16_t      logicalPerPhysical =
        C_CAST(uint16_t, device->drive_info.devicePhyBlockSize / device->drive_info.deviceBlockSize);
    uint32_t dataSize = device->drive_info.deviceBlockSize * logicalPerPhysical;
    uint8_t* dataBuf =
        M_REINTERPRET_CAST(uint8_t*, safe_calloc_aligned(dataSize, sizeof(uint8_t), device->os_info.minimumAlignment));
    if (dataBuf == M_NULLPTR)
    {
        return MEMORY_FAILURE;
    }
    LBA->errorAddress = align_LBA(device, LBA->errorAddress);
    LBA->repairStatus = NOT_REPAIRED;
    if (VERBOSITY_QUIET < device->deviceVerbosity)
    {
        printf("\n\tAttempting repair on LBA %" PRIu64 " (aligned)", LBA->errorAddress);
    }
    if (forcePassthroughCommand &&
        (device->drive_info.drive_type == ATA_DRIVE || device->drive_info.drive_type == ATAPI_DRIVE))
    {
        if (device->drive_info.interface_type != IDE_INTERFACE)
        {
            // need to use child drive info for write
            uint8_t* temp      = M_NULLPTR;
            logicalPerPhysical = C_CAST(uint16_t, device->drive_info.bridge_info.childDevicePhyBlockSize /
                                                      device->drive_info.bridge_info.childDeviceBlockSize);
            dataSize           = device->drive_info.bridge_info.childDeviceBlockSize * logicalPerPhysical;
            temp = M_REINTERPRET_CAST(uint8_t*, safe_realloc_aligned(dataBuf, 0, dataSize * sizeof(uint8_t),
                                                                     device->os_info.minimumAlignment));
            if (temp == M_NULLPTR)
            {
                safe_free_aligned(&dataBuf);
                return MEMORY_FAILURE;
            }
            dataBuf = temp;
            safe_memset(dataBuf, dataSize, 0, dataSize);
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
        eReturnValues readReallocation = FAILURE; // assume failure
        if (automaticReadReallocationEnabled)
        {
            // Attempt a read reallocation to preserve the user's data
            ret              = read_LBA(device, LBA->errorAddress, false, dataBuf, dataSize);
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
            // write reallocation is not allowed, but try to write it anyways...it may reduce a reallocation.
            // If a write succeeds, we're done and don't need to reallocate.
            // If a write fails, then send the reassign blocks command since the sector needs reassignment
            ret = write_LBA(device, LBA->errorAddress, false, dataBuf, dataSize);
            if (ret == SUCCESS)
            {
                ret = flush_Cache(device);
                if (ret == SUCCESS)
                {
                    ret = verify_LBA(device, LBA->errorAddress, logicalPerPhysical);
                }
            }
            if (ret != SUCCESS &&
                device->drive_info.drive_type !=
                    NVME_DRIVE) // make sure the write and verify did actually work! NOTE: NVMe does not have a reassign
                                // command or translation for it in translation spec
            {
                // need to use the reallocate command (SCSI...ATA interfaces should attempt translating it through SAT)
                bool    longLBA   = false;
                uint8_t increment = UINT8_C(4);
                if (LBA->errorAddress + (logicalPerPhysical - UINT16_C(1)) > UINT32_MAX)
                {
                    longLBA   = true;
                    increment = UINT8_C(8);
                }
                uint32_t reassignListLength = (C_CAST(uint32_t, logicalPerPhysical) * C_CAST(uint32_t, increment)) +
                                              UINT32_C(4); //+ 4 is parameter header
                // set up the header
                dataBuf[2]           = M_Byte1(logicalPerPhysical * M_STATIC_CAST(uint16_t, increment));
                dataBuf[3]           = M_Byte0(logicalPerPhysical * M_STATIC_CAST(uint16_t, increment));
                uint64_t reassignLBA = LBA->errorAddress;
                uint32_t offset      = UINT32_C(4);
                uint32_t iter        = UINT32_C(0);
                // create the list of LBAs. 1 for 1 logical per physical, 8 for 8 logical per physical
                for (iter = UINT32_C(0), offset = UINT32_C(4); iter < logicalPerPhysical;
                     ++iter, offset += increment, ++reassignLBA)
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
                bool    done    = false;
                uint8_t counter = UINT8_C(0);
                do
                {
                    // always using short list since we are doing single reallocations at a time...not using enough data
                    // to need a long list.
                    ret = scsi_Reassign_Blocks(device, longLBA, false, reassignListLength, dataBuf);
                    // Need to check and make sure that we didn't get a check condition
                    senseDataFields senseFields;
                    safe_memset(&senseFields, sizeof(senseDataFields), 0, sizeof(senseDataFields));
                    get_Sense_Data_Fields(device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, &senseFields);
                    if (senseFields.validStructure)
                    {
                        bool     updateList         = false;
                        uint64_t commandSpecificLba = UINT64_MAX;
                        uint64_t informationLba     = UINT64_MAX;
                        if (senseFields.scsiStatusCodes.format != 0 &&
                            senseFields.scsiStatusCodes.senseKey != SENSE_KEY_ILLEGAL_REQUEST &&
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
                            // if we have a valid LBA, then we need to remove all LBAs prior to that one and reissue the
                            // command.
                            if (commandSpecificLba < device->drive_info.deviceMaxLba)
                            {
                                updateList = true;
                            }
                            else
                            {
                                commandSpecificLba = UINT64_MAX;
                                done               = true;
                            }
                        }
                        else
                        {
                            done = true;
                        }
                        if (senseFields.scsiStatusCodes.senseKey == SENSE_KEY_MEDIUM_ERROR)
                        {
                            done = false;
                            // check information field for a valid LBA.
                            if (senseFields.fixedFormat)
                            {
                                informationLba = senseFields.fixedInformation;
                            }
                            else
                            {
                                informationLba = senseFields.descriptorInformation;
                            }
                            // if valid, add it to the list and reissue the command
                            if (informationLba < device->drive_info.deviceMaxLba)
                            {
                                updateList = true;
                            }
                            else
                            {
                                informationLba = UINT64_MAX;
                                done           = true;
                            }
                        }
                        else
                        {
                            done = true;
                        }
                        if (updateList)
                        {
                            // we got at least one update to do to the list.
                            // Check both the LBAs we saved above since it's not clear if both conditions can happen at
                            // the same time or not. Most likely only one or the other though...
                            if (commandSpecificLba != UINT64_MAX)
                            {
                                // update the list to remove LBAs before this one
                                reassignLBA = LBA->errorAddress;
                                for (iter = 0, offset = 4; iter < C_CAST(uint32_t, logicalPerPhysical);
                                     ++iter, ++reassignLBA)
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
                                // update list length
                                reassignListLength =
                                    offset - 4; // minus 4 to get just length of list minus the parameter header
                                if (longLBA)
                                {
                                    dataBuf[0] = M_Byte3(reassignListLength);
                                    dataBuf[1] = M_Byte2(reassignListLength);
                                }
                                dataBuf[2] = M_Byte1(reassignListLength);
                                dataBuf[3] = M_Byte0(reassignListLength);
                                reassignListLength +=
                                    4; // add the 4 back in now before we come back around and reissue the command
                            }
                            if (informationLba != UINT64_MAX)
                            {
                                // add this LBA to the list to be reassigned
                                reassignLBA       = LBA->errorAddress;
                                bool infoLBAAdded = false;
                                for (iter = 0, offset = 4; iter < (C_CAST(uint32_t, logicalPerPhysical) + UINT32_C(1));
                                     ++iter, offset += increment)
                                {
                                    uint64_t listLBA = reassignLBA;
                                    if (!infoLBAAdded && informationLba < reassignLBA)
                                    {
                                        listLBA      = informationLba;
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
                                // update list length
                                reassignListLength =
                                    offset - 4; // minus 4 to get just length of list minus the parameter header
                                if (longLBA)
                                {
                                    dataBuf[0] = M_Byte3(reassignListLength);
                                    dataBuf[1] = M_Byte2(reassignListLength);
                                }
                                dataBuf[2] = M_Byte1(reassignListLength);
                                dataBuf[3] = M_Byte0(reassignListLength);
                                reassignListLength +=
                                    4; // add the 4 back in now before we come back around and reissue the command
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
    safe_free_aligned(&dataBuf);
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
    if (ret == PERMISSION_DENIED && !forcePassthroughCommand && device->drive_info.interface_type != IDE_INTERFACE &&
        device->drive_info.drive_type == ATA_DRIVE && !emulationActive)
    {
        // We are going to call this function recursively to try it again forcing ATA passthrough
        if (VERBOSITY_QUIET < device->deviceVerbosity)
        {
            printf("\tAttempting SAT ATA Pass-through command for repair...\n");
        }
        ret = repair_LBA(device, LBA, true, automaticWriteReallocationEnabled, automaticReadReallocationEnabled);
    }
    return ret;
}

void print_LBA_Error_List(constPtrErrorLBA LBAs, uint16_t numberOfErrors)
{
    // need to print out a list of the LBAs and their status
    printf("                            Bad LBAs                            \n");
    printf("Defect Number          Defect LBA                            Repair Status\n");
    uint64_t errorIter            = UINT64_C(1);
    bool     showAccessDeniedNote = false;
    for (errorIter = 1; errorIter <= numberOfErrors; errorIter++)
    {
        const char* repairString = M_NULLPTR;
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
            repairString         = "Access Denied";
            break;
        case NOT_REPAIRED:
        default:
            repairString = "Not Repaired";
            break;
        }
        printf("%5" PRIu64 "                  %-20" PRIu64 "       %19s\n", errorIter, LBAs[errorIter - 1].errorAddress,
               repairString);
    }
    if (showAccessDeniedNote)
    {
        printf("\nNOTE: Some LBAs could not be repaired because access to them was denied.\n");
        printf("This may happen when a secondary drive with a file system installed on\n");
        printf("it is recognized by the current host OS, but the current host doesn't have\n");
        printf("permission to change the contents of the second drive.\n\n");
    }
}

eReturnValues get_Automatic_Reallocation_Support(tDevice* device,
                                                 bool*    automaticWriteReallocationEnabled,
                                                 bool*    automaticReadReallocationEnabled)
{
    eReturnValues ret = NOT_SUPPORTED;
    if (automaticReadReallocationEnabled != M_NULLPTR)
    {
        *automaticReadReallocationEnabled = false;
    }
    if (automaticWriteReallocationEnabled != M_NULLPTR)
    {
        *automaticWriteReallocationEnabled = false;
    }
    if (device->drive_info.drive_type == ATA_DRIVE) // this should also catch USB drives
    {
        // ATA always supports automatic write reallocation.
        // ATA does not support automatic read reallocation.
        if (automaticReadReallocationEnabled != M_NULLPTR)
        {
            *automaticReadReallocationEnabled = false;
        }
        if (automaticWriteReallocationEnabled != M_NULLPTR)
        {
            *automaticWriteReallocationEnabled = true;
        }
        ret = SUCCESS;
    }
    else if (device->drive_info.drive_type == NVME_DRIVE)
    {
        if (automaticReadReallocationEnabled != M_NULLPTR)
        {
            *automaticReadReallocationEnabled = true;
        }
        if (automaticWriteReallocationEnabled != M_NULLPTR)
        {
            *automaticWriteReallocationEnabled = true;
        }
        ret = SUCCESS;
    }
    else
    {
        // Assume it's SCSI and read the read-write error recovery mode page
        bool    readPage     = false;
        uint8_t headerLength = MODE_PARAMETER_HEADER_10_LEN;
        DECLARE_ZERO_INIT_ARRAY(uint8_t, readWriteErrorRecoveryMP,
                                MP_READ_WRITE_ERROR_RECOVERY_LEN + MODE_PARAMETER_HEADER_10_LEN);
        if (SUCCESS == scsi_Mode_Sense_10(device, MP_READ_WRITE_ERROR_RECOVERY,
                                          MP_READ_WRITE_ERROR_RECOVERY_LEN + MODE_PARAMETER_HEADER_10_LEN, 0, true,
                                          false, MPC_CURRENT_VALUES, readWriteErrorRecoveryMP))
        {
            readPage = true;
        }
        else if (SUCCESS == scsi_Mode_Sense_6(device, MP_READ_WRITE_ERROR_RECOVERY,
                                              MP_READ_WRITE_ERROR_RECOVERY_LEN + MODE_PARAMETER_HEADER_6_LEN, 0, true,
                                              MPC_CURRENT_VALUES, readWriteErrorRecoveryMP))
        {
            readPage     = true;
            headerLength = MODE_PARAMETER_HEADER_6_LEN;
        }
        if (readPage)
        {
            if (get_bit_range_uint8(readWriteErrorRecoveryMP[headerLength + 0], 5, 0) == MP_READ_WRITE_ERROR_RECOVERY &&
                readWriteErrorRecoveryMP[headerLength + 1] == 0x0A)
            {
                ret = SUCCESS;
                // we have the right page, so we can get the bits
                if (automaticReadReallocationEnabled != M_NULLPTR)
                {
                    if (readWriteErrorRecoveryMP[headerLength + 2] & BIT7)
                    {
                        *automaticReadReallocationEnabled = true;
                    }
                }
                if (automaticWriteReallocationEnabled != M_NULLPTR)
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

static int errorLBACompare(const void* a, const void* b)
{
    const errorLBA* lba1 = a;
    const errorLBA* lba2 = b;
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

void sort_Error_LBA_List(ptrErrorLBA LBAList, uint32_t* numberOfLBAsInTheList)
{
    if (!LBAList || !numberOfLBAsInTheList)
    {
        return;
    }
    if (*numberOfLBAsInTheList > UINT32_C(1))
    {
        uint32_t duplicatesDetected = UINT32_C(0);
        // Sort the list.
        safe_qsort(LBAList, *numberOfLBAsInTheList, sizeof(errorLBA), errorLBACompare);
        // Remove duplicates and update the number of items in the list (local var only). This should be easy since
        // we've already sorted the list
        uint64_t tempLBA = LBAList[0].errorAddress;
        for (uint32_t iter = UINT32_C(1); iter < *numberOfLBAsInTheList - UINT32_C(1); ++iter)
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
        if (duplicatesDetected > UINT32_C(0))
        {
            // Sort the list one more time.
            safe_qsort(LBAList, *numberOfLBAsInTheList, sizeof(errorLBA), errorLBACompare);
            // set number of LBAs in the list
            (*numberOfLBAsInTheList) -= duplicatesDetected;
        }
    }
}

bool is_LBA_Already_In_The_List(ptrErrorLBA LBAList, uint32_t numberOfLBAsInTheList, uint64_t lba)
{
    bool inList = false;
    if (LBAList == M_NULLPTR)
    {
        return inList;
    }
    for (uint32_t begin = UINT32_C(0), end = numberOfLBAsInTheList; begin < numberOfLBAsInTheList && end > UINT32_C(0);
         ++begin, --end)
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
    uint32_t index = UINT32_MAX; // something invalid
    if (LBAList == M_NULLPTR)
    {
        return index;
    }
    for (uint32_t begin = UINT32_C(0), end = numberOfLBAsInTheList; begin < numberOfLBAsInTheList && end > UINT32_C(0);
         ++begin, --end)
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
