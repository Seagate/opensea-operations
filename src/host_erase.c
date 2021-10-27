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
// \file host_erase.c
// \brief This file defines the function for performing a host based erase functions (host issues a series of write commands)

#include "operations_Common.h"
#include "operations.h"
#include "host_erase.h"
#include "cmds.h"
#include "platform_helper.h"

int erase_Range(tDevice *device, uint64_t eraseRangeStart, uint64_t eraseRangeEnd, uint8_t *pattern, uint32_t patternLength, bool hideLBACounter)
{
    int ret = SUCCESS;
    uint32_t sectors = get_Sector_Count_For_Read_Write(device);
    uint64_t iter = 0;
    uint32_t dataLength = sectors * device->drive_info.deviceBlockSize;
    uint64_t alignedLBA = align_LBA(device, eraseRangeStart);
    uint8_t *writeBuffer = C_CAST(uint8_t*, calloc_aligned(dataLength, sizeof(uint8_t), device->os_info.minimumAlignment));
    if (writeBuffer == NULL)
    {
        perror("calloc failure! Write Buffer - erase range");
        return MEMORY_FAILURE;
    }
    if (VERBOSITY_QUIET < device->deviceVerbosity)
    {
        printf("\n");
    }
    os_Lock_Device(device);
    if (eraseRangeStart != alignedLBA)
    {
        uint64_t adjustmentAmount = eraseRangeStart - alignedLBA;
        //read the LBA, modify ONLY the data the user wants to erase, then write it to the drive.
        if (SUCCESS == read_LBA(device, alignedLBA, false, writeBuffer, dataLength))
        {
            if (alignedLBA + sectors > eraseRangeEnd)
            {
                sectors = C_CAST(uint16_t, eraseRangeEnd - alignedLBA);
                dataLength = sectors * device->drive_info.deviceBlockSize;
            }
            //set the pattern, or clear the buffer at the LBA the user requested
            uint32_t adjustmentBytes = C_CAST(uint32_t, adjustmentAmount * device->drive_info.deviceBlockSize);
            if (pattern)
            {
                fill_Pattern_Buffer_Into_Another_Buffer(pattern, patternLength, &writeBuffer[adjustmentBytes], dataLength - adjustmentBytes);
            }
            else
            {
                memset(&writeBuffer[adjustmentBytes], 0, dataLength - adjustmentBytes);
            }
            if (VERBOSITY_QUIET < device->deviceVerbosity && !hideLBACounter)
            {
                printf("\rWriting LBA: %-20"PRIu64" (aligned write)", alignedLBA);
                fflush(stdout);
            }
            ret = write_LBA(device, alignedLBA, false, writeBuffer, dataLength);
            if (alignedLBA == 0)
            {
                //update the filesystem cache after writing the boot partition sectors so that no other LBA writes have permission errors - TJE
                os_Update_File_System_Cache(device);
            }
            eraseRangeStart -= adjustmentAmount;
            eraseRangeStart += sectors;
        }
    }
    if (pattern)
    {
        fill_Pattern_Buffer_Into_Another_Buffer(pattern, patternLength, writeBuffer, dataLength);
    }
    if (ret == SUCCESS)
    {
        for (iter = eraseRangeStart; iter < eraseRangeEnd; iter += sectors)
        {
            if (iter + sectors > eraseRangeEnd)
            {
                if (iter + sectors > device->drive_info.deviceMaxLba)
                {
                    sectors = C_CAST(uint16_t, eraseRangeEnd - iter);
                    dataLength = sectors * device->drive_info.deviceBlockSize;
                }
                else //we aren't going to the end of the drive and may need to read the nearby data to keep anything the user didn't want to overwrite
                {
                    if (SUCCESS == read_LBA(device, iter, false, writeBuffer, dataLength))
                    {
                        //modify only the LBAs we want to overwrite
                        if (pattern)
                        {
                            fill_Pattern_Buffer_Into_Another_Buffer(pattern, patternLength, writeBuffer, C_CAST(uint32_t, (eraseRangeEnd - iter) * device->drive_info.deviceBlockSize));
                        }
                        else
                        {
                            memset(writeBuffer, 0, C_CAST(uint32_t, (eraseRangeEnd - iter) * device->drive_info.deviceBlockSize));
                        }
                    }
                }
            }
            if (VERBOSITY_QUIET < device->deviceVerbosity &&!hideLBACounter)
            {
                printf("\rWriting LBA: %-40"PRIu64"", iter);
                fflush(stdout);
            }
            ret = write_LBA(device, iter, false, writeBuffer, dataLength);
            if (SUCCESS != ret)
            {
                ret = FAILURE;
                break;
            }
            if (iter == 0)
            {
                //update the filesystem cache after writing the boot partition sectors so that no other LBA writes have permission errors - TJE
                os_Update_File_System_Cache(device);
            }
        }
        if (VERBOSITY_QUIET < device->deviceVerbosity && FAILURE != ret && !hideLBACounter)
        {
            if (eraseRangeEnd > device->drive_info.deviceMaxLba)
            {
                printf("\rWriting LBA: %-40"PRIu64"", device->drive_info.deviceMaxLba);
            }
            else
            {
                printf("\rWriting LBA: %-40"PRIu64"", eraseRangeEnd - 1);
            }
            fflush(stdout);
        }
    }
    flush_Cache(device);
    if (VERBOSITY_QUIET < device->deviceVerbosity)
    {
        printf("\n");
    }
    safe_Free_aligned(writeBuffer)
    os_Unlock_Device(device);
    os_Update_File_System_Cache(device);
    return ret;
}

int erase_Time(tDevice *device, uint64_t eraseStartLBA, time_t eraseTime, uint8_t *pattern, uint32_t patternLength, bool hideLBACounter)
{
    int ret = UNKNOWN;
    time_t currentTime = 0, startTime = 0;
    //first figure out how many writes we'll need to issue, then allocate the memory we need
    uint32_t sectors = get_Sector_Count_For_Read_Write(device);
    uint64_t iter = 0;
    uint32_t dataLength = sectors * device->drive_info.deviceBlockSize;
    uint64_t alignedLBA = align_LBA(device, eraseStartLBA);
    uint8_t *writeBuffer = C_CAST(uint8_t*, calloc_aligned(dataLength, sizeof(uint8_t), device->os_info.minimumAlignment));
    if (writeBuffer == NULL)
    {
        perror("calloc failure! Write Buffer - erase time");
        return MEMORY_FAILURE;
    }
    if (device->drive_info.deviceMaxLba == 0)
    {
        safe_Free(writeBuffer)
        return NOT_SUPPORTED;
    }
    if (VERBOSITY_QUIET < device->deviceVerbosity)
    {
        printf("\n");
    }
    time(&currentTime);//get the current time before starting the loop
    startTime = currentTime;
    os_Lock_Device(device);
    if (eraseStartLBA != alignedLBA)
    {
        uint64_t adjustmentAmount = eraseStartLBA - alignedLBA;
        //read the LBA, modify ONLY the data the user wants to erase, then write it to the drive.
        if (SUCCESS == read_LBA(device, alignedLBA, false, writeBuffer, dataLength))
        {
            if (alignedLBA + sectors > device->drive_info.deviceMaxLba)
            {
                sectors = C_CAST(uint16_t, device->drive_info.deviceMaxLba - alignedLBA);
                dataLength = sectors * device->drive_info.deviceBlockSize;
            }
            //set the pattern, or clear the buffer at the LBA the user requested
            uint32_t adjustmentBytes = C_CAST(uint32_t, adjustmentAmount * device->drive_info.deviceBlockSize);
            if (pattern)
            {
                fill_Pattern_Buffer_Into_Another_Buffer(pattern, patternLength, &writeBuffer[adjustmentBytes], dataLength - adjustmentBytes);
            }
            else
            {
                memset(&writeBuffer[adjustmentBytes], 0, dataLength - adjustmentBytes);
            }
            if (VERBOSITY_QUIET < device->deviceVerbosity && !hideLBACounter)
            {
                printf("\rWriting LBA: %-20"PRIu64" (aligned write)", alignedLBA);
                fflush(stdout);
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
    if (pattern)
    {
        fill_Pattern_Buffer_Into_Another_Buffer(pattern, patternLength, writeBuffer, dataLength);
    }
    for (iter = eraseStartLBA; difftime(currentTime, startTime) < eraseTime; iter += sectors, time(&currentTime))
    {
        if (iter + sectors > device->drive_info.deviceMaxLba)
        {
            sectors = C_CAST(uint16_t, device->drive_info.deviceMaxLba - iter);
            dataLength = sectors * device->drive_info.deviceBlockSize;
        }
        if (VERBOSITY_QUIET < device->deviceVerbosity &&!hideLBACounter)
        {
            printf("\rWriting LBA: %-40"PRIu64"", iter);
            fflush(stdout);
        }
        ret = write_LBA(device, iter, false, writeBuffer, dataLength);
        if (SUCCESS != ret)
        {
            ret = FAILURE;
            break;
        }
        if (iter == 0)
        {
            //update the filesystem cache after writing the boot partition sectors so that no other LBA writes have permission errors - TJE
            os_Update_File_System_Cache(device);
        }
        if (iter + sectors >= device->drive_info.deviceMaxLba)
        {
            //reset the sector count back to what it was and set iter back to 0
            iter = 0;
            sectors = get_Sector_Count_For_Read_Write(device);
        }
    }
    flush_Cache(device);
    if (VERBOSITY_QUIET < device->deviceVerbosity)
    {
        printf("\n");
    }
    safe_Free_aligned(writeBuffer)
    os_Unlock_Device(device);
    os_Update_File_System_Cache(device);
    return ret;
}
