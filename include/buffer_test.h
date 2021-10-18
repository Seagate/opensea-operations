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
// \file buffer_test.h
// \brief This file defines the function calls for performing buffer/cabling tests

#pragma once

#include "operations_Common.h"

#if defined (__cplusplus)
extern "C"
{
#endif

#define ALL_0_TEST_COUNT 2
#define ALL_F_TEST_COUNT 2
#define ALL_5_TEST_COUNT 2
#define ALL_A_TEST_COUNT 2
#define ZERO_F_5_A_TEST_COUNT 2
#define WALKING_1_TEST_COUNT 1
#define WALKING_0_TEST_COUNT 1
#define RANDOM_TEST_COUNT 5

    typedef struct _patternTestResults
    {
        uint64_t totalTimeNS;
        uint32_t totalCommandsSent;//read and write commands
        uint32_t totalCommandTimeouts;//how many take longer than the timeout set for the drive
        uint32_t totalCommandCRCErrors;//how many commands return a CRC error
        uint32_t totalBufferComparisons;//how many write-read buffer, then compare the two have been done in the test;
        uint32_t totalBufferMiscompares;//how many times did the buffer miscompare.
    }patternTestResults, *ptrPatternTestResults;

    typedef struct _cableTestResults
    {
        uint64_t totalTestTimeNS;
        patternTestResults zerosTest[ALL_0_TEST_COUNT];//all zeros tested
        patternTestResults fTest[ALL_F_TEST_COUNT];//all F's tested
        patternTestResults fivesTest[ALL_5_TEST_COUNT];//all 5's tested
        patternTestResults aTest[ALL_A_TEST_COUNT];//all A's tested
        patternTestResults zeroF5ATest[ZERO_F_5_A_TEST_COUNT];//pattern of 00FF55AA tested
        patternTestResults walking1sTest[WALKING_1_TEST_COUNT];
        patternTestResults walking0sTest[WALKING_0_TEST_COUNT];
        patternTestResults randomTest[RANDOM_TEST_COUNT];
    }cableTestResults, *ptrCableTestResults;

    //-----------------------------------------------------------------------------
    //
    //  perform_Cable_Test(tDevice *device, ptrCableTestResults testResults)
    //
    //! \brief   Description: Perform a cable/buffer test using read/write buffer commands to check for mismatches and other bus errors
    //
    //  Entry:
    //!   \param[in] device = file descriptor
    //!   \param[in] testResults = pointer to structure that holds results
    //!
    //  Exit:
    //!   \return SUCCESS = pass, FAILURE = fail
    //
    //-----------------------------------------------------------------------------
    int perform_Cable_Test(tDevice *device, ptrCableTestResults testResults);

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
    void print_Cable_Test_Results(cableTestResults testResults);

#if defined (__cplusplus)
}
#endif
