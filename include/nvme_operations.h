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
#pragma once

#include "operations_Common.h"
#include "nvme_helper.h"

#if defined (__cplusplus)
extern "C"
{
#endif


    //-----------------------------------------------------------------------------
    //
    //  nvme_Print_ERROR_Log_Page
    //
    //! \brief   Description:  Function to send Get Error Information Log Page NVMe command to a device
    //
    //  Entry:
    //!   \param[in] device = pointer to tDevice structure
    //!   \param[in] numOfErrToPrint = set to 0 to print all, otherwise set a reasonable value (e.g. 32) 
    //!                                [NVMe Identify data shows how many entries are present]
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API eReturnValues nvme_Print_ERROR_Log_Page(tDevice *device, uint64_t numOfErrToPrint);

    //-----------------------------------------------------------------------------
    //
    //  nvme_Print_FWSLOTS_Log_Page
    //
    //! \brief   Description:  Function to print Firmware Slots Log Page NVMe command to a device
    //
    //  Entry:
    //!   \param[in] device = pointer to tDevice structure
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API eReturnValues nvme_Print_FWSLOTS_Log_Page(tDevice *device);

    //-----------------------------------------------------------------------------
    //
    //  nvme_Print_CmdSptEfft_Log_Page
    //
    //! \brief   Description:  Function to print Commands Supported and Effects Log Page NVMe command to a device
    //
    //  Entry:
    //!   \param[in] device = pointer to tDevice structure
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API eReturnValues nvme_Print_CmdSptEfft_Log_Page(tDevice *device);

    OPENSEA_OPERATIONS_API void show_effects_log_human(uint32_t effect);

    //-----------------------------------------------------------------------------
    //
    //  nvme_Print_DevSelfTest_Log_Page
    //
    //! \brief   Description:  Function to print Device Self-test Log Page NVMe command to a device
    //
    //  Entry:
    //!   \param[in] device = pointer to tDevice structure
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API eReturnValues nvme_Print_DevSelfTest_Log_Page(tDevice *device);

    //-----------------------------------------------------------------------------
    //
    //  nvme_Print_Feature_Identifiers_Help
    //
    //! \brief   Description:  Function to print Help info for Feature Identifiers 
    //                         
    //  Entry:
    //!
    //  Exit:
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API void nvme_Print_Feature_Identifiers_Help(void);

    //-----------------------------------------------------------------------------
    //
    //  nvme_Print_Feature_Identifiers_Help
    //
    //! \brief   Description:  Function to print ALL the Feature Identifiers 
    //                         
    //  Entry:
    //!   \param[in] device = pointer to tDevice structure
    //!   \param[in] selectType eNvmeFeaturesSelectValue, i.e. current, default, saved etc. 
    //!   \param[in] listOnlySupportedFeatures = !!NOT USED!! list only supported features. 
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API eReturnValues nvme_Print_All_Feature_Identifiers(tDevice *device, eNvmeFeaturesSelectValue selectType, bool listOnlySupportedFeatures);


    //-----------------------------------------------------------------------------
    //
    //  nvme_Print_Feature_Details
    //
    //! \brief   Description:  Function to print ALL the Feature Identifiers 
    //                         
    //  Entry:
    //!   \param[in] device = pointer to tDevice structure
    //!   \param[in] featureID Feature Identifier 
    //!   \param[in] selectType eNvmeFeaturesSelectValue, i.e. current, default, saved etc. 
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API eReturnValues nvme_Print_Feature_Details(tDevice *device, uint8_t featureID, eNvmeFeaturesSelectValue selectType);

    // \fn print_Nvme_Ctrl_Regs(tDevice * device)
    // \brief Prints the controller registers. 
    // \param[in] device struture
    // \return SUCCESS - pass, !SUCCESS fail or something went wrong
    OPENSEA_OPERATIONS_API eReturnValues print_Nvme_Ctrl_Regs(tDevice * device);

    //-----------------------------------------------------------------------------
    //
    //  nvme_Get_Log_Size
    //
    //! \brief   Description:  Function to get the size for GetLog Page command by a utility. 
    //!                        NOTE: Some variable length logs will not return a size at this time. Vendor unique logs are not supported i nthis function
    //                         
    //  Entry:
    //!   \param[in] device = pointer to the device structure. This is needed in order to calculate some log sizes that are not fixed
    //!   \param[in] logPageId = Log Page Identifier. 
    //!   \param[out] logSize = size of the Log to return 
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API eReturnValues nvme_Get_Log_Size(tDevice* device, uint8_t logPageId, uint64_t * logSize);

#if defined (__cplusplus)
}
#endif
