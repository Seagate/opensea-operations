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
// \file generic_tests.c
// \brief This file defines the functions for generic read tests

#include "bit_manip.h"
#include "code_attributes.h"
#include "common_types.h"
#include "error_translation.h"
#include "io_utils.h"
#include "math_utils.h"
#include "memory_safety.h"
#include "precision_timer.h"
#include "prng.h"
#include "string_utils.h"
#include "time_utils.h"
#include "type_conversion.h"
#include "unit_conversion.h"

#include "cmds.h"
#include "generic_tests.h"
#include "operations.h"
#include "sector_repair.h"

M_PARAM_RO(1)
M_NONNULL_IF_NONZERO_PARAM(4, 5)
M_PARAM_RW_SIZE(4, 5)
OPENSEA_OPERATIONS_API eReturnValues read_Write_Seek_Command(const tDevice* M_NONNULL device,
                                                             eRWVCommandType          rwvCommand,
                                                             uint64_t                 lba,
                                                             uint8_t* M_NULLABLE      ptrData,
                                                             uint32_t                 dataSize)
{
    switch (rwvCommand)
    {
    case RWV_COMMAND_READ:
        return read_LBA(device, lba, false, ptrData, dataSize);
    case RWV_COMMAND_WRITE:
        return write_LBA(device, lba, false, ptrData, dataSize);
    case RWV_COMMAND_VERIFY:
    default:
        return verify_LBA(device, lba, dataSize / get_Device_BlockSize(device));
    }
}

M_PARAM_RO(1)
M_PARAM_WO(6)
OPENSEA_OPERATIONS_API eReturnValues sequential_RWV(const tDevice* M_NONNULL device,
                                                    eRWVCommandType          rwvCommand,
                                                    uint64_t                 startingLBA,
                                                    uint64_t                 range,
                                                    uint64_t                 sectorCount,
                                                    uint64_t* M_NONNULL      failingLBA,
                                                    custom_Update M_NULLABLE updateFunction,
                                                    void* M_NULLABLE         updateData,
                                                    bool                     hideLBACounter)
{
    eReturnValues ret              = SUCCESS;
    uint64_t      lbaIter          = startingLBA;
    uint64_t      maxSequentialLBA = startingLBA + range;
    if (maxSequentialLBA >= return_Device_MaxLba(device))
    {
        maxSequentialLBA =
            return_Device_MaxLba(device) + 1; // the plus 1 here should make sure we don't go beyond the max lba
    }
    uint8_t* dataBuf = M_NULLPTR;
    if (rwvCommand != RWV_COMMAND_VERIFY)
    {
        dataBuf = M_REINTERPRET_CAST(
            uint8_t*, safe_calloc_aligned(uint64_to_sizet(sectorCount) * uint32_to_sizet(get_Device_BlockSize(device)),
                                          sizeof(uint8_t), get_Device_IO_Minimum_Alignment(device)));
        if (dataBuf == M_NULLPTR)
        {
            return MEMORY_FAILURE;
        }
    }
    if (maxSequentialLBA < startingLBA)
    {
        return BAD_PARAMETER;
    }
    *failingLBA = UINT64_MAX; // this means LBA access failed
    for (lbaIter = startingLBA; lbaIter < maxSequentialLBA; lbaIter += sectorCount)
    {
        // check that current LBA + sector count doesn't go beyond the maxLBA for the loop
        if ((lbaIter + sectorCount) > maxSequentialLBA)
        {
            uint8_t* temp = M_NULLPTR;
            // adjust the sector count to fit
            sectorCount = maxSequentialLBA - lbaIter;
            if (rwvCommand != RWV_COMMAND_VERIFY)
            {
                // reallocate the memory to be sized appropriately for this change
                temp = M_REINTERPRET_CAST(
                    uint8_t*,
                    safe_reallocf_aligned(C_CAST(void**, &dataBuf), 0,
                                          uint64_to_sizet(sectorCount) * uint32_to_sizet(get_Device_BlockSize(device)),
                                          get_Device_IO_Minimum_Alignment(device)));
                if (temp == M_NULLPTR)
                {
                    perror("memory reallocation failure");
                    return MEMORY_FAILURE;
                }
                dataBuf = temp;
                if (0 !=
                    safe_memset(
                        dataBuf,
                        uint64_to_sizet(sectorCount) * uint32_to_sizet(get_Device_BlockSize(device)) * sizeof(uint8_t),
                        0,
                        uint64_to_sizet(sectorCount) * uint32_to_sizet(get_Device_BlockSize(device)) * sizeof(uint8_t)))
                    M_UNLIKELY
                    {
                        perror("failed to set memory to zero for reallocated memory buffer!\n");
                    }
            }
        }
        if (updateFunction != M_NULLPTR)
        {
            op_emit_lba_cb(updateFunction, updateData, "sequential_RWV", lbaIter,
                           rwvCommand == RWV_COMMAND_READ
                               ? "reading"
                               : (rwvCommand == RWV_COMMAND_WRITE ? "writing" : "verifying"));
        }
        // print out the current LBA we are rwving
        if (VERBOSITY_QUIET < device->deviceVerbosity && !hideLBACounter)
        {
            switch (rwvCommand)
            {
            case RWV_COMMAND_WRITE:
                printf("\rWriting LBA: %-20" PRIu64 "", lbaIter); // 20 wide is the max width for a unsigned 64bit
                                                                  // number
                break;
            case RWV_COMMAND_READ:
                printf("\rReading LBA: %-20" PRIu64 "", lbaIter); // 20 wide is the max width for a unsigned 64bit
                                                                  // number
                break;
            case RWV_COMMAND_VERIFY:
            default:
                printf("\rVerifying LBA: %-20" PRIu64 "",
                       lbaIter); // 20 wide is the max width for a unsigned 64bit number
                break;
            }
            flush_stdout();
        }
        // rwv the lba
        if (SUCCESS != read_Write_Seek_Command(device, rwvCommand, lbaIter, dataBuf,
                                               C_CAST(uint32_t, sectorCount* get_Device_BlockSize(device))))
        {
            bool     errorFound       = false;
            uint64_t maxSingleLoopLBA = lbaIter + sectorCount; // limits the loop to trying to only a certain number of
                                                               // sectors without getting stuck at single LBA reads.
            uint16_t logicalPerPhysical = get_Logical_Sectors_Per_Physical_Sector(device);
            // read command failure...so we need to read until we find the exact failing lba
            for (; lbaIter <= maxSingleLoopLBA; lbaIter += logicalPerPhysical)
            {
                if (updateFunction != M_NULLPTR)
                {
                    op_emit_lba_cb(updateFunction, updateData, "sequential_RWV", lbaIter,
                                   rwvCommand == RWV_COMMAND_READ
                                       ? "reading"
                                       : (rwvCommand == RWV_COMMAND_WRITE ? "writing" : "verifying"));
                }
                // print out the current LBA we are rwving
                if (VERBOSITY_QUIET < device->deviceVerbosity && !hideLBACounter)
                {
                    switch (rwvCommand)
                    {
                    case RWV_COMMAND_WRITE:
                        printf("\rWriting LBA: %-20" PRIu64 "",
                               lbaIter); // 20 wide is the max width for a unsigned 64bit number
                        break;
                    case RWV_COMMAND_READ:
                        printf("\rReading LBA: %-20" PRIu64 "",
                               lbaIter); // 20 wide is the max width for a unsigned 64bit number
                        break;
                    case RWV_COMMAND_VERIFY:
                    default:
                        printf("\rVerifying LBA: %-20" PRIu64 "",
                               lbaIter); // 20 wide is the max width for a unsigned 64bit number
                        break;
                    }
                    flush_stdout();
                }
                if (SUCCESS != read_Write_Seek_Command(device, rwvCommand, lbaIter, dataBuf,
                                                       C_CAST(uint32_t, 1 * get_Device_BlockSize(device))))
                {
                    *failingLBA = lbaIter;
                    if (updateFunction != M_NULLPTR)
                    {
                        op_emit_error_lba_cb(updateFunction, updateData, "sequential_RWV", *failingLBA,
                                             M_STATIC_CAST(int, FAILURE), "LBA access failed");
                    }

                    ret        = FAILURE;
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
    if (updateFunction != M_NULLPTR)
    {
        op_emit_lba_cb(updateFunction, updateData, "sequential_RWV", lbaIter,
                       rwvCommand == RWV_COMMAND_READ ? "reading"
                                                      : (rwvCommand == RWV_COMMAND_WRITE ? "writing" : "verifying"));
    }
    // print out the current LBA we are rwving AND it is not greater than MaxLBA
    if (VERBOSITY_QUIET < device->deviceVerbosity && lbaIter < return_Device_MaxLba(device) && !hideLBACounter)
    {
        switch (rwvCommand)
        {
        case RWV_COMMAND_WRITE:
            printf("\rWriting LBA: %-20" PRIu64 "", lbaIter); // 20 wide is the max width for a unsigned 64bit number
            break;
        case RWV_COMMAND_READ:
            printf("\rReading LBA: %-20" PRIu64 "", lbaIter); // 20 wide is the max width for a unsigned 64bit number
            break;
        case RWV_COMMAND_VERIFY:
        default:
            printf("\rVerifying LBA: %-20" PRIu64 "", lbaIter); // 20 wide is the max width for a unsigned 64bit number
            break;
        }
        flush_stdout();
    }
    safe_free_aligned(&dataBuf);
    return ret;
}

M_PARAM_RO(1)
M_PARAM_WO(5)
OPENSEA_OPERATIONS_API eReturnValues sequential_Read(const tDevice* M_NONNULL device,
                                                     uint64_t                 startingLBA,
                                                     uint64_t                 range,
                                                     uint64_t                 sectorCount,
                                                     uint64_t* M_NONNULL      failingLBA,
                                                     custom_Update M_NULLABLE updateFunction,
                                                     void* M_NULLABLE         updateData,
                                                     bool                     hideLBACounter)
{
    return sequential_RWV(device, RWV_COMMAND_READ, startingLBA, range, sectorCount, failingLBA, updateFunction,
                          updateData, hideLBACounter);
}

M_PARAM_RO(1)
M_PARAM_WO(5)
OPENSEA_OPERATIONS_API eReturnValues sequential_Write(const tDevice* M_NONNULL device,
                                                      uint64_t                 startingLBA,
                                                      uint64_t                 range,
                                                      uint64_t                 sectorCount,
                                                      uint64_t* M_NONNULL      failingLBA,
                                                      custom_Update M_NULLABLE updateFunction,
                                                      void* M_NULLABLE         updateData,
                                                      bool                     hideLBACounter)
{
    return sequential_RWV(device, RWV_COMMAND_WRITE, startingLBA, range, sectorCount, failingLBA, updateFunction,
                          updateData, hideLBACounter);
}

M_PARAM_RO(1)
M_PARAM_WO(5)
OPENSEA_OPERATIONS_API eReturnValues sequential_Verify(const tDevice* M_NONNULL device,
                                                       uint64_t                 startingLBA,
                                                       uint64_t                 range,
                                                       uint64_t                 sectorCount,
                                                       uint64_t* M_NONNULL      failingLBA,
                                                       custom_Update M_NULLABLE updateFunction,
                                                       void* M_NULLABLE         updateData,
                                                       bool                     hideLBACounter)
{
    return sequential_RWV(device, RWV_COMMAND_VERIFY, startingLBA, range, sectorCount, failingLBA, updateFunction,
                          updateData, hideLBACounter);
}

M_PARAM_RO(1)
OPENSEA_OPERATIONS_API eReturnValues short_Generic_Read_Test(const tDevice* M_NONNULL device,
                                                             custom_Update M_NULLABLE updateFunction,
                                                             void* M_NULLABLE         updateData,
                                                             bool                     hideLBACounter)
{
    return short_Generic_Test(device, RWV_COMMAND_READ, updateFunction, updateData, hideLBACounter);
}

M_PARAM_RO(1)
OPENSEA_OPERATIONS_API eReturnValues short_Generic_Verify_Test(const tDevice* M_NONNULL device,
                                                               custom_Update M_NULLABLE updateFunction,
                                                               void* M_NULLABLE         updateData,
                                                               bool                     hideLBACounter)
{
    return short_Generic_Test(device, RWV_COMMAND_VERIFY, updateFunction, updateData, hideLBACounter);
}

M_PARAM_RO(1)
OPENSEA_OPERATIONS_API eReturnValues short_Generic_Write_Test(const tDevice* M_NONNULL device,
                                                              custom_Update M_NULLABLE updateFunction,
                                                              void* M_NULLABLE         updateData,
                                                              bool                     hideLBACounter)
{
    return short_Generic_Test(device, RWV_COMMAND_WRITE, updateFunction, updateData, hideLBACounter);
}

M_PARAM_RO(1)
OPENSEA_OPERATIONS_API eReturnValues short_Generic_Test(const tDevice* M_NONNULL device,
                                                        eRWVCommandType          rwvCommand,
                                                        custom_Update M_NULLABLE updateFunction,
                                                        void* M_NULLABLE         updateData,
                                                        bool                     hideLBACounter)
{
    int           snprintfres = 0;
    eReturnValues ret         = SUCCESS;
    DECLARE_ZERO_INIT_ARRAY(char, message, JSON_REPORT_TEST_STEP_MESSAGE_LENGTH);
    uint16_t  randomLBACount = UINT16_C(5000);
    uint64_t* randomLBAList  = M_REINTERPRET_CAST(uint64_t*, safe_calloc(randomLBACount, sizeof(uint64_t)));
    uint64_t  iterator       = UINT64_C(0);
    uint64_t  onePercentOfDrive =
        C_CAST(uint64_t, C_CAST(double, return_Device_MaxLba(device)) *
                             0.01);   // calculate how many LBAs are 1% of the drive so that we read that many
    uint8_t* dataBuf     = M_NULLPTR; // will be allocated at the random read section
    uint64_t failingLBA  = UINT64_MAX;
    uint32_t sectorCount = get_Sector_Count_For_Read_Write(device);
    if (randomLBAList == M_NULLPTR)
    {
        perror("Memory allocation failure on random LBA list\n");
        return MEMORY_FAILURE;
    }
    // start random number generator
    seed_64(C_CAST(uint64_t, time(M_NULLPTR)));
    // generate the list of random LBAs
    for (iterator = 0; iterator < randomLBACount; iterator++)
    {
        randomLBAList[iterator] = random_Range_64(0, return_Device_MaxLba(device));
    }
    // read 1% at the OD
    switch (rwvCommand)
    {
    case RWV_COMMAND_READ:
        snprintfres = snprintf_err_handle(message, JSON_REPORT_TEST_STEP_MESSAGE_LENGTH,
                                          "Sequential Read Test at OD for %" PRIu64 " LBAs", onePercentOfDrive);
        break;
    case RWV_COMMAND_VERIFY:
        snprintfres = snprintf_err_handle(message, JSON_REPORT_TEST_STEP_MESSAGE_LENGTH,
                                          "Sequential Verify Test at OD for %" PRIu64 " LBAs", onePercentOfDrive);
        break;
    case RWV_COMMAND_WRITE:
        snprintfres = snprintf_err_handle(message, JSON_REPORT_TEST_STEP_MESSAGE_LENGTH,
                                          "Sequential Write Test at OD for %" PRIu64 " LBAs", onePercentOfDrive);
        break;
    default:
        snprintfres = snprintf_err_handle(message, JSON_REPORT_TEST_STEP_MESSAGE_LENGTH,
                                          "Unknown Sequential Test at OD for %" PRIu64 " LBAs", onePercentOfDrive);
        break;
    }
    if (snprintfres >= 0)
    {
        if (updateFunction != M_NULLPTR)
        {
            op_emit_step_cb(updateFunction, updateData, "short generic_test - OD", message);
        }
        if (device->deviceVerbosity > VERBOSITY_QUIET)
        {
            print_str(message);
            print_str("\n");
        }
    }
    else
    {
        perror("Error formatting message for sequential test at OD");
    }
    if (SUCCESS != sequential_RWV(device, rwvCommand, 0, onePercentOfDrive, sectorCount, &failingLBA, M_NULLPTR,
                                  M_NULLPTR, hideLBACounter))
    {
        ret = FAILURE;
        switch (rwvCommand)
        {
        case RWV_COMMAND_READ:
            snprintfres = snprintf_err_handle(message, JSON_REPORT_TEST_STEP_MESSAGE_LENGTH,
                                              "Read failed within OD sequential read");
            break;
        case RWV_COMMAND_VERIFY:
            snprintfres = snprintf_err_handle(message, JSON_REPORT_TEST_STEP_MESSAGE_LENGTH,
                                              "Verify failed within OD sequential read");
            break;
        case RWV_COMMAND_WRITE:
            snprintfres = snprintf_err_handle(message, JSON_REPORT_TEST_STEP_MESSAGE_LENGTH,
                                              "Write failed within OD sequential read");
            break;
        default:
            snprintfres = snprintf_err_handle(message, JSON_REPORT_TEST_STEP_MESSAGE_LENGTH,
                                              "Unknown failed within OD sequential read");
            break;
        }
        if (snprintfres >= 0)
        {
            if (updateFunction != M_NULLPTR)
            {
                op_emit_lba_cb(updateFunction, updateData, "short generic_test - OD", failingLBA,
                               rwvCommand == RWV_COMMAND_READ
                                   ? "reading"
                                   : (rwvCommand == RWV_COMMAND_WRITE ? "writing" : "verifying"));
            }
            if (device->deviceVerbosity > VERBOSITY_QUIET)
            {
                print_str("\n");
                print_str(message);
                print_str("\n");
            }
        }
        else
        {
            perror("Error formatting message for sequential test failure at OD");
        }
        safe_free(&randomLBAList);
        return ret;
    }
    if (device->deviceVerbosity > VERBOSITY_QUIET)
    {
        print_str("\n");
    }
    // read 1% at the ID
    switch (rwvCommand)
    {
    case RWV_COMMAND_READ:
        snprintfres = snprintf_err_handle(message, JSON_REPORT_TEST_STEP_MESSAGE_LENGTH,
                                          "Sequential Read Test at ID for %" PRIu64 " LBAs", onePercentOfDrive);
        break;
    case RWV_COMMAND_VERIFY:
        snprintfres = snprintf_err_handle(message, JSON_REPORT_TEST_STEP_MESSAGE_LENGTH,
                                          "Sequential Verify Test at ID for %" PRIu64 " LBAs", onePercentOfDrive);
        break;
    case RWV_COMMAND_WRITE:
        snprintfres = snprintf_err_handle(message, JSON_REPORT_TEST_STEP_MESSAGE_LENGTH,
                                          "Sequential Write Test at ID for %" PRIu64 " LBAs", onePercentOfDrive);
        break;
    default:
        snprintfres = snprintf_err_handle(message, JSON_REPORT_TEST_STEP_MESSAGE_LENGTH,
                                          "Unknown Sequential Test at ID for %" PRIu64 " LBAs", onePercentOfDrive);
        break;
    }
    if (snprintfres >= 0)
    {
        if (updateFunction != M_NULLPTR)
        {
            op_emit_step_cb(updateFunction, updateData, "short generic_test - ID", message);
        }
        if (device->deviceVerbosity > VERBOSITY_QUIET)
        {
            print_str("\n");
            print_str(message);
            print_str("\n");
        }
    }
    else
    {
        perror("Error formatting message for sequential test at ID");
    }
    if (SUCCESS != sequential_RWV(device, rwvCommand, return_Device_MaxLba(device) - onePercentOfDrive,
                                  onePercentOfDrive, sectorCount, &failingLBA, M_NULLPTR, M_NULLPTR, hideLBACounter))
    {
        ret = FAILURE;
        switch (rwvCommand)
        {
        case RWV_COMMAND_READ:
            snprintfres = snprintf_err_handle(message, JSON_REPORT_TEST_STEP_MESSAGE_LENGTH,
                                              "Read failed within ID sequential read");
            break;
        case RWV_COMMAND_VERIFY:
            snprintfres = snprintf_err_handle(message, JSON_REPORT_TEST_STEP_MESSAGE_LENGTH,
                                              "Verify failed within ID sequential read");
            break;
        case RWV_COMMAND_WRITE:
            snprintfres = snprintf_err_handle(message, JSON_REPORT_TEST_STEP_MESSAGE_LENGTH,
                                              "Write failed within ID sequential read");
            break;
        default:
            snprintfres = snprintf_err_handle(message, JSON_REPORT_TEST_STEP_MESSAGE_LENGTH,
                                              "Unknown failed within ID sequential read");
            break;
        }
        if (snprintfres >= 0)
        {
            if (updateFunction != M_NULLPTR)
            {
                op_emit_lba_cb(updateFunction, updateData, "short generic_test - ID", failingLBA,
                               rwvCommand == RWV_COMMAND_READ
                                   ? "reading"
                                   : (rwvCommand == RWV_COMMAND_WRITE ? "writing" : "verifying"));
            }

            if (device->deviceVerbosity > VERBOSITY_QUIET)
            {
                print_str("\n");
                print_str(message);
                print_str("\n");
            }
        }
        else
        {
            perror("Error formatting message for sequential test failure at ID");
        }
        safe_free(&randomLBAList);
        return ret;
    }
    if (device->deviceVerbosity > VERBOSITY_QUIET)
    {
        print_str("\n");
    }
    // randomly read 50 LBAs
    switch (rwvCommand)
    {
    case RWV_COMMAND_READ:
        snprintfres =
            snprintf_err_handle(message, JSON_REPORT_TEST_STEP_MESSAGE_LENGTH, "Random Read Test of 5000 LBAs");
        break;
    case RWV_COMMAND_VERIFY:
        snprintfres =
            snprintf_err_handle(message, JSON_REPORT_TEST_STEP_MESSAGE_LENGTH, "Random Verify Test of 5000 LBAs");
        break;
    case RWV_COMMAND_WRITE:
        snprintfres =
            snprintf_err_handle(message, JSON_REPORT_TEST_STEP_MESSAGE_LENGTH, "Random Write Test of 5000 LBAs");
        break;
    default:
        snprintfres =
            snprintf_err_handle(message, JSON_REPORT_TEST_STEP_MESSAGE_LENGTH, "Random Unknown Test of 5000 LBAs");
        break;
    }
    if (snprintfres >= 0)
    {
        if (updateFunction != M_NULLPTR)
        {
            op_emit_step_cb(updateFunction, updateData, "short generic_test - random", message);
        }
        if (device->deviceVerbosity > VERBOSITY_QUIET)
        {
            print_str("\n");
            print_str(message);
            print_str("\n");
        }
    }
    else
    {
        perror("Error formatting message for random test");
    }
    if (rwvCommand != RWV_COMMAND_VERIFY)
    {
        dataBuf = M_REINTERPRET_CAST(uint8_t*, safe_malloc(get_Device_BlockSize(device) * sizeof(uint8_t)));
        if (dataBuf == M_NULLPTR)
        {
            perror("malloc data buf failed\n");
            safe_free(&randomLBAList);
            return MEMORY_FAILURE;
        }
    }
    for (iterator = 0; iterator < randomLBACount; iterator++)
    {
        switch (rwvCommand)
        {
        case RWV_COMMAND_READ:
            snprintfres =
                snprintf_err_handle(message, JSON_REPORT_TEST_STEP_MESSAGE_LENGTH, "Reading LBA: %-20" PRIu64 "",
                                    randomLBAList[iterator]); // 20 wide is the max width for a unsigned 64bit number
            break;
        case RWV_COMMAND_VERIFY:
            snprintfres =
                snprintf_err_handle(message, JSON_REPORT_TEST_STEP_MESSAGE_LENGTH, "Verify LBA: %-20" PRIu64 "",
                                    randomLBAList[iterator]); // 20 wide is the max width for a unsigned 64bit number
            break;
        case RWV_COMMAND_WRITE:
            snprintfres =
                snprintf_err_handle(message, JSON_REPORT_TEST_STEP_MESSAGE_LENGTH, "Write LBA: %-20" PRIu64 "",
                                    randomLBAList[iterator]); // 20 wide is the max width for a unsigned 64bit number
            break;
        default:
            snprintfres =
                snprintf_err_handle(message, JSON_REPORT_TEST_STEP_MESSAGE_LENGTH, "Unknown LBA: %-20" PRIu64 "",
                                    randomLBAList[iterator]); // 20 wide is the max width for a unsigned 64bit number
            break;
        }
        if (updateFunction != M_NULLPTR)
        {
            op_emit_lba_cb(updateFunction, updateData, "short generic_test - random", randomLBAList[iterator],
                           M_NULLPTR);
        }
        // print out the current LBA we are reading
        if (VERBOSITY_QUIET < device->deviceVerbosity && !hideLBACounter)
        {
            print_str("\r");
            print_str(message);
            flush_stdout();
        }

        if (SUCCESS != read_Write_Seek_Command(device, rwvCommand, randomLBAList[iterator], dataBuf,
                                               C_CAST(uint32_t, 1 * get_Device_BlockSize(device))))
        {
            switch (rwvCommand)
            {
            case RWV_COMMAND_READ:
                snprintfres =
                    snprintf_err_handle(message, JSON_REPORT_TEST_STEP_MESSAGE_LENGTH,
                                        "\nRead error occurred at LBA %-20" PRIu64 "", randomLBAList[iterator]);
                break;
            case RWV_COMMAND_VERIFY:
                snprintfres =
                    snprintf_err_handle(message, JSON_REPORT_TEST_STEP_MESSAGE_LENGTH,
                                        "\nVerify error occurred at LBA %-20" PRIu64 "", randomLBAList[iterator]);
                break;
            case RWV_COMMAND_WRITE:
                snprintfres =
                    snprintf_err_handle(message, JSON_REPORT_TEST_STEP_MESSAGE_LENGTH,
                                        "\nWrite error occurred at LBA %-20" PRIu64 "", randomLBAList[iterator]);
                break;
            default:
                snprintfres =
                    snprintf_err_handle(message, JSON_REPORT_TEST_STEP_MESSAGE_LENGTH,
                                        "\nUnknown error occurred at LBA %-20" PRIu64 "", randomLBAList[iterator]);
                break;
            }
            if (snprintfres >= 0)
            {
                if (updateFunction != M_NULLPTR)
                {
                    op_emit_error_lba_cb(updateFunction, updateData, "short generic_test - random",
                                         randomLBAList[iterator], M_STATIC_CAST(int, FAILURE), message);
                }
                if (device->deviceVerbosity > VERBOSITY_QUIET)
                {
                    print_str("\n");
                    print_str(message);
                    print_str("\n");
                }
            }
            else
            {
                perror("Error formatting message for random test failure");
            }
            ret = FAILURE;
            break;
        }
    }
    if (device->deviceVerbosity > VERBOSITY_QUIET)
    {
        print_str("\n");
    }
    safe_free(&dataBuf);
    safe_free(&randomLBAList);
    return ret;
}

M_PARAM_RO(1)
OPENSEA_OPERATIONS_API eReturnValues two_Minute_Generic_Read_Test(const tDevice* M_NONNULL device,
                                                                  custom_Update M_NULLABLE updateFunction,
                                                                  void* M_NULLABLE         updateData,
                                                                  bool                     hideLBACounter)
{
    return two_Minute_Generic_Test(device, RWV_COMMAND_READ, updateFunction, updateData, hideLBACounter);
}

M_PARAM_RO(1)
OPENSEA_OPERATIONS_API eReturnValues two_Minute_Generic_Write_Test(const tDevice* M_NONNULL device,
                                                                   custom_Update M_NULLABLE updateFunction,
                                                                   void* M_NULLABLE         updateData,
                                                                   bool                     hideLBACounter)
{
    return two_Minute_Generic_Test(device, RWV_COMMAND_WRITE, updateFunction, updateData, hideLBACounter);
}

M_PARAM_RO(1)
OPENSEA_OPERATIONS_API eReturnValues two_Minute_Generic_Verify_Test(const tDevice* M_NONNULL device,
                                                                    custom_Update M_NULLABLE updateFunction,
                                                                    void* M_NULLABLE         updateData,
                                                                    bool                     hideLBACounter)
{
    return two_Minute_Generic_Test(device, RWV_COMMAND_VERIFY, updateFunction, updateData, hideLBACounter);
}

// TODO: Move this to generic_tests.h...also, should this be a separate parameter so some of these functions so the
// caller can get this data or determine whether or not to show it?
typedef struct s_performanceNumbers
{
    bool     asyncCommandsUsed;      // False for now. Maybe allow async in the future
    uint64_t averageCommandTimeNS;   // average command time in nanoseconds
    uint64_t fastestCommandTimeNS;   // best case
    uint64_t slowestCommandTimeNS;   // worst case
    uint64_t numberOfCommandsIssued; // number of commands issued during operation
    uint64_t totalTimeNS;            // total time for an operation
    uint64_t iops;
    uint16_t sectorCount;
} performanceNumbers;

M_PARAM_RO(1)
OPENSEA_OPERATIONS_API eReturnValues two_Minute_Generic_Test(const tDevice* M_NONNULL device,
                                                             eRWVCommandType          rwvCommand,
                                                             custom_Update M_NULLABLE updateFunction,
                                                             void* M_NULLABLE         updateData,
                                                             bool                     hideLBACounter)
{
    errno_t msgerror       = 0;
    int     seqTimeStrRes  = 0;
    int     randTimeStrRes = 0;
    DECLARE_ZERO_INIT_ARRAY(char, message, JSON_REPORT_TEST_STEP_MESSAGE_LENGTH);
    DECLARE_ZERO_INIT_ARRAY(char, idodTimeString, TIME_STRING_LENGTH);
    DECLARE_ZERO_INIT_ARRAY(char, randomTimeString, TIME_STRING_LENGTH);
    eReturnValues      ret                    = SUCCESS;
    bool               showPerformanceNumbers = false; // TODO: make this a function parameter.
    size_t             dataBufSize            = SIZE_T_C(0);
    uint8_t*           dataBuf                = M_NULLPTR;
    uint32_t           sectorCount            = get_Sector_Count_For_Read_Write(device);
    uint8_t            IDODTimeSeconds        = UINT8_C(45); // can be made into a function input if we wanted
    uint8_t            randomTimeSeconds      = UINT8_C(30); // can be made into a function input if we wanted
    time_t             startTime              = 0;
    uint64_t           IDStartLBA             = UINT64_C(0);
    uint64_t           ODEndingLBA            = UINT64_C(0);
    uint64_t           randomLBA              = UINT64_C(0);
    performanceNumbers idTest, odTest, randomTest;
    M_INITIALIZE_STRUCTURE(&idTest, sizeof(performanceNumbers));
    M_INITIALIZE_STRUCTURE(&odTest, sizeof(performanceNumbers));
    M_INITIALIZE_STRUCTURE(&randomTest, sizeof(performanceNumbers));
    DECLARE_SEATIMER(idTestTimer);
    DECLARE_SEATIMER(odTestTimer);
    DECLARE_SEATIMER(randomTestTimer);
    // allocate memory now that we know the sector count
    if (rwvCommand != RWV_COMMAND_VERIFY)
    {
        dataBufSize = uint32_to_sizet(get_Device_BlockSize(device)) * uint32_to_sizet(sectorCount) * sizeof(uint8_t);
        dataBuf =
            M_REINTERPRET_CAST(uint8_t*, safe_malloc_aligned(dataBufSize, get_Device_IO_Minimum_Alignment(device)));
        if (dataBuf == M_NULLPTR)
        {
            perror("failed to allocate memory for reading data at OD\n");
            return MEMORY_FAILURE;
        }
    }
    // set the ioodtime string and random time string for reporting (seconds only for now)
    seqTimeStrRes = snprintf_err_handle(idodTimeString, TIME_STRING_LENGTH, " %" PRIu8 " seconds", IDODTimeSeconds);
    randTimeStrRes =
        snprintf_err_handle(randomTimeString, TIME_STRING_LENGTH, " %" PRIu8 " seconds", randomTimeSeconds);
    switch (rwvCommand)
    {
    case RWV_COMMAND_READ:
        msgerror = safe_strcpy(message, JSON_REPORT_TEST_STEP_MESSAGE_LENGTH, "Sequential Read Test at OD for ~");
        break;
    case RWV_COMMAND_VERIFY:
        msgerror = safe_strcpy(message, JSON_REPORT_TEST_STEP_MESSAGE_LENGTH, "Sequential Verify Test at OD for ~");
        break;
    case RWV_COMMAND_WRITE:
        msgerror = safe_strcpy(message, JSON_REPORT_TEST_STEP_MESSAGE_LENGTH, "Sequential Write Test at OD for ~");
        break;
    default:
        msgerror = safe_strcpy(message, JSON_REPORT_TEST_STEP_MESSAGE_LENGTH, "Sequential Unknown Test at OD for ~");
        break;
    }
    // read at OD for 2 minutes...remember the LBA count to use for the ID
    if (device->deviceVerbosity > VERBOSITY_QUIET && msgerror == 0)
    {
        print_str(message);
        print_Time_To_Screen(M_NULLPTR, M_NULLPTR, M_NULLPTR, M_NULLPTR, &IDODTimeSeconds);
        print_str("\n");
    }
    if (updateFunction != M_NULLPTR && msgerror == 0 && seqTimeStrRes >= 0)
    {
        msgerror = safe_strcat(message, JSON_REPORT_TEST_STEP_MESSAGE_LENGTH, idodTimeString);
        if (msgerror == 0)
        {
            op_emit_step_cb(updateFunction, updateData, "sequential_RWV", message);
        }
        else
        {
            perror("Error formatting message for sequential test at OD");
        }
    }
    odTest.asyncCommandsUsed    = false;
    odTest.fastestCommandTimeNS = UINT64_MAX; // set this to a max so that it gets readjusted later...-TJE
    odTest.sectorCount          = C_CAST(uint16_t, sectorCount);
    // issue this command to get us in the right place for the OD test.
    read_Write_Seek_Command(device, rwvCommand, 0, dataBuf,
                            C_CAST(uint32_t, sectorCount* get_Device_BlockSize(device)));
    startTime = time(M_NULLPTR);
    start_Timer(&odTestTimer);
    while (difftime(time(M_NULLPTR), startTime) < IDODTimeSeconds && ODEndingLBA < return_Device_MaxLba(device))
    {
        if (updateFunction != M_NULLPTR)
        {
            op_emit_lba_cb(updateFunction, updateData, "generic_test", ODEndingLBA, M_NULLPTR);
        }
        if (VERBOSITY_QUIET < device->deviceVerbosity && !hideLBACounter)
        {
            switch (rwvCommand)
            {
            case RWV_COMMAND_READ:
                printf("\rReading LBA: %-20" PRIu64 "",
                       ODEndingLBA); // 20 wide is the max width for a unsigned 64bit number
                break;
            case RWV_COMMAND_VERIFY:
                printf("\rVerifying LBA: %-20" PRIu64 "",
                       ODEndingLBA); // 20 wide is the max width for a unsigned 64bit number
                break;
            case RWV_COMMAND_WRITE:
                printf("\rWriting LBA: %-20" PRIu64 "",
                       ODEndingLBA); // 20 wide is the max width for a unsigned 64bit number
                break;
            default:
                printf("\rUnknown OPing LBA: %-20" PRIu64 "",
                       ODEndingLBA); // 20 wide is the max width for a unsigned 64bit number
                break;
            }
            flush_stdout();
        }
        // if (SUCCESS != read_LBA(device, ODEndingLBA, false, dataBuf, C_CAST(uint32_t, sectorCount *
        // get_Device_BlockSize(device))))
        if (SUCCESS != read_Write_Seek_Command(device, rwvCommand, ODEndingLBA, dataBuf,
                                               C_CAST(uint32_t, sectorCount* get_Device_BlockSize(device))))
        {
            ret = FAILURE;
            switch (rwvCommand)
            {
            case RWV_COMMAND_READ:
                msgerror =
                    safe_strcpy(message, JSON_REPORT_TEST_STEP_MESSAGE_LENGTH, "Read failed within OD sequential read");
                break;
            case RWV_COMMAND_VERIFY:
                msgerror = safe_strcpy(message, JSON_REPORT_TEST_STEP_MESSAGE_LENGTH,
                                       "Verify failed within OD sequential read");
                break;
            case RWV_COMMAND_WRITE:
                msgerror = safe_strcpy(message, JSON_REPORT_TEST_STEP_MESSAGE_LENGTH,
                                       "Write failed within OD sequential read");
                break;
            default:
                msgerror = safe_strcpy(message, JSON_REPORT_TEST_STEP_MESSAGE_LENGTH,
                                       "Unknown OP failed within OD sequential read");
                break;
            }
            if (device->deviceVerbosity > VERBOSITY_QUIET && msgerror == 0)
            {
                print_str("\n");
                print_str(message);
                print_str("\n");
            }
            if (updateFunction != M_NULLPTR && msgerror == 0)
            {
                op_emit_step_cb(updateFunction, updateData, "generic_test", message);
            }
            safe_free_aligned(&dataBuf);
            return ret;
        }
        ++odTest.numberOfCommandsIssued;
        odTest.averageCommandTimeNS += get_tDevice_Last_Command_Completion_Time_NS(device);
        if (odTest.fastestCommandTimeNS > get_tDevice_Last_Command_Completion_Time_NS(device))
        {
            odTest.fastestCommandTimeNS = get_tDevice_Last_Command_Completion_Time_NS(device);
        }
        if (odTest.slowestCommandTimeNS < get_tDevice_Last_Command_Completion_Time_NS(device))
        {
            odTest.slowestCommandTimeNS = get_tDevice_Last_Command_Completion_Time_NS(device);
        }
        ODEndingLBA += sectorCount;
    }
    stop_Timer(&odTestTimer);
    odTest.averageCommandTimeNS /= odTest.numberOfCommandsIssued;
    odTest.totalTimeNS = get_Nano_Seconds(odTestTimer);
    odTest.iops =
        C_CAST(uint64_t, C_CAST(double, odTest.numberOfCommandsIssued) / (C_CAST(double, odTest.totalTimeNS) * 1e-9));
    if (device->deviceVerbosity > VERBOSITY_QUIET)
    {
        print_str("\n");
    }
    // read at ID for about 2 minutes (or exactly)
    switch (rwvCommand)
    {
    case RWV_COMMAND_READ:
        msgerror = safe_strcpy(message, JSON_REPORT_TEST_STEP_MESSAGE_LENGTH, "Sequential Read Test at ID for ~");
        break;
    case RWV_COMMAND_VERIFY:
        msgerror = safe_strcpy(message, JSON_REPORT_TEST_STEP_MESSAGE_LENGTH, "Sequential Verify Test at ID for ~");
        break;
    case RWV_COMMAND_WRITE:
        msgerror = safe_strcpy(message, JSON_REPORT_TEST_STEP_MESSAGE_LENGTH, "Sequential Write Test at ID for ~");
        break;
    default:
        msgerror = safe_strcpy(message, JSON_REPORT_TEST_STEP_MESSAGE_LENGTH, "Sequential Unknown Test at ID for ~");
        break;
    }
    if (device->deviceVerbosity > VERBOSITY_QUIET && msgerror == 0)
    {
        print_str(message);
        print_Time_To_Screen(M_NULLPTR, M_NULLPTR, M_NULLPTR, M_NULLPTR, &IDODTimeSeconds);
        print_str("\n");
    }
    if (updateFunction != M_NULLPTR && msgerror == 0)
    {
        if (0 == safe_strcat(message, JSON_REPORT_TEST_STEP_MESSAGE_LENGTH, idodTimeString))
        {
            op_emit_step_cb(updateFunction, updateData, "sequential_RWV", message);
        }
        else
        {
            perror("Error formatting message for sequential test at ID");
        }
    }

    IDStartLBA                  = return_Device_MaxLba(device) - ODEndingLBA;
    idTest.asyncCommandsUsed    = false;
    idTest.fastestCommandTimeNS = UINT64_MAX; // set this to a max so that it gets readjusted later...-TJE
    idTest.sectorCount          = C_CAST(uint16_t, sectorCount);
    // issue this read to get the heads in the right place before starting the ID test.
    read_Write_Seek_Command(device, rwvCommand, IDStartLBA, dataBuf,
                            C_CAST(uint32_t, sectorCount* get_Device_BlockSize(device)));
    startTime = time(M_NULLPTR);
    start_Timer(&idTestTimer);
    while (difftime(time(M_NULLPTR), startTime) < IDODTimeSeconds && IDStartLBA < return_Device_MaxLba(device))
    {
        if (updateFunction != M_NULLPTR)
        {
            op_emit_lba_cb(updateFunction, updateData, "generic_test", IDStartLBA, M_NULLPTR);
        }
        if (VERBOSITY_QUIET < device->deviceVerbosity && !hideLBACounter)
        {
            switch (rwvCommand)
            {
            case RWV_COMMAND_READ:
                printf("\rReading LBA: %-20" PRIu64 "",
                       IDStartLBA); // 20 wide is the max width for a unsigned 64bit number
                break;
            case RWV_COMMAND_VERIFY:
                printf("\rVerifying LBA: %-20" PRIu64 "",
                       IDStartLBA); // 20 wide is the max width for a unsigned 64bit number
                break;
            case RWV_COMMAND_WRITE:
                printf("\rWriting LBA: %-20" PRIu64 "",
                       IDStartLBA); // 20 wide is the max width for a unsigned 64bit number
                break;
            default:
                printf("\rUnknown OPing LBA: %-20" PRIu64 "",
                       IDStartLBA); // 20 wide is the max width for a unsigned 64bit number
                break;
            }
            flush_stdout();
        }
        if (SUCCESS != read_Write_Seek_Command(device, rwvCommand, IDStartLBA, dataBuf,
                                               C_CAST(uint32_t, sectorCount* get_Device_BlockSize(device))))
        {
            ret = FAILURE;
            switch (rwvCommand)
            {
            case RWV_COMMAND_READ:
                msgerror =
                    safe_strcpy(message, JSON_REPORT_TEST_STEP_MESSAGE_LENGTH, "Read failed within ID sequential read");
                break;
            case RWV_COMMAND_VERIFY:
                msgerror = safe_strcpy(message, JSON_REPORT_TEST_STEP_MESSAGE_LENGTH,
                                       "Verify failed within ID sequential read");
                break;
            case RWV_COMMAND_WRITE:
                msgerror = safe_strcpy(message, JSON_REPORT_TEST_STEP_MESSAGE_LENGTH,
                                       "Write failed within ID sequential read");
                break;
            default:
                msgerror = safe_strcpy(message, JSON_REPORT_TEST_STEP_MESSAGE_LENGTH,
                                       "Unknown OP failed within ID sequential read");
                break;
            }
            if (updateFunction != M_NULLPTR && msgerror == 0)
            {
                op_emit_step_cb(updateFunction, updateData, "generic_test", message);
            }
            if (device->deviceVerbosity > VERBOSITY_QUIET && msgerror == 0)
            {
                print_str("\n");
                print_str(message);
                print_str("\n");
            }
            safe_free_aligned(&dataBuf);
            return ret;
        }
        ++idTest.numberOfCommandsIssued;
        idTest.averageCommandTimeNS += get_tDevice_Last_Command_Completion_Time_NS(device);
        if (idTest.fastestCommandTimeNS > get_tDevice_Last_Command_Completion_Time_NS(device))
        {
            idTest.fastestCommandTimeNS = get_tDevice_Last_Command_Completion_Time_NS(device);
        }
        if (idTest.slowestCommandTimeNS < get_tDevice_Last_Command_Completion_Time_NS(device))
        {
            idTest.slowestCommandTimeNS = get_tDevice_Last_Command_Completion_Time_NS(device);
        }
        IDStartLBA += sectorCount;
    }
    stop_Timer(&idTestTimer);
    idTest.averageCommandTimeNS /= idTest.numberOfCommandsIssued;
    idTest.totalTimeNS = get_Nano_Seconds(idTestTimer);
    idTest.iops =
        C_CAST(uint64_t, C_CAST(double, idTest.numberOfCommandsIssued) / (C_CAST(double, idTest.totalTimeNS) * 1e-9));
    if (device->deviceVerbosity > VERBOSITY_QUIET)
    {
        print_str("\n");
    }
    // now random reads for 30 seconds
    // start random number generator
    seed_64(C_CAST(uint64_t, time(M_NULLPTR)));
    switch (rwvCommand)
    {
    case RWV_COMMAND_READ:
        msgerror = safe_strcpy(message, JSON_REPORT_TEST_STEP_MESSAGE_LENGTH, "Random Read Test for ~");
        break;
    case RWV_COMMAND_VERIFY:
        msgerror = safe_strcpy(message, JSON_REPORT_TEST_STEP_MESSAGE_LENGTH, "Random Verify Test for ~");
        break;
    case RWV_COMMAND_WRITE:
        msgerror = safe_strcpy(message, JSON_REPORT_TEST_STEP_MESSAGE_LENGTH, "Random Write Test for ~");
        break;
    default:
        msgerror = safe_strcpy(message, JSON_REPORT_TEST_STEP_MESSAGE_LENGTH, "Random Unknown Test for ~");
        break;
    }
    if (device->deviceVerbosity > VERBOSITY_QUIET && msgerror == 0)
    {
        print_str(message);
        print_Time_To_Screen(M_NULLPTR, M_NULLPTR, M_NULLPTR, M_NULLPTR, &randomTimeSeconds);
        print_str("\n");
    }
    if (updateFunction != M_NULLPTR && msgerror == 0 && randTimeStrRes >= 0)
    {
        if (0 == safe_strcat(message, JSON_REPORT_TEST_STEP_MESSAGE_LENGTH, randomTimeString))
        {
            op_emit_step_cb(updateFunction, updateData, "two_minute_test", message);
        }
    }
    randomTest.asyncCommandsUsed    = false;
    randomTest.fastestCommandTimeNS = UINT64_MAX; // set this to a max so that it gets readjusted later...-TJE
    randomTest.sectorCount          = C_CAST(uint16_t, sectorCount);
    startTime                       = time(M_NULLPTR);
    start_Timer(&randomTestTimer);
    while (difftime(time(M_NULLPTR), startTime) < randomTimeSeconds)
    {
        randomLBA = random_Range_64(0, return_Device_MaxLba(device));
        if (updateFunction != M_NULLPTR)
        {
            op_emit_lba_cb(updateFunction, updateData, "generic_test", randomLBA, M_NULLPTR);
        }
        if (VERBOSITY_QUIET < device->deviceVerbosity && !hideLBACounter)
        {
            switch (rwvCommand)
            {
            case RWV_COMMAND_READ:
                printf("\rReading LBA: %-20" PRIu64 "",
                       randomLBA); // 20 wide is the max width for a unsigned 64bit number
                break;
            case RWV_COMMAND_VERIFY:
                printf("\rVerifying LBA: %-20" PRIu64 "",
                       randomLBA); // 20 wide is the max width for a unsigned 64bit number
                break;
            case RWV_COMMAND_WRITE:
                printf("\rWriting LBA: %-20" PRIu64 "",
                       randomLBA); // 20 wide is the max width for a unsigned 64bit number
                break;
            default:
                printf("\rUnknown OPing LBA: %-20" PRIu64 "",
                       randomLBA); // 20 wide is the max width for a unsigned 64bit number
                break;
            }
            flush_stdout();
        }
        if (SUCCESS != read_Write_Seek_Command(device, rwvCommand, randomLBA, dataBuf,
                                               C_CAST(uint32_t, 1 * get_Device_BlockSize(device))))
        {
            ret = FAILURE;
            switch (rwvCommand)
            {
            case RWV_COMMAND_READ:
                msgerror = safe_strcpy(message, JSON_REPORT_TEST_STEP_MESSAGE_LENGTH, "Random Read failed");
                break;
            case RWV_COMMAND_VERIFY:
                msgerror = safe_strcpy(message, JSON_REPORT_TEST_STEP_MESSAGE_LENGTH, "Random Verify failed");
                break;
            case RWV_COMMAND_WRITE:
                msgerror = safe_strcpy(message, JSON_REPORT_TEST_STEP_MESSAGE_LENGTH, "Random Write failed");
                break;
            default:
                msgerror = safe_strcpy(message, JSON_REPORT_TEST_STEP_MESSAGE_LENGTH, "Random Unknown OP failed");
                break;
            }
            if (device->deviceVerbosity > VERBOSITY_QUIET && msgerror == 0)
            {
                print_str("\n");
                print_str(message);
                print_str("\n");
            }
            safe_free_aligned(&dataBuf);
            return ret;
        }
        ++randomTest.numberOfCommandsIssued;
        randomTest.averageCommandTimeNS += get_tDevice_Last_Command_Completion_Time_NS(device);
        if (randomTest.fastestCommandTimeNS > get_tDevice_Last_Command_Completion_Time_NS(device))
        {
            randomTest.fastestCommandTimeNS = get_tDevice_Last_Command_Completion_Time_NS(device);
        }
        if (randomTest.slowestCommandTimeNS < get_tDevice_Last_Command_Completion_Time_NS(device))
        {
            randomTest.slowestCommandTimeNS = get_tDevice_Last_Command_Completion_Time_NS(device);
        }
    }
    stop_Timer(&randomTestTimer);
    if (device->deviceVerbosity > VERBOSITY_QUIET)
    {
        print_str("\n");
    }
    randomTest.averageCommandTimeNS /= randomTest.numberOfCommandsIssued;
    randomTest.totalTimeNS = get_Nano_Seconds(randomTestTimer);
    randomTest.iops        = C_CAST(uint64_t, C_CAST(double, randomTest.numberOfCommandsIssued) /
                                                  (C_CAST(double, randomTest.totalTimeNS) * 1e-9));
    if (device->deviceVerbosity > VERBOSITY_QUIET && showPerformanceNumbers)
    {
        print_str("\n");
        print_str("===Drive Performance Characteristics===\n");
        print_str("\tNOT AN OFFICIAL BENCHMARK\n\n");
        print_str("Read-Look-Ahead: ");
        if (is_Read_Look_Ahead_Enabled(device))
        {
            print_str("Enabled\n");
        }
        else
        {
            print_str("Disabled\n");
        }
        print_str("Write Cache: ");
        if (is_Write_Cache_Enabled(device))
        {
            print_str("Enabled\n");
        }
        else
        {
            print_str("Disabled\n");
        }
        print_str("OD Test:\n");
        if (odTest.asyncCommandsUsed)
        {
            print_str("\tUsed asynchronous commands\n");
        }
        else
        {
            print_str("\tUsed synchronous commands\n");
        }
        print_str("\tAverage Command time: ");
        print_Time(odTest.averageCommandTimeNS);
        print_str("\tFastest Command time: ");
        print_Time(odTest.fastestCommandTimeNS);
        print_str("\tSlowest Command time: ");
        print_Time(odTest.slowestCommandTimeNS);
        printf("\tIOPS: %" PRIu64 "\n", odTest.iops);
        // calculate MB(/GB)/s performance
        uint64_t odBytesPerTransfer =
            C_CAST(uint64_t, get_Device_BlockSize(device)) * C_CAST(uint64_t, odTest.sectorCount);
        double odTotalBytesTransferred = C_CAST(double, odBytesPerTransfer* odTest.numberOfCommandsIssued);
        double odDataRate              = odTotalBytesTransferred / C_CAST(double, odTest.totalTimeNS) * 1e-9;
        DECLARE_ZERO_INIT_ARRAY(char, odDataRateUnits, 3);
        char* odDataRateUnit = &odDataRateUnits[0];
        metric_Unit_Convert(&odDataRate, &odDataRateUnit);
        printf("\tData Rate: %0.02f %s/s\n", odDataRate, odDataRateUnit);
        printf("\tNumber of Commands Issued: %" PRIu64 "\n", odTest.numberOfCommandsIssued);
        printf("\tLBAs accessed per command: %" PRIu16 "\n", odTest.sectorCount);
        printf("\tTotal LBAs accessed: %" PRIu64 "\n", odTest.numberOfCommandsIssued * odTest.sectorCount);

        print_str("ID Test:\n");
        if (idTest.asyncCommandsUsed)
        {
            print_str("\tUsed asynchronous commands\n");
        }
        else
        {
            print_str("\tUsed synchronous commands\n");
        }
        print_str("\tAverage Command time: ");
        print_Time(idTest.averageCommandTimeNS);
        print_str("\tFastest Command time: ");
        print_Time(idTest.fastestCommandTimeNS);
        print_str("\tSlowest Command time: ");
        print_Time(idTest.slowestCommandTimeNS);
        printf("\tIOPS: %" PRIu64 "\n", idTest.iops);
        // calculate MB(/GB)/s performance
        uint64_t idBytesPerTransfer =
            C_CAST(uint64_t, get_Device_BlockSize(device)) * C_CAST(uint64_t, idTest.sectorCount);
        double idTotalBytesTransferred = C_CAST(double, idBytesPerTransfer* idTest.numberOfCommandsIssued);
        double idDataRate              = idTotalBytesTransferred / (C_CAST(double, idTest.totalTimeNS) * 1e-9);
        DECLARE_ZERO_INIT_ARRAY(char, idDataRateUnits, 3);
        char* idDataRateUnit = &idDataRateUnits[0];
        metric_Unit_Convert(&idDataRate, &idDataRateUnit);
        printf("\tData Rate: %0.02f %s/s\n", idDataRate, idDataRateUnit);
        printf("\tNumber of Commands Issued: %" PRIu64 "\n", idTest.numberOfCommandsIssued);
        printf("\tLBAs accessed per command: %" PRIu16 "\n", idTest.sectorCount);
        printf("\tTotal LBAs accessed: %" PRIu64 "\n", idTest.numberOfCommandsIssued * idTest.sectorCount);

        print_str("Random Test:\n");
        if (randomTest.asyncCommandsUsed)
        {
            print_str("\tUsed asynchronous commands\n");
        }
        else
        {
            print_str("\tUsed synchronous commands\n");
        }
        print_str("\tAverage Command time: ");
        print_Time(randomTest.averageCommandTimeNS);
        print_str("\tFastest Command time: ");
        print_Time(randomTest.fastestCommandTimeNS);
        print_str("\tSlowest Command time: ");
        print_Time(randomTest.slowestCommandTimeNS);
        printf("\tIOPS: %" PRIu64 "\n", randomTest.iops);
        // calculate MB(/GB)/s performance
        uint64_t randomBytesPerTransfer =
            C_CAST(uint64_t, get_Device_BlockSize(device)) * C_CAST(uint64_t, randomTest.sectorCount);
        double randomTotalBytesTransferred = C_CAST(double, randomBytesPerTransfer* randomTest.numberOfCommandsIssued);
        double randomDataRate = randomTotalBytesTransferred / (C_CAST(double, randomTest.totalTimeNS) * 1e-9);
        DECLARE_ZERO_INIT_ARRAY(char, randomDataRateUnits, 3);
        char* randomDataRateUnit = &randomDataRateUnits[0];
        metric_Unit_Convert(&randomDataRate, &randomDataRateUnit);
        printf("\tData Rate: %0.02f %s/s\n", randomDataRate, randomDataRateUnit);
        printf("\tNumber of Commands Issued: %" PRIu64 "\n", randomTest.numberOfCommandsIssued);
        printf("\tLBAs accessed per command: %" PRIu16 "\n", randomTest.sectorCount);
        printf("\tTotal LBAs accessed: %" PRIu64 "\n", randomTest.numberOfCommandsIssued * randomTest.sectorCount);
    }
    safe_free_aligned(&dataBuf);
    return ret;
}

M_PARAM_RO(1)
OPENSEA_OPERATIONS_API eReturnValues long_Generic_Read_Test(const tDevice* M_NONNULL device,
                                                            uint16_t                 errorLimit,
                                                            bool                     stopOnError,
                                                            bool                     repairOnTheFly,
                                                            bool                     repairAtEnd,
                                                            custom_Update M_NULLABLE updateFunction,
                                                            void* M_NULLABLE         updateData,
                                                            bool                     hideLBACounter)
{
    return user_Sequential_Read_Test(device, 0, return_Device_MaxLba(device), errorLimit, stopOnError, repairOnTheFly,
                                     repairAtEnd, updateFunction, updateData, hideLBACounter);
}

M_PARAM_RO(1)
OPENSEA_OPERATIONS_API eReturnValues long_Generic_Write_Test(const tDevice* M_NONNULL device,
                                                             uint16_t                 errorLimit,
                                                             bool                     stopOnError,
                                                             bool                     repairOnTheFly,
                                                             bool                     repairAtEnd,
                                                             custom_Update M_NULLABLE updateFunction,
                                                             void* M_NULLABLE         updateData,
                                                             bool                     hideLBACounter)
{
    return user_Sequential_Write_Test(device, 0, return_Device_MaxLba(device), errorLimit, stopOnError, repairOnTheFly,
                                      repairAtEnd, updateFunction, updateData, hideLBACounter);
}

M_PARAM_RO(1)
OPENSEA_OPERATIONS_API eReturnValues long_Generic_Verify_Test(const tDevice* M_NONNULL device,
                                                              uint16_t                 errorLimit,
                                                              bool                     stopOnError,
                                                              bool                     repairOnTheFly,
                                                              bool                     repairAtEnd,
                                                              custom_Update M_NULLABLE updateFunction,
                                                              void* M_NULLABLE         updateData,
                                                              bool                     hideLBACounter)
{
    return user_Sequential_Verify_Test(device, 0, return_Device_MaxLba(device), errorLimit, stopOnError, repairOnTheFly,
                                       repairAtEnd, updateFunction, updateData, hideLBACounter);
}

M_PARAM_RO(1)
OPENSEA_OPERATIONS_API eReturnValues long_Generic_Test(const tDevice* M_NONNULL device,
                                                       eRWVCommandType          rwvCommand,
                                                       uint16_t                 errorLimit,
                                                       bool                     stopOnError,
                                                       bool                     repairOnTheFly,
                                                       bool                     repairAtEnd,
                                                       custom_Update M_NULLABLE updateFunction,
                                                       void* M_NULLABLE         updateData,
                                                       bool                     hideLBACounter)
{
    return user_Sequential_Test(device, rwvCommand, 0, return_Device_MaxLba(device), errorLimit, stopOnError,
                                repairOnTheFly, repairAtEnd, updateFunction, updateData, hideLBACounter);
}

M_PARAM_RO(1)
OPENSEA_OPERATIONS_API eReturnValues user_Sequential_Read_Test(const tDevice* M_NONNULL device,
                                                               uint64_t                 startingLBA,
                                                               uint64_t                 range,
                                                               uint16_t                 errorLimit,
                                                               bool                     stopOnError,
                                                               bool                     repairOnTheFly,
                                                               bool                     repairAtEnd,
                                                               custom_Update M_NULLABLE updateFunction,
                                                               void* M_NULLABLE         updateData,
                                                               bool                     hideLBACounter)
{
    return user_Sequential_Test(device, RWV_COMMAND_READ, startingLBA, range, errorLimit, stopOnError, repairOnTheFly,
                                repairAtEnd, updateFunction, updateData, hideLBACounter);
}

M_PARAM_RO(1)
OPENSEA_OPERATIONS_API eReturnValues user_Sequential_Write_Test(const tDevice* M_NONNULL device,
                                                                uint64_t                 startingLBA,
                                                                uint64_t                 range,
                                                                uint16_t                 errorLimit,
                                                                bool                     stopOnError,
                                                                bool                     repairOnTheFly,
                                                                bool                     repairAtEnd,
                                                                custom_Update M_NULLABLE updateFunction,
                                                                void* M_NULLABLE         updateData,
                                                                bool                     hideLBACounter)
{
    return user_Sequential_Test(device, RWV_COMMAND_WRITE, startingLBA, range, errorLimit, stopOnError, repairOnTheFly,
                                repairAtEnd, updateFunction, updateData, hideLBACounter);
}

M_PARAM_RO(1)
OPENSEA_OPERATIONS_API eReturnValues user_Sequential_Verify_Test(const tDevice* M_NONNULL device,
                                                                 uint64_t                 startingLBA,
                                                                 uint64_t                 range,
                                                                 uint16_t                 errorLimit,
                                                                 bool                     stopOnError,
                                                                 bool                     repairOnTheFly,
                                                                 bool                     repairAtEnd,
                                                                 custom_Update M_NULLABLE updateFunction,
                                                                 void* M_NULLABLE         updateData,
                                                                 bool                     hideLBACounter)
{
    return user_Sequential_Test(device, RWV_COMMAND_VERIFY, startingLBA, range, errorLimit, stopOnError, repairOnTheFly,
                                repairAtEnd, updateFunction, updateData, hideLBACounter);
}

M_PARAM_RO(1)
OPENSEA_OPERATIONS_API eReturnValues user_Sequential_Test(const tDevice* M_NONNULL device,
                                                          eRWVCommandType          rwvCommand,
                                                          uint64_t                 startingLBA,
                                                          uint64_t                 range,
                                                          uint16_t                 errorLimit,
                                                          bool                     stopOnError,
                                                          bool                     repairOnTheFly,
                                                          bool                     repairAtEnd,
                                                          custom_Update M_NULLABLE updateFunction,
                                                          void* M_NULLABLE         updateData,
                                                          bool                     hideLBACounter)
{
    eReturnValues ret               = SUCCESS;
    errorLBA*     errorList         = M_NULLPTR;
    uint64_t      errorIndex        = UINT64_C(0);
    bool          errorLimitReached = false;
    uint32_t      sectorCount       = get_Sector_Count_For_Read_Write(device);
    // only one of these flags should be set. If they are both set, this makes no sense
    if ((repairAtEnd && repairOnTheFly) || (repairAtEnd && (errorLimit == 0)))
    {
        return BAD_PARAMETER;
    }
    if (stopOnError)
    {
        // disable the repair flags in this case since they don't make sense
        repairAtEnd    = false;
        repairOnTheFly = false;
    }
    if (errorLimit < 1)
    {
        // need to be able to store at least 1 error
        errorList = M_REINTERPRET_CAST(errorLBA*, safe_calloc(1 * sizeof(errorLBA), sizeof(errorLBA)));
    }
    else
    {
        errorList = M_REINTERPRET_CAST(errorLBA*, safe_calloc(errorLimit * sizeof(errorLBA), sizeof(errorLBA)));
    }
    if (errorList == M_NULLPTR)
    {
        perror("calloc failure\n");
        return MEMORY_FAILURE;
    }
    errorList[0].errorAddress = UINT64_MAX;
    bool autoReadReassign     = false;
    bool autoWriteReassign    = false;
    if (SUCCESS != get_Automatic_Reallocation_Support(device, &autoWriteReassign, &autoReadReassign))
    {
        autoWriteReassign = true; // just in case this fails, default to previous behavior
    }
    // this is escentially a loop over the sequential read function
    uint64_t endingLBA = startingLBA + range;
    while (!errorLimitReached)
    {
        if (SUCCESS != sequential_RWV(device, rwvCommand, startingLBA, range, sectorCount,
                                      &errorList[errorIndex].errorAddress, updateFunction, updateData, hideLBACounter))
        {
            if (device->deviceVerbosity > VERBOSITY_QUIET)
            {
                printf("\nError Found at LBA %" PRIu64 "", errorList[errorIndex].errorAddress);
                if (errorLimit != 0)
                {
                    print_str("\n");
                }
            }
            if (updateFunction != M_NULLPTR)
            {
                op_emit_error_lba_cb(updateFunction, updateData, "generic_test", errorList[errorIndex].errorAddress,
                                     (int)FAILURE, M_NULLPTR);
            }
            // set a new start for next time through the loop to 1 lba past the last error LBA
            startingLBA = errorList[errorIndex].errorAddress + 1;
            range       = endingLBA - startingLBA;
            if (stopOnError || ((errorLimit != 0) && (errorIndex >= errorLimit)))
            {
                errorLimitReached = true;
                ret               = FAILURE;
            }
            if (repairOnTheFly)
            {
                repair_LBA(device, &errorList[errorIndex], false, autoWriteReassign,
                           autoReadReassign); // This function will set the repair status for us. - TJE
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
        print_str("\n");
    }
    if (repairAtEnd)
    {
        // go through and repair the LBAs
        uint64_t errorIter                 = UINT64_C(0);
        uint64_t lastLBARepaired           = UINT64_MAX;
        uint16_t logicalPerPhysicalSectors = get_Logical_Sectors_Per_Physical_Sector(device);
        for (errorIter = 0; errorIter < errorIndex; errorIter++)
        {
            if (lastLBARepaired != UINT64_MAX)
            {
                // check if the LBA we want to repair is within the same physical sector as the last LBA
                if ((lastLBARepaired + logicalPerPhysicalSectors) > errorList[errorIter].errorAddress)
                {
                    // in this case, we have already repaired this LBA since the repair is issued to the physical
                    // sector, so move on to the next thing in the list
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
            printf("\nError occurred at LBA %" PRIu64 "\n", errorList[0].errorAddress);
        }
        if (updateFunction != M_NULLPTR)
        {
            op_emit_error_lba_cb(updateFunction, updateData, "generic_test", errorList[0].errorAddress, (int)FAILURE,
                                 M_NULLPTR);
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
                    print_str("One or more bad LBAs detected during read scan of device.\n");
                    ret = FAILURE;
                }
            }
            else
            {
                print_str("No bad LBAs detected during read scan of device.\n");
            }
        }
    }
    safe_free_error_lba(&errorList);
    return ret;
}

M_PARAM_RO(1)
OPENSEA_OPERATIONS_API eReturnValues user_Timed_Test(const tDevice* M_NONNULL device,
                                                     eRWVCommandType          rwvCommand,
                                                     uint64_t                 startingLBA,
                                                     uint64_t                 timeInSeconds,
                                                     uint16_t                 errorLimit,
                                                     bool                     stopOnError,
                                                     bool                     repairOnTheFly,
                                                     bool                     repairAtEnd,
                                                     custom_Update M_NULLABLE updateFunction,
                                                     void* M_NULLABLE         updateData,
                                                     bool                     hideLBACounter)
{
    eReturnValues ret               = SUCCESS;
    bool          errorLimitReached = false;
    errorLBA*     errorList         = M_NULLPTR;
    uint64_t      errorIndex        = UINT64_C(0);
    uint32_t      sectorCount       = get_Sector_Count_For_Read_Write(device);
    uint8_t*      dataBuf           = M_NULLPTR;
    size_t        dataBufSize       = SIZE_T_C(0);
    // only one of these flags should be set. If they are both set, this makes no sense
    if (stopOnError)
    {
        // disable the repair flags in this case since they don't make sense
        repairAtEnd    = false;
        repairOnTheFly = false;
    }
    if (errorLimit < 1)
    {
        // need to be able to store at least 1 error
        errorLimit = 1;
    }
    errorList = M_REINTERPRET_CAST(errorLBA*, safe_calloc(errorLimit, sizeof(errorLBA)));
    if (errorList == M_NULLPTR)
    {
        perror("calloc failure\n");
        return MEMORY_FAILURE;
    }
    if (rwvCommand == RWV_COMMAND_READ || rwvCommand == RWV_COMMAND_WRITE)
    {
        // allocate memory
        dataBufSize = uint32_to_sizet(get_Device_BlockSize(device)) * uint32_to_sizet(sectorCount);
        dataBuf     = M_REINTERPRET_CAST(
            uint8_t*, safe_calloc_aligned(dataBufSize, sizeof(uint8_t), get_Device_IO_Minimum_Alignment(device)));
        if (dataBuf == M_NULLPTR)
        {
            perror("failed to allocate memory!\n");
            safe_free_error_lba(&errorList);
            return MEMORY_FAILURE;
        }
    }
    errorList[errorIndex].errorAddress = UINT64_MAX;
    bool autoReadReassign              = false;
    bool autoWriteReassign             = false;
    if (SUCCESS != get_Automatic_Reallocation_Support(device, &autoWriteReassign, &autoReadReassign))
    {
        autoWriteReassign = true; // just in case this fails, default to previous behavior
    }
    // make sure the starting LBA is alligned? If we do this, we need to make sure we don't mess with the data of the
    // LBAs we don't mean to start at...mostly don't want to erase an LBA we shouldn't be starting at. startingLBA =
    // align_LBA(device, startingLBA); this is escentially a loop over the sequential read function
    time_t startTime = time(M_NULLPTR);
    while (!errorLimitReached && C_CAST(uint64_t, difftime(time(M_NULLPTR), startTime)) < timeInSeconds &&
           startingLBA < return_Device_MaxLba(device))
    {
        if ((startingLBA + sectorCount) > return_Device_MaxLba(device))
        {
            sectorCount = C_CAST(uint32_t, return_Device_MaxLba(device) - startingLBA + 1);
        }
        if (updateFunction != M_NULLPTR)
        {
            op_emit_lba_cb(updateFunction, updateData, "user_Sequential_Test", startingLBA, M_NULLPTR);
        }
        if (VERBOSITY_QUIET < device->deviceVerbosity && !hideLBACounter)
        {
            switch (rwvCommand)
            {
            case RWV_COMMAND_WRITE:
                printf("\rWriting LBA: %-20" PRIu64 "",
                       startingLBA); // 20 wide is the max width for a unsigned 64bit number
                break;
            case RWV_COMMAND_READ:
                printf("\rReading LBA: %-20" PRIu64 "",
                       startingLBA); // 20 wide is the max width for a unsigned 64bit number
                break;
            case RWV_COMMAND_VERIFY:
            default:
                printf("\rVerifying LBA: %-20" PRIu64 "",
                       startingLBA); // 20 wide is the max width for a unsigned 64bit number
                break;
            }
            flush_stdout();
        }
        if (SUCCESS != read_Write_Seek_Command(device, rwvCommand, startingLBA, dataBuf,
                                               C_CAST(uint32_t, sectorCount* get_Device_BlockSize(device))))
        {
            uint64_t maxSingleLoopLBA =
                startingLBA + sectorCount; // limits the loop to trying to only a certain number of sectors without
                                           // getting stuck at single LBA reads.
            // read command failure...so we need to read until we find the exact failing lba
            for (; startingLBA <= maxSingleLoopLBA; startingLBA += 1)
            {
                if (updateFunction != M_NULLPTR)
                {
                    op_emit_lba_cb(updateFunction, updateData, "user_Sequential_Test", startingLBA, M_NULLPTR);
                }
                // print out the current LBA we are rwving
                if (VERBOSITY_QUIET < device->deviceVerbosity && !hideLBACounter)
                {
                    switch (rwvCommand)
                    {
                    case RWV_COMMAND_WRITE:
                        printf("\rWriting LBA: %-20" PRIu64 "",
                               startingLBA); // 20 wide is the max width for a unsigned 64bit number
                        break;
                    case RWV_COMMAND_READ:
                        printf("\rReading LBA: %-20" PRIu64 "",
                               startingLBA); // 20 wide is the max width for a unsigned 64bit number
                        break;
                    case RWV_COMMAND_VERIFY:
                    default:
                        printf("\rVerifying LBA: %-20" PRIu64 "",
                               startingLBA); // 20 wide is the max width for a unsigned 64bit number
                        break;
                    }
                    flush_stdout();
                }
                if (SUCCESS != read_Write_Seek_Command(device, rwvCommand, startingLBA, dataBuf,
                                                       C_CAST(uint32_t, 1 * get_Device_BlockSize(device))))
                {
                    errorList[errorIndex].errorAddress = startingLBA;
                    break;
                }
            }
            if (updateFunction != M_NULLPTR)
            {
                op_emit_error_lba_cb(updateFunction, updateData, "generic_test", errorList[errorIndex].errorAddress,
                                     (int)FAILURE, M_NULLPTR);
            }
            if (device->deviceVerbosity > VERBOSITY_QUIET)
            {
                printf("\nError Found at LBA %" PRIu64 "\n", errorList[errorIndex].errorAddress);
            }
            if (stopOnError || errorIndex >= errorLimit)
            {
                errorLimitReached = true;
                ret               = FAILURE;
            }
            if (repairOnTheFly)
            {
                repair_LBA(device, &errorList[errorIndex], false, autoWriteReassign, autoReadReassign);
            }
            // set a new start for next time through the loop to 1 lba past the last error LBA
            if (errorIndex < errorLimit)
            {
                startingLBA = errorList[errorIndex].errorAddress + 1;
                errorIndex++;
                continue; // continuing here since startingLBA will get incremented beyond the error so we pick up where
                          // we left off.
            }
            else
            {
                errorLimitReached = true;
                ret               = FAILURE;
                break;
            }
        }
        startingLBA += sectorCount;
    }
    if (updateFunction != M_NULLPTR)
    {
        op_emit_lba_cb(updateFunction, updateData, "user_Sequential_Test", startingLBA, M_NULLPTR);
    }
    if (VERBOSITY_QUIET < device->deviceVerbosity && !hideLBACounter)
    {
        switch (rwvCommand)
        {
        case RWV_COMMAND_WRITE:
            printf("\rWriting LBA: %-20" PRIu64 "", startingLBA); // 20 wide is the max width for a unsigned 64bit
                                                                  // number
            break;
        case RWV_COMMAND_READ:
            printf("\rReading LBA: %-20" PRIu64 "", startingLBA); // 20 wide is the max width for a unsigned 64bit
                                                                  // number
            break;
        case RWV_COMMAND_VERIFY:
        default:
            printf("\rVerifying LBA: %-20" PRIu64 "",
                   startingLBA); // 20 wide is the max width for a unsigned 64bit number
            break;
        }
        print_str("\n");
        flush_stdout();
    }
    safe_free_aligned(&dataBuf);
    if (device->deviceVerbosity > VERBOSITY_QUIET)
    {
        print_str("\n");
    }
    if (repairAtEnd)
    {
        // go through and repair the LBAs
        uint64_t errorIter                 = UINT64_C(0);
        uint64_t lastLBARepaired           = UINT64_MAX;
        uint16_t logicalPerPhysicalSectors = get_Logical_Sectors_Per_Physical_Sector(device);
        for (errorIter = 0; errorIter < errorIndex; errorIter++)
        {
            if (lastLBARepaired != UINT64_MAX)
            {
                // check if the LBA we want to repair is within the same physical sector as the last LBA
                if ((lastLBARepaired + logicalPerPhysicalSectors) > errorList[errorIter].errorAddress)
                {
                    // in this case, we have already repaired this LBA since the repair is issued to the physical
                    // sector, so move on to the next thing in the list
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
            printf("\nError occurred at LBA %" PRIu64 "\n", errorList[0].errorAddress);
        }
        if (updateFunction != M_NULLPTR)
        {
            op_emit_error_lba_cb(updateFunction, updateData, "generic_test", errorList[0].errorAddress, (int)FAILURE,
                                 M_NULLPTR);
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
                print_str("No bad LBAs detected during read scan of device.\n");
            }
        }
    }
    safe_free_error_lba(&errorList);
    return ret;
}

eReturnValues user_Sequential_Test_LBA_Error_List(const tDevice*              device,
                                                  eRWVCommandType             rwvCommand,
                                                  uint64_t                    startingLBA,
                                                  uint64_t                    range,
                                                  uint16_t                    errorLimit,
                                                  bool                        stopOnError,
                                                  bool                        repairOnTheFly,
                                                  bool                        repairAtEnd,
                                                  M_ATTR_UNUSED custom_Update updateFunction,
                                                  M_ATTR_UNUSED void*         updateData,
                                                  bool                        hideLBACounter,
                                                  errorLBA**                  errorList,
                                                  uint16_t*                   errorListSize)
{
    eReturnValues ret               = SUCCESS;
    uint16_t      errorIndex        = UINT16_C(0);
    bool          errorLimitReached = false;
    uint32_t      sectorCount       = get_Sector_Count_For_Read_Write(device);

    // only one of these flags should be set. If they are both set, this makes no sense
    if ((repairAtEnd && repairOnTheFly) || (repairAtEnd && (errorLimit == 0)))
    {
        return BAD_PARAMETER;
    }
    if (stopOnError)
    {
        // disable the repair flags in this case since they don't make sense
        repairAtEnd    = false;
        repairOnTheFly = false;
    }
    if (errorLimit < 1)
    {
        // need to be able to store at least 1 error
        *errorList = M_REINTERPRET_CAST(errorLBA*, safe_calloc(1 * sizeof(errorLBA), sizeof(errorLBA)));
    }
    else
    {
        *errorList = M_REINTERPRET_CAST(errorLBA*, safe_calloc(errorLimit * sizeof(errorLBA), sizeof(errorLBA)));
    }
    if (*errorList == M_NULLPTR)
    {
        perror("calloc failure\n");
        return MEMORY_FAILURE;
    }
    (*errorList)[0].errorAddress = UINT64_MAX;

    bool autoReadReassign  = false;
    bool autoWriteReassign = false;
    if (SUCCESS != get_Automatic_Reallocation_Support(device, &autoWriteReassign, &autoReadReassign))
    {
        autoWriteReassign = true; // just in case this fails, default to previous behavior
    }

    // this is essentially a loop over the sequential read function
    uint64_t endingLBA = startingLBA + range;
    while (!errorLimitReached)
    {
        if (SUCCESS != sequential_RWV(device, rwvCommand, startingLBA, range, sectorCount,
                                      &(*errorList)[errorIndex].errorAddress, updateFunction, updateData,
                                      hideLBACounter))
        {
            if (device->deviceVerbosity > VERBOSITY_QUIET)
            {
                printf("\nError Found at LBA %" PRIu64 "", (*errorList)[errorIndex].errorAddress);
                if (errorLimit != 0)
                    print_str("\n");
            }
            // set a new start for next time through the loop to 1 lba past the last error LBA
            startingLBA = (*errorList)[errorIndex].errorAddress + 1;
            range       = endingLBA - startingLBA;
            if (stopOnError || ((errorLimit != 0) && (errorIndex >= errorLimit)))
            {
                errorLimitReached = true;
                ret               = FAILURE;
            }
            if (repairOnTheFly)
            {
                repair_LBA(device, &(*errorList)[errorIndex], false, autoWriteReassign,
                           autoReadReassign); // This function will set the repair status for us. - TJE
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
        print_str("\n");
    }

    if (repairAtEnd)
    {
        // go through and repair the LBAs
        uint64_t errorIter       = UINT64_C(0);
        uint64_t lastLBARepaired = UINT64_MAX;
        uint16_t logicalPerPhysicalSectors =
            C_CAST(uint16_t, device->drive_info.devicePhyBlockSize / device->drive_info.deviceBlockSize);
        for (errorIter = 0; errorIter < errorIndex; errorIter++)
        {
            if (lastLBARepaired != UINT64_MAX)
            {
                // check if the LBA we want to repair is within the same physical sector as the last LBA
                if ((lastLBARepaired + logicalPerPhysicalSectors) > (*errorList)[errorIter].errorAddress)
                {
                    // in this case, we have already repaired this LBA since the repair is issued to the physical
                    // sector, so move on to the next thing in the list
                    (*errorList)[errorIter].repairStatus = REPAIR_NOT_REQUIRED;
                    continue;
                }
            }
            if (SUCCESS == repair_LBA(device, &(*errorList)[errorIter], false, autoWriteReassign, autoReadReassign))
            {
                lastLBARepaired = (*errorList)[errorIter].errorAddress;
            }
        }
    }

    if (stopOnError && (*errorList)[0].errorAddress != UINT64_MAX)
    {
        if (device->deviceVerbosity > VERBOSITY_QUIET)
        {
            printf("\nError occurred at LBA %" PRIu64 "\n", (*errorList)[0].errorAddress);
        }
    }
    else
    {
        if (device->deviceVerbosity > VERBOSITY_QUIET)
        {
            if ((*errorList)[0].errorAddress != UINT64_MAX)
            {
                if (errorLimit != 0)
                {
                    printf("\nFound %" PRIu16 " bad LBAs\n", errorIndex);
                }
                else
                {
                    print_str("One or more bad LBAs detected during read scan of device.\n");
                    ret = FAILURE;
                }
            }
            else
            {
                print_str("No bad LBAs detected during read scan of device.\n");
            }
        }
    }

    // Populate output parameter with error count
    // Caller is responsible for freeing the errorList using safe_free_error_lba()
    *errorListSize = errorIndex;

    return ret;
}

M_PARAM_RO(1)
OPENSEA_OPERATIONS_API eReturnValues butterfly_Read_Test(const tDevice* M_NONNULL device,
                                                         uint64_t                 timeLimitSeconds,
                                                         custom_Update M_NULLABLE updateFunction,
                                                         void* M_NULLABLE         updateData,
                                                         bool                     hideLBACounter)
{
    return butterfly_Test(device, RWV_COMMAND_READ, timeLimitSeconds, updateFunction, updateData, hideLBACounter);
}

M_PARAM_RO(1)
OPENSEA_OPERATIONS_API eReturnValues butterfly_Write_Test(const tDevice* M_NONNULL device,
                                                          uint64_t                 timeLimitSeconds,
                                                          custom_Update M_NULLABLE updateFunction,
                                                          void* M_NULLABLE         updateData,
                                                          bool                     hideLBACounter)
{
    return butterfly_Test(device, RWV_COMMAND_WRITE, timeLimitSeconds, updateFunction, updateData, hideLBACounter);
}

M_PARAM_RO(1)
OPENSEA_OPERATIONS_API eReturnValues butterfly_Verify_Test(const tDevice* M_NONNULL device,
                                                           uint64_t                 timeLimitSeconds,
                                                           custom_Update M_NULLABLE updateFunction,
                                                           void* M_NULLABLE         updateData,
                                                           bool                     hideLBACounter)
{
    return butterfly_Test(device, RWV_COMMAND_VERIFY, timeLimitSeconds, updateFunction, updateData, hideLBACounter);
}

M_PARAM_RO(1)
OPENSEA_OPERATIONS_API eReturnValues butterfly_Test(const tDevice* M_NONNULL device,
                                                    eRWVCommandType          rwvcommand,
                                                    uint64_t                 timeLimitSeconds,
                                                    custom_Update M_NULLABLE updateFunction,
                                                    void* M_NULLABLE         updateData,
                                                    bool                     hideLBACounter)
{
    eReturnValues ret         = SUCCESS;
    time_t        startTime   = 0; // will be set to actual current time before we start the test
    uint32_t      sectorCount = get_Sector_Count_For_Read_Write(device);
    uint64_t      outerLBA    = UINT64_C(0);
    uint64_t      innerLBA    = return_Device_MaxLba(device);
    uint8_t*      dataBuf     = M_NULLPTR;
    size_t        dataBufSize = SIZE_T_C(0);
    if (rwvcommand != RWV_COMMAND_VERIFY)
    {
        dataBufSize = uint32_to_sizet(get_Device_BlockSize(device)) * uint32_to_sizet(sectorCount) * sizeof(uint8_t);
        dataBuf =
            M_REINTERPRET_CAST(uint8_t*, safe_malloc_aligned(dataBufSize, get_Device_IO_Minimum_Alignment(device)));
        if (dataBuf == M_NULLPTR)
        {
            return MEMORY_FAILURE;
        }
    }
    uint32_t currentSectorCount = sectorCount;
    innerLBA -= sectorCount;
    startTime       = time(M_NULLPTR); // get the starting time before starting the loop
    double lastTime = 0.0;
    while (C_CAST(uint64_t, (lastTime = difftime(time(M_NULLPTR), startTime))) < timeLimitSeconds)
    {
        // read the outer lba
        if ((outerLBA + sectorCount) > return_Device_MaxLba(device))
        {
            // adjust the sector count to get to the maxLBA for the read
            currentSectorCount = C_CAST(uint32_t, return_Device_MaxLba(device) - outerLBA);
        }
        if (updateFunction != M_NULLPTR)
        {
            op_emit_lba_cb(updateFunction, updateData, "diameter_test", outerLBA, M_NULLPTR);
        }
        if (VERBOSITY_QUIET < device->deviceVerbosity && !hideLBACounter)
        {
            switch (rwvcommand)
            {
            case RWV_COMMAND_READ:
                printf("\rReading LBA: %-20" PRIu64 "", outerLBA);
                break;
            case RWV_COMMAND_WRITE:
                printf("\rWriting LBA: %-20" PRIu64 "", outerLBA);
                break;
            case RWV_COMMAND_VERIFY:
                printf("\rVerifying LBA: %-20" PRIu64 "", outerLBA);
                break;
            default:
                printf("\rUnknown OPing LBA: %-20" PRIu64 "", outerLBA);
                break;
            }
            flush_stdout();
        }
        if (SUCCESS != read_Write_Seek_Command(device, rwvcommand, outerLBA, dataBuf,
                                               C_CAST(uint32_t, currentSectorCount* get_Device_BlockSize(device))))
        {
            ret = FAILURE;
            // error occured, time to exit the loop
            break;
        }
        outerLBA += currentSectorCount;
        if (outerLBA >= return_Device_MaxLba(device)) // reset back to lba 0
        {
            outerLBA = 0;
        }
        // read the inner lba
        if (C_CAST(int64_t, innerLBA - sectorCount) < 0)
        {
            // adjust the sector count to get to 0 for the read
            currentSectorCount = C_CAST(uint32_t, innerLBA); // this should set us up to read the remaining sectors to 0
        }
        if (updateFunction != M_NULLPTR)
        {
            op_emit_lba_cb(updateFunction, updateData, "diameter_test", innerLBA, M_NULLPTR);
        }
        if (VERBOSITY_QUIET < device->deviceVerbosity && !hideLBACounter)
        {
            switch (rwvcommand)
            {
            case RWV_COMMAND_READ:
                printf("\rReading LBA: %-20" PRIu64 "", innerLBA);
                break;
            case RWV_COMMAND_WRITE:
                printf("\rWriting LBA: %-20" PRIu64 "", innerLBA);
                break;
            case RWV_COMMAND_VERIFY:
                printf("\rVerifying LBA: %-20" PRIu64 "", innerLBA);
                break;
            default:
                printf("\rUnknown OPing LBA: %-20" PRIu64 "", innerLBA);
                break;
            }
            flush_stdout();
        }
        if (SUCCESS != read_Write_Seek_Command(device, rwvcommand, innerLBA, dataBuf,
                                               C_CAST(uint32_t, currentSectorCount* get_Device_BlockSize(device))))
        {
            ret = FAILURE;
            // error occured, time to exit the loop
            break;
        }
        // always adjust the innerLBA
        innerLBA -= currentSectorCount;
        if (innerLBA == 0) // time to reset to the maxLBA
        {
            innerLBA = return_Device_MaxLba(device) - sectorCount;
        }
        // set the sector count back to what it was before the next iteration in the loop
        if (currentSectorCount != sectorCount)
        {
            currentSectorCount = sectorCount;
        }
    }
    safe_free(&dataBuf);
    if (VERBOSITY_QUIET < device->deviceVerbosity)
    {
        print_str("\n");
    }
    return ret;
}

M_PARAM_RO(1)
OPENSEA_OPERATIONS_API eReturnValues random_Read_Test(const tDevice* M_NONNULL device,
                                                      uint64_t                 timeLimitSeconds,
                                                      custom_Update M_NULLABLE updateFunction,
                                                      void* M_NULLABLE         updateData,
                                                      bool                     hideLBACounter)
{
    return random_Test(device, RWV_COMMAND_READ, timeLimitSeconds, updateFunction, updateData, hideLBACounter);
}

M_PARAM_RO(1)
OPENSEA_OPERATIONS_API eReturnValues random_Write_Test(const tDevice* M_NONNULL device,
                                                       uint64_t                 timeLimitSeconds,
                                                       custom_Update M_NULLABLE updateFunction,
                                                       void* M_NULLABLE         updateData,
                                                       bool                     hideLBACounter)
{
    return random_Test(device, RWV_COMMAND_WRITE, timeLimitSeconds, updateFunction, updateData, hideLBACounter);
}

M_PARAM_RO(1)
OPENSEA_OPERATIONS_API eReturnValues random_Verify_Test(const tDevice* M_NONNULL device,
                                                        uint64_t                 timeLimitSeconds,
                                                        custom_Update M_NULLABLE updateFunction,
                                                        void* M_NULLABLE         updateData,
                                                        bool                     hideLBACounter)
{
    return random_Test(device, RWV_COMMAND_VERIFY, timeLimitSeconds, updateFunction, updateData, hideLBACounter);
}

M_PARAM_RO(1)
OPENSEA_OPERATIONS_API eReturnValues random_Test(const tDevice* M_NONNULL device,
                                                 eRWVCommandType          rwvcommand,
                                                 uint64_t                 timeLimitSeconds,
                                                 custom_Update M_NULLABLE updateFunction,
                                                 void* M_NULLABLE         updateData,
                                                 bool                     hideLBACounter)
{
    eReturnValues ret         = SUCCESS;
    time_t        startTime   = 0; // will be set to actual current time before we start the test
    uint32_t      sectorCount = UINT32_C(1);
    uint8_t*      dataBuf     = M_NULLPTR;
    if (rwvcommand != RWV_COMMAND_VERIFY)
    {
        dataBuf = M_REINTERPRET_CAST(uint8_t*, safe_malloc(uint32_to_sizet(get_Device_BlockSize(device)) *
                                                           uint32_to_sizet(sectorCount) * sizeof(uint8_t)));
        if (dataBuf == M_NULLPTR)
        {
            return MEMORY_FAILURE;
        }
    }
    seed_64(C_CAST(uint64_t, time(M_NULLPTR))); // start the seed for the random number generator
    startTime       = time(M_NULLPTR);          // get the starting time before starting the loop
    double lastTime = 0.0;
    while (C_CAST(uint64_t, (lastTime = difftime(time(M_NULLPTR), startTime))) < timeLimitSeconds)
    {
        uint64_t randomLBA = random_Range_64(0, return_Device_MaxLba(device));
        if (updateFunction != M_NULLPTR)
        {
            op_emit_lba_cb(updateFunction, updateData, "random_test", randomLBA, M_NULLPTR);
        }
        if (VERBOSITY_QUIET < device->deviceVerbosity && !hideLBACounter)
        {
            switch (rwvcommand)
            {
            case RWV_COMMAND_READ:
                printf("\rReading LBA: %-20" PRIu64 "", randomLBA);
                break;
            case RWV_COMMAND_WRITE:
                printf("\rWriting LBA: %-20" PRIu64 "", randomLBA);
                break;
            case RWV_COMMAND_VERIFY:
                printf("\rVerifying LBA: %-20" PRIu64 "", randomLBA);
                break;
            default:
                printf("\rUnknown OPing LBA: %-20" PRIu64 "", randomLBA);
                break;
            }
            flush_stdout();
        }
        if (SUCCESS != read_Write_Seek_Command(device, rwvcommand, randomLBA, dataBuf,
                                               C_CAST(uint32_t, sectorCount* get_Device_BlockSize(device))))
        {
            ret = FAILURE;
            // error occured, time to exit the loop
            break;
        }
    }
    if (VERBOSITY_QUIET < device->deviceVerbosity)
    {
        print_str("\n");
    }
    safe_free(&dataBuf);
    return ret;
}

M_PARAM_RO(1)
OPENSEA_OPERATIONS_API eReturnValues random_Read_Test_With_Range(const tDevice* M_NONNULL device,
                                                                 uint64_t                 startLBA,
                                                                 uint64_t                 endLBA,
                                                                 uint16_t                 numberOfSeeks,
                                                                 custom_Update M_NULLABLE updateFunction,
                                                                 void* M_NULLABLE         updateData,
                                                                 bool                     hideLBACounter)
{
    return random_Test_With_Range(device, RWV_COMMAND_READ, startLBA, endLBA, numberOfSeeks, updateFunction, updateData,
                                  hideLBACounter);
}

M_PARAM_RO(1)
OPENSEA_OPERATIONS_API eReturnValues random_Write_Test_With_Range(const tDevice* M_NONNULL device,
                                                                  uint64_t                 startLBA,
                                                                  uint64_t                 endLBA,
                                                                  uint16_t                 numberOfSeeks,
                                                                  custom_Update M_NULLABLE updateFunction,
                                                                  void* M_NULLABLE         updateData,
                                                                  bool                     hideLBACounter)
{
    return random_Test_With_Range(device, RWV_COMMAND_WRITE, startLBA, endLBA, numberOfSeeks, updateFunction,
                                  updateData, hideLBACounter);
}

M_PARAM_RO(1)
OPENSEA_OPERATIONS_API eReturnValues random_Verify_Test_With_Range(const tDevice* M_NONNULL device,
                                                                   uint64_t                 startLBA,
                                                                   uint64_t                 endLBA,
                                                                   uint16_t                 numberOfSeeks,
                                                                   custom_Update M_NULLABLE updateFunction,
                                                                   void* M_NULLABLE         updateData,
                                                                   bool                     hideLBACounter)
{
    return random_Test_With_Range(device, RWV_COMMAND_VERIFY, startLBA, endLBA, numberOfSeeks, updateFunction,
                                  updateData, hideLBACounter);
}

M_PARAM_RO(1)
OPENSEA_OPERATIONS_API eReturnValues random_Test_With_Range(const tDevice* M_NONNULL device,
                                                            eRWVCommandType          rwvcommand,
                                                            uint64_t                 startLBA,
                                                            uint64_t                 endLBA,
                                                            uint16_t                 numberOfSeeks,
                                                            custom_Update M_NULLABLE updateFunction,
                                                            void* M_NULLABLE         updateData,
                                                            bool                     hideLBACounter)
{
    eReturnValues ret         = SUCCESS;
    uint64_t      iterator    = UINT64_C(0);
    uint32_t      sectorCount = UINT32_C(1);
    uint8_t*      dataBuf     = M_NULLPTR;

    if (startLBA > endLBA || endLBA > return_Device_MaxLba(device))
    {
        return BAD_PARAMETER;
    }

    if (rwvcommand != RWV_COMMAND_VERIFY)
    {
        dataBuf = M_REINTERPRET_CAST(uint8_t*, safe_malloc(uint32_to_sizet(get_Device_BlockSize(device)) *
                                                           uint32_to_sizet(sectorCount) * sizeof(uint8_t)));
        if (dataBuf == M_NULLPTR)
        {
            return MEMORY_FAILURE;
        }
    }

    seed_64(C_CAST(uint64_t, time(M_NULLPTR))); // start the seed for the random number generator
    for (iterator = 0; iterator < numberOfSeeks; iterator++)
    {
        uint64_t randomLBA = random_Range_64(startLBA, endLBA);
        if (updateFunction != M_NULLPTR)
        {
            op_emit_lba_cb(updateFunction, updateData, "random_test", randomLBA, M_NULLPTR);
        }
        if (VERBOSITY_QUIET < device->deviceVerbosity && !hideLBACounter)
        {
            switch (rwvcommand)
            {
            case RWV_COMMAND_READ:
                printf("\rReading LBA: %-20" PRIu64 "", randomLBA);
                break;
            case RWV_COMMAND_WRITE:
                printf("\rWriting LBA: %-20" PRIu64 "", randomLBA);
                break;
            case RWV_COMMAND_VERIFY:
                printf("\rVerifying LBA: %-20" PRIu64 "", randomLBA);
                break;
            default:
                printf("\rUnknown OPing LBA: %-20" PRIu64 "", randomLBA);
                break;
            }
            flush_stdout();
        }
        if (SUCCESS != read_Write_Seek_Command(device, rwvcommand, randomLBA, dataBuf,
                                               C_CAST(uint32_t, sectorCount* get_Device_BlockSize(device))))
        {
            ret = FAILURE;
            // error occurred, time to exit the loop
            break;
        }
    }
    if (VERBOSITY_QUIET < device->deviceVerbosity)
    {
        print_str("\n");
    }
    safe_free(&dataBuf);
    return ret;
}

M_PARAM_RO(1)
OPENSEA_OPERATIONS_API eReturnValues butterfly_Read_Test_With_Range(const tDevice* M_NONNULL device,
                                                                    uint64_t                 startLBA,
                                                                    uint64_t                 endLBA,
                                                                    uint16_t                 numberOfSeeks,
                                                                    custom_Update M_NULLABLE updateFunction,
                                                                    void* M_NULLABLE         updateData,
                                                                    bool                     hideLBACounter)
{
    return butterfly_Test_With_Range(device, RWV_COMMAND_READ, startLBA, endLBA, numberOfSeeks, updateFunction,
                                     updateData, hideLBACounter);
}

M_PARAM_RO(1)
OPENSEA_OPERATIONS_API eReturnValues butterfly_Write_Test_With_Range(const tDevice* M_NONNULL device,
                                                                     uint64_t                 startLBA,
                                                                     uint64_t                 endLBA,
                                                                     uint16_t                 numberOfSeeks,
                                                                     custom_Update M_NULLABLE updateFunction,
                                                                     void* M_NULLABLE         updateData,
                                                                     bool                     hideLBACounter)
{
    return butterfly_Test_With_Range(device, RWV_COMMAND_WRITE, startLBA, endLBA, numberOfSeeks, updateFunction,
                                     updateData, hideLBACounter);
}

M_PARAM_RO(1)
OPENSEA_OPERATIONS_API eReturnValues butterfly_Verify_Test_With_Range(const tDevice* M_NONNULL device,
                                                                      uint64_t                 startLBA,
                                                                      uint64_t                 endLBA,
                                                                      uint16_t                 numberOfSeeks,
                                                                      custom_Update M_NULLABLE updateFunction,
                                                                      void* M_NULLABLE         updateData,
                                                                      bool                     hideLBACounter)
{
    return butterfly_Test_With_Range(device, RWV_COMMAND_VERIFY, startLBA, endLBA, numberOfSeeks, updateFunction,
                                     updateData, hideLBACounter);
}

M_PARAM_RO(1)
OPENSEA_OPERATIONS_API eReturnValues butterfly_Test_With_Range(const tDevice* M_NONNULL device,
                                                               eRWVCommandType          rwvcommand,
                                                               uint64_t                 startLBA,
                                                               uint64_t                 endLBA,
                                                               uint16_t                 numberOfSeeks,
                                                               custom_Update M_NULLABLE updateFunction,
                                                               void* M_NULLABLE         updateData,
                                                               bool                     hideLBACounter)
{
    eReturnValues ret                = SUCCESS;
    uint64_t      outerLBA           = UINT64_C(0);
    uint64_t      innerLBA           = UINT64_C(0);
    uint64_t      iterator           = UINT64_C(0);
    uint32_t      sectorCount        = get_Sector_Count_For_Read_Write(device);
    uint32_t      currentSectorCount = sectorCount;
    uint8_t*      dataBuf            = M_NULLPTR;
    size_t        dataBufSize        = SIZE_T_C(0);

    if (startLBA > endLBA || endLBA > return_Device_MaxLba(device))
    {
        return BAD_PARAMETER;
    }

    if (rwvcommand != RWV_COMMAND_VERIFY)
    {
        dataBufSize = uint32_to_sizet(get_Device_BlockSize(device)) * uint32_to_sizet(sectorCount) * sizeof(uint8_t);
        dataBuf =
            M_REINTERPRET_CAST(uint8_t*, safe_malloc_aligned(dataBufSize, get_Device_IO_Minimum_Alignment(device)));
        if (dataBuf == M_NULLPTR)
        {
            return MEMORY_FAILURE;
        }
    }

    // Initialize outer and inner LBAs for the butterfly pattern
    outerLBA = startLBA;
    innerLBA = endLBA - sectorCount;
    if (innerLBA < startLBA)
    {
        innerLBA = endLBA;
    }

    for (iterator = 0; iterator < numberOfSeeks; iterator++)
    {
        // Read/write the outer LBA
        currentSectorCount = sectorCount;
        if ((outerLBA + sectorCount) > endLBA)
        {
            currentSectorCount = C_CAST(uint32_t, endLBA - outerLBA);
        }
        if (currentSectorCount == 0)
        {
            currentSectorCount = sectorCount;
        }
        if (updateFunction != M_NULLPTR)
        {
            op_emit_lba_cb(updateFunction, updateData, "butterfly_test", outerLBA, M_NULLPTR);
        }
        if (VERBOSITY_QUIET < device->deviceVerbosity && !hideLBACounter)
        {
            switch (rwvcommand)
            {
            case RWV_COMMAND_READ:
                printf("\rReading LBA: %-20" PRIu64 "", outerLBA);
                break;
            case RWV_COMMAND_WRITE:
                printf("\rWriting LBA: %-20" PRIu64 "", outerLBA);
                break;
            case RWV_COMMAND_VERIFY:
                printf("\rVerifying LBA: %-20" PRIu64 "", outerLBA);
                break;
            default:
                printf("\rUnknown OPing LBA: %-20" PRIu64 "", outerLBA);
                break;
            }
            flush_stdout();
        }
        if (SUCCESS != read_Write_Seek_Command(device, rwvcommand, outerLBA, dataBuf,
                                               C_CAST(uint32_t, currentSectorCount* get_Device_BlockSize(device))))
        {
            ret = FAILURE;
            break;
        }
        outerLBA += currentSectorCount;
        if (outerLBA > endLBA)
        {
            outerLBA = startLBA;
        }

        // Read/write the inner LBA
        currentSectorCount = sectorCount;
        if (C_CAST(int64_t, innerLBA) - C_CAST(int64_t, sectorCount) < C_CAST(int64_t, startLBA))
        {
            currentSectorCount = C_CAST(uint32_t, innerLBA - startLBA);
        }
        if (currentSectorCount == 0)
        {
            currentSectorCount = sectorCount;
        }
        if (updateFunction != M_NULLPTR)
        {
            op_emit_lba_cb(updateFunction, updateData, "butterfly_test", innerLBA, M_NULLPTR);
        }
        if (VERBOSITY_QUIET < device->deviceVerbosity && !hideLBACounter)
        {
            switch (rwvcommand)
            {
            case RWV_COMMAND_READ:
                printf("\rReading LBA: %-20" PRIu64 "", innerLBA);
                break;
            case RWV_COMMAND_WRITE:
                printf("\rWriting LBA: %-20" PRIu64 "", innerLBA);
                break;
            case RWV_COMMAND_VERIFY:
                printf("\rVerifying LBA: %-20" PRIu64 "", innerLBA);
                break;
            default:
                printf("\rUnknown OPing LBA: %-20" PRIu64 "", innerLBA);
                break;
            }
            flush_stdout();
        }
        if (SUCCESS != read_Write_Seek_Command(device, rwvcommand, innerLBA, dataBuf,
                                               C_CAST(uint32_t, currentSectorCount* get_Device_BlockSize(device))))
        {
            ret = FAILURE;
            break;
        }
        innerLBA -= currentSectorCount;
        if (innerLBA <= startLBA)
        {
            innerLBA = endLBA - sectorCount;
            if (C_CAST(int64_t, innerLBA) < C_CAST(int64_t, startLBA))
            {
                innerLBA = endLBA;
            }
        }
    }
    if (VERBOSITY_QUIET < device->deviceVerbosity)
    {
        print_str("\n");
    }
    safe_free_aligned(&dataBuf);
    return ret;
}

M_PARAM_RO(1)
OPENSEA_OPERATIONS_API eReturnValues sweep_Test(const tDevice* M_NONNULL device,
                                                eRWVCommandType          rwvcommand,
                                                uint32_t                 sweepCount)
{
    eReturnValues ret         = SUCCESS;
    uint32_t      sectorCount = UINT32_C(1);
    uint8_t*      dataBuf     = M_NULLPTR;
    if (rwvcommand != RWV_COMMAND_VERIFY)
    {
        dataBuf = M_REINTERPRET_CAST(uint8_t*, safe_malloc(uint32_to_sizet(get_Device_BlockSize(device)) *
                                                           uint32_to_sizet(sectorCount) * sizeof(uint8_t)));
        if (dataBuf == M_NULLPTR)
        {
            return MEMORY_FAILURE;
        }
    }

    for (uint32_t testCount = UINT32_C(0); testCount < sweepCount; testCount++)
    {
        if (VERBOSITY_QUIET < device->deviceVerbosity)
        {
            printf("\rSweep Test count %" PRIu32 "", (testCount + 1));
            flush_stdout();
        }
        // seek to OD
        if (SUCCESS != read_Write_Seek_Command(device, rwvcommand, 0, dataBuf,
                                               C_CAST(uint32_t, sectorCount* get_Device_BlockSize(device))))
        {
            ret = FAILURE;
            // error occured, time to exit the loop
            break;
        }

        // seek to ID
        if (SUCCESS != read_Write_Seek_Command(device, rwvcommand, return_Device_MaxLba(device), dataBuf,
                                               C_CAST(uint32_t, sectorCount* get_Device_BlockSize(device))))
        {
            ret = FAILURE;
            // error occured, time to exit the loop
            break;
        }
    }

    if (device->deviceVerbosity > VERBOSITY_QUIET)
    {
        print_str("\n");
    }

    return ret;
}

M_PARAM_RO(1)
M_PARAM_WO(4)
M_PARAM_WO(5)
OPENSEA_OPERATIONS_API eReturnValues read_Write_Or_Verify_Timed_Test(const tDevice* M_NONNULL device,
                                                                     eRWVCommandType          testMode,
                                                                     uint32_t                 timePerTestSeconds,
                                                                     uint16_t* M_NONNULL      numberOfCommandTimeouts,
                                                                     uint16_t* M_NONNULL      numberOfCommandFailures,
                                                                     custom_Update M_NULLABLE updateFunction,
                                                                     void* M_NULLABLE         updateData)
{
    errno_t msgerror = 0;
    DECLARE_ZERO_INIT_ARRAY(char, message, JSON_REPORT_TEST_STEP_MESSAGE_LENGTH);
    DECLARE_ZERO_INIT_ARRAY(char, timeStr, TIME_STRING_LENGTH);
    uint8_t* dataBuf            = M_NULLPTR;
    size_t   dataBufSize        = SIZE_T_C(0);
    time_t   startTime          = 0;
    uint64_t IDStartLBA         = UINT64_C(0);
    uint64_t ODEndingLBA        = UINT64_C(0);
    uint64_t randomLBA          = UINT64_C(0);
    uint64_t outerLBA           = UINT64_C(0);
    uint64_t innerLBA           = return_Device_MaxLba(device);
    uint32_t sectorCount        = get_Sector_Count_For_Read_Write(device);
    uint32_t currentSectorCount = sectorCount;
    // set timestr to time in seconds for reporting
    int timestrformat = snprintf_err_handle(timeStr, TIME_STRING_LENGTH, " %" PRIu32 " seconds", timePerTestSeconds);
    if (device->deviceVerbosity > VERBOSITY_QUIET)
    {
        print_str("\n");
    }
    // OD
    if (testMode == RWV_COMMAND_READ || testMode == RWV_COMMAND_WRITE)
    {
        dataBufSize = uint32_to_sizet(get_Device_BlockSize(device)) * uint32_to_sizet(sectorCount) * sizeof(uint8_t);
        dataBuf =
            M_REINTERPRET_CAST(uint8_t*, safe_malloc_aligned(dataBufSize, get_Device_IO_Minimum_Alignment(device)));
        if (dataBuf == M_NULLPTR)
        {
            perror("failed to allocate memory!\n");
            return MEMORY_FAILURE;
        }
        if (testMode == RWV_COMMAND_WRITE)
        {
            if (0 != safe_memset(dataBuf, dataBufSize, 0, dataBufSize))
                M_UNLIKELY
                {
                    perror("failed to set memory to zero for writes!\n");
                }
        }
    }
    switch (testMode)
    {
    case RWV_COMMAND_READ:
        msgerror = safe_strcat(message, JSON_REPORT_TEST_STEP_MESSAGE_LENGTH, "Sequential Read Test at OD for ~");
        break;
    case RWV_COMMAND_WRITE:
        msgerror = safe_strcat(message, JSON_REPORT_TEST_STEP_MESSAGE_LENGTH, "Sequential Write Test at OD for ~");
        break;
    case RWV_COMMAND_VERIFY:
        msgerror = safe_strcat(message, JSON_REPORT_TEST_STEP_MESSAGE_LENGTH, "Sequential Verify Test at OD for ~");
        break;
    default:
        msgerror = safe_strcat(message, JSON_REPORT_TEST_STEP_MESSAGE_LENGTH, "Unknown Sequential Test at OD for ~");
        break;
    }
    if (msgerror == 0)
    {
        if (device->deviceVerbosity > VERBOSITY_QUIET)
        {
            uint16_t days    = UINT16_C(0);
            uint8_t  hours   = UINT8_C(0);
            uint8_t  minutes = UINT8_C(0);
            uint8_t  seconds = UINT8_C(0);
            print_str(message);
            convert_Seconds_To_Displayable_Time(timePerTestSeconds, M_NULLPTR, &days, &hours, &minutes, &seconds);
            print_Time_To_Screen(M_NULLPTR, &days, &hours, &minutes, &seconds);
            print_str("\n");
        }
        if (updateFunction != M_NULLPTR && timestrformat >= 0)
        {
            if (0 == safe_strcat(message, JSON_REPORT_TEST_STEP_MESSAGE_LENGTH, timeStr))
            {
                op_emit_step_cb(updateFunction, updateData, "timed_test", message);
            }
        }
    }
    startTime = time(M_NULLPTR);
    while (difftime(time(M_NULLPTR), startTime) < timePerTestSeconds && ODEndingLBA < return_Device_MaxLba(device))
    {

        if (VERBOSITY_QUIET < device->deviceVerbosity)
        {
            switch (testMode)
            {
            case RWV_COMMAND_WRITE:
                printf("\rWriting LBA: %-20" PRIu64 "",
                       ODEndingLBA); // 20 wide is the max width for a unsigned 64bit number
                break;
            case RWV_COMMAND_READ:
                printf("\rReading LBA: %-20" PRIu64 "",
                       ODEndingLBA); // 20 wide is the max width for a unsigned 64bit number
                break;
            case RWV_COMMAND_VERIFY:
            default:
                printf("\rVerifying LBA: %-20" PRIu64 "",
                       ODEndingLBA); // 20 wide is the max width for a unsigned 64bit number
                break;
            }
            flush_stdout();
        }
        switch (read_Write_Seek_Command(device, testMode, ODEndingLBA, dataBuf,
                                        C_CAST(uint32_t, sectorCount* get_Device_BlockSize(device))))
        {
        case SUCCESS:
            break;
        case OS_COMMAND_TIMEOUT:
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
        print_str("\n");
    }
    // ID
    switch (testMode)
    {
    case RWV_COMMAND_READ:
        msgerror = safe_strcat(message, JSON_REPORT_TEST_STEP_MESSAGE_LENGTH, "Sequential Read Test at ID for ~");
        break;
    case RWV_COMMAND_WRITE:
        msgerror = safe_strcat(message, JSON_REPORT_TEST_STEP_MESSAGE_LENGTH, "Sequential Write Test at ID for ~");
        break;
    case RWV_COMMAND_VERIFY:
        msgerror = safe_strcat(message, JSON_REPORT_TEST_STEP_MESSAGE_LENGTH, "Sequential Verify Test at ID for ~");
        break;
    default:
        msgerror = safe_strcat(message, JSON_REPORT_TEST_STEP_MESSAGE_LENGTH, "Unknown Sequential Test at ID for ~");
        break;
    }
    if (msgerror == 0)
    {
        if (device->deviceVerbosity > VERBOSITY_QUIET)
        {
            uint16_t days    = UINT16_C(0);
            uint8_t  hours   = UINT8_C(0);
            uint8_t  minutes = UINT8_C(0);
            uint8_t  seconds = UINT8_C(0);
            print_str(message);
            convert_Seconds_To_Displayable_Time(timePerTestSeconds, M_NULLPTR, &days, &hours, &minutes, &seconds);
            print_Time_To_Screen(M_NULLPTR, &days, &hours, &minutes, &seconds);
            print_str("\n");
        }
        if (updateFunction != M_NULLPTR && timestrformat >= 0)
        {
            if (0 == safe_strcat(message, JSON_REPORT_TEST_STEP_MESSAGE_LENGTH, timeStr))
            {
                op_emit_step_cb(updateFunction, updateData, "timed_test", message);
            }
        }
    }
    IDStartLBA = return_Device_MaxLba(device) - ODEndingLBA;
    startTime  = time(M_NULLPTR);
    while (difftime(time(M_NULLPTR), startTime) < timePerTestSeconds && IDStartLBA < return_Device_MaxLba(device))
    {
        if (updateFunction != M_NULLPTR)
        {
            op_emit_lba_cb(updateFunction, updateData, "timed_test", IDStartLBA, M_NULLPTR);
        }
        if (VERBOSITY_QUIET < device->deviceVerbosity)
        {
            switch (testMode)
            {
            case RWV_COMMAND_WRITE:
                printf("\rWriting LBA: %-20" PRIu64 "",
                       IDStartLBA); // 20 wide is the max width for a unsigned 64bit number
                break;
            case RWV_COMMAND_READ:
                printf("\rReading LBA: %-20" PRIu64 "",
                       IDStartLBA); // 20 wide is the max width for a unsigned 64bit number
                break;
            case RWV_COMMAND_VERIFY:
            default:
                printf("\rVerifying LBA: %-20" PRIu64 "",
                       IDStartLBA); // 20 wide is the max width for a unsigned 64bit number
                break;
            }
            flush_stdout();
        }
        switch (read_Write_Seek_Command(device, testMode, IDStartLBA, dataBuf,
                                        C_CAST(uint32_t, sectorCount* get_Device_BlockSize(device))))
        {
        case SUCCESS:
            break;
        case OS_COMMAND_TIMEOUT:
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
        print_str("\n");
    }
    // Random
    seed_64(C_CAST(uint64_t, time(M_NULLPTR))); // start random number generator
    switch (testMode)
    {
    case RWV_COMMAND_READ:
        msgerror = safe_strcat(message, JSON_REPORT_TEST_STEP_MESSAGE_LENGTH, "Random Read Test for ~");
        break;
    case RWV_COMMAND_WRITE:
        msgerror = safe_strcat(message, JSON_REPORT_TEST_STEP_MESSAGE_LENGTH, "Random Write Test for ~");
        break;
    case RWV_COMMAND_VERIFY:
        msgerror = safe_strcat(message, JSON_REPORT_TEST_STEP_MESSAGE_LENGTH, "Random Verify Test for ~");
        break;
    default:
        msgerror = safe_strcat(message, JSON_REPORT_TEST_STEP_MESSAGE_LENGTH, "Unknown Random Test for ~");
        break;
    }
    if (msgerror == 0)
    {
        if (device->deviceVerbosity > VERBOSITY_QUIET)
        {
            uint16_t days    = UINT16_C(0);
            uint8_t  hours   = UINT8_C(0);
            uint8_t  minutes = UINT8_C(0);
            uint8_t  seconds = UINT8_C(0);
            print_str(message);
            convert_Seconds_To_Displayable_Time(timePerTestSeconds, M_NULLPTR, &days, &hours, &minutes, &seconds);
            print_Time_To_Screen(M_NULLPTR, &days, &hours, &minutes, &seconds);
            print_str("\n");
        }
        if (updateFunction != M_NULLPTR && timestrformat >= 0)
        {
            if (0 == safe_strcat(message, JSON_REPORT_TEST_STEP_MESSAGE_LENGTH, timeStr))
            {
                op_emit_step_cb(updateFunction, updateData, "random_test", message);
            }
        }
    }
    startTime = time(M_NULLPTR);
    while (difftime(time(M_NULLPTR), startTime) < timePerTestSeconds)
    {
        randomLBA = random_Range_64(0, return_Device_MaxLba(device));
        if (updateFunction != M_NULLPTR)
        {
            op_emit_lba_cb(updateFunction, updateData, "random_test", randomLBA, M_NULLPTR);
        }
        if (VERBOSITY_QUIET < device->deviceVerbosity)
        {
            switch (testMode)
            {
            case RWV_COMMAND_WRITE:
                printf("\rWriting LBA: %-20" PRIu64 "",
                       randomLBA); // 20 wide is the max width for a unsigned 64bit number
                break;
            case RWV_COMMAND_READ:
                printf("\rReading LBA: %-20" PRIu64 "",
                       randomLBA); // 20 wide is the max width for a unsigned 64bit number
                break;
            case RWV_COMMAND_VERIFY:
            default:
                printf("\rVerifying LBA: %-20" PRIu64 "",
                       randomLBA); // 20 wide is the max width for a unsigned 64bit number
                break;
            }
            flush_stdout();
        }
        switch (read_Write_Seek_Command(device, testMode, randomLBA, dataBuf,
                                        C_CAST(uint32_t, 1 * get_Device_BlockSize(device))))
        {
        case SUCCESS:
            break;
        case OS_COMMAND_TIMEOUT:
            (*numberOfCommandTimeouts)++;
            break;
        default:
            (*numberOfCommandFailures)++;
            break;
        }
    }
    if (device->deviceVerbosity > VERBOSITY_QUIET)
    {
        print_str("\n");
    }
    // Butterfly
    // outerLBA = ODEndingLBA;
    innerLBA -= sectorCount; // -ODEndingLBA;
    switch (testMode)
    {
    case RWV_COMMAND_READ:
        msgerror = safe_strcat(message, JSON_REPORT_TEST_STEP_MESSAGE_LENGTH, "Butterfly Read Test for ~");
        break;
    case RWV_COMMAND_WRITE:
        msgerror = safe_strcat(message, JSON_REPORT_TEST_STEP_MESSAGE_LENGTH, "Butterfly Write Test for ~");
        break;
    case RWV_COMMAND_VERIFY:
        msgerror = safe_strcat(message, JSON_REPORT_TEST_STEP_MESSAGE_LENGTH, "Butterfly Verify Test for ~");
        break;
    default:
        msgerror = safe_strcat(message, JSON_REPORT_TEST_STEP_MESSAGE_LENGTH, "Unknown Butterfly Test for ~");
        break;
    }
    if (msgerror == 0)
    {
        if (device->deviceVerbosity > VERBOSITY_QUIET)
        {
            uint16_t days    = UINT16_C(0);
            uint8_t  hours   = UINT8_C(0);
            uint8_t  minutes = UINT8_C(0);
            uint8_t  seconds = UINT8_C(0);
            print_str(message);
            convert_Seconds_To_Displayable_Time(timePerTestSeconds, M_NULLPTR, &days, &hours, &minutes, &seconds);
            print_Time_To_Screen(M_NULLPTR, &days, &hours, &minutes, &seconds);
            print_str("\n");
        }
        if (updateFunction != M_NULLPTR && timestrformat >= 0)
        {
            if (0 == safe_strcat(message, JSON_REPORT_TEST_STEP_MESSAGE_LENGTH, timeStr))
            {
                op_emit_step_cb(updateFunction, updateData, "user_sequential", message);
            }
        }
    }
    currentSectorCount = sectorCount = get_Sector_Count_For_Read_Write(device);
    startTime                        = time(M_NULLPTR);
    while (difftime(time(M_NULLPTR), startTime) < timePerTestSeconds)
    {
        // read the outer lba
        if ((outerLBA + sectorCount) > return_Device_MaxLba(device))
        {
            // adjust the sector count to get to the maxLBA for the read
            currentSectorCount = C_CAST(uint32_t, return_Device_MaxLba(device) - outerLBA);
        }
        if (updateFunction != M_NULLPTR)
        {
            op_emit_lba_cb(updateFunction, updateData, "outer_diameter", outerLBA, M_NULLPTR);
        }
        if (VERBOSITY_QUIET < device->deviceVerbosity)
        {
            switch (testMode)
            {
            case RWV_COMMAND_WRITE:
                printf("\rWriting LBA: %-20" PRIu64 "",
                       outerLBA); // 20 wide is the max width for a unsigned 64bit number
                break;
            case RWV_COMMAND_READ:
                printf("\rReading LBA: %-20" PRIu64 "",
                       outerLBA); // 20 wide is the max width for a unsigned 64bit number
                break;
            case RWV_COMMAND_VERIFY:
            default:
                printf("\rVerifying LBA: %-20" PRIu64 "",
                       outerLBA); // 20 wide is the max width for a unsigned 64bit number
                break;
            }
            flush_stdout();
        }
        switch (read_Write_Seek_Command(device, testMode, outerLBA, dataBuf,
                                        C_CAST(uint32_t, currentSectorCount* get_Device_BlockSize(device))))
        {
        case SUCCESS:
            break;
        case OS_COMMAND_TIMEOUT:
            (*numberOfCommandTimeouts)++;
            break;
        default:
            (*numberOfCommandFailures)++;
            break;
        }
        outerLBA += currentSectorCount;
        if (outerLBA >= return_Device_MaxLba(device)) // reset back to lba 0
        {
            outerLBA = 0;
        }
        // read the inner lba
        if (C_CAST(int64_t, innerLBA - sectorCount) < 0)
        {
            // adjust the sector count to get to 0 for the read
            currentSectorCount = C_CAST(uint32_t, innerLBA); // this should set us up to read the remaining sectors to 0
        }
        if (updateFunction != M_NULLPTR)
        {
            op_emit_lba_cb(updateFunction, updateData, "inner_diameter", innerLBA, M_NULLPTR);
        }
        if (VERBOSITY_QUIET < device->deviceVerbosity)
        {
            switch (testMode)
            {
            case RWV_COMMAND_WRITE:
                printf("\rWriting LBA: %-20" PRIu64 "",
                       innerLBA); // 20 wide is the max width for a unsigned 64bit number
                break;
            case RWV_COMMAND_READ:
                printf("\rReading LBA: %-20" PRIu64 "",
                       innerLBA); // 20 wide is the max width for a unsigned 64bit number
                break;
            case RWV_COMMAND_VERIFY:
            default:
                printf("\rVerifying LBA: %-20" PRIu64 "",
                       innerLBA); // 20 wide is the max width for a unsigned 64bit number
                break;
            }
            flush_stdout();
        }
        switch (read_Write_Seek_Command(device, testMode, innerLBA, dataBuf,
                                        C_CAST(uint32_t, currentSectorCount* get_Device_BlockSize(device))))
        {
        case SUCCESS:
            break;
        case OS_COMMAND_TIMEOUT:
            (*numberOfCommandTimeouts)++;
            break;
        default:
            (*numberOfCommandFailures)++;
            break;
        }
        // always adjust the innerLBA
        innerLBA -= currentSectorCount;
        if (innerLBA == 0) // time to reset to the maxLBA
        {
            innerLBA = return_Device_MaxLba(device) - sectorCount;
        }
        // set the sector count back to what it was before the next iteration in the loop
        if (currentSectorCount != sectorCount)
        {
            currentSectorCount = sectorCount;
        }
    }
    if (device->deviceVerbosity > VERBOSITY_QUIET)
    {
        print_str("\n");
    }
    safe_free(&dataBuf);
    return SUCCESS;
}

// This function is very similar to the "user_Sequential_Test" call, but the error list is allocated outside of this
// function instead of having it self contained. Rather than change the user_Sequential_Test and make it potentially
// break others or complicated its already long list of parameters, I wrote this function instead.
M_PARAM_RO(1)
M_PARAM_RW(6)
M_PARAM_RW(7)
static eReturnValues diameter_Test_RWV_Range(const tDevice* M_NONNULL device,
                                             eRWVCommandType          rwvCommand,
                                             uint64_t                 startingLBA,
                                             uint64_t                 range,
                                             uint16_t                 errorLimit,
                                             errorLBA* M_NONNULL      errorList,
                                             uint16_t* M_NONNULL      errorOffset,
                                             bool                     stopOnError,
                                             bool                     repairOnTheFly,
                                             custom_Update M_NULLABLE updateFunction,
                                             void* M_NULLABLE         updateData,
                                             bool                     hideLBACounter)
{
    eReturnValues ret                 = SUCCESS;
    bool          errorLimitReached   = false;
    uint32_t      sectorCount         = get_Sector_Count_For_Read_Write(device);
    uint64_t      originalStartingLBA = startingLBA;
    uint64_t      originalRange       = range;
    // only one of these flags should be set. If they are both set, this makes no sense
    if (stopOnError)
    {
        // disable the repair flag
        repairOnTheFly = false;
    }
    if (errorLimit < 1)
    {
        // need to be able to store at least 1 error
        errorLimit = 1;
    }
    if (errorList == M_NULLPTR)
    {
        return BAD_PARAMETER;
    }
    errorList[*errorOffset].errorAddress = UINT64_MAX;
    // make sure the starting LBA is alligned? If we do this, we need to make sure we don't mess with the data of the
    // LBAs we don't mean to start at...mostly don't want to erase an LBA we shouldn't be starting at. startingLBA =
    // align_LBA(device, startingLBA);
    bool autoReadReassign  = false;
    bool autoWriteReassign = false;
    if (SUCCESS != get_Automatic_Reallocation_Support(device, &autoWriteReassign, &autoReadReassign))
    {
        autoWriteReassign = true; // just in case this fails, default to previous behavior
    }
    // this is essentially a loop over the sequential read function
    while (!errorLimitReached)
    {
        if (SUCCESS != sequential_RWV(device, rwvCommand, startingLBA, range, sectorCount,
                                      &errorList[*errorOffset].errorAddress, updateFunction, updateData,
                                      hideLBACounter))
        {
            if (device->deviceVerbosity > VERBOSITY_QUIET)
            {
                printf("\nError Found at LBA %" PRIu64 "\n", errorList[*errorOffset].errorAddress);
            }
            if (updateFunction != M_NULLPTR)
            {
                op_emit_error_lba_cb(updateFunction, updateData, "generic_test", errorList[*errorOffset].errorAddress,
                                     (int)FAILURE, M_NULLPTR);
            }
            // set a new start for next time through the loop to 1 lba past the last error LBA
            startingLBA = errorList[*errorOffset].errorAddress + 1;
            if (stopOnError || *errorOffset >= errorLimit)
            {
                errorLimitReached = true;
                ret               = FAILURE;
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
            // need to adjust the range after we hit an error lba!
            range = (originalStartingLBA + originalRange) - startingLBA;
        }
        else
        {
            break;
        }
    }
    if (device->deviceVerbosity > VERBOSITY_QUIET)
    {
        print_str("\n");
    }

    return ret;
}

// tests at OD, MD, and/or ID depending on what the caller requests.
M_PARAM_RO(1)
OPENSEA_OPERATIONS_API eReturnValues diameter_Test_Range(const tDevice* M_NONNULL device,
                                                         eRWVCommandType          testMode,
                                                         bool                     outer,
                                                         bool                     middle,
                                                         bool                     inner,
                                                         uint64_t                 numberOfLBAs,
                                                         uint16_t                 errorLimit,
                                                         bool                     stopOnError,
                                                         bool                     repairOnTheFly,
                                                         bool                     repairAtEnd,
                                                         custom_Update M_NULLABLE updateFunction,
                                                         void* M_NULLABLE         updateData,
                                                         bool                     hideLBACounter)
{
    eReturnValues ret       = SUCCESS;
    eReturnValues outerRet  = SUCCESS;
    eReturnValues innerRet  = SUCCESS;
    eReturnValues middleRet = SUCCESS;
    if ((repairOnTheFly && repairAtEnd) || errorLimit == 0)
    {
        return BAD_PARAMETER;
    }
    // eReturnValues user_Sequential_Test(const tDevice *device, eRWVCommandType rwvCommand, uint64_t startingLBA,
    // uint64_t range, uint16_t errorLimit, bool stopOnError, bool repairOnTheFly, bool repairAtEnd, custom_Update
    // updateFunction, void *updateData)
    errorLBA* errorList   = M_REINTERPRET_CAST(errorLBA*, safe_calloc(errorLimit * sizeof(errorLBA), sizeof(errorLBA)));
    uint16_t  errorOffset = UINT16_C(0);

    // OD
    if (outer && (ret == SUCCESS || (errorOffset < errorLimit && !stopOnError)))
    {
        if (device->deviceVerbosity > VERBOSITY_QUIET)
        {
            print_str("Outer Diameter Test\n");
        }
        if (updateFunction != M_NULLPTR)
        {
            op_emit_step_cb(updateFunction, updateData, "diameter_test", "Outer Diameter Test");
        }
        outerRet = diameter_Test_RWV_Range(device, testMode, 0, numberOfLBAs, errorLimit, errorList, &errorOffset,
                                           stopOnError, repairOnTheFly, updateFunction, updateData, hideLBACounter);
        if (device->deviceVerbosity > VERBOSITY_QUIET)
        {
            print_str("\n");
        }
    }
    if (outerRet != SUCCESS && ret == SUCCESS)
    {
        ret = outerRet;
    }
    // MD
    if (middle && (ret == SUCCESS || (errorOffset < errorLimit && !stopOnError)))
    {
        if (device->deviceVerbosity > VERBOSITY_QUIET)
        {
            print_str("Middle Diameter Test\n");
        }
        if (updateFunction != M_NULLPTR)
        {
            op_emit_step_cb(updateFunction, updateData, "diameter_test", "Middle Diameter Test");
        }
        middleRet = diameter_Test_RWV_Range(device, testMode, return_Device_MaxLba(device) / 2, numberOfLBAs,
                                            errorLimit, errorList, &errorOffset, stopOnError, repairOnTheFly,
                                            updateFunction, updateData, hideLBACounter);
        if (device->deviceVerbosity > VERBOSITY_QUIET)
        {
            print_str("\n");
        }
    }
    if (middleRet != SUCCESS && ret == SUCCESS)
    {
        ret = middleRet;
    }
    // ID
    if (inner && (ret == SUCCESS || (errorOffset < errorLimit && !stopOnError)))
    {
        if (device->deviceVerbosity > VERBOSITY_QUIET)
        {
            print_str("Inner Diameter Test\n");
        }
        if (updateFunction != M_NULLPTR)
        {
            op_emit_step_cb(updateFunction, updateData, "diameter_test", "Inner Diameter Test");
        }
        innerRet = diameter_Test_RWV_Range(device, testMode, return_Device_MaxLba(device) - numberOfLBAs + 1,
                                           numberOfLBAs, errorLimit, errorList, &errorOffset, stopOnError,
                                           repairOnTheFly, updateFunction, updateData, hideLBACounter);
        if (device->deviceVerbosity > VERBOSITY_QUIET)
        {
            print_str("\n");
        }
    }
    if (innerRet != SUCCESS && ret == SUCCESS)
    {
        ret = innerRet;
    }
    // handle repair at end if it was set
    if (repairAtEnd)
    {
        bool autoReadReassign  = false;
        bool autoWriteReassign = false;
        if (SUCCESS != get_Automatic_Reallocation_Support(device, &autoWriteReassign, &autoReadReassign))
        {
            autoWriteReassign = true; // just in case this fails, default to previous behavior
        }
        // go through and repair the LBAs
        uint64_t errorIter                 = UINT64_C(0);
        uint64_t lastLBARepaired           = UINT64_MAX;
        uint16_t logicalPerPhysicalSectors = get_Logical_Sectors_Per_Physical_Sector(device);
        for (errorIter = 0; errorIter < errorOffset; errorIter++)
        {
            if (lastLBARepaired != UINT64_MAX)
            {
                // check if the LBA we want to repair is within the same physical sector as the last LBA
                if ((lastLBARepaired + logicalPerPhysicalSectors) > errorList[errorIter].errorAddress)
                {
                    // in this case, we have already repaired this LBA since the repair is issued to the physical
                    // sector, so move on to the next thing in the list
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
    // handle stopping on the error we got
    if (stopOnError && errorList[0].errorAddress != UINT64_MAX)
    {
        if (device->deviceVerbosity > VERBOSITY_QUIET)
        {
            printf("\nError occurred at LBA %" PRIu64 "\n", errorList[0].errorAddress);
        }
        if (updateFunction != M_NULLPTR)
        {
            op_emit_error_lba_cb(updateFunction, updateData, "generic_test", errorList[0].errorAddress, (int)FAILURE,
                                 M_NULLPTR);
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
                print_str("No bad LBAs detected during read scan of device.\n");
            }
        }
    }
    safe_free_error_lba(&errorList);
    return ret;
}

// this function is similar to the range function, but looks for a time limit to run for instead.
M_PARAM_RO(1)
M_PARAM_RW(6)
M_PARAM_RW(7)
M_PARAM_WO(10)
static eReturnValues diameter_Test_RWV_Time(const tDevice* M_NONNULL device,
                                            eRWVCommandType          rwvCommand,
                                            uint64_t                 startingLBA,
                                            uint64_t                 timeInSeconds,
                                            uint16_t                 errorLimit,
                                            errorLBA* M_NONNULL      errorList,
                                            uint16_t* M_NONNULL      errorOffset,
                                            bool                     stopOnError,
                                            bool                     repairOnTheFly,
                                            uint64_t* M_NULLABLE     numberOfLbasAccessed,
                                            bool                     hideLBACounter,
                                            custom_Update            updateFunction,
                                            void*                    updateData)
{
    eReturnValues ret               = SUCCESS;
    bool          errorLimitReached = false;
    uint32_t      sectorCount       = get_Sector_Count_For_Read_Write(device);
    uint8_t*      dataBuf           = M_NULLPTR;
    size_t        dataBufSize       = SIZE_T_C(0);
    if (numberOfLbasAccessed != M_NULLPTR)
    {
        *numberOfLbasAccessed = startingLBA;
    }
    // only one of these flags should be set. If they are both set, this makes no sense
    if (stopOnError)
    {
        // disable the repair flag
        repairOnTheFly = false;
    }
    if (errorLimit < 1)
    {
        // need to be able to store at least 1 error
        errorLimit = 1;
    }
    if (errorList == M_NULLPTR)
    {
        return BAD_PARAMETER;
    }
    if (rwvCommand == RWV_COMMAND_READ || rwvCommand == RWV_COMMAND_WRITE)
    {
        // allocate memory
        dataBufSize = uint32_to_sizet(get_Device_BlockSize(device)) * uint32_to_sizet(sectorCount);
        dataBuf     = M_REINTERPRET_CAST(
            uint8_t*, safe_calloc_aligned(dataBufSize, sizeof(uint8_t), get_Device_IO_Minimum_Alignment(device)));
        if (dataBuf == M_NULLPTR)
        {
            perror("failed to allocate memory!\n");
            return MEMORY_FAILURE;
        }
    }
    errorList[*errorOffset].errorAddress = UINT64_MAX;
    // make sure the starting LBA is alligned? If we do this, we need to make sure we don't mess with the data of the
    // LBAs we don't mean to start at...mostly don't want to erase an LBA we shouldn't be starting at. startingLBA =
    // align_LBA(device, startingLBA);
    bool autoReadReassign  = false;
    bool autoWriteReassign = false;
    if (SUCCESS != get_Automatic_Reallocation_Support(device, &autoWriteReassign, &autoReadReassign))
    {
        autoWriteReassign = true; // just in case this fails, default to previous behavior
    }
    // this is escentially a loop over the sequential read function
    time_t startTime = time(M_NULLPTR);
    while (!errorLimitReached && C_CAST(uint64_t, difftime(time(M_NULLPTR), startTime)) < timeInSeconds &&
           startingLBA < return_Device_MaxLba(device))
    {
        if ((startingLBA + sectorCount) > return_Device_MaxLba(device))
        {
            sectorCount = C_CAST(uint32_t, return_Device_MaxLba(device) - startingLBA + 1);
        }
        if (updateFunction != M_NULLPTR)
        {
            op_emit_lba_cb(updateFunction, updateData, "diameter_Test", startingLBA, M_NULLPTR);
        }
        if (VERBOSITY_QUIET < device->deviceVerbosity && !hideLBACounter)
        {
            switch (rwvCommand)
            {
            case RWV_COMMAND_WRITE:
                printf("\rWriting LBA: %-20" PRIu64 "",
                       startingLBA); // 20 wide is the max width for a unsigned 64bit number
                break;
            case RWV_COMMAND_READ:
                printf("\rReading LBA: %-20" PRIu64 "",
                       startingLBA); // 20 wide is the max width for a unsigned 64bit number
                break;
            case RWV_COMMAND_VERIFY:
            default:
                printf("\rVerifying LBA: %-20" PRIu64 "",
                       startingLBA); // 20 wide is the max width for a unsigned 64bit number
                break;
            }
            flush_stdout();
        }
        if (SUCCESS != read_Write_Seek_Command(device, rwvCommand, startingLBA, dataBuf,
                                               C_CAST(uint32_t, sectorCount* get_Device_BlockSize(device))))
        {
            uint64_t maxSingleLoopLBA =
                startingLBA + sectorCount; // limits the loop to trying to only a certain number of sectors without
                                           // getting stuck at single LBA reads.
            // read command failure...so we need to read until we find the exact failing lba
            for (; startingLBA <= maxSingleLoopLBA; startingLBA += 1)
            {
                // print out the current LBA we are rwving
                if (updateFunction != M_NULLPTR)
                {
                    op_emit_lba_cb(updateFunction, updateData, "diameter_Test", startingLBA, M_NULLPTR);
                }
                if (VERBOSITY_QUIET < device->deviceVerbosity && !hideLBACounter)
                {
                    switch (rwvCommand)
                    {
                    case RWV_COMMAND_WRITE:
                        printf("\rWriting LBA: %-20" PRIu64 "",
                               startingLBA); // 20 wide is the max width for a unsigned 64bit number
                        break;
                    case RWV_COMMAND_READ:
                        printf("\rReading LBA: %-20" PRIu64 "",
                               startingLBA); // 20 wide is the max width for a unsigned 64bit number
                        break;
                    case RWV_COMMAND_VERIFY:
                    default:
                        printf("\rVerifying LBA: %-20" PRIu64 "",
                               startingLBA); // 20 wide is the max width for a unsigned 64bit number
                        break;
                    }
                    flush_stdout();
                }
                if (SUCCESS != read_Write_Seek_Command(device, rwvCommand, startingLBA, dataBuf,
                                                       C_CAST(uint32_t, 1 * get_Device_BlockSize(device))))
                {
                    errorList[*errorOffset].errorAddress = startingLBA;
                    break;
                }
            }
            if (device->deviceVerbosity > VERBOSITY_QUIET)
            {
                printf("\nError Found at LBA %" PRIu64 "\n", errorList[*errorOffset].errorAddress);
            }
            if (updateFunction != M_NULLPTR)
            {
                op_emit_error_lba_cb(updateFunction, updateData, "diameter_Test", errorList[*errorOffset].errorAddress,
                                     (int)FAILURE, M_NULLPTR);
            }
            // set a new start for next time through the loop to 1 lba past the last error LBA
            startingLBA = errorList[*errorOffset].errorAddress + 1;
            if (stopOnError || *errorOffset >= errorLimit)
            {
                errorLimitReached = true;
                ret               = FAILURE;
            }
            if (repairOnTheFly)
            {
                repair_LBA(device, &errorList[*errorOffset], false, autoWriteReassign, autoReadReassign);
            }
            (*errorOffset)++;
            continue; // continuing here since startingLBA will get incremented beyond the error so we pick up where we
                      // left off.
        }
        startingLBA += sectorCount;
    }
    if (updateFunction != M_NULLPTR)
    {
        op_emit_lba_cb(updateFunction, updateData, "diameter_Test", startingLBA, M_NULLPTR);
    }
    if (VERBOSITY_QUIET < device->deviceVerbosity && !hideLBACounter)
    {
        switch (rwvCommand)
        {
        case RWV_COMMAND_WRITE:
            printf("\rWriting LBA: %-20" PRIu64 "", startingLBA); // 20 wide is the max width for a unsigned 64bit
                                                                  // number
            break;
        case RWV_COMMAND_READ:
            printf("\rReading LBA: %-20" PRIu64 "", startingLBA); // 20 wide is the max width for a unsigned 64bit
                                                                  // number
            break;
        case RWV_COMMAND_VERIFY:
        default:
            printf("\rVerifying LBA: %-20" PRIu64 "",
                   startingLBA); // 20 wide is the max width for a unsigned 64bit number
            break;
        }
        print_str("\n");
        flush_stdout();
    }
    if (numberOfLbasAccessed != M_NULLPTR)
    {
        *numberOfLbasAccessed =
            startingLBA + sectorCount -
            *numberOfLbasAccessed; // subtract itself since it gets set to where we start at when we begin.
    }
    safe_free_aligned(&dataBuf);
    return ret;
}

M_PARAM_RO(1)
OPENSEA_OPERATIONS_API eReturnValues diameter_Test_Time(const tDevice* M_NONNULL device,
                                                        eRWVCommandType          testMode,
                                                        bool                     outer,
                                                        bool                     middle,
                                                        bool                     inner,
                                                        uint64_t                 timeInSecondsPerDiameter,
                                                        uint16_t                 errorLimit,
                                                        bool                     stopOnError,
                                                        bool                     repairOnTheFly,
                                                        bool                     repairAtEnd,
                                                        bool                     hideLBACounter,
                                                        custom_Update M_NULLABLE updateFunction,
                                                        void* M_NULLABLE         updateData)
{
    errno_t msgerror = 0;
    DECLARE_ZERO_INIT_ARRAY(char, message, JSON_REPORT_TEST_STEP_MESSAGE_LENGTH);
    DECLARE_ZERO_INIT_ARRAY(char, timeStr, TIME_STRING_LENGTH);
    eReturnValues ret       = SUCCESS;
    eReturnValues outerRet  = SUCCESS;
    eReturnValues middleRet = SUCCESS;
    eReturnValues innerRet  = SUCCESS;
    if ((repairOnTheFly && repairAtEnd) || errorLimit == 0)
    {
        return BAD_PARAMETER;
    }
    errorLBA* errorList   = M_REINTERPRET_CAST(errorLBA*, safe_calloc(errorLimit * sizeof(errorLBA), sizeof(errorLBA)));
    uint16_t  errorOffset = UINT16_C(0);
    uint64_t  odOrMdLBAsAccessed = UINT64_C(0);
    uint16_t  days               = UINT16_C(0);
    uint8_t   hours              = UINT8_C(0);
    uint8_t   minutes            = UINT8_C(0);
    uint8_t   seconds            = UINT8_C(0);
    convert_Seconds_To_Displayable_Time(timeInSecondsPerDiameter, M_NULLPTR, &days, &hours, &minutes, &seconds);

    // set time string seconds only
    int timestrformat =
        snprintf_err_handle(timeStr, TIME_STRING_LENGTH, "%" PRIu64 " seconds", timeInSecondsPerDiameter);

    // OD
    if (outer && (ret == SUCCESS || (errorOffset < errorLimit && !stopOnError)))
    {
        msgerror = safe_strcpy(message, JSON_REPORT_TEST_STEP_MESSAGE_LENGTH, "Outer Diameter Test for ");
        if (msgerror == 0)
        {
            if (device->deviceVerbosity > VERBOSITY_QUIET)
            {
                print_str(message);
                print_Time_To_Screen(M_NULLPTR, &days, &hours, &minutes, &seconds);
                print_str("\n");
            }
            if (updateFunction != M_NULLPTR && timestrformat >= 0)
            {
                if (0 == safe_strcat(message, JSON_REPORT_TEST_STEP_MESSAGE_LENGTH, timeStr))
                {
                    op_emit_step_cb(updateFunction, updateData, "diameter_Test", message);
                }
            }
        }
        outerRet = diameter_Test_RWV_Time(device, testMode, 0, timeInSecondsPerDiameter, errorLimit, errorList,
                                          &errorOffset, stopOnError, repairOnTheFly, &odOrMdLBAsAccessed,
                                          hideLBACounter, updateFunction, updateData);
        if (device->deviceVerbosity > VERBOSITY_QUIET)
        {
            print_str("\n");
        }
    }
    if (outerRet != SUCCESS && ret == SUCCESS)
    {
        ret = outerRet;
    }
    // MD
    if (middle && (ret == SUCCESS || (errorOffset < errorLimit && !stopOnError)))
    {
        uint64_t  mdLBAsAccessed = UINT64_C(0);
        uint64_t* countPointer   = &mdLBAsAccessed;
        msgerror = safe_strcpy(message, JSON_REPORT_TEST_STEP_MESSAGE_LENGTH, "Middle Diameter Test for ");
        if (msgerror == 0)
        {
            if (device->deviceVerbosity > VERBOSITY_QUIET)
            {
                print_str(message);
                print_Time_To_Screen(M_NULLPTR, &days, &hours, &minutes, &seconds);
                print_str("\n");
            }
            if (updateFunction != M_NULLPTR && timestrformat >= 0)
            {
                if (0 == safe_strcat(message, JSON_REPORT_TEST_STEP_MESSAGE_LENGTH, timeStr))
                {
                    op_emit_step_cb(updateFunction, updateData, "diameter_Test", message);
                }
            }
        }
        if (odOrMdLBAsAccessed == 0)
        {
            countPointer = &odOrMdLBAsAccessed;
        }
        middleRet = diameter_Test_RWV_Time(device, testMode, return_Device_MaxLba(device) / 2, timeInSecondsPerDiameter,
                                           errorLimit, errorList, &errorOffset, stopOnError, repairOnTheFly,
                                           countPointer, hideLBACounter, updateFunction, updateData);
        if (device->deviceVerbosity > VERBOSITY_QUIET)
        {
            print_str("\n");
        }
        odOrMdLBAsAccessed = (odOrMdLBAsAccessed > mdLBAsAccessed)
                                 ? odOrMdLBAsAccessed
                                 : mdLBAsAccessed; // get the higher of these two counts.
    }
    if (middleRet != SUCCESS && ret == SUCCESS)
    {
        ret = middleRet;
    }
    // ID
    if (inner && (ret == SUCCESS || (errorOffset < errorLimit && !stopOnError)))
    {
        uint64_t idStartingLBA = return_Device_MaxLba(device) - odOrMdLBAsAccessed;
        if (idStartingLBA == return_Device_MaxLba(device))
        {
            // NOTE: this guestimate can be improved by reading the negotiated interface speed and using that as a
            // maximum performance to get the gestimate closer to the best place to start the ID scan...this would work
            // at least on ATA and SCSI well...USB would be more difficult to guess properly. need to make a guess based
            // on the amount of time we're running where we'll start...let's assume the worse case of an accessing at
            // 550MB/s (max 6Gb/s transfer an SSD on SATA can get...shouldn't happen)
            idStartingLBA = return_Device_MaxLba(device) -
                            (((550 /*megabytes per second*/ * timeInSecondsPerDiameter) /*now convert to Bytes*/ *
                              1000000) /*now convert to LBAs*/
                             / get_Device_BlockSize(device));
        }
        msgerror = safe_strcpy(message, JSON_REPORT_TEST_STEP_MESSAGE_LENGTH, "Inner Diameter Test for ");
        if (msgerror == 0)
        {
            if (device->deviceVerbosity > VERBOSITY_QUIET)
            {
                print_str(message);
                print_Time_To_Screen(M_NULLPTR, &days, &hours, &minutes, &seconds);
                print_str("\n");
            }
            if (updateFunction != M_NULLPTR && timestrformat >= 0)
            {
                if (0 == safe_strcat(message, JSON_REPORT_TEST_STEP_MESSAGE_LENGTH, timeStr))
                {
                    op_emit_step_cb(updateFunction, updateData, "diameter_Test", message);
                }
            }
        }
        innerRet = diameter_Test_RWV_Time(device, testMode, idStartingLBA, timeInSecondsPerDiameter, errorLimit,
                                          errorList, &errorOffset, stopOnError, repairOnTheFly, M_NULLPTR,
                                          hideLBACounter, updateFunction, updateData);
        if (device->deviceVerbosity > VERBOSITY_QUIET)
        {
            print_str("\n");
        }
    }
    if (innerRet != SUCCESS && ret == SUCCESS)
    {
        ret = innerRet;
    }
    // handle repair at end if it was set
    if (repairAtEnd)
    {
        bool autoReadReassign  = false;
        bool autoWriteReassign = false;
        if (SUCCESS != get_Automatic_Reallocation_Support(device, &autoWriteReassign, &autoReadReassign))
        {
            autoWriteReassign = true; // just in case this fails, default to previous behavior
        }
        // go through and repair the LBAs
        uint64_t errorIter                 = UINT64_C(0);
        uint64_t lastLBARepaired           = UINT64_MAX;
        uint16_t logicalPerPhysicalSectors = get_Logical_Sectors_Per_Physical_Sector(device);
        for (errorIter = 0; errorIter < errorOffset; errorIter++)
        {
            if (lastLBARepaired != UINT64_MAX)
            {
                // check if the LBA we want to repair is within the same physical sector as the last LBA
                if ((lastLBARepaired + logicalPerPhysicalSectors) > errorList[errorIter].errorAddress)
                {
                    // in this case, we have already repaired this LBA since the repair is issued to the physical
                    // sector, so move on to the next thing in the list
                    errorList[errorIter].repairStatus = REPAIR_NOT_REQUIRED;
                    continue;
                }
            }
            if (SUCCESS == repair_LBA(device, &errorList[errorIter], false, autoWriteReassign, autoReadReassign))
            {
                errorList[errorIter].repairStatus = REPAIRED;
                lastLBARepaired                   = errorList[errorIter].errorAddress;
            }
            else
            {
                errorList[errorIter].repairStatus = REPAIR_FAILED;
            }
        }
    }
    // handle stopping on the error we got
    if (stopOnError && errorList[0].errorAddress != UINT64_MAX)
    {
        if (device->deviceVerbosity > VERBOSITY_QUIET)
        {
            printf("\nError occurred at LBA %" PRIu64 "\n", errorList[0].errorAddress);
        }
        if (updateFunction != M_NULLPTR)
        {
            op_emit_error_lba_cb(updateFunction, updateData, "generic_test", errorList[0].errorAddress, (int)FAILURE,
                                 M_NULLPTR);
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
                print_str("No bad LBAs detected during read scan of device.\n");
            }
        }
    }
    safe_free_error_lba(&errorList);
    return ret;
}

M_PARAM_RO(1)
OPENSEA_OPERATIONS_API eReturnValues zero_Verify_Test(const tDevice* M_NONNULL device,
                                                      eZeroVerifyTestType      zeroVerifyTestType,
                                                      bool                     hideLBACounter)
{
    if (zeroVerifyTestType == ZERO_VERIFY_TYPE_FULL)
        return full_Zero_Verify_Test(device, hideLBACounter);
    else if (zeroVerifyTestType == ZERO_VERIFY_TYPE_QUICK)
        return quick_Zero_Verify_Test(device, hideLBACounter);
    else
        return BAD_PARAMETER;
}

M_PARAM_RO(1)
OPENSEA_OPERATIONS_API eReturnValues full_Zero_Verify_Test(const tDevice* M_NONNULL device, bool hideLBACounter)
{
    uint32_t sectorCount = get_Sector_Count_For_Read_Write(device);

    for (uint64_t lbaIterator = UINT64_C(0); lbaIterator < return_Device_MaxLba(device); lbaIterator += sectorCount)
    {
        uint32_t lbaRange   = C_CAST(uint32_t, ((lbaIterator + sectorCount) > return_Device_MaxLba(device))
                                                   ? (return_Device_MaxLba(device) - lbaIterator)
                                                   : sectorCount);
        uint32_t bufferSize = get_Device_BlockSize(device) * lbaRange;
        uint8_t* dataBuffer =
            M_REINTERPRET_CAST(uint8_t*, safe_calloc_aligned(uint32_to_sizet(bufferSize), sizeof(uint8_t),
                                                             get_Device_IO_Minimum_Alignment(device)));
        if (dataBuffer == M_NULLPTR)
        {
            perror("\nfailed to allocate memory for reading data\n");
            return MEMORY_FAILURE;
        }

        if (VERBOSITY_QUIET < device->deviceVerbosity && !hideLBACounter)
        {
            printf("\rReading LBA: %-20" PRIu64 "", lbaIterator);
            flush_stdout();
        }

        if (SUCCESS == read_Write_Seek_Command(device, RWV_COMMAND_READ, lbaIterator, dataBuffer, bufferSize))
        {
            // Validate it against zero buffer
            uint8_t* validationBuffer =
                M_REINTERPRET_CAST(uint8_t*, safe_calloc_aligned(uint32_to_sizet(bufferSize), sizeof(uint8_t),
                                                                 get_Device_IO_Minimum_Alignment(device)));
            if (validationBuffer == M_NULLPTR)
            {
                perror("\nfailed to allocate memory for validation data\n");
                safe_free_aligned(&dataBuffer);
                return MEMORY_FAILURE;
            }

            if (memcmp(validationBuffer, dataBuffer, bufferSize) != 0)
            {
                if (VERBOSITY_QUIET < device->deviceVerbosity)
                {
                    printf("\nValidation Failed for LBA Range : %" PRIu64 " - %" PRIu64 "\n", lbaIterator,
                           (lbaIterator + lbaRange));
                }
                safe_free_aligned(&dataBuffer);
                safe_free_aligned(&validationBuffer);
                return VALIDATION_FAILURE;
            }
            safe_free_aligned(&validationBuffer);
        }
        else
        {
            if (VERBOSITY_QUIET < device->deviceVerbosity)
            {
                printf("\nRead failed at LBA %-20" PRIu64 "\n", lbaIterator);
            }
            safe_free_aligned(&dataBuffer);
            return FAILURE;
        }
        safe_free_aligned(&dataBuffer);
    }

    print_str("\n");
    return SUCCESS;
}

#define DRIVE_CAPACITY_PERCENTAGE (0.1) // 0.1 percentage
#define DRIVE_SECTIONS            (10000) // divide drive into these many sections and then pick 2 random LBA from each section

M_PARAM_RO(1)
OPENSEA_OPERATIONS_API eReturnValues quick_Zero_Verify_Test(const tDevice* M_NONNULL device, bool hideLBACounter)
{
    uint32_t sectorCount    = get_Sector_Count_For_Read_Write(device);
    uint64_t totalLBAToRead = C_CAST(
        uint64_t, (C_CAST(double, return_Device_MaxLba(device)) * 0.01 * DRIVE_CAPACITY_PERCENTAGE)); // for OD/ID
    uint64_t startLBA = UINT64_C(0);
    uint64_t endLBA   = UINT64_C(0);

    // 0.1% OD Validation
    startLBA = UINT64_C(0);
    endLBA   = align_LBA(device, totalLBAToRead);
    if (device->deviceVerbosity > VERBOSITY_QUIET)
    {
        printf("\nSequential Verification Test at OD for %0.2f%% Capacity\n", DRIVE_CAPACITY_PERCENTAGE);
    }
    for (uint64_t lbaIterator = startLBA; lbaIterator < endLBA; lbaIterator += sectorCount)
    {
        uint32_t lbaRange   = C_CAST(uint32_t, ((lbaIterator + sectorCount) > return_Device_MaxLba(device))
                                                   ? (return_Device_MaxLba(device) - lbaIterator)
                                                   : sectorCount);
        uint32_t bufferSize = get_Device_BlockSize(device) * lbaRange;
        uint8_t* dataBuffer =
            M_REINTERPRET_CAST(uint8_t*, safe_calloc_aligned(uint32_to_sizet(bufferSize), sizeof(uint8_t),
                                                             get_Device_IO_Minimum_Alignment(device)));
        if (dataBuffer == M_NULLPTR)
        {
            perror("\nfailed to allocate memory for reading data\n");
            return MEMORY_FAILURE;
        }

        if (VERBOSITY_QUIET < device->deviceVerbosity && !hideLBACounter)
        {
            printf("\rReading LBA: %-20" PRIu64 "", lbaIterator);
            flush_stdout();
        }

        if (SUCCESS == read_Write_Seek_Command(device, RWV_COMMAND_READ, lbaIterator, dataBuffer, bufferSize))
        {
            // Validate it against zero buffer
            uint8_t* validationBuffer =
                M_REINTERPRET_CAST(uint8_t*, safe_calloc_aligned(uint32_to_sizet(bufferSize), sizeof(uint8_t),
                                                                 get_Device_IO_Minimum_Alignment(device)));
            if (validationBuffer == M_NULLPTR)
            {
                perror("\nfailed to allocate memory for validation data\n");
                safe_free_aligned(&dataBuffer);
                return MEMORY_FAILURE;
            }

            if (memcmp(validationBuffer, dataBuffer, bufferSize) != 0)
            {
                if (VERBOSITY_QUIET < device->deviceVerbosity)
                {
                    printf("\nValidation Failed for LBA Range: %" PRIu64 " - %" PRIu64 "\n", lbaIterator,
                           (lbaIterator + lbaRange));
                }
                safe_free_aligned(&dataBuffer);
                safe_free_aligned(&validationBuffer);
                return VALIDATION_FAILURE;
            }
            safe_free_aligned(&validationBuffer);
        }
        else
        {
            if (VERBOSITY_QUIET < device->deviceVerbosity)
            {
                printf("\nRead failed at LBA %-20" PRIu64 "\n", lbaIterator);
            }
            safe_free_aligned(&dataBuffer);
            return FAILURE;
        }
        safe_free_aligned(&dataBuffer);
    }

    // 0.1% ID Validation
    startLBA = align_LBA(device, (return_Device_MaxLba(device) - totalLBAToRead));
    endLBA   = return_Device_MaxLba(device);
    if (device->deviceVerbosity > VERBOSITY_QUIET)
    {
        printf("\nSequential Verification Test at ID for %0.2f%% Capacity\n", DRIVE_CAPACITY_PERCENTAGE);
    }
    for (uint64_t lbaIterator = startLBA; lbaIterator < endLBA; lbaIterator += sectorCount)
    {
        uint32_t lbaRange   = C_CAST(uint32_t, ((lbaIterator + sectorCount) > return_Device_MaxLba(device))
                                                   ? (return_Device_MaxLba(device) - lbaIterator)
                                                   : sectorCount);
        uint32_t bufferSize = get_Device_BlockSize(device) * lbaRange;
        uint8_t* dataBuffer =
            M_REINTERPRET_CAST(uint8_t*, safe_calloc_aligned(uint32_to_sizet(bufferSize), sizeof(uint8_t),
                                                             get_Device_IO_Minimum_Alignment(device)));
        if (dataBuffer == M_NULLPTR)
        {
            perror("\nfailed to allocate memory for reading data\n");
            return MEMORY_FAILURE;
        }

        if (VERBOSITY_QUIET < device->deviceVerbosity && !hideLBACounter)
        {
            printf("\rReading LBA: %-20" PRIu64 "", lbaIterator);
            flush_stdout();
        }

        if (SUCCESS == read_Write_Seek_Command(device, RWV_COMMAND_READ, lbaIterator, dataBuffer, bufferSize))
        {
            // Validate it against zero buffer
            uint8_t* validationBuffer =
                M_REINTERPRET_CAST(uint8_t*, safe_calloc_aligned(uint32_to_sizet(bufferSize), sizeof(uint8_t),
                                                                 get_Device_IO_Minimum_Alignment(device)));
            if (validationBuffer == M_NULLPTR)
            {
                perror("\nfailed to allocate memory for validation data\n");
                safe_free_aligned(&dataBuffer);
                return MEMORY_FAILURE;
            }

            if (memcmp(validationBuffer, dataBuffer, bufferSize) != 0)
            {
                if (VERBOSITY_QUIET < device->deviceVerbosity)
                {
                    printf("\nValidation Failed for LBA Range: %" PRIu64 " - %" PRIu64 "\n", lbaIterator,
                           (lbaIterator + lbaRange));
                }
                safe_free_aligned(&dataBuffer);
                safe_free_aligned(&validationBuffer);
                return VALIDATION_FAILURE;
            }
            safe_free_aligned(&validationBuffer);
        }
        else
        {
            if (VERBOSITY_QUIET < device->deviceVerbosity)
            {
                printf("\nRead failed at LBA %-20" PRIu64 "\n", lbaIterator);
            }
            safe_free_aligned(&dataBuffer);
            return FAILURE;
        }
        safe_free_aligned(&dataBuffer);
    }

    // Random Section Validation
    if (device->deviceVerbosity > VERBOSITY_QUIET)
    {
        printf("\nVerification Test for Random LBAs from %" PRId32 " sections\n", DRIVE_SECTIONS);
    }
    uint64_t randomLBA            = UINT64_MAX;
    uint64_t randomLBASectionSize = C_CAST(uint64_t, return_Device_MaxLba(device) / DRIVE_SECTIONS);
    seed_64(C_CAST(uint64_t, time(M_NULLPTR)));
    for (uint64_t sectionCounter = UINT64_C(0), counter = UINT64_C(0); sectionCounter < DRIVE_SECTIONS;
         sectionCounter++, counter += UINT64_C(2))
    {
        // first random LBA from section
        {
            randomLBA = align_LBA(device, random_Range_64((sectionCounter * randomLBASectionSize),
                                                          ((sectionCounter + 1) * randomLBASectionSize)));
            if (VERBOSITY_QUIET < device->deviceVerbosity && !hideLBACounter)
            {
                printf("\rReading LBA: %-20" PRIu64 "", randomLBA);
                flush_stdout();
            }

            uint8_t* dataBuffer = M_REINTERPRET_CAST(
                uint8_t*, safe_calloc_aligned(uint32_to_sizet(get_Device_BlockSize(device)), sizeof(uint8_t),
                                              get_Device_IO_Minimum_Alignment(device)));
            if (dataBuffer == M_NULLPTR)
            {
                perror("\nfailed to allocate memory for reading data\n");
                return MEMORY_FAILURE;
            }

            if (SUCCESS ==
                read_Write_Seek_Command(device, RWV_COMMAND_READ, randomLBA, dataBuffer, get_Device_BlockSize(device)))
            {
                // Validate it against zero buffer
                uint8_t* validationBuffer = M_REINTERPRET_CAST(
                    uint8_t*, safe_calloc_aligned(uint32_to_sizet(get_Device_BlockSize(device)), sizeof(uint8_t),
                                                  get_Device_IO_Minimum_Alignment(device)));
                if (validationBuffer == M_NULLPTR)
                {
                    perror("\nfailed to allocate memory for validation data\n");
                    safe_free_aligned(&dataBuffer);
                    return MEMORY_FAILURE;
                }

                if (memcmp(validationBuffer, dataBuffer, get_Device_BlockSize(device)) != 0)
                {
                    if (VERBOSITY_QUIET < device->deviceVerbosity)
                    {
                        printf("\nValidation Failed for LBA: %-20" PRIu64 "\n", randomLBA);
                    }
                    safe_free_aligned(&dataBuffer);
                    safe_free_aligned(&validationBuffer);
                    return VALIDATION_FAILURE;
                }
                safe_free_aligned(&validationBuffer);
            }
            else
            {
                if (VERBOSITY_QUIET < device->deviceVerbosity)
                {
                    printf("\nRead failed at LBA %-20" PRIu64 "\n", randomLBA);
                }
                safe_free_aligned(&dataBuffer);
                return FAILURE;
            }
            safe_free_aligned(&dataBuffer);
        }

        // second random LBA from section
        {
            randomLBA = align_LBA(device, random_Range_64((sectionCounter * randomLBASectionSize),
                                                          ((sectionCounter + 1) * randomLBASectionSize)));
            if (VERBOSITY_QUIET < device->deviceVerbosity && !hideLBACounter)
            {
                printf("\rReading LBA: %-20" PRIu64 "", randomLBA);
                flush_stdout();
            }

            uint8_t* dataBuffer = M_REINTERPRET_CAST(
                uint8_t*, safe_calloc_aligned(uint32_to_sizet(get_Device_BlockSize(device)), sizeof(uint8_t),
                                              get_Device_IO_Minimum_Alignment(device)));
            if (dataBuffer == M_NULLPTR)
            {
                perror("\nfailed to allocate memory for reading data\n");
                return MEMORY_FAILURE;
            }

            if (SUCCESS ==
                read_Write_Seek_Command(device, RWV_COMMAND_READ, randomLBA, dataBuffer, get_Device_BlockSize(device)))
            {
                // Validate it against zero buffer
                uint8_t* validationBuffer = M_REINTERPRET_CAST(
                    uint8_t*, safe_calloc_aligned(uint32_to_sizet(get_Device_BlockSize(device)), sizeof(uint8_t),
                                                  get_Device_IO_Minimum_Alignment(device)));
                if (validationBuffer == M_NULLPTR)
                {
                    perror("\nfailed to allocate memory for validation data\n");
                    safe_free_aligned(&dataBuffer);
                    return MEMORY_FAILURE;
                }

                if (memcmp(validationBuffer, dataBuffer, get_Device_BlockSize(device)) != 0)
                {
                    if (VERBOSITY_QUIET < device->deviceVerbosity)
                    {
                        printf("\nValidation Failed for LBA: %-20" PRIu64 "\n", randomLBA);
                    }
                    safe_free_aligned(&dataBuffer);
                    safe_free_aligned(&validationBuffer);
                    return VALIDATION_FAILURE;
                }
                safe_free_aligned(&validationBuffer);
            }
            else
            {
                if (VERBOSITY_QUIET < device->deviceVerbosity)
                {
                    printf("\nRead failed at LBA %-20" PRIu64 "\n", randomLBA);
                }
                safe_free_aligned(&dataBuffer);
                return FAILURE;
            }
            safe_free_aligned(&dataBuffer);
        }
    }

    print_str("\n");
    return SUCCESS;
}
