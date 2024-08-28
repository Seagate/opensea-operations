// SPDX-License-Identifier: MPL-2.0
//
// Do NOT modify or remove this copyright and license
//
// Copyright (c) 2023-2024 Seagate Technology LLC and/or its Affiliates, All Rights Reserved
//
// This software is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
//
// ******************************************************************************************
// 
// \file ata_device_config_overlay.c   ATA Device configuration overlay support (DCO)

#pragma once

#if defined (__cplusplus)
extern "C"
{
#endif //__cplusplus

#include "operations_Common.h"

    //-----------------------------------------------------------------------------
    //
    //  bool is_DCO_Supported(tDevice* device, bool* dmaSupport)
    //
    //! \brief   Description:  Check if the drive supports the device configuration overlay (DCO) feature
    //
    //  Entry:
    //!   \param[in] device = file descriptor
    //!   \param[out] dmaSupport = DMA mode DCO commands are supported by the device. This is optional. M_NULLPTR can be passed for this parameter.
    //!
    //  Exit:
    //!   \return true = supported, false = not supported
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API bool is_DCO_Supported(tDevice* device, bool* dmaSupport);

    //-----------------------------------------------------------------------------
    //
    //  eReturnValues dco_Restore(tDevice* device)
    //
    //! \brief   Description:  Issue the DCO restore command. NOTE: This will only succeed if no HPA is established and not DCO frozen
    //
    //  Entry:
    //!   \param[in] device = file descriptor
    //!
    //  Exit:
    //!   \return SUCCESS = successfully restored DCO features, FROZEN = DCO is frozen and cannot be restored, FAILURE = Error issuing command or HPA is established
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API eReturnValues dco_Restore(tDevice* device);

    //-----------------------------------------------------------------------------
    //
    //  eReturnValues dco_Freeze_Lock(tDevice* device)
    //
    //! \brief   Description:  Issue the DCO freeze lock command to block other DCO commands from processing
    //
    //  Entry:
    //!   \param[in] device = file descriptor
    //!
    //  Exit:
    //!   \return SUCCESS = successfully froze DCO feature, FAILURE/ABORTED = command aborted by the device for some unknown reason.
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API eReturnValues dco_Freeze_Lock(tDevice* device);

    //NOTE: This structure only supports the few words defined in the ACS and ACS-2 specs.
    //      Since many were reserved, those are not supported in here at this time.
    //      Vendor unique fields are also not supported at this time.
    //      DCO was removed in ACS-3 so there is not much reason for expanding this.-TJE
    typedef struct _dcoData
    {
        uint16_t revision;//drive reported value. Revision 1 from ATA/ATAPI-6. Revision 2 from all later specs. ignored for the set
        struct mwdmaBits
        {
            bool mwdma2;
            bool mwdma1;
            bool mwdma0;
        }mwdma;
        struct udmaBits
        {
            bool udma6;
            bool udma5;
            bool udma4;
            bool udma3;
            bool udma2;
            bool udma1;
            bool udma0;
        }udma;
        uint64_t maxLBA;
        struct dcoFeat1Bits
        {
            bool writeReadVerify;//WRV
            bool smartConveyanceSelfTest;
            bool smartSelectiveSelfTest;
            bool forceUnitAccess;//FUA
            bool timeLimitedCommands;//TLC features
            bool streaming;
            bool fourtyEightBitAddress;
            bool hostProtectedArea;//HPA
            bool automaticAccousticManagement;//AAM
            bool readWriteDMAQueued;
            bool powerUpInStandby;//PUIS
            bool ATAsecurity;
            bool smartErrorLog;
            bool smartSelfTest;
            bool smartFeature;
        }feat1;
        struct sataFeatBits
        {
            bool softwareSettingsPreservation;
            bool asynchronousNotification;
            bool interfacePowerManagement;
            bool nonZeroBufferOffsets;
            bool ncqFeature;//NCQ
        }sataFeat;
        struct dcoFeat2Bits
        {
            bool nvCache;//NVC
            bool nvCachePowerManagement;//NVCPM
            bool writeUncorrectable;//WUE
            bool trustedComputing;//TCG
            bool freeFall;//FF
            bool dataSetManagement;//DSM
            bool extendedPowerConditions;//EPC
        }feat2;
        bool validChecksum;//ident only. Recalculated for set
    }dcoData, *ptrDcoData;

    //-----------------------------------------------------------------------------
    //
    //  eReturnValues dco_Identify(tDevice* device, ptrDcoData data)
    //
    //! \brief   Description:  Issue DCO identify and populate the output data structure. The output data indicated which features can be changed/disabled/blocked
    //
    //  Entry:
    //!   \param[in] device = file descriptor
    //!   \param[out] data = pointer to dcoData structure (required). Indicated what can be changed on the device
    //!
    //  Exit:
    //!   \return SUCCESS = successfully identified DCO chagable fields, FROZEN = device is DCO frozen FAILURE/ABORTED = command aborted by the device. Possible HPA feature error due to HPA established
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API eReturnValues dco_Identify(tDevice* device, ptrDcoData data);

    //-----------------------------------------------------------------------------
    //
    //  void show_DCO_Identify_Data(ptrDcoData data)
    //
    //! \brief   Description: Display the DCO data structure. For use after DCO identify, but could be used to indicate changes before a set as well.
    //
    //  Entry:
    //!   \param[in] data = pointer to dcoData structure (required). Indicated what can be changed on the device
    //!
    //  Exit:
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API void show_DCO_Identify_Data(ptrDcoData data);

    //-----------------------------------------------------------------------------
    //
    //  eReturnValues dco_Set(tDevice* device, ptrDcoData data)
    //
    //! \brief   Description: Takes the DCO data structure and turns and fields set to "false" to 0's in the data to disable the feature. 
    //!          Anything left as "true" is left as-is when sent to the device.
    //!          Can be used to change the Maximum LBA to a different value.
    //!          Recommend using doc_Identify to collect data, modify that structure, then call this to make changes.
    //!          If an HPA area is established, it must be removed before using this command otherwise it will fail per the ATA DCO feature definitions.
    //
    //  Entry:
    //!   \param[in] device = file descriptor
    //!   \param[in] data = pointer to dcoData structure (required). Indicated what commands/features are allowed (true) and which are not (false)
    //!
    //  Exit:
    //    \return SUCCESS = successfully changed features with DCO, FROZEN = DCO is frozen and cannot be changed, ABORTED/FAILURE = command aborted. Possible HPA established blocking DCO command from completing.
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API eReturnValues dco_Set(tDevice* device, ptrDcoData data);


#if defined (__cplusplus)
}
#endif //__cplusplus
