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
// \file generic_tests.h
// \brief This file defines the functions related to generic read tests

#pragma once

#include "operations_Common.h"

#if defined (__cplusplus)
extern "C"
{
#endif

    typedef enum _eRWVCommandType
    {
        RWV_COMMAND_READ,
        RWV_COMMAND_WRITE,
        RWV_COMMAND_VERIFY,
        RWV_COMMAND_INVALID
    }eRWVCommandType;

    //-----------------------------------------------------------------------------
    //
    //  read_Write_Seek_Command()
    //
    //! \brief   Description:  Will perform a read, write, or seek (verify) command to a specified LBA for a specified range
    //
    //  Entry:
    //!   \param[in] device = file descriptor
    //!   \param[in] rwvCommand = enum value specifying which command type to issue
    //!   \param[in] lba = LBA to start the command at
    //!   \param[in] ptrData = pointer to the data buffer to use for the command (May be NULL for RWV_VERIFY)
    //!   \param[in] dataSize = number of data bytes to transfer (This should be a value that is a multiple of the device's logical sector size)
    //!
    //  Exit:
    //!   \return SUCCESS on successful completion, FAILURE = fail
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API int read_Write_Seek_Command(tDevice *device, eRWVCommandType rwvCommand, uint64_t lba, uint8_t *ptrData, uint32_t dataSize);

    //-----------------------------------------------------------------------------
    //
    //  sequential_RWV()
    //
    //! \brief   Description:  Function to perform a sequential read, write, or verify over a range of LBAs. This function will stop on the first error and return that error as an output parameter for the caller to interpret.
    //
    //  Entry:
    //!   \param[in] device = file descriptor
    //!   \param[in] rwvCommand = enum value specifying which command type to issue
    //!   \param[in] startingLBA = LBA to start the sequential read at
    //!   \param[in] range = the range of LBAs from the starting LBA to read
    //!   \param[in] sectorCount = number of sectors to read at a time. This will be adjusted as necessary at the end of the range to not go beyond the end of the specified range
    //!   \param[in] failingLBA = pointer to a uint64_t that will hold the LBA that this test failed on. If no failure was found, this will be set to UINT64_MAX
    //!   \param[in] updateFunction = callback function to update UI
    //!   \param[in] updateData = hidden data to pass to the callback function
    //!   \param[in] hideLBACounter = set to true to hide the LBA counter being printed to stdout
    //!
    //  Exit:
    //!   \return SUCCESS on successful completion, FAILURE = fail
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API int sequential_RWV(tDevice *device, eRWVCommandType rwvCommand, uint64_t startingLBA, uint64_t range, uint64_t sectorCount, uint64_t *failingLBA, custom_Update updateFunction, void *updateData, bool hideLBACounter);

    //-----------------------------------------------------------------------------
    //
    //  sequential_Write()
    //
    //! \brief   Description:  Function to perform a sequential write over a range of LBAs. This function will stop on the first error and return that error as an output parameter for the caller to interpret.
    //
    //  Entry:
    //!   \param[in] device = file descriptor
    //!   \param[in] startingLBA = LBA to start the sequential read at
    //!   \param[in] range = the range of LBAs from the starting LBA to read
    //!   \param[in] sectorCount = number of sectors to read at a time. This will be adjusted as necessary at the end of the range to not go beyond the end of the specified range
    //!   \param[in] failingLBA = pointer to a uint64_t that will hold the LBA that this test failed on. If no failure was found, this will be set to UINT64_MAX
    //!   \param[in] updateFunction = callback function to update UI
    //!   \param[in] updateData = hidden data to pass to the callback function
    //!   \param[in] hideLBACounter = set to true to hide the LBA counter being printed to stdout
    //!
    //  Exit:
    //!   \return SUCCESS on successful completion, FAILURE = fail
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API int sequential_Write(tDevice *device, uint64_t startingLBA, uint64_t range, uint64_t sectorCount, uint64_t *failingLBA, custom_Update updateFunction, void *updateData, bool hideLBACounter);

    //-----------------------------------------------------------------------------
    //
    //  sequential_Verify()
    //
    //! \brief   Description:  Function to perform a sequential verify over a range of LBAs. This function will stop on the first error and return that error as an output parameter for the caller to interpret.
    //
    //  Entry:
    //!   \param[in] device = file descriptor
    //!   \param[in] startingLBA = LBA to start the sequential read at
    //!   \param[in] range = the range of LBAs from the starting LBA to read
    //!   \param[in] sectorCount = number of sectors to read at a time. This will be adjusted as necessary at the end of the range to not go beyond the end of the specified range
    //!   \param[in] failingLBA = pointer to a uint64_t that will hold the LBA that this test failed on. If no failure was found, this will be set to UINT64_MAX
    //!   \param[in] updateFunction = callback function to update UI
    //!   \param[in] updateData = hidden data to pass to the callback function
    //!   \param[in] hideLBACounter = set to true to hide the LBA counter being printed to stdout
    //!
    //  Exit:
    //!   \return SUCCESS on successful completion, FAILURE = fail
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API int sequential_Verify(tDevice *device, uint64_t startingLBA, uint64_t range, uint64_t sectorCount, uint64_t *failingLBA, custom_Update updateFunction, void *updateData, bool hideLBACounter);

    //-----------------------------------------------------------------------------
    //
    //  sequential_Read()
    //
    //! \brief   Description:  Function to perform a sequential read over a range of LBAs. This function will stop on the first error and return that error as an output parameter for the caller to interpret.
    //
    //  Entry:
    //!   \param[in] device = file descriptor
    //!   \param[in] startingLBA = LBA to start the sequential read at
    //!   \param[in] range = the range of LBAs from the starting LBA to read
    //!   \param[in] sectorCount = number of sectors to read at a time. This will be adjusted as necessary at the end of the range to not go beyond the end of the specified range
    //!   \param[in] failingLBA = pointer to a uint64_t that will hold the LBA that this test failed on. If no failure was found, this will be set to UINT64_MAX
    //!   \param[in] updateFunction = callback function to update UI
    //!   \param[in] updateData = hidden data to pass to the callback function
    //!   \param[in] hideLBACounter = set to true to hide the LBA counter being printed to stdout
    //!
    //  Exit:
    //!   \return SUCCESS on successful completion, FAILURE = fail
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API int sequential_Read(tDevice *device, uint64_t startingLBA, uint64_t range, uint64_t sectorCount, uint64_t *failingLBA, custom_Update updateFunction, void *updateData, bool hideLBACounter);

    //-----------------------------------------------------------------------------
    //
    //  short_Generic_Read_Test()
    //
    //! \brief   Description:  This function performs a short generic test like in ST4W. 1% read at OD, 1% read at ID, then read 5000 random LBAs. This function will stop on the first error found
    //
    //  Entry:
    //!   \param[in] device = file descriptor
    //!   \param[in] updateFunction = callback function to update UI
    //!   \param[in] updateData = hidden data to pass to the callback function
    //!   \param[in] hideLBACounter = set to true to hide the LBA counter being printed to stdout
    //!
    //  Exit:
    //!   \return SUCCESS on successful completion, FAILURE = fail
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API int short_Generic_Read_Test(tDevice *device, custom_Update updateFunction, void *updateData, bool hideLBACounter);

    //-----------------------------------------------------------------------------
    //
    //  short_Generic_Verify_Test()
    //
    //! \brief   Description:  This function performs a short generic test using verify commands . 1% read at OD, 1% read at ID, then read 5000 random LBAs. This function will stop on the first error found
    //
    //  Entry:
    //!   \param[in] device = file descriptor
    //!   \param[in] updateFunction = callback function to update UI
    //!   \param[in] updateData = hidden data to pass to the callback function
    //!   \param[in] hideLBACounter = set to true to hide the LBA counter being printed to stdout
    //!
    //  Exit:
    //!   \return SUCCESS on successful completion, FAILURE = fail
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API int short_Generic_Verify_Test(tDevice *device, custom_Update updateFunction, void *updateData, bool hideLBACounter);

    //-----------------------------------------------------------------------------
    //
    //  short_Generic_Write_Test()
    //
    //! \brief   Description:  This function performs a short generic test using write commands . 1% read at OD, 1% read at ID, then read 5000 random LBAs. This function will stop on the first error found
    //
    //  Entry:
    //!   \param[in] device = file descriptor
    //!   \param[in] updateFunction = callback function to update UI
    //!   \param[in] updateData = hidden data to pass to the callback function
    //!   \param[in] hideLBACounter = set to true to hide the LBA counter being printed to stdout
    //!
    //  Exit:
    //!   \return SUCCESS on successful completion, FAILURE = fail
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API int short_Generic_Write_Test(tDevice *device, custom_Update updateFunction, void *updateData, bool hideLBACounter);

    //-----------------------------------------------------------------------------
    //
    //  short_Generic_Test()
    //
    //! \brief   Description:  This function performs a short generic test using read, write, or verify commands . 1% read at OD, 1% read at ID, then read 5000 random LBAs. This function will stop on the first error found
    //
    //  Entry:
    //!   \param[in] device = file descriptor
    //!   \param[in] rwvCommand = enum value specifying which command type to issue
    //!   \param[in] updateFunction = callback function to update UI
    //!   \param[in] updateData = hidden data to pass to the callback function
    //!   \param[in] hideLBACounter = set to true to hide the LBA counter being printed to stdout
    //!
    //  Exit:
    //!   \return SUCCESS on successful completion, FAILURE = fail
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API int short_Generic_Test(tDevice *device, eRWVCommandType rwvCommand, custom_Update updateFunction, void *updateData, bool hideLBACounter);

    //-----------------------------------------------------------------------------
    //
    //  two_Minute_Generic_Read_Test()
    //
    //! \brief   Description:  This function performs a short generic read test that is time based and should complete within 2 minutes
    //
    //  Entry:
    //!   \param[in] device = file descriptor
    //!   \param[in] updateFunction = callback function to update UI
    //!   \param[in] updateData = hidden data to pass to the callback function
    //!   \param[in] hideLBACounter = set to true to hide the LBA counter being printed to stdout
    //!
    //  Exit:
    //!   \return SUCCESS on successful completion, FAILURE = fail
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API int two_Minute_Generic_Read_Test(tDevice *device, custom_Update updateFunction, void *updateData, bool hideLBACounter);

    //-----------------------------------------------------------------------------
    //
    //  two_Minute_Generic_Write_Test()
    //
    //! \brief   Description:  This function performs a short generic write test that is time based and should complete within 2 minutes
    //
    //  Entry:
    //!   \param[in] device = file descriptor
    //!   \param[in] updateFunction = callback function to update UI
    //!   \param[in] updateData = hidden data to pass to the callback function
    //!   \param[in] hideLBACounter = set to true to hide the LBA counter being printed to stdout
    //!
    //  Exit:
    //!   \return SUCCESS on successful completion, FAILURE = fail
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API int two_Minute_Generic_Write_Test(tDevice *device, custom_Update updateFunction, void *updateData, bool hideLBACounter);

    //-----------------------------------------------------------------------------
    //
    //  two_Minute_Generic_Verify_Test()
    //
    //! \brief   Description:  This function performs a short generic verify test that is time based and should complete within 2 minutes
    //
    //  Entry:
    //!   \param[in] device = file descriptor
    //!   \param[in] updateFunction = callback function to update UI
    //!   \param[in] updateData = hidden data to pass to the callback function
    //!   \param[in] hideLBACounter = set to true to hide the LBA counter being printed to stdout
    //!
    //  Exit:
    //!   \return SUCCESS on successful completion, FAILURE = fail
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API int two_Minute_Generic_Verify_Test(tDevice *device, custom_Update updateFunction, void *updateData, bool hideLBACounter);

    //-----------------------------------------------------------------------------
    //
    //  two_Minute_Generic_Test()
    //
    //! \brief   Description:  This function performs a short generic read, write, or verify test that is time based and should complete within 2 minutes
    //
    //  Entry:
    //!   \param[in] device = file descriptor
    //!   \param[in] rwvCommand = enum value specifying which command type to issue
    //!   \param[in] updateFunction = callback function to update UI
    //!   \param[in] updateData = hidden data to pass to the callback function
    //!   \param[in] hideLBACounter = set to true to hide the LBA counter being printed to stdout
    //!
    //  Exit:
    //!   \return SUCCESS on successful completion, FAILURE = fail
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API int two_Minute_Generic_Test(tDevice *device, eRWVCommandType rwvCommand, custom_Update updateFunction, void *updateData, bool hideLBACounter);

    //-----------------------------------------------------------------------------
    //
    //  long_Generic_Read_Test()
    //
    //! \brief   Description:  This function performs a long generic read test. The error limit is changeable to whatever you wish. Stop on error can be set (removes need for error limit). This operation can issue repairs to fix LBAs, this can be done on the fly (as they are found) or at the end of the scan. This function will print out a list of the bad LBAs found and their repair status at the end of the test.
    //
    //  Entry:
    //!   \param[in] device = file descriptor
    //!   \param[in] errorLimit = the maximum number of allowed errors in this operation
    //!   \param[in] stopOnError = set to true to stop the read test on the first error found.
    //!   \param[in] repairOnTheFly = set to true to issue repairs to LBAs as they are found to be bad. This option is mutually exclusive with the repairAtEnd. Do not set both to true
    //!   \param[in] repairAtEnd = set to true to issue repairs to LBAs upon completion of the scan or the error limit is reached. This option is mutually exclusive with the repairOnTheFly. Do not set both to true
    //!   \param[in] updateFunction = callback function to update UI
    //!   \param[in] updateData = hidden data to pass to the callback function
    //!   \param[in] hideLBACounter = set to true to hide the LBA counter being printed to stdout
    //!
    //  Exit:
    //!   \return SUCCESS on successful completion, FAILURE = fail
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API int long_Generic_Read_Test(tDevice *device, uint16_t errorLimit, bool stopOnError, bool repairOnTheFly, bool repairAtEnd, custom_Update updateFunction, void *updateData, bool hideLBACounter);

    //-----------------------------------------------------------------------------
    //
    //  long_Generic_Write_Test()
    //
    //! \brief   Description:  This function performs a long generic write test. The error limit is changeable to whatever you wish. Stop on error can be set (removes need for error limit). This operation can issue repairs to fix LBAs, this can be done on the fly (as they are found) or at the end of the scan. This function will print out a list of the bad LBAs found and their repair status at the end of the test.
    //
    //  Entry:
    //!   \param[in] device = file descriptor
    //!   \param[in] errorLimit = the maximum number of allowed errors in this operation
    //!   \param[in] stopOnError = set to true to stop the read test on the first error found.
    //!   \param[in] repairOnTheFly = set to true to issue repairs to LBAs as they are found to be bad. This option is mutually exclusive with the repairAtEnd. Do not set both to true
    //!   \param[in] repairAtEnd = set to true to issue repairs to LBAs upon completion of the scan or the error limit is reached. This option is mutually exclusive with the repairOnTheFly. Do not set both to true
    //!   \param[in] updateFunction = callback function to update UI
    //!   \param[in] updateData = hidden data to pass to the callback function
    //!   \param[in] hideLBACounter = set to true to hide the LBA counter being printed to stdout
    //!
    //  Exit:
    //!   \return SUCCESS on successful completion, FAILURE = fail
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API int long_Generic_Write_Test(tDevice *device, uint16_t errorLimit, bool stopOnError, bool repairOnTheFly, bool repairAtEnd, custom_Update updateFunction, void *updateData, bool hideLBACounter);

    //-----------------------------------------------------------------------------
    //
    //  long_Generic_Verify_Test()
    //
    //! \brief   Description:  This function performs a long generic verify test. The error limit is changeable to whatever you wish. Stop on error can be set (removes need for error limit). This operation can issue repairs to fix LBAs, this can be done on the fly (as they are found) or at the end of the scan. This function will print out a list of the bad LBAs found and their repair status at the end of the test.
    //
    //  Entry:
    //!   \param[in] device = file descriptor
    //!   \param[in] errorLimit = the maximum number of allowed errors in this operation
    //!   \param[in] stopOnError = set to true to stop the read test on the first error found.
    //!   \param[in] repairOnTheFly = set to true to issue repairs to LBAs as they are found to be bad. This option is mutually exclusive with the repairAtEnd. Do not set both to true
    //!   \param[in] repairAtEnd = set to true to issue repairs to LBAs upon completion of the scan or the error limit is reached. This option is mutually exclusive with the repairOnTheFly. Do not set both to true
    //!   \param[in] updateFunction = callback function to update UI
    //!   \param[in] updateData = hidden data to pass to the callback function
    //!   \param[in] hideLBACounter = set to true to hide the LBA counter being printed to stdout
    //!
    //  Exit:
    //!   \return SUCCESS on successful completion, FAILURE = fail
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API int long_Generic_Verify_Test(tDevice *device, uint16_t errorLimit, bool stopOnError, bool repairOnTheFly, bool repairAtEnd, custom_Update updateFunction, void *updateData, bool hideLBACounter);

    //-----------------------------------------------------------------------------
    //
    //  long_Generic_Test()
    //
    //! \brief   Description:  This function performs a long generic read, write, or verify test. The error limit is changeable to whatever you wish. Stop on error can be set (removes need for error limit). This operation can issue repairs to fix LBAs, this can be done on the fly (as they are found) or at the end of the scan. This function will print out a list of the bad LBAs found and their repair status at the end of the test.
    //
    //  Entry:
    //!   \param[in] device = file descriptor
    //!   \param[in] rwvCommand = enum value specifying which command type to issue
    //!   \param[in] errorLimit = the maximum number of allowed errors in this operation
    //!   \param[in] stopOnError = set to true to stop the read test on the first error found.
    //!   \param[in] repairOnTheFly = set to true to issue repairs to LBAs as they are found to be bad. This option is mutually exclusive with the repairAtEnd. Do not set both to true
    //!   \param[in] repairAtEnd = set to true to issue repairs to LBAs upon completion of the scan or the error limit is reached. This option is mutually exclusive with the repairOnTheFly. Do not set both to true
    //!   \param[in] updateFunction = callback function to update UI
    //!   \param[in] updateData = hidden data to pass to the callback function
    //!   \param[in] hideLBACounter = set to true to hide the LBA counter being printed to stdout
    //!
    //  Exit:
    //!   \return SUCCESS on successful completion, FAILURE = fail
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API int long_Generic_Test(tDevice *device, eRWVCommandType rwvCommand, uint16_t errorLimit, bool stopOnError, bool repairOnTheFly, bool repairAtEnd, custom_Update updateFunction, void *updateData, bool hideLBACounter);
    
    //-----------------------------------------------------------------------------
    //
    //  user_Sequential_Read_Test()
    //
    //! \brief   Description:  This function performs a user defined generic read test from a starting LBA for a range of LBAs. The error limit is changeable to whatever you wish. Stop on error can be set (removes need for error limit). This operation can issue repairs to fix LBAs, this can be done on the fly (as they are found) or at the end of the scan. This function will print out a list of the bad LBAs found and their repair status at the end of the test.
    //
    //  Entry:
    //!   \param[in] device = file descriptor
    //!   \param[in] startingLBA = the LBA to start the read scan at
    //!   \param[in] range = the range of LBAs to read during this test.
    //!   \param[in] errorLimit = the maximum number of allowed errors in this operation
    //!   \param[in] stopOnError = set to true to stop the read test on the first error found.
    //!   \param[in] repairOnTheFly = set to true to issue repairs to LBAs as they are found to be bad. This option is mutually exclusive with the repairAtEnd. Do not set both to true
    //!   \param[in] repairAtEnd = set to true to issue repairs to LBAs upon completion of the scan or the error limit is reached. This option is mutually exclusive with the repairOnTheFly. Do not set both to true
    //!   \param[in] updateFunction = callback function to update UI
    //!   \param[in] updateData = hidden data to pass to the callback function
    //!   \param[in] hideLBACounter = set to true to hide the LBA counter being printed to stdout
    //!
    //  Exit:
    //!   \return SUCCESS on successful completion, FAILURE = fail
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API int user_Sequential_Read_Test(tDevice *device, uint64_t startingLBA, uint64_t range, uint16_t errorLimit, bool stopOnError, bool repairOnTheFly, bool repairAtEnd, custom_Update updateFunction, void *updateData, bool hideLBACounter);

    //-----------------------------------------------------------------------------
    //
    //  user_Sequential_Write_Test()
    //
    //! \brief   Description:  This function performs a user defined generic write test from a starting LBA for a range of LBAs. The error limit is changeable to whatever you wish. Stop on error can be set (removes need for error limit). This operation can issue repairs to fix LBAs, this can be done on the fly (as they are found) or at the end of the scan. This function will print out a list of the bad LBAs found and their repair status at the end of the test.
    //
    //  Entry:
    //!   \param[in] device = file descriptor
    //!   \param[in] startingLBA = the LBA to start the read scan at
    //!   \param[in] range = the range of LBAs to read during this test.
    //!   \param[in] errorLimit = the maximum number of allowed errors in this operation
    //!   \param[in] stopOnError = set to true to stop the read test on the first error found.
    //!   \param[in] repairOnTheFly = set to true to issue repairs to LBAs as they are found to be bad. This option is mutually exclusive with the repairAtEnd. Do not set both to true
    //!   \param[in] repairAtEnd = set to true to issue repairs to LBAs upon completion of the scan or the error limit is reached. This option is mutually exclusive with the repairOnTheFly. Do not set both to true
    //!   \param[in] updateFunction = callback function to update UI
    //!   \param[in] updateData = hidden data to pass to the callback function
    //!   \param[in] hideLBACounter = set to true to hide the LBA counter being printed to stdout
    //!
    //  Exit:
    //!   \return SUCCESS on successful completion, FAILURE = fail
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API int user_Sequential_Write_Test(tDevice *device, uint64_t startingLBA, uint64_t range, uint16_t errorLimit, bool stopOnError, bool repairOnTheFly, bool repairAtEnd, custom_Update updateFunction, void *updateData, bool hideLBACounter);

    //-----------------------------------------------------------------------------
    //
    //  user_Sequential_Verify_Test()
    //
    //! \brief   Description:  This function performs a user defined generic verify test from a starting LBA for a range of LBAs. The error limit is changeable to whatever you wish. Stop on error can be set (removes need for error limit). This operation can issue repairs to fix LBAs, this can be done on the fly (as they are found) or at the end of the scan. This function will print out a list of the bad LBAs found and their repair status at the end of the test.
    //
    //  Entry:
    //!   \param[in] device = file descriptor
    //!   \param[in] startingLBA = the LBA to start the read scan at
    //!   \param[in] range = the range of LBAs to read during this test.
    //!   \param[in] errorLimit = the maximum number of allowed errors in this operation
    //!   \param[in] stopOnError = set to true to stop the read test on the first error found.
    //!   \param[in] repairOnTheFly = set to true to issue repairs to LBAs as they are found to be bad. This option is mutually exclusive with the repairAtEnd. Do not set both to true
    //!   \param[in] repairAtEnd = set to true to issue repairs to LBAs upon completion of the scan or the error limit is reached. This option is mutually exclusive with the repairOnTheFly. Do not set both to true
    //!   \param[in] updateFunction = callback function to update UI
    //!   \param[in] updateData = hidden data to pass to the callback function
    //!   \param[in] hideLBACounter = set to true to hide the LBA counter being printed to stdout
    //!
    //  Exit:
    //!   \return SUCCESS on successful completion, FAILURE = fail
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API int user_Sequential_Verify_Test(tDevice *device, uint64_t startingLBA, uint64_t range, uint16_t errorLimit, bool stopOnError, bool repairOnTheFly, bool repairAtEnd, custom_Update updateFunction, void *updateData, bool hideLBACounter);

    //-----------------------------------------------------------------------------
    //
    //  user_Sequential_Test()
    //
    //! \brief   Description:  This function performs a user defined generic read, write, or verify test from a starting LBA for a range of LBAs. The error limit is changeable to whatever you wish. Stop on error can be set (removes need for error limit). This operation can issue repairs to fix LBAs, this can be done on the fly (as they are found) or at the end of the scan. This function will print out a list of the bad LBAs found and their repair status at the end of the test.
    //
    //  Entry:
    //!   \param[in] device = file descriptor
    //!   \param[in] rwvCommand = enum value specifying which command type to issue
    //!   \param[in] startingLBA = the LBA to start the read scan at
    //!   \param[in] range = the range of LBAs to read during this test.
    //!   \param[in] errorLimit = the maximum number of allowed errors in this operation
    //!   \param[in] stopOnError = set to true to stop the read test on the first error found.
    //!   \param[in] repairOnTheFly = set to true to issue repairs to LBAs as they are found to be bad. This option is mutually exclusive with the repairAtEnd. Do not set both to true
    //!   \param[in] repairAtEnd = set to true to issue repairs to LBAs upon completion of the scan or the error limit is reached. This option is mutually exclusive with the repairOnTheFly. Do not set both to true
    //!   \param[in] updateFunction = callback function to update UI
    //!   \param[in] updateData = hidden data to pass to the callback function
    //!   \param[in] hideLBACounter = set to true to hide the LBA counter being printed to stdout
    //!
    //  Exit:
    //!   \return SUCCESS on successful completion, FAILURE = fail
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API int user_Sequential_Test(tDevice *device, eRWVCommandType rwvCommand, uint64_t startingLBA, uint64_t range, uint16_t errorLimit, bool stopOnError, bool repairOnTheFly, bool repairAtEnd, custom_Update updateFunction, void *updateData, bool hideLBACounter);

    //-----------------------------------------------------------------------------
    //
    //  butterfly_Read_Test()
    //
    //! \brief   Description:  This function performs a butterfly read test for the amount of time specified. Will stop on the first error found
    //
    //  Entry:
    //!   \param[in] device = file descriptor
    //!   \param[in] timeLimitSeconds = the time limit for this operation to run in seconds
    //!   \param[in] updateFunction = callback function to update UI
    //!   \param[in] updateData = hidden data to pass to the callback function
    //!   \param[in] hideLBACounter = set to true to hide the LBA counter being printed to stdout
    //!
    //  Exit:
    //!   \return SUCCESS on successful completion, FAILURE = fail
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API int butterfly_Read_Test(tDevice *device, time_t timeLimitSeconds, custom_Update updateFunction, void *updateData, bool hideLBACounter);

    //-----------------------------------------------------------------------------
    //
    //  butterfly_Write_Test()
    //
    //! \brief   Description:  This function performs a butterfly write test for the amount of time specified. Will stop on the first error found
    //
    //  Entry:
    //!   \param[in] device = file descriptor
    //!   \param[in] timeLimitSeconds = the time limit for this operation to run in seconds
    //!   \param[in] updateFunction = callback function to update UI
    //!   \param[in] updateData = hidden data to pass to the callback function
    //!   \param[in] hideLBACounter = set to true to hide the LBA counter being printed to stdout
    //!
    //  Exit:
    //!   \return SUCCESS on successful completion, FAILURE = fail
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API int butterfly_Write_Test(tDevice *device, time_t timeLimitSeconds, custom_Update updateFunction, void *updateData, bool hideLBACounter);

    //-----------------------------------------------------------------------------
    //
    //  butterfly_Verify_Test()
    //
    //! \brief   Description:  This function performs a butterfly verify test for the amount of time specified. Will stop on the first error found
    //
    //  Entry:
    //!   \param[in] device = file descriptor
    //!   \param[in] timeLimitSeconds = the time limit for this operation to run in seconds
    //!   \param[in] updateFunction = callback function to update UI
    //!   \param[in] updateData = hidden data to pass to the callback function
    //!   \param[in] hideLBACounter = set to true to hide the LBA counter being printed to stdout
    //!
    //  Exit:
    //!   \return SUCCESS on successful completion, FAILURE = fail
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API int butterfly_Verify_Test(tDevice *device, time_t timeLimitSeconds, custom_Update updateFunction, void *updateData, bool hideLBACounter);

    //-----------------------------------------------------------------------------
    //
    //  butterfly_Test()
    //
    //! \brief   Description:  This function performs a butterfly read, write, or verify test for the amount of time specified. Will stop on the first error found
    //
    //  Entry:
    //!   \param[in] device = file descriptor
    //!   \param[in] rwvcommand = enum value specifying which command type to issue
    //!   \param[in] timeLimitSeconds = the time limit for this operation to run in seconds
    //!   \param[in] updateFunction = callback function to update UI
    //!   \param[in] updateData = hidden data to pass to the callback function
    //!   \param[in] hideLBACounter = set to true to hide the LBA counter being printed to stdout
    //!
    //  Exit:
    //!   \return SUCCESS on successful completion, FAILURE = fail
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API int butterfly_Test(tDevice *device, eRWVCommandType rwvcommand, time_t timeLimitSeconds, custom_Update updateFunction, void *updateData, bool hideLBACounter);

    //-----------------------------------------------------------------------------
    //
    //  random_Read_Test()
    //
    //! \brief   Description:  This function performs a random read test for the amount of time specified. Will stop on the first error found
    //
    //  Entry:
    //!   \param[in] device = file descriptor
    //!   \param[in] timeLimitSeconds = the time limit for this operation to run in seconds
    //!   \param[in] updateFunction = callback function to update UI
    //!   \param[in] updateData = hidden data to pass to the callback function
    //!   \param[in] hideLBACounter = set to true to hide the LBA counter being printed to stdout
    //!
    //  Exit:
    //!   \return SUCCESS on successful completion, FAILURE = fail
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API int random_Read_Test(tDevice *device, time_t timeLimitSeconds, custom_Update updateFunction, void *updateData, bool hideLBACounter);

    //-----------------------------------------------------------------------------
    //
    //  random_Write_Test()
    //
    //! \brief   Description:  This function performs a random write test for the amount of time specified. Will stop on the first error found
    //
    //  Entry:
    //!   \param[in] device = file descriptor
    //!   \param[in] timeLimitSeconds = the time limit for this operation to run in seconds
    //!   \param[in] updateFunction = callback function to update UI
    //!   \param[in] updateData = hidden data to pass to the callback function
    //!   \param[in] hideLBACounter = set to true to hide the LBA counter being printed to stdout
    //!
    //  Exit:
    //!   \return SUCCESS on successful completion, FAILURE = fail
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API int random_Write_Test(tDevice *device, time_t timeLimitSeconds, custom_Update updateFunction, void *updateData, bool hideLBACounter);

    //-----------------------------------------------------------------------------
    //
    //  random_Verify_Test()
    //
    //! \brief   Description:  This function performs a random verify test for the amount of time specified. Will stop on the first error found
    //
    //  Entry:
    //!   \param[in] device = file descriptor
    //!   \param[in] timeLimitSeconds = the time limit for this operation to run in seconds
    //!   \param[in] updateFunction = callback function to update UI
    //!   \param[in] updateData = hidden data to pass to the callback function
    //!   \param[in] hideLBACounter = set to true to hide the LBA counter being printed to stdout
    //!
    //  Exit:
    //!   \return SUCCESS on successful completion, FAILURE = fail
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API int random_Verify_Test(tDevice *device, time_t timeLimitSeconds, custom_Update updateFunction, void *updateData, bool hideLBACounter);

    //-----------------------------------------------------------------------------
    //
    //  random_Test()
    //
    //! \brief   Description:  This function performs a random read, write, or verify test for the amount of time specified. Will stop on the first error found
    //
    //  Entry:
    //!   \param[in] device = file descriptor
    //!   \param[in] rwvcommand = enum value specifying which command type to issue
    //!   \param[in] timeLimitSeconds = the time limit for this operation to run in seconds
    //!   \param[in] updateFunction = callback function to update UI
    //!   \param[in] updateData = hidden data to pass to the callback function
    //!   \param[in] hideLBACounter = set to true to hide the LBA counter being printed to stdout
    //!
    //  Exit:
    //!   \return SUCCESS on successful completion, FAILURE = fail
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API int random_Test(tDevice *device, eRWVCommandType rwvcommand, time_t timeLimitSeconds, custom_Update updateFunction, void *updateData, bool hideLBACounter);

    //will do a read, write, or verify timed test. Each test runs at OD, ID, random, and butterfly for the time specified
    OPENSEA_OPERATIONS_API int read_Write_Or_Verify_Timed_Test(tDevice *device, eRWVCommandType testMode, uint32_t timePerTestSeconds, uint16_t *numberOfCommandTimeouts, uint16_t *numberOfCommandFailures, custom_Update updateFunction, void *updateData);

    OPENSEA_OPERATIONS_API int diameter_Test_Range(tDevice *device, eRWVCommandType testMode, bool outer, bool middle, bool inner, uint64_t numberOfLBAs, uint16_t errorLimit, bool stopOnError, bool repairOnTheFly, bool repairAtEnd, custom_Update updateFunction, void *updateData, bool hideLBACounter);

    OPENSEA_OPERATIONS_API int diameter_Test_Time(tDevice *device, eRWVCommandType testMode, bool outer, bool middle, bool inner, uint64_t timeInSecondsPerDiameter, uint16_t errorLimit, bool stopOnError, bool repairOnTheFly, bool repairAtEnd, bool hideLBACounter);

    OPENSEA_OPERATIONS_API int user_Timed_Test(tDevice *device, eRWVCommandType rwvCommand, uint64_t startingLBA, uint64_t timeInSeconds, uint16_t errorLimit, bool stopOnError, bool repairOnTheFly, bool repairAtEnd, custom_Update updateFunction, void *updateData, bool hideLBACounter);

#if defined (__cplusplus)
}
#endif
