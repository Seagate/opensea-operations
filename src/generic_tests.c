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
// \file generic_tests.c
// \brief This file defines the functions for generic read tests

#include "common.h"
#include "generic_tests.h"
#include "sector_repair.h"
#include "cmds.h"
#include "operations.h"

int read_Write_Seek_Command(tDevice *device, eRWVCommandType rwvCommand, uint64_t lba, uint8_t *ptrData, uint32_t dataSize)
{
    switch (rwvCommand)
    {
    case RWV_COMMAND_READ:
        return read_LBA(device, lba, false, ptrData, dataSize);
    case RWV_COMMAND_WRITE:
        return write_LBA(device, lba, false, ptrData, dataSize);
    case RWV_COMMAND_VERIFY:
    default:
        return verify_LBA(device, lba, dataSize / device->drive_info.deviceBlockSize);
    }
}

int sequential_RWV(tDevice *device, eRWVCommandType rwvCommand, uint64_t startingLBA, uint64_t range, uint64_t sectorCount, uint64_t *failingLBA, M_ATTR_UNUSED custom_Update updateFunction, M_ATTR_UNUSED void *updateData, bool hideLBACounter)
{
    int ret = SUCCESS;
    uint64_t lbaIter = startingLBA;
    uint64_t maxSequentialLBA = startingLBA + range;
    if (maxSequentialLBA >= device->drive_info.deviceMaxLba)
    {
        maxSequentialLBA = device->drive_info.deviceMaxLba + 1;//the plus 1 here should make sure we don't go beyond the max lba
    }
    uint8_t *dataBuf = NULL;
    if (rwvCommand != RWV_COMMAND_VERIFY)
    {
        dataBuf = C_CAST(uint8_t*, calloc_aligned(C_CAST(size_t, sectorCount * device->drive_info.deviceBlockSize), sizeof(uint8_t), device->os_info.minimumAlignment));
        if (!dataBuf)
        {
            return MEMORY_FAILURE;
        }
    }
    if (maxSequentialLBA < startingLBA)
    {
        return BAD_PARAMETER;
    }
    *failingLBA = UINT64_MAX;//this means LBA access failed
    for (lbaIter = startingLBA; lbaIter < maxSequentialLBA; lbaIter += sectorCount)
    {
        //check that current LBA + sector count doesn't go beyond the maxLBA for the loop
        if ((lbaIter + sectorCount) > maxSequentialLBA)
        {
            uint8_t *temp = NULL;
            //adjust the sector count to fit
            sectorCount = maxSequentialLBA - lbaIter;
            if (rwvCommand != RWV_COMMAND_VERIFY)
            {
                //reallocate the memory to be sized appropriately for this change
                temp = C_CAST(uint8_t*, realloc_aligned(dataBuf, 0, C_CAST(size_t, sectorCount * device->drive_info.deviceBlockSize), device->os_info.minimumAlignment));
                if (!temp)
                {
                    perror("memory reallocation failure");
                    return MEMORY_FAILURE;
                }
                dataBuf = temp;
                memset(dataBuf, 0, C_CAST(size_t, sectorCount * device->drive_info.deviceBlockSize * sizeof(uint8_t)));
            }
        }
        //print out the current LBA we are rwving
        if (VERBOSITY_QUIET < device->deviceVerbosity && !hideLBACounter)
        {
            switch (rwvCommand)
            {
            case RWV_COMMAND_WRITE: 
                printf("\rWriting LBA: %-20"PRIu64"", lbaIter);//20 wide is the max width for a unsigned 64bit number
                break;
            case RWV_COMMAND_READ:
                printf("\rReading LBA: %-20"PRIu64"", lbaIter);//20 wide is the max width for a unsigned 64bit number
                break;
            case RWV_COMMAND_VERIFY:
            default:
                printf("\rVerifying LBA: %-20"PRIu64"", lbaIter);//20 wide is the max width for a unsigned 64bit number
                break;
            }
            fflush(stdout);
        }
        //rwv the lba
        if (SUCCESS != read_Write_Seek_Command(device, rwvCommand, lbaIter, dataBuf, C_CAST(uint32_t, sectorCount * device->drive_info.deviceBlockSize)))
        {
            bool errorFound = false;
            uint64_t maxSingleLoopLBA = lbaIter + sectorCount;//limits the loop to trying to only a certain number of sectors without getting stuck at single LBA reads.
            //read command failure...so we need to read until we find the exact failing lba
            for (; lbaIter <= maxSingleLoopLBA; lbaIter += 1)
            {
                //print out the current LBA we are rwving
                if (VERBOSITY_QUIET < device->deviceVerbosity && !hideLBACounter)
                {
                    switch (rwvCommand)
                    {
                    case RWV_COMMAND_WRITE: 
                        printf("\rWriting LBA: %-20"PRIu64"", lbaIter);//20 wide is the max width for a unsigned 64bit number
                        break;
                    case RWV_COMMAND_READ:
                        printf("\rReading LBA: %-20"PRIu64"", lbaIter);//20 wide is the max width for a unsigned 64bit number
                        break;
                    case RWV_COMMAND_VERIFY:
                    default:
                        printf("\rVerifying LBA: %-20"PRIu64"", lbaIter);//20 wide is the max width for a unsigned 64bit number
                        break;
                    }
                    fflush(stdout);
                }
                if (SUCCESS != read_Write_Seek_Command(device, rwvCommand, lbaIter, dataBuf, C_CAST(uint32_t, 1 * device->drive_info.deviceBlockSize)))
                {
                    *failingLBA = lbaIter;
                    ret = FAILURE;
                    errorFound = true;
                    break;
                }
            }
            if (errorFound)
            {
                break;
            }
        }
    }
    //print out the current LBA we are rwving AND it is not greater than MaxLBA
    if (VERBOSITY_QUIET < device->deviceVerbosity && lbaIter < device->drive_info.deviceMaxLba && !hideLBACounter)
    {
        switch (rwvCommand)
        {
        case RWV_COMMAND_WRITE: 
            printf("\rWriting LBA: %-20"PRIu64"", lbaIter);//20 wide is the max width for a unsigned 64bit number
            break;
        case RWV_COMMAND_READ:
            printf("\rReading LBA: %-20"PRIu64"", lbaIter);//20 wide is the max width for a unsigned 64bit number
            break;
        case RWV_COMMAND_VERIFY:
        default:
            printf("\rVerifying LBA: %-20"PRIu64"", lbaIter);//20 wide is the max width for a unsigned 64bit number
            break;
        }
        fflush(stdout);
    }
    safe_Free_aligned(dataBuf)
    return ret;
}

int sequential_Read(tDevice *device, uint64_t startingLBA, uint64_t range, uint64_t sectorCount, uint64_t *failingLBA, custom_Update updateFunction, void *updateData, bool hideLBACounter)
{
    return sequential_RWV(device, RWV_COMMAND_READ, startingLBA, range, sectorCount, failingLBA, updateFunction, updateData, hideLBACounter);
}

int sequential_Write(tDevice *device, uint64_t startingLBA, uint64_t range, uint64_t sectorCount, uint64_t *failingLBA, custom_Update updateFunction, void *updateData, bool hideLBACounter)
{
    return sequential_RWV(device, RWV_COMMAND_WRITE, startingLBA, range, sectorCount, failingLBA, updateFunction, updateData, hideLBACounter);
}

int sequential_Verify(tDevice *device, uint64_t startingLBA, uint64_t range, uint64_t sectorCount, uint64_t *failingLBA, custom_Update updateFunction, void *updateData, bool hideLBACounter)
{
    return sequential_RWV(device, RWV_COMMAND_VERIFY, startingLBA, range, sectorCount, failingLBA, updateFunction, updateData, hideLBACounter);
}

int short_Generic_Read_Test(tDevice *device, custom_Update updateFunction, void *updateData, bool hideLBACounter)
{
    return short_Generic_Test(device, RWV_COMMAND_READ, updateFunction, updateData, hideLBACounter);
}

int short_Generic_Verify_Test(tDevice *device, custom_Update updateFunction, void *updateData, bool hideLBACounter)
{
    return short_Generic_Test(device, RWV_COMMAND_VERIFY, updateFunction, updateData, hideLBACounter);
}

int short_Generic_Write_Test(tDevice *device, custom_Update updateFunction, void *updateData, bool hideLBACounter)
{
    return short_Generic_Test(device, RWV_COMMAND_WRITE, updateFunction, updateData, hideLBACounter);
}

int short_Generic_Test(tDevice *device, eRWVCommandType rwvCommand, M_ATTR_UNUSED custom_Update updateFunction, M_ATTR_UNUSED void *updateData, bool hideLBACounter)
{
    int ret = SUCCESS;
    char message[256] = { 0 };
    uint16_t randomLBACount = 5000;
    uint64_t *randomLBAList = C_CAST(uint64_t*, calloc(randomLBACount, sizeof(uint64_t)));
    uint64_t iterator = 0;
    uint64_t onePercentOfDrive = C_CAST(uint64_t, device->drive_info.deviceMaxLba * 0.01);//calculate how many LBAs are 1% of the drive so that we read that many
    uint8_t *dataBuf = NULL;//will be allocated at the random read section
    uint64_t failingLBA = UINT64_MAX;
    uint32_t sectorCount = get_Sector_Count_For_Read_Write(device);
    if (!randomLBAList)
    {
        perror("Memory allocation failure on random LBA list\n");
        return MEMORY_FAILURE;
    }
    //start random number generator
    seed_64(time(NULL));
    //generate the list of random LBAs
    for (iterator = 0; iterator < randomLBACount; iterator++)
    {
        randomLBAList[iterator] = random_Range_64(0, device->drive_info.deviceMaxLba);
    }
    //read 1% at the OD
    if (device->deviceVerbosity > VERBOSITY_QUIET)
    {
        switch (rwvCommand)
        {
        case RWV_COMMAND_READ:
            snprintf(message, 256, "Sequential Read Test at OD");
            break;
        case RWV_COMMAND_VERIFY:
            snprintf(message, 256, "Sequential Verify Test at OD");
            break;
        case RWV_COMMAND_WRITE:
            snprintf(message, 256, "Sequential Write Test at OD");
            break;
        default:
            snprintf(message, 256, "Unknown Sequential Test at OD");
            break;
        }
        printf("%s for %"PRIu64" LBAs\n", message, onePercentOfDrive);
    }
    if (SUCCESS != sequential_RWV(device, rwvCommand, 0, onePercentOfDrive, sectorCount, &failingLBA, NULL, NULL, hideLBACounter))
    {
        ret = FAILURE;
        if (device->deviceVerbosity > VERBOSITY_QUIET)
        {
            switch (rwvCommand)
            {
            case RWV_COMMAND_READ:
                snprintf(message, 256, "Read failed within OD sequential read");
                break;
            case RWV_COMMAND_VERIFY:
                snprintf(message, 256, "Verify failed within OD sequential read");
                break;
            case RWV_COMMAND_WRITE:
                snprintf(message, 256, "Write failed within OD sequential read");
                break;
            default:
                snprintf(message, 256, "Unknown failed within OD sequential read");
                break;
            }
            printf("\n%s\n",message);
        }
        safe_Free(randomLBAList)
        return ret;
    }
    if (device->deviceVerbosity > VERBOSITY_QUIET)
    {
        printf("\n");
    }
    //read 1% at the ID
    if (device->deviceVerbosity > VERBOSITY_QUIET)
    {
        switch (rwvCommand)
        {
        case RWV_COMMAND_READ:
            snprintf(message, 256, "Sequential Read Test at ID");
            break;
        case RWV_COMMAND_VERIFY:
            snprintf(message, 256, "Sequential Verify Test at ID");
            break;
        case RWV_COMMAND_WRITE:
            snprintf(message, 256, "Sequential Write Test at ID");
            break;
        default:
            snprintf(message, 256, "Unknown Sequential Test at ID");
            break;
        }
        printf("%s for %"PRIu64" LBAs\n", message, onePercentOfDrive);
    }
    if (SUCCESS != sequential_RWV(device, rwvCommand, device->drive_info.deviceMaxLba - onePercentOfDrive, onePercentOfDrive, sectorCount, &failingLBA, NULL, NULL, hideLBACounter))
    {
        ret = FAILURE;
        if (device->deviceVerbosity > VERBOSITY_QUIET)
        {
            switch (rwvCommand)
            {
            case RWV_COMMAND_READ:
                snprintf(message, 256, "Read failed within ID sequential read");
                break;
            case RWV_COMMAND_VERIFY:
                snprintf(message, 256, "Verify failed within ID sequential read");
                break;
            case RWV_COMMAND_WRITE:
                snprintf(message, 256, "Write failed within ID sequential read");
                break;
            default:
                snprintf(message, 256, "Unknown failed within ID sequential read");
                break;
            }
            printf("\n%s\n",message);
        }
        safe_Free(randomLBAList)
        return ret;
    }
    if (device->deviceVerbosity > VERBOSITY_QUIET)
    {
        printf("\n");
    }
    //randomly read 50 LBAs
    if (device->deviceVerbosity > VERBOSITY_QUIET)
    {
        switch (rwvCommand)
        {
        case RWV_COMMAND_READ:
            snprintf(message, 256, "Random Read Test of 5000 LBAs");
            break;
        case RWV_COMMAND_VERIFY:
            snprintf(message, 256, "Random Verify Test of 5000 LBAs");
            break;
        case RWV_COMMAND_WRITE:
            snprintf(message, 256, "Random Write Test of 5000 LBAs");
            break;
        default:
            snprintf(message, 256, "Random Unknown Test of 5000 LBAs");
            break;
        }
        printf("%s\n", message);
    }
    if (rwvCommand != RWV_COMMAND_VERIFY)
    {
        dataBuf = C_CAST(uint8_t*, malloc(device->drive_info.deviceBlockSize * sizeof(uint8_t)));
        if (!dataBuf)
        {
            perror("malloc data buf failed\n");
            safe_Free(randomLBAList)
            return MEMORY_FAILURE;
        }
    }
    for (iterator = 0; iterator < randomLBACount; iterator++)
    {
        //print out the current LBA we are reading
        if (VERBOSITY_QUIET < device->deviceVerbosity && !hideLBACounter)
        {
            switch (rwvCommand)
            {
            case RWV_COMMAND_READ:
                printf("\rReading LBA: %-20" PRIu64 "", randomLBAList[iterator]);//20 wide is the max width for a unsigned 64bit number
                break;
            case RWV_COMMAND_VERIFY:
                printf("\rVerify LBA: %-20" PRIu64 "", randomLBAList[iterator]);//20 wide is the max width for a unsigned 64bit number
                break;
            case RWV_COMMAND_WRITE:
                printf("\rWrite LBA: %-20" PRIu64 "", randomLBAList[iterator]);//20 wide is the max width for a unsigned 64bit number
                break;
            default:
                printf("\rUnknown LBA: %-20" PRIu64 "", randomLBAList[iterator]);//20 wide is the max width for a unsigned 64bit number
                break;
            }
            fflush(stdout);
        }
        if (SUCCESS != read_Write_Seek_Command(device, rwvCommand, randomLBAList[iterator], dataBuf, C_CAST(uint32_t, 1 * device->drive_info.deviceBlockSize)))
        {
            switch (rwvCommand)
            {
            case RWV_COMMAND_READ:
                snprintf(message, 256, "\nRead error occurred at LBA %-20" PRIu64 "", randomLBAList[iterator]);
                break;
            case RWV_COMMAND_VERIFY:
                snprintf(message, 256, "\nVerify error occurred at LBA %-20" PRIu64 "", randomLBAList[iterator]);
                break;
            case RWV_COMMAND_WRITE:
                snprintf(message, 256, "\nWrite error occurred at LBA %-20" PRIu64 "", randomLBAList[iterator]);
                break;
            default:
                snprintf(message, 256, "\nUnknown error occurred at LBA %-20" PRIu64 "", randomLBAList[iterator]);
                break;
            }
            printf("%s\n", message);
            ret = FAILURE;
            break;
        }
    }
    if (device->deviceVerbosity > VERBOSITY_QUIET)
    {
        printf("\n");
    }
    safe_Free(dataBuf)
    safe_Free(randomLBAList)
    return ret;
}

int two_Minute_Generic_Read_Test(tDevice *device, custom_Update updateFunction, void *updateData, bool hideLBACounter)
{
    return two_Minute_Generic_Test(device, RWV_COMMAND_READ, updateFunction, updateData, hideLBACounter);
}

int two_Minute_Generic_Write_Test(tDevice *device, custom_Update updateFunction, void *updateData, bool hideLBACounter)
{
    return two_Minute_Generic_Test(device, RWV_COMMAND_WRITE, updateFunction, updateData, hideLBACounter);
}

int two_Minute_Generic_Verify_Test(tDevice *device, custom_Update updateFunction, void *updateData, bool hideLBACounter)
{
    return two_Minute_Generic_Test(device, RWV_COMMAND_VERIFY, updateFunction, updateData, hideLBACounter);
}

//TODO: Move this to generic_tests.h...also, should this be a separate parameter so some of these functions so the caller can get this data or determine whether or not to show it?
typedef struct _performanceNumbers
{
    bool asyncCommandsUsed;//False for now. Maybe allow async in the future
    uint64_t averageCommandTimeNS;//average command time in nanoseconds
    uint64_t fastestCommandTimeNS;//best case
    uint64_t slowestCommandTimeNS;//worst case
    uint64_t numberOfCommandsIssued;//number of commands issued during operation
    uint64_t totalTimeNS;//total time for an operation
    uint64_t iops;
    uint16_t sectorCount;
}performanceNumbers;

int two_Minute_Generic_Test(tDevice *device, eRWVCommandType rwvCommand, M_ATTR_UNUSED custom_Update updateFunction, M_ATTR_UNUSED void *updateData, bool hideLBACounter)
{
    int ret = SUCCESS;
    bool showPerformanceNumbers = false;//TODO: make this a function parameter.
    size_t dataBufSize = 0;
    uint8_t *dataBuf = NULL;
    uint32_t sectorCount = get_Sector_Count_For_Read_Write(device);
    uint8_t IDODTimeSeconds = 45;//can be made into a function input if we wanted
    uint8_t randomTimeSeconds = 30;//can be made into a function input if we wanted
    time_t startTime = 0;
    uint64_t IDStartLBA = 0;
    uint64_t ODEndingLBA = 0;
    uint64_t randomLBA = 0;
    performanceNumbers idTest, odTest, randomTest;
    memset(&idTest, 0, sizeof(performanceNumbers));
    memset(&odTest, 0, sizeof(performanceNumbers));
    memset(&randomTest, 0, sizeof(performanceNumbers));
    seatimer_t idTestTimer, odTestTimer, randomTestTimer;
    memset(&odTestTimer, 0, sizeof(seatimer_t));
    memset(&idTestTimer, 0, sizeof(seatimer_t));
    memset(&randomTestTimer, 0, sizeof(seatimer_t));
    //allocate memory now that we know the sector count
    if (rwvCommand != RWV_COMMAND_VERIFY)
    {
        dataBufSize = C_CAST(size_t, device->drive_info.deviceBlockSize) * C_CAST(size_t, sectorCount) * sizeof(uint8_t);
        dataBuf = C_CAST(uint8_t*, malloc_aligned(dataBufSize, device->os_info.minimumAlignment));
        if (!dataBuf)
        {
            perror("failed to allocate memory for reading data at OD\n");
            return MEMORY_FAILURE;
        }
    }
    //read at OD for 2 minutes...remember the LBA count to use for the ID
    if (device->deviceVerbosity > VERBOSITY_QUIET)
    {
        switch (rwvCommand)
        {
        case RWV_COMMAND_READ:
            printf("Sequential Read Test at OD for ~");
            break;
        case RWV_COMMAND_VERIFY:
            printf("Sequential Verify Test at OD for ~");
            break;
        case RWV_COMMAND_WRITE:
            printf("Sequential Write Test at OD for ~");
            break;
        default:
            printf("Sequential Unknown Test at OD for ~");
            break;
        }
        print_Time_To_Screen(NULL, NULL, NULL, NULL, &IDODTimeSeconds);
        printf("\n");
    }
    odTest.asyncCommandsUsed = false;
    odTest.fastestCommandTimeNS = UINT64_MAX;//set this to a max so that it gets readjusted later...-TJE
    odTest.sectorCount = C_CAST(uint16_t, sectorCount);
    //issue this command to get us in the right place for the OD test.
    read_Write_Seek_Command(device, rwvCommand, 0, dataBuf, C_CAST(uint32_t, sectorCount * device->drive_info.deviceBlockSize));
    startTime = time(NULL);
    start_Timer(&odTestTimer);
    while (difftime(time(NULL), startTime) < IDODTimeSeconds && ODEndingLBA < device->drive_info.deviceMaxLba)
    {
        if (VERBOSITY_QUIET < device->deviceVerbosity && !hideLBACounter)
        {
            switch (rwvCommand)
            {
            case RWV_COMMAND_READ:
                printf("\rReading LBA: %-20"PRIu64"", ODEndingLBA);//20 wide is the max width for a unsigned 64bit number
                break;
            case RWV_COMMAND_VERIFY:
                printf("\rVerifying LBA: %-20"PRIu64"", ODEndingLBA);//20 wide is the max width for a unsigned 64bit number
                break;
            case RWV_COMMAND_WRITE:
                printf("\rWriting LBA: %-20"PRIu64"", ODEndingLBA);//20 wide is the max width for a unsigned 64bit number
                break;
            default:
                printf("\rUnknown OPing LBA: %-20"PRIu64"", ODEndingLBA);//20 wide is the max width for a unsigned 64bit number
                break;
            }
            fflush(stdout);
        }
        //if (SUCCESS != read_LBA(device, ODEndingLBA, false, dataBuf, C_CAST(uint32_t, sectorCount * device->drive_info.deviceBlockSize)))
        if (SUCCESS != read_Write_Seek_Command(device, rwvCommand, ODEndingLBA, dataBuf, C_CAST(uint32_t, sectorCount * device->drive_info.deviceBlockSize)))
        {
            ret = FAILURE;
            if (device->deviceVerbosity > VERBOSITY_QUIET)
            {
                switch (rwvCommand)
                {
                case RWV_COMMAND_READ:
                    printf("\nRead failed within OD sequential read\n");
                    break;
                case RWV_COMMAND_VERIFY:
                    printf("\nVerify failed within OD sequential read\n");
                    break;
                case RWV_COMMAND_WRITE:
                    printf("\nWrite failed within OD sequential read\n");
                    break;
                default:
                    printf("\nUnknown OP failed within OD sequential read\n");
                    break;
                }
            }
            safe_Free_aligned(dataBuf)
            return ret;
        }
        ++odTest.numberOfCommandsIssued;
        odTest.averageCommandTimeNS += device->drive_info.lastCommandTimeNanoSeconds;
        if (odTest.fastestCommandTimeNS > device->drive_info.lastCommandTimeNanoSeconds)
        {
            odTest.fastestCommandTimeNS = device->drive_info.lastCommandTimeNanoSeconds;
        }
        if (odTest.slowestCommandTimeNS < device->drive_info.lastCommandTimeNanoSeconds)
        {
            odTest.slowestCommandTimeNS = device->drive_info.lastCommandTimeNanoSeconds;
        }
        ODEndingLBA += sectorCount;
    }
    stop_Timer(&odTestTimer);
    odTest.averageCommandTimeNS /= odTest.numberOfCommandsIssued;
    odTest.totalTimeNS = get_Nano_Seconds(odTestTimer);
    odTest.iops = C_CAST(uint64_t, odTest.numberOfCommandsIssued / (odTest.totalTimeNS * 1e-9));
    if (device->deviceVerbosity > VERBOSITY_QUIET)
    {
        printf("\n");
    }
    //read at ID for about 2 minutes (or exactly)
    if (device->deviceVerbosity > VERBOSITY_QUIET)
    {
        switch (rwvCommand)
        {
        case RWV_COMMAND_READ:
            printf("Sequential Read Test at ID for ~");
            break;
        case RWV_COMMAND_VERIFY:
            printf("Sequential Verify Test at ID for ~");
            break;
        case RWV_COMMAND_WRITE:
            printf("Sequential Write Test at ID for ~");
            break;
        default:
            printf("Sequential Unknown Test at ID for ~");
            break;
        }
        print_Time_To_Screen(NULL, NULL, NULL, NULL, &IDODTimeSeconds);
        printf("\n");
    }
    IDStartLBA = device->drive_info.deviceMaxLba - ODEndingLBA;
    idTest.asyncCommandsUsed = false;
    idTest.fastestCommandTimeNS = UINT64_MAX;//set this to a max so that it gets readjusted later...-TJE
    idTest.sectorCount = C_CAST(uint16_t, sectorCount);
    //issue this read to get the heads in the right place before starting the ID test.
    read_Write_Seek_Command(device, rwvCommand, IDStartLBA, dataBuf, C_CAST(uint32_t, sectorCount * device->drive_info.deviceBlockSize));
    startTime = time(NULL);
    start_Timer(&idTestTimer);
    while (difftime(time(NULL), startTime) < IDODTimeSeconds && IDStartLBA < device->drive_info.deviceMaxLba)
    {
        if (VERBOSITY_QUIET < device->deviceVerbosity && !hideLBACounter)
        {
            switch (rwvCommand)
            {
            case RWV_COMMAND_READ:
                printf("\rReading LBA: %-20"PRIu64"", IDStartLBA);//20 wide is the max width for a unsigned 64bit number
                break;
            case RWV_COMMAND_VERIFY:
                printf("\rVerifying LBA: %-20"PRIu64"", IDStartLBA);//20 wide is the max width for a unsigned 64bit number
                break;
            case RWV_COMMAND_WRITE:
                printf("\rWriting LBA: %-20"PRIu64"", IDStartLBA);//20 wide is the max width for a unsigned 64bit number
                break;
            default:
                printf("\rUnknown OPing LBA: %-20"PRIu64"", IDStartLBA);//20 wide is the max width for a unsigned 64bit number
                break;
            }
            fflush(stdout);
        }
        if (SUCCESS != read_Write_Seek_Command(device, rwvCommand, IDStartLBA, dataBuf, C_CAST(uint32_t, sectorCount * device->drive_info.deviceBlockSize)))
        {
            ret = FAILURE;
            if (device->deviceVerbosity > VERBOSITY_QUIET)
            {
                switch (rwvCommand)
                {
                case RWV_COMMAND_READ:
                    printf("\nRead failed within ID sequential read\n");
                    break;
                case RWV_COMMAND_VERIFY:
                    printf("\nVerify failed within ID sequential read\n");
                    break;
                case RWV_COMMAND_WRITE:
                    printf("\nWrite failed within ID sequential read\n");
                    break;
                default:
                    printf("\nUnknown OP failed within ID sequential read\n");
                    break;
                }
            }
            safe_Free_aligned(dataBuf)
            return ret;
        }
        ++idTest.numberOfCommandsIssued;
        idTest.averageCommandTimeNS += device->drive_info.lastCommandTimeNanoSeconds;
        if (idTest.fastestCommandTimeNS > device->drive_info.lastCommandTimeNanoSeconds)
        {
            idTest.fastestCommandTimeNS = device->drive_info.lastCommandTimeNanoSeconds;
        }
        if (idTest.slowestCommandTimeNS < device->drive_info.lastCommandTimeNanoSeconds)
        {
            idTest.slowestCommandTimeNS = device->drive_info.lastCommandTimeNanoSeconds;
        }
        IDStartLBA += sectorCount;
    }
    stop_Timer(&idTestTimer);
    idTest.averageCommandTimeNS /= idTest.numberOfCommandsIssued;
    idTest.totalTimeNS = get_Nano_Seconds(idTestTimer);
    idTest.iops = C_CAST(uint64_t, idTest.numberOfCommandsIssued / (idTest.totalTimeNS * 1e-9));
    if (device->deviceVerbosity > VERBOSITY_QUIET)
    {
        printf("\n");
    }
    //now random reads for 30 seconds
    //start random number generator
    seed_64(time(NULL));
    if (device->deviceVerbosity > VERBOSITY_QUIET)
    {
        switch (rwvCommand)
        {
        case RWV_COMMAND_READ:
            printf("Random Read Test for ~");
            break;
        case RWV_COMMAND_VERIFY:
            printf("Random Verify Test for ~");
            break;
        case RWV_COMMAND_WRITE:
            printf("Random Write Test for ~");
            break;
        default:
            printf("Random Unknown Test for ~");
            break;
        }
        print_Time_To_Screen(NULL, NULL, NULL, NULL, &randomTimeSeconds);
        printf("\n");
    }
    randomTest.asyncCommandsUsed = false;
    randomTest.fastestCommandTimeNS = UINT64_MAX;//set this to a max so that it gets readjusted later...-TJE
    randomTest.sectorCount = C_CAST(uint16_t, sectorCount);
    startTime = time(NULL);
    start_Timer(&randomTestTimer);
    while (difftime(time(NULL), startTime) < randomTimeSeconds)
    {
        randomLBA = random_Range_64(0, device->drive_info.deviceMaxLba);
        if (VERBOSITY_QUIET < device->deviceVerbosity && !hideLBACounter)
        {
            switch (rwvCommand)
            {
            case RWV_COMMAND_READ:
                printf("\rReading LBA: %-20"PRIu64"", randomLBA);//20 wide is the max width for a unsigned 64bit number
                break;
            case RWV_COMMAND_VERIFY:
                printf("\rVerifying LBA: %-20"PRIu64"", randomLBA);//20 wide is the max width for a unsigned 64bit number
                break;
            case RWV_COMMAND_WRITE:
                printf("\rWriting LBA: %-20"PRIu64"", randomLBA);//20 wide is the max width for a unsigned 64bit number
                break;
            default:
                printf("\rUnknown OPing LBA: %-20"PRIu64"", randomLBA);//20 wide is the max width for a unsigned 64bit number
                break;
            }
            fflush(stdout);
        }
        if (SUCCESS != read_Write_Seek_Command(device, rwvCommand, randomLBA, dataBuf, C_CAST(uint32_t, 1 * device->drive_info.deviceBlockSize)))
        {
            ret = FAILURE;
            if (device->deviceVerbosity > VERBOSITY_QUIET)
            {
                switch (rwvCommand)
                {
                case RWV_COMMAND_READ:
                    printf("\nRandom Read failed\n");
                    break;
                case RWV_COMMAND_VERIFY:
                    printf("\nRandom Verify failed\n");
                    break;
                case RWV_COMMAND_WRITE:
                    printf("\nRandom Write failed\n");
                    break;
                default:
                    printf("\nRandom Unknown OP failed\n");
                    break;
                }
            }
            safe_Free_aligned(dataBuf)
            return ret;
        }
        ++randomTest.numberOfCommandsIssued;
        randomTest.averageCommandTimeNS += device->drive_info.lastCommandTimeNanoSeconds;
        if (randomTest.fastestCommandTimeNS > device->drive_info.lastCommandTimeNanoSeconds)
        {
            randomTest.fastestCommandTimeNS = device->drive_info.lastCommandTimeNanoSeconds;
        }
        if (randomTest.slowestCommandTimeNS < device->drive_info.lastCommandTimeNanoSeconds)
        {
            randomTest.slowestCommandTimeNS = device->drive_info.lastCommandTimeNanoSeconds;
        }
    }
    stop_Timer(&randomTestTimer);
    if (device->deviceVerbosity > VERBOSITY_QUIET)
    {
        printf("\n");
    }
    randomTest.averageCommandTimeNS /= randomTest.numberOfCommandsIssued;
    randomTest.totalTimeNS = get_Nano_Seconds(randomTestTimer);
    randomTest.iops = C_CAST(uint64_t, randomTest.numberOfCommandsIssued / (randomTest.totalTimeNS * 1e-9));
    if (device->deviceVerbosity > VERBOSITY_QUIET && showPerformanceNumbers)
    {
        printf("\n");
        printf("===Drive Performance Characteristics===\n");
        printf("\tNOT AN OFFICIAL BENCHMARK\n\n");
        printf("Read-Look-Ahead: ");
        if (is_Read_Look_Ahead_Enabled(device))
        {
            printf("Enabled\n");
        }
        else
        {
            printf("Disabled\n");
        }
        printf("Write Cache: ");
        if (is_Write_Cache_Enabled(device))
        {
            printf("Enabled\n");
        }
        else
        {
            printf("Disabled\n");
        }
        printf("OD Test:\n");
        if (odTest.asyncCommandsUsed)
        {
            printf("\tUsed asynchronous commands\n");
        }
        else
        {
            printf("\tUsed synchronous commands\n");
        }
        printf("\tAverage Command time: ");
        print_Time(odTest.averageCommandTimeNS);
        printf("\tFastest Command time: ");
        print_Time(odTest.fastestCommandTimeNS);
        printf("\tSlowest Command time: ");
        print_Time(odTest.slowestCommandTimeNS);
        printf("\tIOPS: %"PRIu64"\n", odTest.iops);
        //calculate MB(/GB)/s performance
        uint64_t odBytesPerTransfer = C_CAST(uint64_t, device->drive_info.deviceBlockSize) * C_CAST(uint64_t, odTest.sectorCount);
        double odTotalBytesTransferred = C_CAST(double, odBytesPerTransfer * odTest.numberOfCommandsIssued);
        double odDataRate = odTotalBytesTransferred / C_CAST(double, odTest.totalTimeNS * 1e-9);
        char odDataRateUnits[3] = { 0 };
        char *odDataRateUnit = &odDataRateUnits[0];
        metric_Unit_Convert(&odDataRate, &odDataRateUnit);
        printf("\tData Rate: %0.02f %s/s\n", odDataRate, odDataRateUnit);
        printf("\tNumber of Commands Issued: %"PRIu64"\n", odTest.numberOfCommandsIssued);
        printf("\tLBAs accessed per command: %"PRIu16"\n", odTest.sectorCount);
        printf("\tTotal LBAs accessed: %"PRIu64"\n", odTest.numberOfCommandsIssued * odTest.sectorCount);


        printf("ID Test:\n");
        if (idTest.asyncCommandsUsed)
        {
            printf("\tUsed asynchronous commands\n");
        }
        else
        {
            printf("\tUsed synchronous commands\n");
        }
        printf("\tAverage Command time: ");
        print_Time(idTest.averageCommandTimeNS);
        printf("\tFastest Command time: ");
        print_Time(idTest.fastestCommandTimeNS);
        printf("\tSlowest Command time: ");
        print_Time(idTest.slowestCommandTimeNS);
        printf("\tIOPS: %"PRIu64"\n", idTest.iops);
        //calculate MB(/GB)/s performance
        uint64_t idBytesPerTransfer = C_CAST(uint64_t, device->drive_info.deviceBlockSize) * C_CAST(uint64_t, idTest.sectorCount);
        double idTotalBytesTransferred = C_CAST(double, idBytesPerTransfer * idTest.numberOfCommandsIssued);
        double idDataRate = idTotalBytesTransferred / C_CAST(double, idTest.totalTimeNS * 1e-9);
        char idDataRateUnits[3] = { 0 };
        char *idDataRateUnit = &idDataRateUnits[0];
        metric_Unit_Convert(&idDataRate, &idDataRateUnit);
        printf("\tData Rate: %0.02f %s/s\n", idDataRate, idDataRateUnit);
        printf("\tNumber of Commands Issued: %"PRIu64"\n", idTest.numberOfCommandsIssued);
        printf("\tLBAs accessed per command: %"PRIu16"\n", idTest.sectorCount);
        printf("\tTotal LBAs accessed: %"PRIu64"\n", idTest.numberOfCommandsIssued * idTest.sectorCount);

        printf("Random Test:\n");
        if (randomTest.asyncCommandsUsed)
        {
            printf("\tUsed asynchronous commands\n");
        }
        else
        {
            printf("\tUsed synchronous commands\n");
        }
        printf("\tAverage Command time: ");
        print_Time(randomTest.averageCommandTimeNS);
        printf("\tFastest Command time: ");
        print_Time(randomTest.fastestCommandTimeNS);
        printf("\tSlowest Command time: ");
        print_Time(randomTest.slowestCommandTimeNS);
        printf("\tIOPS: %"PRIu64"\n", randomTest.iops);
        //calculate MB(/GB)/s performance
        uint64_t randomBytesPerTransfer = C_CAST(uint64_t, device->drive_info.deviceBlockSize) * C_CAST(uint64_t, randomTest.sectorCount);
        double randomTotalBytesTransferred = C_CAST(double, randomBytesPerTransfer * randomTest.numberOfCommandsIssued);
        double randomDataRate = randomTotalBytesTransferred / C_CAST(double, randomTest.totalTimeNS * 1e-9);
        char randomDataRateUnits[3] = { 0 };
        char *randomDataRateUnit = &randomDataRateUnits[0];
        metric_Unit_Convert(&randomDataRate, &randomDataRateUnit);
        printf("\tData Rate: %0.02f %s/s\n", randomDataRate, randomDataRateUnit);
        printf("\tNumber of Commands Issued: %"PRIu64"\n", randomTest.numberOfCommandsIssued);
        printf("\tLBAs accessed per command: %"PRIu16"\n", randomTest.sectorCount);
        printf("\tTotal LBAs accessed: %"PRIu64"\n", randomTest.numberOfCommandsIssued * randomTest.sectorCount);
    }
    safe_Free_aligned(dataBuf)
    return ret;
}

int long_Generic_Read_Test(tDevice *device, uint16_t errorLimit, bool stopOnError, bool repairOnTheFly, bool repairAtEnd, custom_Update updateFunction, void *updateData, bool hideLBACounter)
{
    return user_Sequential_Read_Test(device, 0, device->drive_info.deviceMaxLba, errorLimit, stopOnError, repairOnTheFly, repairAtEnd, updateFunction, updateData, hideLBACounter);
}

int long_Generic_Write_Test(tDevice *device, uint16_t errorLimit, bool stopOnError, bool repairOnTheFly, bool repairAtEnd, custom_Update updateFunction, void *updateData, bool hideLBACounter)
{
    return user_Sequential_Write_Test(device, 0, device->drive_info.deviceMaxLba, errorLimit, stopOnError, repairOnTheFly, repairAtEnd, updateFunction, updateData, hideLBACounter);
}

int long_Generic_Verify_Test(tDevice *device, uint16_t errorLimit, bool stopOnError, bool repairOnTheFly, bool repairAtEnd, custom_Update updateFunction, void *updateData, bool hideLBACounter)
{
    return user_Sequential_Verify_Test(device, 0, device->drive_info.deviceMaxLba, errorLimit, stopOnError, repairOnTheFly, repairAtEnd, updateFunction, updateData, hideLBACounter);
}

int long_Generic_Test(tDevice *device, eRWVCommandType rwvCommand, uint16_t errorLimit, bool stopOnError, bool repairOnTheFly, bool repairAtEnd, custom_Update updateFunction, void *updateData, bool hideLBACounter)
{
    return user_Sequential_Test(device, rwvCommand, 0, device->drive_info.deviceMaxLba, errorLimit, stopOnError, repairOnTheFly, repairAtEnd, updateFunction, updateData, hideLBACounter);
}

int user_Sequential_Read_Test(tDevice *device, uint64_t startingLBA, uint64_t range, uint16_t errorLimit, bool stopOnError, bool repairOnTheFly, bool repairAtEnd, custom_Update updateFunction, void *updateData, bool hideLBACounter)
{
    return user_Sequential_Test(device, RWV_COMMAND_READ, startingLBA, range, errorLimit, stopOnError, repairOnTheFly, repairAtEnd, updateFunction, updateData, hideLBACounter);
}

int user_Sequential_Write_Test(tDevice *device, uint64_t startingLBA, uint64_t range, uint16_t errorLimit, bool stopOnError, bool repairOnTheFly, bool repairAtEnd, custom_Update updateFunction, void *updateData, bool hideLBACounter)
{
    return user_Sequential_Test(device, RWV_COMMAND_WRITE, startingLBA, range, errorLimit, stopOnError, repairOnTheFly, repairAtEnd, updateFunction, updateData, hideLBACounter);
}

int user_Sequential_Verify_Test(tDevice *device, uint64_t startingLBA, uint64_t range, uint16_t errorLimit, bool stopOnError, bool repairOnTheFly, bool repairAtEnd, custom_Update updateFunction, void *updateData, bool hideLBACounter)
{
    return user_Sequential_Test(device, RWV_COMMAND_VERIFY, startingLBA, range, errorLimit, stopOnError, repairOnTheFly, repairAtEnd, updateFunction, updateData, hideLBACounter);
}

int user_Sequential_Test(tDevice *device, eRWVCommandType rwvCommand, uint64_t startingLBA, uint64_t range, uint16_t errorLimit, bool stopOnError, bool repairOnTheFly, bool repairAtEnd, M_ATTR_UNUSED custom_Update updateFunction, M_ATTR_UNUSED void *updateData, bool hideLBACounter)
{
    int ret = SUCCESS;
    errorLBA *errorList = NULL;
    uint64_t errorIndex = 0;
    bool errorLimitReached = false;
    uint32_t sectorCount = get_Sector_Count_For_Read_Write(device);
    //only one of these flags should be set. If they are both set, this makes no sense
    if ((repairAtEnd && repairOnTheFly) || (repairAtEnd && (errorLimit == 0)))
    {
        return BAD_PARAMETER;
    }
    if (stopOnError)
    {
        //disable the repair flags in this case since they don't make sense
        repairAtEnd = false;
        repairOnTheFly = false;
    }
    if (errorLimit < 1)
    {
        //need to be able to store at least 1 error
        errorList = C_CAST(errorLBA*, calloc(1 * sizeof(errorLBA), sizeof(errorLBA)));
    }
    else
    {
        errorList = C_CAST(errorLBA*, calloc(errorLimit * sizeof(errorLBA), sizeof(errorLBA)));
    }
    if (!errorList)
    {
        perror("calloc failure\n");
        return MEMORY_FAILURE;
    }
    errorList[0].errorAddress = UINT64_MAX;
    bool autoReadReassign = false;
    bool autoWriteReassign = false;
    if (SUCCESS != get_Automatic_Reallocation_Support(device, &autoWriteReassign, &autoReadReassign))
    {
        autoWriteReassign = true;//just in case this fails, default to previous behavior
    }
    //this is escentially a loop over the sequential read function
    uint64_t endingLBA = startingLBA + range;
    while (!errorLimitReached)
    {
        if (SUCCESS != sequential_RWV(device, rwvCommand, startingLBA, range, sectorCount, &errorList[errorIndex].errorAddress, updateFunction, updateData, hideLBACounter))
        {
            if (device->deviceVerbosity > VERBOSITY_QUIET)
            {
                printf("\nError Found at LBA %"PRIu64"", errorList[errorIndex].errorAddress);
                if (errorLimit != 0)
                    printf("\n");
            }
            //set a new start for next time through the loop to 1 lba past the last error LBA
            startingLBA = errorList[errorIndex].errorAddress + 1;
            range = endingLBA - startingLBA;
            if (stopOnError || ((errorLimit != 0) && (errorIndex >= errorLimit)))
            {
                errorLimitReached = true;
                ret = FAILURE;
            }
            if (repairOnTheFly)
            {
                repair_LBA(device, &errorList[errorIndex], false, autoWriteReassign, autoReadReassign);//This function will set the repair status for us. - TJE
            }
            if (errorLimit != 0)
                errorIndex++;
        }
        else
        {
            break;
        }
    }
    if (device->deviceVerbosity > VERBOSITY_QUIET)
    {
        printf("\n");
    }
    if (repairAtEnd)
    {
        //go through and repair the LBAs
        uint64_t errorIter = 0;
        uint64_t lastLBARepaired = UINT64_MAX;
        uint16_t logicalPerPhysicalSectors = C_CAST(uint16_t, device->drive_info.devicePhyBlockSize / device->drive_info.deviceBlockSize);
        for (errorIter = 0; errorIter < errorIndex; errorIter++)
        {
            if (lastLBARepaired != UINT64_MAX)
            {
                //check if the LBA we want to repair is within the same physical sector as the last LBA
                if ((lastLBARepaired + logicalPerPhysicalSectors) > errorList[errorIter].errorAddress)
                {
                    //in this case, we have already repaired this LBA since the repair is issued to the physical sector, so move on to the next thing in the list
                    errorList[errorIter].repairStatus = REPAIR_NOT_REQUIRED;
                    continue;
                }
            }
            if (SUCCESS == repair_LBA(device, &errorList[errorIter], false, autoWriteReassign, autoReadReassign))
            {
                lastLBARepaired = errorList[errorIter].errorAddress;
            }
        }
    }
    if (stopOnError && errorList[0].errorAddress != UINT64_MAX)
    {
        if (device->deviceVerbosity > VERBOSITY_QUIET)
        {
            printf("\nError occured at LBA %"PRIu64"\n",errorList[0].errorAddress);
        }
    }
    else
    {
        if (device->deviceVerbosity > VERBOSITY_QUIET)
        {
            if (errorList[0].errorAddress != UINT64_MAX)
            {
                if (errorLimit != 0)
                {
                    print_LBA_Error_List(errorList, C_CAST(uint16_t, errorIndex));
                }
                else
                {
                    printf("One or more bad LBAs detected during read scan of device.\n");
                    ret = FAILURE;
                }
            }
            else
            {
                printf("No bad LBAs detected during read scan of device.\n");
            }
        }
    }
    safe_Free(errorList)
    return ret;
}

int user_Timed_Test(tDevice *device, eRWVCommandType rwvCommand, uint64_t startingLBA, uint64_t timeInSeconds, uint16_t errorLimit, bool stopOnError, bool repairOnTheFly, bool repairAtEnd, M_ATTR_UNUSED custom_Update updateFunction, M_ATTR_UNUSED void *updateData, bool hideLBACounter)
{
    int ret = SUCCESS;
    bool errorLimitReached = false;
    errorLBA *errorList = NULL;
    uint64_t errorIndex = 0;
    uint32_t sectorCount = get_Sector_Count_For_Read_Write(device);
    uint8_t *dataBuf = NULL;
    size_t dataBufSize = 0;
    //only one of these flags should be set. If they are both set, this makes no sense
    if (stopOnError)
    {
        //disable the repair flags in this case since they don't make sense
        repairAtEnd = false;
        repairOnTheFly = false;
    }
    if (errorLimit < 1)
    {
        //need to be able to store at least 1 error
        errorLimit = 1;
    }
    errorList = C_CAST(errorLBA*, calloc(errorLimit, sizeof(errorLBA)));
    if (!errorList)
    {
        perror("calloc failure\n");
        return MEMORY_FAILURE;
    }
    if (rwvCommand == RWV_COMMAND_READ || rwvCommand == RWV_COMMAND_WRITE)
    {
        //allocate memory
        dataBufSize = C_CAST(size_t, device->drive_info.deviceBlockSize) * C_CAST(size_t, sectorCount);
        dataBuf = C_CAST(uint8_t*, calloc_aligned(dataBufSize, sizeof(uint8_t), device->os_info.minimumAlignment));
        if (!dataBuf)
        {
            perror("failed to allocate memory!\n");
            safe_Free(errorList)
            return MEMORY_FAILURE;
        }
    }
    errorList[errorIndex].errorAddress = UINT64_MAX;
    bool autoReadReassign = false;
    bool autoWriteReassign = false;
    if (SUCCESS != get_Automatic_Reallocation_Support(device, &autoWriteReassign, &autoReadReassign))
    {
        autoWriteReassign = true;//just in case this fails, default to previous behavior
    }
    //TODO: make sure the starting LBA is alligned? If we do this, we need to make sure we don't mess with the data of the LBAs we don't mean to start at...mostly don't want to erase an LBA we shouldn't be starting at.
    //startingLBA = align_LBA(device, startingLBA);
    //this is escentially a loop over the sequential read function
    time_t startTime = time(NULL);
    while (!errorLimitReached && difftime(time(NULL), startTime) < timeInSeconds && startingLBA < device->drive_info.deviceMaxLba)
    {
        if ((startingLBA + sectorCount) > device->drive_info.deviceMaxLba)
        {
            sectorCount = C_CAST(uint32_t, device->drive_info.deviceMaxLba - startingLBA + 1);
        }
        if (VERBOSITY_QUIET < device->deviceVerbosity && !hideLBACounter)
        {
            switch (rwvCommand)
            {
            case RWV_COMMAND_WRITE:
                printf("\rWriting LBA: %-20"PRIu64"", startingLBA);//20 wide is the max width for a unsigned 64bit number
                break;
            case RWV_COMMAND_READ:
                printf("\rReading LBA: %-20"PRIu64"", startingLBA);//20 wide is the max width for a unsigned 64bit number
                break;
            case RWV_COMMAND_VERIFY:
            default:
                printf("\rVerifying LBA: %-20"PRIu64"", startingLBA);//20 wide is the max width for a unsigned 64bit number
                break;
            }
            fflush(stdout);
        }
        if (SUCCESS != read_Write_Seek_Command(device, rwvCommand, startingLBA, dataBuf, C_CAST(uint32_t, sectorCount * device->drive_info.deviceBlockSize)))
        {
            uint64_t maxSingleLoopLBA = startingLBA + sectorCount;//limits the loop to trying to only a certain number of sectors without getting stuck at single LBA reads.
            //read command failure...so we need to read until we find the exact failing lba
            for (; startingLBA <= maxSingleLoopLBA; startingLBA += 1)
            {
                //print out the current LBA we are rwving
                if (VERBOSITY_QUIET < device->deviceVerbosity && !hideLBACounter)
                {
                    switch (rwvCommand)
                    {
                    case RWV_COMMAND_WRITE:
                        printf("\rWriting LBA: %-20"PRIu64"", startingLBA);//20 wide is the max width for a unsigned 64bit number
                        break;
                    case RWV_COMMAND_READ:
                        printf("\rReading LBA: %-20"PRIu64"", startingLBA);//20 wide is the max width for a unsigned 64bit number
                        break;
                    case RWV_COMMAND_VERIFY:
                    default:
                        printf("\rVerifying LBA: %-20"PRIu64"", startingLBA);//20 wide is the max width for a unsigned 64bit number
                        break;
                    }
                    fflush(stdout);
                }
                if (SUCCESS != read_Write_Seek_Command(device, rwvCommand, startingLBA, dataBuf, C_CAST(uint32_t, 1 * device->drive_info.deviceBlockSize)))
                {
                    errorList[errorIndex].errorAddress = startingLBA;
                    break;
                }
            }
            if (device->deviceVerbosity > VERBOSITY_QUIET)
            {
                printf("\nError Found at LBA %"PRIu64"\n", errorList[errorIndex].errorAddress);
            }
            if (stopOnError || errorIndex >= errorLimit)
            {
                errorLimitReached = true;
                ret = FAILURE;
            }
            if (repairOnTheFly)
            {
                repair_LBA(device, &errorList[errorIndex], false, autoWriteReassign, autoReadReassign);
            }
            //set a new start for next time through the loop to 1 lba past the last error LBA
            if (errorIndex < errorLimit)
            {
                startingLBA = errorList[errorIndex].errorAddress + 1;
                errorIndex++;
                continue;//continuing here since startingLBA will get incremented beyond the error so we pick up where we left off.
            }
            else
            {
                errorLimitReached = true;
                ret = FAILURE;
                break;
            }
        }
        startingLBA += sectorCount;

    }
    if (VERBOSITY_QUIET < device->deviceVerbosity && !hideLBACounter)
    {
        switch (rwvCommand)
        {
        case RWV_COMMAND_WRITE:
            printf("\rWriting LBA: %-20"PRIu64"", startingLBA);//20 wide is the max width for a unsigned 64bit number
            break;
        case RWV_COMMAND_READ:
            printf("\rReading LBA: %-20"PRIu64"", startingLBA);//20 wide is the max width for a unsigned 64bit number
            break;
        case RWV_COMMAND_VERIFY:
        default:
            printf("\rVerifying LBA: %-20"PRIu64"", startingLBA);//20 wide is the max width for a unsigned 64bit number
            break;
        }
        printf("\n");
        fflush(stdout);
    }
    safe_Free_aligned(dataBuf)
    if (device->deviceVerbosity > VERBOSITY_QUIET)
    {
        printf("\n");
    }
    if (repairAtEnd)
    {
        //go through and repair the LBAs
        uint64_t errorIter = 0;
        uint64_t lastLBARepaired = UINT64_MAX;
        uint16_t logicalPerPhysicalSectors = C_CAST(uint16_t, device->drive_info.devicePhyBlockSize / device->drive_info.deviceBlockSize);
        for (errorIter = 0; errorIter < errorIndex; errorIter++)
        {
            if (lastLBARepaired != UINT64_MAX)
            {
                //check if the LBA we want to repair is within the same physical sector as the last LBA
                if ((lastLBARepaired + logicalPerPhysicalSectors) > errorList[errorIter].errorAddress)
                {
                    //in this case, we have already repaired this LBA since the repair is issued to the physical sector, so move on to the next thing in the list
                    errorList[errorIter].repairStatus = REPAIR_NOT_REQUIRED;
                    continue;
                }
            }
            if (SUCCESS == repair_LBA(device, &errorList[errorIter], false, autoWriteReassign, autoReadReassign))
            {
                lastLBARepaired = errorList[errorIter].errorAddress;
            }
        }
    }
    if (stopOnError && errorList[0].errorAddress != UINT64_MAX)
    {
        if (device->deviceVerbosity > VERBOSITY_QUIET)
        {
            printf("\nError occured at LBA %"PRIu64"\n", errorList[0].errorAddress);
        }
    }
    else
    {
        if (device->deviceVerbosity > VERBOSITY_QUIET)
        {
            if (errorList[0].errorAddress != UINT64_MAX)
            {
                print_LBA_Error_List(errorList, C_CAST(uint16_t, errorIndex));
            }
            else
            {
                printf("No bad LBAs detected during read scan of device.\n");
            }
        }
    }
    safe_Free(errorList)
    return ret;
}

int butterfly_Read_Test(tDevice *device, time_t timeLimitSeconds, custom_Update updateFunction, void *updateData, bool hideLBACounter)
{
    return butterfly_Test(device, RWV_COMMAND_READ, timeLimitSeconds, updateFunction, updateData, hideLBACounter);
}

int butterfly_Write_Test(tDevice *device, time_t timeLimitSeconds, custom_Update updateFunction, void *updateData, bool hideLBACounter)
{
    return butterfly_Test(device, RWV_COMMAND_WRITE, timeLimitSeconds, updateFunction, updateData, hideLBACounter);
}

int butterfly_Verify_Test(tDevice *device, time_t timeLimitSeconds, custom_Update updateFunction, void *updateData, bool hideLBACounter)
{
    return butterfly_Test(device, RWV_COMMAND_VERIFY, timeLimitSeconds, updateFunction, updateData, hideLBACounter);
}

int butterfly_Test(tDevice *device, eRWVCommandType rwvcommand, time_t timeLimitSeconds, M_ATTR_UNUSED custom_Update updateFunction, M_ATTR_UNUSED void *updateData, bool hideLBACounter)
{
    int ret = SUCCESS;
    time_t startTime = 0;//will be set to actual current time before we start the test
    uint32_t sectorCount = get_Sector_Count_For_Read_Write(device);
    uint64_t outerLBA = 0, innerLBA = device->drive_info.deviceMaxLba;
    uint8_t *dataBuf = NULL;
    size_t dataBufSize = 0;
    if (rwvcommand != RWV_COMMAND_VERIFY)
    {
        dataBufSize = C_CAST(size_t, device->drive_info.deviceBlockSize) * C_CAST(size_t, sectorCount) * sizeof(uint8_t);
        dataBuf = C_CAST(uint8_t*, malloc_aligned(dataBufSize, device->os_info.minimumAlignment));
        if (!dataBuf)
        {
            return MEMORY_FAILURE;
        }
    }
    uint32_t currentSectorCount = sectorCount;
    innerLBA -= sectorCount;
    time(&startTime);//get the starting time before starting the loop
    double lastTime = 0.0;
    while ((lastTime = difftime(time(NULL), startTime)) < timeLimitSeconds)
    {
        //read the outer lba
        if ((outerLBA + sectorCount) > device->drive_info.deviceMaxLba)
        {
            //adjust the sector count to get to the maxLBA for the read
            currentSectorCount = C_CAST(uint32_t, device->drive_info.deviceMaxLba - outerLBA);
        }
        if (VERBOSITY_QUIET < device->deviceVerbosity && !hideLBACounter)
        {
            switch (rwvcommand)
            {
            case RWV_COMMAND_READ:
                printf("\rReading LBA: %-20"PRIu64"", outerLBA);
                break;
            case RWV_COMMAND_WRITE:
                printf("\rWriting LBA: %-20"PRIu64"", outerLBA);
                break;
            case RWV_COMMAND_VERIFY:
                printf("\rVerifying LBA: %-20"PRIu64"", outerLBA);
                break;
            default:
                printf("\rUnknown OPing LBA: %-20"PRIu64"", outerLBA);
                break;
            }
            fflush(stdout);
        }
        if (SUCCESS != read_Write_Seek_Command(device, rwvcommand, outerLBA, dataBuf, C_CAST(uint32_t, currentSectorCount * device->drive_info.deviceBlockSize)))
        {
            ret = FAILURE;
            //error occured, time to exit the loop
            break;
        }
        outerLBA += currentSectorCount;
        if (outerLBA >= device->drive_info.deviceMaxLba)//reset back to lba 0
        {
            outerLBA = 0;
        }
        //read the inner lba
        if (C_CAST(int64_t, innerLBA - sectorCount) < 0)
        {
            //adjust the sector count to get to 0 for the read
            currentSectorCount = C_CAST(uint32_t, innerLBA);//this should set us up to read the remaining sectors to 0
        }
        if (VERBOSITY_QUIET < device->deviceVerbosity && !hideLBACounter)
        {
            switch (rwvcommand)
            {
            case RWV_COMMAND_READ:
                printf("\rReading LBA: %-20"PRIu64"", innerLBA);
                break;
            case RWV_COMMAND_WRITE:
                printf("\rWriting LBA: %-20"PRIu64"", innerLBA);
                break;
            case RWV_COMMAND_VERIFY:
                printf("\rVerifying LBA: %-20"PRIu64"", innerLBA);
                break;
            default:
                printf("\rUnknown OPing LBA: %-20"PRIu64"", innerLBA);
                break;
            }
            fflush(stdout);
        }
        if (SUCCESS != read_Write_Seek_Command(device, rwvcommand, innerLBA, dataBuf, C_CAST(uint32_t, currentSectorCount * device->drive_info.deviceBlockSize)))
        {
            ret = FAILURE;
            //error occured, time to exit the loop
            break;
        }
        //always adjust the innerLBA
        innerLBA -= currentSectorCount;
        if (innerLBA == 0)//time to reset to the maxLBA
        {
            innerLBA = device->drive_info.deviceMaxLba - sectorCount;
        }
        //set the sector count back to what it was before the next iteration in the loop
        if (currentSectorCount != sectorCount)
        {
            currentSectorCount = sectorCount;
        }
    }
    safe_Free(dataBuf)
    if (VERBOSITY_QUIET < device->deviceVerbosity)
    {
        printf("\n");
    }
    return ret;
}

int random_Read_Test(tDevice *device, time_t timeLimitSeconds, custom_Update updateFunction, void *updateData, bool hideLBACounter)
{
    return random_Test(device, RWV_COMMAND_READ, timeLimitSeconds, updateFunction, updateData, hideLBACounter);
}

int random_Write_Test(tDevice *device, time_t timeLimitSeconds, custom_Update updateFunction, void *updateData, bool hideLBACounter)
{
    return random_Test(device, RWV_COMMAND_WRITE, timeLimitSeconds, updateFunction, updateData, hideLBACounter);
}

int random_Verify_Test(tDevice *device, time_t timeLimitSeconds, custom_Update updateFunction, void *updateData, bool hideLBACounter)
{
    return random_Test(device, RWV_COMMAND_VERIFY, timeLimitSeconds, updateFunction, updateData, hideLBACounter);
}

int random_Test(tDevice *device, eRWVCommandType rwvcommand, time_t timeLimitSeconds, M_ATTR_UNUSED custom_Update updateFunction, M_ATTR_UNUSED void *updateData, bool hideLBACounter)
{
    int ret = SUCCESS;
    time_t startTime = 0;//will be set to actual current time before we start the test
    uint32_t sectorCount = 1;
    uint8_t *dataBuf = NULL;
    if (rwvcommand != RWV_COMMAND_VERIFY)
    {
        dataBuf = C_CAST(uint8_t*, malloc(device->drive_info.deviceBlockSize * sectorCount * sizeof(uint8_t)));
        if (!dataBuf)
        {
            return MEMORY_FAILURE;
        }
    }
    seed_64(time(NULL));//start the seed for the random number generator
    time(&startTime);//get the starting time before starting the loop
    double lastTime = 0.0;
    while ((lastTime = difftime(time(NULL), startTime)) < timeLimitSeconds)
    {
        uint64_t randomLBA = random_Range_64(0, device->drive_info.deviceMaxLba);
        if (VERBOSITY_QUIET < device->deviceVerbosity && !hideLBACounter)
        {
            switch (rwvcommand)
            {
            case RWV_COMMAND_READ:
                printf("\rReading LBA: %-20"PRIu64"", randomLBA);
                break;
            case RWV_COMMAND_WRITE:
                printf("\rWriting LBA: %-20"PRIu64"", randomLBA);
                break;
            case RWV_COMMAND_VERIFY:
                printf("\rVerifying LBA: %-20"PRIu64"", randomLBA);
                break;
            default:
                printf("\rUnknown OPing LBA: %-20"PRIu64"", randomLBA);
                break;
            }
            fflush(stdout);
        }
        if (SUCCESS != read_Write_Seek_Command(device, rwvcommand, randomLBA, dataBuf, C_CAST(uint32_t, sectorCount * device->drive_info.deviceBlockSize)))
        {
            ret = FAILURE;
            //error occured, time to exit the loop
            break;
        }
    }
    if (VERBOSITY_QUIET < device->deviceVerbosity)
    {
        printf("\n");
    }
    safe_Free(dataBuf)
    return ret;
}

int read_Write_Or_Verify_Timed_Test(tDevice *device, eRWVCommandType testMode, uint32_t timePerTestSeconds, uint16_t *numberOfCommandTimeouts, uint16_t *numberOfCommandFailures, M_ATTR_UNUSED custom_Update updateFunction, M_ATTR_UNUSED void *updateData)
{
    uint8_t *dataBuf = NULL;
    size_t dataBufSize = 0;
    time_t startTime = 0;
    uint64_t IDStartLBA = 0;
    uint64_t ODEndingLBA = 0;
    uint64_t randomLBA = 0;
    uint64_t outerLBA = 0, innerLBA = device->drive_info.deviceMaxLba;
    uint32_t sectorCount = get_Sector_Count_For_Read_Write(device);
    uint32_t currentSectorCount = sectorCount;
    if (device->deviceVerbosity > VERBOSITY_QUIET)
    {
        printf("\n");
    }
    //OD
    if (testMode == RWV_COMMAND_READ || testMode == RWV_COMMAND_WRITE)
    {
        dataBufSize = C_CAST(size_t, device->drive_info.deviceBlockSize) * C_CAST(size_t, sectorCount) * sizeof(uint8_t);
        dataBuf = C_CAST(uint8_t*, malloc_aligned(dataBufSize, device->os_info.minimumAlignment));
        if (!dataBuf)
        {
            perror("failed to allocate memory!\n");
            return MEMORY_FAILURE;
        }
        if (testMode == RWV_COMMAND_WRITE)
        {
            memset(dataBuf, 0, dataBufSize);
        }
    }
    if (device->deviceVerbosity > VERBOSITY_QUIET)
    {
        uint8_t days = 0, hours = 0, minutes = 0, seconds = 0;
        switch (testMode)
        {
        case RWV_COMMAND_READ:
            printf("Sequential Read Test at OD for ~");
            break;
        case RWV_COMMAND_WRITE:
            printf("Sequential Write Test at OD for ~");
            break;
        case RWV_COMMAND_VERIFY:
        default:
            printf("Sequential Verify Test at OD for ~");
            break;
        }
        convert_Seconds_To_Displayable_Time(timePerTestSeconds, NULL, &days, &hours, &minutes, &seconds);
        print_Time_To_Screen(NULL, &days, &hours, &minutes, &seconds);
        printf("\n");
    }
    startTime = time(NULL);
    while (difftime(time(NULL), startTime) < timePerTestSeconds && ODEndingLBA < device->drive_info.deviceMaxLba)
    {
        if (VERBOSITY_QUIET < device->deviceVerbosity)
        {
            switch (testMode)
            {
            case RWV_COMMAND_WRITE:
                printf("\rWriting LBA: %-20"PRIu64"", ODEndingLBA);//20 wide is the max width for a unsigned 64bit number
                break;
            case RWV_COMMAND_READ:
                printf("\rReading LBA: %-20"PRIu64"", ODEndingLBA);//20 wide is the max width for a unsigned 64bit number
                break;
            case RWV_COMMAND_VERIFY:
            default:
                printf("\rVerifying LBA: %-20"PRIu64"", ODEndingLBA);//20 wide is the max width for a unsigned 64bit number
                break;
            }
            fflush(stdout);
        }
        switch (read_Write_Seek_Command(device, testMode, ODEndingLBA, dataBuf, C_CAST(uint32_t, sectorCount * device->drive_info.deviceBlockSize)))
        {
        case SUCCESS:
            break;
        case COMMAND_TIMEOUT:
            (*numberOfCommandTimeouts)++;
            break;
        default:
            (*numberOfCommandFailures)++;
            break;
        }
        ODEndingLBA += sectorCount;
    }
    if (device->deviceVerbosity > VERBOSITY_QUIET)
    {
        printf("\n");
    }
    //ID
    if (device->deviceVerbosity > VERBOSITY_QUIET)
    {
        uint8_t days = 0, hours = 0, minutes = 0, seconds = 0;
        switch (testMode)
        {
        case RWV_COMMAND_READ:
            printf("Sequential Read Test at ID for ~");
            break;
        case RWV_COMMAND_WRITE:
            printf("Sequential Write Test at ID for ~");
            break;
        case RWV_COMMAND_VERIFY:
        default:
            printf("Sequential Verify Test at ID for ~");
            break;
        }
        convert_Seconds_To_Displayable_Time(timePerTestSeconds, NULL, &days, &hours, &minutes, &seconds);
        print_Time_To_Screen(NULL, &days, &hours, &minutes, &seconds);
        printf("\n");
    }
    IDStartLBA = device->drive_info.deviceMaxLba - ODEndingLBA;
    startTime = time(NULL);
    while (difftime(time(NULL), startTime) < timePerTestSeconds && IDStartLBA < device->drive_info.deviceMaxLba)
    {
        if (VERBOSITY_QUIET < device->deviceVerbosity)
        {
            switch (testMode)
            {
            case RWV_COMMAND_WRITE:
                printf("\rWriting LBA: %-20"PRIu64"", IDStartLBA);//20 wide is the max width for a unsigned 64bit number
                break;
            case RWV_COMMAND_READ:
                printf("\rReading LBA: %-20"PRIu64"", IDStartLBA);//20 wide is the max width for a unsigned 64bit number
                break;
            case RWV_COMMAND_VERIFY:
            default:
                printf("\rVerifying LBA: %-20"PRIu64"", IDStartLBA);//20 wide is the max width for a unsigned 64bit number
                break;
            }
            fflush(stdout);
        }
        switch (read_Write_Seek_Command(device, testMode, IDStartLBA, dataBuf, C_CAST(uint32_t, sectorCount * device->drive_info.deviceBlockSize)))
        {
        case SUCCESS:
            break;
        case COMMAND_TIMEOUT:
            (*numberOfCommandTimeouts)++;
            break;
        default:
            (*numberOfCommandFailures)++;
            break;
        }
        IDStartLBA += sectorCount;
    }
    if (device->deviceVerbosity > VERBOSITY_QUIET)
    {
        printf("\n");
    }
    //Random
    seed_64(time(NULL));//start random number generator
    if (device->deviceVerbosity > VERBOSITY_QUIET)
    {
        uint8_t days = 0, hours = 0, minutes = 0, seconds = 0;
        switch (testMode)
        {
        case RWV_COMMAND_READ:
            printf("Random Read Test for ~");
            break;
        case RWV_COMMAND_WRITE:
            printf("Random Write Test for ~");
            break;
        case RWV_COMMAND_VERIFY:
        default:
            printf("Random Verify Test for ~");
            break;
        }
        convert_Seconds_To_Displayable_Time(timePerTestSeconds, NULL, &days, &hours, &minutes, &seconds);
        print_Time_To_Screen(NULL, &days, &hours, &minutes, &seconds);
        printf("\n");
    }
    startTime = time(NULL);
    while (difftime(time(NULL), startTime) < timePerTestSeconds)
    {
        randomLBA = random_Range_64(0, device->drive_info.deviceMaxLba);
        if (VERBOSITY_QUIET < device->deviceVerbosity)
        {
            switch (testMode)
            {
            case RWV_COMMAND_WRITE:
                printf("\rWriting LBA: %-20"PRIu64"", randomLBA);//20 wide is the max width for a unsigned 64bit number
                break;
            case RWV_COMMAND_READ:
                printf("\rReading LBA: %-20"PRIu64"", randomLBA);//20 wide is the max width for a unsigned 64bit number
                break;
            case RWV_COMMAND_VERIFY:
            default:
                printf("\rVerifying LBA: %-20"PRIu64"", randomLBA);//20 wide is the max width for a unsigned 64bit number
                break;
            }
            fflush(stdout);
        }
        switch (read_Write_Seek_Command(device, testMode, randomLBA, dataBuf, C_CAST(uint32_t, 1 * device->drive_info.deviceBlockSize)))
        {
        case SUCCESS:
            break;
        case COMMAND_TIMEOUT:
            (*numberOfCommandTimeouts)++;
            break;
        default:
            (*numberOfCommandFailures)++;
            break;
        }
    }
    if (device->deviceVerbosity > VERBOSITY_QUIET)
    {
        printf("\n");
    }
    //Butterfly
    //outerLBA = ODEndingLBA;
    innerLBA -= sectorCount;// -ODEndingLBA;
    if (device->deviceVerbosity > VERBOSITY_QUIET)
    {
        uint8_t days = 0, hours = 0, minutes = 0, seconds = 0;
        switch (testMode)
        {
        case RWV_COMMAND_READ:
            printf("Butterfly Read Test for ~");
            break;
        case RWV_COMMAND_WRITE:
            printf("Butterfly Write Test for ~");
            break;
        case RWV_COMMAND_VERIFY:
        default:
            printf("Butterfly Verify Test for ~");
            break;
        }
        convert_Seconds_To_Displayable_Time(timePerTestSeconds, NULL, &days, &hours, &minutes, &seconds);
        print_Time_To_Screen(NULL, &days, &hours, &minutes, &seconds);
        printf("\n");
    }
    currentSectorCount = sectorCount = get_Sector_Count_For_Read_Write(device);
    startTime = time(NULL);
    while (difftime(time(NULL), startTime) < timePerTestSeconds)
    {
        //read the outer lba
        if ((outerLBA + sectorCount) > device->drive_info.deviceMaxLba)
        {
            //adjust the sector count to get to the maxLBA for the read
            currentSectorCount = C_CAST(uint32_t, device->drive_info.deviceMaxLba - outerLBA);
        }
        if (VERBOSITY_QUIET < device->deviceVerbosity)
        {
            switch (testMode)
            {
            case RWV_COMMAND_WRITE:
                printf("\rWriting LBA: %-20"PRIu64"", outerLBA);//20 wide is the max width for a unsigned 64bit number
                break;
            case RWV_COMMAND_READ:
                printf("\rReading LBA: %-20"PRIu64"", outerLBA);//20 wide is the max width for a unsigned 64bit number
                break;
            case RWV_COMMAND_VERIFY:
            default:
                printf("\rVerifying LBA: %-20"PRIu64"", outerLBA);//20 wide is the max width for a unsigned 64bit number
                break;
            }
            fflush(stdout);
        }
        switch (read_Write_Seek_Command(device, testMode, outerLBA, dataBuf, C_CAST(uint32_t, currentSectorCount * device->drive_info.deviceBlockSize)))
        {
        case SUCCESS:
            break;
        case COMMAND_TIMEOUT:
            (*numberOfCommandTimeouts)++;
            break;
        default:
            (*numberOfCommandFailures)++;
            break;
        }
        outerLBA += currentSectorCount;
        if (outerLBA >= device->drive_info.deviceMaxLba)//reset back to lba 0
        {
            outerLBA = 0;
        }
        //read the inner lba
        if (C_CAST(int64_t, innerLBA - sectorCount) < 0)
        {
            //adjust the sector count to get to 0 for the read
            currentSectorCount = C_CAST(uint32_t, innerLBA);//this should set us up to read the remaining sectors to 0
        }
        if (VERBOSITY_QUIET < device->deviceVerbosity)
        {
            switch (testMode)
            {
            case RWV_COMMAND_WRITE:
                printf("\rWriting LBA: %-20"PRIu64"", innerLBA);//20 wide is the max width for a unsigned 64bit number
                break;
            case RWV_COMMAND_READ:
                printf("\rReading LBA: %-20"PRIu64"", innerLBA);//20 wide is the max width for a unsigned 64bit number
                break;
            case RWV_COMMAND_VERIFY:
            default:
                printf("\rVerifying LBA: %-20"PRIu64"", innerLBA);//20 wide is the max width for a unsigned 64bit number
                break;
            }
            fflush(stdout);
        }
        switch (read_Write_Seek_Command(device, testMode, innerLBA, dataBuf, C_CAST(uint32_t, currentSectorCount * device->drive_info.deviceBlockSize)))
        {
        case SUCCESS:
            break;
        case COMMAND_TIMEOUT:
            (*numberOfCommandTimeouts)++;
            break;
        default:
            (*numberOfCommandFailures)++;
            break;
        }
        //always adjust the innerLBA
        innerLBA -= currentSectorCount;
        if (innerLBA == 0)//time to reset to the maxLBA
        {
            innerLBA = device->drive_info.deviceMaxLba - sectorCount;
        }
        //set the sector count back to what it was before the next iteration in the loop
        if (currentSectorCount != sectorCount)
        {
            currentSectorCount = sectorCount;
        }
    }
    if (device->deviceVerbosity > VERBOSITY_QUIET)
    {
        printf("\n");
    }
    safe_Free(dataBuf)
    return SUCCESS;
}

//This function is very similar to the "user_Sequential_Test" call, but the error list is allocated outside of this function instead of having it self containted.
//Rather than change the user_Sequential_Test and make it potentially break others or complicated its already long list of parameters, I wrote this function instead.
int diamter_Test_RWV_Range(tDevice *device, eRWVCommandType rwvCommand, uint64_t startingLBA, uint64_t range, uint16_t errorLimit, errorLBA *errorList, uint16_t *errorOffset, bool stopOnError, bool repairOnTheFly, custom_Update updateFunction, void *updateData, bool hideLBACounter)
{
    int ret = SUCCESS;
    bool errorLimitReached = false;
    uint32_t sectorCount = get_Sector_Count_For_Read_Write(device);
    uint64_t originalStartingLBA = startingLBA;
    uint64_t originalRange = range;
    //only one of these flags should be set. If they are both set, this makes no sense
    if (stopOnError)
    {
        //disable the repair flag
        repairOnTheFly = false;
    }
    if (errorLimit < 1)
    {
        //need to be able to store at least 1 error
        errorLimit = 1;
    }
    if (!errorList)
    {
        return BAD_PARAMETER;
    }
    errorList[*errorOffset].errorAddress = UINT64_MAX;
    //TODO: make sure the starting LBA is alligned? If we do this, we need to make sure we don't mess with the data of the LBAs we don't mean to start at...mostly don't want to erase an LBA we shouldn't be starting at.
    //startingLBA = align_LBA(device, startingLBA);
    bool autoReadReassign = false;
    bool autoWriteReassign = false;
    if (SUCCESS != get_Automatic_Reallocation_Support(device, &autoWriteReassign, &autoReadReassign))
    {
        autoWriteReassign = true;//just in case this fails, default to previous behavior
    }
    //this is escentially a loop over the sequential read function
    while (!errorLimitReached)
    {
        if (SUCCESS != sequential_RWV(device, rwvCommand, startingLBA, range, sectorCount, &errorList[*errorOffset].errorAddress, updateFunction, updateData, hideLBACounter))
        {
            if (device->deviceVerbosity > VERBOSITY_QUIET)
            {
                printf("\nError Found at LBA %"PRIu64"\n", errorList[*errorOffset].errorAddress);
            }
            //set a new start for next time through the loop to 1 lba past the last error LBA
            startingLBA = errorList[*errorOffset].errorAddress + 1;
            if (stopOnError || *errorOffset >= errorLimit)
            {
                errorLimitReached = true;
                ret = FAILURE;
            }
            if (repairOnTheFly)
            {
                repair_LBA(device, &errorList[*errorOffset], false, autoWriteReassign, autoWriteReassign);
            }
            (*errorOffset)++;
            if (startingLBA > (originalStartingLBA + originalRange))
            {
                break;
            }
            //need to adjust the range after we hit an error lba!
            range = (originalStartingLBA + originalRange) - startingLBA;
        }
        else
        {
            break;
        }
    }
    if (device->deviceVerbosity > VERBOSITY_QUIET)
    {
        printf("\n");
    }
    
    return ret;
}

//tests at OD, MD, and/or ID depending on what the caller requests.
int diameter_Test_Range(tDevice *device, eRWVCommandType testMode, bool outer, bool middle, bool inner, uint64_t numberOfLBAs, uint16_t errorLimit, bool stopOnError, bool repairOnTheFly, bool repairAtEnd, custom_Update updateFunction, void *updateData, bool hideLBACounter)
{
    int ret = SUCCESS, outerRet = SUCCESS, innerRet = SUCCESS, middleRet = SUCCESS;
    if ((repairOnTheFly && repairAtEnd) || errorLimit == 0)
    {
        return BAD_PARAMETER;
    }
    //int user_Sequential_Test(tDevice *device, eRWVCommandType rwvCommand, uint64_t startingLBA, uint64_t range, uint16_t errorLimit, bool stopOnError, bool repairOnTheFly, bool repairAtEnd, custom_Update updateFunction, void *updateData)
    errorLBA *errorList = C_CAST(errorLBA*, calloc(errorLimit * sizeof(errorLBA), sizeof(errorLBA)));
    uint16_t errorOffset = 0;

    //OD
    if (outer && (ret == SUCCESS || (errorOffset < errorLimit && !stopOnError)))
    {
        if (device->deviceVerbosity > VERBOSITY_QUIET)
        {
            printf("Outer Diameter Test\n");
        }
        outerRet = diamter_Test_RWV_Range(device, testMode, 0, numberOfLBAs, errorLimit, errorList, &errorOffset, stopOnError, repairOnTheFly, updateFunction, updateData, hideLBACounter);
        if (device->deviceVerbosity > VERBOSITY_QUIET)
        {
            printf("\n");
        }
    }
    if(outerRet != SUCCESS && ret == SUCCESS)
    {
        ret = outerRet;
    }
    //MD
    if (middle && (ret == SUCCESS || (errorOffset < errorLimit && !stopOnError)))
    {
        if (device->deviceVerbosity > VERBOSITY_QUIET)
        {
            printf("Middle Diameter Test\n");
        }
        middleRet = diamter_Test_RWV_Range(device, testMode, device->drive_info.deviceMaxLba / 2, numberOfLBAs, errorLimit, errorList, &errorOffset, stopOnError, repairOnTheFly, updateFunction, updateData, hideLBACounter);
        if (device->deviceVerbosity > VERBOSITY_QUIET)
        {
            printf("\n");
        }
    }
    if(middleRet != SUCCESS && ret == SUCCESS)
    {
        ret = middleRet;
    }
    //ID
    if (inner && (ret == SUCCESS || (errorOffset < errorLimit && !stopOnError)))
    {
        if (device->deviceVerbosity > VERBOSITY_QUIET)
        {
            printf("Inner Diameter Test\n");
        }
        innerRet = diamter_Test_RWV_Range(device, testMode, device->drive_info.deviceMaxLba - numberOfLBAs + 1, numberOfLBAs, errorLimit, errorList, &errorOffset, stopOnError, repairOnTheFly, updateFunction, updateData, hideLBACounter);
        if (device->deviceVerbosity > VERBOSITY_QUIET)
        {
            printf("\n");
        }
    }
    if(innerRet != SUCCESS && ret == SUCCESS)
    {
        ret = innerRet;
    }
    //handle repair at end if it was set
    if (repairAtEnd)
    {
        bool autoReadReassign = false;
        bool autoWriteReassign = false;
        if (SUCCESS != get_Automatic_Reallocation_Support(device, &autoWriteReassign, &autoReadReassign))
        {
            autoWriteReassign = true;//just in case this fails, default to previous behavior
        }
        //go through and repair the LBAs
        uint64_t errorIter = 0;
        uint64_t lastLBARepaired = UINT64_MAX;
        uint16_t logicalPerPhysicalSectors = C_CAST(uint16_t, device->drive_info.devicePhyBlockSize / device->drive_info.deviceBlockSize);
        for (errorIter = 0; errorIter < errorOffset; errorIter++)
        {
            if (lastLBARepaired != UINT64_MAX)
            {
                //check if the LBA we want to repair is within the same physical sector as the last LBA
                if ((lastLBARepaired + logicalPerPhysicalSectors) > errorList[errorIter].errorAddress)
                {
                    //in this case, we have already repaired this LBA since the repair is issued to the physical sector, so move on to the next thing in the list
                    errorList[errorIter].repairStatus = REPAIR_NOT_REQUIRED;
                    continue;
                }
            }
            if (SUCCESS == repair_LBA(device, &errorList[errorIter], false, autoWriteReassign, autoReadReassign))
            {
                lastLBARepaired = errorList[errorIter].errorAddress;
            }
        }
    }
    //handle stopping on the error we got
    if (stopOnError && errorList[0].errorAddress != UINT64_MAX)
    {
        if (device->deviceVerbosity > VERBOSITY_QUIET)
        {
            printf("\nError occured at LBA %"PRIu64"\n", errorList[0].errorAddress);
        }
    }
    else
    {
        if (device->deviceVerbosity > VERBOSITY_QUIET)
        {
            if (errorList[0].errorAddress != UINT64_MAX)
            {
                print_LBA_Error_List(errorList, errorOffset);
            }
            else
            {
                printf("No bad LBAs detected during read scan of device.\n");
            }
        }
    }
    safe_Free(errorList)
    return ret;
}

//this function is similar to the range function, but looks for a time limit to run for instead.
int diamter_Test_RWV_Time(tDevice *device, eRWVCommandType rwvCommand, uint64_t startingLBA, uint64_t timeInSeconds, uint16_t errorLimit, errorLBA *errorList, uint16_t *errorOffset, bool stopOnError, bool repairOnTheFly, uint64_t *numberOfLbasAccessed, bool hideLBACounter)
{
    int ret = SUCCESS;
    bool errorLimitReached = false;
    uint32_t sectorCount = get_Sector_Count_For_Read_Write(device);
    uint8_t *dataBuf = NULL;
    size_t dataBufSize = 0;
    if (numberOfLbasAccessed)
    {
        *numberOfLbasAccessed = startingLBA;
    }
    //only one of these flags should be set. If they are both set, this makes no sense
    if (stopOnError)
    {
        //disable the repair flag
        repairOnTheFly = false;
    }
    if (errorLimit < 1)
    {
        //need to be able to store at least 1 error
        errorLimit = 1;
    }
    if (!errorList)
    {
        return BAD_PARAMETER;
    }
    if (rwvCommand == RWV_COMMAND_READ || rwvCommand == RWV_COMMAND_WRITE)
    {
        //allocate memory
        dataBufSize = C_CAST(size_t, device->drive_info.deviceBlockSize) * C_CAST(size_t, sectorCount);
        dataBuf = C_CAST(uint8_t*, calloc_aligned(dataBufSize, sizeof(uint8_t), device->os_info.minimumAlignment));
        if (!dataBuf)
        {
            perror("failed to allocate memory!\n");
            return MEMORY_FAILURE;
        }
    }
    errorList[*errorOffset].errorAddress = UINT64_MAX;
    //TODO: make sure the starting LBA is alligned? If we do this, we need to make sure we don't mess with the data of the LBAs we don't mean to start at...mostly don't want to erase an LBA we shouldn't be starting at.
    //startingLBA = align_LBA(device, startingLBA);
    bool autoReadReassign = false;
    bool autoWriteReassign = false;
    if (SUCCESS != get_Automatic_Reallocation_Support(device, &autoWriteReassign, &autoReadReassign))
    {
        autoWriteReassign = true;//just in case this fails, default to previous behavior
    }
    //this is escentially a loop over the sequential read function
    time_t startTime = time(NULL);
    while (!errorLimitReached && difftime(time(NULL), startTime) < timeInSeconds && startingLBA < device->drive_info.deviceMaxLba)
    {
        if ((startingLBA + sectorCount) > device->drive_info.deviceMaxLba)
        {
            sectorCount = C_CAST(uint32_t, device->drive_info.deviceMaxLba - startingLBA + 1);
        }
        if (VERBOSITY_QUIET < device->deviceVerbosity && !hideLBACounter)
        {
            switch (rwvCommand)
            {
            case RWV_COMMAND_WRITE:
                printf("\rWriting LBA: %-20"PRIu64"", startingLBA);//20 wide is the max width for a unsigned 64bit number
                break;
            case RWV_COMMAND_READ:
                printf("\rReading LBA: %-20"PRIu64"", startingLBA);//20 wide is the max width for a unsigned 64bit number
                break;
            case RWV_COMMAND_VERIFY:
            default:
                printf("\rVerifying LBA: %-20"PRIu64"", startingLBA);//20 wide is the max width for a unsigned 64bit number
                break;
            }
            fflush(stdout);
        }
        if (SUCCESS != read_Write_Seek_Command(device, rwvCommand, startingLBA, dataBuf, C_CAST(uint32_t, sectorCount * device->drive_info.deviceBlockSize)))
        {
            uint64_t maxSingleLoopLBA = startingLBA + sectorCount;//limits the loop to trying to only a certain number of sectors without getting stuck at single LBA reads.
            //read command failure...so we need to read until we find the exact failing lba
            for (; startingLBA <= maxSingleLoopLBA; startingLBA += 1)
            {
                //print out the current LBA we are rwving
                if (VERBOSITY_QUIET < device->deviceVerbosity && !hideLBACounter)
                {
                    switch (rwvCommand)
                    {
                    case RWV_COMMAND_WRITE: 
                        printf("\rWriting LBA: %-20"PRIu64"", startingLBA);//20 wide is the max width for a unsigned 64bit number
                        break;
                    case RWV_COMMAND_READ:
                        printf("\rReading LBA: %-20"PRIu64"", startingLBA);//20 wide is the max width for a unsigned 64bit number
                        break;
                    case RWV_COMMAND_VERIFY:
                    default:
                        printf("\rVerifying LBA: %-20"PRIu64"", startingLBA);//20 wide is the max width for a unsigned 64bit number
                        break;
                    }
                    fflush(stdout);
                }
                if (SUCCESS != read_Write_Seek_Command(device, rwvCommand, startingLBA, dataBuf, C_CAST(uint32_t, 1 * device->drive_info.deviceBlockSize)))
                {
                    errorList[*errorOffset].errorAddress = startingLBA;
                    break;
                }
            }
            if (device->deviceVerbosity > VERBOSITY_QUIET)
            {
                printf("\nError Found at LBA %"PRIu64"\n", errorList[*errorOffset].errorAddress);
            }
            //set a new start for next time through the loop to 1 lba past the last error LBA
            startingLBA = errorList[*errorOffset].errorAddress + 1;
            if (stopOnError || *errorOffset >= errorLimit)
            {
                errorLimitReached = true;
                ret = FAILURE;
            }
            if (repairOnTheFly)
            {
                repair_LBA(device, &errorList[*errorOffset], false, autoWriteReassign, autoReadReassign);
            }
            (*errorOffset)++;
            continue;//continuing here since startingLBA will get incremented beyond the error so we pick up where we left off.
        }
        startingLBA += sectorCount;
        
    }
    if (VERBOSITY_QUIET < device->deviceVerbosity && !hideLBACounter)
    {
        switch (rwvCommand)
        {
        case RWV_COMMAND_WRITE:
            printf("\rWriting LBA: %-20"PRIu64"", startingLBA);//20 wide is the max width for a unsigned 64bit number
            break;
        case RWV_COMMAND_READ:
            printf("\rReading LBA: %-20"PRIu64"", startingLBA);//20 wide is the max width for a unsigned 64bit number
            break;
        case RWV_COMMAND_VERIFY:
        default:
            printf("\rVerifying LBA: %-20"PRIu64"", startingLBA);//20 wide is the max width for a unsigned 64bit number
            break;
        }
        printf("\n");
        fflush(stdout);
    }
    if (numberOfLbasAccessed)
    {
        *numberOfLbasAccessed = startingLBA + sectorCount - *numberOfLbasAccessed;//subtract itself since it gets set to where we start at when we begin.
    }
    safe_Free_aligned(dataBuf)
    return ret;
}

int diameter_Test_Time(tDevice *device, eRWVCommandType testMode, bool outer, bool middle, bool inner, uint64_t timeInSecondsPerDiameter, uint16_t errorLimit, bool stopOnError, bool repairOnTheFly, bool repairAtEnd, bool hideLBACounter)
{
    int ret = SUCCESS, outerRet = SUCCESS, middleRet = SUCCESS, innerRet = SUCCESS;
    if ((repairOnTheFly && repairAtEnd) || errorLimit == 0)
    {
        return BAD_PARAMETER;
    }
    //int user_Sequential_Test(tDevice *device, eRWVCommandType rwvCommand, uint64_t startingLBA, uint64_t range, uint16_t errorLimit, bool stopOnError, bool repairOnTheFly, bool repairAtEnd, custom_Update updateFunction, void *updateData)
    errorLBA *errorList = C_CAST(errorLBA*, calloc(errorLimit * sizeof(errorLBA), sizeof(errorLBA)));
    uint16_t errorOffset = 0;
    uint64_t odOrMdLBAsAccessed = 0;
    uint8_t days = 0, hours = 0, minutes = 0, seconds = 0;
    convert_Seconds_To_Displayable_Time(timeInSecondsPerDiameter, NULL, &days, &hours, &minutes, &seconds);

    //OD
    if (outer && (ret == SUCCESS || (errorOffset < errorLimit && !stopOnError)))
    {
        if (device->deviceVerbosity > VERBOSITY_QUIET)
        {
            printf("Outer Diameter Test for");
            print_Time_To_Screen(NULL, &days, &hours, &minutes, &seconds);
            printf("\n");
        }
        outerRet = diamter_Test_RWV_Time(device, testMode, 0, timeInSecondsPerDiameter, errorLimit, errorList, &errorOffset, stopOnError, repairOnTheFly, &odOrMdLBAsAccessed, hideLBACounter);
        if (device->deviceVerbosity > VERBOSITY_QUIET)
        {
            printf("\n");
        }
    }
    if(outerRet != SUCCESS && ret == SUCCESS)
    {
        ret = outerRet;
    }
    //MD
    if (middle && (ret == SUCCESS || (errorOffset < errorLimit && !stopOnError)))
    {
        uint64_t mdLBAsAccessed = 0;
        uint64_t *countPointer = &mdLBAsAccessed;
        if (device->deviceVerbosity > VERBOSITY_QUIET)
        {
            printf("Middle Diameter Test for");
            print_Time_To_Screen(NULL, &days, &hours, &minutes, &seconds);
            printf("\n");
        }
        if (odOrMdLBAsAccessed == 0)
        {
            countPointer = &odOrMdLBAsAccessed;
        }
        middleRet = diamter_Test_RWV_Time(device, testMode, device->drive_info.deviceMaxLba / 2, timeInSecondsPerDiameter, errorLimit, errorList, &errorOffset, stopOnError, repairOnTheFly, countPointer, hideLBACounter);
        if (device->deviceVerbosity > VERBOSITY_QUIET)
        {
            printf("\n");
        }
        odOrMdLBAsAccessed = (odOrMdLBAsAccessed > mdLBAsAccessed) ? odOrMdLBAsAccessed : mdLBAsAccessed;//get the higher of these two counts.
    }
    if(middleRet != SUCCESS && ret == SUCCESS)
    {
        ret = middleRet;
    }
    //ID
    if (inner && (ret == SUCCESS || (errorOffset < errorLimit && !stopOnError)))
    {
        uint64_t idStartingLBA = device->drive_info.deviceMaxLba - odOrMdLBAsAccessed;
        if (idStartingLBA == device->drive_info.deviceMaxLba)
        {
            //TODO: this guestimate can be improved by reading the negotiated interface speed and using that as a maximum performance to get the gestimate closer to the best place to start the ID scan...this would work at least on ATA and SCSI well...USB would be more difficult to guess properly.
            //need to make a guess based on the amount of time we're running where we'll start...let's assume the worse case of an accessing at 550MB/s (max 6Gb/s transfer an SSD on SATA can get...shouldn't happen)
            idStartingLBA = device->drive_info.deviceMaxLba - (((550 /*megabytes per second*/ * timeInSecondsPerDiameter) /*now convert to Bytes*/ * 1000000) /*now convert to LBAs*/ / device->drive_info.deviceBlockSize);
        }
        if (device->deviceVerbosity > VERBOSITY_QUIET)
        {
            printf("Inner Diameter Test for");
            print_Time_To_Screen(NULL, &days, &hours, &minutes, &seconds);
            printf("\n");
        }
        innerRet = diamter_Test_RWV_Time(device, testMode, idStartingLBA, timeInSecondsPerDiameter, errorLimit, errorList, &errorOffset, stopOnError, repairOnTheFly, NULL, hideLBACounter);
        if (device->deviceVerbosity > VERBOSITY_QUIET)
        {
            printf("\n");
        }
    }
    if(innerRet != SUCCESS && ret == SUCCESS)
    {
        ret = innerRet;
    }
    //handle repair at end if it was set
    if (repairAtEnd)
    {
        bool autoReadReassign = false;
        bool autoWriteReassign = false;
        if (SUCCESS != get_Automatic_Reallocation_Support(device, &autoWriteReassign, &autoReadReassign))
        {
            autoWriteReassign = true;//just in case this fails, default to previous behavior
        }
        //go through and repair the LBAs
        uint64_t errorIter = 0;
        uint64_t lastLBARepaired = UINT64_MAX;
        uint16_t logicalPerPhysicalSectors = C_CAST(uint16_t, device->drive_info.devicePhyBlockSize / device->drive_info.deviceBlockSize);
        for (errorIter = 0; errorIter < errorOffset; errorIter++)
        {
            if (lastLBARepaired != UINT64_MAX)
            {
                //check if the LBA we want to repair is within the same physical sector as the last LBA
                if ((lastLBARepaired + logicalPerPhysicalSectors) > errorList[errorIter].errorAddress)
                {
                    //in this case, we have already repaired this LBA since the repair is issued to the physical sector, so move on to the next thing in the list
                    errorList[errorIter].repairStatus = REPAIR_NOT_REQUIRED;
                    continue;
                }
            }
            if (SUCCESS == repair_LBA(device, &errorList[errorIter], false, autoWriteReassign, autoReadReassign))
            {
                errorList[errorIter].repairStatus = REPAIRED;
                lastLBARepaired = errorList[errorIter].errorAddress;
            }
            else
            {
                errorList[errorIter].repairStatus = REPAIR_FAILED;
            }
        }
    }
    //handle stopping on the error we got
    if (stopOnError && errorList[0].errorAddress != UINT64_MAX)
    {
        if (device->deviceVerbosity > VERBOSITY_QUIET)
        {
            printf("\nError occured at LBA %"PRIu64"\n", errorList[0].errorAddress);
        }
    }
    else
    {
        if (device->deviceVerbosity > VERBOSITY_QUIET)
        {
            if (errorList[0].errorAddress != UINT64_MAX)
            {
                print_LBA_Error_List(errorList, errorOffset);
            }
            else
            {
                printf("No bad LBAs detected during read scan of device.\n");
            }
        }
    }
    safe_Free(errorList)
    return ret;
}
