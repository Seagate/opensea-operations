// SPDX-License-Identifier: MPL-2.0
//
// Do NOT modify or remove this copyright and license
//
// Copyright (c) 2012-2025 Seagate Technology LLC and/or its Affiliates, All Rights Reserved
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

typedef enum {
    REASSIGN_LBA_ADD_TO_LIST,
    REASSIGN_LBA_REMOVE_ALL_BEFORE_LBA_VALUE
} eReassignLBAOperation;

static M_INLINE uint32_t get_Current_Reassign_List_Length(const uint8_t* reassignList, bool longLBA)
{
    if (longLBA)
    {
        return M_BytesTo4ByteValue(reassignList[0], reassignList[1], reassignList[2], reassignList[3]);
    }
    else
    {
        return M_BytesTo2ByteValue(reassignList[2], reassignList[3]);
    }
}

M_NONNULL_PARAM_LIST(1, 2)
static eReturnValues convert_LBA_Reassign_List_To_LongLBA(uint8_t **reassignList, uint32_t* listLength, size_t listAlignment)
{
    eReturnValues ret = SUCCESS;
    uint32_t      currentListLength = get_Current_Reassign_List_Length(*reassignList, false);
    if (currentListLength == 0)
    {
        return ret; // nothing to do as the list is still empty
    }
    else
    {
        uint32_t      newListLength = ((currentListLength / REASSIGN_BLOCKS_SHORT_LBA_LENGTH) * REASSIGN_BLOCKS_LONG_LBA_LENGTH) + REASSIGN_BLOCKS_LIST_HEADER_LENGTH;
        uint8_t*      temp = safe_reallocf_aligned(M_REINTERPRET_CAST(void**, reassignList), *listLength, newListLength, listAlignment);
        if (temp == M_NULLPTR)
        {
            return MEMORY_FAILURE;
        }
        *reassignList = temp;
        *listLength = newListLength;
        // expand each LBA from 4 bytes to 8 bytes in the buffer. Prepend 0's to the upper 4 bytes.
        for (uint32_t currentOffset = REASSIGN_BLOCKS_LIST_HEADER_LENGTH + (currentListLength - 4), newOffset = newListLength - REASSIGN_BLOCKS_LONG_LBA_LENGTH; ;
             currentOffset -= REASSIGN_BLOCKS_SHORT_LBA_LENGTH, newOffset -= REASSIGN_BLOCKS_LONG_LBA_LENGTH)
        {
            // use currentListLength since it is still the old length in bytes to find the end of the list.
            // Then use that LBA value to write into the new location at the end of the reallocated buffer.
            // Move backwards through the list to avoid overwriting data we still need to read.
            (*reassignList)[newOffset + 0] = 0x00;
            (*reassignList)[newOffset + 1] = 0x00;
            (*reassignList)[newOffset + 2] = 0x00;
            (*reassignList)[newOffset + 3] = 0x00;
            (*reassignList)[newOffset + 4] = (*reassignList)[currentOffset + 0];
            (*reassignList)[newOffset + 5] = (*reassignList)[currentOffset + 1];
            (*reassignList)[newOffset + 6] = (*reassignList)[currentOffset + 2];
            (*reassignList)[newOffset + 7] = (*reassignList)[currentOffset + 3];
            if (currentOffset <= REASSIGN_BLOCKS_LIST_HEADER_LENGTH)
            {
                break;
            }
        }
        // update header to long LBA mode
        currentListLength = (currentListLength / REASSIGN_BLOCKS_SHORT_LBA_LENGTH) * REASSIGN_BLOCKS_LONG_LBA_LENGTH;// convert count from 4 byte LBAs to 8 byte LBAs
        (*reassignList)[0] = M_Byte3(currentListLength);
        (*reassignList)[1] = M_Byte2(currentListLength);
        (*reassignList)[2] = M_Byte1(currentListLength);
        (*reassignList)[3] = M_Byte0(currentListLength);
    }
    return ret;
}

// Adds an LBA to the reassign list buffer
// First detects how many LBAs are in it, then adds the new one to the list in the correct place since they must be in order.
// TODO: Handle case where current list is 32bit LBAs and adding a 64bit LBA to it so longLBA mode is changed.
M_NONNULL_PARAM_LIST(1, 2)
static eReturnValues update_LBA_Reassign_List(uint8_t **reassignList, uint32_t* listLength, size_t listAlignment, uint64_t lba, eReassignLBAOperation operation, bool *longLBA, uint16_t logicalPerPhysical)
{
    eReturnValues ret = SUCCESS;
    uint8_t       increment = *longLBA ? REASSIGN_BLOCKS_LONG_LBA_LENGTH : REASSIGN_BLOCKS_SHORT_LBA_LENGTH;
    uint32_t      currentListLength = UINT32_C(0);
    if (*reassignList != M_NULLPTR)
    {
        currentListLength = get_Current_Reassign_List_Length(*reassignList, *longLBA);
    }
    if (lba > UINT32_MAX && *longLBA == false)
    {
        // need to convert existing list to long LBA mode before doing anything else.
        ret = convert_LBA_Reassign_List_To_LongLBA(reassignList, listLength, listAlignment);
        if (ret != SUCCESS)
        {
            return ret;
        }
        *longLBA = true;
    }
    for (uint32_t offset = REASSIGN_BLOCKS_LIST_HEADER_LENGTH; ; offset += increment)
    {
        uint64_t currentLBA = UINT64_C(0);
        if (offset > *listLength)
        {
            uint32_t fullListLength = *listLength;
            // reallocate the list for more memory
            uint8_t *temp = safe_reallocf_aligned(M_REINTERPRET_CAST(void**, reassignList), fullListLength, fullListLength + (C_CAST(uint32_t, logicalPerPhysical) * C_CAST(uint32_t, increment)) + REASSIGN_BLOCKS_LIST_HEADER_LENGTH, listAlignment);
            if (temp == M_NULLPTR)
            {
                return MEMORY_FAILURE;
            }
            *reassignList = temp;
            *listLength += (C_CAST(uint32_t, logicalPerPhysical) * C_CAST(uint32_t, increment)) + REASSIGN_BLOCKS_LIST_HEADER_LENGTH;
            // zero out new memory
            safe_memset(&(*reassignList)[fullListLength], *listLength - fullListLength, 0, *listLength - fullListLength);
        }
        // First read current LBA in the list, then determine where we need to place the new one/remove it from the list
        if (*longLBA)
        {
            currentLBA = M_BytesTo8ByteValue(
                (*reassignList)[offset + 0], (*reassignList)[offset + 1], (*reassignList)[offset + 2], (*reassignList)[offset + 3],
                (*reassignList)[offset + 4], (*reassignList)[offset + 5], (*reassignList)[offset + 6], (*reassignList)[offset + 7]);
        }
        else
        {
            currentLBA = M_BytesTo4ByteValue(
                (*reassignList)[offset + 0], (*reassignList)[offset + 1], (*reassignList)[offset + 2], (*reassignList)[offset + 3]);
        }
        if (operation == REASSIGN_LBA_ADD_TO_LIST)
        {
            if (offset >= currentListLength + REASSIGN_BLOCKS_LIST_HEADER_LENGTH || lba < currentLBA)
            {
                if (!(offset >= currentListLength + REASSIGN_BLOCKS_LIST_HEADER_LENGTH))
                {
                    // move the list for an insertion
                    uint32_t bytesToMove = currentListLength + REASSIGN_BLOCKS_LIST_HEADER_LENGTH - offset;
                    safe_memmove(&(*reassignList)[offset + increment], *listLength - (offset + increment), &(*reassignList)[offset], bytesToMove);
                }
                // offset is at an empty location, so add the LBA here
                if (*longLBA)
                {
                    (*reassignList)[offset + 0] = M_Byte7(lba);
                    (*reassignList)[offset + 1] = M_Byte6(lba);
                    (*reassignList)[offset + 2] = M_Byte5(lba);
                    (*reassignList)[offset + 3] = M_Byte4(lba);
                    (*reassignList)[offset + 4] = M_Byte3(lba);
                    (*reassignList)[offset + 5] = M_Byte2(lba);
                    (*reassignList)[offset + 6] = M_Byte1(lba);
                    (*reassignList)[offset + 7] = M_Byte0(lba);
                }
                else
                {
                    (*reassignList)[offset + 0] = M_Byte3(lba);
                    (*reassignList)[offset + 1] = M_Byte2(lba);
                    (*reassignList)[offset + 2] = M_Byte1(lba);
                    (*reassignList)[offset + 3] = M_Byte0(lba);
                }
                currentListLength += increment;
                break;
            }
            if (lba > currentLBA)
            {
                // go to next entry in the list
                continue;
            }
        }
        else if (operation == REASSIGN_LBA_REMOVE_ALL_BEFORE_LBA_VALUE)
        {
            if (lba > currentLBA)
            {
                // need to remove this LBA from the list (shift existing list to overwrite this, then zero out end)
                uint32_t bytesToMove = currentListLength + REASSIGN_BLOCKS_LIST_HEADER_LENGTH - (offset + increment);
                safe_memmove(&(*reassignList)[offset], *listLength - offset, &(*reassignList)[offset + increment], bytesToMove);
                // zero out the end of the list that was just moved up
                safe_memset(&(*reassignList)[*listLength - increment], increment, 0, increment);
                currentListLength -= increment;
                continue; // go to next entry in the list and figure out if it needs removing or not.
            }
            else
            {
                // we are done removing LBAs
                break;
            }
        }
        break;
    }
    // Update the list length header after adding/removing LBAs above.
    (*reassignList)[0] = M_Byte3(currentListLength);
    (*reassignList)[1] = M_Byte2(currentListLength);
    (*reassignList)[2] = M_Byte1(currentListLength);
    (*reassignList)[3] = M_Byte0(currentListLength);
    return ret;
}

static M_INLINE bool is_Valid_Reassign_LBA(uint64_t lba, uint64_t maxLba, bool reportedFixedFormatSense)
{
    bool valid = true;
    if (lba > maxLba)
    {
        valid = false;
    }
    else
    {
        if (reportedFixedFormatSense)
        {
            // fixed format sense data cannot report LBAs beyond 0xFFFFFFFF
            if (lba >= UINT32_MAX)
            {
                valid = false;
            }
        }
        else
        {
            // descriptor format sense data cannot report LBAs beyond 0xFFFFFFFFFFFF
            if (lba >= UINT64_MAX)
            {
                valid = false;
            }
        }
    }
    return valid;
}

eReturnValues reallocate_LBAs(const tDevice* device, ptrErrorLBA lbaList, uint32_t lbaListLength)
{
    eReturnValues ret       = SUCCESS;
    bool          longLBA   = false;
    uint32_t      listIndex = UINT32_C(0);
    uint8_t       increment = REASSIGN_BLOCKS_SHORT_LBA_LENGTH;
    uint16_t      logicalPerPhysical =
        C_CAST(uint16_t, device->drive_info.devicePhyBlockSize / device->drive_info.deviceBlockSize);
    if (lbaListLength == 0 || lbaList == M_NULLPTR)
    {
        return BAD_PARAMETER;
    }
    // Check if any of the LBAs are beyond 32bit range to set long LBA mode
    for (listIndex = UINT32_C(0); listIndex < lbaListLength; ++listIndex)
    {
        if (lbaList[listIndex].errorAddress > UINT32_MAX)
        {
            longLBA = true;
            increment = REASSIGN_BLOCKS_LONG_LBA_LENGTH;
            break;
        }
    }
    uint32_t reassignListLength =
        (C_CAST(uint32_t, logicalPerPhysical) * C_CAST(uint32_t, increment)) + REASSIGN_BLOCKS_LIST_HEADER_LENGTH;
    uint32_t dataSize = device->drive_info.deviceBlockSize * logicalPerPhysical;
    uint8_t* dataBuf =
        M_REINTERPRET_CAST(uint8_t*, safe_calloc_aligned(dataSize, sizeof(uint8_t), device->os_info.minimumAlignment));
    if (dataBuf == M_NULLPTR)
    {
        return MEMORY_FAILURE;
    }
    // build the list of LBAs to reassign
    for (listIndex = UINT32_C(0); listIndex < lbaListLength; ++listIndex)
    {
        uint64_t reassignLBA  = lbaList[listIndex].errorAddress;
        // create the list of LBAs. 1 for 1 logical per physical, 8 for 8 logical per physical
        ret = update_LBA_Reassign_List(&dataBuf, &reassignListLength, device->os_info.minimumAlignment,
                                reassignLBA, REASSIGN_LBA_ADD_TO_LIST, &longLBA, logicalPerPhysical);
    }
    if (ret != SUCCESS)
    {
        safe_free_aligned(&dataBuf);
        return ret;
    }
    bool    done    = false;
    uint8_t counter = UINT8_C(0);
    uint8_t maxRetries = UINT8_C(5) * logicalPerPhysical;
    do
    {
        bool longList = false;
        if ((reassignListLength - REASSIGN_BLOCKS_LIST_HEADER_LENGTH) > UINT16_MAX)
        {
            longList = true;
        }
        // always using short list since we are doing single reallocations at a time...not using enough data
        // to need a long list.
        ret = scsi_Reassign_Blocks(device, longLBA, longList, reassignListLength, dataBuf);
        // Need to check and make sure that we didn't get a check condition
        if (is_Invalid_Opcode(device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN))
        {
            // Device does not support the reassign blocks command.
            ret = NOT_SUPPORTED;
            done = true;
            break;
        }
        else if (is_Invalid_Field_In_CDB(device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN))
        {
            // TODO: Can possibly check if long/short LBA or long/short list is the issue and retry.
            ret = NOT_SUPPORTED;
            done = true;
            break;
        }
        else if (is_LBA_Out_Of_Range(device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN))
        {
            // LBA is out of range, so we cannot reassign it.
            // Note, this may also be invalid field in parameter list.
            // That would need more evaluation to determine which one it is.
            ret = BAD_PARAMETER;
            done = true;
            break;
        }
        else if (is_HW_Error_No_Defect_Spare_Available(device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN))
        {
            // Hardware error and no defect spare available, so we cannot reassign it.
            ret = FAILURE;
            done = true;
            break;
        }
        else
        {
            senseDataFields senseFields;
            safe_memset(&senseFields, sizeof(senseDataFields), 0, sizeof(senseDataFields));
            get_Sense_Data_Fields(device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, &senseFields);
            if (senseFields.validStructure)
            {

                if (senseFields.scsiStatusCodes.format != 0 &&
                    senseFields.scsiStatusCodes.senseKey != SENSE_KEY_ILLEGAL_REQUEST &&
                    senseFields.scsiStatusCodes.senseKey != SENSE_KEY_HARDWARE_ERROR &&
                    senseFields.scsiStatusCodes.senseKey != SENSE_KEY_NO_ERROR)
                {
                    uint64_t commandSpecificLba = senseFields.fixedFormat ? senseFields.fixedCommandSpecificInformation :
                                                                        senseFields.descriptorCommandSpecificInformation;
                    done = false;
                    // if we have a valid LBA, then we need to remove all LBAs prior to that one and reissue the
                    // command.
                    if (is_Valid_Reassign_LBA(commandSpecificLba, device->drive_info.deviceMaxLba, senseFields.fixedFormat))
                    {
                        ret = update_LBA_Reassign_List(&dataBuf, &reassignListLength, device->os_info.minimumAlignment,
                                                commandSpecificLba, REASSIGN_LBA_REMOVE_ALL_BEFORE_LBA_VALUE, &longLBA, logicalPerPhysical);
                        if (ret != SUCCESS)
                        {
                            safe_free_aligned(&dataBuf);
                            return ret;
                        }
                    }
                    else
                    {
                        ret = FAILURE;
                        done = true;
                    }
                }
                else
                {
                    done = true;
                }
                if (senseFields.scsiStatusCodes.senseKey == SENSE_KEY_MEDIUM_ERROR)
                {
                    uint64_t informationLba = senseFields.fixedFormat ? senseFields.fixedInformation :
                                                                    senseFields.descriptorInformation;
                    done = false;
                    // if valid, add it to the list and reissue the command
                    if (is_Valid_Reassign_LBA(informationLba, device->drive_info.deviceMaxLba, senseFields.fixedFormat))
                    {
                        update_LBA_Reassign_List(&dataBuf, &reassignListLength, device->os_info.minimumAlignment,
                                                informationLba, REASSIGN_LBA_ADD_TO_LIST, &longLBA, logicalPerPhysical);
                    }
                    else
                    {
                        done = true;
                    }
                }
                else
                {
                    done = true;
                }
            }
            else
            {
                done = true;
            }
        }
        ++counter;
    } while (!done && counter < maxRetries);
    safe_free_aligned(&dataBuf);
    if (ret == SUCCESS)
    {
        for (listIndex = UINT32_C(0); listIndex < lbaListLength && ret == SUCCESS; ++listIndex)
        {
            // verify each LBA was successfully reallocated
            ret = verify_LBA(device, lbaList[listIndex].errorAddress, logicalPerPhysical);
            if (ret == SUCCESS)
            {
                lbaList[listIndex].repairStatus = REPAIRED;
            }
            else if (ret == PERMISSION_DENIED)
            {
                lbaList[listIndex].repairStatus = UNABLE_TO_REPAIR_ACCESS_DENIED;
            }
            else
            {
                lbaList[listIndex].repairStatus = REPAIR_FAILED;
            }
        }
    }
    return ret;
}

eReturnValues repair_LBA(const tDevice*    device,
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
        ret = FAILURE; // assume failure for now.
        if (automaticReadReallocationEnabled)
        {
            // Attempt a read reallocation to preserve the user's data
            ret = read_LBA(device, LBA->errorAddress, false, dataBuf, dataSize);
            if (ret == SUCCESS)
            {
                ret = verify_LBA(device, LBA->errorAddress, logicalPerPhysical);
            }
        }
        if (automaticWriteReallocationEnabled && ret != SUCCESS)
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
        // Try sending reassign blocks last since this will increase g-list count.
        // Do not do this with NVMe since no translation for this command has been defined.
        if (ret != SUCCESS && device->drive_info.drive_type != NVME_DRIVE)
        {
            // need to use the reallocate command (ATA interfaces should attempt translating it through SAT)
            ret = reallocate_LBAs(device, LBA, 1);
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
        print_str("...");
        if (ret == SUCCESS)
        {
            print_str("repaired!\n");
        }
        else if (ret == PERMISSION_DENIED)
        {
            print_str("access denied!\n");
        }
        else
        {
            print_str("failed!\n");
        }
    }
    bool emulationActive = is_Sector_Size_Emulation_Active(device);
    if (ret == PERMISSION_DENIED && !forcePassthroughCommand && device->drive_info.interface_type != IDE_INTERFACE &&
        device->drive_info.drive_type == ATA_DRIVE && !emulationActive)
    {
        // We are going to call this function recursively to try it again forcing ATA passthrough
        if (VERBOSITY_QUIET < device->deviceVerbosity)
        {
            print_str("\tAttempting SAT ATA Pass-through command for repair...\n");
        }
        ret = repair_LBA(device, LBA, true, automaticWriteReallocationEnabled, automaticReadReallocationEnabled);
    }
    return ret;
}

void print_LBA_Error_List(constPtrErrorLBA LBAs, uint16_t numberOfErrors)
{
    // need to print out a list of the LBAs and their status
    print_str("                            Bad LBAs                            \n");
    print_str("Defect Number          Defect LBA                            Repair Status\n");
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
        print_str("\nNOTE: Some LBAs could not be repaired because access to them was denied.\n");
        print_str("This may happen when a secondary drive with a file system installed on\n");
        print_str("it is recognized by the current host OS, but the current host doesn't have\n");
        print_str("permission to change the contents of the second drive.\n\n");
    }
}

eReturnValues get_Automatic_Reallocation_Support(const tDevice* device,
                                                 bool*          automaticWriteReallocationEnabled,
                                                 bool*          automaticReadReallocationEnabled)
{
    eReturnValues ret = NOT_SUPPORTED;
    DISABLE_NONNULL_COMPARE
    if (automaticReadReallocationEnabled != M_NULLPTR)
    {
        *automaticReadReallocationEnabled = false;
    }
    if (automaticWriteReallocationEnabled != M_NULLPTR)
    {
        *automaticWriteReallocationEnabled = false;
    }
    RESTORE_NONNULL_COMPARE
    if (device->drive_info.drive_type == ATA_DRIVE) // this should also catch USB drives
    {
        // ATA always supports automatic write reallocation.
        // ATA does not support automatic read reallocation.
        DISABLE_NONNULL_COMPARE
        if (automaticReadReallocationEnabled != M_NULLPTR)
        {
            *automaticReadReallocationEnabled = false;
        }
        if (automaticWriteReallocationEnabled != M_NULLPTR)
        {
            *automaticWriteReallocationEnabled = true;
        }
        RESTORE_NONNULL_COMPARE
        ret = SUCCESS;
    }
    else if (device->drive_info.drive_type == NVME_DRIVE)
    {
        DISABLE_NONNULL_COMPARE
        if (automaticReadReallocationEnabled != M_NULLPTR)
        {
            *automaticReadReallocationEnabled = true;
        }
        if (automaticWriteReallocationEnabled != M_NULLPTR)
        {
            *automaticWriteReallocationEnabled = true;
        }
        RESTORE_NONNULL_COMPARE
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
                DISABLE_NONNULL_COMPARE
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
                RESTORE_NONNULL_COMPARE
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
    DISABLE_NONNULL_COMPARE
    if (LBAList == M_NULLPTR || numberOfLBAsInTheList == M_NULLPTR)
    {
        return;
    }
    RESTORE_NONNULL_COMPARE
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
    DISABLE_NONNULL_COMPARE
    if (LBAList == M_NULLPTR)
    {
        return inList;
    }
    RESTORE_NONNULL_COMPARE
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
    DISABLE_NONNULL_COMPARE
    if (LBAList == M_NULLPTR)
    {
        return index;
    }
    RESTORE_NONNULL_COMPARE
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
