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
// \file buffer_test.h
// \brief This file defines the function calls for performing buffer/cabling tests

#pragma once

#include "operations_Common.h"

#if defined(__cplusplus)
extern "C"
{
#endif

#define ALL_0_TEST_COUNT         10
#define ALL_F_TEST_COUNT         10
#define ALL_5_TEST_COUNT         10
#define ALL_A_TEST_COUNT         10
#define ZERO_F_5_A_TEST_COUNT    10
#define ROW_BOAT_TEST_COUNT      10
#define CHECKER_BOARD_TEST_COUNT 10
#define MARK_TEST_COUNT          10
#define WALKING_1_TEST_COUNT     5
#define WALKING_0_TEST_COUNT     5
#define RANDOM_TEST_COUNT        10

    typedef struct s_patternTestResults
    {
        uint64_t totalTimeNS;
        uint32_t totalCommandsSent;      // read and write commands
        uint32_t totalCommandTimeouts;   // how many take longer than the timeout set for the drive
        uint32_t totalCommandCRCErrors;  // how many commands return a CRC error
        uint32_t totalBufferComparisons; // how many write-read buffer, then compare the two have been done in the test;
        uint32_t totalBufferMiscompares; // how many times did the buffer miscompare.
    } patternTestResults, *ptrPatternTestResults;

    // additional things to log during test:
    // error LBA location
    // device statistics, smart, phy counters (interface, defects, temperatures, )
    // performance stuff? latency, MB/s, etc

    typedef struct s_cableTestResults
    {
        uint64_t           totalTestTimeNS;
        patternTestResults zerosTest[ALL_0_TEST_COUNT];        // all zeros tested
        patternTestResults fTest[ALL_F_TEST_COUNT];            // all F's tested
        patternTestResults fivesTest[ALL_5_TEST_COUNT];        // all 5's tested
        patternTestResults aTest[ALL_A_TEST_COUNT];            // all A's tested
        patternTestResults zeroF5ATest[ZERO_F_5_A_TEST_COUNT]; // pattern of 00FF55AA tested
        patternTestResults rowBoat1[ROW_BOAT_TEST_COUNT];
        patternTestResults rowBoat2[ROW_BOAT_TEST_COUNT];
        patternTestResults rowBoat3[ROW_BOAT_TEST_COUNT];
        patternTestResults rowBoat4[ROW_BOAT_TEST_COUNT];
        patternTestResults checkerBoardByte[CHECKER_BOARD_TEST_COUNT];
        patternTestResults checkerBoardWord[CHECKER_BOARD_TEST_COUNT];
        patternTestResults mark[MARK_TEST_COUNT];
        patternTestResults walking1sTest[WALKING_1_TEST_COUNT];
        patternTestResults walking0sTest[WALKING_0_TEST_COUNT];
        patternTestResults randomTest[RANDOM_TEST_COUNT];
    } cableTestResults, *ptrCableTestResults;

    typedef enum eCableTestModeEnum
    {
        CABLE_TEST_MODE_BUFFER_CMDS,
        CABLE_TEST_MODE_READ_WRITE_CMDS
    } eCableTestMode;

    //-----------------------------------------------------------------------------
    //
    //  perform_Cable_Test(const tDevice *device, ptrCableTestResults testResults)
    //
    //! \brief   Description: Perform a cable/buffer test using read/write buffer commands to check for mismatches and
    //! other bus errors
    //
    //  Entry:
    //!   \param[in] device = file descriptor
    //!   \param[in] testResults = pointer to structure that holds results
    //!
    //  Exit:
    //!   \return SUCCESS = pass, FAILURE = fail
    //
    //-----------------------------------------------------------------------------
    M_PARAM_RO(1)
    M_PARAM_WO(2)
    OPENSEA_OPERATIONS_API eReturnValues perform_Cable_Test(const tDevice* M_NONNULL      device,
                                                            ptrCableTestResults M_NONNULL testResults);

    typedef struct s_fuaCmd
    {
        bool writeFUA;
        bool readFUA;
    } fuaCmd;

    M_NONNULL_PARAM_LIST(1, 2)
    M_PARAM_RO(1)
    M_PARAM_WO(2)
    eReturnValues perform_Write_Read_Compare_Test(const tDevice*      device,
                                                  ptrCableTestResults testResults,
                                                  uint64_t            startingLBA,
                                                  uint64_t            range,
                                                  fuaCmd              fua);

    //-----------------------------------------------------------------------------
    //
    //  print_Cable_Test_Results(cableTestResults testResults)
    //
    //! \brief   Description: Print the cable/buffer test results after running a test.
    //
    //  Entry:
    //!   \param[in] testResults = structure that holds results from a cable/buffer test
    //!
    //  Exit:
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API void print_Cable_Test_Results(cableTestResults testResults);

#if defined(__cplusplus)
}
#endif
