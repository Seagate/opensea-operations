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
// \file sector_repair.h
// \brief This file defines the functions related to sector repair. This file also contians functions for creating uncorrectables on the drive since these functions are useful for testing repairs

#pragma once

#include "operations_Common.h"

#if defined (__cplusplus)
extern "C"
{
#endif

    typedef enum _eRepairStatus
    {
        NOT_REPAIRED,
        REPAIR_FAILED,
        REPAIRED,
        REPAIR_NOT_REQUIRED,
        UNABLE_TO_REPAIR_ACCESS_DENIED, //This can happen if the OS blocks our commands to the LBA we are trying to repair. Saw this happen on Win10, when a secondary drive with a Win10 installation on it had errors, we could not repair them. - TJE
    }eRepairStatus;

    typedef struct _errorLBA
    {
        uint64_t errorAddress;
        eRepairStatus repairStatus;
    }errorLBA, *ptrErrorLBA;
   
    //-----------------------------------------------------------------------------
    //
    //  repair_LBA()
    //
    //! \brief   Description:  This function takes an LBA, then aligns it to the beginning of a physical block, then issues a write to the full physical block to repair the whole physical block. By doing this, reallocations are minimized.
    //
    //  Entry:
    //!   \param[in] device = file descriptor
    //!   \param[in] LBA = the LBA to repair
    //!   \param[in] forcePassthroughCommand = boolean value to force sending a SAT passthrough command to repair the LBA instead of a SCSI command. This value is ignored on non-ATA devices.
    //!   \param[in] automaticWriteReallocationEnabled = when set to true, will perform write reallocation. If set to false, reassign blocks command will be used (ATA translation will be run on ATA drives). Ignored when forcePassthroughCommand is set
    //!   \param[in] automaticReadReallocationEnabled = when set to true, will attempt read reallocation before attempting write reallocation or using the reassign blocks command. Ignored when forcePassthroughCommand is set
    //!
    //  Exit:
    //!   \return SUCCESS on successful completion, FAILURE = fail
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API int repair_LBA(tDevice *device, ptrErrorLBA LBA, bool forcePassthroughCommand, bool automaticWriteReallocationEnabled, bool automaticReadReallocationEnabled);

    //-----------------------------------------------------------------------------
    //
    //  print_LBA_Error_List()
    //
    //! \brief   Description:  This function takes a list of LBAs with errors and their current repair status and prints it to the screen.
    //
    //  Entry:
    //!   \param[in] LBAs = pointer to the list of LBAs in the ERROR LBA struct type to read and print to the screen
    //!   \param[in] numberOfErrors = this is the number of items in the list to print (list length)
    //!
    //  Exit:
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API void print_LBA_Error_List(ptrErrorLBA const LBAs, uint16_t numberOfErrors);

    OPENSEA_OPERATIONS_API int get_Automatic_Reallocation_Support(tDevice *device, bool *automaticWriteReallocationEnabled, bool *automaticReadReallocationEnabled);

    //Use this call to determine if you've already logged an error in the list so that you don't log it again
    OPENSEA_OPERATIONS_API bool is_LBA_Already_In_The_List(ptrErrorLBA LBAList, uint32_t numberOfLBAsInTheList, uint64_t lba);

    //Use this call to sort the list of Error LBAs. This will also remove any duplicates it finds and adjust the value of numberOfLBAsInTheList
    OPENSEA_OPERATIONS_API void sort_Error_LBA_List(ptrErrorLBA LBAList, uint32_t *numberOfLBAsInTheList);

    OPENSEA_OPERATIONS_API uint32_t find_LBA_Entry_In_List(ptrErrorLBA LBAList, uint32_t numberOfLBAsInTheList, uint64_t lba);//returns UINT32_MAX if not found

#if defined (__cplusplus)
}
#endif
