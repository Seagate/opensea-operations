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

#if defined(__cplusplus)
extern "C"
{
#endif

#define MAX_CDL_READ_DESCRIPTOR                7
#define MAX_CDL_WRITE_DESCRIPTOR               7
#define MAX_CDL_T2A_DESCRIPTOR                 7
#define MAX_CDL_T2B_DESCRIPTOR                 7
#define SUPPORTED_POLICY_STRING_LENGTH         80

#define COMBINE_CDL_FEATURE_VERSIONS_(x, y, z) #x "." #y "." #z
#define COMBINE_CDL_FEATURE_VERSIONS(x, y, z)  COMBINE_CDL_FEATURE_VERSIONS_(x, y, z)
#define CDL_FEATURE_MAJOR_VERSION              2
#define CDL_FEATURE_MINOR_VERSION              0
#define CDL_FEATURE_PATCH_VERSION              0
#define CDL_FEATURE_VERSION                                                                                            \
    COMBINE_CDL_FEATURE_VERSIONS(CDL_FEATURE_MAJOR_VERSION, CDL_FEATURE_MINOR_VERSION, CDL_FEATURE_PATCH_VERSION)

    //! \enum eCDLFeatureSet
    //! \brief Enum representing CDL Feature enable or disable state.
    //!
    //! This enum defines the different state (enable/disable) of CDL Feature.
    M_DECLARE_ENUM(eCDLFeatureSet,
                   /*!< CDL Feature Unknown state. */
                   CDL_FEATURE_UNKNOWN = -1,
                   /*!< CDL Feature Disable. */
                   CDL_FEATURE_DISABLE = 0,
                   /*!< CDL Feature Enable. */
                   CDL_FEATURE_ENABLE = 1);

    //! \enum eCDLSettingsOutMode
    //! \brief Enum representing output mode for CDL Settings.
    //!
    //! This enum defines the different mode of output for CDL Settings.
    M_DECLARE_ENUM(eCDLSettingsOutMode,
                   /*!< CDL Settings Output Raw. */
                   CDL_SETTINGS_OUTPUT_RAW = 0,
                   /*!< CDL Settings Output JSON. */
                   CDL_SETTINGS_OUTPUT_JSON = 1);

    //! \enum eCDLPolicyType
    //! \brief Enum representing CDL Policy.
    //!
    //! This enum defines the different CDL Policies.
    M_DECLARE_ENUM(
        eCDLPolicyType,
        /*!< CDL Policy Inactive Time. */
        CDL_POLICY_TYPE_INACTIVE_TIME = 0,
        /*!< CDL Policy Active Time. */
        CDL_POLICY_TYPE_ACTIVE_TIME = 1,
        /*!< CDL Policy Total Time. */
        CDL_POLICY_TYPE_TOTAL_TIME = 2,
        /*!< CDL Policy Command Duration Guideline. This is represetation of Total Time policy for SCSI drives*/
        CDL_POLICY_TYPE_COMMAND_DURATION_GUIDELINE = 3);

    //! \enum eCDLTimeFieldUnitType
    //! \brief Enum representing units for time fields.
    //!
    //! This enum defines the different of units for CDL Time fields.
    M_DECLARE_ENUM(eCDLTimeFieldUnitType,
                   /*!< CDL Time Field Unit in microseconds. */
                   CDL_TIME_FIELD_UNIT_TYPE_MICROSECONDS = 0,
                   /*!< CDL Time Field Unit in milliseconds. */
                   CDL_TIME_FIELD_UNIT_TYPE_MILLISECONDS = 1,
                   /*!< CDL Time Field Unit in seconds. */
                   CDL_TIME_FIELD_UNIT_TYPE_SECONDS = 2,
                   /*!< CDL Time Field Unit in 500 nanoseconds. */
                   CDL_TIME_FIELD_UNIT_TYPE_500_NANOSECONDS = 3,
                   /*!< CDL Time Field Unit in 10 milliseconds. */
                   CDL_TIME_FIELD_UNIT_TYPE_10_MILLISECONDS = 4,
                   /*!< CDL Time Field Unit in 500 milliseconds. */
                   CDL_TIME_FIELD_UNIT_TYPE_500_MILLISECONDS = 5,
                   /*!< CDL Time Field No Unit. */
                   CDL_TIME_FIELD_UNIT_TYPE_NO_VALUE = 6,
                   /*!< CDL Time Field Reserved Unit. */
                   CDL_TIME_FIELD_UNIT_TYPE_RESERVED = 7);

    typedef struct _tCDLDescriptor
    {
        eCDLTimeFieldUnitType timeFieldUnitType;
        uint8_t               inactiveTimePolicy;
        uint8_t               activeTimePolicy;
        union
        {
            uint8_t totalTimePolicy;
            uint8_t CommandDurationGuidelinePolicy;
        };
        uint32_t activeTime;
        uint32_t inactiveTime;
        union
        {
            uint32_t totalTime;
            uint32_t commandDurationGuideline;
        };
    } tCDLDescriptor;

    typedef struct _tATACDLSettings
    {
        bool           isCommandDurationGuidelineSupported;
        uint32_t       minimumTimeLimit;
        uint32_t       maximumTimeLimit;
        uint8_t        performanceVsCommandCompletion;
        uint16_t       inactiveTimePolicySupportedDescriptor;
        uint16_t       activeTimePolicySupportedDescriptor;
        uint16_t       totalTimePolicySupportedDescriptor;
        tCDLDescriptor cdlReadDescriptor[MAX_CDL_READ_DESCRIPTOR];
        tCDLDescriptor cdlWriteDescriptor[MAX_CDL_WRITE_DESCRIPTOR];
    } tATACDLSettings;

    typedef struct _tSCSICDLSettings
    {
        uint8_t        performanceVsCommandDurationGuidelines;
        tCDLDescriptor cdlT2ADescriptor[MAX_CDL_T2A_DESCRIPTOR];
        tCDLDescriptor cdlT2BDescriptor[MAX_CDL_T2B_DESCRIPTOR];
    } tSCSICDLSettings;

    typedef struct _tCDLSettings
    {
        bool isSupported;
        bool isEnabled;
        union
        {
            tATACDLSettings  ataCDLSettings;
            tSCSICDLSettings scsiCDLSettings;
        };
    } tCDLSettings;

    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1)
    OPENSEA_OPERATIONS_API eReturnValues enable_Disable_CDL_Feature(tDevice* device, eCDLFeatureSet countField);

    M_NONNULL_PARAM_LIST(1, 2)
    M_PARAM_RO(1)
    M_PARAM_WO(2)
    OPENSEA_OPERATIONS_API eReturnValues get_CDL_Settings(tDevice* device, tCDLSettings* cdlSettings);

    M_NONNULL_PARAM_LIST(1, 2)
    M_PARAM_RO(1)
    M_PARAM_RO(2)
    OPENSEA_OPERATIONS_API eReturnValues print_CDL_Settings(tDevice* device, tCDLSettings* cdlSettings);

    M_NONNULL_PARAM_LIST(1, 2)
    M_PARAM_RO(1)
    M_PARAM_RO(2)
    OPENSEA_OPERATIONS_API eReturnValues config_CDL_Settings(tDevice* device, tCDLSettings* cdlSettings);

    M_NONNULL_PARAM_LIST(1, 2)
    M_PARAM_RO(1)
    M_PARAM_RO(2)
    OPENSEA_OPERATIONS_API eReturnValues is_Valid_Config_CDL_Settings(tDevice* device, tCDLSettings* cdlSettings);

    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1)
    OPENSEA_OPERATIONS_API bool is_Total_Time_Policy_Type_Supported(tCDLSettings* cdlSettings);

    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1)
    OPENSEA_OPERATIONS_API bool is_Performance_Versus_Command_Completion_Supported(tCDLSettings* cdlSettings);

    M_NONNULL_PARAM_LIST(4)
    M_PARAM_WO(4)
    OPENSEA_OPERATIONS_API void get_Supported_Policy_String(eDriveType     driveType,
                                                            eCDLPolicyType policyType,
                                                            uint16_t       policySupportedDescriptor,
                                                            char*          policyString);

    OPENSEA_OPERATIONS_API uint32_t convert_CDL_TimeField_To_Microseconds(eCDLTimeFieldUnitType unitType,
                                                                          uint32_t              value);
#if defined(__cplusplus)
}
#endif
