// SPDX-License-Identifier: MPL-2.0
//
// Do NOT modify or remove this copyright and license
//
// Copyright (c) 2012-2026 Seagate Technology LLC and/or its Affiliates, All Rights Reserved
//
// This software is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
//
// ******************************************************************************************
//
// \file host_erase.c
// \brief This file defines the function for performing a host based erase functions (host issues a series of write
// commands)

#include "bit_manip.h"
#include "code_attributes.h"
#include "common_types.h"
#include "error_translation.h"
#include "io_utils.h"
#include "math_utils.h"
#include "memory_safety.h"
#include "pattern_utils.h"
#include "precision_timer.h"
#include "string_utils.h"
#include "type_conversion.h"

#include "cmds.h"
#include "host_erase.h"
#include "operations.h"
#include "operations_Common.h"
#include "platform_helper.h"

M_PARAM_RO(1)
M_NONNULL_IF_NONZERO_PARAM(4, 5)
M_PARAM_RO_SIZE(4, 5)
OPENSEA_OPERATIONS_API eReturnValues erase_Range(const tDevice* M_NONNULL device,
                                                 uint64_t                 eraseRangeStart,
                                                 uint64_t                 eraseRangeEnd,
                                                 uint8_t* M_NULLABLE      pattern,
                                                 uint32_t                 patternLength,
                                                 bool                     hideLBACounter)
{
    eReturnValues ret         = SUCCESS;
    uint32_t      sectors     = get_Sector_Count_For_Read_Write(device);
    uint64_t      iter        = UINT64_C(0);
    uint32_t      dataLength  = sectors * get_Device_BlockSize(device);
    uint64_t      alignedLBA  = align_LBA(device, eraseRangeStart);
    uint8_t*      writeBuffer = M_REINTERPRET_CAST(
        uint8_t*, safe_calloc_aligned(dataLength, sizeof(uint8_t), get_Device_IO_Minimum_Alignment(device)));
    if (writeBuffer == M_NULLPTR)
    {
        perror("calloc failure! Write Buffer - erase range");
        return MEMORY_FAILURE;
    }
    if (VERBOSITY_QUIET < device->deviceVerbosity)
    {
        print_str("\n");
    }
    os_Lock_Device(device);
    if (eraseRangeStart == UINT64_C(0))
    {
        // only unmount when we are touching boot sectors!
        os_Unmount_File_Systems_On_Device(device);
        if ((eraseRangeStart + eraseRangeEnd) >= return_Device_MaxLba(device))
        {
            // At least in WIndows, you MIGHT get a permissions issue trying to write LBA 0 and maxlba.
            // So if this erase is erasing the whole drive, do this first to make sure we use a low-level
            // IOCTL to do this. Once that completes it should be possible to erase the drive without an error.
            if (SUCCESS == os_Erase_Boot_Sectors(device))
            {
                flush_Cache(device); // in case the OS call didn't flush the writes, make sure we flush them
                os_Update_File_System_Cache(device); // tell the OS to update what it knows about the disk (that the
                                                     // partition table is gone now basically)
            }
        }
    }
    if (eraseRangeStart != alignedLBA)
    {
        uint64_t adjustmentAmount = eraseRangeStart - alignedLBA;
        // read the LBA, modify ONLY the data the user wants to erase, then write it to the drive.
        if (SUCCESS == read_LBA(device, alignedLBA, false, writeBuffer, dataLength))
        {
            if (alignedLBA + sectors > eraseRangeEnd)
            {
                sectors    = C_CAST(uint16_t, eraseRangeEnd - alignedLBA);
                dataLength = sectors * get_Device_BlockSize(device);
            }
            // set the pattern, or clear the buffer at the LBA the user requested
            uint32_t adjustmentBytes = C_CAST(uint32_t, adjustmentAmount* get_Device_BlockSize(device));
            if (pattern != M_NULLPTR)
            {
                fill_Pattern_Buffer_Into_Another_Buffer(pattern, patternLength, &writeBuffer[adjustmentBytes],
                                                        dataLength - adjustmentBytes);
            }
            else
            {
                if (0 != safe_memset(&writeBuffer[adjustmentBytes], dataLength - adjustmentBytes, 0,
                                     dataLength - adjustmentBytes))
                {
                    perror("Error clearing buffer for erase");
                    safe_free_aligned(&writeBuffer);
                    return MEMORY_FAILURE;
                }
            }
            if (VERBOSITY_QUIET < device->deviceVerbosity && !hideLBACounter)
            {
                printf("\rWriting LBA: %-20" PRIu64 " (aligned write)", alignedLBA);
                flush_stdout();
            }
            ret = write_LBA(device, alignedLBA, false, writeBuffer, dataLength);
            if (alignedLBA == 0)
            {
                // update the filesystem cache after writing the boot partition sectors so that no other LBA writes have
                // permission errors - TJE
                os_Update_File_System_Cache(device);
            }
            eraseRangeStart -= adjustmentAmount;
            eraseRangeStart += sectors;
        }
    }
    if (pattern != M_NULLPTR)
    {
        fill_Pattern_Buffer_Into_Another_Buffer(pattern, patternLength, writeBuffer, dataLength);
    }
    if (ret == SUCCESS)
    {
        for (iter = eraseRangeStart; iter < eraseRangeEnd; iter += sectors)
        {
            if (iter + sectors > eraseRangeEnd)
            {
                if (iter + sectors > return_Device_MaxLba(device))
                {
                    sectors    = C_CAST(uint16_t, eraseRangeEnd - iter);
                    dataLength = sectors * get_Device_BlockSize(device);
                }
                else // we aren't going to the end of the drive and may need to read the nearby data to keep anything
                     // the user didn't want to overwrite
                {
                    if (SUCCESS == read_LBA(device, iter, false, writeBuffer, dataLength))
                    {
                        // modify only the LBAs we want to overwrite
                        if (pattern != M_NULLPTR)
                        {
                            fill_Pattern_Buffer_Into_Another_Buffer(
                                pattern, patternLength, writeBuffer,
                                C_CAST(uint32_t, (eraseRangeEnd - iter) * get_Device_BlockSize(device)));
                        }
                        else
                        {
                            if (0 !=
                                safe_memset(writeBuffer, dataLength, 0,
                                            C_CAST(uint32_t, (eraseRangeEnd - iter) * get_Device_BlockSize(device))))
                            {
                                perror("Error clearing buffer for erase");
                                safe_free_aligned(&writeBuffer);
                                return MEMORY_FAILURE;
                            }
                        }
                    }
                }
            }
            if (VERBOSITY_QUIET < device->deviceVerbosity && !hideLBACounter)
            {
                printf("\rWriting LBA: %-40" PRIu64 "", iter);
                flush_stdout();
            }
            ret = write_LBA(device, iter, false, writeBuffer, dataLength);
            if (SUCCESS != ret)
            {
                ret = FAILURE;
                break;
            }
            if (iter == 0)
            {
                // update the filesystem cache after writing the boot partition sectors so that no other LBA writes have
                // permission errors - TJE
                os_Update_File_System_Cache(device);
            }
        }
        if (VERBOSITY_QUIET < device->deviceVerbosity && FAILURE != ret && !hideLBACounter)
        {
            if (eraseRangeEnd > return_Device_MaxLba(device))
            {
                printf("\rWriting LBA: %-40" PRIu64 "", return_Device_MaxLba(device));
            }
            else
            {
                printf("\rWriting LBA: %-40" PRIu64 "", eraseRangeEnd - 1);
            }
            flush_stdout();
        }
    }
    flush_Cache(device);
    if (VERBOSITY_QUIET < device->deviceVerbosity)
    {
        print_str("\n");
    }
    safe_free_aligned(&writeBuffer);
    os_Unlock_Device(device);
    os_Update_File_System_Cache(device);
    return ret;
}

M_PARAM_RO(1)
M_NONNULL_IF_NONZERO_PARAM(4, 5)
M_PARAM_RO_SIZE(4, 5)
OPENSEA_OPERATIONS_API eReturnValues erase_Time(const tDevice* M_NONNULL device,
                                                uint64_t                 eraseStartLBA,
                                                uint64_t                 eraseTime,
                                                uint8_t* M_NULLABLE      pattern,
                                                uint32_t                 patternLength,
                                                bool                     hideLBACounter)
{
    eReturnValues ret         = UNKNOWN;
    time_t        currentTime = 0;
    time_t        startTime   = 0;
    // first figure out how many writes we'll need to issue, then allocate the memory we need
    uint32_t sectors     = get_Sector_Count_For_Read_Write(device);
    uint64_t iter        = UINT64_C(0);
    uint32_t dataLength  = sectors * get_Device_BlockSize(device);
    uint64_t alignedLBA  = align_LBA(device, eraseStartLBA);
    uint8_t* writeBuffer = M_REINTERPRET_CAST(
        uint8_t*, safe_calloc_aligned(dataLength, sizeof(uint8_t), get_Device_IO_Minimum_Alignment(device)));
    if (writeBuffer == M_NULLPTR)
    {
        perror("calloc failure! Write Buffer - erase time");
        return MEMORY_FAILURE;
    }
    if (return_Device_MaxLba(device) == 0)
    {
        safe_free(&writeBuffer);
        return NOT_SUPPORTED;
    }
    if (VERBOSITY_QUIET < device->deviceVerbosity)
    {
        print_str("\n");
    }
    currentTime = time(M_NULLPTR); // get the current time before starting the loop
    startTime   = currentTime;
    os_Lock_Device(device);
    if (eraseStartLBA == 0)
    {
        // only unmount when we are touching boot sectors!
        os_Unmount_File_Systems_On_Device(device);
    }
    if (eraseStartLBA != alignedLBA)
    {
        uint64_t adjustmentAmount = eraseStartLBA - alignedLBA;
        // read the LBA, modify ONLY the data the user wants to erase, then write it to the drive.
        if (SUCCESS == read_LBA(device, alignedLBA, false, writeBuffer, dataLength))
        {
            if (alignedLBA + sectors > return_Device_MaxLba(device))
            {
                sectors    = C_CAST(uint16_t, return_Device_MaxLba(device) - alignedLBA);
                dataLength = sectors * get_Device_BlockSize(device);
            }
            // set the pattern, or clear the buffer at the LBA the user requested
            uint32_t adjustmentBytes = C_CAST(uint32_t, adjustmentAmount* get_Device_BlockSize(device));
            if (pattern != M_NULLPTR)
            {
                fill_Pattern_Buffer_Into_Another_Buffer(pattern, patternLength, &writeBuffer[adjustmentBytes],
                                                        dataLength - adjustmentBytes);
            }
            else
            {
                if (0 != safe_memset(&writeBuffer[adjustmentBytes], dataLength - adjustmentBytes, 0,
                                     dataLength - adjustmentBytes))
                {
                    perror("Error clearing buffer for erase");
                    safe_free_aligned(&writeBuffer);
                    return MEMORY_FAILURE;
                }
            }
            if (VERBOSITY_QUIET < device->deviceVerbosity && !hideLBACounter)
            {
                printf("\rWriting LBA: %-20" PRIu64 " (aligned write)", alignedLBA);
                flush_stdout();
            }
            ret = write_LBA(device, alignedLBA, false, writeBuffer, dataLength);
            eraseStartLBA -= adjustmentAmount;
            eraseStartLBA += sectors;
            if (alignedLBA == 0)
            {
                os_Update_File_System_Cache(device);
            }
        }
    }
    if (pattern != M_NULLPTR)
    {
        fill_Pattern_Buffer_Into_Another_Buffer(pattern, patternLength, writeBuffer, dataLength);
    }
    for (iter = eraseStartLBA; C_CAST(uint64_t, difftime(currentTime, startTime)) < eraseTime;
         iter += sectors, currentTime = time(M_NULLPTR))
    {
        if (iter + sectors > return_Device_MaxLba(device))
        {
            sectors    = C_CAST(uint16_t, return_Device_MaxLba(device) - iter);
            dataLength = sectors * get_Device_BlockSize(device);
        }
        if (VERBOSITY_QUIET < device->deviceVerbosity && !hideLBACounter)
        {
            printf("\rWriting LBA: %-40" PRIu64 "", iter);
            flush_stdout();
        }
        ret = write_LBA(device, iter, false, writeBuffer, dataLength);
        if (SUCCESS != ret)
        {
            ret = FAILURE;
            break;
        }
        if (iter == 0)
        {
            // update the filesystem cache after writing the boot partition sectors so that no other LBA writes have
            // permission errors - TJE
            os_Update_File_System_Cache(device);
        }
        if (iter + sectors >= return_Device_MaxLba(device))
        {
            // reset the sector count back to what it was and set iter back to 0
            iter    = 0;
            sectors = get_Sector_Count_For_Read_Write(device);
        }
    }
    flush_Cache(device);
    if (VERBOSITY_QUIET < device->deviceVerbosity)
    {
        print_str("\n");
    }
    safe_free_aligned(&writeBuffer);
    os_Unlock_Device(device);
    os_Update_File_System_Cache(device);
    return ret;
}

// This erases the first 32KiB and last 32 KiB of the drive.
M_PARAM_RO(1) OPENSEA_OPERATIONS_API eReturnValues erase_Boot_Sectors(const tDevice* M_NONNULL device)
{
    eReturnValues ret         = SUCCESS;
    uint32_t      sectors     = get_Sector_Count_For_Read_Write(device);
    uint64_t      iter        = UINT64_C(0);
    uint32_t      dataLength  = sectors * get_Device_BlockSize(device);
    uint8_t*      writeBuffer = M_REINTERPRET_CAST(
        uint8_t*, safe_calloc_aligned(dataLength, sizeof(uint8_t), get_Device_IO_Minimum_Alignment(device)));
    if (writeBuffer == M_NULLPTR)
    {
        perror("calloc failure! Write Buffer - erase range");
        return MEMORY_FAILURE;
    }
    if (VERBOSITY_QUIET < device->deviceVerbosity)
    {
        print_str("\n");
    }
    os_Lock_Device(device);
    os_Unmount_File_Systems_On_Device(device);

    // Try this first. Currently in Windows this is needed for some devices as raw writes return permision denied
    ret = os_Erase_Boot_Sectors(device);
    if (ret == SUCCESS)
    {
        os_Update_File_System_Cache(device);
    }
    // even if the OS erase boot sectors succeeds, issue the following writes anyways to make sure it really did erase
    // everything.
    if (VERBOSITY_QUIET < device->deviceVerbosity)
    {
        printf("\rWriting LBA: %-40" PRIu64 "", iter);
        flush_stdout();
    }
    // NOTE: this currently only issues 2 writes counting on the sector count being either enough for 32KiB or 64KiB.
    // This is far from perfect, but will work for now.-TJE write sector zero
    ret = write_LBA(device, iter, false, writeBuffer, dataLength);
    if (ret == SUCCESS)
    {
        // write max LBA only if LBA 0 wrote successfully
        ret = write_LBA(device, return_Device_MaxLba(device) - iter, false, writeBuffer, dataLength);
    }
    flush_Cache(device);
    if (VERBOSITY_QUIET < device->deviceVerbosity)
    {
        print_str("\n");
    }
    safe_free_aligned(&writeBuffer);
    os_Unlock_Device(device);
    os_Update_File_System_Cache(device);
    return ret;
}
