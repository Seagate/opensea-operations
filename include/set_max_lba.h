//
// Do NOT modify or remove this copyright and license
//
// Copyright (c) 2012-2023 Seagate Technology LLC and/or its Affiliates, All Rights Reserved
//
// This software is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
//
// ******************************************************************************************
// 
// \file set_max_lba.h
// \brief This file defines the functions for setting the maxLBA on a device

#pragma once

#include "operations_Common.h"

#if defined (__cplusplus)
extern "C"
{
#endif

    //-----------------------------------------------------------------------------
    //
    //  ata_Get_Native_Max_LBA(tDevice *device, uint64_t *nativeMaxLBA)
    //
    //! \brief   Gets the native maxLBA from an ATA device with the HPA or AMA feature set
    //
    //  Entry:
    //!   \param[in]  device file descriptor
    //!   \param[out]  nativeMaxLBA the maxLBA of the device. If neither HPA or AMA are supported, then this will be set to UINT64_MAX
    //!
    //  Exit:
    //!   \return SUCCESS = good, !SUCCESS something went wrong see error codes
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API int ata_Get_Native_Max_LBA(tDevice *device, uint64_t *nativeMaxLBA);

    //-----------------------------------------------------------------------------
    //
    //  ata_Get_Native_Max_LBA(tDevice *device, uint64_t *nativeMaxLBA)
    //
    //! \brief   Gets the native maxLBA for the specified device.
    //
    //  Entry:
    //!   \param[in]  device file descriptor
    //!   \param[out]  nativeMaxLBA the maxLBA of the device. If unable to retrieve or not supported, UINT64_MAX is returned
    //!
    //  Exit:
    //!   \return SUCCESS = good, !SUCCESS something went wrong see error codes
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API int get_Native_Max_LBA(tDevice *device, uint64_t *nativeMaxLBA);

    //-----------------------------------------------------------------------------
    //
    //  scsi_Set_Max_LBA( tDevice * device )
    //
    //! \brief   Sets the maxLBA of the selected device using SCSI methods
    //
    //  Entry:
    //!   \param[in]  device file descriptor
    //!   \param[in]  newMaxLBA the new maxLBA you wish to set
    //!   \param[in]  reset if set to 1 (or higher), this will reset to the native max, otherwise it will use the newMaxLBA param to set the maxLBA
    //!
    //  Exit:
    //!   \return SUCCESS = good, !SUCCESS something went wrong see error codes
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API int scsi_Set_Max_LBA(tDevice *device, uint64_t newMaxLBA, bool reset);

    //-----------------------------------------------------------------------------
    //
    //  ata_Set_Max_LBA( tDevice * device )
    //
    //! \brief   Sets the maxLBA of the selected device using HPA or AMA feature sets
    //
    //  Entry:
    //!   \param[in]  device file descriptor
    //!   \param[in]  newMaxLBA the new maxLBA you wish to set
    //!   \param[in]  reset if set to 1 (or higher), this will reset to the native max, otherwise it will use the newMaxLBA param to set the maxLBA
    //!
    //  Exit:
    //!   \return SUCCESS = good, !SUCCESS something went wrong see error codes
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API int ata_Set_Max_LBA(tDevice *device, uint64_t newMaxLBA, bool reset);

    //-----------------------------------------------------------------------------
    //
    //  set_Max_LBA( tDevice * device )
    //
    //! \brief   Sets the maxLBA of the selected device. This will work with new and old methods.
    //!          If ATA, we have only implemented the legacy method for 48bit drives
    //
    //  Entry:
    //!   \param[in]  device file descriptor
    //!   \param[in]  newMaxLBA the new maxLBA you wish to set
    //!   \param[in]  reset if set to 1 (or higher), this will reset to the native max, otherwise it will use the newMaxLBA param to set the maxLBA
    //!
    //  Exit:
    //!   \return SUCCESS = good, !SUCCESS something went wrong see error codes
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API int set_Max_LBA(tDevice *device, uint64_t newMaxLBA, bool reset);

    //-----------------------------------------------------------------------------
    //
    //  restore_Max_LBA_For_Erase(tDevice* device)
    //
    //! \brief   This function is specifically named since it has a main purpose: restoring max LBA to erase a drive as much as possible
    //!          or to allow validation of an erase as much as possible.
    //!          Because of this, it handles all the ATA checks to make sure all features are restored or a proper
    //!          error code for frozen or access denied is returned (HPA/AMAC/DCO and HPA security are all handled)
    //
    //  Entry:
    //!   \param[in]  device file descriptor
    //!
    //  Exit:
    //!   \return SUCCESS = good, DEVICE_ACCESS_DENIED means HPA security is active and blocked the restoration of the maxLBA
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API int restore_Max_LBA_For_Erase(tDevice* device);

    //-----------------------------------------------------------------------------
    //
    //  bool is_Max_LBA_In_Sync_With_Adapter_Or_Driver(tDevice* device)
    //
    //! \brief   This function checks if the adapter and device are reporting the same maxLBA to the host.
    //!          SATA drives behind USB adapters or SAS HBAs work through a SATL (translator) that may not be in sync after changing the maxLBA.
    //!          This checks if these things are in sync or not and reports the status. If not in sync, a powercycle or reboot is necessary for the adapter
    //!          to update what it knows about the attached device.
    //
    //  Entry:
    //!   \param[in]  device file descriptor
    //!
    //  Exit:
    //!   \return true = in sync, false = out of sync. Recommend rebooting/power cycling to resync the adapters knowledge of the device.
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API bool is_Max_LBA_In_Sync_With_Adapter_Or_Driver(tDevice* device);

#if defined (__cplusplus)
}
#endif
