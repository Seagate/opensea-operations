// SPDX-License-Identifier: MPL-2.0
//
// Do NOT modify or remove this copyright and license
//
// Copyright (c) 2012-2024 Seagate Technology LLC and/or its Affiliates, All Rights Reserved
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
    OPENSEA_OPERATIONS_API eReturnValues ata_Get_Native_Max_LBA(tDevice *device, uint64_t *nativeMaxLBA);

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
    OPENSEA_OPERATIONS_API eReturnValues get_Native_Max_LBA(tDevice *device, uint64_t *nativeMaxLBA);

    //-----------------------------------------------------------------------------
    //
    //  scsi_Set_Max_LBA_2( tDevice * device )
    //
    //! \brief   Sets the maxLBA of the selected device using SCSI methods
    //
    //  Entry:
    //!   \param[in]  device file descriptor
    //!   \param[in]  newMaxLBA the new maxLBA you wish to set
    //!   \param[in]  reset if set to 1 (or higher), this will reset to the native max, otherwise it will use the newMaxLBA param to set the maxLBA
    //!   \param[in]  changeId if set to 1 (or higher), this will change model number if available in AMAC feature set
    //!
    //  Exit:
    //!   \return SUCCESS = good, !SUCCESS something went wrong see error codes
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API eReturnValues scsi_Set_Max_LBA_2(tDevice* device, uint64_t newMaxLBA, bool reset, bool changeId);

    // deprecated wrapper for scsi_Set_Max_LBA_2
    // TODO: remove me when next major version bump
    OPENSEA_OPERATIONS_API eReturnValues scsi_Set_Max_LBA(tDevice* device, uint64_t newMaxLBA, bool reset);

    //-----------------------------------------------------------------------------
    //
    //  ata_Set_Max_LBA_2( tDevice * device )
    //
    //! \brief   Sets the maxLBA of the selected device using HPA or AMA feature sets
    //
    //  Entry:
    //!   \param[in]  device file descriptor
    //!   \param[in]  newMaxLBA the new maxLBA you wish to set
    //!   \param[in]  reset if set to 1 (or higher), this will reset to the native max, otherwise it will use the newMaxLBA param to set the maxLBA
    //!   \param[in]  changeId if set to 1 (or higher), this will change model number if available in AMAC feature set
    //!
    //  Exit:
    //!   \return SUCCESS = good, !SUCCESS something went wrong see error codes
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API eReturnValues ata_Set_Max_LBA_2(tDevice* device, uint64_t newMaxLBA, bool reset, bool changeId);

    // deprecated wrapper for ata_Set_Max_LBA_2
    // TODO: remove me when next major version bump
    OPENSEA_OPERATIONS_API eReturnValues ata_Set_Max_LBA(tDevice* device, uint64_t newMaxLBA, bool reset);

    //-----------------------------------------------------------------------------
    //
    //  set_Max_LBA_2( tDevice * device )
    //
    //! \brief   Sets the maxLBA of the selected device. This will work with new and old methods.
    //!          If ATA, we have only implemented the legacy method for 48bit drives
    //
    //  Entry:
    //!   \param[in]  device file descriptor
    //!   \param[in]  newMaxLBA the new maxLBA you wish to set
    //!   \param[in]  reset if set to 1 (or higher), this will reset to the native max, otherwise it will use the newMaxLBA param to set the maxLBA
    //!   \param[in]  changeId if set to 1 (or higher), this will change model number if available in AMAC feature set
    //!
    //  Exit:
    //!   \return SUCCESS = good, !SUCCESS something went wrong see error codes
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API eReturnValues set_Max_LBA_2(tDevice* device, uint64_t newMaxLBA, bool reset, bool changeId);

    // deprecated wrapper for set_Max_LBA_2
    // TODO: remove me when next major version bump
    OPENSEA_OPERATIONS_API eReturnValues set_Max_LBA(tDevice *device, uint64_t newMaxLBA, bool reset);

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
    OPENSEA_OPERATIONS_API eReturnValues restore_Max_LBA_For_Erase(tDevice* device);

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
    //!   \param[in]  device = file descriptor
    //!   \param[in]  issueReset = attempt to issue a low-level reset to synchronize scsi and ata reported info. This reset will not work in all scenarios. if set to false, this will be called recursively to try the reset for you as needed.
    //!
    //  Exit:
    //!   \return true = in sync, false = out of sync. Recommend rebooting/power cycling to resync the adapters knowledge of the device.
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API bool is_Max_LBA_In_Sync_With_Adapter_Or_Driver(tDevice* device, bool issueReset);

    typedef struct _capacityModelDescriptor
    {
        uint64_t capacityMaxAddress;
        char modelNumber[MODEL_NUM_LEN + 1];//Null terminated
    }capacityModelDescriptor, *ptrDriveCapacityModelDescriptor;

    typedef struct _capacityModelNumberMapping
    {
        uint32_t numberOfDescriptors;
        capacityModelDescriptor descriptor[1];//NOTE: This must be allocated based on how many descriptors are actually available! ex: malloc(sizeof(capacityModelNumberMapping) + (get_capacityModelDescriptor_Count() * sizeof(capacityModelDescriptor)));
    }capacityModelNumberMapping, *ptrcapacityModelNumberMapping;

    static M_INLINE void safe_free_cap_mn_map(capacityModelNumberMapping **mnmap)
    {
        safe_Free(M_REINTERPRET_CAST(void**, mnmap));
    }

    //-----------------------------------------------------------------------------
    //
    //  is_Change_Identify_String_Supported(tDevice *device)
    //
    //! \brief   Description:  Checks if the device supports Change ID Strings.
    //
    //  Entry:
    //!   \param[in] device = file descriptor
    //!
    //  Exit:
    //!   \return true = changing sector size supported, false = not supported
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API bool is_Change_Identify_String_Supported(tDevice* device);

    //-----------------------------------------------------------------------------
    //
    //  get_Capacity_Model_Number_Mapping(tDevice* device)
    //
    //! \brief   Description:  This function fills in Capacity/Product Mapping
    //
    //  Entry:
    //!   \param[in] device = file descriptor
    //!
    //  Exit:
    //!   \return SUCCESS = pointer to the struct to fill in with Capacity/Product Mapping, FAILURE = M_NULLPTR.
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API ptrcapacityModelNumberMapping get_Capacity_Model_Number_Mapping(tDevice* device);

    //-----------------------------------------------------------------------------
    //
    //  delete_Capacity_Model_Number_Mapping(ptrcapacityModelNumberMapping capModelMapping)
    //
    //! \brief   Description:  This function free allocated Capacity/Product Mapping
    //
    //  Entry:
    //!   \param[in] capModelMapping = pointer to the struct to fill in with Capacity/Product Mapping
    //!
    //  Exit:
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API void delete_Capacity_Model_Number_Mapping(ptrcapacityModelNumberMapping capModelMapping);

    OPENSEA_OPERATIONS_API void print_Capacity_Model_Number_Mapping(ptrcapacityModelNumberMapping capModelMapping);

#if defined (__cplusplus)
}
#endif
