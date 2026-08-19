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
// \file generic_tests.h
// \brief This file defines the functions related to generic read tests

#pragma once

#include "operations_Common.h"
#include "sector_repair.h"

#if defined(__cplusplus)
extern "C"
{
#endif

    typedef enum eRWVCommandTypeEnum
    {
        RWV_COMMAND_READ,
        RWV_COMMAND_WRITE,
        RWV_COMMAND_VERIFY,
        RWV_COMMAND_INVALID
    } eRWVCommandType;

    //-----------------------------------------------------------------------------
    //
    //  read_Write_Seek_Command()
    //
    //! \brief   Description:  Will perform a read, write, or seek (verify) command to a specified LBA for a specified
    //! range
    //
    //  Entry:
    //!   \param[in] device = file descriptor
    //!   \param[in] rwvCommand = enum value specifying which command type to issue
    //!   \param[in] lba = LBA to start the command at
    //!   \param[in] ptrData = pointer to the data buffer to use for the command (May be M_NULLPTR for RWV_VERIFY)
    //!   \param[in] dataSize = number of data bytes to transfer (This should be a value that is a multiple of the
    //!   device's logical sector size)
    //!
    //  Exit:
    //!   \return SUCCESS on successful completion, FAILURE = fail
    //
    //-----------------------------------------------------------------------------
    M_PARAM_RO(1)
    M_NONNULL_IF_NONZERO_PARAM(4, 5)
    M_PARAM_RW_SIZE(4, 5)
    OPENSEA_OPERATIONS_API eReturnValues read_Write_Seek_Command(const tDevice* M_NONNULL device,
                                                                 eRWVCommandType          rwvCommand,
                                                                 uint64_t                 lba,
                                                                 uint8_t* M_NULLABLE      ptrData,
                                                                 uint32_t                 dataSize);

    //-----------------------------------------------------------------------------
    //
    //  sequential_RWV()
    //
    //! \brief   Description:  Function to perform a sequential read, write, or verify over a range of LBAs. This
    //! function will stop on the first error and return that error as an output parameter for the caller to interpret.
    //
    //  Entry:
    //!   \param[in] device = file descriptor
    //!   \param[in] rwvCommand = enum value specifying which command type to issue
    //!   \param[in] startingLBA = LBA to start the sequential read at
    //!   \param[in] range = the range of LBAs from the starting LBA to read
    //!   \param[in] sectorCount = number of sectors to read at a time. This will be adjusted as necessary at the end of
    //!   the range to not go beyond the end of the specified range \param[in] failingLBA = pointer to a uint64_t that
    //!   will hold the LBA that this test failed on. If no failure was found, this will be set to UINT64_MAX \param[in]
    //!   updateFunction = callback function to update UI \param[in] updateData = hidden data to pass to the callback
    //!   function \param[in] hideLBACounter = set to true to hide the LBA counter being printed to stdout
    //!
    //  Exit:
    //!   \return SUCCESS on successful completion, FAILURE = fail
    //
    //-----------------------------------------------------------------------------
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
                                                        bool                     hideLBACounter);

    //-----------------------------------------------------------------------------
    //
    //  sequential_Write()
    //
    //! \brief   Description:  Function to perform a sequential write over a range of LBAs. This function will stop on
    //! the first error and return that error as an output parameter for the caller to interpret.
    //
    //  Entry:
    //!   \param[in] device = file descriptor
    //!   \param[in] startingLBA = LBA to start the sequential read at
    //!   \param[in] range = the range of LBAs from the starting LBA to read
    //!   \param[in] sectorCount = number of sectors to read at a time. This will be adjusted as necessary at the end of
    //!   the range to not go beyond the end of the specified range \param[in] failingLBA = pointer to a uint64_t that
    //!   will hold the LBA that this test failed on. If no failure was found, this will be set to UINT64_MAX \param[in]
    //!   updateFunction = callback function to update UI \param[in] updateData = hidden data to pass to the callback
    //!   function \param[in] hideLBACounter = set to true to hide the LBA counter being printed to stdout
    //!
    //  Exit:
    //!   \return SUCCESS on successful completion, FAILURE = fail
    //
    //-----------------------------------------------------------------------------
    M_PARAM_RO(1)
    M_PARAM_WO(5)
    OPENSEA_OPERATIONS_API eReturnValues sequential_Write(const tDevice* M_NONNULL device,
                                                          uint64_t                 startingLBA,
                                                          uint64_t                 range,
                                                          uint64_t                 sectorCount,
                                                          uint64_t* M_NONNULL      failingLBA,
                                                          custom_Update M_NULLABLE updateFunction,
                                                          void* M_NULLABLE         updateData,
                                                          bool                     hideLBACounter);

    //-----------------------------------------------------------------------------
    //
    //  sequential_Verify()
    //
    //! \brief   Description:  Function to perform a sequential verify over a range of LBAs. This function will stop on
    //! the first error and return that error as an output parameter for the caller to interpret.
    //
    //  Entry:
    //!   \param[in] device = file descriptor
    //!   \param[in] startingLBA = LBA to start the sequential read at
    //!   \param[in] range = the range of LBAs from the starting LBA to read
    //!   \param[in] sectorCount = number of sectors to read at a time. This will be adjusted as necessary at the end of
    //!   the range to not go beyond the end of the specified range \param[in] failingLBA = pointer to a uint64_t that
    //!   will hold the LBA that this test failed on. If no failure was found, this will be set to UINT64_MAX \param[in]
    //!   updateFunction = callback function to update UI \param[in] updateData = hidden data to pass to the callback
    //!   function \param[in] hideLBACounter = set to true to hide the LBA counter being printed to stdout
    //!
    //  Exit:
    //!   \return SUCCESS on successful completion, FAILURE = fail
    //
    //-----------------------------------------------------------------------------
    M_PARAM_RO(1)
    M_PARAM_WO(5)
    OPENSEA_OPERATIONS_API eReturnValues sequential_Verify(const tDevice* M_NONNULL device,
                                                           uint64_t                 startingLBA,
                                                           uint64_t                 range,
                                                           uint64_t                 sectorCount,
                                                           uint64_t* M_NONNULL      failingLBA,
                                                           custom_Update M_NULLABLE updateFunction,
                                                           void* M_NULLABLE         updateData,
                                                           bool                     hideLBACounter);

    //-----------------------------------------------------------------------------
    //
    //  sequential_Read()
    //
    //! \brief   Description:  Function to perform a sequential read over a range of LBAs. This function will stop on
    //! the first error and return that error as an output parameter for the caller to interpret.
    //
    //  Entry:
    //!   \param[in] device = file descriptor
    //!   \param[in] startingLBA = LBA to start the sequential read at
    //!   \param[in] range = the range of LBAs from the starting LBA to read
    //!   \param[in] sectorCount = number of sectors to read at a time. This will be adjusted as necessary at the end of
    //!   the range to not go beyond the end of the specified range \param[in] failingLBA = pointer to a uint64_t that
    //!   will hold the LBA that this test failed on. If no failure was found, this will be set to UINT64_MAX \param[in]
    //!   updateFunction = callback function to update UI \param[in] updateData = hidden data to pass to the callback
    //!   function \param[in] hideLBACounter = set to true to hide the LBA counter being printed to stdout
    //!
    //  Exit:
    //!   \return SUCCESS on successful completion, FAILURE = fail
    //
    //-----------------------------------------------------------------------------
    M_PARAM_RO(1)
    M_PARAM_WO(5)
    OPENSEA_OPERATIONS_API eReturnValues sequential_Read(const tDevice* M_NONNULL device,
                                                         uint64_t                 startingLBA,
                                                         uint64_t                 range,
                                                         uint64_t                 sectorCount,
                                                         uint64_t* M_NONNULL      failingLBA,
                                                         custom_Update M_NULLABLE updateFunction,
                                                         void* M_NULLABLE         updateData,
                                                         bool                     hideLBACounter);

    //-----------------------------------------------------------------------------
    //
    //  short_Generic_Read_Test()
    //
    //! \brief   Description:  This function performs a short generic test like in ST4W. 1% read at OD, 1% read at ID,
    //! then read 5000 random LBAs. This function will stop on the first error found
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
    M_PARAM_RO(1)
    OPENSEA_OPERATIONS_API eReturnValues short_Generic_Read_Test(const tDevice* M_NONNULL device,
                                                                 custom_Update M_NULLABLE updateFunction,
                                                                 void* M_NULLABLE         updateData,
                                                                 bool                     hideLBACounter);

    //-----------------------------------------------------------------------------
    //
    //  short_Generic_Verify_Test()
    //
    //! \brief   Description:  This function performs a short generic test using verify commands . 1% read at OD, 1%
    //! read at ID, then read 5000 random LBAs. This function will stop on the first error found
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
    M_PARAM_RO(1)
    OPENSEA_OPERATIONS_API eReturnValues short_Generic_Verify_Test(const tDevice* M_NONNULL device,
                                                                   custom_Update M_NULLABLE updateFunction,
                                                                   void* M_NULLABLE         updateData,
                                                                   bool                     hideLBACounter);

    //-----------------------------------------------------------------------------
    //
    //  short_Generic_Write_Test()
    //
    //! \brief   Description:  This function performs a short generic test using write commands . 1% read at OD, 1% read
    //! at ID, then read 5000 random LBAs. This function will stop on the first error found
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
    M_PARAM_RO(1)
    OPENSEA_OPERATIONS_API eReturnValues short_Generic_Write_Test(const tDevice* M_NONNULL device,
                                                                  custom_Update M_NULLABLE updateFunction,
                                                                  void* M_NULLABLE         updateData,
                                                                  bool                     hideLBACounter);

    //-----------------------------------------------------------------------------
    //
    //  short_Generic_Test()
    //
    //! \brief   Description:  This function performs a short generic test using read, write, or verify commands . 1%
    //! read at OD, 1% read at ID, then read 5000 random LBAs. This function will stop on the first error found
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
    M_PARAM_RO(1)
    OPENSEA_OPERATIONS_API eReturnValues short_Generic_Test(const tDevice* M_NONNULL device,
                                                            eRWVCommandType          rwvCommand,
                                                            custom_Update M_NULLABLE updateFunction,
                                                            void* M_NULLABLE         updateData,
                                                            bool                     hideLBACounter);

    //-----------------------------------------------------------------------------
    //
    //  two_Minute_Generic_Read_Test()
    //
    //! \brief   Description:  This function performs a short generic read test that is time based and should complete
    //! within 2 minutes
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
    M_PARAM_RO(1)
    OPENSEA_OPERATIONS_API eReturnValues two_Minute_Generic_Read_Test(const tDevice* M_NONNULL device,
                                                                      custom_Update M_NULLABLE updateFunction,
                                                                      void* M_NULLABLE         updateData,
                                                                      bool                     hideLBACounter);

    //-----------------------------------------------------------------------------
    //
    //  two_Minute_Generic_Write_Test()
    //
    //! \brief   Description:  This function performs a short generic write test that is time based and should complete
    //! within 2 minutes
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
    M_PARAM_RO(1)
    OPENSEA_OPERATIONS_API eReturnValues two_Minute_Generic_Write_Test(const tDevice* M_NONNULL device,
                                                                       custom_Update M_NULLABLE updateFunction,
                                                                       void* M_NULLABLE         updateData,
                                                                       bool                     hideLBACounter);

    //-----------------------------------------------------------------------------
    //
    //  two_Minute_Generic_Verify_Test()
    //
    //! \brief   Description:  This function performs a short generic verify test that is time based and should complete
    //! within 2 minutes
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
    M_PARAM_RO(1)
    OPENSEA_OPERATIONS_API eReturnValues two_Minute_Generic_Verify_Test(const tDevice* M_NONNULL device,
                                                                        custom_Update M_NULLABLE updateFunction,
                                                                        void* M_NULLABLE         updateData,
                                                                        bool                     hideLBACounter);

    //-----------------------------------------------------------------------------
    //
    //  two_Minute_Generic_Test()
    //
    //! \brief   Description:  This function performs a short generic read, write, or verify test that is time based and
    //! should complete within 2 minutes
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
    M_PARAM_RO(1)
    OPENSEA_OPERATIONS_API eReturnValues two_Minute_Generic_Test(const tDevice* M_NONNULL device,
                                                                 eRWVCommandType          rwvCommand,
                                                                 custom_Update M_NULLABLE updateFunction,
                                                                 void* M_NULLABLE         updateData,
                                                                 bool                     hideLBACounter);

    //-----------------------------------------------------------------------------
    //
    //  long_Generic_Read_Test()
    //
    //! \brief   Description:  This function performs a long generic read test. The error limit is changeable to
    //! whatever you wish. Stop on error can be set (removes need for error limit). This operation can issue repairs to
    //! fix LBAs, this can be done on the fly (as they are found) or at the end of the scan. This function will print
    //! out a list of the bad LBAs found and their repair status at the end of the test.
    //
    //  Entry:
    //!   \param[in] device = file descriptor
    //!   \param[in] errorLimit = the maximum number of allowed errors in this operation
    //!   \param[in] stopOnError = set to true to stop the read test on the first error found.
    //!   \param[in] repairOnTheFly = set to true to issue repairs to LBAs as they are found to be bad. This option is
    //!   mutually exclusive with the repairAtEnd. Do not set both to true \param[in] repairAtEnd = set to true to issue
    //!   repairs to LBAs upon completion of the scan or the error limit is reached. This option is mutually exclusive
    //!   with the repairOnTheFly. Do not set both to true \param[in] updateFunction = callback function to update UI
    //!   \param[in] updateData = hidden data to pass to the callback function
    //!   \param[in] hideLBACounter = set to true to hide the LBA counter being printed to stdout
    //!
    //  Exit:
    //!   \return SUCCESS on successful completion, FAILURE = fail
    //
    //-----------------------------------------------------------------------------
    M_PARAM_RO(1)
    OPENSEA_OPERATIONS_API eReturnValues long_Generic_Read_Test(const tDevice* M_NONNULL device,
                                                                uint16_t                 errorLimit,
                                                                bool                     stopOnError,
                                                                bool                     repairOnTheFly,
                                                                bool                     repairAtEnd,
                                                                custom_Update M_NULLABLE updateFunction,
                                                                void* M_NULLABLE         updateData,
                                                                bool                     hideLBACounter);

    //-----------------------------------------------------------------------------
    //
    //  long_Generic_Write_Test()
    //
    //! \brief   Description:  This function performs a long generic write test. The error limit is changeable to
    //! whatever you wish. Stop on error can be set (removes need for error limit). This operation can issue repairs to
    //! fix LBAs, this can be done on the fly (as they are found) or at the end of the scan. This function will print
    //! out a list of the bad LBAs found and their repair status at the end of the test.
    //
    //  Entry:
    //!   \param[in] device = file descriptor
    //!   \param[in] errorLimit = the maximum number of allowed errors in this operation
    //!   \param[in] stopOnError = set to true to stop the read test on the first error found.
    //!   \param[in] repairOnTheFly = set to true to issue repairs to LBAs as they are found to be bad. This option is
    //!   mutually exclusive with the repairAtEnd. Do not set both to true \param[in] repairAtEnd = set to true to issue
    //!   repairs to LBAs upon completion of the scan or the error limit is reached. This option is mutually exclusive
    //!   with the repairOnTheFly. Do not set both to true \param[in] updateFunction = callback function to update UI
    //!   \param[in] updateData = hidden data to pass to the callback function
    //!   \param[in] hideLBACounter = set to true to hide the LBA counter being printed to stdout
    //!
    //  Exit:
    //!   \return SUCCESS on successful completion, FAILURE = fail
    //
    //-----------------------------------------------------------------------------
    M_PARAM_RO(1)
    OPENSEA_OPERATIONS_API eReturnValues long_Generic_Write_Test(const tDevice* M_NONNULL device,
                                                                 uint16_t                 errorLimit,
                                                                 bool                     stopOnError,
                                                                 bool                     repairOnTheFly,
                                                                 bool                     repairAtEnd,
                                                                 custom_Update M_NULLABLE updateFunction,
                                                                 void* M_NULLABLE         updateData,
                                                                 bool                     hideLBACounter);

    //-----------------------------------------------------------------------------
    //
    //  long_Generic_Verify_Test()
    //
    //! \brief   Description:  This function performs a long generic verify test. The error limit is changeable to
    //! whatever you wish. Stop on error can be set (removes need for error limit). This operation can issue repairs to
    //! fix LBAs, this can be done on the fly (as they are found) or at the end of the scan. This function will print
    //! out a list of the bad LBAs found and their repair status at the end of the test.
    //
    //  Entry:
    //!   \param[in] device = file descriptor
    //!   \param[in] errorLimit = the maximum number of allowed errors in this operation
    //!   \param[in] stopOnError = set to true to stop the read test on the first error found.
    //!   \param[in] repairOnTheFly = set to true to issue repairs to LBAs as they are found to be bad. This option is
    //!   mutually exclusive with the repairAtEnd. Do not set both to true \param[in] repairAtEnd = set to true to issue
    //!   repairs to LBAs upon completion of the scan or the error limit is reached. This option is mutually exclusive
    //!   with the repairOnTheFly. Do not set both to true \param[in] updateFunction = callback function to update UI
    //!   \param[in] updateData = hidden data to pass to the callback function
    //!   \param[in] hideLBACounter = set to true to hide the LBA counter being printed to stdout
    //!
    //  Exit:
    //!   \return SUCCESS on successful completion, FAILURE = fail
    //
    //-----------------------------------------------------------------------------
    M_PARAM_RO(1)
    OPENSEA_OPERATIONS_API eReturnValues long_Generic_Verify_Test(const tDevice* M_NONNULL device,
                                                                  uint16_t                 errorLimit,
                                                                  bool                     stopOnError,
                                                                  bool                     repairOnTheFly,
                                                                  bool                     repairAtEnd,
                                                                  custom_Update M_NULLABLE updateFunction,
                                                                  void* M_NULLABLE         updateData,
                                                                  bool                     hideLBACounter);

    //-----------------------------------------------------------------------------
    //
    //  long_Generic_Test()
    //
    //! \brief   Description:  This function performs a long generic read, write, or verify test. The error limit is
    //! changeable to whatever you wish. Stop on error can be set (removes need for error limit). This operation can
    //! issue repairs to fix LBAs, this can be done on the fly (as they are found) or at the end of the scan. This
    //! function will print out a list of the bad LBAs found and their repair status at the end of the test.
    //
    //  Entry:
    //!   \param[in] device = file descriptor
    //!   \param[in] rwvCommand = enum value specifying which command type to issue
    //!   \param[in] errorLimit = the maximum number of allowed errors in this operation
    //!   \param[in] stopOnError = set to true to stop the read test on the first error found.
    //!   \param[in] repairOnTheFly = set to true to issue repairs to LBAs as they are found to be bad. This option is
    //!   mutually exclusive with the repairAtEnd. Do not set both to true \param[in] repairAtEnd = set to true to issue
    //!   repairs to LBAs upon completion of the scan or the error limit is reached. This option is mutually exclusive
    //!   with the repairOnTheFly. Do not set both to true \param[in] updateFunction = callback function to update UI
    //!   \param[in] updateData = hidden data to pass to the callback function
    //!   \param[in] hideLBACounter = set to true to hide the LBA counter being printed to stdout
    //!
    //  Exit:
    //!   \return SUCCESS on successful completion, FAILURE = fail
    //
    //-----------------------------------------------------------------------------
    M_PARAM_RO(1)
    OPENSEA_OPERATIONS_API eReturnValues long_Generic_Test(const tDevice* M_NONNULL device,
                                                           eRWVCommandType          rwvCommand,
                                                           uint16_t                 errorLimit,
                                                           bool                     stopOnError,
                                                           bool                     repairOnTheFly,
                                                           bool                     repairAtEnd,
                                                           custom_Update M_NULLABLE updateFunction,
                                                           void* M_NULLABLE         updateData,
                                                           bool                     hideLBACounter);

    //-----------------------------------------------------------------------------
    //
    //  user_Sequential_Read_Test()
    //
    //! \brief   Description:  This function performs a user defined generic read test from a starting LBA for a range
    //! of LBAs. The error limit is changeable to whatever you wish. Stop on error can be set (removes need for error
    //! limit). This operation can issue repairs to fix LBAs, this can be done on the fly (as they are found) or at the
    //! end of the scan. This function will print out a list of the bad LBAs found and their repair status at the end of
    //! the test.
    //
    //  Entry:
    //!   \param[in] device = file descriptor
    //!   \param[in] startingLBA = the LBA to start the read scan at
    //!   \param[in] range = the range of LBAs to read during this test.
    //!   \param[in] errorLimit = the maximum number of allowed errors in this operation
    //!   \param[in] stopOnError = set to true to stop the read test on the first error found.
    //!   \param[in] repairOnTheFly = set to true to issue repairs to LBAs as they are found to be bad. This option is
    //!   mutually exclusive with the repairAtEnd. Do not set both to true \param[in] repairAtEnd = set to true to issue
    //!   repairs to LBAs upon completion of the scan or the error limit is reached. This option is mutually exclusive
    //!   with the repairOnTheFly. Do not set both to true \param[in] updateFunction = callback function to update UI
    //!   \param[in] updateData = hidden data to pass to the callback function
    //!   \param[in] hideLBACounter = set to true to hide the LBA counter being printed to stdout
    //!
    //  Exit:
    //!   \return SUCCESS on successful completion, FAILURE = fail
    //
    //-----------------------------------------------------------------------------
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
                                                                   bool                     hideLBACounter);

    //-----------------------------------------------------------------------------
    //
    //  user_Sequential_Write_Test()
    //
    //! \brief   Description:  This function performs a user defined generic write test from a starting LBA for a range
    //! of LBAs. The error limit is changeable to whatever you wish. Stop on error can be set (removes need for error
    //! limit). This operation can issue repairs to fix LBAs, this can be done on the fly (as they are found) or at the
    //! end of the scan. This function will print out a list of the bad LBAs found and their repair status at the end of
    //! the test.
    //
    //  Entry:
    //!   \param[in] device = file descriptor
    //!   \param[in] startingLBA = the LBA to start the read scan at
    //!   \param[in] range = the range of LBAs to read during this test.
    //!   \param[in] errorLimit = the maximum number of allowed errors in this operation
    //!   \param[in] stopOnError = set to true to stop the read test on the first error found.
    //!   \param[in] repairOnTheFly = set to true to issue repairs to LBAs as they are found to be bad. This option is
    //!   mutually exclusive with the repairAtEnd. Do not set both to true \param[in] repairAtEnd = set to true to issue
    //!   repairs to LBAs upon completion of the scan or the error limit is reached. This option is mutually exclusive
    //!   with the repairOnTheFly. Do not set both to true \param[in] updateFunction = callback function to update UI
    //!   \param[in] updateData = hidden data to pass to the callback function
    //!   \param[in] hideLBACounter = set to true to hide the LBA counter being printed to stdout
    //!
    //  Exit:
    //!   \return SUCCESS on successful completion, FAILURE = fail
    //
    //-----------------------------------------------------------------------------
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
                                                                    bool                     hideLBACounter);

    //-----------------------------------------------------------------------------
    //
    //  user_Sequential_Verify_Test()
    //
    //! \brief   Description:  This function performs a user defined generic verify test from a starting LBA for a range
    //! of LBAs. The error limit is changeable to whatever you wish. Stop on error can be set (removes need for error
    //! limit). This operation can issue repairs to fix LBAs, this can be done on the fly (as they are found) or at the
    //! end of the scan. This function will print out a list of the bad LBAs found and their repair status at the end of
    //! the test.
    //
    //  Entry:
    //!   \param[in] device = file descriptor
    //!   \param[in] startingLBA = the LBA to start the read scan at
    //!   \param[in] range = the range of LBAs to read during this test.
    //!   \param[in] errorLimit = the maximum number of allowed errors in this operation
    //!   \param[in] stopOnError = set to true to stop the read test on the first error found.
    //!   \param[in] repairOnTheFly = set to true to issue repairs to LBAs as they are found to be bad. This option is
    //!   mutually exclusive with the repairAtEnd. Do not set both to true \param[in] repairAtEnd = set to true to issue
    //!   repairs to LBAs upon completion of the scan or the error limit is reached. This option is mutually exclusive
    //!   with the repairOnTheFly. Do not set both to true \param[in] updateFunction = callback function to update UI
    //!   \param[in] updateData = hidden data to pass to the callback function
    //!   \param[in] hideLBACounter = set to true to hide the LBA counter being printed to stdout
    //!
    //  Exit:
    //!   \return SUCCESS on successful completion, FAILURE = fail
    //
    //-----------------------------------------------------------------------------
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
                                                                     bool                     hideLBACounter);

    //-----------------------------------------------------------------------------
    //
    //  user_Sequential_Test()
    //
    //! \brief   Description:  This function performs a user defined generic read, write, or verify test from a starting
    //! LBA for a range of LBAs. The error limit is changeable to whatever you wish. Stop on error can be set (removes
    //! need for error limit). This operation can issue repairs to fix LBAs, this can be done on the fly (as they are
    //! found) or at the end of the scan. This function will print out a list of the bad LBAs found and their repair
    //! status at the end of the test.
    //
    //  Entry:
    //!   \param[in] device = file descriptor
    //!   \param[in] rwvCommand = enum value specifying which command type to issue
    //!   \param[in] startingLBA = the LBA to start the read scan at
    //!   \param[in] range = the range of LBAs to read during this test.
    //!   \param[in] errorLimit = the maximum number of allowed errors in this operation
    //!   \param[in] stopOnError = set to true to stop the read test on the first error found.
    //!   \param[in] repairOnTheFly = set to true to issue repairs to LBAs as they are found to be bad. This option is
    //!   mutually exclusive with the repairAtEnd. Do not set both to true \param[in] repairAtEnd = set to true to issue
    //!   repairs to LBAs upon completion of the scan or the error limit is reached. This option is mutually exclusive
    //!   with the repairOnTheFly. Do not set both to true \param[in] updateFunction = callback function to update UI
    //!   \param[in] updateData = hidden data to pass to the callback function
    //!   \param[in] hideLBACounter = set to true to hide the LBA counter being printed to stdout
    //!
    //  Exit:
    //!   \return SUCCESS on successful completion, FAILURE = fail
    //
    //-----------------------------------------------------------------------------
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
                                                              bool                     hideLBACounter);

    //-----------------------------------------------------------------------------
    //
    //  user_Sequential_Test_LBA_Error_List()
    //
    //! \brief   Description:  This function performs a user defined generic read, write, or verify test from a starting
    //! LBA for a range of LBAs and returns a list of all errors found during the test. The error limit is changeable to
    //! whatever you wish. Stop on error can be set (removes need for error limit). This operation can issue repairs to
    //! fix LBAs, this can be done on the fly (as they are found) or at the end of the scan. The caller is responsible
    //! for freeing the error list using safe_free_error_lba() when done.
    //
    //  Entry:
    //!   \param[in] device = file descriptor
    //!   \param[in] rwvCommand = enum value specifying which command type to issue
    //!   \param[in] startingLBA = the LBA to start the read scan at
    //!   \param[in] range = the range of LBAs to read during this test
    //!   \param[in] errorLimit = the maximum number of allowed errors in this operation (0 = unlimited)
    //!   \param[in] stopOnError = set to true to stop the test on the first error found
    //!   \param[in] repairOnTheFly = set to true to issue repairs to LBAs as they are found to be bad. Mutually
    //!   exclusive with repairAtEnd. Do not set both to true
    //!   \param[in] repairAtEnd = set to true to issue repairs to LBAs upon completion of the scan or when the error
    //!   limit is reached. Mutually exclusive with repairOnTheFly. Do not set both to true
    //!   \param[in] updateFunction = callback function to update UI (optional, can be M_NULLPTR)
    //!   \param[in] updateData = hidden data to pass to the callback function (optional, can be M_NULLPTR)
    //!   \param[in] hideLBACounter = set to true to hide the LBA counter being printed to stdout
    //!   \param[out] errorList = pointer to errorLBA pointer array. Function allocates memory for the error list.
    //!   Caller must free using safe_free_error_lba()
    //!   \param[out] errorListSize = pointer to uint16_t to receive the count of errors found
    //!
    //  Exit:
    //!   \return SUCCESS on successful completion, FAILURE if errors were found
    //
    //-----------------------------------------------------------------------------
    M_PARAM_RO(1)
    OPENSEA_OPERATIONS_API eReturnValues user_Sequential_Test_LBA_Error_List(const tDevice* M_NONNULL device,
                                                                             eRWVCommandType          rwvCommand,
                                                                             uint64_t                 startingLBA,
                                                                             uint64_t                 range,
                                                                             uint16_t                 errorLimit,
                                                                             bool                     stopOnError,
                                                                             bool                     repairOnTheFly,
                                                                             bool                     repairAtEnd,
                                                                             custom_Update M_NULLABLE updateFunction,
                                                                             void* M_NULLABLE         updateData,
                                                                             bool                     hideLBACounter,
                                                                             errorLBA* M_NONNULL* M_NONNULL errorList,
                                                                             uint16_t* M_NONNULL errorListSize);

    //-----------------------------------------------------------------------------
    //
    //  butterfly_Read_Test()
    //
    //! \brief   Description:  This function performs a butterfly read test for the amount of time specified. Will stop
    //! on the first error found
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
    M_PARAM_RO(1)
    OPENSEA_OPERATIONS_API eReturnValues butterfly_Read_Test(const tDevice* M_NONNULL device,
                                                             uint64_t                 timeLimitSeconds,
                                                             custom_Update M_NULLABLE updateFunction,
                                                             void* M_NULLABLE         updateData,
                                                             bool                     hideLBACounter);

    //-----------------------------------------------------------------------------
    //
    //  butterfly_Write_Test()
    //
    //! \brief   Description:  This function performs a butterfly write test for the amount of time specified. Will stop
    //! on the first error found
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
    M_PARAM_RO(1)
    OPENSEA_OPERATIONS_API eReturnValues butterfly_Write_Test(const tDevice* M_NONNULL device,
                                                              uint64_t                 timeLimitSeconds,
                                                              custom_Update M_NULLABLE updateFunction,
                                                              void* M_NULLABLE         updateData,
                                                              bool                     hideLBACounter);

    //-----------------------------------------------------------------------------
    //
    //  butterfly_Verify_Test()
    //
    //! \brief   Description:  This function performs a butterfly verify test for the amount of time specified. Will
    //! stop on the first error found
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
    M_PARAM_RO(1)
    OPENSEA_OPERATIONS_API eReturnValues butterfly_Verify_Test(const tDevice* M_NONNULL device,
                                                               uint64_t                 timeLimitSeconds,
                                                               custom_Update M_NULLABLE updateFunction,
                                                               void* M_NULLABLE         updateData,
                                                               bool                     hideLBACounter);

    //-----------------------------------------------------------------------------
    //
    //  butterfly_Test()
    //
    //! \brief   Description:  This function performs a butterfly read, write, or verify test for the amount of time
    //! specified. Will stop on the first error found
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
    M_PARAM_RO(1)
    OPENSEA_OPERATIONS_API eReturnValues butterfly_Test(const tDevice* M_NONNULL device,
                                                        eRWVCommandType          rwvcommand,
                                                        uint64_t                 timeLimitSeconds,
                                                        custom_Update M_NULLABLE updateFunction,
                                                        void* M_NULLABLE         updateData,
                                                        bool                     hideLBACounter);

    //-----------------------------------------------------------------------------
    //
    //  random_Read_Test_With_Range()
    //
    //! \brief   Description:  This function performs a random read test within a specified LBA range with a specified
    //! number of seeks. Will stop on the first error found
    //
    //  Entry:
    //!   \param[in] device = file descriptor
    //!   \param[in] startLBA = starting LBA for random test range
    //!   \param[in] endLBA = ending LBA for random test range
    //!   \param[in] numberOfSeeks = number of random seek operations to perform
    //!   \param[in] updateFunction = callback function to update UI
    //!   \param[in] updateData = hidden data to pass to the callback function
    //!   \param[in] hideLBACounter = set to true to hide the LBA counter being printed to stdout
    //!
    //  Exit:
    //!   \return SUCCESS on successful completion, FAILURE = fail
    //
    //-----------------------------------------------------------------------------
    M_PARAM_RO(1)
    OPENSEA_OPERATIONS_API eReturnValues random_Read_Test_With_Range(const tDevice* M_NONNULL device,
                                                                     uint64_t                 startLBA,
                                                                     uint64_t                 endLBA,
                                                                     uint16_t                 numberOfSeeks,
                                                                     custom_Update M_NULLABLE updateFunction,
                                                                     void* M_NULLABLE         updateData,
                                                                     bool                     hideLBACounter);

    //-----------------------------------------------------------------------------
    //
    //  random_Write_Test_With_Range()
    //
    //! \brief   Description:  This function performs a random write test within a specified LBA range with a specified
    //! number of seeks. Will stop on the first error found
    //
    //  Entry:
    //!   \param[in] device = file descriptor
    //!   \param[in] startLBA = starting LBA for random test range
    //!   \param[in] endLBA = ending LBA for random test range
    //!   \param[in] numberOfSeeks = number of random seek operations to perform
    //!   \param[in] updateFunction = callback function to update UI
    //!   \param[in] updateData = hidden data to pass to the callback function
    //!   \param[in] hideLBACounter = set to true to hide the LBA counter being printed to stdout
    //!
    //  Exit:
    //!   \return SUCCESS on successful completion, FAILURE = fail
    //
    //-----------------------------------------------------------------------------
    M_PARAM_RO(1)
    OPENSEA_OPERATIONS_API eReturnValues random_Write_Test_With_Range(const tDevice* M_NONNULL device,
                                                                      uint64_t                 startLBA,
                                                                      uint64_t                 endLBA,
                                                                      uint16_t                 numberOfSeeks,
                                                                      custom_Update M_NULLABLE updateFunction,
                                                                      void* M_NULLABLE         updateData,
                                                                      bool                     hideLBACounter);

    //-----------------------------------------------------------------------------
    //
    //  random_Verify_Test_With_Range()
    //
    //! \brief   Description:  This function performs a random verify test within a specified LBA range with a
    //! specified number of seeks. Will stop on the first error found
    //
    //  Entry:
    //!   \param[in] device = file descriptor
    //!   \param[in] startLBA = starting LBA for random test range
    //!   \param[in] endLBA = ending LBA for random test range
    //!   \param[in] numberOfSeeks = number of random seek operations to perform
    //!   \param[in] updateFunction = callback function to update UI
    //!   \param[in] updateData = hidden data to pass to the callback function
    //!   \param[in] hideLBACounter = set to true to hide the LBA counter being printed to stdout
    //!
    //  Exit:
    //!   \return SUCCESS on successful completion, FAILURE = fail
    //
    //-----------------------------------------------------------------------------
    M_PARAM_RO(1)
    OPENSEA_OPERATIONS_API eReturnValues random_Verify_Test_With_Range(const tDevice* M_NONNULL device,
                                                                       uint64_t                 startLBA,
                                                                       uint64_t                 endLBA,
                                                                       uint16_t                 numberOfSeeks,
                                                                       custom_Update M_NULLABLE updateFunction,
                                                                       void* M_NULLABLE         updateData,
                                                                       bool                     hideLBACounter);

    //-----------------------------------------------------------------------------
    //
    //  random_Test_With_Range()
    //
    //! \brief   Description:  This function performs a random read, write, or verify test within a specified LBA
    //! range with a specified number of seeks. Will stop on the first error found
    //
    //  Entry:
    //!   \param[in] device = file descriptor
    //!   \param[in] rwvcommand = enum value specifying which command type to issue
    //!   \param[in] startLBA = starting LBA for random test range
    //!   \param[in] endLBA = ending LBA for random test range
    //!   \param[in] numberOfSeeks = number of random seek operations to perform
    //!   \param[in] updateFunction = callback function to update UI
    //!   \param[in] updateData = hidden data to pass to the callback function
    //!   \param[in] hideLBACounter = set to true to hide the LBA counter being printed to stdout
    //!
    //  Exit:
    //!   \return SUCCESS on successful completion, FAILURE = fail
    //
    //-----------------------------------------------------------------------------
    M_PARAM_RO(1)
    OPENSEA_OPERATIONS_API eReturnValues random_Test_With_Range(const tDevice* M_NONNULL device,
                                                                eRWVCommandType          rwvcommand,
                                                                uint64_t                 startLBA,
                                                                uint64_t                 endLBA,
                                                                uint16_t                 numberOfSeeks,
                                                                custom_Update M_NULLABLE updateFunction,
                                                                void* M_NULLABLE         updateData,
                                                                bool                     hideLBACounter);

    //-----------------------------------------------------------------------------
    //
    //  butterfly_Read_Test_With_Range()
    //
    //! \brief   Description:  This function performs a butterfly read test within a specified LBA range with a
    //! specified number of seeks. Will stop on the first error found
    //
    //  Entry:
    //!   \param[in] device = file descriptor
    //!   \param[in] startLBA = starting LBA for butterfly test range (outer diameter)
    //!   \param[in] endLBA = ending LBA for butterfly test range (inner diameter)
    //!   \param[in] numberOfSeeks = number of butterfly seek operations to perform
    //!   \param[in] updateFunction = callback function to update UI
    //!   \param[in] updateData = hidden data to pass to the callback function
    //!   \param[in] hideLBACounter = set to true to hide the LBA counter being printed to stdout
    //!
    //  Exit:
    //!   \return SUCCESS on successful completion, FAILURE = fail
    //
    //-----------------------------------------------------------------------------
    M_PARAM_RO(1)
    OPENSEA_OPERATIONS_API eReturnValues butterfly_Read_Test_With_Range(const tDevice* M_NONNULL device,
                                                                        uint64_t                 startLBA,
                                                                        uint64_t                 endLBA,
                                                                        uint16_t                 numberOfSeeks,
                                                                        custom_Update M_NULLABLE updateFunction,
                                                                        void* M_NULLABLE         updateData,
                                                                        bool                     hideLBACounter);

    //-----------------------------------------------------------------------------
    //
    //  butterfly_Write_Test_With_Range()
    //
    //! \brief   Description:  This function performs a butterfly write test within a specified LBA range with a
    //! specified number of seeks. Will stop on the first error found
    //
    //  Entry:
    //!   \param[in] device = file descriptor
    //!   \param[in] startLBA = starting LBA for butterfly test range (outer diameter)
    //!   \param[in] endLBA = ending LBA for butterfly test range (inner diameter)
    //!   \param[in] numberOfSeeks = number of butterfly seek operations to perform
    //!   \param[in] updateFunction = callback function to update UI
    //!   \param[in] updateData = hidden data to pass to the callback function
    //!   \param[in] hideLBACounter = set to true to hide the LBA counter being printed to stdout
    //!
    //  Exit:
    //!   \return SUCCESS on successful completion, FAILURE = fail
    //
    //-----------------------------------------------------------------------------
    M_PARAM_RO(1)
    OPENSEA_OPERATIONS_API eReturnValues butterfly_Write_Test_With_Range(const tDevice* M_NONNULL device,
                                                                         uint64_t                 startLBA,
                                                                         uint64_t                 endLBA,
                                                                         uint16_t                 numberOfSeeks,
                                                                         custom_Update M_NULLABLE updateFunction,
                                                                         void* M_NULLABLE         updateData,
                                                                         bool                     hideLBACounter);

    //-----------------------------------------------------------------------------
    //
    //  butterfly_Verify_Test_With_Range()
    //
    //! \brief   Description:  This function performs a butterfly verify test within a specified LBA range with a
    //! specified number of seeks. Will stop on the first error found
    //
    //  Entry:
    //!   \param[in] device = file descriptor
    //!   \param[in] startLBA = starting LBA for butterfly test range (outer diameter)
    //!   \param[in] endLBA = ending LBA for butterfly test range (inner diameter)
    //!   \param[in] numberOfSeeks = number of butterfly seek operations to perform
    //!   \param[in] updateFunction = callback function to update UI
    //!   \param[in] updateData = hidden data to pass to the callback function
    //!   \param[in] hideLBACounter = set to true to hide the LBA counter being printed to stdout
    //!
    //  Exit:
    //!   \return SUCCESS on successful completion, FAILURE = fail
    //
    //-----------------------------------------------------------------------------
    M_PARAM_RO(1)
    OPENSEA_OPERATIONS_API eReturnValues butterfly_Verify_Test_With_Range(const tDevice* M_NONNULL device,
                                                                          uint64_t                 startLBA,
                                                                          uint64_t                 endLBA,
                                                                          uint16_t                 numberOfSeeks,
                                                                          custom_Update M_NULLABLE updateFunction,
                                                                          void* M_NULLABLE         updateData,
                                                                          bool                     hideLBACounter);

    //-----------------------------------------------------------------------------
    //
    //  butterfly_Test_With_Range()
    //
    //! \brief   Description:  This function performs a butterfly read, write, or verify test within a specified LBA
    //! range with a specified number of seeks. Will stop on the first error found
    //
    //  Entry:
    //!   \param[in] device = file descriptor
    //!   \param[in] rwvcommand = enum value specifying which command type to issue
    //!   \param[in] startLBA = starting LBA for butterfly test range (outer diameter)
    //!   \param[in] endLBA = ending LBA for butterfly test range (inner diameter)
    //!   \param[in] numberOfSeeks = number of butterfly seek operations to perform
    //!   \param[in] updateFunction = callback function to update UI
    //!   \param[in] updateData = hidden data to pass to the callback function
    //!   \param[in] hideLBACounter = set to true to hide the LBA counter being printed to stdout
    //!
    //  Exit:
    //!   \return SUCCESS on successful completion, FAILURE = fail
    //
    //-----------------------------------------------------------------------------
    M_PARAM_RO(1)
    OPENSEA_OPERATIONS_API eReturnValues butterfly_Test_With_Range(const tDevice* M_NONNULL device,
                                                                   eRWVCommandType          rwvcommand,
                                                                   uint64_t                 startLBA,
                                                                   uint64_t                 endLBA,
                                                                   uint16_t                 numberOfSeeks,
                                                                   custom_Update M_NULLABLE updateFunction,
                                                                   void* M_NULLABLE         updateData,
                                                                   bool                     hideLBACounter);

    //-----------------------------------------------------------------------------
    //
    //  random_Read_Test()
    //
    //! \brief   Description:  This function performs a random read test for the amount of time specified. Will stop on
    //! the first error found
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
    M_PARAM_RO(1)
    OPENSEA_OPERATIONS_API eReturnValues random_Read_Test(const tDevice* M_NONNULL device,
                                                          uint64_t                 timeLimitSeconds,
                                                          custom_Update M_NULLABLE updateFunction,
                                                          void* M_NULLABLE         updateData,
                                                          bool                     hideLBACounter);

    //-----------------------------------------------------------------------------
    //
    //  random_Write_Test()
    //
    //! \brief   Description:  This function performs a random write test for the amount of time specified. Will stop on
    //! the first error found
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
    M_PARAM_RO(1)
    OPENSEA_OPERATIONS_API eReturnValues random_Write_Test(const tDevice* M_NONNULL device,
                                                           uint64_t                 timeLimitSeconds,
                                                           custom_Update M_NULLABLE updateFunction,
                                                           void* M_NULLABLE         updateData,
                                                           bool                     hideLBACounter);

    //-----------------------------------------------------------------------------
    //
    //  random_Verify_Test()
    //
    //! \brief   Description:  This function performs a random verify test for the amount of time specified. Will stop
    //! on the first error found
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
    M_PARAM_RO(1)
    OPENSEA_OPERATIONS_API eReturnValues random_Verify_Test(const tDevice* M_NONNULL device,
                                                            uint64_t                 timeLimitSeconds,
                                                            custom_Update M_NULLABLE updateFunction,
                                                            void* M_NULLABLE         updateData,
                                                            bool                     hideLBACounter);

    //-----------------------------------------------------------------------------
    //
    //  random_Test()
    //
    //! \brief   Description:  This function performs a random read, write, or verify test for the amount of time
    //! specified. Will stop on the first error found
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
    M_PARAM_RO(1)
    OPENSEA_OPERATIONS_API eReturnValues random_Test(const tDevice* M_NONNULL device,
                                                     eRWVCommandType          rwvcommand,
                                                     uint64_t                 timeLimitSeconds,
                                                     custom_Update M_NULLABLE updateFunction,
                                                     void* M_NULLABLE         updateData,
                                                     bool                     hideLBACounter);

    //-----------------------------------------------------------------------------
    //
    //  sweep_Test()
    //
    //! \brief   Description:  This function performs a sweep read, write, or verify test for the number of times
    //! specified. Will stop on the first error found
    //
    //  Entry:
    //!   \param[in] device = file descriptor
    //!   \param[in] rwvcommand = enum value specifying which command type to issue
    //!   \param[in] sweepCount = number of times the operation will run on drive
    //!
    //  Exit:
    //!   \return SUCCESS on successful completion, FAILURE = fail
    //
    //-----------------------------------------------------------------------------
    M_PARAM_RO(1)
    OPENSEA_OPERATIONS_API eReturnValues sweep_Test(const tDevice* M_NONNULL device,
                                                    eRWVCommandType          rwvcommand,
                                                    uint32_t                 sweepCount);

    // will do a read, write, or verify timed test. Each test runs at OD, ID, random, and butterfly for the time
    // specified
    M_PARAM_RO(1)
    M_PARAM_WO(4)
    M_PARAM_WO(5)
    OPENSEA_OPERATIONS_API eReturnValues read_Write_Or_Verify_Timed_Test(const tDevice* M_NONNULL device,
                                                                         eRWVCommandType          testMode,
                                                                         uint32_t                 timePerTestSeconds,
                                                                         uint16_t* M_NONNULL numberOfCommandTimeouts,
                                                                         uint16_t* M_NONNULL numberOfCommandFailures,
                                                                         custom_Update M_NULLABLE updateFunction,
                                                                         void* M_NULLABLE         updateData);

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
                                                             bool                     hideLBACounter);

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
                                                            void* M_NULLABLE         updateData);

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
                                                         bool                     hideLBACounter);

    typedef enum eZeroVerifyTestTypeEnum
    {
        ZERO_VERIFY_TYPE_FULL,
        ZERO_VERIFY_TYPE_QUICK,
        ZERO_VERIFY_TYPE_INVALID
    } eZeroVerifyTestType;

    //-----------------------------------------------------------------------------
    //
    //  zero_Verify_Test()
    //
    //! \brief   Description:  This function performs a zero pattern verification with read and validate it against
    //! "zero". This will return on first "zero" mismatch
    //
    //  Entry:
    //!   \param[in] device = file descriptor
    //!   \param[in] zeroVerifyTestType = enum value specifying which mode for zero verification to run
    //!   \param[in] hideLBACounter = set to true to hide the LBA counter being printed to stdout
    //!
    //  Exit:
    //!   \return SUCCESS on successful completion, FAILURE = fail
    //
    //-----------------------------------------------------------------------------
    M_PARAM_RO(1)
    OPENSEA_OPERATIONS_API eReturnValues zero_Verify_Test(const tDevice* M_NONNULL device,
                                                          eZeroVerifyTestType      zeroVerifyTestType,
                                                          bool                     hideLBACounter);

    M_PARAM_RO(1)
    OPENSEA_OPERATIONS_API eReturnValues full_Zero_Verify_Test(const tDevice* M_NONNULL device, bool hideLBACounter);

    M_PARAM_RO(1)
    OPENSEA_OPERATIONS_API eReturnValues quick_Zero_Verify_Test(const tDevice* M_NONNULL device, bool hideLBACounter);

#if defined(__cplusplus)
}
#endif
