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
    //  is_Trim_Or_Unmap_Supported( tDevice * device )
    //
    //! \brief   Get whether a device supports TRIM (ATA) or UNMAP (SCSI) commands. Can also tell you how many descriptors can be specified in the command.
    //
    //  Entry:
    //!   \param device - file descriptor
    //!   \param maxTrimOrUnmapBlockDescriptors - pointer to a uint16_t to hold the number of decriptors that can be sent. This can be NULL. On ATA, this will be a value divisible by 64 since 64 descriptors can be placed inside each TRIM command.
    //!   \param maxLBACount - this is only for SAS since SAS can specify the maximum number of LBA's to unmap in a single command. If maxTrimOrUnmapBlockDescriptors is non-NULL, this MUST be non-NULL as well or neither value will be filled in
    //!
    //  Exit:
    //!   \return SUCCESS = good, !SUCCESS something went wrong see error codes
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API bool is_Trim_Or_Unmap_Supported(tDevice *device, uint32_t *maxTrimOrUnmapBlockDescriptors, uint32_t *maxLBACount);

    //-----------------------------------------------------------------------------
    //
    //  trim_unmap_range( tDevice * device )
    //
    //! \brief   TRIM or UNMAP a range of LBAs from a starting LBA until the end of the range. This will auto detect ATA vs SCSI to send the appropriate command
    //
    //  Entry:
    //!   \param device - file descriptor
    //!   \param startLBA - the LBA to start the unmap/trim at
    //!   \param range - the range of LBAs to trim/unmap from the starting LBA
    //!
    //  Exit:
    //!   \return SUCCESS = good, !SUCCESS something went wrong see error codes
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API int trim_Unmap_Range(tDevice *device, uint64_t startLBA, uint64_t range);

    //-----------------------------------------------------------------------------
    //
    //  scsi_Unmap_Range( tDevice * device )
    //
    //! \brief   UNMAP a range of LBAs from a starting LBA until the end of the range. This will send the SCSI unmap command, possibly multiple times depending on the range. A SAT driver or interface may translate this to an ATA TRIM command, but that is beyond this library
    //
    //  Entry:
    //!   \param device - file descriptor
    //!   \param startLBA - the LBA to start the unmap/trim at
    //!   \param range - the range of LBAs to trim/unmap from the starting LBA
    //!
    //  Exit:
    //!   \return SUCCESS = good, !SUCCESS something went wrong see error codes
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API int scsi_Unmap_Range(tDevice *device, uint64_t startLBA, uint64_t range);

    //-----------------------------------------------------------------------------
    //
    //  ata_Trim_Range( tDevice * device )
    //
    //! \brief   TRIM a range of LBAs from a starting LBA until the end of the range. This will send the ATA data set management command with the TRIM bit set, possibly multiple times depending on the range.
    //
    //  Entry:
    //!   \param device - file descriptor
    //!   \param startLBA - the LBA to start the unmap/trim at
    //!   \param range - the range of LBAs to trim/unmap from the starting LBA
    //!
    //  Exit:
    //!   \return SUCCESS = good, !SUCCESS something went wrong see error codes
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API int ata_Trim_Range(tDevice *device, uint64_t startLBA, uint64_t range);

    //-----------------------------------------------------------------------------
    //
    //  nvme_Deallocate_Range( tDevice * device )
    //
    //! \brief   Deallocate a range of LBAs from a starting LBA until the end of the range. This will send the NVMe data set management command with the deallocate bit set. Currently, this will only issue a single command. NOTE: Lower level OS's might have limitations on this command.
    //
    //  Entry:
    //!   \param device - file descriptor
    //!   \param startLBA - the LBA to start the unmap/trim at
    //!   \param range - the range of LBAs to trim/unmap from the starting LBA
    //!
    //  Exit:
    //!   \return SUCCESS = good, !SUCCESS something went wrong see error codes
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API int nvme_Deallocate_Range(tDevice *device, uint64_t startLBA, uint64_t range);

#if defined (__cplusplus)
}
#endif
