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
// \file cdl.h
// \brief This file defines the functions related to displaying and changing CDL settings

#pragma once

#include "operations_Common.h"

#if defined (__cplusplus)
extern "C"
{
#endif   

#define MAX_CDL_READ_DESCRIPTOR         7
#define MAX_CDL_WRITE_DESCRIPTOR        7

    typedef enum _eCDLFeatureSet
    {
        CDL_FEATURE_UNKNOWN = -1,
        CDL_FEATURE_ENABLE = 0,
        CDL_FEATURE_DISABLE = 1,
    } eCDLFeatureSet;

    typedef enum _eCDLSettingsOutMode
    {
        CDL_SETTINGS_OUTPUT_RAW,
        CDL_SETTINGS_OUTPUT_JSON,
    } eCDLSettingsOutMode;

    typedef enum _eCDLPolicyType
    {
        CDL_POLICY_TYPE_INACTIVE_TIME,
        CDL_POLICY_TYPE_ACTIVE_TIME,
        CDL_POLICY_TYPE_COMMAND_DURATION_GUIDELINE,
    } eCDLPolicyType;

    typedef struct _tCDLDescriptor
    {
        uint8_t inactiveTimePolicy;
        uint8_t activeTimePolicy;
        uint8_t commandDurationGuidelinePolicy;
        uint32_t activeTime;
        uint32_t inactiveTime;
        uint32_t commandDurationGuideline;
    } tCDLDescriptor;

    typedef struct _tCDLSettings
    {
        bool isSupported;
        bool isEnabled;
        bool isCommandDurationGuidelineSupported;
        uint32_t minimumTimeLimit;
        uint32_t maximumTimeLimit;
        uint8_t performanceVsCommandDurationGuideline;
        tCDLDescriptor cdlReadDescriptor[MAX_CDL_READ_DESCRIPTOR];
        tCDLDescriptor cdlWriteDescriptor[MAX_CDL_WRITE_DESCRIPTOR];
    } tCDLSettings;

    OPENSEA_OPERATIONS_API eReturnValues enable_Disable_CDL_Feature(tDevice *device, eCDLFeatureSet countField);
    OPENSEA_OPERATIONS_API eReturnValues get_CDL_Settings(tDevice *device, tCDLSettings *cdlSettings);
    OPENSEA_OPERATIONS_API eReturnValues print_CDL_Settings(tDevice *device, tCDLSettings *cdlSettings);
    OPENSEA_OPERATIONS_API eReturnValues config_CDL_Settings(tDevice *device, tCDLSettings *cdlSettings);
    OPENSEA_OPERATIONS_API eReturnValues is_Valid_Config_CDL_Settings(tDevice *device, tCDLSettings *cdlSettings);

#if defined (__cplusplus)
}
#endif
