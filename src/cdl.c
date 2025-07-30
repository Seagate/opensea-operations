// SPDX-License-Identifier: MPL-2.0
//
// Do NOT modify or remove this copyright and license
//
// Copyright (c) 2012-2025 Seagate Technology LLC and/or its Affiliates, All Rights Reserved
//
// This software is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
//
// ******************************************************************************************
//
// \file cdl.c
// \brief This file defines the functions related to displaying and changing CDL settings

#include "cdl.h"
#include "common_types.h"
#include "io_utils.h"
#include "logs.h"
#include "string_utils.h"

#define MAX_CDL_PERFORMANCE_VS_CMD_COMPLETION_STATUS_STRING_LENGTH 16
#define MAX_POLICY_STRING_LENGHT                                   130
#define CDL_DESCRIPTOR_LENGTH                                      32
#define CDL_READ_DESCRIPTOR_OFFSET                                 64
#define CDL_WRITE_DESCRIPTOR_OFFSET                                288
#define CDL_T2A_DESCRIPTOR_OFFSET                                  8
#define CDL_T2B_DESCRIPTOR_OFFSET                                  8

eReturnValues enable_Disable_CDL_Feature(tDevice* device, eCDLFeatureSet countField)
{
    eReturnValues ret = NOT_SUPPORTED;

    if (countField != CDL_FEATURE_ENABLE && countField != CDL_FEATURE_DISABLE)
    {
        return BAD_PARAMETER;
    }

    if (device->drive_info.drive_type == ATA_DRIVE)
    {
        ret = ata_Set_Features(device, SF_CDL_FEATURE, C_CAST(uint8_t, countField), 0, 0, 0);
    }

    return ret;
}

static eReturnValues get_ATA_CDL_Settings(tDevice* device, tCDLSettings* cdlSettings)
{
    eReturnValues ret = NOT_SUPPORTED;

    if (!cdlSettings)
    {
        return BAD_PARAMETER;
    }

    // read the logaddress 0x18, logpage 0x00
    // Note - read this page at begining, as this will tell if the feature is supported or not
    uint32_t logSize = 0;
    ret              = get_ATA_Log_Size(device, ATA_LOG_COMMAND_DURATION_LIMITS_LOG, &logSize, true, false);
    if (ret == SUCCESS)
    {
        uint8_t* logBuffer = safe_calloc_aligned(logSize, sizeof(uint8_t), device->os_info.minimumAlignment);
        if (!logBuffer)
        {
            return MEMORY_FAILURE;
        }

        ret = get_ATA_Log(device, ATA_LOG_COMMAND_DURATION_LIMITS_LOG, M_NULLPTR, M_NULLPTR, true, false, true,
                          logBuffer, logSize, M_NULLPTR, LEGACY_DRIVE_SEC_SIZE, 0);
        if (SUCCESS == ret)
        {
            cdlSettings->ataCDLSettings.performanceVsCommandCompletion = M_Nibble0(logBuffer[0]);

            uint8_t* cdlReadDescriptorBuffer = logBuffer + CDL_READ_DESCRIPTOR_OFFSET;
            for (uint8_t descriptorIndex = 0; descriptorIndex < MAX_CDL_READ_DESCRIPTOR; descriptorIndex++)
            {
                cdlSettings->ataCDLSettings.cdlReadDescriptor[descriptorIndex].timeFieldUnitType =
                    CDL_TIME_FIELD_UNIT_TYPE_MICROSECONDS;
                uint32_t commandLimitDescriptor =
                    M_BytesTo4ByteValue(cdlReadDescriptorBuffer[(descriptorIndex * CDL_DESCRIPTOR_LENGTH) + 3],
                                        cdlReadDescriptorBuffer[(descriptorIndex * CDL_DESCRIPTOR_LENGTH) + 2],
                                        cdlReadDescriptorBuffer[(descriptorIndex * CDL_DESCRIPTOR_LENGTH) + 1],
                                        cdlReadDescriptorBuffer[(descriptorIndex * CDL_DESCRIPTOR_LENGTH) + 0]);
                cdlSettings->ataCDLSettings.cdlReadDescriptor[descriptorIndex].totalTimePolicy =
                    M_Nibble0(M_Byte0(commandLimitDescriptor));
                cdlSettings->ataCDLSettings.cdlReadDescriptor[descriptorIndex].activeTimePolicy =
                    M_Nibble1(M_Byte0(commandLimitDescriptor));
                cdlSettings->ataCDLSettings.cdlReadDescriptor[descriptorIndex].inactiveTimePolicy =
                    M_Nibble0(M_Byte1(commandLimitDescriptor));
                cdlSettings->ataCDLSettings.cdlReadDescriptor[descriptorIndex].activeTime =
                    M_BytesTo4ByteValue(cdlReadDescriptorBuffer[(descriptorIndex * CDL_DESCRIPTOR_LENGTH) + 7],
                                        cdlReadDescriptorBuffer[(descriptorIndex * CDL_DESCRIPTOR_LENGTH) + 6],
                                        cdlReadDescriptorBuffer[(descriptorIndex * CDL_DESCRIPTOR_LENGTH) + 5],
                                        cdlReadDescriptorBuffer[(descriptorIndex * CDL_DESCRIPTOR_LENGTH) + 4]);
                cdlSettings->ataCDLSettings.cdlReadDescriptor[descriptorIndex].inactiveTime =
                    M_BytesTo4ByteValue(cdlReadDescriptorBuffer[(descriptorIndex * CDL_DESCRIPTOR_LENGTH) + 11],
                                        cdlReadDescriptorBuffer[(descriptorIndex * CDL_DESCRIPTOR_LENGTH) + 10],
                                        cdlReadDescriptorBuffer[(descriptorIndex * CDL_DESCRIPTOR_LENGTH) + 9],
                                        cdlReadDescriptorBuffer[(descriptorIndex * CDL_DESCRIPTOR_LENGTH) + 8]);
                cdlSettings->ataCDLSettings.cdlReadDescriptor[descriptorIndex].totalTime =
                    M_BytesTo4ByteValue(cdlReadDescriptorBuffer[(descriptorIndex * CDL_DESCRIPTOR_LENGTH) + 19],
                                        cdlReadDescriptorBuffer[(descriptorIndex * CDL_DESCRIPTOR_LENGTH) + 18],
                                        cdlReadDescriptorBuffer[(descriptorIndex * CDL_DESCRIPTOR_LENGTH) + 17],
                                        cdlReadDescriptorBuffer[(descriptorIndex * CDL_DESCRIPTOR_LENGTH) + 16]);
            }

            uint8_t* cdlWriteDescriptorBuffer = logBuffer + CDL_WRITE_DESCRIPTOR_OFFSET;
            for (uint8_t descriptorIndex = 0; descriptorIndex < MAX_CDL_WRITE_DESCRIPTOR; descriptorIndex++)
            {
                cdlSettings->ataCDLSettings.cdlWriteDescriptor[descriptorIndex].timeFieldUnitType =
                    CDL_TIME_FIELD_UNIT_TYPE_MICROSECONDS;
                uint32_t commandLimitDescriptor =
                    M_BytesTo4ByteValue(cdlWriteDescriptorBuffer[(descriptorIndex * CDL_DESCRIPTOR_LENGTH) + 3],
                                        cdlWriteDescriptorBuffer[(descriptorIndex * CDL_DESCRIPTOR_LENGTH) + 2],
                                        cdlWriteDescriptorBuffer[(descriptorIndex * CDL_DESCRIPTOR_LENGTH) + 1],
                                        cdlWriteDescriptorBuffer[(descriptorIndex * CDL_DESCRIPTOR_LENGTH) + 0]);
                cdlSettings->ataCDLSettings.cdlWriteDescriptor[descriptorIndex].totalTimePolicy =
                    M_Nibble0(M_Byte0(commandLimitDescriptor));
                cdlSettings->ataCDLSettings.cdlWriteDescriptor[descriptorIndex].activeTimePolicy =
                    M_Nibble1(M_Byte0(commandLimitDescriptor));
                cdlSettings->ataCDLSettings.cdlWriteDescriptor[descriptorIndex].inactiveTimePolicy =
                    M_Nibble0(M_Byte1(commandLimitDescriptor));
                cdlSettings->ataCDLSettings.cdlWriteDescriptor[descriptorIndex].activeTime =
                    M_BytesTo4ByteValue(cdlWriteDescriptorBuffer[(descriptorIndex * CDL_DESCRIPTOR_LENGTH) + 7],
                                        cdlWriteDescriptorBuffer[(descriptorIndex * CDL_DESCRIPTOR_LENGTH) + 6],
                                        cdlWriteDescriptorBuffer[(descriptorIndex * CDL_DESCRIPTOR_LENGTH) + 5],
                                        cdlWriteDescriptorBuffer[(descriptorIndex * CDL_DESCRIPTOR_LENGTH) + 4]);
                cdlSettings->ataCDLSettings.cdlWriteDescriptor[descriptorIndex].inactiveTime =
                    M_BytesTo4ByteValue(cdlWriteDescriptorBuffer[(descriptorIndex * CDL_DESCRIPTOR_LENGTH) + 11],
                                        cdlWriteDescriptorBuffer[(descriptorIndex * CDL_DESCRIPTOR_LENGTH) + 10],
                                        cdlWriteDescriptorBuffer[(descriptorIndex * CDL_DESCRIPTOR_LENGTH) + 9],
                                        cdlWriteDescriptorBuffer[(descriptorIndex * CDL_DESCRIPTOR_LENGTH) + 8]);
                cdlSettings->ataCDLSettings.cdlWriteDescriptor[descriptorIndex].totalTime =
                    M_BytesTo4ByteValue(cdlWriteDescriptorBuffer[(descriptorIndex * CDL_DESCRIPTOR_LENGTH) + 19],
                                        cdlWriteDescriptorBuffer[(descriptorIndex * CDL_DESCRIPTOR_LENGTH) + 18],
                                        cdlWriteDescriptorBuffer[(descriptorIndex * CDL_DESCRIPTOR_LENGTH) + 17],
                                        cdlWriteDescriptorBuffer[(descriptorIndex * CDL_DESCRIPTOR_LENGTH) + 16]);
            }
        }
        else
        {
            safe_free_aligned(&logBuffer);
            return ret;
        }

        uint8_t* temp = C_CAST(
            uint8_t*, safe_realloc_aligned(logBuffer, 0, LEGACY_DRIVE_SEC_SIZE, device->os_info.minimumAlignment));
        if (!temp)
        {
            return MEMORY_FAILURE;
        }
        logBuffer = temp;

        // read the logaddress 0x30, logpage 0x03
        memset(logBuffer, 0, LEGACY_DRIVE_SEC_SIZE);
        ret =
            send_ATA_Read_Log_Ext_Cmd(device, ATA_LOG_IDENTIFY_DEVICE_DATA, 0x03, logBuffer, LEGACY_DRIVE_SEC_SIZE, 0);
        if (SUCCESS == ret)
        {
            uint64_t cdlLimitSupported =
                M_BytesTo8ByteValue(logBuffer[175], logBuffer[174], logBuffer[173], logBuffer[172], logBuffer[171],
                                    logBuffer[170], logBuffer[169], logBuffer[168]);
            if ((cdlLimitSupported & ATA_ID_DATA_QWORD_VALID_BIT))
            {
                if (cdlLimitSupported & BIT0)
                    cdlSettings->isSupported = true;
                if (cdlLimitSupported & BIT1)
                    cdlSettings->ataCDLSettings.isCommandDurationGuidelineSupported = true;
            }
            uint64_t cdlMinimumTimeLimit =
                M_BytesTo8ByteValue(logBuffer[183], logBuffer[182], logBuffer[181], logBuffer[180], logBuffer[179],
                                    logBuffer[178], logBuffer[177], logBuffer[176]);
            if (cdlMinimumTimeLimit & ATA_ID_DATA_QWORD_VALID_BIT)
            {
                cdlSettings->ataCDLSettings.minimumTimeLimit = M_DoubleWord0(cdlMinimumTimeLimit);
            }
            uint64_t cdlMaximumTimeLimit =
                M_BytesTo8ByteValue(logBuffer[191], logBuffer[190], logBuffer[189], logBuffer[188], logBuffer[187],
                                    logBuffer[186], logBuffer[185], logBuffer[184]);
            if (cdlMaximumTimeLimit & ATA_ID_DATA_QWORD_VALID_BIT)
            {
                cdlSettings->ataCDLSettings.maximumTimeLimit = M_DoubleWord0(cdlMaximumTimeLimit);
            }
            uint64_t policySupportedDescriptor =
                M_BytesTo8ByteValue(logBuffer[215], logBuffer[214], logBuffer[213], logBuffer[212], logBuffer[211],
                                    logBuffer[210], logBuffer[209], logBuffer[208]);
            if (policySupportedDescriptor & ATA_ID_DATA_QWORD_VALID_BIT)
            {
                cdlSettings->ataCDLSettings.inactiveTimePolicySupportedDescriptor = M_Word2(policySupportedDescriptor);
                cdlSettings->ataCDLSettings.activeTimePolicySupportedDescriptor   = M_Word1(policySupportedDescriptor);
                cdlSettings->ataCDLSettings.totalTimePolicySupportedDescriptor    = M_Word0(policySupportedDescriptor);
            }
        }
        else
        {
            safe_free_aligned(&logBuffer);
            return ret;
        }

        // read the logaddress 0x30, logpage 0x04
        memset(logBuffer, 0, LEGACY_DRIVE_SEC_SIZE);
        ret =
            send_ATA_Read_Log_Ext_Cmd(device, ATA_LOG_IDENTIFY_DEVICE_DATA, 0x04, logBuffer, LEGACY_DRIVE_SEC_SIZE, 0);
        if (SUCCESS == ret)
        {
            uint64_t currentSettings = M_BytesTo8ByteValue(logBuffer[15], logBuffer[14], logBuffer[13], logBuffer[12],
                                                           logBuffer[11], logBuffer[10], logBuffer[9], logBuffer[8]);
            if ((currentSettings & ATA_ID_DATA_QWORD_VALID_BIT) && (currentSettings & BIT21))
            {
                cdlSettings->isEnabled = true;
            }
        }
        else
        {
            safe_free_aligned(&logBuffer);
            return ret;
        }

        safe_free_aligned(&logBuffer);
    }

    return ret;
}

static eCDLTimeFieldUnitType translate_Value_To_CDL_Unit(uint8_t unitValue)
{
    eCDLTimeFieldUnitType unitType = CDL_TIME_FIELD_UNIT_TYPE_MICROSECONDS;
    switch (unitValue)
    {
    case 0x00:
        unitType = CDL_TIME_FIELD_UNIT_TYPE_NO_VALUE;
        break;

    case 0x06:
        unitType = CDL_TIME_FIELD_UNIT_TYPE_500_NANOSECONDS;
        break;

    case 0x08:
        unitType = CDL_TIME_FIELD_UNIT_TYPE_MICROSECONDS;
        break;

    case 0x0A:
        unitType = CDL_TIME_FIELD_UNIT_TYPE_10_MILLISECONDS;
        break;

    case 0x0E:
        unitType = CDL_TIME_FIELD_UNIT_TYPE_500_MILLISECONDS;
        break;

    default:
        unitType = CDL_TIME_FIELD_UNIT_TYPE_RESERVED;
        break;
    }

    return unitType;
}

static eReturnValues get_SCSI_CDL_Settings(tDevice* device, tCDLSettings* cdlSettings)
{
    eReturnValues ret = NOT_SUPPORTED;

    if (!cdlSettings)
    {
        return BAD_PARAMETER;
    }

    // read T2A mode page
    uint32_t modePageLength = 0;
    if (SUCCESS == get_SCSI_Mode_Page_Size(device, MPC_CURRENT_VALUES, MP_CONTROL, 0x07, &modePageLength))
    {
        uint8_t* modeData =
            C_CAST(uint8_t*, safe_calloc_aligned(modePageLength, sizeof(uint8_t), device->os_info.minimumAlignment));
        if (!modeData)
        {
            return MEMORY_FAILURE;
        }

        // now read all the data
        bool used6ByteCmd = false;
        ret = get_SCSI_Mode_Page(device, MPC_CURRENT_VALUES, MP_CONTROL, 0x07, M_NULLPTR, M_NULLPTR, true, modeData,
                                 modePageLength, M_NULLPTR, &used6ByteCmd);
        if (SUCCESS == ret)
        {
            uint32_t offsetToModePage = UINT32_C(0);
            if (!used6ByteCmd)
            {
                uint16_t blockDescLen = UINT16_C(0);
                get_mode_param_header_10_fields(modeData, modePageLength, M_NULLPTR, M_NULLPTR, M_NULLPTR, M_NULLPTR,
                                                &blockDescLen);
                offsetToModePage = blockDescLen + MODE_PARAMETER_HEADER_10_LEN;
            }
            else
            {
                uint8_t blockDescLen = UINT8_C(0);
                get_mode_param_header_6_fields(modeData, modePageLength, M_NULLPTR, M_NULLPTR, M_NULLPTR,
                                               &blockDescLen);
                offsetToModePage = blockDescLen + MODE_PARAMETER_HEADER_6_LEN;
            }
            // parse the mode page buffer
            cdlSettings->isSupported = true;
            cdlSettings->scsiCDLSettings.performanceVsCommandDurationGuidelines =
                M_Nibble1(modeData[offsetToModePage + 7]);
            uint8_t* cdlT2ADescriptorBuffer = modeData + offsetToModePage + CDL_T2A_DESCRIPTOR_OFFSET;
            for (uint8_t descriptorIndex = 0; descriptorIndex < MAX_CDL_T2A_DESCRIPTOR; descriptorIndex++)
            {
                cdlSettings->scsiCDLSettings.cdlT2ADescriptor[descriptorIndex].timeFieldUnitType =
                    translate_Value_To_CDL_Unit(
                        M_Nibble0(cdlT2ADescriptorBuffer[(descriptorIndex * CDL_DESCRIPTOR_LENGTH) + 0]));
                cdlSettings->scsiCDLSettings.cdlT2ADescriptor[descriptorIndex].inactiveTime =
                    M_BytesTo4ByteValue(0, 0, cdlT2ADescriptorBuffer[(descriptorIndex * CDL_DESCRIPTOR_LENGTH) + 2],
                                        cdlT2ADescriptorBuffer[(descriptorIndex * CDL_DESCRIPTOR_LENGTH) + 3]);
                cdlSettings->scsiCDLSettings.cdlT2ADescriptor[descriptorIndex].activeTime =
                    M_BytesTo4ByteValue(0, 0, cdlT2ADescriptorBuffer[(descriptorIndex * CDL_DESCRIPTOR_LENGTH) + 4],
                                        cdlT2ADescriptorBuffer[(descriptorIndex * CDL_DESCRIPTOR_LENGTH) + 5]);
                cdlSettings->scsiCDLSettings.cdlT2ADescriptor[descriptorIndex].inactiveTimePolicy =
                    M_Nibble1(cdlT2ADescriptorBuffer[(descriptorIndex * CDL_DESCRIPTOR_LENGTH) + 6]);
                cdlSettings->scsiCDLSettings.cdlT2ADescriptor[descriptorIndex].activeTimePolicy =
                    M_Nibble0(cdlT2ADescriptorBuffer[(descriptorIndex * CDL_DESCRIPTOR_LENGTH) + 6]);
                cdlSettings->scsiCDLSettings.cdlT2ADescriptor[descriptorIndex].commandDurationGuideline =
                    M_BytesTo4ByteValue(0, 0, cdlT2ADescriptorBuffer[(descriptorIndex * CDL_DESCRIPTOR_LENGTH) + 10],
                                        cdlT2ADescriptorBuffer[(descriptorIndex * CDL_DESCRIPTOR_LENGTH) + 11]);
                cdlSettings->scsiCDLSettings.cdlT2ADescriptor[descriptorIndex].CommandDurationGuidelinePolicy =
                    M_Nibble0(cdlT2ADescriptorBuffer[(descriptorIndex * CDL_DESCRIPTOR_LENGTH) + 14]);
            }
        }
        else
        {
            safe_free_aligned(&modeData);
            return ret;
        }

        safe_free_aligned(&modeData);
    }

    // read T2B mode page
    if (SUCCESS == get_SCSI_Mode_Page_Size(device, MPC_CURRENT_VALUES, MP_CONTROL, 0x08, &modePageLength))
    {
        uint8_t* modeData =
            C_CAST(uint8_t*, safe_calloc_aligned(modePageLength, sizeof(uint8_t), device->os_info.minimumAlignment));
        if (!modeData)
        {
            return MEMORY_FAILURE;
        }

        // now read all the data
        bool used6ByteCmd = false;
        ret = get_SCSI_Mode_Page(device, MPC_CURRENT_VALUES, MP_CONTROL, 0x08, M_NULLPTR, M_NULLPTR, true, modeData,
                                 modePageLength, M_NULLPTR, &used6ByteCmd);
        if (SUCCESS == ret)
        {
            uint32_t offsetToModePage = UINT32_C(0);
            if (!used6ByteCmd)
            {
                uint16_t blockDescLen = UINT16_C(0);
                get_mode_param_header_10_fields(modeData, modePageLength, M_NULLPTR, M_NULLPTR, M_NULLPTR, M_NULLPTR,
                                                &blockDescLen);
                offsetToModePage = blockDescLen + MODE_PARAMETER_HEADER_10_LEN;
            }
            else
            {
                uint8_t blockDescLen = UINT8_C(0);
                get_mode_param_header_6_fields(modeData, modePageLength, M_NULLPTR, M_NULLPTR, M_NULLPTR,
                                               &blockDescLen);
                offsetToModePage = blockDescLen + MODE_PARAMETER_HEADER_6_LEN;
            }
            // parse the mode page buffer
            cdlSettings->isSupported        = true;
            uint8_t* cdlT2BDescriptorBuffer = modeData + offsetToModePage + CDL_T2B_DESCRIPTOR_OFFSET;
            for (uint8_t descriptorIndex = 0; descriptorIndex < MAX_CDL_T2B_DESCRIPTOR; descriptorIndex++)
            {
                cdlSettings->scsiCDLSettings.cdlT2BDescriptor[descriptorIndex].timeFieldUnitType =
                    translate_Value_To_CDL_Unit(
                        M_Nibble0(cdlT2BDescriptorBuffer[(descriptorIndex * CDL_DESCRIPTOR_LENGTH) + 0]));
                cdlSettings->scsiCDLSettings.cdlT2BDescriptor[descriptorIndex].inactiveTime =
                    M_BytesTo4ByteValue(0, 0, cdlT2BDescriptorBuffer[(descriptorIndex * CDL_DESCRIPTOR_LENGTH) + 2],
                                        cdlT2BDescriptorBuffer[(descriptorIndex * CDL_DESCRIPTOR_LENGTH) + 3]);
                cdlSettings->scsiCDLSettings.cdlT2BDescriptor[descriptorIndex].activeTime =
                    M_BytesTo4ByteValue(0, 0, cdlT2BDescriptorBuffer[(descriptorIndex * CDL_DESCRIPTOR_LENGTH) + 4],
                                        cdlT2BDescriptorBuffer[(descriptorIndex * CDL_DESCRIPTOR_LENGTH) + 5]);
                cdlSettings->scsiCDLSettings.cdlT2BDescriptor[descriptorIndex].inactiveTimePolicy =
                    M_Nibble1(cdlT2BDescriptorBuffer[(descriptorIndex * CDL_DESCRIPTOR_LENGTH) + 6]);
                cdlSettings->scsiCDLSettings.cdlT2BDescriptor[descriptorIndex].activeTimePolicy =
                    M_Nibble0(cdlT2BDescriptorBuffer[(descriptorIndex * CDL_DESCRIPTOR_LENGTH) + 6]);
                cdlSettings->scsiCDLSettings.cdlT2BDescriptor[descriptorIndex].commandDurationGuideline =
                    M_BytesTo4ByteValue(0, 0, cdlT2BDescriptorBuffer[(descriptorIndex * CDL_DESCRIPTOR_LENGTH) + 10],
                                        cdlT2BDescriptorBuffer[(descriptorIndex * CDL_DESCRIPTOR_LENGTH) + 11]);
                cdlSettings->scsiCDLSettings.cdlT2BDescriptor[descriptorIndex].CommandDurationGuidelinePolicy =
                    M_Nibble0(cdlT2BDescriptorBuffer[(descriptorIndex * CDL_DESCRIPTOR_LENGTH) + 14]);
            }
        }
        else
        {
            safe_free_aligned(&modeData);
            return ret;
        }

        safe_free_aligned(&modeData);
    }

    return ret;
}

eReturnValues get_CDL_Settings(tDevice* device, tCDLSettings* cdlSettings)
{
    eReturnValues ret = NOT_SUPPORTED;

    if (device->drive_info.drive_type == ATA_DRIVE)
    {
        ret = get_ATA_CDL_Settings(device, cdlSettings);
    }
    else if (device->drive_info.drive_type == SCSI_DRIVE)
    {
        ret = get_SCSI_CDL_Settings(device, cdlSettings);
    }

    return ret;
}

static void translate_CDL_Performance_Vs_Command_Completion_Field_To_String(uint8_t cmdCompletionField,
                                                                            char*   translatedString)
{
    switch (cmdCompletionField)
    {
    case 0x00:
        snprintf_err_handle(translatedString, MAX_CDL_PERFORMANCE_VS_CMD_COMPLETION_STATUS_STRING_LENGTH,
                            "0x%02" PRIX8 ", 0%%", cmdCompletionField);
        break;
    case 0x01:
        snprintf_err_handle(translatedString, MAX_CDL_PERFORMANCE_VS_CMD_COMPLETION_STATUS_STRING_LENGTH,
                            "0x%02" PRIX8 ", 0.5%%", cmdCompletionField);
        break;
    case 0x02:
        snprintf_err_handle(translatedString, MAX_CDL_PERFORMANCE_VS_CMD_COMPLETION_STATUS_STRING_LENGTH,
                            "0x%02" PRIX8 ", 1.0%%", cmdCompletionField);
        break;
    case 0x03:
        snprintf_err_handle(translatedString, MAX_CDL_PERFORMANCE_VS_CMD_COMPLETION_STATUS_STRING_LENGTH,
                            "0x%02" PRIX8 ", 1.5%%", cmdCompletionField);
        break;
    case 0x04:
        snprintf_err_handle(translatedString, MAX_CDL_PERFORMANCE_VS_CMD_COMPLETION_STATUS_STRING_LENGTH,
                            "0x%02" PRIX8 ", 2.0%%", cmdCompletionField);
        break;
    case 0x05:
        snprintf_err_handle(translatedString, MAX_CDL_PERFORMANCE_VS_CMD_COMPLETION_STATUS_STRING_LENGTH,
                            "0x%02" PRIX8 ", 2.5%%", cmdCompletionField);
        break;
    case 0x06:
        snprintf_err_handle(translatedString, MAX_CDL_PERFORMANCE_VS_CMD_COMPLETION_STATUS_STRING_LENGTH,
                            "0x%02" PRIX8 ", 3%%", cmdCompletionField);
        break;
    case 0x07:
        snprintf_err_handle(translatedString, MAX_CDL_PERFORMANCE_VS_CMD_COMPLETION_STATUS_STRING_LENGTH,
                            "0x%02" PRIX8 ", 4%%", cmdCompletionField);
        break;
    case 0x08:
        snprintf_err_handle(translatedString, MAX_CDL_PERFORMANCE_VS_CMD_COMPLETION_STATUS_STRING_LENGTH,
                            "0x%02" PRIX8 ", 5%%", cmdCompletionField);
        break;
    case 0x09:
        snprintf_err_handle(translatedString, MAX_CDL_PERFORMANCE_VS_CMD_COMPLETION_STATUS_STRING_LENGTH,
                            "0x%02" PRIX8 ", 8%%", cmdCompletionField);
        break;
    case 0x0A:
        snprintf_err_handle(translatedString, MAX_CDL_PERFORMANCE_VS_CMD_COMPLETION_STATUS_STRING_LENGTH,
                            "0x%02" PRIX8 ", 10%%", cmdCompletionField);
        break;
    case 0x0B:
        snprintf_err_handle(translatedString, MAX_CDL_PERFORMANCE_VS_CMD_COMPLETION_STATUS_STRING_LENGTH,
                            "0x%02" PRIX8 ", 15%%", cmdCompletionField);
        break;
    case 0x0C:
        snprintf_err_handle(translatedString, MAX_CDL_PERFORMANCE_VS_CMD_COMPLETION_STATUS_STRING_LENGTH,
                            "0x%02" PRIX8 ", 20%%", cmdCompletionField);
        break;
    case 0x0D:
    case 0x0E:
    case 0x0F:
        snprintf_err_handle(translatedString, MAX_CDL_PERFORMANCE_VS_CMD_COMPLETION_STATUS_STRING_LENGTH,
                            "0x%02" PRIX8 ", Reserved", cmdCompletionField);
        break;
    default:
        snprintf_err_handle(translatedString, MAX_CDL_PERFORMANCE_VS_CMD_COMPLETION_STATUS_STRING_LENGTH,
                            "0x%02" PRIX8 ", Unknown", cmdCompletionField);
        break;
    }
}

static void translate_Policy_To_String(eDriveType     driveType,
                                       eCDLPolicyType policyType,
                                       uint8_t        policyField,
                                       char*          translatedString)
{
    if (policyType == CDL_POLICY_TYPE_TOTAL_TIME || policyType == CDL_POLICY_TYPE_COMMAND_DURATION_GUIDELINE)
    {
        switch (policyField)
        {
        case 0x00:
            snprintf_err_handle(translatedString, MAX_POLICY_STRING_LENGHT,
                                "0x%02" PRIX8 ", When time exceeded, device completes the command as soon as possible",
                                policyField);
            break;
        case 0x01:
            snprintf_err_handle(translatedString, MAX_POLICY_STRING_LENGHT,
                                "0x%02" PRIX8 ", When time exceeded, uses next descriptor to extend time limit",
                                policyField);
            break;
        case 0x02:
            snprintf_err_handle(translatedString, MAX_POLICY_STRING_LENGHT,
                                "0x%02" PRIX8
                                ", When time exceeded, continue processing command and disregard latency requirements",
                                policyField);
            break;
        case 0x0D:
            snprintf_err_handle(translatedString, MAX_POLICY_STRING_LENGHT,
                                "0x%02" PRIX8
                                ", When time exceeded, device completes the command without error and sense code "
                                "set to \"Data Currently Unavailable\"",
                                policyField);
            break;
        case 0x0F:
            snprintf_err_handle(
                translatedString, MAX_POLICY_STRING_LENGHT,
                "0x%02" PRIX8
                ", When time exceeded, device returns command abort and sense code describing command timeout",
                policyField);
            break;
        default:
            snprintf_err_handle(translatedString, MAX_POLICY_STRING_LENGHT, "0x%02" PRIX8 ", Reserved", policyField);
            break;
        }
    }
    else
    {
        switch (policyField)
        {
        case 0x00:
            if (driveType == ATA_DRIVE)
            {
                if (policyType == CDL_POLICY_TYPE_INACTIVE_TIME)
                    snprintf_err_handle(translatedString, MAX_POLICY_STRING_LENGHT,
                                        "0x%02" PRIX8 ", The device ignores the INACTIVE TIME LIMIT field",
                                        policyField);
                else
                    snprintf_err_handle(translatedString, MAX_POLICY_STRING_LENGHT,
                                        "0x%02" PRIX8 ", The device ignores the ACTIVE TIME LIMIT field", policyField);
            }
            else if (driveType == SCSI_DRIVE)
            {
                snprintf_err_handle(translatedString, MAX_POLICY_STRING_LENGHT,
                                    "0x%02" PRIX8 ", The device completes the command at the earliest possible time",
                                    policyField);
            }
            break;
        case 0x0D:
            snprintf_err_handle(translatedString, MAX_POLICY_STRING_LENGHT,
                                "0x%02" PRIX8
                                ", When time exceeded, device completes the command without error and sense code "
                                "set to \"Data Currently Unavailable\"",
                                policyField);
            break;
        case 0x0E:
            if (driveType == ATA_DRIVE)
            {
                snprintf_err_handle(translatedString, MAX_POLICY_STRING_LENGHT, "0x%02" PRIX8 ", Reserved",
                                    policyField);
            }
            else if (driveType == SCSI_DRIVE)
            {
                if (policyType == CDL_POLICY_TYPE_INACTIVE_TIME)
                    snprintf_err_handle(translatedString, MAX_POLICY_STRING_LENGHT, "0x%02" PRIX8 ", Reserved",
                                        policyField);
                else
                    snprintf_err_handle(
                        translatedString, MAX_POLICY_STRING_LENGHT,
                        "0x%02" PRIX8
                        ", When time exceeded, device returns command abort and sense code describing command timeout",
                        policyField);
            }
            break;
        case 0x0F:
            snprintf_err_handle(
                translatedString, MAX_POLICY_STRING_LENGHT,
                "0x%02" PRIX8
                ", When time exceeded, device returns command abort and sense code describing command timeout",
                policyField);
            break;
        default:
            snprintf_err_handle(translatedString, MAX_POLICY_STRING_LENGHT, "0x%02" PRIX8 ", Reserved", policyField);
            break;
        }
    }
}

static eReturnValues print_ATA_CDL_Settings(tCDLSettings* cdlSettings)
{
    eReturnValues ret = SUCCESS;
    if (!cdlSettings)
    {
        return BAD_PARAMETER;
    }

    if (cdlSettings->isSupported)
    {
        printf("\tCommand Duration Limit : Supported, %s\n", cdlSettings->isEnabled ? "Enabled" : "Disabled");
        uint32_t currentCDLFeatureVersion =
            M_BytesTo4ByteValue(CDL_FEATURE_MAJOR_VERSION, CDL_FEATURE_MINOR_VERSION, CDL_FEATURE_PATCH_VERSION, 0);
        if (currentCDLFeatureVersion == 0x01000000)
            printf("\tCommand Duration Guideline : %s\n",
                   cdlSettings->ataCDLSettings.isCommandDurationGuidelineSupported ? "Supported" : "Not Supported");
        printf("\tCommand Duration Limit Minimum Limit (us) : %" PRIu32 "\n",
               cdlSettings->ataCDLSettings.minimumTimeLimit);
        printf("\tCommand Duration Limit Maximum Limit (us) : %" PRIu32 "\n",
               cdlSettings->ataCDLSettings.maximumTimeLimit);
        DECLARE_ZERO_INIT_ARRAY(char, inactivePolicyString, SUPPORTED_POLICY_STRING_LENGTH);
        get_Supported_Policy_String(ATA_DRIVE, CDL_POLICY_TYPE_INACTIVE_TIME,
                                    cdlSettings->ataCDLSettings.inactiveTimePolicySupportedDescriptor,
                                    inactivePolicyString);
        printf("\tSupported Inactive Time Policy : %s\n", inactivePolicyString);
        DECLARE_ZERO_INIT_ARRAY(char, activePolicyString, SUPPORTED_POLICY_STRING_LENGTH);
        get_Supported_Policy_String(ATA_DRIVE, CDL_POLICY_TYPE_ACTIVE_TIME,
                                    cdlSettings->ataCDLSettings.activeTimePolicySupportedDescriptor,
                                    activePolicyString);
        printf("\tSupported Active Time Policy : %s\n", activePolicyString);
        if (is_Total_Time_Policy_Type_Supported(cdlSettings))
        {
            DECLARE_ZERO_INIT_ARRAY(char, totalPolicyString, SUPPORTED_POLICY_STRING_LENGTH);
            get_Supported_Policy_String(ATA_DRIVE, CDL_POLICY_TYPE_TOTAL_TIME,
                                        cdlSettings->ataCDLSettings.totalTimePolicySupportedDescriptor,
                                        totalPolicyString);
            printf("\tSupported Total Time Policy : %s\n", totalPolicyString);
        }

        if (is_Performance_Versus_Command_Completion_Supported(cdlSettings))
        {
            DECLARE_ZERO_INIT_ARRAY(char, statusTranslation,
                                    MAX_CDL_PERFORMANCE_VS_CMD_COMPLETION_STATUS_STRING_LENGTH);
            translate_CDL_Performance_Vs_Command_Completion_Field_To_String(
                cdlSettings->ataCDLSettings.performanceVsCommandCompletion, statusTranslation);
            printf("\tPerformance Versus Command Completion : %s\n", statusTranslation);
        }

        for (uint8_t descriptorIndex = 0; descriptorIndex < MAX_CDL_READ_DESCRIPTOR; descriptorIndex++)
        {
            printf("\tDescriptor : R%" PRIu8 "\n", (descriptorIndex + 1));
            printf("\t\tInactive Time (us) : %" PRIu32 "\n",
                   cdlSettings->ataCDLSettings.cdlReadDescriptor[descriptorIndex].inactiveTime);
            DECLARE_ZERO_INIT_ARRAY(char, policyTranslation, MAX_POLICY_STRING_LENGHT);
            translate_Policy_To_String(
                ATA_DRIVE, CDL_POLICY_TYPE_INACTIVE_TIME,
                cdlSettings->ataCDLSettings.cdlReadDescriptor[descriptorIndex].inactiveTimePolicy, policyTranslation);
            printf("\t\tInactive Time Policy : %s\n", policyTranslation);
            printf("\t\tActive Time (us) : %" PRIu32 "\n",
                   cdlSettings->ataCDLSettings.cdlReadDescriptor[descriptorIndex].activeTime);
            translate_Policy_To_String(ATA_DRIVE, CDL_POLICY_TYPE_ACTIVE_TIME,
                                       cdlSettings->ataCDLSettings.cdlReadDescriptor[descriptorIndex].activeTimePolicy,
                                       policyTranslation);
            printf("\t\tActive Time Policy : %s\n", policyTranslation);
            if (is_Total_Time_Policy_Type_Supported(cdlSettings))
            {
                printf("\t\tTotal Time (us) : %" PRIu32 "\n",
                       cdlSettings->ataCDLSettings.cdlReadDescriptor[descriptorIndex].totalTime);
                translate_Policy_To_String(
                    ATA_DRIVE, CDL_POLICY_TYPE_TOTAL_TIME,
                    cdlSettings->ataCDLSettings.cdlReadDescriptor[descriptorIndex].totalTimePolicy, policyTranslation);
                printf("\t\tTotal Time Policy : %s\n", policyTranslation);
            }
        }

        for (uint8_t descriptorIndex = 0; descriptorIndex < MAX_CDL_WRITE_DESCRIPTOR; descriptorIndex++)
        {
            printf("\tDescriptor : W%" PRIu8 "\n", (descriptorIndex + 1));
            printf("\t\tInactive Time (us) : %" PRIu32 "\n",
                   cdlSettings->ataCDLSettings.cdlWriteDescriptor[descriptorIndex].inactiveTime);
            DECLARE_ZERO_INIT_ARRAY(char, policyTranslation, MAX_POLICY_STRING_LENGHT);
            translate_Policy_To_String(
                ATA_DRIVE, CDL_POLICY_TYPE_INACTIVE_TIME,
                cdlSettings->ataCDLSettings.cdlWriteDescriptor[descriptorIndex].inactiveTimePolicy, policyTranslation);
            printf("\t\tInactive Time Policy : %s\n", policyTranslation);
            printf("\t\tActive Time (us) : %" PRIu32 "\n",
                   cdlSettings->ataCDLSettings.cdlWriteDescriptor[descriptorIndex].activeTime);
            translate_Policy_To_String(ATA_DRIVE, CDL_POLICY_TYPE_ACTIVE_TIME,
                                       cdlSettings->ataCDLSettings.cdlWriteDescriptor[descriptorIndex].activeTimePolicy,
                                       policyTranslation);
            printf("\t\tActive Time Policy : %s\n", policyTranslation);
            if (is_Total_Time_Policy_Type_Supported(cdlSettings))
            {
                printf("\t\tTotal Time (us) : %" PRIu32 "\n",
                       cdlSettings->ataCDLSettings.cdlWriteDescriptor[descriptorIndex].totalTime);
                translate_Policy_To_String(
                    ATA_DRIVE, CDL_POLICY_TYPE_TOTAL_TIME,
                    cdlSettings->ataCDLSettings.cdlWriteDescriptor[descriptorIndex].totalTimePolicy, policyTranslation);
                printf("\t\tTotal Time Policy : %s\n", policyTranslation);
            }
        }
    }
    else
    {
        printf("\tCommand Duration Limit : Not Supported\n");
    }

    return ret;
}

static eReturnValues print_SCSI_CDL_Settings(tCDLSettings* cdlSettings)
{
    eReturnValues ret = SUCCESS;
    if (!cdlSettings)
    {
        return BAD_PARAMETER;
    }

    if (cdlSettings->isSupported)
    {
        printf("\tCommand Duration Limit : Supported\n");
        printf("\tCommand Duration Guideline : Supported\n");
        printf("\tCommand Duration Limit Minimum Limit (ns) : %llu\n", 500ULL); // TODO - read values from drive
        printf("\tCommand Duration Limit Maximum Limit (ns) : %llu\n",
               (500000000ULL * 500000000ULL)); // TODO - read values from drive
        DECLARE_ZERO_INIT_ARRAY(char, inactivePolicyString, SUPPORTED_POLICY_STRING_LENGTH);
        get_Supported_Policy_String(SCSI_DRIVE, CDL_POLICY_TYPE_INACTIVE_TIME, 0, inactivePolicyString);
        printf("\tSupported Inactive Time Policy : %s\n", inactivePolicyString);
        DECLARE_ZERO_INIT_ARRAY(char, activePolicyString, SUPPORTED_POLICY_STRING_LENGTH);
        get_Supported_Policy_String(SCSI_DRIVE, CDL_POLICY_TYPE_ACTIVE_TIME, 0, activePolicyString);
        printf("\tSupported Active Time Policy : %s\n", activePolicyString);
        DECLARE_ZERO_INIT_ARRAY(char, commandDurationGuidelinePolicyString, SUPPORTED_POLICY_STRING_LENGTH);
        get_Supported_Policy_String(SCSI_DRIVE, CDL_POLICY_TYPE_COMMAND_DURATION_GUIDELINE, 0,
                                    commandDurationGuidelinePolicyString);
        printf("\tSupported Command Duration Guideline Policy : %s\n", commandDurationGuidelinePolicyString);
        DECLARE_ZERO_INIT_ARRAY(char, statusTranslation, MAX_CDL_PERFORMANCE_VS_CMD_COMPLETION_STATUS_STRING_LENGTH);
        translate_CDL_Performance_Vs_Command_Completion_Field_To_String(
            cdlSettings->scsiCDLSettings.performanceVsCommandDurationGuidelines, statusTranslation);
        printf("\tPerformance Versus Command Duration Guidelines : %s\n", statusTranslation);

        for (uint8_t descriptorIndex = 0; descriptorIndex < MAX_CDL_T2A_DESCRIPTOR; descriptorIndex++)
        {
            printf("\tT2A Descriptor : %" PRIu8 "\n", (descriptorIndex + 1));
            printf("\t\tInactive Time (us) : %" PRIu32 "\n",
                   convert_CDL_TimeField_To_Microseconds(
                       cdlSettings->scsiCDLSettings.cdlT2ADescriptor[descriptorIndex].timeFieldUnitType,
                       cdlSettings->scsiCDLSettings.cdlT2ADescriptor[descriptorIndex].inactiveTime));
            DECLARE_ZERO_INIT_ARRAY(char, policyTranslation, MAX_POLICY_STRING_LENGHT);
            translate_Policy_To_String(
                SCSI_DRIVE, CDL_POLICY_TYPE_INACTIVE_TIME,
                cdlSettings->scsiCDLSettings.cdlT2ADescriptor[descriptorIndex].inactiveTimePolicy, policyTranslation);
            printf("\t\tInactive Time Policy : %s\n", policyTranslation);
            printf("\t\tActive Time (us) : %" PRIu32 "\n",
                   convert_CDL_TimeField_To_Microseconds(
                       cdlSettings->scsiCDLSettings.cdlT2ADescriptor[descriptorIndex].timeFieldUnitType,
                       cdlSettings->scsiCDLSettings.cdlT2ADescriptor[descriptorIndex].activeTime));
            translate_Policy_To_String(SCSI_DRIVE, CDL_POLICY_TYPE_ACTIVE_TIME,
                                       cdlSettings->scsiCDLSettings.cdlT2ADescriptor[descriptorIndex].activeTimePolicy,
                                       policyTranslation);
            printf("\t\tActive Time Policy : %s\n", policyTranslation);
            printf("\t\tCommand Duration Guideline (us) : %" PRIu32 "\n",
                   convert_CDL_TimeField_To_Microseconds(
                       cdlSettings->scsiCDLSettings.cdlT2ADescriptor[descriptorIndex].timeFieldUnitType,
                       cdlSettings->scsiCDLSettings.cdlT2ADescriptor[descriptorIndex].commandDurationGuideline));
            translate_Policy_To_String(
                SCSI_DRIVE, CDL_POLICY_TYPE_COMMAND_DURATION_GUIDELINE,
                cdlSettings->scsiCDLSettings.cdlT2ADescriptor[descriptorIndex].CommandDurationGuidelinePolicy,
                policyTranslation);
            printf("\t\tCommand Duration Guideline Policy : %s\n", policyTranslation);
        }

        for (uint8_t descriptorIndex = 0; descriptorIndex < MAX_CDL_T2A_DESCRIPTOR; descriptorIndex++)
        {
            printf("\tT2B Descriptor : %" PRIu8 "\n", (descriptorIndex + 1));
            printf("\t\tInactive Time (us) : %" PRIu32 "\n",
                   convert_CDL_TimeField_To_Microseconds(
                       cdlSettings->scsiCDLSettings.cdlT2BDescriptor[descriptorIndex].timeFieldUnitType,
                       cdlSettings->scsiCDLSettings.cdlT2BDescriptor[descriptorIndex].inactiveTime));
            DECLARE_ZERO_INIT_ARRAY(char, policyTranslation, MAX_POLICY_STRING_LENGHT);
            translate_Policy_To_String(
                SCSI_DRIVE, CDL_POLICY_TYPE_INACTIVE_TIME,
                cdlSettings->scsiCDLSettings.cdlT2BDescriptor[descriptorIndex].inactiveTimePolicy, policyTranslation);
            printf("\t\tInactive Time Policy : %s\n", policyTranslation);
            printf("\t\tActive Time (us) : %" PRIu32 "\n",
                   convert_CDL_TimeField_To_Microseconds(
                       cdlSettings->scsiCDLSettings.cdlT2BDescriptor[descriptorIndex].timeFieldUnitType,
                       cdlSettings->scsiCDLSettings.cdlT2BDescriptor[descriptorIndex].activeTime));
            translate_Policy_To_String(SCSI_DRIVE, CDL_POLICY_TYPE_ACTIVE_TIME,
                                       cdlSettings->scsiCDLSettings.cdlT2BDescriptor[descriptorIndex].activeTimePolicy,
                                       policyTranslation);
            printf("\t\tActive Time Policy : %s\n", policyTranslation);
            printf("\t\tCommand Duration Guideline (us) : %" PRIu32 "\n",
                   convert_CDL_TimeField_To_Microseconds(
                       cdlSettings->scsiCDLSettings.cdlT2BDescriptor[descriptorIndex].timeFieldUnitType,
                       cdlSettings->scsiCDLSettings.cdlT2BDescriptor[descriptorIndex].commandDurationGuideline));
            translate_Policy_To_String(
                SCSI_DRIVE, CDL_POLICY_TYPE_COMMAND_DURATION_GUIDELINE,
                cdlSettings->scsiCDLSettings.cdlT2BDescriptor[descriptorIndex].CommandDurationGuidelinePolicy,
                policyTranslation);
            printf("\t\tCommand Duration Guideline Policy : %s\n", policyTranslation);
        }
    }
    else
    {
        printf("\tCommand Duration Limit : Not Supported\n");
    }

    return ret;
}

eReturnValues print_CDL_Settings(tDevice* device, tCDLSettings* cdlSettings)
{
    eReturnValues ret = NOT_SUPPORTED;
    if (!cdlSettings)
    {
        return BAD_PARAMETER;
    }

    if (device->drive_info.drive_type == ATA_DRIVE)
    {
        ret = print_ATA_CDL_Settings(cdlSettings);
    }
    else if (device->drive_info.drive_type == SCSI_DRIVE)
    {
        ret = print_SCSI_CDL_Settings(cdlSettings);
    }

    return ret;
}

static eReturnValues config_ATA_CDL_Settings(tDevice* device, tCDLSettings* cdlSettings)
{
    eReturnValues ret = SUCCESS;
    if (!cdlSettings)
    {
        return BAD_PARAMETER;
    }

    // read current CDL settings
    uint32_t logSize = 0;
    ret              = get_ATA_Log_Size(device, ATA_LOG_COMMAND_DURATION_LIMITS_LOG, &logSize, true, false);
    if (ret == SUCCESS)
    {
        uint8_t* logBuffer = safe_calloc_aligned(logSize, sizeof(uint8_t), device->os_info.minimumAlignment);
        if (!logBuffer)
        {
            return MEMORY_FAILURE;
        }

        ret = get_ATA_Log(device, ATA_LOG_COMMAND_DURATION_LIMITS_LOG, M_NULLPTR, M_NULLPTR, true, false, true,
                          logBuffer, logSize, M_NULLPTR, LEGACY_DRIVE_SEC_SIZE, 0);
        if (SUCCESS == ret)
        {
            // change the values provided by user
            logBuffer[0] =
                (M_Nibble1(logBuffer[0]) << 4) | M_Nibble0(cdlSettings->ataCDLSettings.performanceVsCommandCompletion);

            uint8_t* cdlReadDescriptorBuffer = (logBuffer + CDL_READ_DESCRIPTOR_OFFSET);
            for (uint8_t descriptorIndex = 0; descriptorIndex < MAX_CDL_READ_DESCRIPTOR; descriptorIndex++)
            {
                cdlReadDescriptorBuffer[(descriptorIndex * CDL_DESCRIPTOR_LENGTH) + 0] =
                    (M_Nibble0(cdlSettings->ataCDLSettings.cdlReadDescriptor[descriptorIndex].activeTimePolicy) << 4) |
                    M_Nibble0(cdlSettings->ataCDLSettings.cdlReadDescriptor[descriptorIndex].totalTimePolicy);
                cdlReadDescriptorBuffer[(descriptorIndex * CDL_DESCRIPTOR_LENGTH) + 1] =
                    (M_Nibble1(cdlReadDescriptorBuffer[(descriptorIndex * CDL_DESCRIPTOR_LENGTH) + 1]) << 4) |
                    M_Nibble0(cdlSettings->ataCDLSettings.cdlReadDescriptor[descriptorIndex].inactiveTimePolicy);
                memcpy(cdlReadDescriptorBuffer + ((descriptorIndex * CDL_DESCRIPTOR_LENGTH) + 4),
                       &cdlSettings->ataCDLSettings.cdlReadDescriptor[descriptorIndex].activeTime, sizeof(uint32_t));
                memcpy(cdlReadDescriptorBuffer + ((descriptorIndex * CDL_DESCRIPTOR_LENGTH) + 8),
                       &cdlSettings->ataCDLSettings.cdlReadDescriptor[descriptorIndex].inactiveTime, sizeof(uint32_t));
                memcpy(cdlReadDescriptorBuffer + ((descriptorIndex * CDL_DESCRIPTOR_LENGTH) + 16),
                       &cdlSettings->ataCDLSettings.cdlReadDescriptor[descriptorIndex].totalTime, sizeof(uint32_t));
            }

            uint8_t* cdlWriteDescriptorBuffer = logBuffer + CDL_WRITE_DESCRIPTOR_OFFSET;
            for (uint8_t descriptorIndex = 0; descriptorIndex < MAX_CDL_WRITE_DESCRIPTOR; descriptorIndex++)
            {
                cdlWriteDescriptorBuffer[(descriptorIndex * CDL_DESCRIPTOR_LENGTH) + 0] =
                    (M_Nibble0(cdlSettings->ataCDLSettings.cdlWriteDescriptor[descriptorIndex].activeTimePolicy) << 4) |
                    M_Nibble0(cdlSettings->ataCDLSettings.cdlWriteDescriptor[descriptorIndex].totalTimePolicy);
                cdlWriteDescriptorBuffer[(descriptorIndex * CDL_DESCRIPTOR_LENGTH) + 1] =
                    (M_Nibble1(cdlWriteDescriptorBuffer[(descriptorIndex * CDL_DESCRIPTOR_LENGTH) + 1]) << 4) |
                    M_Nibble0(cdlSettings->ataCDLSettings.cdlWriteDescriptor[descriptorIndex].inactiveTimePolicy);
                memcpy(cdlWriteDescriptorBuffer + ((descriptorIndex * CDL_DESCRIPTOR_LENGTH) + 4),
                       &cdlSettings->ataCDLSettings.cdlWriteDescriptor[descriptorIndex].activeTime, sizeof(uint32_t));
                memcpy(cdlWriteDescriptorBuffer + ((descriptorIndex * CDL_DESCRIPTOR_LENGTH) + 8),
                       &cdlSettings->ataCDLSettings.cdlWriteDescriptor[descriptorIndex].inactiveTime, sizeof(uint32_t));
                memcpy(cdlWriteDescriptorBuffer + ((descriptorIndex * CDL_DESCRIPTOR_LENGTH) + 16),
                       &cdlSettings->ataCDLSettings.cdlWriteDescriptor[descriptorIndex].totalTime, sizeof(uint32_t));
            }

            // sent write log command
            ret = ata_Write_Log_Ext(device, ATA_LOG_COMMAND_DURATION_LIMITS_LOG, 0, logBuffer, logSize,
                                    device->drive_info.ata_Options.readLogWriteLogDMASupported, false);
            if (SUCCESS == ret)
            {
                safe_free_aligned(&logBuffer);
                return ret;
            }
        }
        else
        {
            safe_free_aligned(&logBuffer);
            return ret;
        }

        safe_free_aligned(&logBuffer);
    }

    return ret;
}

static uint8_t translate_CDL_Unit_To_Value(eCDLTimeFieldUnitType unitType)
{
    uint8_t value = 0x08;
    switch (unitType)
    {
    case CDL_TIME_FIELD_UNIT_TYPE_NO_VALUE:
        value = 0x00;
        break;

    case CDL_TIME_FIELD_UNIT_TYPE_500_NANOSECONDS:
        value = 0x06;
        break;

    case CDL_TIME_FIELD_UNIT_TYPE_10_MILLISECONDS:
        value = 0x0A;
        break;

    case CDL_TIME_FIELD_UNIT_TYPE_500_MILLISECONDS:
        value = 0x0E;
        break;

    case CDL_TIME_FIELD_UNIT_TYPE_MICROSECONDS:
    default:
        value = 0x08;
        break;
    }

    return value;
}

static eReturnValues config_SCSI_CDL_Settings(tDevice* device, tCDLSettings* cdlSettings)
{
    eReturnValues ret = SUCCESS;
    if (!cdlSettings)
    {
        return BAD_PARAMETER;
    }

    // read T2A mode page before modifying data
    uint32_t modePageLength = 0;
    ret                     = get_SCSI_Mode_Page_Size(device, MPC_CURRENT_VALUES, MP_CONTROL, 0x07, &modePageLength);
    if (SUCCESS == ret)
    {
        uint8_t* modeData =
            C_CAST(uint8_t*, safe_calloc_aligned(modePageLength, sizeof(uint8_t), device->os_info.minimumAlignment));
        if (!modeData)
        {
            return MEMORY_FAILURE;
        }

        // now read all the data
        bool used6ByteCmd = false;
        ret = get_SCSI_Mode_Page(device, MPC_CURRENT_VALUES, MP_CONTROL, 0x07, M_NULLPTR, M_NULLPTR, true, modeData,
                                 modePageLength, M_NULLPTR, &used6ByteCmd);
        if (SUCCESS == ret)
        {
            uint32_t offsetToModePage = UINT32_C(0);
            if (!used6ByteCmd)
            {
                uint16_t blockDescLen = UINT16_C(0);
                get_mode_param_header_10_fields(modeData, modePageLength, M_NULLPTR, M_NULLPTR, M_NULLPTR, M_NULLPTR,
                                                &blockDescLen);
                offsetToModePage = blockDescLen + MODE_PARAMETER_HEADER_10_LEN;
            }
            else
            {
                uint8_t blockDescLen = UINT8_C(0);
                get_mode_param_header_6_fields(modeData, modePageLength, M_NULLPTR, M_NULLPTR, M_NULLPTR,
                                               &blockDescLen);
                offsetToModePage = blockDescLen + MODE_PARAMETER_HEADER_6_LEN;
            }

            modeData[offsetToModePage + 7] =
                (M_Nibble0(cdlSettings->scsiCDLSettings.performanceVsCommandDurationGuidelines) << 4) |
                M_Nibble0(modeData[offsetToModePage + 7]);
            uint8_t* cdlT2ADescriptorBuffer = modeData + offsetToModePage + CDL_T2A_DESCRIPTOR_OFFSET;
            for (uint8_t descriptorIndex = 0; descriptorIndex < MAX_CDL_T2A_DESCRIPTOR; descriptorIndex++)
            {
                cdlT2ADescriptorBuffer[(descriptorIndex * CDL_DESCRIPTOR_LENGTH) + 0] =
                    (M_Nibble1(cdlT2ADescriptorBuffer[(descriptorIndex * CDL_DESCRIPTOR_LENGTH) + 0]) << 4) |
                    M_Nibble0(translate_CDL_Unit_To_Value(
                        cdlSettings->scsiCDLSettings.cdlT2ADescriptor[descriptorIndex].timeFieldUnitType));
                uint16_t value = M_Word0(cdlSettings->scsiCDLSettings.cdlT2ADescriptor[descriptorIndex].inactiveTime);
                byte_Swap_16(&value);
                memcpy(cdlT2ADescriptorBuffer + ((descriptorIndex * CDL_DESCRIPTOR_LENGTH) + 2), &value,
                       sizeof(uint16_t));
                value = M_Word0(cdlSettings->scsiCDLSettings.cdlT2ADescriptor[descriptorIndex].activeTime);
                byte_Swap_16(&value);
                memcpy(cdlT2ADescriptorBuffer + ((descriptorIndex * CDL_DESCRIPTOR_LENGTH) + 4), &value,
                       sizeof(uint16_t));
                cdlT2ADescriptorBuffer[(descriptorIndex * CDL_DESCRIPTOR_LENGTH) + 6] =
                    (M_Nibble0(cdlSettings->scsiCDLSettings.cdlT2ADescriptor[descriptorIndex].inactiveTimePolicy)
                     << 4) |
                    (M_Nibble0(cdlSettings->scsiCDLSettings.cdlT2ADescriptor[descriptorIndex].activeTimePolicy));
                value =
                    M_Word0(cdlSettings->scsiCDLSettings.cdlT2ADescriptor[descriptorIndex].commandDurationGuideline);
                byte_Swap_16(&value);
                memcpy(cdlT2ADescriptorBuffer + ((descriptorIndex * CDL_DESCRIPTOR_LENGTH) + 10), &value,
                       sizeof(uint16_t));
                cdlT2ADescriptorBuffer[(descriptorIndex * CDL_DESCRIPTOR_LENGTH) + 14] =
                    (M_Nibble1(cdlT2ADescriptorBuffer[(descriptorIndex * CDL_DESCRIPTOR_LENGTH) + 14]) << 4) |
                    M_Nibble0(
                        cdlSettings->scsiCDLSettings.cdlT2ADescriptor[descriptorIndex].CommandDurationGuidelinePolicy);
            }

            // now send the modified data to drive
            ret = scsi_Mode_Select_10(device, C_CAST(uint16_t, modePageLength), true, true, false, modeData,
                                      modePageLength);
            if (ret != SUCCESS)
            {
                safe_free_aligned(&modeData);
                return ret;
            }
        }
        else
        {
            safe_free_aligned(&modeData);
            return ret;
        }

        safe_free_aligned(&modeData);
    }

    // read T2B mode page before modifying data
    ret = get_SCSI_Mode_Page_Size(device, MPC_CURRENT_VALUES, MP_CONTROL, 0x08, &modePageLength);
    if (SUCCESS == ret)
    {
        uint8_t* modeData =
            C_CAST(uint8_t*, safe_calloc_aligned(modePageLength, sizeof(uint8_t), device->os_info.minimumAlignment));
        if (!modeData)
        {
            return MEMORY_FAILURE;
        }

        // now read all the data
        bool used6ByteCmd = false;
        ret = get_SCSI_Mode_Page(device, MPC_CURRENT_VALUES, MP_CONTROL, 0x08, M_NULLPTR, M_NULLPTR, true, modeData,
                                 modePageLength, M_NULLPTR, &used6ByteCmd);
        if (SUCCESS == ret)
        {
            uint32_t offsetToModePage = UINT32_C(0);
            if (!used6ByteCmd)
            {
                uint16_t blockDescLen = UINT16_C(0);
                get_mode_param_header_10_fields(modeData, modePageLength, M_NULLPTR, M_NULLPTR, M_NULLPTR, M_NULLPTR,
                                                &blockDescLen);
                offsetToModePage = blockDescLen + MODE_PARAMETER_HEADER_10_LEN;
            }
            else
            {
                uint8_t blockDescLen = UINT8_C(0);
                get_mode_param_header_6_fields(modeData, modePageLength, M_NULLPTR, M_NULLPTR, M_NULLPTR,
                                               &blockDescLen);
                offsetToModePage = blockDescLen + MODE_PARAMETER_HEADER_6_LEN;
            }

            uint8_t* cdlT2BDescriptorBuffer = modeData + offsetToModePage + CDL_T2B_DESCRIPTOR_OFFSET;
            for (uint8_t descriptorIndex = 0; descriptorIndex < MAX_CDL_T2B_DESCRIPTOR; descriptorIndex++)
            {
                cdlT2BDescriptorBuffer[(descriptorIndex * CDL_DESCRIPTOR_LENGTH) + 0] =
                    (M_Nibble1(cdlT2BDescriptorBuffer[(descriptorIndex * CDL_DESCRIPTOR_LENGTH) + 0]) << 4) |
                    M_Nibble0(translate_CDL_Unit_To_Value(
                        cdlSettings->scsiCDLSettings.cdlT2ADescriptor[descriptorIndex].timeFieldUnitType));
                uint16_t value = M_Word0(cdlSettings->scsiCDLSettings.cdlT2ADescriptor[descriptorIndex].inactiveTime);
                byte_Swap_16(&value);
                memcpy(cdlT2BDescriptorBuffer + ((descriptorIndex * CDL_DESCRIPTOR_LENGTH) + 2), &value,
                       sizeof(uint16_t));
                value = M_Word0(cdlSettings->scsiCDLSettings.cdlT2ADescriptor[descriptorIndex].activeTime);
                byte_Swap_16(&value);
                memcpy(cdlT2BDescriptorBuffer + ((descriptorIndex * CDL_DESCRIPTOR_LENGTH) + 4), &value,
                       sizeof(uint16_t));
                cdlT2BDescriptorBuffer[(descriptorIndex * CDL_DESCRIPTOR_LENGTH) + 6] =
                    (M_Nibble0(cdlSettings->scsiCDLSettings.cdlT2ADescriptor[descriptorIndex].inactiveTimePolicy)
                     << 4) |
                    (M_Nibble0(cdlSettings->scsiCDLSettings.cdlT2ADescriptor[descriptorIndex].activeTimePolicy));
                value =
                    M_Word0(cdlSettings->scsiCDLSettings.cdlT2ADescriptor[descriptorIndex].commandDurationGuideline);
                byte_Swap_16(&value);
                memcpy(cdlT2BDescriptorBuffer + ((descriptorIndex * CDL_DESCRIPTOR_LENGTH) + 10), &value,
                       sizeof(uint16_t));
                cdlT2BDescriptorBuffer[(descriptorIndex * CDL_DESCRIPTOR_LENGTH) + 14] =
                    (M_Nibble1(cdlT2BDescriptorBuffer[(descriptorIndex * CDL_DESCRIPTOR_LENGTH) + 14]) << 4) |
                    M_Nibble0(
                        cdlSettings->scsiCDLSettings.cdlT2ADescriptor[descriptorIndex].CommandDurationGuidelinePolicy);
            }

            // now send the modified data to drive
            ret = scsi_Mode_Select_10(device, C_CAST(uint16_t, modePageLength), true, true, false, modeData,
                                      modePageLength);
            if (ret != SUCCESS)
            {
                safe_free_aligned(&modeData);
                return ret;
            }
        }
        else
        {
            safe_free_aligned(&modeData);
            return ret;
        }

        safe_free_aligned(&modeData);
    }

    return ret;
}

eReturnValues config_CDL_Settings(tDevice* device, tCDLSettings* cdlSettings)
{
    eReturnValues ret = NOT_SUPPORTED;
    if (!cdlSettings)
    {
        return BAD_PARAMETER;
    }

    if (device->drive_info.drive_type == ATA_DRIVE)
    {
        ret = config_ATA_CDL_Settings(device, cdlSettings);
    }
    else if (device->drive_info.drive_type == SCSI_DRIVE)
    {
        ret = config_SCSI_CDL_Settings(device, cdlSettings);
    }

    return ret;
}

static bool is_Valid_Supported_Policy(eDriveType     driveType,
                                      eCDLPolicyType policyType,
                                      uint16_t       policySupportedDescriptor,
                                      uint8_t        policyField)
{
    uint32_t currentCDLFeatureVersion =
        M_BytesTo4ByteValue(CDL_FEATURE_MAJOR_VERSION, CDL_FEATURE_MINOR_VERSION, CDL_FEATURE_PATCH_VERSION, 0);
    if (driveType == ATA_DRIVE)
    {
        if (currentCDLFeatureVersion == 0x01000000)
        {
            switch (policyType)
            {
            case CDL_POLICY_TYPE_INACTIVE_TIME:
            case CDL_POLICY_TYPE_ACTIVE_TIME:
                if (policyField == 0x00 || policyField == 0x03 || policyField == 0x04 || policyField == 0x05 ||
                    policyField == 0x0D || policyField == 0x0F)
                {
                    return true;
                }
                break;

            case CDL_POLICY_TYPE_TOTAL_TIME:
                if (policyField <= 0x02 || policyField == 0x03 || policyField == 0x04 || policyField == 0x05 ||
                    policyField == 0x0D || policyField == 0x0F)
                {
                    return true;
                }
                break;

            default:
                return false;
                break;
            }
        }
        else if (currentCDLFeatureVersion == 0x02000000)
        {
            switch (policyField)
            {
            case 0x03:
                if (policySupportedDescriptor & M_BitN16(3))
                    return true;
                break;

            case 0x04:
                if (policySupportedDescriptor & M_BitN16(4))
                    return true;
                break;

            case 0x05:
                if (policySupportedDescriptor & M_BitN16(5))
                    return true;
                break;

            case 0x06:
                if (policySupportedDescriptor & M_BitN16(6))
                    return true;
                break;

            case 0x07:
                if (policySupportedDescriptor & M_BitN16(7))
                    return true;
                break;

            case 0x08:
                if (policySupportedDescriptor & M_BitN16(8))
                    return true;
                break;

            case 0x09:
                if (policySupportedDescriptor & M_BitN16(9))
                    return true;
                break;

            case 0x0A:
                if (policySupportedDescriptor & M_BitN16(10))
                    return true;
                break;

            case 0x0B:
                if (policySupportedDescriptor & M_BitN16(11))
                    return true;
                break;

            case 0x0C:
                if (policySupportedDescriptor & M_BitN16(12))
                    return true;
                break;

            case 0x0D:
                if (policySupportedDescriptor & M_BitN16(13))
                    return true;
                break;

            case 0x0E:
                if (policySupportedDescriptor & M_BitN16(14))
                    return true;
                break;

            case 0x0F:
                if (policySupportedDescriptor & M_BitN16(15))
                    return true;
                break;

            default:
                break;
            }
        }
    }
    else if (driveType == SCSI_DRIVE)
    {
        switch (policyType)
        {
        case CDL_POLICY_TYPE_INACTIVE_TIME:
            if (policyField == 0x00 || policyField == 0x03 || policyField == 0x04 || policyField == 0x05 ||
                policyField == 0x0D || policyField == 0x0F)
            {
                return true;
            }
            break;

        case CDL_POLICY_TYPE_ACTIVE_TIME:
            if (policyField == 0x00 || policyField == 0x03 || policyField == 0x04 || policyField == 0x05 ||
                policyField == 0x0D || policyField == 0x0E || policyField == 0x0F)
            {
                return true;
            }
            break;

        case CDL_POLICY_TYPE_COMMAND_DURATION_GUIDELINE:
            if (policyField <= 0x02 || policyField == 0x03 || policyField == 0x04 || policyField == 0x05 ||
                policyField == 0x0D || policyField == 0x0F)
            {
                return true;
            }
            break;

        default:
            return false;
            break;
        }
    }

    return false;
}

static eReturnValues is_Valid_ATA_Config_CDL_Settings(tCDLSettings* cdlSettings)
{
    eReturnValues ret = SUCCESS;
    if (!cdlSettings)
    {
        return BAD_PARAMETER;
    }

    // check if valid performanceVsCommandCompletion
    if (is_Performance_Versus_Command_Completion_Supported(cdlSettings) &&
        cdlSettings->ataCDLSettings.performanceVsCommandCompletion > 0x0C)
    {
        printf("Invalid Entry for \"Performance Versus Command Completion\".\n");
        printf("Accepted values are in range of 0x00 - 0x0C. Provided value : 0x%02" PRIX8 "\n",
               cdlSettings->ataCDLSettings.performanceVsCommandCompletion);
        return VALIDATION_FAILURE;
    }

    // check fields for each read descriptor
    for (uint8_t descriptorIndex = 0; descriptorIndex < MAX_CDL_READ_DESCRIPTOR; descriptorIndex++)
    {
        // if Active Time Policy Type is supported, then check the user provided field value for validation
        if (!is_Valid_Supported_Policy(ATA_DRIVE, CDL_POLICY_TYPE_ACTIVE_TIME,
                                       cdlSettings->ataCDLSettings.activeTimePolicySupportedDescriptor,
                                       cdlSettings->ataCDLSettings.cdlReadDescriptor[descriptorIndex].activeTimePolicy))
        {
            DECLARE_ZERO_INIT_ARRAY(char, policyString, SUPPORTED_POLICY_STRING_LENGTH);
            get_Supported_Policy_String(ATA_DRIVE, CDL_POLICY_TYPE_ACTIVE_TIME,
                                        cdlSettings->ataCDLSettings.activeTimePolicySupportedDescriptor, policyString);
            printf("Invalid Entry for \"Active Time Policy\" for Descriptor R%" PRIu8 ".\n", (descriptorIndex + 1));
            printf("Accepted values are [%s]. Provided value : 0x%02" PRIX8 "\n", policyString,
                   cdlSettings->ataCDLSettings.cdlReadDescriptor[descriptorIndex].activeTimePolicy);
            return VALIDATION_FAILURE;
        }

        // if Inactive Time Policy Type is supported, then check the user provided field value for validation
        if (!is_Valid_Supported_Policy(
                ATA_DRIVE, CDL_POLICY_TYPE_INACTIVE_TIME,
                cdlSettings->ataCDLSettings.inactiveTimePolicySupportedDescriptor,
                cdlSettings->ataCDLSettings.cdlReadDescriptor[descriptorIndex].inactiveTimePolicy))
        {
            DECLARE_ZERO_INIT_ARRAY(char, policyString, SUPPORTED_POLICY_STRING_LENGTH);
            get_Supported_Policy_String(ATA_DRIVE, CDL_POLICY_TYPE_INACTIVE_TIME,
                                        cdlSettings->ataCDLSettings.inactiveTimePolicySupportedDescriptor,
                                        policyString);
            printf("Invalid Entry for \"Inactive Time Policy\" for Descriptor R%" PRIu8 ".\n", (descriptorIndex + 1));
            printf("Accepted values are [%s]. Provided value : 0x%02" PRIX8 "\n", policyString,
                   cdlSettings->ataCDLSettings.cdlReadDescriptor[descriptorIndex].inactiveTimePolicy);
            return VALIDATION_FAILURE;
        }

        // if Total Time Policy Type is supported, then check the user provided field value for validation
        if (is_Total_Time_Policy_Type_Supported(cdlSettings) &&
            !is_Valid_Supported_Policy(ATA_DRIVE, CDL_POLICY_TYPE_TOTAL_TIME,
                                       cdlSettings->ataCDLSettings.totalTimePolicySupportedDescriptor,
                                       cdlSettings->ataCDLSettings.cdlReadDescriptor[descriptorIndex].totalTimePolicy))
        {
            DECLARE_ZERO_INIT_ARRAY(char, policyString, SUPPORTED_POLICY_STRING_LENGTH);
            get_Supported_Policy_String(ATA_DRIVE, CDL_POLICY_TYPE_TOTAL_TIME,
                                        cdlSettings->ataCDLSettings.totalTimePolicySupportedDescriptor, policyString);
            printf("Invalid Entry for \"Total Time Policy\" for Descriptor R%" PRIu8 ".\n", (descriptorIndex + 1));
            printf("Accepted values are [%s]. Provided value : 0x%02" PRIX8 "\n", policyString,
                   cdlSettings->ataCDLSettings.cdlReadDescriptor[descriptorIndex].totalTimePolicy);
            return VALIDATION_FAILURE;
        }
    }

    // check fields for each write descriptor
    for (uint8_t descriptorIndex = 0; descriptorIndex < MAX_CDL_WRITE_DESCRIPTOR; descriptorIndex++)
    {
        // if Active Time Policy Type is supported, then check the user provided field value for validation
        if (!is_Valid_Supported_Policy(
                ATA_DRIVE, CDL_POLICY_TYPE_ACTIVE_TIME, cdlSettings->ataCDLSettings.activeTimePolicySupportedDescriptor,
                cdlSettings->ataCDLSettings.cdlWriteDescriptor[descriptorIndex].activeTimePolicy))
        {
            DECLARE_ZERO_INIT_ARRAY(char, policyString, SUPPORTED_POLICY_STRING_LENGTH);
            get_Supported_Policy_String(ATA_DRIVE, CDL_POLICY_TYPE_ACTIVE_TIME,
                                        cdlSettings->ataCDLSettings.activeTimePolicySupportedDescriptor, policyString);
            printf("Invalid Entry for \"Active Time Policy\" for Descriptor W%" PRIu8 ".\n", (descriptorIndex + 1));
            printf("Accepted values are [%s]. Provided value : 0x%02" PRIX8 "\n", policyString,
                   cdlSettings->ataCDLSettings.cdlWriteDescriptor[descriptorIndex].activeTimePolicy);
            return VALIDATION_FAILURE;
        }

        // if Inactive Time Policy Type is supported, then check the user provided field value for validation
        if (!is_Valid_Supported_Policy(
                ATA_DRIVE, CDL_POLICY_TYPE_INACTIVE_TIME,
                cdlSettings->ataCDLSettings.inactiveTimePolicySupportedDescriptor,
                cdlSettings->ataCDLSettings.cdlWriteDescriptor[descriptorIndex].inactiveTimePolicy))
        {
            DECLARE_ZERO_INIT_ARRAY(char, policyString, SUPPORTED_POLICY_STRING_LENGTH);
            get_Supported_Policy_String(ATA_DRIVE, CDL_POLICY_TYPE_INACTIVE_TIME,
                                        cdlSettings->ataCDLSettings.inactiveTimePolicySupportedDescriptor,
                                        policyString);
            printf("Invalid Entry for \"Inactive Time Policy\" for Descriptor W%" PRIu8 ".\n", (descriptorIndex + 1));
            printf("Accepted values are [%s]. Provided value : 0x%02" PRIX8 "\n", policyString,
                   cdlSettings->ataCDLSettings.cdlWriteDescriptor[descriptorIndex].inactiveTimePolicy);
            return VALIDATION_FAILURE;
        }

        // if Total Time Policy Type is supported, then check the user provided field value for validation
        if (is_Total_Time_Policy_Type_Supported(cdlSettings) &&
            !is_Valid_Supported_Policy(ATA_DRIVE, CDL_POLICY_TYPE_TOTAL_TIME,
                                       cdlSettings->ataCDLSettings.totalTimePolicySupportedDescriptor,
                                       cdlSettings->ataCDLSettings.cdlWriteDescriptor[descriptorIndex].totalTimePolicy))
        {
            DECLARE_ZERO_INIT_ARRAY(char, policyString, SUPPORTED_POLICY_STRING_LENGTH);
            get_Supported_Policy_String(ATA_DRIVE, CDL_POLICY_TYPE_TOTAL_TIME,
                                        cdlSettings->ataCDLSettings.totalTimePolicySupportedDescriptor, policyString);
            printf("Invalid Entry for \"Total Time Policy\" for Descriptor W%" PRIu8 ".\n", (descriptorIndex + 1));
            printf("Accepted values are [%s]. Provided value : 0x%02" PRIX8 "\n", policyString,
                   cdlSettings->ataCDLSettings.cdlWriteDescriptor[descriptorIndex].totalTimePolicy);
            return VALIDATION_FAILURE;
        }
    }

    return ret;
}

static eReturnValues is_Valid_SCSI_Config_CDL_Settings(tCDLSettings* cdlSettings)
{
    eReturnValues ret = SUCCESS;
    if (!cdlSettings)
    {
        return BAD_PARAMETER;
    }

    // check if valid performanceVsCommandDurationGuidelines
    if (cdlSettings->scsiCDLSettings.performanceVsCommandDurationGuidelines > 0x0C)
    {
        printf("Invalid Entry for \"Performance Versus Command Duration Guidelines\".\n");
        printf("Accepted values are in range of 0x00 - 0x0C. Provided value : 0x%02" PRIX8 "\n",
               cdlSettings->scsiCDLSettings.performanceVsCommandDurationGuidelines);
        return VALIDATION_FAILURE;
    }

    // check fields for each T2A descriptor
    for (uint8_t descriptorIndex = 0; descriptorIndex < MAX_CDL_T2A_DESCRIPTOR; descriptorIndex++)
    {
        // check the user provided field value for validation
        if (!(cdlSettings->scsiCDLSettings.cdlT2ADescriptor[descriptorIndex].timeFieldUnitType ==
                  CDL_TIME_FIELD_UNIT_TYPE_MICROSECONDS ||
              cdlSettings->scsiCDLSettings.cdlT2ADescriptor[descriptorIndex].timeFieldUnitType ==
                  CDL_TIME_FIELD_UNIT_TYPE_500_NANOSECONDS ||
              cdlSettings->scsiCDLSettings.cdlT2ADescriptor[descriptorIndex].timeFieldUnitType ==
                  CDL_TIME_FIELD_UNIT_TYPE_10_MILLISECONDS ||
              cdlSettings->scsiCDLSettings.cdlT2ADescriptor[descriptorIndex].timeFieldUnitType ==
                  CDL_TIME_FIELD_UNIT_TYPE_500_MILLISECONDS ||
              cdlSettings->scsiCDLSettings.cdlT2ADescriptor[descriptorIndex].timeFieldUnitType ==
                  CDL_TIME_FIELD_UNIT_TYPE_NO_VALUE))
        {
            printf("Invalid Entry for \"Time Feild Unit\" for T2A Descriptor %" PRIu8 ".\n", (descriptorIndex + 1));
            printf("Accepted values are [0x00,0x06,0x08,0x0A,0x0E]. Provided value : 0x%02" PRIX8 "\n",
                   cdlSettings->scsiCDLSettings.cdlT2ADescriptor[descriptorIndex].activeTimePolicy);
            return VALIDATION_FAILURE;
        }

        // check the user provided field value for validation
        if (!is_Valid_Supported_Policy(SCSI_DRIVE, CDL_POLICY_TYPE_ACTIVE_TIME, 0,
                                       cdlSettings->scsiCDLSettings.cdlT2ADescriptor[descriptorIndex].activeTimePolicy))
        {
            DECLARE_ZERO_INIT_ARRAY(char, policyString, SUPPORTED_POLICY_STRING_LENGTH);
            get_Supported_Policy_String(SCSI_DRIVE, CDL_POLICY_TYPE_ACTIVE_TIME, 0, policyString);
            printf("Invalid Entry for \"Active Time Policy\" for T2A Descriptor %" PRIu8 ".\n", (descriptorIndex + 1));
            printf("Accepted values are [%s]. Provided value : 0x%02" PRIX8 "\n", policyString,
                   cdlSettings->scsiCDLSettings.cdlT2ADescriptor[descriptorIndex].activeTimePolicy);
            return VALIDATION_FAILURE;
        }

        // check the user provided field value for validation
        if (!is_Valid_Supported_Policy(
                SCSI_DRIVE, CDL_POLICY_TYPE_INACTIVE_TIME, 0,
                cdlSettings->scsiCDLSettings.cdlT2ADescriptor[descriptorIndex].inactiveTimePolicy))
        {
            DECLARE_ZERO_INIT_ARRAY(char, policyString, SUPPORTED_POLICY_STRING_LENGTH);
            get_Supported_Policy_String(SCSI_DRIVE, CDL_POLICY_TYPE_INACTIVE_TIME, 0, policyString);
            printf("Invalid Entry for \"Inactive Time Policy\" for T2A Descriptor %" PRIu8 ".\n",
                   (descriptorIndex + 1));
            printf("Accepted values are [%s]. Provided value : 0x%02" PRIX8 "\n", policyString,
                   cdlSettings->scsiCDLSettings.cdlT2ADescriptor[descriptorIndex].inactiveTimePolicy);
            return VALIDATION_FAILURE;
        }

        // check the user provided field value for validation
        if (!is_Valid_Supported_Policy(
                SCSI_DRIVE, CDL_POLICY_TYPE_COMMAND_DURATION_GUIDELINE, 0,
                cdlSettings->scsiCDLSettings.cdlT2ADescriptor[descriptorIndex].CommandDurationGuidelinePolicy))
        {
            DECLARE_ZERO_INIT_ARRAY(char, policyString, SUPPORTED_POLICY_STRING_LENGTH);
            get_Supported_Policy_String(SCSI_DRIVE, CDL_POLICY_TYPE_COMMAND_DURATION_GUIDELINE, 0, policyString);
            printf("Invalid Entry for \"Command Duration Guideline Policy\" for T2A Descriptor %" PRIu8 ".\n",
                   (descriptorIndex + 1));
            printf("Accepted values are [%s]. Provided value : 0x%02" PRIX8 "\n", policyString,
                   cdlSettings->scsiCDLSettings.cdlT2ADescriptor[descriptorIndex].CommandDurationGuidelinePolicy);
            return VALIDATION_FAILURE;
        }
    }

    // check fields for each T2B descriptor
    for (uint8_t descriptorIndex = 0; descriptorIndex < MAX_CDL_T2B_DESCRIPTOR; descriptorIndex++)
    {
        // check the user provided field value for validation
        if (!(cdlSettings->scsiCDLSettings.cdlT2BDescriptor[descriptorIndex].timeFieldUnitType ==
                  CDL_TIME_FIELD_UNIT_TYPE_MICROSECONDS ||
              cdlSettings->scsiCDLSettings.cdlT2BDescriptor[descriptorIndex].timeFieldUnitType ==
                  CDL_TIME_FIELD_UNIT_TYPE_500_NANOSECONDS ||
              cdlSettings->scsiCDLSettings.cdlT2BDescriptor[descriptorIndex].timeFieldUnitType ==
                  CDL_TIME_FIELD_UNIT_TYPE_10_MILLISECONDS ||
              cdlSettings->scsiCDLSettings.cdlT2BDescriptor[descriptorIndex].timeFieldUnitType ==
                  CDL_TIME_FIELD_UNIT_TYPE_500_MILLISECONDS ||
              cdlSettings->scsiCDLSettings.cdlT2BDescriptor[descriptorIndex].timeFieldUnitType ==
                  CDL_TIME_FIELD_UNIT_TYPE_NO_VALUE))
        {
            printf("Invalid Entry for \"Time Feild Unit\" for T2A Descriptor %" PRIu8 ".\n", (descriptorIndex + 1));
            printf("Accepted values are [0x00,0x06,0x08,0x0A,0x0E]. Provided value : 0x%02" PRIX8 "\n",
                   cdlSettings->scsiCDLSettings.cdlT2BDescriptor[descriptorIndex].activeTimePolicy);
            return VALIDATION_FAILURE;
        }

        // check the user provided field value for validation
        if (!is_Valid_Supported_Policy(SCSI_DRIVE, CDL_POLICY_TYPE_ACTIVE_TIME, 0,
                                       cdlSettings->scsiCDLSettings.cdlT2BDescriptor[descriptorIndex].activeTimePolicy))
        {
            DECLARE_ZERO_INIT_ARRAY(char, policyString, SUPPORTED_POLICY_STRING_LENGTH);
            get_Supported_Policy_String(SCSI_DRIVE, CDL_POLICY_TYPE_ACTIVE_TIME, 0, policyString);
            printf("Invalid Entry for \"Active Time Policy\" for T2B Descriptor %" PRIu8 ".\n", (descriptorIndex + 1));
            printf("Accepted values are [%s]. Provided value : 0x%02" PRIX8 "\n", policyString,
                   cdlSettings->scsiCDLSettings.cdlT2BDescriptor[descriptorIndex].activeTimePolicy);
            return VALIDATION_FAILURE;
        }

        // check the user provided field value for validation
        if (!is_Valid_Supported_Policy(
                SCSI_DRIVE, CDL_POLICY_TYPE_INACTIVE_TIME, 0,
                cdlSettings->scsiCDLSettings.cdlT2BDescriptor[descriptorIndex].inactiveTimePolicy))
        {
            DECLARE_ZERO_INIT_ARRAY(char, policyString, SUPPORTED_POLICY_STRING_LENGTH);
            get_Supported_Policy_String(SCSI_DRIVE, CDL_POLICY_TYPE_INACTIVE_TIME, 0, policyString);
            printf("Invalid Entry for \"Inactive Time Policy\" for T2B Descriptor %" PRIu8 ".\n",
                   (descriptorIndex + 1));
            printf("Accepted values are [%s]. Provided value : 0x%02" PRIX8 "\n", policyString,
                   cdlSettings->scsiCDLSettings.cdlT2BDescriptor[descriptorIndex].inactiveTimePolicy);
            return VALIDATION_FAILURE;
        }

        // check the user provided field value for validation
        if (!is_Valid_Supported_Policy(
                SCSI_DRIVE, CDL_POLICY_TYPE_COMMAND_DURATION_GUIDELINE, 0,
                cdlSettings->scsiCDLSettings.cdlT2BDescriptor[descriptorIndex].CommandDurationGuidelinePolicy))
        {
            DECLARE_ZERO_INIT_ARRAY(char, policyString, SUPPORTED_POLICY_STRING_LENGTH);
            get_Supported_Policy_String(SCSI_DRIVE, CDL_POLICY_TYPE_COMMAND_DURATION_GUIDELINE, 0, policyString);
            printf("Invalid Entry for \"Command Duration Guideline Policy\" for T2B Descriptor %" PRIu8 ".\n",
                   (descriptorIndex + 1));
            printf("Accepted values are [%s]. Provided value : 0x%02" PRIX8 "\n", policyString,
                   cdlSettings->scsiCDLSettings.cdlT2BDescriptor[descriptorIndex].CommandDurationGuidelinePolicy);
            return VALIDATION_FAILURE;
        }
    }

    return ret;
}

eReturnValues is_Valid_Config_CDL_Settings(tDevice* device, tCDLSettings* cdlSettings)
{
    eReturnValues ret = NOT_SUPPORTED;
    if (!cdlSettings)
    {
        return BAD_PARAMETER;
    }

    if (device->drive_info.drive_type == ATA_DRIVE)
    {
        ret = is_Valid_ATA_Config_CDL_Settings(cdlSettings);
    }
    else if (device->drive_info.drive_type == SCSI_DRIVE)
    {
        ret = is_Valid_SCSI_Config_CDL_Settings(cdlSettings);
    }

    return ret;
}

bool is_Total_Time_Policy_Type_Supported(tCDLSettings* cdlSettings)
{
    uint32_t currentCDLFeatureVersion =
        M_BytesTo4ByteValue(CDL_FEATURE_MAJOR_VERSION, CDL_FEATURE_MINOR_VERSION, CDL_FEATURE_PATCH_VERSION, 0);
    if (currentCDLFeatureVersion == 0x01000000)
        return cdlSettings->ataCDLSettings.isCommandDurationGuidelineSupported;
    else if (currentCDLFeatureVersion == 0x02000000)
    {
        uint32_t policySupportedDescriptor = cdlSettings->ataCDLSettings.totalTimePolicySupportedDescriptor;
        // bit 0:2 should be 001b, and rest of the bits will report the supported policies
        // so, if any of the bit is set, then atleast one policy value is supported for this type of policy field
        if (((policySupportedDescriptor & M_BitN16(0)) && !(policySupportedDescriptor & M_BitN16(1)) &&
             !(policySupportedDescriptor & M_BitN16(2))) &&
            (policySupportedDescriptor & M_BitN16(3) || policySupportedDescriptor & M_BitN16(4) ||
             policySupportedDescriptor & M_BitN16(5) || policySupportedDescriptor & M_BitN16(6) ||
             policySupportedDescriptor & M_BitN16(7) || policySupportedDescriptor & M_BitN16(8) ||
             policySupportedDescriptor & M_BitN16(9) || policySupportedDescriptor & M_BitN16(10) ||
             policySupportedDescriptor & M_BitN16(11) || policySupportedDescriptor & M_BitN16(12) ||
             policySupportedDescriptor & M_BitN16(13) || policySupportedDescriptor & M_BitN16(14) ||
             policySupportedDescriptor & M_BitN16(15)))
            return true;
    }

    return false;
}

bool is_Performance_Versus_Command_Completion_Supported(tCDLSettings* cdlSettings)
{
    uint32_t currentCDLFeatureVersion =
        M_BytesTo4ByteValue(CDL_FEATURE_MAJOR_VERSION, CDL_FEATURE_MINOR_VERSION, CDL_FEATURE_PATCH_VERSION, 0);
    if (currentCDLFeatureVersion == 0x01000000)
        return cdlSettings->ataCDLSettings.isCommandDurationGuidelineSupported;
    else if (currentCDLFeatureVersion == 0x02000000)
        return true;
    return false;
}

void get_Supported_Policy_String(eDriveType     driveType,
                                 eCDLPolicyType policyType,
                                 uint16_t       policySupportedDescriptor,
                                 char*          policyString)
{
    uint32_t currentCDLFeatureVersion =
        M_BytesTo4ByteValue(CDL_FEATURE_MAJOR_VERSION, CDL_FEATURE_MINOR_VERSION, CDL_FEATURE_PATCH_VERSION, 0);
    if (driveType == ATA_DRIVE)
    {
        if (currentCDLFeatureVersion == 0x01000000)
        {
            switch (policyType)
            {
            case CDL_POLICY_TYPE_INACTIVE_TIME:
            case CDL_POLICY_TYPE_ACTIVE_TIME:
                snprintf_err_handle(policyString, SUPPORTED_POLICY_STRING_LENGTH, "%s",
                                    "0x00,0x03,0x04,0x05,0x0D,0x0F");
                break;

            case CDL_POLICY_TYPE_TOTAL_TIME:
                snprintf_err_handle(policyString, SUPPORTED_POLICY_STRING_LENGTH, "%s",
                                    "0x00,0x01,0x02,0x03,0x04,0x05,0x0D,0x0F");
                break;

            default:
                break;
            }
        }
        else if (currentCDLFeatureVersion == 0x02000000)
        {
            if (policySupportedDescriptor & M_BitN16(3))
            {
                if (safe_strlen(policyString) > 0)
                    safe_strcat(policyString, SUPPORTED_POLICY_STRING_LENGTH, ",");
                safe_strcat(policyString, SUPPORTED_POLICY_STRING_LENGTH, "0x03");
            }
            if (policySupportedDescriptor & M_BitN16(4))
            {
                if (safe_strlen(policyString) > 0)
                    safe_strcat(policyString, SUPPORTED_POLICY_STRING_LENGTH, ",");
                safe_strcat(policyString, SUPPORTED_POLICY_STRING_LENGTH, "0x04");
            }
            if (policySupportedDescriptor & M_BitN16(5))
            {
                if (safe_strlen(policyString) > 0)
                    safe_strcat(policyString, SUPPORTED_POLICY_STRING_LENGTH, ",");
                safe_strcat(policyString, SUPPORTED_POLICY_STRING_LENGTH, "0x05");
            }
            if (policySupportedDescriptor & M_BitN16(6))
            {
                if (safe_strlen(policyString) > 0)
                    safe_strcat(policyString, SUPPORTED_POLICY_STRING_LENGTH, ",");
                safe_strcat(policyString, SUPPORTED_POLICY_STRING_LENGTH, "0x06");
            }
            if (policySupportedDescriptor & M_BitN16(7))
            {
                if (safe_strlen(policyString) > 0)
                    safe_strcat(policyString, SUPPORTED_POLICY_STRING_LENGTH, ",");
                safe_strcat(policyString, SUPPORTED_POLICY_STRING_LENGTH, "0x07");
            }
            if (policySupportedDescriptor & M_BitN16(8))
            {
                if (safe_strlen(policyString) > 0)
                    safe_strcat(policyString, SUPPORTED_POLICY_STRING_LENGTH, ",");
                safe_strcat(policyString, SUPPORTED_POLICY_STRING_LENGTH, "0x08");
            }
            if (policySupportedDescriptor & M_BitN16(9))
            {
                if (safe_strlen(policyString) > 0)
                    safe_strcat(policyString, SUPPORTED_POLICY_STRING_LENGTH, ",");
                safe_strcat(policyString, SUPPORTED_POLICY_STRING_LENGTH, "0x09");
            }
            if (policySupportedDescriptor & M_BitN16(10))
            {
                if (safe_strlen(policyString) > 0)
                    safe_strcat(policyString, SUPPORTED_POLICY_STRING_LENGTH, ",");
                safe_strcat(policyString, SUPPORTED_POLICY_STRING_LENGTH, "0x0A");
            }
            if (policySupportedDescriptor & M_BitN16(11))
            {
                if (safe_strlen(policyString) > 0)
                    safe_strcat(policyString, SUPPORTED_POLICY_STRING_LENGTH, ",");
                safe_strcat(policyString, SUPPORTED_POLICY_STRING_LENGTH, "0x0B");
            }
            if (policySupportedDescriptor & M_BitN16(12))
            {
                if (safe_strlen(policyString) > 0)
                    safe_strcat(policyString, SUPPORTED_POLICY_STRING_LENGTH, ",");
                safe_strcat(policyString, SUPPORTED_POLICY_STRING_LENGTH, "0x0C");
            }
            if (policySupportedDescriptor & M_BitN16(13))
            {
                if (safe_strlen(policyString) > 0)
                    safe_strcat(policyString, SUPPORTED_POLICY_STRING_LENGTH, ",");
                safe_strcat(policyString, SUPPORTED_POLICY_STRING_LENGTH, "0x0D");
            }
            if (policySupportedDescriptor & M_BitN16(14))
            {
                if (safe_strlen(policyString) > 0)
                    safe_strcat(policyString, SUPPORTED_POLICY_STRING_LENGTH, ",");
                safe_strcat(policyString, SUPPORTED_POLICY_STRING_LENGTH, "0x0E");
            }
            if (policySupportedDescriptor & M_BitN16(15))
            {
                if (safe_strlen(policyString) > 0)
                    safe_strcat(policyString, SUPPORTED_POLICY_STRING_LENGTH, ",");
                safe_strcat(policyString, SUPPORTED_POLICY_STRING_LENGTH, "0x0F");
            }
        }
    }
    else if (driveType == SCSI_DRIVE)
    {
        switch (policyType)
        {
        case CDL_POLICY_TYPE_INACTIVE_TIME:
            snprintf_err_handle(policyString, SUPPORTED_POLICY_STRING_LENGTH, "%s", "0x00,0x03,0x04,0x05,0x0D,0x0F");
            break;

        case CDL_POLICY_TYPE_ACTIVE_TIME:
            snprintf_err_handle(policyString, SUPPORTED_POLICY_STRING_LENGTH, "%s",
                                "0x00,0x03,0x04,0x05,0x0D,0x0E,0x0F");
            break;

        case CDL_POLICY_TYPE_COMMAND_DURATION_GUIDELINE:
            snprintf_err_handle(policyString, SUPPORTED_POLICY_STRING_LENGTH, "%s",
                                "0x00,0x01,0x02,0x03,0x04,0x05,0x0D,0x0F");
            break;

        default:
            break;
        }
    }
}

uint32_t convert_CDL_TimeField_To_Microseconds(eCDLTimeFieldUnitType unitType, uint32_t value)
{
    uint32_t convertedValue = value;
    switch (unitType)
    {
    case CDL_TIME_FIELD_UNIT_TYPE_MILLISECONDS:
        convertedValue = value * 1000;
        break;

    case CDL_TIME_FIELD_UNIT_TYPE_SECONDS:
        convertedValue = value * 1000000;
        break;

    case CDL_TIME_FIELD_UNIT_TYPE_500_NANOSECONDS:
        convertedValue = C_CAST(uint32_t, value * 0.5);
        break;

    case CDL_TIME_FIELD_UNIT_TYPE_10_MILLISECONDS:
        convertedValue = value * 10000;
        break;

    case CDL_TIME_FIELD_UNIT_TYPE_500_MILLISECONDS:
        convertedValue = value * 500000;
        break;

    case CDL_TIME_FIELD_UNIT_TYPE_NO_VALUE:
        convertedValue = 0;
        break;

    default:
        break;
    }

    return convertedValue;
}
