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
// \file cdl.c
// \brief This file defines the functions related to displaying and changing CDL settings

#include "common_types.h"
#include "io_utils.h"
#include "cdl.h"
#include "logs.h"

#define MAX_CDL_PERFORMANCE_VS_CMD_COMPLETION_STATUS_STRING_LENGTH  16
#define MAX_POLICY_STRING_LENGHT                                    130

#define CDL_DESCRIPTOR_LENGTH                                       32
#define CDL_READ_DESCRIPTOR_OFFSET                                  64
#define CDL_WRITE_DESCRIPTOR_OFFSET                                 288

static eReturnValues show_ATA_CDL_Settings(tDevice *device)
{
    eReturnValues ret = NOT_SUPPORTED;
    tCDLSettings cdlSettings;
    memset(&cdlSettings, 0, sizeof(tCDLSettings));

    uint8_t *logBuffer = safe_calloc_aligned(LEGACY_DRIVE_SEC_SIZE, sizeof(uint8_t), device->os_info.minimumAlignment);
    if (!logBuffer)
    {
        ret = MEMORY_FAILURE;
        return ret;
    }

    //read the logaddress 0x30, logpage 0x03
    memset(logBuffer, 0, LEGACY_DRIVE_SEC_SIZE);
    if (SUCCESS == send_ATA_Read_Log_Ext_Cmd(device, ATA_LOG_IDENTIFY_DEVICE_DATA, 0x03, logBuffer, LEGACY_DRIVE_SEC_SIZE, 0))
    {
        uint64_t cdlLimitSupported = M_BytesTo8ByteValue(logBuffer[175], logBuffer[174], logBuffer[173], logBuffer[172], logBuffer[171], logBuffer[170], logBuffer[169], logBuffer[168]);
        if ((cdlLimitSupported & ATA_ID_DATA_QWORD_VALID_BIT))
        {
            if (cdlLimitSupported & BIT0)
                cdlSettings.isSupported = true;
            if (cdlLimitSupported & BIT1)
                cdlSettings.isCommandDurationGuidelineSupported = true;
        }
        uint64_t cdlMinimumTimeLimit = M_BytesTo8ByteValue(logBuffer[183], logBuffer[182], logBuffer[181], logBuffer[180], logBuffer[179], logBuffer[178], logBuffer[177], logBuffer[176]);
        if (cdlMinimumTimeLimit & ATA_ID_DATA_QWORD_VALID_BIT)
        {
            cdlSettings.minimumTimeLimit = M_DoubleWord0(cdlMinimumTimeLimit);
        }
        uint64_t cdlMaximumTimeLimit = M_BytesTo8ByteValue(logBuffer[191], logBuffer[190], logBuffer[189], logBuffer[188], logBuffer[187], logBuffer[186], logBuffer[185], logBuffer[184]);
        if (cdlMaximumTimeLimit & ATA_ID_DATA_QWORD_VALID_BIT)
        {
            cdlSettings.maximumTimeLimit = M_DoubleWord0(cdlMaximumTimeLimit);
        }
    }
    else
    {
        safe_free_aligned(&logBuffer);
        return NOT_SUPPORTED;
    }

    //read the logaddress 0x30, logpage 0x04
    memset(logBuffer, 0, LEGACY_DRIVE_SEC_SIZE);
    ret = send_ATA_Read_Log_Ext_Cmd(device, ATA_LOG_IDENTIFY_DEVICE_DATA, 0x04, logBuffer, LEGACY_DRIVE_SEC_SIZE, 0);
    if (SUCCESS == ret)
    {
        uint64_t currentSettings = M_BytesTo8ByteValue(logBuffer[15], logBuffer[14], logBuffer[13], logBuffer[12], logBuffer[11], logBuffer[10], logBuffer[9], logBuffer[8]);
        if ((currentSettings & ATA_ID_DATA_QWORD_VALID_BIT)
            && (currentSettings & BIT21))
        {
            cdlSettings.isEnabled = true;
        }
    }
    else
    {
        printf("Fail read log EXT command\n");
        safe_free_aligned(&logBuffer);
        return ret;
    }

    //read the logaddress 0x18, logpage 0x00
    memset(logBuffer, 0, LEGACY_DRIVE_SEC_SIZE);
    ret = send_ATA_Read_Log_Ext_Cmd(device, ATA_LOG_COMMAND_DURATION_LIMITS_LOG, 0x00, logBuffer, LEGACY_DRIVE_SEC_SIZE, 0);
    if (SUCCESS == ret)
    {
        cdlSettings.performanceVsCommandCompletion = M_Nibble0(logBuffer[0]);
        uint8_t *cdlReadDescriptorBuffer = logBuffer + CDL_READ_DESCRIPTOR_OFFSET;
        for (uint8_t descriptorIndex = 0; descriptorIndex < MAX_CDL_READ_DESCRIPTOR; descriptorIndex++)
        {
            uint32_t commandLimitDescriptor = M_BytesTo4ByteValue(cdlReadDescriptorBuffer[(descriptorIndex * CDL_DESCRIPTOR_LENGTH) + 3],
                cdlReadDescriptorBuffer[(descriptorIndex * CDL_DESCRIPTOR_LENGTH) + 2],
                cdlReadDescriptorBuffer[(descriptorIndex * CDL_DESCRIPTOR_LENGTH) + 1],
                cdlReadDescriptorBuffer[(descriptorIndex * CDL_DESCRIPTOR_LENGTH) + 0]);
            cdlSettings.cdlReadDescriptor[descriptorIndex].commandDurationGuidelinePolicy = M_Nibble0(M_Byte0(commandLimitDescriptor));
            cdlSettings.cdlReadDescriptor[descriptorIndex].activeTimePolicy = M_Nibble1(M_Byte0(commandLimitDescriptor));
            cdlSettings.cdlReadDescriptor[descriptorIndex].inactiveTimePolicy = M_Nibble2(M_Byte1(commandLimitDescriptor));
            cdlSettings.cdlReadDescriptor[descriptorIndex].activeTime = M_BytesTo4ByteValue(cdlReadDescriptorBuffer[(descriptorIndex * CDL_DESCRIPTOR_LENGTH) + 7],
                cdlReadDescriptorBuffer[(descriptorIndex * CDL_DESCRIPTOR_LENGTH) + 6],
                cdlReadDescriptorBuffer[(descriptorIndex * CDL_DESCRIPTOR_LENGTH) + 5],
                cdlReadDescriptorBuffer[(descriptorIndex * CDL_DESCRIPTOR_LENGTH) + 4]);
            cdlSettings.cdlReadDescriptor[descriptorIndex].inactiveTime = M_BytesTo4ByteValue(cdlReadDescriptorBuffer[(descriptorIndex * CDL_DESCRIPTOR_LENGTH) + 11],
                cdlReadDescriptorBuffer[(descriptorIndex * CDL_DESCRIPTOR_LENGTH) + 10],
                cdlReadDescriptorBuffer[(descriptorIndex * CDL_DESCRIPTOR_LENGTH) + 9],
                cdlReadDescriptorBuffer[(descriptorIndex * CDL_DESCRIPTOR_LENGTH) + 8]);
            cdlSettings.cdlReadDescriptor[descriptorIndex].commandDurationGuideline = M_BytesTo4ByteValue(cdlReadDescriptorBuffer[(descriptorIndex * CDL_DESCRIPTOR_LENGTH) + 19],
                cdlReadDescriptorBuffer[(descriptorIndex * CDL_DESCRIPTOR_LENGTH) + 18],
                cdlReadDescriptorBuffer[(descriptorIndex * CDL_DESCRIPTOR_LENGTH) + 17],
                cdlReadDescriptorBuffer[(descriptorIndex * CDL_DESCRIPTOR_LENGTH) + 16]);
        }

        uint8_t *cdlWriteDescriptorBuffer = logBuffer + CDL_WRITE_DESCRIPTOR_OFFSET;
        for (uint8_t descriptorIndex = 0; descriptorIndex < MAX_CDL_WRITE_DESCRIPTOR; descriptorIndex++)
        {
            uint32_t commandLimitDescriptor = M_BytesTo4ByteValue(cdlWriteDescriptorBuffer[(descriptorIndex * CDL_DESCRIPTOR_LENGTH) + 3],
                cdlWriteDescriptorBuffer[(descriptorIndex * CDL_DESCRIPTOR_LENGTH) + 2],
                cdlWriteDescriptorBuffer[(descriptorIndex * CDL_DESCRIPTOR_LENGTH) + 1],
                cdlWriteDescriptorBuffer[(descriptorIndex * CDL_DESCRIPTOR_LENGTH) + 0]);
            cdlSettings.cdlWriteDescriptor[descriptorIndex].commandDurationGuidelinePolicy = M_Nibble0(M_Byte0(commandLimitDescriptor));
            cdlSettings.cdlWriteDescriptor[descriptorIndex].activeTimePolicy = M_Nibble1(M_Byte0(commandLimitDescriptor));
            cdlSettings.cdlWriteDescriptor[descriptorIndex].inactiveTimePolicy = M_Nibble2(M_Byte1(commandLimitDescriptor));
            cdlSettings.cdlWriteDescriptor[descriptorIndex].activeTime = M_BytesTo4ByteValue(cdlWriteDescriptorBuffer[(descriptorIndex * CDL_DESCRIPTOR_LENGTH) + 7],
                cdlWriteDescriptorBuffer[(descriptorIndex * CDL_DESCRIPTOR_LENGTH) + 6],
                cdlWriteDescriptorBuffer[(descriptorIndex * CDL_DESCRIPTOR_LENGTH) + 5],
                cdlWriteDescriptorBuffer[(descriptorIndex * CDL_DESCRIPTOR_LENGTH) + 4]);
            cdlSettings.cdlWriteDescriptor[descriptorIndex].inactiveTime = M_BytesTo4ByteValue(cdlWriteDescriptorBuffer[(descriptorIndex * CDL_DESCRIPTOR_LENGTH) + 11],
                cdlWriteDescriptorBuffer[(descriptorIndex * CDL_DESCRIPTOR_LENGTH) + 10],
                cdlWriteDescriptorBuffer[(descriptorIndex * CDL_DESCRIPTOR_LENGTH) + 9],
                cdlWriteDescriptorBuffer[(descriptorIndex * CDL_DESCRIPTOR_LENGTH) + 8]);
            cdlSettings.cdlWriteDescriptor[descriptorIndex].commandDurationGuideline = M_BytesTo4ByteValue(cdlWriteDescriptorBuffer[(descriptorIndex * CDL_DESCRIPTOR_LENGTH) + 19],
                cdlWriteDescriptorBuffer[(descriptorIndex * CDL_DESCRIPTOR_LENGTH) + 18],
                cdlWriteDescriptorBuffer[(descriptorIndex * CDL_DESCRIPTOR_LENGTH) + 17],
                cdlWriteDescriptorBuffer[(descriptorIndex * CDL_DESCRIPTOR_LENGTH) + 16]);
        }
    }
    else
    {
        printf("Fail read log EXT command\n");
        safe_free_aligned(&logBuffer);
        return ret;
    }

    //print information
    printf("\tCommand Duration Limit : Supported, %s\n", cdlSettings.isEnabled ? "Enabled" : "Disabled");
    printf("\tCommand Duration Guideline : %s\n", cdlSettings.isCommandDurationGuidelineSupported ? "Supported" : "Not Supported");
    printf("\tCommand Duration Limit Minimum Limit (us) : %" PRIu32 "\n", cdlSettings.minimumTimeLimit);
    printf("\tCommand Duration Limit Maximum Limit (us) : %" PRIu32 "\n", cdlSettings.maximumTimeLimit);
    if (cdlSettings.isCommandDurationGuidelineSupported)
    {
        DECLARE_ZERO_INIT_ARRAY(char, statusTranslation, MAX_CDL_PERFORMANCE_VS_CMD_COMPLETION_STATUS_STRING_LENGTH);
        translate_CDL_Performance_Vs_Command_Completion_Status_To_String(cdlSettings.performanceVsCommandCompletion, statusTranslation);
        printf("\tPerformance Versus Command Completion : %s\n", statusTranslation);
    }
    for (uint8_t descriptorIndex = 0; descriptorIndex < MAX_CDL_READ_DESCRIPTOR; descriptorIndex++)
    {
        printf("\tDescriptor R%" PRIu8 "\n", descriptorIndex + 1);
        DECLARE_ZERO_INIT_ARRAY(char, policyTranslation, MAX_POLICY_STRING_LENGHT);
        printf("\t\tInactive Time (us) : %" PRIu32 "\n", cdlSettings.cdlReadDescriptor[descriptorIndex].inactiveTime);
        translate_Policy_To_String(CDL_POLICY_TYPE_INACTIVE_TIME, cdlSettings.cdlReadDescriptor[descriptorIndex].inactiveTimePolicy, policyTranslation);
        printf("\t\tInactive Time Policy : %s\n", policyTranslation);
        printf("\t\tActive Time (us) : %" PRIu32 "\n", cdlSettings.cdlReadDescriptor[descriptorIndex].activeTime);
        translate_Policy_To_String(CDL_POLICY_TYPE_ACTIVE_TIME, cdlSettings.cdlReadDescriptor[descriptorIndex].activeTimePolicy, policyTranslation);
        printf("\t\tActive Time Policy : %s\n", policyTranslation);
        if (cdlSettings.isCommandDurationGuidelineSupported)
        {
            printf("\t\tCommand Duration Guideline (us) : %" PRIu32 "\n", cdlSettings.cdlReadDescriptor[descriptorIndex].commandDurationGuideline);
            translate_Policy_To_String(CDL_POLICY_TYPE_COMMAND_DURATION_GUIDELINE, cdlSettings.cdlReadDescriptor[descriptorIndex].commandDurationGuidelinePolicy, policyTranslation);
            printf("\t\tCommand Duration Guideline Policy : %s\n", policyTranslation);
        }
    }
    for (uint8_t descriptorIndex = 0; descriptorIndex < MAX_CDL_WRITE_DESCRIPTOR; descriptorIndex++)
    {
        printf("\tDescriptor W%" PRIu8 "\n", descriptorIndex + 1);
        DECLARE_ZERO_INIT_ARRAY(char, policyTranslation, MAX_POLICY_STRING_LENGHT);
        printf("\t\tInactive Time (us) : %" PRIu32 "\n", cdlSettings.cdlWriteDescriptor[descriptorIndex].inactiveTime);
        translate_Policy_To_String(CDL_POLICY_TYPE_INACTIVE_TIME, cdlSettings.cdlWriteDescriptor[descriptorIndex].inactiveTimePolicy, policyTranslation);
        printf("\t\tInactive Time Policy : %s\n", policyTranslation);
        printf("\t\tActive Time (us) : %" PRIu32 "\n", cdlSettings.cdlWriteDescriptor[descriptorIndex].activeTime);
        translate_Policy_To_String(CDL_POLICY_TYPE_ACTIVE_TIME, cdlSettings.cdlWriteDescriptor[descriptorIndex].activeTimePolicy, policyTranslation);
        printf("\t\tActive Time Policy : %s\n", policyTranslation);
        if (cdlSettings.isCommandDurationGuidelineSupported)
        {
            printf("\t\tCommand Duration Guideline (us): %" PRIu32 "\n", cdlSettings.cdlWriteDescriptor[descriptorIndex].commandDurationGuideline);
            translate_Policy_To_String(CDL_POLICY_TYPE_COMMAND_DURATION_GUIDELINE, cdlSettings.cdlWriteDescriptor[descriptorIndex].commandDurationGuidelinePolicy, policyTranslation);
            printf("\t\tCommand Duration Guideline Policy : %s\n", policyTranslation);
        }
    }

    safe_free_aligned(&logBuffer);
    return ret;
}

eReturnValues show_CDL_Settings(tDevice *device)
{
    eReturnValues ret = NOT_SUPPORTED;

    if (device->drive_info.drive_type == ATA_DRIVE)
    {
        return show_ATA_CDL_Settings(device);
    }

    return ret;
}

void translate_CDL_Performance_Vs_Command_Completion_Status_To_String(uint8_t cmdCompletionField, char *translatedString)
{
    switch (cmdCompletionField)
    {
    case 0x00:
        snprintf(translatedString, MAX_CDL_PERFORMANCE_VS_CMD_COMPLETION_STATUS_STRING_LENGTH, "0%%");
        break;
    case 0x01:
        snprintf(translatedString, MAX_CDL_PERFORMANCE_VS_CMD_COMPLETION_STATUS_STRING_LENGTH, "0.5%%");
        break;
    case 0x02:
        snprintf(translatedString, MAX_CDL_PERFORMANCE_VS_CMD_COMPLETION_STATUS_STRING_LENGTH, "1.0%%");
        break;
    case 0x03:
        snprintf(translatedString, MAX_CDL_PERFORMANCE_VS_CMD_COMPLETION_STATUS_STRING_LENGTH, "1.5%%");
        break;
    case 0x04:
        snprintf(translatedString, MAX_CDL_PERFORMANCE_VS_CMD_COMPLETION_STATUS_STRING_LENGTH, "2.0%%");
        break;
    case 0x05:
        snprintf(translatedString, MAX_CDL_PERFORMANCE_VS_CMD_COMPLETION_STATUS_STRING_LENGTH, "2.5%%");
        break;
    case 0x06:
        snprintf(translatedString, MAX_CDL_PERFORMANCE_VS_CMD_COMPLETION_STATUS_STRING_LENGTH, "3%%");
        break;
    case 0x07:
        snprintf(translatedString, MAX_CDL_PERFORMANCE_VS_CMD_COMPLETION_STATUS_STRING_LENGTH, "4%%");
        break;
    case 0x08:
        snprintf(translatedString, MAX_CDL_PERFORMANCE_VS_CMD_COMPLETION_STATUS_STRING_LENGTH, "5%%");
        break;
    case 0x09:
        snprintf(translatedString, MAX_CDL_PERFORMANCE_VS_CMD_COMPLETION_STATUS_STRING_LENGTH, "8%%");
        break;
    case 0x0A:
        snprintf(translatedString, MAX_CDL_PERFORMANCE_VS_CMD_COMPLETION_STATUS_STRING_LENGTH, "10%%");
        break;
    case 0x0B:
        snprintf(translatedString, MAX_CDL_PERFORMANCE_VS_CMD_COMPLETION_STATUS_STRING_LENGTH, "15%%");
        break;
    case 0x0C:
        snprintf(translatedString, MAX_CDL_PERFORMANCE_VS_CMD_COMPLETION_STATUS_STRING_LENGTH, "20%%");
        break;
    case 0x0D:
    case 0x0E:
    case 0x0F:
        snprintf(translatedString, MAX_CDL_PERFORMANCE_VS_CMD_COMPLETION_STATUS_STRING_LENGTH, "Reserved");
        break;
    default:
        snprintf(translatedString, MAX_CDL_PERFORMANCE_VS_CMD_COMPLETION_STATUS_STRING_LENGTH, "Unknown");
        break;
    }
}

void translate_Policy_To_String(eCDLPolicyType policyType, uint8_t policyField, char *translatedString)
{
    if (policyType == CDL_POLICY_TYPE_COMMAND_DURATION_GUIDELINE)
    {
        switch (policyField)
        {
        case 0x00:
            snprintf(translatedString, MAX_POLICY_STRING_LENGHT, "When time exceeded, device completes the command as soon as possible");
            break;
        case 0x01:
            snprintf(translatedString, MAX_POLICY_STRING_LENGHT, "When time exceeded, uses next descriptor to extend time limit");
            break;
        case 0x02:
            snprintf(translatedString, MAX_POLICY_STRING_LENGHT, "When time exceeded, continue processing command and disregard latency requirements");
            break;
        case 0x0D:
            snprintf(translatedString, MAX_POLICY_STRING_LENGHT, "When time exceeded, device completes the command without error and sense code set to \"Data Currently Unavailable\"");
            break;
        case 0x0F:
            snprintf(translatedString, MAX_POLICY_STRING_LENGHT, "When time exceeded, device returns command abort and sense code describing command timeout");
            break;
        default:
            snprintf(translatedString, MAX_POLICY_STRING_LENGHT, "Reserved");
            break;
        }
    }
    else
    {
        switch (policyField)
        {
        case 0x00:
            if (policyType == CDL_POLICY_TYPE_INACTIVE_TIME)
                snprintf(translatedString, MAX_POLICY_STRING_LENGHT, "The device ignores the INACTIVE TIME LIMIT field");
            else
                snprintf(translatedString, MAX_POLICY_STRING_LENGHT, "The device ignores the ACTIVE TIME LIMIT field");
            break;
        case 0x0D:
            snprintf(translatedString, MAX_POLICY_STRING_LENGHT, "When time exceeded, device completes the command without error and sense code set to \"Data Currently Unavailable\"");
            break;
        case 0x0F:
            snprintf(translatedString, MAX_POLICY_STRING_LENGHT, "When time exceeded, device returns command abort and sense code describing command timeout");
            break;
        default:
            snprintf(translatedString, MAX_POLICY_STRING_LENGHT, "Reserved");
            break;
        }
    }
}