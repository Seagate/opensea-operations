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
// \file host_erase.h
// \brief This file defines the function for performing a host based erase functions (host issues a series of write commands)

#pragma once

#include "operations_Common.h"

#if defined (__cplusplus)
extern "C"
{
#endif

    //-----------------------------------------------------------------------------
    //
    //  erase_Range( tDevice * device )
    //
    //! \brief   Erase a range of LBAs on a drive.
    //
    //  Entry:
    //!   \param device - file descriptor
    //!   \param eraseRangeStart - the LBA to start the erase at
    //!   \param eraseRangeEnd - the end LBA. If this is set to MAX64, this will be corrected to the MaxLba of the drive
    //!   \param pattern - pointer to a buffer with a pattern to use.
    //!   \param patternLength - length of the buffer pointed to by the pattern parameter. This must be at least 1 logical sector in size
    //!   \param[in] hideLBACounter = set to true to hide the LBA counter being printed to stdout
    //!
    //  Exit:
    //!   \return SUCCESS = good, !SUCCESS something went wrong see error codes
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API int erase_Range(tDevice *device, uint64_t eraseRangeStart, uint64_t eraseRangeEnd, uint8_t *pattern, uint32_t patternLength, bool hideLBACounter);

    //-----------------------------------------------------------------------------
    //
    //  erase_Time( tDevice * device )
    //
    //! \brief   Erase a LBAs from a starting LBA for a time in seconds.
    //
    //  Entry:
    //!   \param device - file descriptor
    //!   \param eraseStartLBA - the LBA to start the erase at
    //!   \param eraseTime - a time in seconds to erase for
    //!   \param pattern - pointer to a buffer with a pattern to use.
    //!   \param patternLength - length of the buffer pointed to by the pattern parameter. This must be at least 1 logical sector in size
    //!   \param[in] hideLBACounter = set to true to hide the LBA counter being printed to stdout
    //!
    //  Exit:
    //!   \return SUCCESS = good, !SUCCESS something went wrong see error codes
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API int erase_Time(tDevice *device, uint64_t eraseStartLBA, time_t eraseTime, uint8_t *pattern, uint32_t patternLength, bool hideLBACounter);

#if defined (__cplusplus)
}
#endif
