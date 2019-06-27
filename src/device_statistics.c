//
// Do NOT modify or remove this copyright and license
//
// Copyright (c) 2012 - 2018 Seagate Technology LLC and/or its Affiliates, All Rights Reserved
//
// This software is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
//
// ******************************************************************************************
// 
// \file device_statistics.c
// \brief This file defines the functions related to getting/displaying device statistics

#include "device_statistics.h"
#include "logs.h"

//this is ued to determine which device statistic is being talked about by the DSN log on ata
int map_Page_And_Offset_To_Device_Statistic_And_Set_Threshold_Data(tDevice *device, ptrDeviceStatistics deviceStats)
{
    int ret = FAILURE;
    if (device->drive_info.drive_type == ATA_DRIVE)
    {

    }
    return ret;
}
void scsi_Threshold_Comparison(statistic *ptrStatistic);//prototype

int get_ATA_DeviceStatistics(tDevice *device, ptrDeviceStatistics deviceStats)
{
    int ret = NOT_SUPPORTED;
    if (!deviceStats)
    {
        return BAD_PARAMETER;
    }
    uint32_t deviceStatsSize = 0, deviceStatsNotificationsSize = 0;
    //need to get the device statistics log
    if (SUCCESS == get_ATA_Log_Size(device, ATA_LOG_DEVICE_STATISTICS, &deviceStatsSize, true, true))
    {
        bool dsnFeatureSupported = device->drive_info.IdentifyData.ata.Word119 & BIT9;
        bool dsnFeatureEnabled = device->drive_info.IdentifyData.ata.Word120 & BIT9;
        uint8_t *deviceStatsLog = (uint8_t*)calloc(deviceStatsSize, sizeof(uint8_t));
        if (!deviceStatsLog)
        {
            return MEMORY_FAILURE;
        }
        //this is to get the threshold stuff
        if (dsnFeatureSupported && dsnFeatureEnabled && SUCCESS == get_ATA_Log_Size(device, ATA_LOG_DEVICE_STATISTICS_NOTIFICATION, &deviceStatsNotificationsSize, true, false))
        {
            uint8_t *devStatsNotificationsLog = (uint8_t*)calloc(deviceStatsNotificationsSize, sizeof(uint8_t));
            if (SUCCESS == get_ATA_Log(device, ATA_LOG_DEVICE_STATISTICS_NOTIFICATION, NULL, NULL, true, false, true, devStatsNotificationsLog, deviceStatsNotificationsSize, NULL, 0,0))
            {
                //Start at page 1 since we want all the details, not just the summary from page 0
                //increment by 2 qwords and go through each statistic and it's condition individually
                for (uint32_t offset = LEGACY_DRIVE_SEC_SIZE; offset < deviceStatsNotificationsSize; offset += 16)
                {
                    uint64_t statisticLocation = M_BytesTo8ByteValue(devStatsNotificationsLog[offset + 7], devStatsNotificationsLog[offset + 6], devStatsNotificationsLog[offset + 5], devStatsNotificationsLog[offset + 4], devStatsNotificationsLog[offset + 3], devStatsNotificationsLog[offset + 2], devStatsNotificationsLog[offset + 1], devStatsNotificationsLog[offset]);
                    uint64_t statisticCondition = M_BytesTo8ByteValue(devStatsNotificationsLog[offset + 15], devStatsNotificationsLog[offset + 14], devStatsNotificationsLog[offset + 13], devStatsNotificationsLog[offset + 12], devStatsNotificationsLog[offset + 11], devStatsNotificationsLog[offset + 10], devStatsNotificationsLog[offset + 9], devStatsNotificationsLog[offset + 8]);
                    uint8_t statisticLogPage = M_Byte3(statisticLocation);
                    uint8_t statisticByteOffset = M_Byte0(statisticLocation);//first byte of the statistic offset (view ACS3/4 spec for this)
                    //device statistics condition definition:
                    //Bits 63:56 = DSN Condition Flags (below)
                    //Bit 63 = notification enabled
                    //Bits 62:60 = value comparison type
                    //  000b = does not trigger on any update
                    //  001b = triggers on every update of the statistics value
                    //  010b = triggers on the device statistic value equal to the threshold value
                    //  011b = triggers on the device statistic value less than the threshold value
                    //  100b = triggers on the device statistic value greater than the threshold value
                    //Bit 59 = non-validity trigger
                    //Bit 58 = validity trigger
                    uint8_t dsnConditionFlags = M_Byte7(statisticCondition);
                    bool notificationEnabled = dsnConditionFlags & BIT7;
                    uint8_t comparisonType = M_Nibble1(dsnConditionFlags) & 0x03;
                    bool nonValidityTrigger = dsnConditionFlags & BIT3;
                    bool validityTrigger = dsnConditionFlags & BIT2;
                    //Bits 55:0 = Threshold Value
                    uint64_t thresholdValue = statisticCondition & 0x00FFFFFFFFFFFFFFULL;//removing byte 7
                    switch (statisticLogPage)
                    {
                    case ATA_DEVICE_STATS_LOG_LIST:
                        //nothing on this page since this is the summary page, break
                        break;
                    case ATA_DEVICE_STATS_LOG_GENERAL:
                        switch (statisticByteOffset)
                        {
                        case 8://lifetime power on resets
                            deviceStats->sataStatistics.lifetimePoweronResets.isThresholdValid = true;
                            deviceStats->sataStatistics.lifetimePoweronResets.thresholdNotificationEnabled = notificationEnabled;
                            deviceStats->sataStatistics.lifetimePoweronResets.threshType = (eThresholdType)comparisonType;
                            deviceStats->sataStatistics.lifetimePoweronResets.nonValidityTrigger = nonValidityTrigger;
                            deviceStats->sataStatistics.lifetimePoweronResets.validityTrigger = validityTrigger;
                            deviceStats->sataStatistics.lifetimePoweronResets.threshold = thresholdValue;
                            break;
                        case 16://power on hours
                            deviceStats->sataStatistics.powerOnHours.isThresholdValid = true;
                            deviceStats->sataStatistics.powerOnHours.thresholdNotificationEnabled = notificationEnabled;
                            deviceStats->sataStatistics.powerOnHours.threshType = (eThresholdType)comparisonType;
                            deviceStats->sataStatistics.powerOnHours.nonValidityTrigger = nonValidityTrigger;
                            deviceStats->sataStatistics.powerOnHours.validityTrigger = validityTrigger;
                            deviceStats->sataStatistics.powerOnHours.threshold = thresholdValue;
                            break;
                        case 24://logical sectors written
                            deviceStats->sataStatistics.logicalSectorsWritten.isThresholdValid = true;
                            deviceStats->sataStatistics.logicalSectorsWritten.thresholdNotificationEnabled = notificationEnabled;
                            deviceStats->sataStatistics.logicalSectorsWritten.threshType = (eThresholdType)comparisonType;
                            deviceStats->sataStatistics.logicalSectorsWritten.nonValidityTrigger = nonValidityTrigger;
                            deviceStats->sataStatistics.logicalSectorsWritten.validityTrigger = validityTrigger;
                            deviceStats->sataStatistics.logicalSectorsWritten.threshold = thresholdValue;
                            break;
                        case 32://number of write commands
                            deviceStats->sataStatistics.numberOfWriteCommands.isThresholdValid = true;
                            deviceStats->sataStatistics.numberOfWriteCommands.thresholdNotificationEnabled = notificationEnabled;
                            deviceStats->sataStatistics.numberOfWriteCommands.threshType = (eThresholdType)comparisonType;
                            deviceStats->sataStatistics.numberOfWriteCommands.nonValidityTrigger = nonValidityTrigger;
                            deviceStats->sataStatistics.numberOfWriteCommands.validityTrigger = validityTrigger;
                            deviceStats->sataStatistics.numberOfWriteCommands.threshold = thresholdValue;
                            break;
                        case 40://logical sectors read
                            deviceStats->sataStatistics.logicalSectorsRead.isThresholdValid = true;
                            deviceStats->sataStatistics.logicalSectorsRead.thresholdNotificationEnabled = notificationEnabled;
                            deviceStats->sataStatistics.logicalSectorsRead.threshType = (eThresholdType)comparisonType;
                            deviceStats->sataStatistics.logicalSectorsRead.nonValidityTrigger = nonValidityTrigger;
                            deviceStats->sataStatistics.logicalSectorsRead.validityTrigger = validityTrigger;
                            deviceStats->sataStatistics.logicalSectorsRead.threshold = thresholdValue;
                            break;
                        case 48://number of read commands
                            deviceStats->sataStatistics.numberOfReadCommands.isThresholdValid = true;
                            deviceStats->sataStatistics.numberOfReadCommands.thresholdNotificationEnabled = notificationEnabled;
                            deviceStats->sataStatistics.numberOfReadCommands.threshType = (eThresholdType)comparisonType;
                            deviceStats->sataStatistics.numberOfReadCommands.nonValidityTrigger = nonValidityTrigger;
                            deviceStats->sataStatistics.numberOfReadCommands.validityTrigger = validityTrigger;
                            deviceStats->sataStatistics.numberOfReadCommands.threshold = thresholdValue;
                            break;
                        case 56://Date and Time Timestamp
                            deviceStats->sataStatistics.dateAndTimeTimestamp.isThresholdValid = true;
                            deviceStats->sataStatistics.dateAndTimeTimestamp.thresholdNotificationEnabled = notificationEnabled;
                            deviceStats->sataStatistics.dateAndTimeTimestamp.threshType = (eThresholdType)comparisonType;
                            deviceStats->sataStatistics.dateAndTimeTimestamp.nonValidityTrigger = nonValidityTrigger;
                            deviceStats->sataStatistics.dateAndTimeTimestamp.validityTrigger = validityTrigger;
                            deviceStats->sataStatistics.dateAndTimeTimestamp.threshold = thresholdValue;
                            break;
                        case 64://Pending Error Count
                            deviceStats->sataStatistics.pendingErrorCount.isThresholdValid = true;
                            deviceStats->sataStatistics.pendingErrorCount.thresholdNotificationEnabled = notificationEnabled;
                            deviceStats->sataStatistics.pendingErrorCount.threshType = (eThresholdType)comparisonType;
                            deviceStats->sataStatistics.pendingErrorCount.nonValidityTrigger = nonValidityTrigger;
                            deviceStats->sataStatistics.pendingErrorCount.validityTrigger = validityTrigger;
                            deviceStats->sataStatistics.pendingErrorCount.threshold = thresholdValue;
                            break;
                        case 72://workload utilization
                            deviceStats->sataStatistics.workloadUtilization.isThresholdValid = true;
                            deviceStats->sataStatistics.workloadUtilization.thresholdNotificationEnabled = notificationEnabled;
                            deviceStats->sataStatistics.workloadUtilization.threshType = (eThresholdType)comparisonType;
                            deviceStats->sataStatistics.workloadUtilization.nonValidityTrigger = nonValidityTrigger;
                            deviceStats->sataStatistics.workloadUtilization.validityTrigger = validityTrigger;
                            deviceStats->sataStatistics.workloadUtilization.threshold = thresholdValue;
                            break;
                        case 80://utilization usage rate
                            deviceStats->sataStatistics.utilizationUsageRate.isThresholdValid = true;
                            deviceStats->sataStatistics.utilizationUsageRate.thresholdNotificationEnabled = notificationEnabled;
                            deviceStats->sataStatistics.utilizationUsageRate.threshType = (eThresholdType)comparisonType;
                            deviceStats->sataStatistics.utilizationUsageRate.nonValidityTrigger = nonValidityTrigger;
                            deviceStats->sataStatistics.utilizationUsageRate.validityTrigger = validityTrigger;
                            deviceStats->sataStatistics.utilizationUsageRate.threshold = thresholdValue;
                            break;
                        case 88://resource availability
                            deviceStats->sataStatistics.resourceAvailability.isThresholdValid = true;
                            deviceStats->sataStatistics.resourceAvailability.thresholdNotificationEnabled = notificationEnabled;
                            deviceStats->sataStatistics.resourceAvailability.threshType = (eThresholdType)comparisonType;
                            deviceStats->sataStatistics.resourceAvailability.nonValidityTrigger = nonValidityTrigger;
                            deviceStats->sataStatistics.resourceAvailability.validityTrigger = validityTrigger;
                            deviceStats->sataStatistics.resourceAvailability.threshold = thresholdValue;
                            break;
                        case 96://random write resources used
                            deviceStats->sataStatistics.randomWriteResourcesUsed.isThresholdValid = true;
                            deviceStats->sataStatistics.randomWriteResourcesUsed.thresholdNotificationEnabled = notificationEnabled;
                            deviceStats->sataStatistics.randomWriteResourcesUsed.threshType = (eThresholdType)comparisonType;
                            deviceStats->sataStatistics.randomWriteResourcesUsed.nonValidityTrigger = nonValidityTrigger;
                            deviceStats->sataStatistics.randomWriteResourcesUsed.validityTrigger = validityTrigger;
                            deviceStats->sataStatistics.randomWriteResourcesUsed.threshold = thresholdValue;
                            break;
                        default:
                            //unknown
                            break;
                        }
                        break;
                    case ATA_DEVICE_STATS_LOG_FREE_FALL:
                        switch (statisticByteOffset)
                        {
                        case 8://number of free fall events detected
                            deviceStats->sataStatistics.numberOfFreeFallEventsDetected.isThresholdValid = true;
                            deviceStats->sataStatistics.numberOfFreeFallEventsDetected.thresholdNotificationEnabled = notificationEnabled;
                            deviceStats->sataStatistics.numberOfFreeFallEventsDetected.threshType = (eThresholdType)comparisonType;
                            deviceStats->sataStatistics.numberOfFreeFallEventsDetected.nonValidityTrigger = nonValidityTrigger;
                            deviceStats->sataStatistics.numberOfFreeFallEventsDetected.validityTrigger = validityTrigger;
                            deviceStats->sataStatistics.numberOfFreeFallEventsDetected.threshold = thresholdValue;
                            break;
                        case 16://overlimit shock events
                            deviceStats->sataStatistics.overlimitShockEvents.isThresholdValid = true;
                            deviceStats->sataStatistics.overlimitShockEvents.thresholdNotificationEnabled = notificationEnabled;
                            deviceStats->sataStatistics.overlimitShockEvents.threshType = (eThresholdType)comparisonType;
                            deviceStats->sataStatistics.overlimitShockEvents.nonValidityTrigger = nonValidityTrigger;
                            deviceStats->sataStatistics.overlimitShockEvents.validityTrigger = validityTrigger;
                            deviceStats->sataStatistics.overlimitShockEvents.threshold = thresholdValue;
                            break;
                        default:
                            //unknown
                            break;
                        }
                        break;
                    case ATA_DEVICE_STATS_LOG_ROTATING_MEDIA:
                        switch (statisticByteOffset)
                        {
                        case 8://spindle motor power-on hours
                            deviceStats->sataStatistics.spindleMotorPoweronHours.isThresholdValid = true;
                            deviceStats->sataStatistics.spindleMotorPoweronHours.thresholdNotificationEnabled = notificationEnabled;
                            deviceStats->sataStatistics.spindleMotorPoweronHours.threshType = (eThresholdType)comparisonType;
                            deviceStats->sataStatistics.spindleMotorPoweronHours.nonValidityTrigger = nonValidityTrigger;
                            deviceStats->sataStatistics.spindleMotorPoweronHours.validityTrigger = validityTrigger;
                            deviceStats->sataStatistics.spindleMotorPoweronHours.threshold = thresholdValue;
                            break;
                        case 16://head flying hours
                            deviceStats->sataStatistics.headFlyingHours.isThresholdValid = true;
                            deviceStats->sataStatistics.headFlyingHours.thresholdNotificationEnabled = notificationEnabled;
                            deviceStats->sataStatistics.headFlyingHours.threshType = (eThresholdType)comparisonType;
                            deviceStats->sataStatistics.headFlyingHours.nonValidityTrigger = nonValidityTrigger;
                            deviceStats->sataStatistics.headFlyingHours.validityTrigger = validityTrigger;
                            deviceStats->sataStatistics.headFlyingHours.threshold = thresholdValue;
                            break;
                        case 24://head load events
                            deviceStats->sataStatistics.headLoadEvents.isThresholdValid = true;
                            deviceStats->sataStatistics.headLoadEvents.thresholdNotificationEnabled = notificationEnabled;
                            deviceStats->sataStatistics.headLoadEvents.threshType = (eThresholdType)comparisonType;
                            deviceStats->sataStatistics.headLoadEvents.nonValidityTrigger = nonValidityTrigger;
                            deviceStats->sataStatistics.headLoadEvents.validityTrigger = validityTrigger;
                            deviceStats->sataStatistics.headLoadEvents.threshold = thresholdValue;
                            break;
                        case 32://number of reallocated logical sectors
                            deviceStats->sataStatistics.numberOfReallocatedLogicalSectors.isThresholdValid = true;
                            deviceStats->sataStatistics.numberOfReallocatedLogicalSectors.thresholdNotificationEnabled = notificationEnabled;
                            deviceStats->sataStatistics.numberOfReallocatedLogicalSectors.threshType = (eThresholdType)comparisonType;
                            deviceStats->sataStatistics.numberOfReallocatedLogicalSectors.nonValidityTrigger = nonValidityTrigger;
                            deviceStats->sataStatistics.numberOfReallocatedLogicalSectors.validityTrigger = validityTrigger;
                            deviceStats->sataStatistics.numberOfReallocatedLogicalSectors.threshold = thresholdValue;
                            break;
                        case 40://read recovery attempts
                            deviceStats->sataStatistics.readRecoveryAttempts.isThresholdValid = true;
                            deviceStats->sataStatistics.readRecoveryAttempts.thresholdNotificationEnabled = notificationEnabled;
                            deviceStats->sataStatistics.readRecoveryAttempts.threshType = (eThresholdType)comparisonType;
                            deviceStats->sataStatistics.readRecoveryAttempts.nonValidityTrigger = nonValidityTrigger;
                            deviceStats->sataStatistics.readRecoveryAttempts.validityTrigger = validityTrigger;
                            deviceStats->sataStatistics.readRecoveryAttempts.threshold = thresholdValue;
                            break;
                        case 48://number of mechanical start failures
                            deviceStats->sataStatistics.numberOfMechanicalStartFailures.isThresholdValid = true;
                            deviceStats->sataStatistics.numberOfMechanicalStartFailures.thresholdNotificationEnabled = notificationEnabled;
                            deviceStats->sataStatistics.numberOfMechanicalStartFailures.threshType = (eThresholdType)comparisonType;
                            deviceStats->sataStatistics.numberOfMechanicalStartFailures.nonValidityTrigger = nonValidityTrigger;
                            deviceStats->sataStatistics.numberOfMechanicalStartFailures.validityTrigger = validityTrigger;
                            deviceStats->sataStatistics.numberOfMechanicalStartFailures.threshold = thresholdValue;
                            break;
                        case 56://numberOfReallocationCandidateLogicalSectors
                            deviceStats->sataStatistics.numberOfReallocationCandidateLogicalSectors.isThresholdValid = true;
                            deviceStats->sataStatistics.numberOfReallocationCandidateLogicalSectors.thresholdNotificationEnabled = notificationEnabled;
                            deviceStats->sataStatistics.numberOfReallocationCandidateLogicalSectors.threshType = (eThresholdType)comparisonType;
                            deviceStats->sataStatistics.numberOfReallocationCandidateLogicalSectors.nonValidityTrigger = nonValidityTrigger;
                            deviceStats->sataStatistics.numberOfReallocationCandidateLogicalSectors.validityTrigger = validityTrigger;
                            deviceStats->sataStatistics.numberOfReallocationCandidateLogicalSectors.threshold = thresholdValue;
                            break;
                        case 64://number of high priority unload events
                            deviceStats->sataStatistics.numberOfHighPriorityUnloadEvents.isThresholdValid = true;
                            deviceStats->sataStatistics.numberOfHighPriorityUnloadEvents.thresholdNotificationEnabled = notificationEnabled;
                            deviceStats->sataStatistics.numberOfHighPriorityUnloadEvents.threshType = (eThresholdType)comparisonType;
                            deviceStats->sataStatistics.numberOfHighPriorityUnloadEvents.nonValidityTrigger = nonValidityTrigger;
                            deviceStats->sataStatistics.numberOfHighPriorityUnloadEvents.validityTrigger = validityTrigger;
                            deviceStats->sataStatistics.numberOfHighPriorityUnloadEvents.threshold = thresholdValue;
                            break;
                        default:
                            //unknown
                            break;
                        }
                        break;
                    case ATA_DEVICE_STATS_LOG_GEN_ERR:
                        switch (statisticByteOffset)
                        {
                        case 8://number of reported uncorrectable errors
                            deviceStats->sataStatistics.numberOfReportedUncorrectableErrors.isThresholdValid = true;
                            deviceStats->sataStatistics.numberOfReportedUncorrectableErrors.thresholdNotificationEnabled = notificationEnabled;
                            deviceStats->sataStatistics.numberOfReportedUncorrectableErrors.threshType = (eThresholdType)comparisonType;
                            deviceStats->sataStatistics.numberOfReportedUncorrectableErrors.nonValidityTrigger = nonValidityTrigger;
                            deviceStats->sataStatistics.numberOfReportedUncorrectableErrors.validityTrigger = validityTrigger;
                            deviceStats->sataStatistics.numberOfReportedUncorrectableErrors.threshold = thresholdValue;
                            break;
                        case 16://number of resets between command acceptance and command completion
                            deviceStats->sataStatistics.numberOfResetsBetweenCommandAcceptanceAndCommandCompletion.isThresholdValid = true;
                            deviceStats->sataStatistics.numberOfResetsBetweenCommandAcceptanceAndCommandCompletion.thresholdNotificationEnabled = notificationEnabled;
                            deviceStats->sataStatistics.numberOfResetsBetweenCommandAcceptanceAndCommandCompletion.threshType = (eThresholdType)comparisonType;
                            deviceStats->sataStatistics.numberOfResetsBetweenCommandAcceptanceAndCommandCompletion.nonValidityTrigger = nonValidityTrigger;
                            deviceStats->sataStatistics.numberOfResetsBetweenCommandAcceptanceAndCommandCompletion.validityTrigger = validityTrigger;
                            deviceStats->sataStatistics.numberOfResetsBetweenCommandAcceptanceAndCommandCompletion.threshold = thresholdValue;
                            break;
                        case 24://physical element status changed
                            deviceStats->sataStatistics.physicalElementStatusChanged.isThresholdValid = true;
                            deviceStats->sataStatistics.physicalElementStatusChanged.thresholdNotificationEnabled = notificationEnabled;
                            deviceStats->sataStatistics.physicalElementStatusChanged.threshType = (eThresholdType)comparisonType;
                            deviceStats->sataStatistics.physicalElementStatusChanged.nonValidityTrigger = nonValidityTrigger;
                            deviceStats->sataStatistics.physicalElementStatusChanged.validityTrigger = validityTrigger;
                            deviceStats->sataStatistics.physicalElementStatusChanged.threshold = thresholdValue;
                            break;
                        default:
                            //unknown
                            break;
                        }
                        break;
                    case ATA_DEVICE_STATS_LOG_TEMP:
                        switch (statisticByteOffset)
                        {
                        case 8://current temperature
                            deviceStats->sataStatistics.currentTemperature.isThresholdValid = true;
                            deviceStats->sataStatistics.currentTemperature.thresholdNotificationEnabled = notificationEnabled;
                            deviceStats->sataStatistics.currentTemperature.threshType = (eThresholdType)comparisonType;
                            deviceStats->sataStatistics.currentTemperature.nonValidityTrigger = nonValidityTrigger;
                            deviceStats->sataStatistics.currentTemperature.validityTrigger = validityTrigger;
                            deviceStats->sataStatistics.currentTemperature.threshold = thresholdValue;
                            break;
                        case 16://average short term temperature
                            deviceStats->sataStatistics.averageShortTermTemperature.isThresholdValid = true;
                            deviceStats->sataStatistics.averageShortTermTemperature.thresholdNotificationEnabled = notificationEnabled;
                            deviceStats->sataStatistics.averageShortTermTemperature.threshType = (eThresholdType)comparisonType;
                            deviceStats->sataStatistics.averageShortTermTemperature.nonValidityTrigger = nonValidityTrigger;
                            deviceStats->sataStatistics.averageShortTermTemperature.validityTrigger = validityTrigger;
                            deviceStats->sataStatistics.averageShortTermTemperature.threshold = thresholdValue;
                            break;
                        case 24://average long term temperature
                            deviceStats->sataStatistics.averageLongTermTemperature.isThresholdValid = true;
                            deviceStats->sataStatistics.averageLongTermTemperature.thresholdNotificationEnabled = notificationEnabled;
                            deviceStats->sataStatistics.averageLongTermTemperature.threshType = (eThresholdType)comparisonType;
                            deviceStats->sataStatistics.averageLongTermTemperature.nonValidityTrigger = nonValidityTrigger;
                            deviceStats->sataStatistics.averageLongTermTemperature.validityTrigger = validityTrigger;
                            deviceStats->sataStatistics.averageLongTermTemperature.threshold = thresholdValue;
                            break;
                        case 32://highest temperature
                            deviceStats->sataStatistics.highestTemperature.isThresholdValid = true;
                            deviceStats->sataStatistics.highestTemperature.thresholdNotificationEnabled = notificationEnabled;
                            deviceStats->sataStatistics.highestTemperature.threshType = (eThresholdType)comparisonType;
                            deviceStats->sataStatistics.highestTemperature.nonValidityTrigger = nonValidityTrigger;
                            deviceStats->sataStatistics.highestTemperature.validityTrigger = validityTrigger;
                            deviceStats->sataStatistics.highestTemperature.threshold = thresholdValue;
                            break;
                        case 40://lowest temperature
                            deviceStats->sataStatistics.lowestTemperature.isThresholdValid = true;
                            deviceStats->sataStatistics.lowestTemperature.thresholdNotificationEnabled = notificationEnabled;
                            deviceStats->sataStatistics.lowestTemperature.threshType = (eThresholdType)comparisonType;
                            deviceStats->sataStatistics.lowestTemperature.nonValidityTrigger = nonValidityTrigger;
                            deviceStats->sataStatistics.lowestTemperature.validityTrigger = validityTrigger;
                            deviceStats->sataStatistics.lowestTemperature.threshold = thresholdValue;
                            break;
                        case 48://highest Averagre Short Term Temperature
                            deviceStats->sataStatistics.highestAverageShortTermTemperature.isThresholdValid = true;
                            deviceStats->sataStatistics.highestAverageShortTermTemperature.thresholdNotificationEnabled = notificationEnabled;
                            deviceStats->sataStatistics.highestAverageShortTermTemperature.threshType = (eThresholdType)comparisonType;
                            deviceStats->sataStatistics.highestAverageShortTermTemperature.nonValidityTrigger = nonValidityTrigger;
                            deviceStats->sataStatistics.highestAverageShortTermTemperature.validityTrigger = validityTrigger;
                            deviceStats->sataStatistics.highestAverageShortTermTemperature.threshold = thresholdValue;
                            break;
                        case 56://lowest average short term temperature
                            deviceStats->sataStatistics.lowestAverageShortTermTemperature.isThresholdValid = true;
                            deviceStats->sataStatistics.lowestAverageShortTermTemperature.thresholdNotificationEnabled = notificationEnabled;
                            deviceStats->sataStatistics.lowestAverageShortTermTemperature.threshType = (eThresholdType)comparisonType;
                            deviceStats->sataStatistics.lowestAverageShortTermTemperature.nonValidityTrigger = nonValidityTrigger;
                            deviceStats->sataStatistics.lowestAverageShortTermTemperature.validityTrigger = validityTrigger;
                            deviceStats->sataStatistics.lowestAverageShortTermTemperature.threshold = thresholdValue;
                            break;
                        case 64://highest average long term temperature
                            deviceStats->sataStatistics.highestAverageLongTermTemperature.isThresholdValid = true;
                            deviceStats->sataStatistics.highestAverageLongTermTemperature.thresholdNotificationEnabled = notificationEnabled;
                            deviceStats->sataStatistics.highestAverageLongTermTemperature.threshType = (eThresholdType)comparisonType;
                            deviceStats->sataStatistics.highestAverageLongTermTemperature.nonValidityTrigger = nonValidityTrigger;
                            deviceStats->sataStatistics.highestAverageLongTermTemperature.validityTrigger = validityTrigger;
                            deviceStats->sataStatistics.highestAverageLongTermTemperature.threshold = thresholdValue;
                            break;
                        case 72://lowest average long term temperature
                            deviceStats->sataStatistics.lowestAverageLongTermTemperature.isThresholdValid = true;
                            deviceStats->sataStatistics.lowestAverageLongTermTemperature.thresholdNotificationEnabled = notificationEnabled;
                            deviceStats->sataStatistics.lowestAverageLongTermTemperature.threshType = (eThresholdType)comparisonType;
                            deviceStats->sataStatistics.lowestAverageLongTermTemperature.nonValidityTrigger = nonValidityTrigger;
                            deviceStats->sataStatistics.lowestAverageLongTermTemperature.validityTrigger = validityTrigger;
                            deviceStats->sataStatistics.lowestAverageLongTermTemperature.threshold = thresholdValue;
                            break;
                        case 80://time in over temperature
                            deviceStats->sataStatistics.timeInOverTemperature.isThresholdValid = true;
                            deviceStats->sataStatistics.timeInOverTemperature.thresholdNotificationEnabled = notificationEnabled;
                            deviceStats->sataStatistics.timeInOverTemperature.threshType = (eThresholdType)comparisonType;
                            deviceStats->sataStatistics.timeInOverTemperature.nonValidityTrigger = nonValidityTrigger;
                            deviceStats->sataStatistics.timeInOverTemperature.validityTrigger = validityTrigger;
                            deviceStats->sataStatistics.timeInOverTemperature.threshold = thresholdValue;
                            break;
                        case 88://specified maximum operating temperature
                            deviceStats->sataStatistics.specifiedMaximumOperatingTemperature.isThresholdValid = true;
                            deviceStats->sataStatistics.specifiedMaximumOperatingTemperature.thresholdNotificationEnabled = notificationEnabled;
                            deviceStats->sataStatistics.specifiedMaximumOperatingTemperature.threshType = (eThresholdType)comparisonType;
                            deviceStats->sataStatistics.specifiedMaximumOperatingTemperature.nonValidityTrigger = nonValidityTrigger;
                            deviceStats->sataStatistics.specifiedMaximumOperatingTemperature.validityTrigger = validityTrigger;
                            deviceStats->sataStatistics.specifiedMaximumOperatingTemperature.threshold = thresholdValue;
                            break;
                        case 96://time in under temperature
                            deviceStats->sataStatistics.timeInUnderTemperature.isThresholdValid = true;
                            deviceStats->sataStatistics.timeInUnderTemperature.thresholdNotificationEnabled = notificationEnabled;
                            deviceStats->sataStatistics.timeInUnderTemperature.threshType = (eThresholdType)comparisonType;
                            deviceStats->sataStatistics.timeInUnderTemperature.nonValidityTrigger = nonValidityTrigger;
                            deviceStats->sataStatistics.timeInUnderTemperature.validityTrigger = validityTrigger;
                            deviceStats->sataStatistics.timeInUnderTemperature.threshold = thresholdValue;
                            break;
                        case 104://specified minimum operating temperature
                            deviceStats->sataStatistics.specifiedMinimumOperatingTemperature.isThresholdValid = true;
                            deviceStats->sataStatistics.specifiedMinimumOperatingTemperature.thresholdNotificationEnabled = notificationEnabled;
                            deviceStats->sataStatistics.specifiedMinimumOperatingTemperature.threshType = (eThresholdType)comparisonType;
                            deviceStats->sataStatistics.specifiedMinimumOperatingTemperature.nonValidityTrigger = nonValidityTrigger;
                            deviceStats->sataStatistics.specifiedMinimumOperatingTemperature.validityTrigger = validityTrigger;
                            deviceStats->sataStatistics.specifiedMinimumOperatingTemperature.threshold = thresholdValue;
                            break;
                        default:
                            //unknown
                            break;
                        }
                        break;
                    case ATA_DEVICE_STATS_LOG_TRANSPORT:
                        switch (statisticByteOffset)
                        {
                        case 8://number of hardware resets
                            deviceStats->sataStatistics.numberOfHardwareResets.isThresholdValid = true;
                            deviceStats->sataStatistics.numberOfHardwareResets.thresholdNotificationEnabled = notificationEnabled;
                            deviceStats->sataStatistics.numberOfHardwareResets.threshType = (eThresholdType)comparisonType;
                            deviceStats->sataStatistics.numberOfHardwareResets.nonValidityTrigger = nonValidityTrigger;
                            deviceStats->sataStatistics.numberOfHardwareResets.validityTrigger = validityTrigger;
                            deviceStats->sataStatistics.numberOfHardwareResets.threshold = thresholdValue;
                            break;
                        case 16://number of ASR events
                            deviceStats->sataStatistics.numberOfASREvents.isThresholdValid = true;
                            deviceStats->sataStatistics.numberOfASREvents.thresholdNotificationEnabled = notificationEnabled;
                            deviceStats->sataStatistics.numberOfASREvents.threshType = (eThresholdType)comparisonType;
                            deviceStats->sataStatistics.numberOfASREvents.nonValidityTrigger = nonValidityTrigger;
                            deviceStats->sataStatistics.numberOfASREvents.validityTrigger = validityTrigger;
                            deviceStats->sataStatistics.numberOfASREvents.threshold = thresholdValue;
                            break;
                        case 24://number of interface CRC errors
                            deviceStats->sataStatistics.numberOfInterfaceCRCErrors.isThresholdValid = true;
                            deviceStats->sataStatistics.numberOfInterfaceCRCErrors.thresholdNotificationEnabled = notificationEnabled;
                            deviceStats->sataStatistics.numberOfInterfaceCRCErrors.threshType = (eThresholdType)comparisonType;
                            deviceStats->sataStatistics.numberOfInterfaceCRCErrors.nonValidityTrigger = nonValidityTrigger;
                            deviceStats->sataStatistics.numberOfInterfaceCRCErrors.validityTrigger = validityTrigger;
                            deviceStats->sataStatistics.numberOfInterfaceCRCErrors.threshold = thresholdValue;
                            break;
                        default:
                            //unknown
                            break;
                        }
                        break;
                    case ATA_DEVICE_STATS_LOG_SSD:
                        switch (statisticByteOffset)
                        {
                        case 8://percent used indicator
                            deviceStats->sataStatistics.percentageUsedIndicator.isThresholdValid = true;
                            deviceStats->sataStatistics.percentageUsedIndicator.thresholdNotificationEnabled = notificationEnabled;
                            deviceStats->sataStatistics.percentageUsedIndicator.threshType = (eThresholdType)comparisonType;
                            deviceStats->sataStatistics.percentageUsedIndicator.nonValidityTrigger = nonValidityTrigger;
                            deviceStats->sataStatistics.percentageUsedIndicator.validityTrigger = validityTrigger;
                            deviceStats->sataStatistics.percentageUsedIndicator.threshold = thresholdValue;
                            break;
                        default:
                            //unknown
                            break;
                        }
                        break;
                    case ATA_DEVICE_STATS_LOG_ZONED_DEVICE:
                        switch (statisticByteOffset)
                        {
                        case 8://maximum open zones
                            deviceStats->sataStatistics.maximumOpenZones.isThresholdValid = true;
                            deviceStats->sataStatistics.maximumOpenZones.thresholdNotificationEnabled = notificationEnabled;
                            deviceStats->sataStatistics.maximumOpenZones.threshType = (eThresholdType)comparisonType;
                            deviceStats->sataStatistics.maximumOpenZones.nonValidityTrigger = nonValidityTrigger;
                            deviceStats->sataStatistics.maximumOpenZones.validityTrigger = validityTrigger;
                            deviceStats->sataStatistics.maximumOpenZones.threshold = thresholdValue;
                            break;
                        case 16://maximum explicitly open zones
                            deviceStats->sataStatistics.maximumExplicitlyOpenZones.isThresholdValid = true;
                            deviceStats->sataStatistics.maximumExplicitlyOpenZones.thresholdNotificationEnabled = notificationEnabled;
                            deviceStats->sataStatistics.maximumExplicitlyOpenZones.threshType = (eThresholdType)comparisonType;
                            deviceStats->sataStatistics.maximumExplicitlyOpenZones.nonValidityTrigger = nonValidityTrigger;
                            deviceStats->sataStatistics.maximumExplicitlyOpenZones.validityTrigger = validityTrigger;
                            deviceStats->sataStatistics.maximumExplicitlyOpenZones.threshold = thresholdValue;
                            break;
                        case 24://maximum implicitly open zones
                            deviceStats->sataStatistics.maximumImplicitlyOpenZones.isThresholdValid = true;
                            deviceStats->sataStatistics.maximumImplicitlyOpenZones.thresholdNotificationEnabled = notificationEnabled;
                            deviceStats->sataStatistics.maximumImplicitlyOpenZones.threshType = (eThresholdType)comparisonType;
                            deviceStats->sataStatistics.maximumImplicitlyOpenZones.nonValidityTrigger = nonValidityTrigger;
                            deviceStats->sataStatistics.maximumImplicitlyOpenZones.validityTrigger = validityTrigger;
                            deviceStats->sataStatistics.maximumImplicitlyOpenZones.threshold = thresholdValue;
                            break;
                        case 32://minimum empty zones
                            deviceStats->sataStatistics.minimumEmptyZones.isThresholdValid = true;
                            deviceStats->sataStatistics.minimumEmptyZones.thresholdNotificationEnabled = notificationEnabled;
                            deviceStats->sataStatistics.minimumEmptyZones.threshType = (eThresholdType)comparisonType;
                            deviceStats->sataStatistics.minimumEmptyZones.nonValidityTrigger = nonValidityTrigger;
                            deviceStats->sataStatistics.minimumEmptyZones.validityTrigger = validityTrigger;
                            deviceStats->sataStatistics.minimumEmptyZones.threshold = thresholdValue;
                            break;
                        case 40://maximum non-sequential zones
                            deviceStats->sataStatistics.maximumNonSequentialZones.isThresholdValid = true;
                            deviceStats->sataStatistics.maximumNonSequentialZones.thresholdNotificationEnabled = notificationEnabled;
                            deviceStats->sataStatistics.maximumNonSequentialZones.threshType = (eThresholdType)comparisonType;
                            deviceStats->sataStatistics.maximumNonSequentialZones.nonValidityTrigger = nonValidityTrigger;
                            deviceStats->sataStatistics.maximumNonSequentialZones.validityTrigger = validityTrigger;
                            deviceStats->sataStatistics.maximumNonSequentialZones.threshold = thresholdValue;
                            break;
                        case 48://zones emptied
                            deviceStats->sataStatistics.zonesEmptied.isThresholdValid = true;
                            deviceStats->sataStatistics.zonesEmptied.thresholdNotificationEnabled = notificationEnabled;
                            deviceStats->sataStatistics.zonesEmptied.threshType = (eThresholdType)comparisonType;
                            deviceStats->sataStatistics.zonesEmptied.nonValidityTrigger = nonValidityTrigger;
                            deviceStats->sataStatistics.zonesEmptied.validityTrigger = validityTrigger;
                            deviceStats->sataStatistics.zonesEmptied.threshold = thresholdValue;
                            break;
                        case 56://suboptimal write commands
                            deviceStats->sataStatistics.suboptimalWriteCommands.isThresholdValid = true;
                            deviceStats->sataStatistics.suboptimalWriteCommands.thresholdNotificationEnabled = notificationEnabled;
                            deviceStats->sataStatistics.suboptimalWriteCommands.threshType = (eThresholdType)comparisonType;
                            deviceStats->sataStatistics.suboptimalWriteCommands.nonValidityTrigger = nonValidityTrigger;
                            deviceStats->sataStatistics.suboptimalWriteCommands.validityTrigger = validityTrigger;
                            deviceStats->sataStatistics.suboptimalWriteCommands.threshold = thresholdValue;
                            break;
                        case 64://commands exceeding optimal limit
                            deviceStats->sataStatistics.commandsExceedingOptimalLimit.isThresholdValid = true;
                            deviceStats->sataStatistics.commandsExceedingOptimalLimit.thresholdNotificationEnabled = notificationEnabled;
                            deviceStats->sataStatistics.commandsExceedingOptimalLimit.threshType = (eThresholdType)comparisonType;
                            deviceStats->sataStatistics.commandsExceedingOptimalLimit.nonValidityTrigger = nonValidityTrigger;
                            deviceStats->sataStatistics.commandsExceedingOptimalLimit.validityTrigger = validityTrigger;
                            deviceStats->sataStatistics.commandsExceedingOptimalLimit.threshold = thresholdValue;
                            break;
                        case 72://failed explicit opens
                            deviceStats->sataStatistics.failedExplicitOpens.isThresholdValid = true;
                            deviceStats->sataStatistics.failedExplicitOpens.thresholdNotificationEnabled = notificationEnabled;
                            deviceStats->sataStatistics.failedExplicitOpens.threshType = (eThresholdType)comparisonType;
                            deviceStats->sataStatistics.failedExplicitOpens.nonValidityTrigger = nonValidityTrigger;
                            deviceStats->sataStatistics.failedExplicitOpens.validityTrigger = validityTrigger;
                            deviceStats->sataStatistics.failedExplicitOpens.threshold = thresholdValue;
                            break;
                        case 80://read rule violations
                            deviceStats->sataStatistics.readRuleViolations.isThresholdValid = true;
                            deviceStats->sataStatistics.readRuleViolations.thresholdNotificationEnabled = notificationEnabled;
                            deviceStats->sataStatistics.readRuleViolations.threshType = (eThresholdType)comparisonType;
                            deviceStats->sataStatistics.readRuleViolations.nonValidityTrigger = nonValidityTrigger;
                            deviceStats->sataStatistics.readRuleViolations.validityTrigger = validityTrigger;
                            deviceStats->sataStatistics.readRuleViolations.threshold = thresholdValue;
                            break;
                        case 88://write rule violations
                            deviceStats->sataStatistics.writeRuleViolations.isThresholdValid = true;
                            deviceStats->sataStatistics.writeRuleViolations.thresholdNotificationEnabled = notificationEnabled;
                            deviceStats->sataStatistics.writeRuleViolations.threshType = (eThresholdType)comparisonType;
                            deviceStats->sataStatistics.writeRuleViolations.nonValidityTrigger = nonValidityTrigger;
                            deviceStats->sataStatistics.writeRuleViolations.validityTrigger = validityTrigger;
                            deviceStats->sataStatistics.writeRuleViolations.threshold = thresholdValue;
                            break;
                        default:
                            break;
                        }
                    default:
                        //unknown page, break
                        break;
                    }
                }
            }
            safe_Free(devStatsNotificationsLog);
        }
        if (SUCCESS == get_ATA_Log(device, ATA_LOG_DEVICE_STATISTICS, NULL, NULL, true, true, true, deviceStatsLog, deviceStatsSize, NULL, 0,0))
        {
            ret = SUCCESS;
            uint32_t offset = 0;//start offset 1 sector to get to the general statistics
            uint64_t *qwordPtrDeviceStatsLog = NULL;
            for (uint8_t pageIter = 0; pageIter < deviceStatsLog[8]; ++pageIter)
            {
                //statistics flags:
                //bit 63 = supported
                //bit 62 = valid value
                //bit 61 = normalized
                //bit 60 = supports DSN
                //bit 59 = monitored condition met
                //bits 58-56 are reserved
                offset = deviceStatsLog[9 + pageIter] * LEGACY_DRIVE_SEC_SIZE;
                if (offset > deviceStatsSize)
                {
                    //this exists for the hack loop above
                    break;
                }
                qwordPtrDeviceStatsLog = (uint64_t*)&deviceStatsLog[offset];
                switch (deviceStatsLog[9 + pageIter])
                {
                case 0://supported pages page...
                    break;
                case 1://general statistics
                    if (0x01 == M_Byte2(qwordPtrDeviceStatsLog[0]))
                    {
                        deviceStats->sataStatistics.generalStatisticsSupported = true;
                        if (qwordPtrDeviceStatsLog[1] & BIT63)
                        {
                            deviceStats->sataStatistics.lifetimePoweronResets.isSupported = true;
                            deviceStats->sataStatistics.lifetimePoweronResets.isValueValid = qwordPtrDeviceStatsLog[1] & BIT62;
                            deviceStats->sataStatistics.lifetimePoweronResets.isNormalized = qwordPtrDeviceStatsLog[1] & BIT61;
                            deviceStats->sataStatistics.lifetimePoweronResets.supportsNotification = qwordPtrDeviceStatsLog[1] & BIT60;
                            deviceStats->sataStatistics.lifetimePoweronResets.monitoredConditionMet = qwordPtrDeviceStatsLog[1] & BIT59;
                            deviceStats->sataStatistics.lifetimePoweronResets.statisticValue = M_DoubleWord0(qwordPtrDeviceStatsLog[1]);
                            ++deviceStats->sataStatistics.statisticsPopulated;
                        }
                        if (qwordPtrDeviceStatsLog[2] & BIT63)
                        {
                            deviceStats->sataStatistics.powerOnHours.isSupported = true;
                            deviceStats->sataStatistics.powerOnHours.isValueValid = qwordPtrDeviceStatsLog[2] & BIT62;
                            deviceStats->sataStatistics.powerOnHours.isNormalized = qwordPtrDeviceStatsLog[2] & BIT61;
                            deviceStats->sataStatistics.powerOnHours.supportsNotification = qwordPtrDeviceStatsLog[2] & BIT60;
                            deviceStats->sataStatistics.powerOnHours.monitoredConditionMet = qwordPtrDeviceStatsLog[2] & BIT59;
                            deviceStats->sataStatistics.powerOnHours.statisticValue = M_DoubleWord0(qwordPtrDeviceStatsLog[2]);
                            ++deviceStats->sataStatistics.statisticsPopulated;
                        }
                        if (qwordPtrDeviceStatsLog[3] & BIT63)
                        {
                            deviceStats->sataStatistics.logicalSectorsWritten.isSupported = true;
                            deviceStats->sataStatistics.logicalSectorsWritten.isValueValid = qwordPtrDeviceStatsLog[3] & BIT62;
                            deviceStats->sataStatistics.logicalSectorsWritten.isNormalized = qwordPtrDeviceStatsLog[3] & BIT61;
                            deviceStats->sataStatistics.logicalSectorsWritten.supportsNotification = qwordPtrDeviceStatsLog[3] & BIT60;
                            deviceStats->sataStatistics.logicalSectorsWritten.monitoredConditionMet = qwordPtrDeviceStatsLog[3] & BIT59;
                            deviceStats->sataStatistics.logicalSectorsWritten.statisticValue = qwordPtrDeviceStatsLog[3] & MAX_48_BIT_LBA;
                            ++deviceStats->sataStatistics.statisticsPopulated;
                        }
                        if (qwordPtrDeviceStatsLog[4] & BIT63)
                        {
                            deviceStats->sataStatistics.numberOfWriteCommands.isSupported = true;
                            deviceStats->sataStatistics.numberOfWriteCommands.isValueValid = qwordPtrDeviceStatsLog[4] & BIT62;
                            deviceStats->sataStatistics.numberOfWriteCommands.isNormalized = qwordPtrDeviceStatsLog[4] & BIT61;
                            deviceStats->sataStatistics.numberOfWriteCommands.supportsNotification = qwordPtrDeviceStatsLog[4] & BIT60;
                            deviceStats->sataStatistics.numberOfWriteCommands.monitoredConditionMet = qwordPtrDeviceStatsLog[4] & BIT59;
                            deviceStats->sataStatistics.numberOfWriteCommands.statisticValue = qwordPtrDeviceStatsLog[4] & MAX_48_BIT_LBA;
                            ++deviceStats->sataStatistics.statisticsPopulated;
                        }
                        if (qwordPtrDeviceStatsLog[5] & BIT63)
                        {
                            deviceStats->sataStatistics.logicalSectorsRead.isSupported = true;
                            deviceStats->sataStatistics.logicalSectorsRead.isValueValid = qwordPtrDeviceStatsLog[5] & BIT62;
                            deviceStats->sataStatistics.logicalSectorsRead.isNormalized = qwordPtrDeviceStatsLog[5] & BIT61;
                            deviceStats->sataStatistics.logicalSectorsRead.supportsNotification = qwordPtrDeviceStatsLog[5] & BIT60;
                            deviceStats->sataStatistics.logicalSectorsRead.monitoredConditionMet = qwordPtrDeviceStatsLog[5] & BIT59;
                            deviceStats->sataStatistics.logicalSectorsRead.statisticValue = qwordPtrDeviceStatsLog[5] & MAX_48_BIT_LBA;
                            ++deviceStats->sataStatistics.statisticsPopulated;
                        }
                        if (qwordPtrDeviceStatsLog[6] & BIT63)
                        {
                            deviceStats->sataStatistics.numberOfReadCommands.isSupported = true;
                            deviceStats->sataStatistics.numberOfReadCommands.isValueValid = qwordPtrDeviceStatsLog[6] & BIT62;
                            deviceStats->sataStatistics.numberOfReadCommands.isNormalized = qwordPtrDeviceStatsLog[6] & BIT61;
                            deviceStats->sataStatistics.numberOfReadCommands.supportsNotification = qwordPtrDeviceStatsLog[6] & BIT60;
                            deviceStats->sataStatistics.numberOfReadCommands.monitoredConditionMet = qwordPtrDeviceStatsLog[6] & BIT59;
                            deviceStats->sataStatistics.numberOfReadCommands.statisticValue = qwordPtrDeviceStatsLog[6] & MAX_48_BIT_LBA;
                            ++deviceStats->sataStatistics.statisticsPopulated;
                        }
                        if (qwordPtrDeviceStatsLog[7] & BIT63)
                        {
                            deviceStats->sataStatistics.dateAndTimeTimestamp.isSupported = true;
                            deviceStats->sataStatistics.dateAndTimeTimestamp.isValueValid = qwordPtrDeviceStatsLog[7] & BIT62;
                            deviceStats->sataStatistics.dateAndTimeTimestamp.isNormalized = qwordPtrDeviceStatsLog[7] & BIT61;
                            deviceStats->sataStatistics.dateAndTimeTimestamp.supportsNotification = qwordPtrDeviceStatsLog[7] & BIT60;
                            deviceStats->sataStatistics.dateAndTimeTimestamp.monitoredConditionMet = qwordPtrDeviceStatsLog[7] & BIT59;
                            deviceStats->sataStatistics.dateAndTimeTimestamp.statisticValue = qwordPtrDeviceStatsLog[7] & MAX_48_BIT_LBA;
                            ++deviceStats->sataStatistics.statisticsPopulated;
                        }
                        if (qwordPtrDeviceStatsLog[8] & BIT63)
                        {
                            deviceStats->sataStatistics.pendingErrorCount.isSupported = true;
                            deviceStats->sataStatistics.pendingErrorCount.isValueValid = qwordPtrDeviceStatsLog[8] & BIT62;
                            deviceStats->sataStatistics.pendingErrorCount.isNormalized = qwordPtrDeviceStatsLog[8] & BIT61;
                            deviceStats->sataStatistics.pendingErrorCount.supportsNotification = qwordPtrDeviceStatsLog[8] & BIT60;
                            deviceStats->sataStatistics.pendingErrorCount.monitoredConditionMet = qwordPtrDeviceStatsLog[8] & BIT59;
                            deviceStats->sataStatistics.pendingErrorCount.statisticValue = M_DoubleWord0(qwordPtrDeviceStatsLog[8]);
                            ++deviceStats->sataStatistics.statisticsPopulated;
                        }
                        if (qwordPtrDeviceStatsLog[9] & BIT63)
                        {
                            deviceStats->sataStatistics.workloadUtilization.isSupported = true;
                            deviceStats->sataStatistics.workloadUtilization.isValueValid = qwordPtrDeviceStatsLog[9] & BIT62;
                            deviceStats->sataStatistics.workloadUtilization.isNormalized = qwordPtrDeviceStatsLog[9] & BIT61;
                            deviceStats->sataStatistics.workloadUtilization.supportsNotification = qwordPtrDeviceStatsLog[9] & BIT60;
                            deviceStats->sataStatistics.workloadUtilization.monitoredConditionMet = qwordPtrDeviceStatsLog[9] & BIT59;
                            deviceStats->sataStatistics.workloadUtilization.statisticValue = M_Word0(qwordPtrDeviceStatsLog[9]);
                            ++deviceStats->sataStatistics.statisticsPopulated;
                        }
                        if (qwordPtrDeviceStatsLog[10] & BIT63)
                        {
                            deviceStats->sataStatistics.utilizationUsageRate.isSupported = true;
                            deviceStats->sataStatistics.utilizationUsageRate.isValueValid = qwordPtrDeviceStatsLog[10] & BIT62;
                            deviceStats->sataStatistics.utilizationUsageRate.isNormalized = qwordPtrDeviceStatsLog[10] & BIT61;
                            deviceStats->sataStatistics.utilizationUsageRate.supportsNotification = qwordPtrDeviceStatsLog[10] & BIT60;
                            deviceStats->sataStatistics.utilizationUsageRate.monitoredConditionMet = qwordPtrDeviceStatsLog[10] & BIT59;
                            deviceStats->sataStatistics.utilizationUsageRate.statisticValue = qwordPtrDeviceStatsLog[10] & MAX_48_BIT_LBA;
                            ++deviceStats->sataStatistics.statisticsPopulated;
                        }
                        if (qwordPtrDeviceStatsLog[11] & BIT63)
                        {
                            deviceStats->sataStatistics.resourceAvailability.isSupported = true;
                            deviceStats->sataStatistics.resourceAvailability.isValueValid = qwordPtrDeviceStatsLog[11] & BIT62;
                            deviceStats->sataStatistics.resourceAvailability.isNormalized = qwordPtrDeviceStatsLog[11] & BIT61;
                            deviceStats->sataStatistics.resourceAvailability.supportsNotification = qwordPtrDeviceStatsLog[11] & BIT60;
                            deviceStats->sataStatistics.resourceAvailability.monitoredConditionMet = qwordPtrDeviceStatsLog[11] & BIT59;
                            deviceStats->sataStatistics.resourceAvailability.statisticValue = M_Word0(qwordPtrDeviceStatsLog[11]);
                            ++deviceStats->sataStatistics.statisticsPopulated;
                        }
                        if (qwordPtrDeviceStatsLog[12] & BIT63)
                        {
                            deviceStats->sataStatistics.randomWriteResourcesUsed.isSupported = true;
                            deviceStats->sataStatistics.randomWriteResourcesUsed.isValueValid = qwordPtrDeviceStatsLog[12] & BIT62;
                            deviceStats->sataStatistics.randomWriteResourcesUsed.isNormalized = qwordPtrDeviceStatsLog[12] & BIT61;
                            deviceStats->sataStatistics.randomWriteResourcesUsed.supportsNotification = qwordPtrDeviceStatsLog[12] & BIT60;
                            deviceStats->sataStatistics.randomWriteResourcesUsed.monitoredConditionMet = qwordPtrDeviceStatsLog[12] & BIT59;
                            deviceStats->sataStatistics.randomWriteResourcesUsed.statisticValue = M_Byte0(qwordPtrDeviceStatsLog[12]);
                            ++deviceStats->sataStatistics.statisticsPopulated;
                        }
                    }
                    break;
                case 2://free fall statistics
                    if (0x02 == M_Byte2(qwordPtrDeviceStatsLog[0]))
                    {
                        deviceStats->sataStatistics.freeFallStatisticsSupported = true;
                        if (qwordPtrDeviceStatsLog[1] & BIT63)
                        {
                            deviceStats->sataStatistics.numberOfFreeFallEventsDetected.isSupported = true;
                            deviceStats->sataStatistics.numberOfFreeFallEventsDetected.isValueValid = qwordPtrDeviceStatsLog[1] & BIT62;
                            deviceStats->sataStatistics.numberOfFreeFallEventsDetected.isNormalized = qwordPtrDeviceStatsLog[1] & BIT61;
                            deviceStats->sataStatistics.numberOfFreeFallEventsDetected.supportsNotification = qwordPtrDeviceStatsLog[1] & BIT60;
                            deviceStats->sataStatistics.numberOfFreeFallEventsDetected.monitoredConditionMet = qwordPtrDeviceStatsLog[1] & BIT59;
                            deviceStats->sataStatistics.numberOfFreeFallEventsDetected.statisticValue = M_DoubleWord0(qwordPtrDeviceStatsLog[1]);
                            ++deviceStats->sataStatistics.statisticsPopulated;
                        }
                        if (qwordPtrDeviceStatsLog[2] & BIT63)
                        {
                            deviceStats->sataStatistics.overlimitShockEvents.isSupported = true;
                            deviceStats->sataStatistics.overlimitShockEvents.isValueValid = qwordPtrDeviceStatsLog[2] & BIT62;
                            deviceStats->sataStatistics.overlimitShockEvents.isNormalized = qwordPtrDeviceStatsLog[2] & BIT61;
                            deviceStats->sataStatistics.overlimitShockEvents.supportsNotification = qwordPtrDeviceStatsLog[2] & BIT60;
                            deviceStats->sataStatistics.overlimitShockEvents.monitoredConditionMet = qwordPtrDeviceStatsLog[2] & BIT59;
                            deviceStats->sataStatistics.overlimitShockEvents.statisticValue = M_DoubleWord0(qwordPtrDeviceStatsLog[2]);
                            ++deviceStats->sataStatistics.statisticsPopulated;
                        }
                    }
                    break;
                case 3://rotating media statistics
                    if (0x03 == M_Byte2(qwordPtrDeviceStatsLog[0]))
                    {
                        deviceStats->sataStatistics.rotatingMediaStatisticsSupported = true;
                        if (qwordPtrDeviceStatsLog[1] & BIT63)
                        {
                            deviceStats->sataStatistics.spindleMotorPoweronHours.isSupported = true;
                            deviceStats->sataStatistics.spindleMotorPoweronHours.isValueValid = qwordPtrDeviceStatsLog[1] & BIT62;
                            deviceStats->sataStatistics.spindleMotorPoweronHours.isNormalized = qwordPtrDeviceStatsLog[1] & BIT61;
                            deviceStats->sataStatistics.spindleMotorPoweronHours.supportsNotification = qwordPtrDeviceStatsLog[1] & BIT60;
                            deviceStats->sataStatistics.spindleMotorPoweronHours.monitoredConditionMet = qwordPtrDeviceStatsLog[1] & BIT59;
                            deviceStats->sataStatistics.spindleMotorPoweronHours.statisticValue = M_DoubleWord0(qwordPtrDeviceStatsLog[1]);
                            ++deviceStats->sataStatistics.statisticsPopulated;
                        }
                        if (qwordPtrDeviceStatsLog[2] & BIT63)
                        {
                            deviceStats->sataStatistics.headFlyingHours.isSupported = true;
                            deviceStats->sataStatistics.headFlyingHours.isValueValid = qwordPtrDeviceStatsLog[2] & BIT62;
                            deviceStats->sataStatistics.headFlyingHours.isNormalized = qwordPtrDeviceStatsLog[2] & BIT61;
                            deviceStats->sataStatistics.headFlyingHours.supportsNotification = qwordPtrDeviceStatsLog[2] & BIT60;
                            deviceStats->sataStatistics.headFlyingHours.monitoredConditionMet = qwordPtrDeviceStatsLog[2] & BIT59;
                            deviceStats->sataStatistics.headFlyingHours.statisticValue = M_DoubleWord0(qwordPtrDeviceStatsLog[2]);
                            ++deviceStats->sataStatistics.statisticsPopulated;
                        }
                        if (qwordPtrDeviceStatsLog[3] & BIT63)
                        {
                            deviceStats->sataStatistics.headLoadEvents.isSupported = true;
                            deviceStats->sataStatistics.headLoadEvents.isValueValid = qwordPtrDeviceStatsLog[3] & BIT62;
                            deviceStats->sataStatistics.headLoadEvents.isNormalized = qwordPtrDeviceStatsLog[3] & BIT61;
                            deviceStats->sataStatistics.headLoadEvents.supportsNotification = qwordPtrDeviceStatsLog[3] & BIT60;
                            deviceStats->sataStatistics.headLoadEvents.monitoredConditionMet = qwordPtrDeviceStatsLog[3] & BIT59;
                            deviceStats->sataStatistics.headLoadEvents.statisticValue = M_DoubleWord0(qwordPtrDeviceStatsLog[3]);
                            ++deviceStats->sataStatistics.statisticsPopulated;
                        }
                        if (qwordPtrDeviceStatsLog[4] & BIT63)
                        {
                            deviceStats->sataStatistics.numberOfReallocatedLogicalSectors.isSupported = true;
                            deviceStats->sataStatistics.numberOfReallocatedLogicalSectors.isValueValid = qwordPtrDeviceStatsLog[4] & BIT62;
                            deviceStats->sataStatistics.numberOfReallocatedLogicalSectors.isNormalized = qwordPtrDeviceStatsLog[4] & BIT61;
                            deviceStats->sataStatistics.numberOfReallocatedLogicalSectors.supportsNotification = qwordPtrDeviceStatsLog[4] & BIT60;
                            deviceStats->sataStatistics.numberOfReallocatedLogicalSectors.monitoredConditionMet = qwordPtrDeviceStatsLog[4] & BIT59;
                            deviceStats->sataStatistics.numberOfReallocatedLogicalSectors.statisticValue = M_DoubleWord0(qwordPtrDeviceStatsLog[4]);
                            ++deviceStats->sataStatistics.statisticsPopulated;
                        }
                        if (qwordPtrDeviceStatsLog[5] & BIT63)
                        {
                            deviceStats->sataStatistics.readRecoveryAttempts.isSupported = true;
                            deviceStats->sataStatistics.readRecoveryAttempts.isValueValid = qwordPtrDeviceStatsLog[5] & BIT62;
                            deviceStats->sataStatistics.readRecoveryAttempts.isNormalized = qwordPtrDeviceStatsLog[5] & BIT61;
                            deviceStats->sataStatistics.readRecoveryAttempts.supportsNotification = qwordPtrDeviceStatsLog[5] & BIT60;
                            deviceStats->sataStatistics.readRecoveryAttempts.monitoredConditionMet = qwordPtrDeviceStatsLog[5] & BIT59;
                            deviceStats->sataStatistics.readRecoveryAttempts.statisticValue = M_DoubleWord0(qwordPtrDeviceStatsLog[5]);
                            ++deviceStats->sataStatistics.statisticsPopulated;
                        }
                        if (qwordPtrDeviceStatsLog[6] & BIT63)
                        {
                            deviceStats->sataStatistics.numberOfMechanicalStartFailures.isSupported = true;
                            deviceStats->sataStatistics.numberOfMechanicalStartFailures.isValueValid = qwordPtrDeviceStatsLog[6] & BIT62;
                            deviceStats->sataStatistics.numberOfMechanicalStartFailures.isNormalized = qwordPtrDeviceStatsLog[6] & BIT61;
                            deviceStats->sataStatistics.numberOfMechanicalStartFailures.supportsNotification = qwordPtrDeviceStatsLog[6] & BIT60;
                            deviceStats->sataStatistics.numberOfMechanicalStartFailures.monitoredConditionMet = qwordPtrDeviceStatsLog[6] & BIT59;
                            deviceStats->sataStatistics.numberOfMechanicalStartFailures.statisticValue = M_DoubleWord0(qwordPtrDeviceStatsLog[6]);
                            ++deviceStats->sataStatistics.statisticsPopulated;
                        }
                        if (qwordPtrDeviceStatsLog[7] & BIT63)
                        {
                            deviceStats->sataStatistics.numberOfReallocationCandidateLogicalSectors.isSupported = true;
                            deviceStats->sataStatistics.numberOfReallocationCandidateLogicalSectors.isValueValid = qwordPtrDeviceStatsLog[7] & BIT62;
                            deviceStats->sataStatistics.numberOfReallocationCandidateLogicalSectors.isNormalized = qwordPtrDeviceStatsLog[7] & BIT61;
                            deviceStats->sataStatistics.numberOfReallocationCandidateLogicalSectors.supportsNotification = qwordPtrDeviceStatsLog[7] & BIT60;
                            deviceStats->sataStatistics.numberOfReallocationCandidateLogicalSectors.monitoredConditionMet = qwordPtrDeviceStatsLog[7] & BIT59;
                            deviceStats->sataStatistics.numberOfReallocationCandidateLogicalSectors.statisticValue = M_DoubleWord0(qwordPtrDeviceStatsLog[7]);
                            ++deviceStats->sataStatistics.statisticsPopulated;
                        }
                        if (qwordPtrDeviceStatsLog[8] & BIT63)
                        {
                            deviceStats->sataStatistics.numberOfHighPriorityUnloadEvents.isSupported = true;
                            deviceStats->sataStatistics.numberOfHighPriorityUnloadEvents.isValueValid = qwordPtrDeviceStatsLog[8] & BIT62;
                            deviceStats->sataStatistics.numberOfHighPriorityUnloadEvents.isNormalized = qwordPtrDeviceStatsLog[8] & BIT61;
                            deviceStats->sataStatistics.numberOfHighPriorityUnloadEvents.supportsNotification = qwordPtrDeviceStatsLog[8] & BIT60;
                            deviceStats->sataStatistics.numberOfHighPriorityUnloadEvents.monitoredConditionMet = qwordPtrDeviceStatsLog[8] & BIT59;
                            deviceStats->sataStatistics.numberOfHighPriorityUnloadEvents.statisticValue = M_DoubleWord0(qwordPtrDeviceStatsLog[8]);
                            ++deviceStats->sataStatistics.statisticsPopulated;
                        }
                    }
                    break;
                case 4://general errors statistics
                    if (0x04 == M_Byte2(qwordPtrDeviceStatsLog[0]))
                    {
                        deviceStats->sataStatistics.generalErrorsStatisticsSupported = true;
                        if (qwordPtrDeviceStatsLog[1] & BIT63)
                        {
                            deviceStats->sataStatistics.numberOfReportedUncorrectableErrors.isSupported = true;
                            deviceStats->sataStatistics.numberOfReportedUncorrectableErrors.isValueValid = qwordPtrDeviceStatsLog[1] & BIT62;
                            deviceStats->sataStatistics.numberOfReportedUncorrectableErrors.isNormalized = qwordPtrDeviceStatsLog[1] & BIT61;
                            deviceStats->sataStatistics.numberOfReportedUncorrectableErrors.supportsNotification = qwordPtrDeviceStatsLog[1] & BIT60;
                            deviceStats->sataStatistics.numberOfReportedUncorrectableErrors.monitoredConditionMet = qwordPtrDeviceStatsLog[1] & BIT59;
                            deviceStats->sataStatistics.numberOfReportedUncorrectableErrors.statisticValue = M_DoubleWord0(qwordPtrDeviceStatsLog[1]);
                            ++deviceStats->sataStatistics.statisticsPopulated;
                        }
                        if (qwordPtrDeviceStatsLog[2] & BIT63)
                        {
                            deviceStats->sataStatistics.numberOfResetsBetweenCommandAcceptanceAndCommandCompletion.isSupported = true;
                            deviceStats->sataStatistics.numberOfResetsBetweenCommandAcceptanceAndCommandCompletion.isValueValid = qwordPtrDeviceStatsLog[2] & BIT62;
                            deviceStats->sataStatistics.numberOfResetsBetweenCommandAcceptanceAndCommandCompletion.isNormalized = qwordPtrDeviceStatsLog[2] & BIT61;
                            deviceStats->sataStatistics.numberOfResetsBetweenCommandAcceptanceAndCommandCompletion.supportsNotification = qwordPtrDeviceStatsLog[2] & BIT60;
                            deviceStats->sataStatistics.numberOfResetsBetweenCommandAcceptanceAndCommandCompletion.monitoredConditionMet = qwordPtrDeviceStatsLog[2] & BIT59;
                            deviceStats->sataStatistics.numberOfResetsBetweenCommandAcceptanceAndCommandCompletion.statisticValue = M_DoubleWord0(qwordPtrDeviceStatsLog[2]);
                            ++deviceStats->sataStatistics.statisticsPopulated;
                        }
                        if (qwordPtrDeviceStatsLog[3] & BIT63)
                        {
                            deviceStats->sataStatistics.physicalElementStatusChanged.isSupported = true;
                            deviceStats->sataStatistics.physicalElementStatusChanged.isValueValid = qwordPtrDeviceStatsLog[3] & BIT62;
                            deviceStats->sataStatistics.physicalElementStatusChanged.isNormalized = qwordPtrDeviceStatsLog[3] & BIT61;
                            deviceStats->sataStatistics.physicalElementStatusChanged.supportsNotification = qwordPtrDeviceStatsLog[3] & BIT60;
                            deviceStats->sataStatistics.physicalElementStatusChanged.monitoredConditionMet = qwordPtrDeviceStatsLog[3] & BIT59;
                            deviceStats->sataStatistics.physicalElementStatusChanged.statisticValue = M_DoubleWord0(qwordPtrDeviceStatsLog[3]);
                            ++deviceStats->sataStatistics.statisticsPopulated;
                        }
                    }
                    break;
                case 5://temperature statistics
                    if (0x05 == M_Byte2(qwordPtrDeviceStatsLog[0]))
                    {
                        deviceStats->sataStatistics.temperatureStatisticsSupported = true;
                        if (qwordPtrDeviceStatsLog[1] & BIT63)
                        {
                            deviceStats->sataStatistics.currentTemperature.isSupported = true;
                            deviceStats->sataStatistics.currentTemperature.isValueValid = qwordPtrDeviceStatsLog[1] & BIT62;
                            deviceStats->sataStatistics.currentTemperature.isNormalized = qwordPtrDeviceStatsLog[1] & BIT61;
                            deviceStats->sataStatistics.currentTemperature.supportsNotification = qwordPtrDeviceStatsLog[1] & BIT60;
                            deviceStats->sataStatistics.currentTemperature.monitoredConditionMet = qwordPtrDeviceStatsLog[1] & BIT59;
                            deviceStats->sataStatistics.currentTemperature.statisticValue = M_Byte0(qwordPtrDeviceStatsLog[1]);
                            ++deviceStats->sataStatistics.statisticsPopulated;
                        }
                        if (qwordPtrDeviceStatsLog[2] & BIT63)
                        {
                            deviceStats->sataStatistics.averageShortTermTemperature.isSupported = true;
                            deviceStats->sataStatistics.averageShortTermTemperature.isValueValid = qwordPtrDeviceStatsLog[2] & BIT62;
                            deviceStats->sataStatistics.averageShortTermTemperature.isNormalized = qwordPtrDeviceStatsLog[2] & BIT61;
                            deviceStats->sataStatistics.averageShortTermTemperature.supportsNotification = qwordPtrDeviceStatsLog[2] & BIT60;
                            deviceStats->sataStatistics.averageShortTermTemperature.monitoredConditionMet = qwordPtrDeviceStatsLog[2] & BIT59;
                            deviceStats->sataStatistics.averageShortTermTemperature.statisticValue = M_Byte0(qwordPtrDeviceStatsLog[2]);
                            ++deviceStats->sataStatistics.statisticsPopulated;
                        }
                        if (qwordPtrDeviceStatsLog[3] & BIT63)
                        {
                            deviceStats->sataStatistics.averageLongTermTemperature.isSupported = true;
                            deviceStats->sataStatistics.averageLongTermTemperature.isValueValid = qwordPtrDeviceStatsLog[3] & BIT62;
                            deviceStats->sataStatistics.averageLongTermTemperature.isNormalized = qwordPtrDeviceStatsLog[3] & BIT61;
                            deviceStats->sataStatistics.averageLongTermTemperature.supportsNotification = qwordPtrDeviceStatsLog[3] & BIT60;
                            deviceStats->sataStatistics.averageLongTermTemperature.monitoredConditionMet = qwordPtrDeviceStatsLog[3] & BIT59;
                            deviceStats->sataStatistics.averageLongTermTemperature.statisticValue = M_Byte0(qwordPtrDeviceStatsLog[3]);
                            ++deviceStats->sataStatistics.statisticsPopulated;
                        }
                        if (qwordPtrDeviceStatsLog[4] & BIT63)
                        {
                            deviceStats->sataStatistics.highestTemperature.isSupported = true;
                            deviceStats->sataStatistics.highestTemperature.isValueValid = qwordPtrDeviceStatsLog[4] & BIT62;
                            deviceStats->sataStatistics.highestTemperature.isNormalized = qwordPtrDeviceStatsLog[4] & BIT61;
                            deviceStats->sataStatistics.highestTemperature.supportsNotification = qwordPtrDeviceStatsLog[4] & BIT60;
                            deviceStats->sataStatistics.highestTemperature.monitoredConditionMet = qwordPtrDeviceStatsLog[4] & BIT59;
                            deviceStats->sataStatistics.highestTemperature.statisticValue = M_Byte0(qwordPtrDeviceStatsLog[4]);
                            ++deviceStats->sataStatistics.statisticsPopulated;
                        }
                        if (qwordPtrDeviceStatsLog[5] & BIT63)
                        {
                            deviceStats->sataStatistics.lowestTemperature.isSupported = true;
                            deviceStats->sataStatistics.lowestTemperature.isValueValid = qwordPtrDeviceStatsLog[5] & BIT62;
                            deviceStats->sataStatistics.lowestTemperature.isNormalized = qwordPtrDeviceStatsLog[5] & BIT61;
                            deviceStats->sataStatistics.lowestTemperature.supportsNotification = qwordPtrDeviceStatsLog[5] & BIT60;
                            deviceStats->sataStatistics.lowestTemperature.monitoredConditionMet = qwordPtrDeviceStatsLog[5] & BIT59;
                            deviceStats->sataStatistics.lowestTemperature.statisticValue = M_Byte0(qwordPtrDeviceStatsLog[5]);
                            ++deviceStats->sataStatistics.statisticsPopulated;
                        }
                        if (qwordPtrDeviceStatsLog[6] & BIT63)
                        {
                            deviceStats->sataStatistics.highestAverageShortTermTemperature.isSupported = true;
                            deviceStats->sataStatistics.highestAverageShortTermTemperature.isValueValid = qwordPtrDeviceStatsLog[6] & BIT62;
                            deviceStats->sataStatistics.highestAverageShortTermTemperature.isNormalized = qwordPtrDeviceStatsLog[6] & BIT61;
                            deviceStats->sataStatistics.highestAverageShortTermTemperature.supportsNotification = qwordPtrDeviceStatsLog[6] & BIT60;
                            deviceStats->sataStatistics.highestAverageShortTermTemperature.monitoredConditionMet = qwordPtrDeviceStatsLog[6] & BIT59;
                            deviceStats->sataStatistics.highestAverageShortTermTemperature.statisticValue = M_Byte0(qwordPtrDeviceStatsLog[6]);
                            ++deviceStats->sataStatistics.statisticsPopulated;
                        }
                        if (qwordPtrDeviceStatsLog[7] & BIT63)
                        {
                            deviceStats->sataStatistics.lowestAverageShortTermTemperature.isSupported = true;
                            deviceStats->sataStatistics.lowestAverageShortTermTemperature.isValueValid = qwordPtrDeviceStatsLog[7] & BIT62;
                            deviceStats->sataStatistics.lowestAverageShortTermTemperature.isNormalized = qwordPtrDeviceStatsLog[7] & BIT61;
                            deviceStats->sataStatistics.lowestAverageShortTermTemperature.supportsNotification = qwordPtrDeviceStatsLog[7] & BIT60;
                            deviceStats->sataStatistics.lowestAverageShortTermTemperature.monitoredConditionMet = qwordPtrDeviceStatsLog[7] & BIT59;
                            deviceStats->sataStatistics.lowestAverageShortTermTemperature.statisticValue = M_Byte0(qwordPtrDeviceStatsLog[7]);
                            ++deviceStats->sataStatistics.statisticsPopulated;
                        }
                        if (qwordPtrDeviceStatsLog[8] & BIT63)
                        {
                            deviceStats->sataStatistics.highestAverageLongTermTemperature.isSupported = true;
                            deviceStats->sataStatistics.highestAverageLongTermTemperature.isValueValid = qwordPtrDeviceStatsLog[8] & BIT62;
                            deviceStats->sataStatistics.highestAverageLongTermTemperature.isNormalized = qwordPtrDeviceStatsLog[8] & BIT61;
                            deviceStats->sataStatistics.highestAverageLongTermTemperature.supportsNotification = qwordPtrDeviceStatsLog[8] & BIT60;
                            deviceStats->sataStatistics.highestAverageLongTermTemperature.monitoredConditionMet = qwordPtrDeviceStatsLog[8] & BIT59;
                            deviceStats->sataStatistics.highestAverageLongTermTemperature.statisticValue = M_Byte0(qwordPtrDeviceStatsLog[8]);
                            ++deviceStats->sataStatistics.statisticsPopulated;
                        }
                        if (qwordPtrDeviceStatsLog[9] & BIT63)
                        {
                            deviceStats->sataStatistics.lowestAverageLongTermTemperature.isSupported = true;
                            deviceStats->sataStatistics.lowestAverageLongTermTemperature.isValueValid = qwordPtrDeviceStatsLog[9] & BIT62;
                            deviceStats->sataStatistics.lowestAverageLongTermTemperature.isNormalized = qwordPtrDeviceStatsLog[9] & BIT61;
                            deviceStats->sataStatistics.lowestAverageLongTermTemperature.supportsNotification = qwordPtrDeviceStatsLog[9] & BIT60;
                            deviceStats->sataStatistics.lowestAverageLongTermTemperature.monitoredConditionMet = qwordPtrDeviceStatsLog[9] & BIT59;
                            deviceStats->sataStatistics.lowestAverageLongTermTemperature.statisticValue = M_Byte0(qwordPtrDeviceStatsLog[9]);
                            ++deviceStats->sataStatistics.statisticsPopulated;
                        }
                        if (qwordPtrDeviceStatsLog[10] & BIT63)
                        {
                            deviceStats->sataStatistics.timeInOverTemperature.isSupported = true;
                            deviceStats->sataStatistics.timeInOverTemperature.isValueValid = qwordPtrDeviceStatsLog[10] & BIT62;
                            deviceStats->sataStatistics.timeInOverTemperature.isNormalized = qwordPtrDeviceStatsLog[10] & BIT61;
                            deviceStats->sataStatistics.timeInOverTemperature.supportsNotification = qwordPtrDeviceStatsLog[10] & BIT60;
                            deviceStats->sataStatistics.timeInOverTemperature.monitoredConditionMet = qwordPtrDeviceStatsLog[10] & BIT59;
                            deviceStats->sataStatistics.timeInOverTemperature.statisticValue = M_DoubleWord0(qwordPtrDeviceStatsLog[10]);
                            ++deviceStats->sataStatistics.statisticsPopulated;
                        }
                        if (qwordPtrDeviceStatsLog[11] & BIT63)
                        {
                            deviceStats->sataStatistics.specifiedMaximumOperatingTemperature.isSupported = true;
                            deviceStats->sataStatistics.specifiedMaximumOperatingTemperature.isValueValid = qwordPtrDeviceStatsLog[11] & BIT62;
                            deviceStats->sataStatistics.specifiedMaximumOperatingTemperature.isNormalized = qwordPtrDeviceStatsLog[11] & BIT61;
                            deviceStats->sataStatistics.specifiedMaximumOperatingTemperature.supportsNotification = qwordPtrDeviceStatsLog[11] & BIT60;
                            deviceStats->sataStatistics.specifiedMaximumOperatingTemperature.monitoredConditionMet = qwordPtrDeviceStatsLog[11] & BIT59;
                            deviceStats->sataStatistics.specifiedMaximumOperatingTemperature.statisticValue = M_Byte0(qwordPtrDeviceStatsLog[11]);
                            ++deviceStats->sataStatistics.statisticsPopulated;
                        }
                        if (qwordPtrDeviceStatsLog[12] & BIT63)
                        {
                            deviceStats->sataStatistics.timeInUnderTemperature.isSupported = true;
                            deviceStats->sataStatistics.timeInUnderTemperature.isValueValid = qwordPtrDeviceStatsLog[12] & BIT62;
                            deviceStats->sataStatistics.timeInUnderTemperature.isNormalized = qwordPtrDeviceStatsLog[12] & BIT61;
                            deviceStats->sataStatistics.timeInUnderTemperature.supportsNotification = qwordPtrDeviceStatsLog[12] & BIT60;
                            deviceStats->sataStatistics.timeInUnderTemperature.monitoredConditionMet = qwordPtrDeviceStatsLog[12] & BIT59;
                            deviceStats->sataStatistics.timeInUnderTemperature.statisticValue = M_DoubleWord0(qwordPtrDeviceStatsLog[12]);
                            ++deviceStats->sataStatistics.statisticsPopulated;
                        }
                        if (qwordPtrDeviceStatsLog[13] & BIT63)
                        {
                            deviceStats->sataStatistics.specifiedMinimumOperatingTemperature.isSupported = true;
                            deviceStats->sataStatistics.specifiedMinimumOperatingTemperature.isValueValid = qwordPtrDeviceStatsLog[13] & BIT62;
                            deviceStats->sataStatistics.specifiedMinimumOperatingTemperature.isNormalized = qwordPtrDeviceStatsLog[13] & BIT61;
                            deviceStats->sataStatistics.specifiedMinimumOperatingTemperature.supportsNotification = qwordPtrDeviceStatsLog[13] & BIT60;
                            deviceStats->sataStatistics.specifiedMinimumOperatingTemperature.monitoredConditionMet = qwordPtrDeviceStatsLog[13] & BIT59;
                            deviceStats->sataStatistics.specifiedMinimumOperatingTemperature.statisticValue = M_Byte0(qwordPtrDeviceStatsLog[13]);
                            ++deviceStats->sataStatistics.statisticsPopulated;
                        }
                    }
                    break;
                case 6://transport statistics
                    if (0x06 == M_Byte2(qwordPtrDeviceStatsLog[0]))
                    {
                        deviceStats->sataStatistics.transportStatisticsSupported = true;
                        if (qwordPtrDeviceStatsLog[1] & BIT63)
                        {
                            deviceStats->sataStatistics.numberOfHardwareResets.isSupported = true;
                            deviceStats->sataStatistics.numberOfHardwareResets.isValueValid = qwordPtrDeviceStatsLog[1] & BIT62;
                            deviceStats->sataStatistics.numberOfHardwareResets.isNormalized = qwordPtrDeviceStatsLog[1] & BIT61;
                            deviceStats->sataStatistics.numberOfHardwareResets.supportsNotification = qwordPtrDeviceStatsLog[1] & BIT60;
                            deviceStats->sataStatistics.numberOfHardwareResets.monitoredConditionMet = qwordPtrDeviceStatsLog[1] & BIT59;
                            deviceStats->sataStatistics.numberOfHardwareResets.statisticValue = M_DoubleWord0(qwordPtrDeviceStatsLog[1]);
                            ++deviceStats->sataStatistics.statisticsPopulated;
                        }
                        if (qwordPtrDeviceStatsLog[2] & BIT63)
                        {
                            deviceStats->sataStatistics.numberOfASREvents.isSupported = true;
                            deviceStats->sataStatistics.numberOfASREvents.isValueValid = qwordPtrDeviceStatsLog[2] & BIT62;
                            deviceStats->sataStatistics.numberOfASREvents.isNormalized = qwordPtrDeviceStatsLog[2] & BIT61;
                            deviceStats->sataStatistics.numberOfASREvents.supportsNotification = qwordPtrDeviceStatsLog[2] & BIT60;
                            deviceStats->sataStatistics.numberOfASREvents.monitoredConditionMet = qwordPtrDeviceStatsLog[2] & BIT59;
                            deviceStats->sataStatistics.numberOfASREvents.statisticValue = M_DoubleWord0(qwordPtrDeviceStatsLog[2]);
                            ++deviceStats->sataStatistics.statisticsPopulated;
                        }
                        if (qwordPtrDeviceStatsLog[3] & BIT63)
                        {
                            deviceStats->sataStatistics.numberOfInterfaceCRCErrors.isSupported = true;
                            deviceStats->sataStatistics.numberOfInterfaceCRCErrors.isValueValid = qwordPtrDeviceStatsLog[3] & BIT62;
                            deviceStats->sataStatistics.numberOfInterfaceCRCErrors.isNormalized = qwordPtrDeviceStatsLog[3] & BIT61;
                            deviceStats->sataStatistics.numberOfInterfaceCRCErrors.supportsNotification = qwordPtrDeviceStatsLog[3] & BIT60;
                            deviceStats->sataStatistics.numberOfInterfaceCRCErrors.monitoredConditionMet = qwordPtrDeviceStatsLog[3] & BIT59;
                            deviceStats->sataStatistics.numberOfInterfaceCRCErrors.statisticValue = M_DoubleWord0(qwordPtrDeviceStatsLog[3]);
                            ++deviceStats->sataStatistics.statisticsPopulated;
                        }
                    }
                    break;
                case 7://solid state device statistics
                    if (0x07 == M_Byte2(qwordPtrDeviceStatsLog[0]))
                    {
                        deviceStats->sataStatistics.ssdStatisticsSupported = true;
                        if (qwordPtrDeviceStatsLog[1] & BIT63)
                        {
                            deviceStats->sataStatistics.percentageUsedIndicator.isSupported = true;
                            deviceStats->sataStatistics.percentageUsedIndicator.isValueValid = qwordPtrDeviceStatsLog[1] & BIT62;
                            deviceStats->sataStatistics.percentageUsedIndicator.isNormalized = qwordPtrDeviceStatsLog[1] & BIT61;
                            deviceStats->sataStatistics.percentageUsedIndicator.supportsNotification = qwordPtrDeviceStatsLog[1] & BIT60;
                            deviceStats->sataStatistics.percentageUsedIndicator.monitoredConditionMet = qwordPtrDeviceStatsLog[1] & BIT59;
                            deviceStats->sataStatistics.percentageUsedIndicator.statisticValue = M_DoubleWord0(qwordPtrDeviceStatsLog[1]);
                            ++deviceStats->sataStatistics.statisticsPopulated;
                        }
                    }
                    break;
                case 8://ZAC statistics
                    if (0x08 == M_Byte2(qwordPtrDeviceStatsLog[0]))
                    {
                        deviceStats->sataStatistics.zonedDeviceStatisticsSupported = true;
                        if (qwordPtrDeviceStatsLog[1] & BIT63)
                        {
                            deviceStats->sataStatistics.maximumOpenZones.isSupported = true;
                            deviceStats->sataStatistics.maximumOpenZones.isValueValid = qwordPtrDeviceStatsLog[1] & BIT62;
                            deviceStats->sataStatistics.maximumOpenZones.isNormalized = qwordPtrDeviceStatsLog[1] & BIT61;
                            deviceStats->sataStatistics.maximumOpenZones.supportsNotification = qwordPtrDeviceStatsLog[1] & BIT60;
                            deviceStats->sataStatistics.maximumOpenZones.monitoredConditionMet = qwordPtrDeviceStatsLog[1] & BIT59;
                            deviceStats->sataStatistics.maximumOpenZones.statisticValue = qwordPtrDeviceStatsLog[1] & MAX_48_BIT_LBA;
                            ++deviceStats->sataStatistics.statisticsPopulated;
                        }
                        if (qwordPtrDeviceStatsLog[2] & BIT63)
                        {
                            deviceStats->sataStatistics.maximumExplicitlyOpenZones.isSupported = true;
                            deviceStats->sataStatistics.maximumExplicitlyOpenZones.isValueValid = qwordPtrDeviceStatsLog[2] & BIT62;
                            deviceStats->sataStatistics.maximumExplicitlyOpenZones.isNormalized = qwordPtrDeviceStatsLog[2] & BIT61;
                            deviceStats->sataStatistics.maximumExplicitlyOpenZones.supportsNotification = qwordPtrDeviceStatsLog[2] & BIT60;
                            deviceStats->sataStatistics.maximumExplicitlyOpenZones.monitoredConditionMet = qwordPtrDeviceStatsLog[2] & BIT59;
                            deviceStats->sataStatistics.maximumExplicitlyOpenZones.statisticValue = qwordPtrDeviceStatsLog[2] & MAX_48_BIT_LBA;
                            ++deviceStats->sataStatistics.statisticsPopulated;
                        }
                        if (qwordPtrDeviceStatsLog[3] & BIT63)
                        {
                            deviceStats->sataStatistics.maximumImplicitlyOpenZones.isSupported = true;
                            deviceStats->sataStatistics.maximumImplicitlyOpenZones.isValueValid = qwordPtrDeviceStatsLog[3] & BIT62;
                            deviceStats->sataStatistics.maximumImplicitlyOpenZones.isNormalized = qwordPtrDeviceStatsLog[3] & BIT61;
                            deviceStats->sataStatistics.maximumImplicitlyOpenZones.supportsNotification = qwordPtrDeviceStatsLog[3] & BIT60;
                            deviceStats->sataStatistics.maximumImplicitlyOpenZones.monitoredConditionMet = qwordPtrDeviceStatsLog[3] & BIT59;
                            deviceStats->sataStatistics.maximumImplicitlyOpenZones.statisticValue = qwordPtrDeviceStatsLog[3] & MAX_48_BIT_LBA;
                            ++deviceStats->sataStatistics.statisticsPopulated;
                        }
                        if (qwordPtrDeviceStatsLog[4] & BIT63)
                        {
                            deviceStats->sataStatistics.minimumEmptyZones.isSupported = true;
                            deviceStats->sataStatistics.minimumEmptyZones.isValueValid = qwordPtrDeviceStatsLog[4] & BIT62;
                            deviceStats->sataStatistics.minimumEmptyZones.isNormalized = qwordPtrDeviceStatsLog[4] & BIT61;
                            deviceStats->sataStatistics.minimumEmptyZones.supportsNotification = qwordPtrDeviceStatsLog[4] & BIT60;
                            deviceStats->sataStatistics.minimumEmptyZones.monitoredConditionMet = qwordPtrDeviceStatsLog[4] & BIT59;
                            deviceStats->sataStatistics.minimumEmptyZones.statisticValue = qwordPtrDeviceStatsLog[4] & MAX_48_BIT_LBA;
                            ++deviceStats->sataStatistics.statisticsPopulated;
                        }
                        if (qwordPtrDeviceStatsLog[5] & BIT63)
                        {
                            deviceStats->sataStatistics.maximumNonSequentialZones.isSupported = true;
                            deviceStats->sataStatistics.maximumNonSequentialZones.isValueValid = qwordPtrDeviceStatsLog[5] & BIT62;
                            deviceStats->sataStatistics.maximumNonSequentialZones.isNormalized = qwordPtrDeviceStatsLog[5] & BIT61;
                            deviceStats->sataStatistics.maximumNonSequentialZones.supportsNotification = qwordPtrDeviceStatsLog[5] & BIT60;
                            deviceStats->sataStatistics.maximumNonSequentialZones.monitoredConditionMet = qwordPtrDeviceStatsLog[5] & BIT59;
                            deviceStats->sataStatistics.maximumNonSequentialZones.statisticValue = qwordPtrDeviceStatsLog[5] & MAX_48_BIT_LBA;
                            ++deviceStats->sataStatistics.statisticsPopulated;
                        }
                        if (qwordPtrDeviceStatsLog[6] & BIT63)
                        {
                            deviceStats->sataStatistics.zonesEmptied.isSupported = true;
                            deviceStats->sataStatistics.zonesEmptied.isValueValid = qwordPtrDeviceStatsLog[6] & BIT62;
                            deviceStats->sataStatistics.zonesEmptied.isNormalized = qwordPtrDeviceStatsLog[6] & BIT61;
                            deviceStats->sataStatistics.zonesEmptied.supportsNotification = qwordPtrDeviceStatsLog[6] & BIT60;
                            deviceStats->sataStatistics.zonesEmptied.monitoredConditionMet = qwordPtrDeviceStatsLog[6] & BIT59;
                            deviceStats->sataStatistics.zonesEmptied.statisticValue = qwordPtrDeviceStatsLog[6] & MAX_48_BIT_LBA;
                            ++deviceStats->sataStatistics.statisticsPopulated;
                        }
                        if (qwordPtrDeviceStatsLog[7] & BIT63)
                        {
                            deviceStats->sataStatistics.suboptimalWriteCommands.isSupported = true;
                            deviceStats->sataStatistics.suboptimalWriteCommands.isValueValid = qwordPtrDeviceStatsLog[7] & BIT62;
                            deviceStats->sataStatistics.suboptimalWriteCommands.isNormalized = qwordPtrDeviceStatsLog[7] & BIT61;
                            deviceStats->sataStatistics.suboptimalWriteCommands.supportsNotification = qwordPtrDeviceStatsLog[7] & BIT60;
                            deviceStats->sataStatistics.suboptimalWriteCommands.monitoredConditionMet = qwordPtrDeviceStatsLog[7] & BIT59;
                            deviceStats->sataStatistics.suboptimalWriteCommands.statisticValue = qwordPtrDeviceStatsLog[7] & MAX_48_BIT_LBA;
                            ++deviceStats->sataStatistics.statisticsPopulated;
                        }
                        if (qwordPtrDeviceStatsLog[8] & BIT63)
                        {
                            deviceStats->sataStatistics.commandsExceedingOptimalLimit.isSupported = true;
                            deviceStats->sataStatistics.commandsExceedingOptimalLimit.isValueValid = qwordPtrDeviceStatsLog[8] & BIT62;
                            deviceStats->sataStatistics.commandsExceedingOptimalLimit.isNormalized = qwordPtrDeviceStatsLog[8] & BIT61;
                            deviceStats->sataStatistics.commandsExceedingOptimalLimit.supportsNotification = qwordPtrDeviceStatsLog[8] & BIT60;
                            deviceStats->sataStatistics.commandsExceedingOptimalLimit.monitoredConditionMet = qwordPtrDeviceStatsLog[8] & BIT59;
                            deviceStats->sataStatistics.commandsExceedingOptimalLimit.statisticValue = qwordPtrDeviceStatsLog[8] & MAX_48_BIT_LBA;
                            ++deviceStats->sataStatistics.statisticsPopulated;
                        }
                        if (qwordPtrDeviceStatsLog[9] & BIT63)
                        {
                            deviceStats->sataStatistics.failedExplicitOpens.isSupported = true;
                            deviceStats->sataStatistics.failedExplicitOpens.isValueValid = qwordPtrDeviceStatsLog[9] & BIT62;
                            deviceStats->sataStatistics.failedExplicitOpens.isNormalized = qwordPtrDeviceStatsLog[9] & BIT61;
                            deviceStats->sataStatistics.failedExplicitOpens.supportsNotification = qwordPtrDeviceStatsLog[9] & BIT60;
                            deviceStats->sataStatistics.failedExplicitOpens.monitoredConditionMet = qwordPtrDeviceStatsLog[9] & BIT59;
                            deviceStats->sataStatistics.failedExplicitOpens.statisticValue = qwordPtrDeviceStatsLog[9] & MAX_48_BIT_LBA;
                            ++deviceStats->sataStatistics.statisticsPopulated;
                        }
                        if (qwordPtrDeviceStatsLog[10] & BIT63)
                        {
                            deviceStats->sataStatistics.readRuleViolations.isSupported = true;
                            deviceStats->sataStatistics.readRuleViolations.isValueValid = qwordPtrDeviceStatsLog[10] & BIT62;
                            deviceStats->sataStatistics.readRuleViolations.isNormalized = qwordPtrDeviceStatsLog[10] & BIT61;
                            deviceStats->sataStatistics.readRuleViolations.supportsNotification = qwordPtrDeviceStatsLog[10] & BIT60;
                            deviceStats->sataStatistics.readRuleViolations.monitoredConditionMet = qwordPtrDeviceStatsLog[10] & BIT59;
                            deviceStats->sataStatistics.readRuleViolations.statisticValue = qwordPtrDeviceStatsLog[10] & MAX_48_BIT_LBA;
                            ++deviceStats->sataStatistics.statisticsPopulated;
                        }
                        if (qwordPtrDeviceStatsLog[11] & BIT63)
                        {
                            deviceStats->sataStatistics.writeRuleViolations.isSupported = true;
                            deviceStats->sataStatistics.writeRuleViolations.isValueValid = qwordPtrDeviceStatsLog[11] & BIT62;
                            deviceStats->sataStatistics.writeRuleViolations.isNormalized = qwordPtrDeviceStatsLog[11] & BIT61;
                            deviceStats->sataStatistics.writeRuleViolations.supportsNotification = qwordPtrDeviceStatsLog[11] & BIT60;
                            deviceStats->sataStatistics.writeRuleViolations.monitoredConditionMet = qwordPtrDeviceStatsLog[11] & BIT59;
                            deviceStats->sataStatistics.writeRuleViolations.statisticValue = qwordPtrDeviceStatsLog[11] & MAX_48_BIT_LBA;
                            ++deviceStats->sataStatistics.statisticsPopulated;
                        }
                    }
                    break;
                case 0xFF://vendor specific
                    if (is_Seagate_Family(device) == SEAGATE && 0xFF == M_Byte2(qwordPtrDeviceStatsLog[0]))
                    {
                        deviceStats->sataStatistics.vendorSpecificStatisticsSupported = true;
                        for (uint8_t vendorSpecificIter = 1; vendorSpecificIter < 64; ++vendorSpecificIter)
                        {
                            if (qwordPtrDeviceStatsLog[vendorSpecificIter] & BIT63)
                            {
                                deviceStats->sataStatistics.vendorSpecificStatistics[vendorSpecificIter - 1].isSupported = true;
                                deviceStats->sataStatistics.vendorSpecificStatistics[vendorSpecificIter - 1].isValueValid = qwordPtrDeviceStatsLog[vendorSpecificIter] & BIT62;
                                deviceStats->sataStatistics.vendorSpecificStatistics[vendorSpecificIter - 1].isNormalized = qwordPtrDeviceStatsLog[vendorSpecificIter] & BIT61;
                                deviceStats->sataStatistics.vendorSpecificStatistics[vendorSpecificIter - 1].supportsNotification = qwordPtrDeviceStatsLog[vendorSpecificIter] & BIT60;
                                deviceStats->sataStatistics.vendorSpecificStatistics[vendorSpecificIter - 1].monitoredConditionMet = qwordPtrDeviceStatsLog[vendorSpecificIter] & BIT59;
                                deviceStats->sataStatistics.vendorSpecificStatistics[vendorSpecificIter - 1].statisticValue = qwordPtrDeviceStatsLog[vendorSpecificIter] & MAX_48_BIT_LBA;
                                ++deviceStats->sataStatistics.statisticsPopulated;
                                ++deviceStats->sataStatistics.vendorSpecificStatisticsPopulated;
                            }
                        }
                    }
                    break;
                default:
                    break;
                }
            }
        }
        safe_Free(deviceStatsLog)
    }
    return ret;
}

int get_SCSI_DeviceStatistics(tDevice *device, ptrDeviceStatistics deviceStats)
{
    int ret = NOT_SUPPORTED;
    if (!deviceStats)
    {
        return BAD_PARAMETER;
    }
    uint8_t supportedLogPages[LEGACY_DRIVE_SEC_SIZE] = { 0 };
    //read list of supported logs, the with that list we'll populate the statistics data
    bool dummyUpLogPages = false;
    bool subpagesSupported = true;
    if (SUCCESS != scsi_Log_Sense_Cmd(device, false, LPC_CUMULATIVE_VALUES, LP_SUPPORTED_LOG_PAGES_AND_SUBPAGES, 0xFF, 0, supportedLogPages, LEGACY_DRIVE_SEC_SIZE))
    {
        //either device doesn't support logs, or it just doesn't support subpages, so let's try reading the list of supported pages (no subpages) before saying we need to dummy up the list
        if (SUCCESS != scsi_Log_Sense_Cmd(device, false, LPC_CUMULATIVE_VALUES, LP_SUPPORTED_LOG_PAGES, 0, 0, supportedLogPages, LEGACY_DRIVE_SEC_SIZE))
        {
            dummyUpLogPages = true;
        }
        else
        {
            subpagesSupported = false;
        }
    }
    if (!dummyUpLogPages)
    {
        //memcmp to make sure we weren't given zeros
        uint8_t zeroMem[LEGACY_DRIVE_SEC_SIZE] = { 0 };
        if (memcmp(zeroMem, supportedLogPages, LEGACY_DRIVE_SEC_SIZE) == 0)
        {
            dummyUpLogPages = true;
        }
    }
    //this is really a work-around for USB drives since some DO support pages, but the don't actually list them (same as the VPD pages above). Most USB drives don't work though - TJE
    if (dummyUpLogPages)
    {
        subpagesSupported = true;
        memset(supportedLogPages, 0, LEGACY_DRIVE_SEC_SIZE);
        supportedLogPages[0] = 0;
        supportedLogPages[1] = 0;
        //page length
        supportedLogPages[2] = 0;
        supportedLogPages[3] = 0x29;// <---increment me when adding a new dummy page below
        //descriptors (2 bytes per page for pages + subpage format)) if you add a new page here, make the page length above bigger
        supportedLogPages[4] = LP_SUPPORTED_LOG_PAGES;//just to be correct/accurate
        supportedLogPages[5] = 0;//subpage
        supportedLogPages[6] = LP_SUPPORTED_LOG_PAGES_AND_SUBPAGES;
        supportedLogPages[7] = 0xFF;//supported subpages
        supportedLogPages[8] = LP_WRITE_ERROR_COUNTERS;
        supportedLogPages[9] = 0;//subpage
        supportedLogPages[10] = LP_READ_ERROR_COUNTERS;
        supportedLogPages[11] = 0;//subpage
        supportedLogPages[12] = LP_READ_REVERSE_ERROR_COUNTERS;
        supportedLogPages[13] = 0;//subpage
        supportedLogPages[14] = LP_VERIFY_ERROR_COUNTERS;
        supportedLogPages[15] = 0;//subpage
        supportedLogPages[16] = LP_NON_MEDIUM_ERROR;
        supportedLogPages[17] = 0;//subpage
        supportedLogPages[18] = LP_FORMAT_STATUS_LOG_PAGE;
        supportedLogPages[19] = 0;//subpage
        supportedLogPages[20] = LP_LOGICAL_BLOCK_PROVISIONING;
        supportedLogPages[21] = 0;//subpage
        supportedLogPages[22] = LP_TEMPERATURE;
        supportedLogPages[23] = 0;//subpage
        supportedLogPages[24] = LP_ENVIRONMENTAL_REPORTING;
        supportedLogPages[25] = 0x01;//subpage (page number is same as temperature)
        supportedLogPages[26] = LP_ENVIRONMENTAL_LIMITS;
        supportedLogPages[27] = 0x02;//subpage (page number is same as temperature)
        supportedLogPages[28] = LP_START_STOP_CYCLE_COUNTER;
        supportedLogPages[29] = 0;
        supportedLogPages[30] = LP_UTILIZATION;
        supportedLogPages[31] = 0x01;//subpage (page number is same as start stop cycle counter)
        supportedLogPages[32] = LP_SOLID_STATE_MEDIA;
        supportedLogPages[33] = 0;//subpage
        supportedLogPages[34] = LP_BACKGROUND_SCAN_RESULTS;
        supportedLogPages[35] = 0;//subpage
        supportedLogPages[36] = LP_PENDING_DEFECTS;
        supportedLogPages[37] = 0x01;//subpage (page number is same as background scan results)
        supportedLogPages[38] = LP_LPS_MISALLIGNMENT;
        supportedLogPages[39] = 0x03;//subpage (page number is same as background scan results)
        supportedLogPages[40] = LP_NON_VOLITILE_CACHE;
        supportedLogPages[41] = 0;//subpage
        supportedLogPages[42] = LP_GENERAL_STATISTICS_AND_PERFORMANCE;
        supportedLogPages[43] = 0;//subpage
        supportedLogPages[44] = LP_CACHE_MEMORY_STATISTICS;
        supportedLogPages[45] = 0x20;//subpage (page number is same as general statistics and performance)
    }
    uint16_t logPageIter = LOG_PAGE_HEADER_LENGTH;//log page descriptors start on offset 4 and are 2 bytes long each
    uint16_t supportedPagesLength = M_BytesTo2ByteValue(supportedLogPages[2], supportedLogPages[3]);
    uint8_t incrementAmount = subpagesSupported ? 2 : 1;
    uint8_t tempLogBuf[LEGACY_DRIVE_SEC_SIZE] = { 0 };
    for (; logPageIter < M_Min(supportedPagesLength + LOG_PAGE_HEADER_LENGTH, LEGACY_DRIVE_SEC_SIZE); logPageIter += incrementAmount)
    {
        uint8_t pageCode = supportedLogPages[logPageIter] & 0x3F;//outer switch statement
        uint8_t subpageCode = 0;
        if (subpagesSupported)
        {
            subpageCode = supportedLogPages[logPageIter + 1];//inner switch statement
        }
        switch (pageCode)
        {
        case LP_WRITE_ERROR_COUNTERS:
            if (subpageCode == 0)
            {
                memset(tempLogBuf, 0, LEGACY_DRIVE_SEC_SIZE);
                if (SUCCESS == scsi_Log_Sense_Cmd(device, false, LPC_CUMULATIVE_VALUES, pageCode, subpageCode, 0x0000, tempLogBuf, LEGACY_DRIVE_SEC_SIZE))
                {
                    deviceStats->sasStatistics.writeErrorCountersSupported = true;
                    uint16_t pageLength = M_BytesTo2ByteValue(tempLogBuf[2], tempLogBuf[3]);
                    uint8_t parameterLength = 0;
                    //loop through the data and gather the data from each parameter we care about getting.
                    for (uint16_t iter = 4; iter < pageLength && iter < LEGACY_DRIVE_SEC_SIZE; iter += (parameterLength + 4))
                    {
                        uint16_t parameterCode = M_BytesTo2ByteValue(tempLogBuf[iter], tempLogBuf[iter + 1]);
                        parameterLength = tempLogBuf[iter + 3];
                        switch (parameterCode)
                        {
                        case 0://Errors corrected without substantial delay
                            deviceStats->sasStatistics.writeErrorsCorrectedWithoutSubstantialDelay.isSupported = true;
                            deviceStats->sasStatistics.writeErrorsCorrectedWithoutSubstantialDelay.isValueValid = true;
                            //check if thresholds supported, etc
                            deviceStats->sasStatistics.writeErrorsCorrectedWithoutSubstantialDelay.thresholdNotificationEnabled = tempLogBuf[iter + 2] & BIT4;//ETC bit
                            if (tempLogBuf[iter + 2] & BIT4)
                            {
                                switch ((tempLogBuf[iter + 2] & (BIT2 | BIT3)) >> 2)
                                {
                                case 3:
                                    deviceStats->sasStatistics.writeErrorsCorrectedWithoutSubstantialDelay.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_GREATER;
                                    break;
                                case 2:
                                    deviceStats->sasStatistics.writeErrorsCorrectedWithoutSubstantialDelay.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL;
                                    break;
                                case 1:
                                    deviceStats->sasStatistics.writeErrorsCorrectedWithoutSubstantialDelay.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL;
                                    break;
                                case 0:
                                default:
                                    deviceStats->sasStatistics.writeErrorsCorrectedWithoutSubstantialDelay.threshType = THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE;
                                    break;
                                }
                            }
                            switch (parameterLength)
                            {
                            case 1://single byte
                                deviceStats->sasStatistics.writeErrorsCorrectedWithoutSubstantialDelay.statisticValue = tempLogBuf[iter + 4];
                                break;
                            case 2://word
                                deviceStats->sasStatistics.writeErrorsCorrectedWithoutSubstantialDelay.statisticValue = M_BytesTo2ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5]);
                                break;
                            case 4://double word
                                deviceStats->sasStatistics.writeErrorsCorrectedWithoutSubstantialDelay.statisticValue = M_BytesTo4ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
                                break;
                            case 8://quad word
                                deviceStats->sasStatistics.writeErrorsCorrectedWithoutSubstantialDelay.statisticValue = M_BytesTo8ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7], tempLogBuf[iter + 8], tempLogBuf[iter + 9], tempLogBuf[iter + 10], tempLogBuf[iter + 11]);
                                break;
                            default://don't bother trying to read the data since it's in a more complicated format to read than we care to handle in this code right now
                                deviceStats->sasStatistics.writeErrorsCorrectedWithoutSubstantialDelay.isValueValid = false;
                                break;
                            }
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            break;
                        case 1://Errors corrected with possible delays
                            deviceStats->sasStatistics.writeErrorsCorrectedWithPossibleDelays.isSupported = true;
                            deviceStats->sasStatistics.writeErrorsCorrectedWithPossibleDelays.isValueValid = true;
                            //check if thresholds supported, etc
                            deviceStats->sasStatistics.writeErrorsCorrectedWithPossibleDelays.thresholdNotificationEnabled = tempLogBuf[iter + 2] & BIT4;//ETC bit
                            if (tempLogBuf[iter + 2] & BIT4)
                            {
                                switch ((tempLogBuf[iter + 2] & (BIT2 | BIT3)) >> 2)
                                {
                                case 3:
                                    deviceStats->sasStatistics.writeErrorsCorrectedWithPossibleDelays.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_GREATER;
                                    break;
                                case 2:
                                    deviceStats->sasStatistics.writeErrorsCorrectedWithPossibleDelays.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL;
                                    break;
                                case 1:
                                    deviceStats->sasStatistics.writeErrorsCorrectedWithPossibleDelays.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL;
                                    break;
                                case 0:
                                default:
                                    deviceStats->sasStatistics.writeErrorsCorrectedWithPossibleDelays.threshType = THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE;
                                    break;
                                }
                            }
                            switch (parameterLength)
                            {
                            case 1://single byte
                                deviceStats->sasStatistics.writeErrorsCorrectedWithPossibleDelays.statisticValue = tempLogBuf[iter + 4];
                                break;
                            case 2://word
                                deviceStats->sasStatistics.writeErrorsCorrectedWithPossibleDelays.statisticValue = M_BytesTo2ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5]);
                                break;
                            case 4://double word
                                deviceStats->sasStatistics.writeErrorsCorrectedWithPossibleDelays.statisticValue = M_BytesTo4ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
                                break;
                            case 8://quad word
                                deviceStats->sasStatistics.writeErrorsCorrectedWithPossibleDelays.statisticValue = M_BytesTo8ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7], tempLogBuf[iter + 8], tempLogBuf[iter + 9], tempLogBuf[iter + 10], tempLogBuf[iter + 11]);
                                break;
                            default://don't bother trying to read the data since it's in a more complicated format to read than we care to handle in this code right now
                                deviceStats->sasStatistics.writeErrorsCorrectedWithPossibleDelays.isValueValid = false;
                                break;
                            }
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            break;
                        case 2://total
                            deviceStats->sasStatistics.writeTotal.isSupported = true;
                            deviceStats->sasStatistics.writeTotal.isValueValid = true;
                            //check if thresholds supported, etc
                            deviceStats->sasStatistics.writeTotal.thresholdNotificationEnabled = tempLogBuf[iter + 2] & BIT4;//ETC bit
                            if (tempLogBuf[iter + 2] & BIT4)
                            {
                                switch ((tempLogBuf[iter + 2] & (BIT2 | BIT3)) >> 2)
                                {
                                case 3:
                                    deviceStats->sasStatistics.writeTotal.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_GREATER;
                                    break;
                                case 2:
                                    deviceStats->sasStatistics.writeTotal.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL;
                                    break;
                                case 1:
                                    deviceStats->sasStatistics.writeTotal.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL;
                                    break;
                                case 0:
                                default:
                                    deviceStats->sasStatistics.writeTotal.threshType = THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE;
                                    break;
                                }
                            }
                            switch (parameterLength)
                            {
                            case 1://single byte
                                deviceStats->sasStatistics.writeTotal.statisticValue = tempLogBuf[iter + 4];
                                break;
                            case 2://word
                                deviceStats->sasStatistics.writeTotal.statisticValue = M_BytesTo2ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5]);
                                break;
                            case 4://double word
                                deviceStats->sasStatistics.writeTotal.statisticValue = M_BytesTo4ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
                                break;
                            case 8://quad word
                                deviceStats->sasStatistics.writeTotal.statisticValue = M_BytesTo8ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7], tempLogBuf[iter + 8], tempLogBuf[iter + 9], tempLogBuf[iter + 10], tempLogBuf[iter + 11]);
                                break;
                            default://don't bother trying to read the data since it's in a more complicated format to read than we care to handle in this code right now
                                deviceStats->sasStatistics.writeTotal.isValueValid = false;
                                break;
                            }
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            break;
                        case 3://total errors corrected
                            deviceStats->sasStatistics.writeErrorsCorrected.isSupported = true;
                            deviceStats->sasStatistics.writeErrorsCorrected.isValueValid = true;
                            //check if thresholds supported, etc
                            deviceStats->sasStatistics.writeErrorsCorrected.thresholdNotificationEnabled = tempLogBuf[iter + 2] & BIT4;//ETC bit
                            if (tempLogBuf[iter + 2] & BIT4)
                            {
                                switch ((tempLogBuf[iter + 2] & (BIT2 | BIT3)) >> 2)
                                {
                                case 3:
                                    deviceStats->sasStatistics.writeErrorsCorrected.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_GREATER;
                                    break;
                                case 2:
                                    deviceStats->sasStatistics.writeErrorsCorrected.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL;
                                    break;
                                case 1:
                                    deviceStats->sasStatistics.writeErrorsCorrected.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL;
                                    break;
                                case 0:
                                default:
                                    deviceStats->sasStatistics.writeErrorsCorrected.threshType = THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE;
                                    break;
                                }
                            }
                            switch (parameterLength)
                            {
                            case 1://single byte
                                deviceStats->sasStatistics.writeErrorsCorrected.statisticValue = tempLogBuf[iter + 4];
                                break;
                            case 2://word
                                deviceStats->sasStatistics.writeErrorsCorrected.statisticValue = M_BytesTo2ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5]);
                                break;
                            case 4://double word
                                deviceStats->sasStatistics.writeErrorsCorrected.statisticValue = M_BytesTo4ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
                                break;
                            case 8://quad word
                                deviceStats->sasStatistics.writeErrorsCorrected.statisticValue = M_BytesTo8ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7], tempLogBuf[iter + 8], tempLogBuf[iter + 9], tempLogBuf[iter + 10], tempLogBuf[iter + 11]);
                                break;
                            default://don't bother trying to read the data since it's in a more complicated format to read than we care to handle in this code right now
                                deviceStats->sasStatistics.writeErrorsCorrected.isValueValid = false;
                                break;
                            }
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            break;
                        case 4://total times correction algorithm processed
                            deviceStats->sasStatistics.writeTotalTimeCorrectionAlgorithmProcessed.isSupported = true;
                            deviceStats->sasStatistics.writeTotalTimeCorrectionAlgorithmProcessed.isValueValid = true;
                            //check if thresholds supported, etc
                            deviceStats->sasStatistics.writeTotalTimeCorrectionAlgorithmProcessed.thresholdNotificationEnabled = tempLogBuf[iter + 2] & BIT4;//ETC bit
                            if (tempLogBuf[iter + 2] & BIT4)
                            {
                                switch ((tempLogBuf[iter + 2] & (BIT2 | BIT3)) >> 2)
                                {
                                case 3:
                                    deviceStats->sasStatistics.writeTotalTimeCorrectionAlgorithmProcessed.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_GREATER;
                                    break;
                                case 2:
                                    deviceStats->sasStatistics.writeTotalTimeCorrectionAlgorithmProcessed.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL;
                                    break;
                                case 1:
                                    deviceStats->sasStatistics.writeTotalTimeCorrectionAlgorithmProcessed.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL;
                                    break;
                                case 0:
                                default:
                                    deviceStats->sasStatistics.writeTotalTimeCorrectionAlgorithmProcessed.threshType = THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE;
                                    break;
                                }
                            }
                            switch (parameterLength)
                            {
                            case 1://single byte
                                deviceStats->sasStatistics.writeTotalTimeCorrectionAlgorithmProcessed.statisticValue = tempLogBuf[iter + 4];
                                break;
                            case 2://word
                                deviceStats->sasStatistics.writeTotalTimeCorrectionAlgorithmProcessed.statisticValue = M_BytesTo2ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5]);
                                break;
                            case 4://double word
                                deviceStats->sasStatistics.writeTotalTimeCorrectionAlgorithmProcessed.statisticValue = M_BytesTo4ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
                                break;
                            case 8://quad word
                                deviceStats->sasStatistics.writeTotalTimeCorrectionAlgorithmProcessed.statisticValue = M_BytesTo8ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7], tempLogBuf[iter + 8], tempLogBuf[iter + 9], tempLogBuf[iter + 10], tempLogBuf[iter + 11]);
                                break;
                            default://don't bother trying to read the data since it's in a more complicated format to read than we care to handle in this code right now
                                deviceStats->sasStatistics.writeTotalTimeCorrectionAlgorithmProcessed.isValueValid = false;
                                break;
                            }
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            break;
                        case 5://total bytes processed
                            deviceStats->sasStatistics.writeTotalBytesProcessed.isSupported = true;
                            deviceStats->sasStatistics.writeTotalBytesProcessed.isValueValid = true;
                            //check if thresholds supported, etc
                            deviceStats->sasStatistics.writeTotalBytesProcessed.thresholdNotificationEnabled = tempLogBuf[iter + 2] & BIT4;//ETC bit
                            if (tempLogBuf[iter + 2] & BIT4)
                            {
                                switch ((tempLogBuf[iter + 2] & (BIT2 | BIT3)) >> 2)
                                {
                                case 3:
                                    deviceStats->sasStatistics.writeTotalBytesProcessed.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_GREATER;
                                    break;
                                case 2:
                                    deviceStats->sasStatistics.writeTotalBytesProcessed.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL;
                                    break;
                                case 1:
                                    deviceStats->sasStatistics.writeTotalBytesProcessed.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL;
                                    break;
                                case 0:
                                default:
                                    deviceStats->sasStatistics.writeTotalBytesProcessed.threshType = THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE;
                                    break;
                                }
                            }
                            switch (parameterLength)
                            {
                            case 1://single byte
                                deviceStats->sasStatistics.writeTotalBytesProcessed.statisticValue = tempLogBuf[iter + 4];
                                break;
                            case 2://word
                                deviceStats->sasStatistics.writeTotalBytesProcessed.statisticValue = M_BytesTo2ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5]);
                                break;
                            case 4://double word
                                deviceStats->sasStatistics.writeTotalBytesProcessed.statisticValue = M_BytesTo4ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
                                break;
                            case 8://quad word
                                deviceStats->sasStatistics.writeTotalBytesProcessed.statisticValue = M_BytesTo8ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7], tempLogBuf[iter + 8], tempLogBuf[iter + 9], tempLogBuf[iter + 10], tempLogBuf[iter + 11]);
                                break;
                            default://don't bother trying to read the data since it's in a more complicated format to read than we care to handle in this code right now
                                deviceStats->sasStatistics.writeTotalBytesProcessed.isValueValid = false;
                                break;
                            }
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            break;
                        case 6://total uncorrected errors
                            deviceStats->sasStatistics.writeTotalUncorrectedErrors.isSupported = true;
                            deviceStats->sasStatistics.writeTotalUncorrectedErrors.isValueValid = true;
                            //check if thresholds supported, etc
                            deviceStats->sasStatistics.writeTotalUncorrectedErrors.thresholdNotificationEnabled = tempLogBuf[iter + 2] & BIT4;//ETC bit
                            if (tempLogBuf[iter + 2] & BIT4)
                            {
                                switch ((tempLogBuf[iter + 2] & (BIT2 | BIT3)) >> 2)
                                {
                                case 3:
                                    deviceStats->sasStatistics.writeTotalUncorrectedErrors.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_GREATER;
                                    break;
                                case 2:
                                    deviceStats->sasStatistics.writeTotalUncorrectedErrors.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL;
                                    break;
                                case 1:
                                    deviceStats->sasStatistics.writeTotalUncorrectedErrors.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL;
                                    break;
                                case 0:
                                default:
                                    deviceStats->sasStatistics.writeTotalUncorrectedErrors.threshType = THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE;
                                    break;
                                }
                            }
                            switch (parameterLength)
                            {
                            case 1://single byte
                                deviceStats->sasStatistics.writeTotalUncorrectedErrors.statisticValue = tempLogBuf[iter + 4];
                                break;
                            case 2://word
                                deviceStats->sasStatistics.writeTotalUncorrectedErrors.statisticValue = M_BytesTo2ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5]);
                                break;
                            case 4://double word
                                deviceStats->sasStatistics.writeTotalUncorrectedErrors.statisticValue = M_BytesTo4ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
                                break;
                            case 8://quad word
                                deviceStats->sasStatistics.writeTotalUncorrectedErrors.statisticValue = M_BytesTo8ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7], tempLogBuf[iter + 8], tempLogBuf[iter + 9], tempLogBuf[iter + 10], tempLogBuf[iter + 11]);
                                break;
                            default://don't bother trying to read the data since it's in a more complicated format to read than we care to handle in this code right now
                                deviceStats->sasStatistics.writeTotalUncorrectedErrors.isValueValid = false;
                                break;
                            }
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            break;
                        default:
                            break;
                        }
                        if (parameterLength == 0)
                        {
                            break;
                        }
                    }
                    //thresholds
                    memset(tempLogBuf, 0, LEGACY_DRIVE_SEC_SIZE);
                    if (SUCCESS == scsi_Log_Sense_Cmd(device, false, LPC_THRESHOLD_VALUES, pageCode, subpageCode, 0x0000, tempLogBuf, LEGACY_DRIVE_SEC_SIZE))
                    {
                        uint16_t pageLength = M_BytesTo2ByteValue(tempLogBuf[2], tempLogBuf[3]);
                        uint8_t parameterLength = 0;
                        //loop through the data and gather the data from each parameter we care about getting.
                        for (uint16_t iter = 4; iter < pageLength && iter < LEGACY_DRIVE_SEC_SIZE; iter += (parameterLength + 4))
                        {
                            uint16_t parameterCode = M_BytesTo2ByteValue(tempLogBuf[iter], tempLogBuf[iter + 1]);
                            parameterLength = tempLogBuf[iter + 3];
                            switch (parameterCode)
                            {
                            case 0://Errors corrected without substantial delay
                                deviceStats->sasStatistics.writeErrorsCorrectedWithoutSubstantialDelay.supportsNotification = true;
                                if (tempLogBuf[iter + 2] & BIT4)
                                {
                                    deviceStats->sasStatistics.writeErrorsCorrectedWithoutSubstantialDelay.isThresholdValid = true;
                                    switch (parameterLength)
                                    {
                                    case 1://single byte
                                        deviceStats->sasStatistics.writeErrorsCorrectedWithoutSubstantialDelay.threshold = tempLogBuf[iter + 4];
                                        break;
                                    case 2://word
                                        deviceStats->sasStatistics.writeErrorsCorrectedWithoutSubstantialDelay.threshold = M_BytesTo2ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5]);
                                        break;
                                    case 4://double word
                                        deviceStats->sasStatistics.writeErrorsCorrectedWithoutSubstantialDelay.threshold = M_BytesTo4ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
                                        break;
                                    case 8://quad word
                                        deviceStats->sasStatistics.writeErrorsCorrectedWithoutSubstantialDelay.threshold = M_BytesTo8ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7], tempLogBuf[iter + 8], tempLogBuf[iter + 9], tempLogBuf[iter + 10], tempLogBuf[iter + 11]);
                                        break;
                                    default://don't bother trying to read the data since it's in a more complicated format to read than we care to handle in this code right now
                                        deviceStats->sasStatistics.writeErrorsCorrectedWithoutSubstantialDelay.isThresholdValid = false;
                                        break;
                                    }
                                    scsi_Threshold_Comparison(&deviceStats->sasStatistics.writeErrorsCorrectedWithoutSubstantialDelay);
                                }
                                break;
                            case 1://Errors corrected with possible delays
                                deviceStats->sasStatistics.writeErrorsCorrectedWithPossibleDelays.supportsNotification = true;
                                if (tempLogBuf[iter + 2] & BIT4)
                                {
                                    deviceStats->sasStatistics.writeErrorsCorrectedWithPossibleDelays.isThresholdValid = true;
                                    switch (parameterLength)
                                    {
                                    case 1://single byte
                                        deviceStats->sasStatistics.writeErrorsCorrectedWithPossibleDelays.threshold = tempLogBuf[iter + 4];
                                        break;
                                    case 2://word
                                        deviceStats->sasStatistics.writeErrorsCorrectedWithPossibleDelays.threshold = M_BytesTo2ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5]);
                                        break;
                                    case 4://double word
                                        deviceStats->sasStatistics.writeErrorsCorrectedWithPossibleDelays.threshold = M_BytesTo4ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
                                        break;
                                    case 8://quad word
                                        deviceStats->sasStatistics.writeErrorsCorrectedWithPossibleDelays.threshold = M_BytesTo8ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7], tempLogBuf[iter + 8], tempLogBuf[iter + 9], tempLogBuf[iter + 10], tempLogBuf[iter + 11]);
                                        break;
                                    default://don't bother trying to read the data since it's in a more complicated format to read than we care to handle in this code right now
                                        deviceStats->sasStatistics.writeErrorsCorrectedWithPossibleDelays.isThresholdValid = false;
                                        break;
                                    }
                                    scsi_Threshold_Comparison(&deviceStats->sasStatistics.writeErrorsCorrectedWithPossibleDelays);
                                }
                                break;
                            case 2://total
                                deviceStats->sasStatistics.writeTotal.supportsNotification = true;
                                if (tempLogBuf[iter + 2] & BIT4)
                                {
                                    deviceStats->sasStatistics.writeTotal.isThresholdValid = true;
                                    switch (parameterLength)
                                    {
                                    case 1://single byte
                                        deviceStats->sasStatistics.writeTotal.threshold = tempLogBuf[iter + 4];
                                        break;
                                    case 2://word
                                        deviceStats->sasStatistics.writeTotal.threshold = M_BytesTo2ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5]);
                                        break;
                                    case 4://double word
                                        deviceStats->sasStatistics.writeTotal.threshold = M_BytesTo4ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
                                        break;
                                    case 8://quad word
                                        deviceStats->sasStatistics.writeTotal.threshold = M_BytesTo8ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7], tempLogBuf[iter + 8], tempLogBuf[iter + 9], tempLogBuf[iter + 10], tempLogBuf[iter + 11]);
                                        break;
                                    default://don't bother trying to read the data since it's in a more complicated format to read than we care to handle in this code right now
                                        deviceStats->sasStatistics.writeTotal.isThresholdValid = false;
                                        break;
                                    }
                                    scsi_Threshold_Comparison(&deviceStats->sasStatistics.writeTotal);
                                }
                                break;
                            case 3://total errors corrected
                                deviceStats->sasStatistics.writeErrorsCorrected.supportsNotification = true;
                                if (tempLogBuf[iter + 2] & BIT4)
                                {
                                    deviceStats->sasStatistics.writeErrorsCorrected.isThresholdValid = true;
                                    switch (parameterLength)
                                    {
                                    case 1://single byte
                                        deviceStats->sasStatistics.writeErrorsCorrected.threshold = tempLogBuf[iter + 4];
                                        break;
                                    case 2://word
                                        deviceStats->sasStatistics.writeErrorsCorrected.threshold = M_BytesTo2ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5]);
                                        break;
                                    case 4://double word
                                        deviceStats->sasStatistics.writeErrorsCorrected.threshold = M_BytesTo4ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
                                        break;
                                    case 8://quad word
                                        deviceStats->sasStatistics.writeErrorsCorrected.threshold = M_BytesTo8ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7], tempLogBuf[iter + 8], tempLogBuf[iter + 9], tempLogBuf[iter + 10], tempLogBuf[iter + 11]);
                                        break;
                                    default://don't bother trying to read the data since it's in a more complicated format to read than we care to handle in this code right now
                                        deviceStats->sasStatistics.writeErrorsCorrected.isThresholdValid = false;
                                        break;
                                    }
                                    scsi_Threshold_Comparison(&deviceStats->sasStatistics.writeErrorsCorrected);
                                }
                                break;
                            case 4://total times correction algorithm processed
                                deviceStats->sasStatistics.writeTotalTimeCorrectionAlgorithmProcessed.supportsNotification = true;
                                if (tempLogBuf[iter + 2] & BIT4)
                                {
                                    deviceStats->sasStatistics.writeTotalTimeCorrectionAlgorithmProcessed.isThresholdValid = true;
                                    switch (parameterLength)
                                    {
                                    case 1://single byte
                                        deviceStats->sasStatistics.writeTotalTimeCorrectionAlgorithmProcessed.threshold = tempLogBuf[iter + 4];
                                        break;
                                    case 2://word
                                        deviceStats->sasStatistics.writeTotalTimeCorrectionAlgorithmProcessed.threshold = M_BytesTo2ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5]);
                                        break;
                                    case 4://double word
                                        deviceStats->sasStatistics.writeTotalTimeCorrectionAlgorithmProcessed.threshold = M_BytesTo4ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
                                        break;
                                    case 8://quad word
                                        deviceStats->sasStatistics.writeTotalTimeCorrectionAlgorithmProcessed.threshold = M_BytesTo8ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7], tempLogBuf[iter + 8], tempLogBuf[iter + 9], tempLogBuf[iter + 10], tempLogBuf[iter + 11]);
                                        break;
                                    default://don't bother trying to read the data since it's in a more complicated format to read than we care to handle in this code right now
                                        deviceStats->sasStatistics.writeTotalTimeCorrectionAlgorithmProcessed.isThresholdValid = false;
                                        break;
                                    }
                                    scsi_Threshold_Comparison(&deviceStats->sasStatistics.writeTotalTimeCorrectionAlgorithmProcessed);
                                }
                                break;
                            case 5://total bytes processed
                                deviceStats->sasStatistics.writeTotalBytesProcessed.supportsNotification = true;
                                if (tempLogBuf[iter + 2] & BIT4)
                                {
                                    deviceStats->sasStatistics.writeTotalBytesProcessed.isThresholdValid = true;
                                    switch (parameterLength)
                                    {
                                    case 1://single byte
                                        deviceStats->sasStatistics.writeTotalBytesProcessed.threshold = tempLogBuf[iter + 4];
                                        break;
                                    case 2://word
                                        deviceStats->sasStatistics.writeTotalBytesProcessed.threshold = M_BytesTo2ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5]);
                                        break;
                                    case 4://double word
                                        deviceStats->sasStatistics.writeTotalBytesProcessed.threshold = M_BytesTo4ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
                                        break;
                                    case 8://quad word
                                        deviceStats->sasStatistics.writeTotalBytesProcessed.threshold = M_BytesTo8ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7], tempLogBuf[iter + 8], tempLogBuf[iter + 9], tempLogBuf[iter + 10], tempLogBuf[iter + 11]);
                                        break;
                                    default://don't bother trying to read the data since it's in a more complicated format to read than we care to handle in this code right now
                                        deviceStats->sasStatistics.writeTotalBytesProcessed.isThresholdValid = false;
                                        break;
                                    }
                                    scsi_Threshold_Comparison(&deviceStats->sasStatistics.writeTotalBytesProcessed);
                                }
                                break;
                            case 6://total uncorrected errors
                                deviceStats->sasStatistics.writeTotalUncorrectedErrors.supportsNotification = true;
                                if (tempLogBuf[iter + 2] & BIT4)
                                {
                                    deviceStats->sasStatistics.writeTotalUncorrectedErrors.isThresholdValid = true;
                                    switch (parameterLength)
                                    {
                                    case 1://single byte
                                        deviceStats->sasStatistics.writeTotalUncorrectedErrors.threshold = tempLogBuf[iter + 4];
                                        break;
                                    case 2://word
                                        deviceStats->sasStatistics.writeTotalUncorrectedErrors.threshold = M_BytesTo2ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5]);
                                        break;
                                    case 4://double word
                                        deviceStats->sasStatistics.writeTotalUncorrectedErrors.threshold = M_BytesTo4ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
                                        break;
                                    case 8://quad word
                                        deviceStats->sasStatistics.writeTotalUncorrectedErrors.threshold = M_BytesTo8ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7], tempLogBuf[iter + 8], tempLogBuf[iter + 9], tempLogBuf[iter + 10], tempLogBuf[iter + 11]);
                                        break;
                                    default://don't bother trying to read the data since it's in a more complicated format to read than we care to handle in this code right now
                                        deviceStats->sasStatistics.writeTotalUncorrectedErrors.isThresholdValid = false;
                                        break;
                                    }
                                    scsi_Threshold_Comparison(&deviceStats->sasStatistics.writeTotalUncorrectedErrors);
                                }
                                break;
                            default:
                                break;
                            }
                            if (parameterLength == 0)
                            {
                                break;
                            }
                        }
                    }
                }
            }
            break;
        case LP_READ_ERROR_COUNTERS:
            ret = SUCCESS;
            if (subpageCode == 0)
            {
                memset(tempLogBuf, 0, LEGACY_DRIVE_SEC_SIZE);
                if (SUCCESS == scsi_Log_Sense_Cmd(device, false, LPC_CUMULATIVE_VALUES, pageCode, subpageCode, 0x0000, tempLogBuf, LEGACY_DRIVE_SEC_SIZE))
                {
                    deviceStats->sasStatistics.readErrorCountersSupported = true;
                    uint16_t pageLength = M_BytesTo2ByteValue(tempLogBuf[2], tempLogBuf[3]);
                    uint8_t parameterLength = 0;
                    //loop through the data and gather the data from each parameter we care about getting.
                    for (uint16_t iter = 4; iter < pageLength && iter < LEGACY_DRIVE_SEC_SIZE; iter += (parameterLength + 4))
                    {
                        uint16_t parameterCode = M_BytesTo2ByteValue(tempLogBuf[iter], tempLogBuf[iter + 1]);
                        parameterLength = tempLogBuf[iter + 3];
                        switch (parameterCode)
                        {
                        case 0://Errors corrected without substantial delay
                            deviceStats->sasStatistics.readErrorsCorrectedWithoutSubstantialDelay.isSupported = true;
                            deviceStats->sasStatistics.readErrorsCorrectedWithoutSubstantialDelay.isValueValid = true;
                            //check if thresholds supported, etc
                            deviceStats->sasStatistics.readErrorsCorrectedWithoutSubstantialDelay.thresholdNotificationEnabled = tempLogBuf[iter + 2] & BIT4;//ETC bit
                            if (tempLogBuf[iter + 2] & BIT4)
                            {
                                switch ((tempLogBuf[iter + 2] & (BIT2 | BIT3)) >> 2)
                                {
                                case 3:
                                    deviceStats->sasStatistics.readErrorsCorrectedWithoutSubstantialDelay.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_GREATER;
                                    break;
                                case 2:
                                    deviceStats->sasStatistics.readErrorsCorrectedWithoutSubstantialDelay.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL;
                                    break;
                                case 1:
                                    deviceStats->sasStatistics.readErrorsCorrectedWithoutSubstantialDelay.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL;
                                    break;
                                case 0:
                                default:
                                    deviceStats->sasStatistics.readErrorsCorrectedWithoutSubstantialDelay.threshType = THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE;
                                    break;
                                }
                            }
                            switch (parameterLength)
                            {
                            case 1://single byte
                                deviceStats->sasStatistics.readErrorsCorrectedWithoutSubstantialDelay.statisticValue = tempLogBuf[iter + 4];
                                break;
                            case 2://word
                                deviceStats->sasStatistics.readErrorsCorrectedWithoutSubstantialDelay.statisticValue = M_BytesTo2ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5]);
                                break;
                            case 4://double word
                                deviceStats->sasStatistics.readErrorsCorrectedWithoutSubstantialDelay.statisticValue = M_BytesTo4ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
                                break;
                            case 8://quad word
                                deviceStats->sasStatistics.readErrorsCorrectedWithoutSubstantialDelay.statisticValue = M_BytesTo8ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7], tempLogBuf[iter + 8], tempLogBuf[iter + 9], tempLogBuf[iter + 10], tempLogBuf[iter + 11]);
                                break;
                            default://don't bother trying to read the data since it's in a more complicated format to read than we care to handle in this code right now
                                deviceStats->sasStatistics.readErrorsCorrectedWithoutSubstantialDelay.isValueValid = false;
                                break;
                            }
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            break;
                        case 1://Errors corrected with possible delays
                            deviceStats->sasStatistics.readErrorsCorrectedWithPossibleDelays.isSupported = true;
                            deviceStats->sasStatistics.readErrorsCorrectedWithPossibleDelays.isValueValid = true;
                            //check if thresholds supported, etc
                            deviceStats->sasStatistics.readErrorsCorrectedWithPossibleDelays.thresholdNotificationEnabled = tempLogBuf[iter + 2] & BIT4;//ETC bit
                            if (tempLogBuf[iter + 2] & BIT4)
                            {
                                switch ((tempLogBuf[iter + 2] & (BIT2 | BIT3)) >> 2)
                                {
                                case 3:
                                    deviceStats->sasStatistics.readErrorsCorrectedWithPossibleDelays.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_GREATER;
                                    break;
                                case 2:
                                    deviceStats->sasStatistics.readErrorsCorrectedWithPossibleDelays.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL;
                                    break;
                                case 1:
                                    deviceStats->sasStatistics.readErrorsCorrectedWithPossibleDelays.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL;
                                    break;
                                case 0:
                                default:
                                    deviceStats->sasStatistics.readErrorsCorrectedWithPossibleDelays.threshType = THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE;
                                    break;
                                }
                            }
                            switch (parameterLength)
                            {
                            case 1://single byte
                                deviceStats->sasStatistics.readErrorsCorrectedWithPossibleDelays.statisticValue = tempLogBuf[iter + 4];
                                break;
                            case 2://word
                                deviceStats->sasStatistics.readErrorsCorrectedWithPossibleDelays.statisticValue = M_BytesTo2ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5]);
                                break;
                            case 4://double word
                                deviceStats->sasStatistics.readErrorsCorrectedWithPossibleDelays.statisticValue = M_BytesTo4ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
                                break;
                            case 8://quad word
                                deviceStats->sasStatistics.readErrorsCorrectedWithPossibleDelays.statisticValue = M_BytesTo8ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7], tempLogBuf[iter + 8], tempLogBuf[iter + 9], tempLogBuf[iter + 10], tempLogBuf[iter + 11]);
                                break;
                            default://don't bother trying to read the data since it's in a more complicated format to read than we care to handle in this code right now
                                deviceStats->sasStatistics.readErrorsCorrectedWithPossibleDelays.isValueValid = false;
                                break;
                            }
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            break;
                        case 2://total
                            deviceStats->sasStatistics.readTotal.isSupported = true;
                            deviceStats->sasStatistics.readTotal.isValueValid = true;
                            //check if thresholds supported, etc
                            deviceStats->sasStatistics.readTotal.thresholdNotificationEnabled = tempLogBuf[iter + 2] & BIT4;//ETC bit
                            if (tempLogBuf[iter + 2] & BIT4)
                            {
                                switch ((tempLogBuf[iter + 2] & (BIT2 | BIT3)) >> 2)
                                {
                                case 3:
                                    deviceStats->sasStatistics.readTotal.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_GREATER;
                                    break;
                                case 2:
                                    deviceStats->sasStatistics.readTotal.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL;
                                    break;
                                case 1:
                                    deviceStats->sasStatistics.readTotal.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL;
                                    break;
                                case 0:
                                default:
                                    deviceStats->sasStatistics.readTotal.threshType = THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE;
                                    break;
                                }
                            }
                            switch (parameterLength)
                            {
                            case 1://single byte
                                deviceStats->sasStatistics.readTotal.statisticValue = tempLogBuf[iter + 4];
                                break;
                            case 2://word
                                deviceStats->sasStatistics.readTotal.statisticValue = M_BytesTo2ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5]);
                                break;
                            case 4://double word
                                deviceStats->sasStatistics.readTotal.statisticValue = M_BytesTo4ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
                                break;
                            case 8://quad word
                                deviceStats->sasStatistics.readTotal.statisticValue = M_BytesTo8ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7], tempLogBuf[iter + 8], tempLogBuf[iter + 9], tempLogBuf[iter + 10], tempLogBuf[iter + 11]);
                                break;
                            default://don't bother trying to read the data since it's in a more complicated format to read than we care to handle in this code right now
                                deviceStats->sasStatistics.readTotal.isValueValid = false;
                                break;
                            }
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            break;
                        case 3://total errors corrected
                            deviceStats->sasStatistics.readErrorsCorrected.isSupported = true;
                            deviceStats->sasStatistics.readErrorsCorrected.isValueValid = true;
                            //check if thresholds supported, etc
                            deviceStats->sasStatistics.readErrorsCorrected.thresholdNotificationEnabled = tempLogBuf[iter + 2] & BIT4;//ETC bit
                            if (tempLogBuf[iter + 2] & BIT4)
                            {
                                switch ((tempLogBuf[iter + 2] & (BIT2 | BIT3)) >> 2)
                                {
                                case 3:
                                    deviceStats->sasStatistics.readErrorsCorrected.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_GREATER;
                                    break;
                                case 2:
                                    deviceStats->sasStatistics.readErrorsCorrected.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL;
                                    break;
                                case 1:
                                    deviceStats->sasStatistics.readErrorsCorrected.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL;
                                    break;
                                case 0:
                                default:
                                    deviceStats->sasStatistics.readErrorsCorrected.threshType = THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE;
                                    break;
                                }
                            }
                            switch (parameterLength)
                            {
                            case 1://single byte
                                deviceStats->sasStatistics.readErrorsCorrected.statisticValue = tempLogBuf[iter + 4];
                                break;
                            case 2://word
                                deviceStats->sasStatistics.readErrorsCorrected.statisticValue = M_BytesTo2ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5]);
                                break;
                            case 4://double word
                                deviceStats->sasStatistics.readErrorsCorrected.statisticValue = M_BytesTo4ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
                                break;
                            case 8://quad word
                                deviceStats->sasStatistics.readErrorsCorrected.statisticValue = M_BytesTo8ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7], tempLogBuf[iter + 8], tempLogBuf[iter + 9], tempLogBuf[iter + 10], tempLogBuf[iter + 11]);
                                break;
                            default://don't bother trying to read the data since it's in a more complicated format to read than we care to handle in this code right now
                                deviceStats->sasStatistics.readErrorsCorrected.isValueValid = false;
                                break;
                            }
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            break;
                        case 4://total times correction algorithm processed
                            deviceStats->sasStatistics.readTotalTimeCorrectionAlgorithmProcessed.isSupported = true;
                            deviceStats->sasStatistics.readTotalTimeCorrectionAlgorithmProcessed.isValueValid = true;
                            //check if thresholds supported, etc
                            deviceStats->sasStatistics.readTotalTimeCorrectionAlgorithmProcessed.thresholdNotificationEnabled = tempLogBuf[iter + 2] & BIT4;//ETC bit
                            if (tempLogBuf[iter + 2] & BIT4)
                            {
                                switch ((tempLogBuf[iter + 2] & (BIT2 | BIT3)) >> 2)
                                {
                                case 3:
                                    deviceStats->sasStatistics.readTotalTimeCorrectionAlgorithmProcessed.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_GREATER;
                                    break;
                                case 2:
                                    deviceStats->sasStatistics.readTotalTimeCorrectionAlgorithmProcessed.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL;
                                    break;
                                case 1:
                                    deviceStats->sasStatistics.readTotalTimeCorrectionAlgorithmProcessed.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL;
                                    break;
                                case 0:
                                default:
                                    deviceStats->sasStatistics.readTotalTimeCorrectionAlgorithmProcessed.threshType = THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE;
                                    break;
                                }
                            }
                            switch (parameterLength)
                            {
                            case 1://single byte
                                deviceStats->sasStatistics.readTotalTimeCorrectionAlgorithmProcessed.statisticValue = tempLogBuf[iter + 4];
                                break;
                            case 2://word
                                deviceStats->sasStatistics.readTotalTimeCorrectionAlgorithmProcessed.statisticValue = M_BytesTo2ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5]);
                                break;
                            case 4://double word
                                deviceStats->sasStatistics.readTotalTimeCorrectionAlgorithmProcessed.statisticValue = M_BytesTo4ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
                                break;
                            case 8://quad word
                                deviceStats->sasStatistics.readTotalTimeCorrectionAlgorithmProcessed.statisticValue = M_BytesTo8ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7], tempLogBuf[iter + 8], tempLogBuf[iter + 9], tempLogBuf[iter + 10], tempLogBuf[iter + 11]);
                                break;
                            default://don't bother trying to read the data since it's in a more complicated format to read than we care to handle in this code right now
                                deviceStats->sasStatistics.readTotalTimeCorrectionAlgorithmProcessed.isValueValid = false;
                                break;
                            }
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            break;
                        case 5://total bytes processed
                            deviceStats->sasStatistics.readTotalBytesProcessed.isSupported = true;
                            deviceStats->sasStatistics.readTotalBytesProcessed.isValueValid = true;
                            //check if thresholds supported, etc
                            deviceStats->sasStatistics.readTotalBytesProcessed.thresholdNotificationEnabled = tempLogBuf[iter + 2] & BIT4;//ETC bit
                            if (tempLogBuf[iter + 2] & BIT4)
                            {
                                switch ((tempLogBuf[iter + 2] & (BIT2 | BIT3)) >> 2)
                                {
                                case 3:
                                    deviceStats->sasStatistics.readTotalBytesProcessed.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_GREATER;
                                    break;
                                case 2:
                                    deviceStats->sasStatistics.readTotalBytesProcessed.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL;
                                    break;
                                case 1:
                                    deviceStats->sasStatistics.readTotalBytesProcessed.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL;
                                    break;
                                case 0:
                                default:
                                    deviceStats->sasStatistics.readTotalBytesProcessed.threshType = THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE;
                                    break;
                                }
                            }
                            switch (parameterLength)
                            {
                            case 1://single byte
                                deviceStats->sasStatistics.readTotalBytesProcessed.statisticValue = tempLogBuf[iter + 4];
                                break;
                            case 2://word
                                deviceStats->sasStatistics.readTotalBytesProcessed.statisticValue = M_BytesTo2ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5]);
                                break;
                            case 4://double word
                                deviceStats->sasStatistics.readTotalBytesProcessed.statisticValue = M_BytesTo4ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
                                break;
                            case 8://quad word
                                deviceStats->sasStatistics.readTotalBytesProcessed.statisticValue = M_BytesTo8ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7], tempLogBuf[iter + 8], tempLogBuf[iter + 9], tempLogBuf[iter + 10], tempLogBuf[iter + 11]);
                                break;
                            default://don't bother trying to read the data since it's in a more complicated format to read than we care to handle in this code right now
                                deviceStats->sasStatistics.readTotalBytesProcessed.isValueValid = false;
                                break;
                            }
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            break;
                        case 6://total uncorrected errors
                            deviceStats->sasStatistics.readTotalUncorrectedErrors.isSupported = true;
                            deviceStats->sasStatistics.readTotalUncorrectedErrors.isValueValid = true;
                            //check if thresholds supported, etc
                            deviceStats->sasStatistics.readTotalUncorrectedErrors.thresholdNotificationEnabled = tempLogBuf[iter + 2] & BIT4;//ETC bit
                            if (tempLogBuf[iter + 2] & BIT4)
                            {
                                switch ((tempLogBuf[iter + 2] & (BIT2 | BIT3)) >> 2)
                                {
                                case 3:
                                    deviceStats->sasStatistics.readTotalUncorrectedErrors.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_GREATER;
                                    break;
                                case 2:
                                    deviceStats->sasStatistics.readTotalUncorrectedErrors.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL;
                                    break;
                                case 1:
                                    deviceStats->sasStatistics.readTotalUncorrectedErrors.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL;
                                    break;
                                case 0:
                                default:
                                    deviceStats->sasStatistics.readTotalUncorrectedErrors.threshType = THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE;
                                    break;
                                }
                            }
                            switch (parameterLength)
                            {
                            case 1://single byte
                                deviceStats->sasStatistics.readTotalUncorrectedErrors.statisticValue = tempLogBuf[iter + 4];
                                break;
                            case 2://word
                                deviceStats->sasStatistics.readTotalUncorrectedErrors.statisticValue = M_BytesTo2ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5]);
                                break;
                            case 4://double word
                                deviceStats->sasStatistics.readTotalUncorrectedErrors.statisticValue = M_BytesTo4ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
                                break;
                            case 8://quad word
                                deviceStats->sasStatistics.readTotalUncorrectedErrors.statisticValue = M_BytesTo8ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7], tempLogBuf[iter + 8], tempLogBuf[iter + 9], tempLogBuf[iter + 10], tempLogBuf[iter + 11]);
                                break;
                            default://don't bother trying to read the data since it's in a more complicated format to read than we care to handle in this code right now
                                deviceStats->sasStatistics.readTotalUncorrectedErrors.isValueValid = false;
                                break;
                            }
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            break;
                        default:
                            break;
                        }
                        if (parameterLength == 0)
                        {
                            break;
                        }
                    }
                    //thresholds
                    memset(tempLogBuf, 0, LEGACY_DRIVE_SEC_SIZE);
                    if (SUCCESS == scsi_Log_Sense_Cmd(device, false, LPC_THRESHOLD_VALUES, pageCode, subpageCode, 0x0000, tempLogBuf, LEGACY_DRIVE_SEC_SIZE))
                    {
                        uint16_t pageLength = M_BytesTo2ByteValue(tempLogBuf[2], tempLogBuf[3]);
                        uint8_t parameterLength = 0;
                        //loop through the data and gather the data from each parameter we care about getting.
                        for (uint16_t iter = 4; iter < pageLength && iter < LEGACY_DRIVE_SEC_SIZE; iter += (parameterLength + 4))
                        {
                            uint16_t parameterCode = M_BytesTo2ByteValue(tempLogBuf[iter], tempLogBuf[iter + 1]);
                            parameterLength = tempLogBuf[iter + 3];
                            switch (parameterCode)
                            {
                            case 0://Errors corrected without substantial delay
                                deviceStats->sasStatistics.readErrorsCorrectedWithoutSubstantialDelay.supportsNotification = true;
                                if (tempLogBuf[iter + 2] & BIT4)
                                {
                                    deviceStats->sasStatistics.readErrorsCorrectedWithoutSubstantialDelay.isThresholdValid = true;
                                    switch (parameterLength)
                                    {
                                    case 1://single byte
                                        deviceStats->sasStatistics.readErrorsCorrectedWithoutSubstantialDelay.threshold = tempLogBuf[iter + 4];
                                        break;
                                    case 2://word
                                        deviceStats->sasStatistics.readErrorsCorrectedWithoutSubstantialDelay.threshold = M_BytesTo2ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5]);
                                        break;
                                    case 4://double word
                                        deviceStats->sasStatistics.readErrorsCorrectedWithoutSubstantialDelay.threshold = M_BytesTo4ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
                                        break;
                                    case 8://quad word
                                        deviceStats->sasStatistics.readErrorsCorrectedWithoutSubstantialDelay.threshold = M_BytesTo8ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7], tempLogBuf[iter + 8], tempLogBuf[iter + 9], tempLogBuf[iter + 10], tempLogBuf[iter + 11]);
                                        break;
                                    default://don't bother trying to read the data since it's in a more complicated format to read than we care to handle in this code right now
                                        deviceStats->sasStatistics.readErrorsCorrectedWithoutSubstantialDelay.isThresholdValid = false;
                                        break;
                                    }
                                    scsi_Threshold_Comparison(&deviceStats->sasStatistics.readErrorsCorrectedWithoutSubstantialDelay);
                                }
                                break;
                            case 1://Errors corrected with possible delays
                                deviceStats->sasStatistics.readErrorsCorrectedWithPossibleDelays.supportsNotification = true;
                                if (tempLogBuf[iter + 2] & BIT4)
                                {
                                    deviceStats->sasStatistics.readErrorsCorrectedWithPossibleDelays.isThresholdValid = true;
                                    switch (parameterLength)
                                    {
                                    case 1://single byte
                                        deviceStats->sasStatistics.readErrorsCorrectedWithPossibleDelays.threshold = tempLogBuf[iter + 4];
                                        break;
                                    case 2://word
                                        deviceStats->sasStatistics.readErrorsCorrectedWithPossibleDelays.threshold = M_BytesTo2ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5]);
                                        break;
                                    case 4://double word
                                        deviceStats->sasStatistics.readErrorsCorrectedWithPossibleDelays.threshold = M_BytesTo4ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
                                        break;
                                    case 8://quad word
                                        deviceStats->sasStatistics.readErrorsCorrectedWithPossibleDelays.threshold = M_BytesTo8ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7], tempLogBuf[iter + 8], tempLogBuf[iter + 9], tempLogBuf[iter + 10], tempLogBuf[iter + 11]);
                                        break;
                                    default://don't bother trying to read the data since it's in a more complicated format to read than we care to handle in this code right now
                                        deviceStats->sasStatistics.readErrorsCorrectedWithPossibleDelays.isThresholdValid = false;
                                        break;
                                    }
                                    scsi_Threshold_Comparison(&deviceStats->sasStatistics.readErrorsCorrectedWithPossibleDelays);
                                }
                                break;
                            case 2://total
                                deviceStats->sasStatistics.readTotal.supportsNotification = true;
                                if (tempLogBuf[iter + 2] & BIT4)
                                {
                                    deviceStats->sasStatistics.readTotal.isThresholdValid = true;
                                    switch (parameterLength)
                                    {
                                    case 1://single byte
                                        deviceStats->sasStatistics.readTotal.threshold = tempLogBuf[iter + 4];
                                        break;
                                    case 2://word
                                        deviceStats->sasStatistics.readTotal.threshold = M_BytesTo2ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5]);
                                        break;
                                    case 4://double word
                                        deviceStats->sasStatistics.readTotal.threshold = M_BytesTo4ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
                                        break;
                                    case 8://quad word
                                        deviceStats->sasStatistics.readTotal.threshold = M_BytesTo8ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7], tempLogBuf[iter + 8], tempLogBuf[iter + 9], tempLogBuf[iter + 10], tempLogBuf[iter + 11]);
                                        break;
                                    default://don't bother trying to read the data since it's in a more complicated format to read than we care to handle in this code right now
                                        deviceStats->sasStatistics.readTotal.isThresholdValid = false;
                                        break;
                                    }
                                    scsi_Threshold_Comparison(&deviceStats->sasStatistics.readTotal);
                                }
                                break;
                            case 3://total errors corrected
                                deviceStats->sasStatistics.readErrorsCorrected.supportsNotification = true;
                                if (tempLogBuf[iter + 2] & BIT4)
                                {
                                    deviceStats->sasStatistics.readErrorsCorrected.isThresholdValid = true;
                                    switch (parameterLength)
                                    {
                                    case 1://single byte
                                        deviceStats->sasStatistics.readErrorsCorrected.threshold = tempLogBuf[iter + 4];
                                        break;
                                    case 2://word
                                        deviceStats->sasStatistics.readErrorsCorrected.threshold = M_BytesTo2ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5]);
                                        break;
                                    case 4://double word
                                        deviceStats->sasStatistics.readErrorsCorrected.threshold = M_BytesTo4ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
                                        break;
                                    case 8://quad word
                                        deviceStats->sasStatistics.readErrorsCorrected.threshold = M_BytesTo8ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7], tempLogBuf[iter + 8], tempLogBuf[iter + 9], tempLogBuf[iter + 10], tempLogBuf[iter + 11]);
                                        break;
                                    default://don't bother trying to read the data since it's in a more complicated format to read than we care to handle in this code right now
                                        deviceStats->sasStatistics.readErrorsCorrected.isThresholdValid = false;
                                        break;
                                    }
                                    scsi_Threshold_Comparison(&deviceStats->sasStatistics.readErrorsCorrected);
                                }
                                break;
                            case 4://total times correction algorithm processed
                                deviceStats->sasStatistics.readTotalTimeCorrectionAlgorithmProcessed.supportsNotification = true;
                                if (tempLogBuf[iter + 2] & BIT4)
                                {
                                    deviceStats->sasStatistics.readTotalTimeCorrectionAlgorithmProcessed.isThresholdValid = true;
                                    switch (parameterLength)
                                    {
                                    case 1://single byte
                                        deviceStats->sasStatistics.readTotalTimeCorrectionAlgorithmProcessed.threshold = tempLogBuf[iter + 4];
                                        break;
                                    case 2://word
                                        deviceStats->sasStatistics.readTotalTimeCorrectionAlgorithmProcessed.threshold = M_BytesTo2ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5]);
                                        break;
                                    case 4://double word
                                        deviceStats->sasStatistics.readTotalTimeCorrectionAlgorithmProcessed.threshold = M_BytesTo4ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
                                        break;
                                    case 8://quad word
                                        deviceStats->sasStatistics.readTotalTimeCorrectionAlgorithmProcessed.threshold = M_BytesTo8ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7], tempLogBuf[iter + 8], tempLogBuf[iter + 9], tempLogBuf[iter + 10], tempLogBuf[iter + 11]);
                                        break;
                                    default://don't bother trying to read the data since it's in a more complicated format to read than we care to handle in this code right now
                                        deviceStats->sasStatistics.readTotalTimeCorrectionAlgorithmProcessed.isThresholdValid = false;
                                        break;
                                    }
                                    scsi_Threshold_Comparison(&deviceStats->sasStatistics.readTotalTimeCorrectionAlgorithmProcessed);
                                }
                                break;
                            case 5://total bytes processed
                                deviceStats->sasStatistics.readTotalBytesProcessed.supportsNotification = true;
                                if (tempLogBuf[iter + 2] & BIT4)
                                {
                                    deviceStats->sasStatistics.readTotalBytesProcessed.isThresholdValid = true;
                                    switch (parameterLength)
                                    {
                                    case 1://single byte
                                        deviceStats->sasStatistics.readTotalBytesProcessed.threshold = tempLogBuf[iter + 4];
                                        break;
                                    case 2://word
                                        deviceStats->sasStatistics.readTotalBytesProcessed.threshold = M_BytesTo2ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5]);
                                        break;
                                    case 4://double word
                                        deviceStats->sasStatistics.readTotalBytesProcessed.threshold = M_BytesTo4ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
                                        break;
                                    case 8://quad word
                                        deviceStats->sasStatistics.readTotalBytesProcessed.threshold = M_BytesTo8ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7], tempLogBuf[iter + 8], tempLogBuf[iter + 9], tempLogBuf[iter + 10], tempLogBuf[iter + 11]);
                                        break;
                                    default://don't bother trying to read the data since it's in a more complicated format to read than we care to handle in this code right now
                                        deviceStats->sasStatistics.readTotalBytesProcessed.isThresholdValid = false;
                                        break;
                                    }
                                    scsi_Threshold_Comparison(&deviceStats->sasStatistics.readTotalBytesProcessed);
                                }
                                break;
                            case 6://total uncorrected errors
                                deviceStats->sasStatistics.readTotalUncorrectedErrors.supportsNotification = true;
                                if (tempLogBuf[iter + 2] & BIT4)
                                {
                                    deviceStats->sasStatistics.readTotalUncorrectedErrors.isThresholdValid = true;
                                    switch (parameterLength)
                                    {
                                    case 1://single byte
                                        deviceStats->sasStatistics.readTotalUncorrectedErrors.threshold = tempLogBuf[iter + 4];
                                        break;
                                    case 2://word
                                        deviceStats->sasStatistics.readTotalUncorrectedErrors.threshold = M_BytesTo2ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5]);
                                        break;
                                    case 4://double word
                                        deviceStats->sasStatistics.readTotalUncorrectedErrors.threshold = M_BytesTo4ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
                                        break;
                                    case 8://quad word
                                        deviceStats->sasStatistics.readTotalUncorrectedErrors.threshold = M_BytesTo8ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7], tempLogBuf[iter + 8], tempLogBuf[iter + 9], tempLogBuf[iter + 10], tempLogBuf[iter + 11]);
                                        break;
                                    default://don't bother trying to read the data since it's in a more complicated format to read than we care to handle in this code right now
                                        deviceStats->sasStatistics.readTotalUncorrectedErrors.isThresholdValid = false;
                                        break;
                                    }
                                    scsi_Threshold_Comparison(&deviceStats->sasStatistics.readTotalUncorrectedErrors);
                                }
                                break;
                            default:
                                break;
                            }
                            if (parameterLength == 0)
                            {
                                break;
                            }
                        }
                    }
                }
            }
            break;
        case LP_READ_REVERSE_ERROR_COUNTERS:
            if (subpageCode == 0)
            {
                memset(tempLogBuf, 0, LEGACY_DRIVE_SEC_SIZE);
                if (SUCCESS == scsi_Log_Sense_Cmd(device, false, LPC_CUMULATIVE_VALUES, pageCode, subpageCode, 0x0000, tempLogBuf, LEGACY_DRIVE_SEC_SIZE))
                {
                    deviceStats->sasStatistics.readReverseErrorCountersSupported = true;
                    uint16_t pageLength = M_BytesTo2ByteValue(tempLogBuf[2], tempLogBuf[3]);
                    uint8_t parameterLength = 0;
                    //loop through the data and gather the data from each parameter we care about getting.
                    for (uint16_t iter = 4; iter < pageLength && iter < LEGACY_DRIVE_SEC_SIZE; iter += (parameterLength + 4))
                    {
                        uint16_t parameterCode = M_BytesTo2ByteValue(tempLogBuf[iter], tempLogBuf[iter + 1]);
                        parameterLength = tempLogBuf[iter + 3];
                        switch (parameterCode)
                        {
                        case 0://Errors corrected without substantial delay
                            deviceStats->sasStatistics.readReverseErrorsCorrectedWithoutSubstantialDelay.isSupported = true;
                            deviceStats->sasStatistics.readReverseErrorsCorrectedWithoutSubstantialDelay.isValueValid = true;
                            //check if thresholds supported, etc
                            deviceStats->sasStatistics.readReverseErrorsCorrectedWithoutSubstantialDelay.thresholdNotificationEnabled = tempLogBuf[iter + 2] & BIT4;//ETC bit
                            if (tempLogBuf[iter + 2] & BIT4)
                            {
                                switch ((tempLogBuf[iter + 2] & (BIT2 | BIT3)) >> 2)
                                {
                                case 3:
                                    deviceStats->sasStatistics.readReverseErrorsCorrectedWithoutSubstantialDelay.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_GREATER;
                                    break;
                                case 2:
                                    deviceStats->sasStatistics.readReverseErrorsCorrectedWithoutSubstantialDelay.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL;
                                    break;
                                case 1:
                                    deviceStats->sasStatistics.readReverseErrorsCorrectedWithoutSubstantialDelay.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL;
                                    break;
                                case 0:
                                default:
                                    deviceStats->sasStatistics.readReverseErrorsCorrectedWithoutSubstantialDelay.threshType = THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE;
                                    break;
                                }
                            }
                            switch (parameterLength)
                            {
                            case 1://single byte
                                deviceStats->sasStatistics.readReverseErrorsCorrectedWithoutSubstantialDelay.statisticValue = tempLogBuf[iter + 4];
                                break;
                            case 2://word
                                deviceStats->sasStatistics.readReverseErrorsCorrectedWithoutSubstantialDelay.statisticValue = M_BytesTo2ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5]);
                                break;
                            case 4://double word
                                deviceStats->sasStatistics.readReverseErrorsCorrectedWithoutSubstantialDelay.statisticValue = M_BytesTo4ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
                                break;
                            case 8://quad word
                                deviceStats->sasStatistics.readReverseErrorsCorrectedWithoutSubstantialDelay.statisticValue = M_BytesTo8ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7], tempLogBuf[iter + 8], tempLogBuf[iter + 9], tempLogBuf[iter + 10], tempLogBuf[iter + 11]);
                                break;
                            default://don't bother trying to read the data since it's in a more complicated format to read than we care to handle in this code right now
                                deviceStats->sasStatistics.readReverseErrorsCorrectedWithoutSubstantialDelay.isValueValid = false;
                                break;
                            }
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            break;
                        case 1://Errors corrected with possible delays
                            deviceStats->sasStatistics.readReverseErrorsCorrectedWithPossibleDelays.isSupported = true;
                            deviceStats->sasStatistics.readReverseErrorsCorrectedWithPossibleDelays.isValueValid = true;
                            //check if thresholds supported, etc
                            deviceStats->sasStatistics.readReverseErrorsCorrectedWithPossibleDelays.thresholdNotificationEnabled = tempLogBuf[iter + 2] & BIT4;//ETC bit
                            if (tempLogBuf[iter + 2] & BIT4)
                            {
                                switch ((tempLogBuf[iter + 2] & (BIT2 | BIT3)) >> 2)
                                {
                                case 3:
                                    deviceStats->sasStatistics.readReverseErrorsCorrectedWithPossibleDelays.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_GREATER;
                                    break;
                                case 2:
                                    deviceStats->sasStatistics.readReverseErrorsCorrectedWithPossibleDelays.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL;
                                    break;
                                case 1:
                                    deviceStats->sasStatistics.readReverseErrorsCorrectedWithPossibleDelays.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL;
                                    break;
                                case 0:
                                default:
                                    deviceStats->sasStatistics.readReverseErrorsCorrectedWithPossibleDelays.threshType = THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE;
                                    break;
                                }
                            }
                            switch (parameterLength)
                            {
                            case 1://single byte
                                deviceStats->sasStatistics.readReverseErrorsCorrectedWithPossibleDelays.statisticValue = tempLogBuf[iter + 4];
                                break;
                            case 2://word
                                deviceStats->sasStatistics.readReverseErrorsCorrectedWithPossibleDelays.statisticValue = M_BytesTo2ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5]);
                                break;
                            case 4://double word
                                deviceStats->sasStatistics.readReverseErrorsCorrectedWithPossibleDelays.statisticValue = M_BytesTo4ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
                                break;
                            case 8://quad word
                                deviceStats->sasStatistics.readReverseErrorsCorrectedWithPossibleDelays.statisticValue = M_BytesTo8ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7], tempLogBuf[iter + 8], tempLogBuf[iter + 9], tempLogBuf[iter + 10], tempLogBuf[iter + 11]);
                                break;
                            default://don't bother trying to read the data since it's in a more complicated format to read than we care to handle in this code right now
                                deviceStats->sasStatistics.readReverseErrorsCorrectedWithPossibleDelays.isValueValid = false;
                                break;
                            }
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            break;
                        case 2://total
                            deviceStats->sasStatistics.readReverseTotal.isSupported = true;
                            deviceStats->sasStatistics.readReverseTotal.isValueValid = true;
                            //check if thresholds supported, etc
                            deviceStats->sasStatistics.readReverseTotal.thresholdNotificationEnabled = tempLogBuf[iter + 2] & BIT4;//ETC bit
                            if (tempLogBuf[iter + 2] & BIT4)
                            {
                                switch ((tempLogBuf[iter + 2] & (BIT2 | BIT3)) >> 2)
                                {
                                case 3:
                                    deviceStats->sasStatistics.readReverseTotal.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_GREATER;
                                    break;
                                case 2:
                                    deviceStats->sasStatistics.readReverseTotal.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL;
                                    break;
                                case 1:
                                    deviceStats->sasStatistics.readReverseTotal.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL;
                                    break;
                                case 0:
                                default:
                                    deviceStats->sasStatistics.readReverseTotal.threshType = THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE;
                                    break;
                                }
                            }
                            switch (parameterLength)
                            {
                            case 1://single byte
                                deviceStats->sasStatistics.readReverseTotal.statisticValue = tempLogBuf[iter + 4];
                                break;
                            case 2://word
                                deviceStats->sasStatistics.readReverseTotal.statisticValue = M_BytesTo2ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5]);
                                break;
                            case 4://double word
                                deviceStats->sasStatistics.readReverseTotal.statisticValue = M_BytesTo4ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
                                break;
                            case 8://quad word
                                deviceStats->sasStatistics.readReverseTotal.statisticValue = M_BytesTo8ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7], tempLogBuf[iter + 8], tempLogBuf[iter + 9], tempLogBuf[iter + 10], tempLogBuf[iter + 11]);
                                break;
                            default://don't bother trying to read the data since it's in a more complicated format to read than we care to handle in this code right now
                                deviceStats->sasStatistics.readReverseTotal.isValueValid = false;
                                break;
                            }
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            break;
                        case 3://total errors corrected
                            deviceStats->sasStatistics.readReverseErrorsCorrected.isSupported = true;
                            deviceStats->sasStatistics.readReverseErrorsCorrected.isValueValid = true;
                            //check if thresholds supported, etc
                            deviceStats->sasStatistics.readReverseErrorsCorrected.thresholdNotificationEnabled = tempLogBuf[iter + 2] & BIT4;//ETC bit
                            if (tempLogBuf[iter + 2] & BIT4)
                            {
                                switch ((tempLogBuf[iter + 2] & (BIT2 | BIT3)) >> 2)
                                {
                                case 3:
                                    deviceStats->sasStatistics.readReverseErrorsCorrected.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_GREATER;
                                    break;
                                case 2:
                                    deviceStats->sasStatistics.readReverseErrorsCorrected.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL;
                                    break;
                                case 1:
                                    deviceStats->sasStatistics.readReverseErrorsCorrected.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL;
                                    break;
                                case 0:
                                default:
                                    deviceStats->sasStatistics.readReverseErrorsCorrected.threshType = THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE;
                                    break;
                                }
                            }
                            switch (parameterLength)
                            {
                            case 1://single byte
                                deviceStats->sasStatistics.readReverseErrorsCorrected.statisticValue = tempLogBuf[iter + 4];
                                break;
                            case 2://word
                                deviceStats->sasStatistics.readReverseErrorsCorrected.statisticValue = M_BytesTo2ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5]);
                                break;
                            case 4://double word
                                deviceStats->sasStatistics.readReverseErrorsCorrected.statisticValue = M_BytesTo4ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
                                break;
                            case 8://quad word
                                deviceStats->sasStatistics.readReverseErrorsCorrected.statisticValue = M_BytesTo8ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7], tempLogBuf[iter + 8], tempLogBuf[iter + 9], tempLogBuf[iter + 10], tempLogBuf[iter + 11]);
                                break;
                            default://don't bother trying to read the data since it's in a more complicated format to read than we care to handle in this code right now
                                deviceStats->sasStatistics.readReverseErrorsCorrected.isValueValid = false;
                                break;
                            }
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            break;
                        case 4://total times correction algorithm processed
                            deviceStats->sasStatistics.readReverseTotalTimeCorrectionAlgorithmProcessed.isSupported = true;
                            deviceStats->sasStatistics.readReverseTotalTimeCorrectionAlgorithmProcessed.isValueValid = true;
                            //check if thresholds supported, etc
                            deviceStats->sasStatistics.readReverseTotalTimeCorrectionAlgorithmProcessed.thresholdNotificationEnabled = tempLogBuf[iter + 2] & BIT4;//ETC bit
                            if (tempLogBuf[iter + 2] & BIT4)
                            {
                                switch ((tempLogBuf[iter + 2] & (BIT2 | BIT3)) >> 2)
                                {
                                case 3:
                                    deviceStats->sasStatistics.readReverseTotalTimeCorrectionAlgorithmProcessed.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_GREATER;
                                    break;
                                case 2:
                                    deviceStats->sasStatistics.readReverseTotalTimeCorrectionAlgorithmProcessed.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL;
                                    break;
                                case 1:
                                    deviceStats->sasStatistics.readReverseTotalTimeCorrectionAlgorithmProcessed.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL;
                                    break;
                                case 0:
                                default:
                                    deviceStats->sasStatistics.readReverseTotalTimeCorrectionAlgorithmProcessed.threshType = THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE;
                                    break;
                                }
                            }
                            switch (parameterLength)
                            {
                            case 1://single byte
                                deviceStats->sasStatistics.readReverseTotalTimeCorrectionAlgorithmProcessed.statisticValue = tempLogBuf[iter + 4];
                                break;
                            case 2://word
                                deviceStats->sasStatistics.readReverseTotalTimeCorrectionAlgorithmProcessed.statisticValue = M_BytesTo2ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5]);
                                break;
                            case 4://double word
                                deviceStats->sasStatistics.readReverseTotalTimeCorrectionAlgorithmProcessed.statisticValue = M_BytesTo4ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
                                break;
                            case 8://quad word
                                deviceStats->sasStatistics.readReverseTotalTimeCorrectionAlgorithmProcessed.statisticValue = M_BytesTo8ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7], tempLogBuf[iter + 8], tempLogBuf[iter + 9], tempLogBuf[iter + 10], tempLogBuf[iter + 11]);
                                break;
                            default://don't bother trying to read the data since it's in a more complicated format to read than we care to handle in this code right now
                                deviceStats->sasStatistics.readReverseTotalTimeCorrectionAlgorithmProcessed.isValueValid = false;
                                break;
                            }
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            break;
                        case 5://total bytes processed
                            deviceStats->sasStatistics.readReverseTotalBytesProcessed.isSupported = true;
                            deviceStats->sasStatistics.readReverseTotalBytesProcessed.isValueValid = true;
                            //check if thresholds supported, etc
                            deviceStats->sasStatistics.readReverseTotalBytesProcessed.thresholdNotificationEnabled = tempLogBuf[iter + 2] & BIT4;//ETC bit
                            if (tempLogBuf[iter + 2] & BIT4)
                            {
                                switch ((tempLogBuf[iter + 2] & (BIT2 | BIT3)) >> 2)
                                {
                                case 3:
                                    deviceStats->sasStatistics.readReverseTotalBytesProcessed.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_GREATER;
                                    break;
                                case 2:
                                    deviceStats->sasStatistics.readReverseTotalBytesProcessed.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL;
                                    break;
                                case 1:
                                    deviceStats->sasStatistics.readReverseTotalBytesProcessed.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL;
                                    break;
                                case 0:
                                default:
                                    deviceStats->sasStatistics.readReverseTotalBytesProcessed.threshType = THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE;
                                    break;
                                }
                            }
                            switch (parameterLength)
                            {
                            case 1://single byte
                                deviceStats->sasStatistics.readReverseTotalBytesProcessed.statisticValue = tempLogBuf[iter + 4];
                                break;
                            case 2://word
                                deviceStats->sasStatistics.readReverseTotalBytesProcessed.statisticValue = M_BytesTo2ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5]);
                                break;
                            case 4://double word
                                deviceStats->sasStatistics.readReverseTotalBytesProcessed.statisticValue = M_BytesTo4ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
                                break;
                            case 8://quad word
                                deviceStats->sasStatistics.readReverseTotalBytesProcessed.statisticValue = M_BytesTo8ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7], tempLogBuf[iter + 8], tempLogBuf[iter + 9], tempLogBuf[iter + 10], tempLogBuf[iter + 11]);
                                break;
                            default://don't bother trying to read the data since it's in a more complicated format to read than we care to handle in this code right now
                                deviceStats->sasStatistics.readReverseTotalBytesProcessed.isValueValid = false;
                                break;
                            }
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            break;
                        case 6://total uncorrected errors
                            deviceStats->sasStatistics.readReverseTotalUncorrectedErrors.isSupported = true;
                            deviceStats->sasStatistics.readReverseTotalUncorrectedErrors.isValueValid = true;
                            //check if thresholds supported, etc
                            deviceStats->sasStatistics.readReverseTotalUncorrectedErrors.thresholdNotificationEnabled = tempLogBuf[iter + 2] & BIT4;//ETC bit
                            if (tempLogBuf[iter + 2] & BIT4)
                            {
                                switch ((tempLogBuf[iter + 2] & (BIT2 | BIT3)) >> 2)
                                {
                                case 3:
                                    deviceStats->sasStatistics.readReverseTotalUncorrectedErrors.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_GREATER;
                                    break;
                                case 2:
                                    deviceStats->sasStatistics.readReverseTotalUncorrectedErrors.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL;
                                    break;
                                case 1:
                                    deviceStats->sasStatistics.readReverseTotalUncorrectedErrors.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL;
                                    break;
                                case 0:
                                default:
                                    deviceStats->sasStatistics.readReverseTotalUncorrectedErrors.threshType = THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE;
                                    break;
                                }
                            }
                            switch (parameterLength)
                            {
                            case 1://single byte
                                deviceStats->sasStatistics.readReverseTotalUncorrectedErrors.statisticValue = tempLogBuf[iter + 4];
                                break;
                            case 2://word
                                deviceStats->sasStatistics.readReverseTotalUncorrectedErrors.statisticValue = M_BytesTo2ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5]);
                                break;
                            case 4://double word
                                deviceStats->sasStatistics.readReverseTotalUncorrectedErrors.statisticValue = M_BytesTo4ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
                                break;
                            case 8://quad word
                                deviceStats->sasStatistics.readReverseTotalUncorrectedErrors.statisticValue = M_BytesTo8ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7], tempLogBuf[iter + 8], tempLogBuf[iter + 9], tempLogBuf[iter + 10], tempLogBuf[iter + 11]);
                                break;
                            default://don't bother trying to read the data since it's in a more complicated format to read than we care to handle in this code right now
                                deviceStats->sasStatistics.readReverseTotalUncorrectedErrors.isValueValid = false;
                                break;
                            }
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            break;
                        default:
                            break;
                        }
                        if (parameterLength == 0)
                        {
                            break;
                        }
                    }
                    //thresholds
                    memset(tempLogBuf, 0, LEGACY_DRIVE_SEC_SIZE);
                    if (SUCCESS == scsi_Log_Sense_Cmd(device, false, LPC_THRESHOLD_VALUES, pageCode, subpageCode, 0x0000, tempLogBuf, LEGACY_DRIVE_SEC_SIZE))
                    {
                        uint16_t pageLength = M_BytesTo2ByteValue(tempLogBuf[2], tempLogBuf[3]);
                        uint8_t parameterLength = 0;
                        //loop through the data and gather the data from each parameter we care about getting.
                        for (uint16_t iter = 4; iter < pageLength && iter < LEGACY_DRIVE_SEC_SIZE; iter += (parameterLength + 4))
                        {
                            uint16_t parameterCode = M_BytesTo2ByteValue(tempLogBuf[iter], tempLogBuf[iter + 1]);
                            parameterLength = tempLogBuf[iter + 3];
                            switch (parameterCode)
                            {
                            case 0://Errors corrected without substantial delay
                                deviceStats->sasStatistics.readReverseErrorsCorrectedWithoutSubstantialDelay.supportsNotification = true;
                                if (tempLogBuf[iter + 2] & BIT4)
                                {
                                    deviceStats->sasStatistics.readReverseErrorsCorrectedWithoutSubstantialDelay.isThresholdValid = true;
                                    switch (parameterLength)
                                    {
                                    case 1://single byte
                                        deviceStats->sasStatistics.readReverseErrorsCorrectedWithoutSubstantialDelay.threshold = tempLogBuf[iter + 4];
                                        break;
                                    case 2://word
                                        deviceStats->sasStatistics.readReverseErrorsCorrectedWithoutSubstantialDelay.threshold = M_BytesTo2ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5]);
                                        break;
                                    case 4://double word
                                        deviceStats->sasStatistics.readReverseErrorsCorrectedWithoutSubstantialDelay.threshold = M_BytesTo4ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
                                        break;
                                    case 8://quad word
                                        deviceStats->sasStatistics.readReverseErrorsCorrectedWithoutSubstantialDelay.threshold = M_BytesTo8ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7], tempLogBuf[iter + 8], tempLogBuf[iter + 9], tempLogBuf[iter + 10], tempLogBuf[iter + 11]);
                                        break;
                                    default://don't bother trying to read the data since it's in a more complicated format to read than we care to handle in this code right now
                                        deviceStats->sasStatistics.readReverseErrorsCorrectedWithoutSubstantialDelay.isThresholdValid = false;
                                        break;
                                    }
                                    scsi_Threshold_Comparison(&deviceStats->sasStatistics.readReverseErrorsCorrectedWithoutSubstantialDelay);
                                }
                                break;
                            case 1://Errors corrected with possible delays
                                deviceStats->sasStatistics.readReverseErrorsCorrectedWithPossibleDelays.supportsNotification = true;
                                if (tempLogBuf[iter + 2] & BIT4)
                                {
                                    deviceStats->sasStatistics.readReverseErrorsCorrectedWithPossibleDelays.isThresholdValid = true;
                                    switch (parameterLength)
                                    {
                                    case 1://single byte
                                        deviceStats->sasStatistics.readReverseErrorsCorrectedWithPossibleDelays.threshold = tempLogBuf[iter + 4];
                                        break;
                                    case 2://word
                                        deviceStats->sasStatistics.readReverseErrorsCorrectedWithPossibleDelays.threshold = M_BytesTo2ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5]);
                                        break;
                                    case 4://double word
                                        deviceStats->sasStatistics.readReverseErrorsCorrectedWithPossibleDelays.threshold = M_BytesTo4ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
                                        break;
                                    case 8://quad word
                                        deviceStats->sasStatistics.readReverseErrorsCorrectedWithPossibleDelays.threshold = M_BytesTo8ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7], tempLogBuf[iter + 8], tempLogBuf[iter + 9], tempLogBuf[iter + 10], tempLogBuf[iter + 11]);
                                        break;
                                    default://don't bother trying to read the data since it's in a more complicated format to read than we care to handle in this code right now
                                        deviceStats->sasStatistics.readReverseErrorsCorrectedWithPossibleDelays.isThresholdValid = false;
                                        break;
                                    }
                                    scsi_Threshold_Comparison(&deviceStats->sasStatistics.readReverseErrorsCorrectedWithPossibleDelays);
                                }
                                break;
                            case 2://total
                                deviceStats->sasStatistics.readReverseTotal.supportsNotification = true;
                                if (tempLogBuf[iter + 2] & BIT4)
                                {
                                    deviceStats->sasStatistics.readReverseTotal.isThresholdValid = true;
                                    switch (parameterLength)
                                    {
                                    case 1://single byte
                                        deviceStats->sasStatistics.readReverseTotal.threshold = tempLogBuf[iter + 4];
                                        break;
                                    case 2://word
                                        deviceStats->sasStatistics.readReverseTotal.threshold = M_BytesTo2ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5]);
                                        break;
                                    case 4://double word
                                        deviceStats->sasStatistics.readReverseTotal.threshold = M_BytesTo4ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
                                        break;
                                    case 8://quad word
                                        deviceStats->sasStatistics.readReverseTotal.threshold = M_BytesTo8ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7], tempLogBuf[iter + 8], tempLogBuf[iter + 9], tempLogBuf[iter + 10], tempLogBuf[iter + 11]);
                                        break;
                                    default://don't bother trying to read the data since it's in a more complicated format to read than we care to handle in this code right now
                                        deviceStats->sasStatistics.readReverseTotal.isThresholdValid = false;
                                        break;
                                    }
                                    scsi_Threshold_Comparison(&deviceStats->sasStatistics.readReverseTotal);
                                }
                                break;
                            case 3://total errors corrected
                                deviceStats->sasStatistics.readReverseErrorsCorrected.supportsNotification = true;
                                if (tempLogBuf[iter + 2] & BIT4)
                                {
                                    deviceStats->sasStatistics.readReverseErrorsCorrected.isThresholdValid = true;
                                    switch (parameterLength)
                                    {
                                    case 1://single byte
                                        deviceStats->sasStatistics.readReverseErrorsCorrected.threshold = tempLogBuf[iter + 4];
                                        break;
                                    case 2://word
                                        deviceStats->sasStatistics.readReverseErrorsCorrected.threshold = M_BytesTo2ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5]);
                                        break;
                                    case 4://double word
                                        deviceStats->sasStatistics.readReverseErrorsCorrected.threshold = M_BytesTo4ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
                                        break;
                                    case 8://quad word
                                        deviceStats->sasStatistics.readReverseErrorsCorrected.threshold = M_BytesTo8ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7], tempLogBuf[iter + 8], tempLogBuf[iter + 9], tempLogBuf[iter + 10], tempLogBuf[iter + 11]);
                                        break;
                                    default://don't bother trying to read the data since it's in a more complicated format to read than we care to handle in this code right now
                                        deviceStats->sasStatistics.readReverseErrorsCorrected.isThresholdValid = false;
                                        break;
                                    }
                                    scsi_Threshold_Comparison(&deviceStats->sasStatistics.readReverseErrorsCorrected);
                                }
                                break;
                            case 4://total times correction algorithm processed
                                deviceStats->sasStatistics.readReverseTotalTimeCorrectionAlgorithmProcessed.supportsNotification = true;
                                if (tempLogBuf[iter + 2] & BIT4)
                                {
                                    deviceStats->sasStatistics.readReverseTotalTimeCorrectionAlgorithmProcessed.isThresholdValid = true;
                                    switch (parameterLength)
                                    {
                                    case 1://single byte
                                        deviceStats->sasStatistics.readReverseTotalTimeCorrectionAlgorithmProcessed.threshold = tempLogBuf[iter + 4];
                                        break;
                                    case 2://word
                                        deviceStats->sasStatistics.readReverseTotalTimeCorrectionAlgorithmProcessed.threshold = M_BytesTo2ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5]);
                                        break;
                                    case 4://double word
                                        deviceStats->sasStatistics.readReverseTotalTimeCorrectionAlgorithmProcessed.threshold = M_BytesTo4ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
                                        break;
                                    case 8://quad word
                                        deviceStats->sasStatistics.readReverseTotalTimeCorrectionAlgorithmProcessed.threshold = M_BytesTo8ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7], tempLogBuf[iter + 8], tempLogBuf[iter + 9], tempLogBuf[iter + 10], tempLogBuf[iter + 11]);
                                        break;
                                    default://don't bother trying to read the data since it's in a more complicated format to read than we care to handle in this code right now
                                        deviceStats->sasStatistics.readReverseTotalTimeCorrectionAlgorithmProcessed.isThresholdValid = false;
                                        break;
                                    }
                                    scsi_Threshold_Comparison(&deviceStats->sasStatistics.readReverseTotalTimeCorrectionAlgorithmProcessed);
                                }
                                break;
                            case 5://total bytes processed
                                deviceStats->sasStatistics.readReverseTotalBytesProcessed.supportsNotification = true;
                                if (tempLogBuf[iter + 2] & BIT4)
                                {
                                    deviceStats->sasStatistics.readReverseTotalBytesProcessed.isThresholdValid = true;
                                    switch (parameterLength)
                                    {
                                    case 1://single byte
                                        deviceStats->sasStatistics.readReverseTotalBytesProcessed.threshold = tempLogBuf[iter + 4];
                                        break;
                                    case 2://word
                                        deviceStats->sasStatistics.readReverseTotalBytesProcessed.threshold = M_BytesTo2ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5]);
                                        break;
                                    case 4://double word
                                        deviceStats->sasStatistics.readReverseTotalBytesProcessed.threshold = M_BytesTo4ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
                                        break;
                                    case 8://quad word
                                        deviceStats->sasStatistics.readReverseTotalBytesProcessed.threshold = M_BytesTo8ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7], tempLogBuf[iter + 8], tempLogBuf[iter + 9], tempLogBuf[iter + 10], tempLogBuf[iter + 11]);
                                        break;
                                    default://don't bother trying to read the data since it's in a more complicated format to read than we care to handle in this code right now
                                        deviceStats->sasStatistics.readReverseTotalBytesProcessed.isThresholdValid = false;
                                        break;
                                    }
                                    scsi_Threshold_Comparison(&deviceStats->sasStatistics.readReverseTotalBytesProcessed);
                                }
                                break;
                            case 6://total uncorrected errors
                                deviceStats->sasStatistics.readReverseTotalUncorrectedErrors.supportsNotification = true;
                                if (tempLogBuf[iter + 2] & BIT4)
                                {
                                    deviceStats->sasStatistics.readReverseTotalUncorrectedErrors.isThresholdValid = true;
                                    switch (parameterLength)
                                    {
                                    case 1://single byte
                                        deviceStats->sasStatistics.readReverseTotalUncorrectedErrors.threshold = tempLogBuf[iter + 4];
                                        break;
                                    case 2://word
                                        deviceStats->sasStatistics.readReverseTotalUncorrectedErrors.threshold = M_BytesTo2ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5]);
                                        break;
                                    case 4://double word
                                        deviceStats->sasStatistics.readReverseTotalUncorrectedErrors.threshold = M_BytesTo4ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
                                        break;
                                    case 8://quad word
                                        deviceStats->sasStatistics.readReverseTotalUncorrectedErrors.threshold = M_BytesTo8ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7], tempLogBuf[iter + 8], tempLogBuf[iter + 9], tempLogBuf[iter + 10], tempLogBuf[iter + 11]);
                                        break;
                                    default://don't bother trying to read the data since it's in a more complicated format to read than we care to handle in this code right now
                                        deviceStats->sasStatistics.readReverseTotalUncorrectedErrors.isThresholdValid = false;
                                        break;
                                    }
                                    scsi_Threshold_Comparison(&deviceStats->sasStatistics.readReverseTotalUncorrectedErrors);
                                }
                                break;
                            default:
                                break;
                            }
                            if (parameterLength == 0)
                            {
                                break;
                            }
                        }
                    }
                }
            }
            break;
        case LP_VERIFY_ERROR_COUNTERS:
            if (subpageCode == 0)
            {
                memset(tempLogBuf, 0, LEGACY_DRIVE_SEC_SIZE);
                if (SUCCESS == scsi_Log_Sense_Cmd(device, false, LPC_CUMULATIVE_VALUES, pageCode, subpageCode, 0x0000, tempLogBuf, LEGACY_DRIVE_SEC_SIZE))
                {
                    deviceStats->sasStatistics.verifyErrorCountersSupported = true;
                    uint16_t pageLength = M_BytesTo2ByteValue(tempLogBuf[2], tempLogBuf[3]);
                    uint8_t parameterLength = 0;
                    //loop through the data and gather the data from each parameter we care about getting.
                    for (uint16_t iter = 4; iter < pageLength && iter < LEGACY_DRIVE_SEC_SIZE; iter += (parameterLength + 4))
                    {
                        uint16_t parameterCode = M_BytesTo2ByteValue(tempLogBuf[iter], tempLogBuf[iter + 1]);
                        parameterLength = tempLogBuf[iter + 3];
                        switch (parameterCode)
                        {
                        case 0://Errors corrected without substantial delay
                            deviceStats->sasStatistics.verifyErrorsCorrectedWithoutSubstantialDelay.isSupported = true;
                            deviceStats->sasStatistics.verifyErrorsCorrectedWithoutSubstantialDelay.isValueValid = true;
                            //check if thresholds supported, etc
                            deviceStats->sasStatistics.verifyErrorsCorrectedWithoutSubstantialDelay.thresholdNotificationEnabled = tempLogBuf[iter + 2] & BIT4;//ETC bit
                            if (tempLogBuf[iter + 2] & BIT4)
                            {
                                switch ((tempLogBuf[iter + 2] & (BIT2 | BIT3)) >> 2)
                                {
                                case 3:
                                    deviceStats->sasStatistics.verifyErrorsCorrectedWithoutSubstantialDelay.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_GREATER;
                                    break;
                                case 2:
                                    deviceStats->sasStatistics.verifyErrorsCorrectedWithoutSubstantialDelay.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL;
                                    break;
                                case 1:
                                    deviceStats->sasStatistics.verifyErrorsCorrectedWithoutSubstantialDelay.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL;
                                    break;
                                case 0:
                                default:
                                    deviceStats->sasStatistics.verifyErrorsCorrectedWithoutSubstantialDelay.threshType = THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE;
                                    break;
                                }
                            }
                            switch (parameterLength)
                            {
                            case 1://single byte
                                deviceStats->sasStatistics.verifyErrorsCorrectedWithoutSubstantialDelay.statisticValue = tempLogBuf[iter + 4];
                                break;
                            case 2://word
                                deviceStats->sasStatistics.verifyErrorsCorrectedWithoutSubstantialDelay.statisticValue = M_BytesTo2ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5]);
                                break;
                            case 4://double word
                                deviceStats->sasStatistics.verifyErrorsCorrectedWithoutSubstantialDelay.statisticValue = M_BytesTo4ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
                                break;
                            case 8://quad word
                                deviceStats->sasStatistics.verifyErrorsCorrectedWithoutSubstantialDelay.statisticValue = M_BytesTo8ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7], tempLogBuf[iter + 8], tempLogBuf[iter + 9], tempLogBuf[iter + 10], tempLogBuf[iter + 11]);
                                break;
                            default://don't bother trying to read the data since it's in a more complicated format to read than we care to handle in this code right now
                                deviceStats->sasStatistics.verifyErrorsCorrectedWithoutSubstantialDelay.isValueValid = false;
                                break;
                            }
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            break;
                        case 1://Errors corrected with possible delays
                            deviceStats->sasStatistics.verifyErrorsCorrectedWithPossibleDelays.isSupported = true;
                            deviceStats->sasStatistics.verifyErrorsCorrectedWithPossibleDelays.isValueValid = true;
                            //check if thresholds supported, etc
                            deviceStats->sasStatistics.verifyErrorsCorrectedWithPossibleDelays.thresholdNotificationEnabled = tempLogBuf[iter + 2] & BIT4;//ETC bit
                            if (tempLogBuf[iter + 2] & BIT4)
                            {
                                switch ((tempLogBuf[iter + 2] & (BIT2 | BIT3)) >> 2)
                                {
                                case 3:
                                    deviceStats->sasStatistics.verifyErrorsCorrectedWithPossibleDelays.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_GREATER;
                                    break;
                                case 2:
                                    deviceStats->sasStatistics.verifyErrorsCorrectedWithPossibleDelays.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL;
                                    break;
                                case 1:
                                    deviceStats->sasStatistics.verifyErrorsCorrectedWithPossibleDelays.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL;
                                    break;
                                case 0:
                                default:
                                    deviceStats->sasStatistics.verifyErrorsCorrectedWithPossibleDelays.threshType = THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE;
                                    break;
                                }
                            }
                            switch (parameterLength)
                            {
                            case 1://single byte
                                deviceStats->sasStatistics.verifyErrorsCorrectedWithPossibleDelays.statisticValue = tempLogBuf[iter + 4];
                                break;
                            case 2://word
                                deviceStats->sasStatistics.verifyErrorsCorrectedWithPossibleDelays.statisticValue = M_BytesTo2ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5]);
                                break;
                            case 4://double word
                                deviceStats->sasStatistics.verifyErrorsCorrectedWithPossibleDelays.statisticValue = M_BytesTo4ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
                                break;
                            case 8://quad word
                                deviceStats->sasStatistics.verifyErrorsCorrectedWithPossibleDelays.statisticValue = M_BytesTo8ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7], tempLogBuf[iter + 8], tempLogBuf[iter + 9], tempLogBuf[iter + 10], tempLogBuf[iter + 11]);
                                break;
                            default://don't bother trying to read the data since it's in a more complicated format to read than we care to handle in this code right now
                                deviceStats->sasStatistics.verifyErrorsCorrectedWithPossibleDelays.isValueValid = false;
                                break;
                            }
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            break;
                        case 2://total
                            deviceStats->sasStatistics.verifyTotal.isSupported = true;
                            deviceStats->sasStatistics.verifyTotal.isValueValid = true;
                            //check if thresholds supported, etc
                            deviceStats->sasStatistics.verifyTotal.thresholdNotificationEnabled = tempLogBuf[iter + 2] & BIT4;//ETC bit
                            if (tempLogBuf[iter + 2] & BIT4)
                            {
                                switch ((tempLogBuf[iter + 2] & (BIT2 | BIT3)) >> 2)
                                {
                                case 3:
                                    deviceStats->sasStatistics.verifyTotal.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_GREATER;
                                    break;
                                case 2:
                                    deviceStats->sasStatistics.verifyTotal.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL;
                                    break;
                                case 1:
                                    deviceStats->sasStatistics.verifyTotal.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL;
                                    break;
                                case 0:
                                default:
                                    deviceStats->sasStatistics.verifyTotal.threshType = THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE;
                                    break;
                                }
                            }
                            switch (parameterLength)
                            {
                            case 1://single byte
                                deviceStats->sasStatistics.verifyTotal.statisticValue = tempLogBuf[iter + 4];
                                break;
                            case 2://word
                                deviceStats->sasStatistics.verifyTotal.statisticValue = M_BytesTo2ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5]);
                                break;
                            case 4://double word
                                deviceStats->sasStatistics.verifyTotal.statisticValue = M_BytesTo4ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
                                break;
                            case 8://quad word
                                deviceStats->sasStatistics.verifyTotal.statisticValue = M_BytesTo8ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7], tempLogBuf[iter + 8], tempLogBuf[iter + 9], tempLogBuf[iter + 10], tempLogBuf[iter + 11]);
                                break;
                            default://don't bother trying to read the data since it's in a more complicated format to read than we care to handle in this code right now
                                deviceStats->sasStatistics.verifyTotal.isValueValid = false;
                                break;
                            }
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            break;
                        case 3://total errors corrected
                            deviceStats->sasStatistics.verifyErrorsCorrected.isSupported = true;
                            deviceStats->sasStatistics.verifyErrorsCorrected.isValueValid = true;
                            //check if thresholds supported, etc
                            deviceStats->sasStatistics.verifyErrorsCorrected.thresholdNotificationEnabled = tempLogBuf[iter + 2] & BIT4;//ETC bit
                            if (tempLogBuf[iter + 2] & BIT4)
                            {
                                switch ((tempLogBuf[iter + 2] & (BIT2 | BIT3)) >> 2)
                                {
                                case 3:
                                    deviceStats->sasStatistics.verifyErrorsCorrected.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_GREATER;
                                    break;
                                case 2:
                                    deviceStats->sasStatistics.verifyErrorsCorrected.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL;
                                    break;
                                case 1:
                                    deviceStats->sasStatistics.verifyErrorsCorrected.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL;
                                    break;
                                case 0:
                                default:
                                    deviceStats->sasStatistics.verifyErrorsCorrected.threshType = THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE;
                                    break;
                                }
                            }
                            switch (parameterLength)
                            {
                            case 1://single byte
                                deviceStats->sasStatistics.verifyErrorsCorrected.statisticValue = tempLogBuf[iter + 4];
                                break;
                            case 2://word
                                deviceStats->sasStatistics.verifyErrorsCorrected.statisticValue = M_BytesTo2ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5]);
                                break;
                            case 4://double word
                                deviceStats->sasStatistics.verifyErrorsCorrected.statisticValue = M_BytesTo4ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
                                break;
                            case 8://quad word
                                deviceStats->sasStatistics.verifyErrorsCorrected.statisticValue = M_BytesTo8ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7], tempLogBuf[iter + 8], tempLogBuf[iter + 9], tempLogBuf[iter + 10], tempLogBuf[iter + 11]);
                                break;
                            default://don't bother trying to read the data since it's in a more complicated format to read than we care to handle in this code right now
                                deviceStats->sasStatistics.verifyErrorsCorrected.isValueValid = false;
                                break;
                            }
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            break;
                        case 4://total times correction algorithm processed
                            deviceStats->sasStatistics.verifyTotalTimeCorrectionAlgorithmProcessed.isSupported = true;
                            deviceStats->sasStatistics.verifyTotalTimeCorrectionAlgorithmProcessed.isValueValid = true;
                            //check if thresholds supported, etc
                            deviceStats->sasStatistics.verifyTotalTimeCorrectionAlgorithmProcessed.thresholdNotificationEnabled = tempLogBuf[iter + 2] & BIT4;//ETC bit
                            if (tempLogBuf[iter + 2] & BIT4)
                            {
                                switch ((tempLogBuf[iter + 2] & (BIT2 | BIT3)) >> 2)
                                {
                                case 3:
                                    deviceStats->sasStatistics.verifyTotalTimeCorrectionAlgorithmProcessed.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_GREATER;
                                    break;
                                case 2:
                                    deviceStats->sasStatistics.verifyTotalTimeCorrectionAlgorithmProcessed.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL;
                                    break;
                                case 1:
                                    deviceStats->sasStatistics.verifyTotalTimeCorrectionAlgorithmProcessed.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL;
                                    break;
                                case 0:
                                default:
                                    deviceStats->sasStatistics.verifyTotalTimeCorrectionAlgorithmProcessed.threshType = THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE;
                                    break;
                                }
                            }
                            switch (parameterLength)
                            {
                            case 1://single byte
                                deviceStats->sasStatistics.verifyTotalTimeCorrectionAlgorithmProcessed.statisticValue = tempLogBuf[iter + 4];
                                break;
                            case 2://word
                                deviceStats->sasStatistics.verifyTotalTimeCorrectionAlgorithmProcessed.statisticValue = M_BytesTo2ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5]);
                                break;
                            case 4://double word
                                deviceStats->sasStatistics.verifyTotalTimeCorrectionAlgorithmProcessed.statisticValue = M_BytesTo4ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
                                break;
                            case 8://quad word
                                deviceStats->sasStatistics.verifyTotalTimeCorrectionAlgorithmProcessed.statisticValue = M_BytesTo8ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7], tempLogBuf[iter + 8], tempLogBuf[iter + 9], tempLogBuf[iter + 10], tempLogBuf[iter + 11]);
                                break;
                            default://don't bother trying to read the data since it's in a more complicated format to read than we care to handle in this code right now
                                deviceStats->sasStatistics.verifyTotalTimeCorrectionAlgorithmProcessed.isValueValid = false;
                                break;
                            }
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            break;
                        case 5://total bytes processed
                            deviceStats->sasStatistics.verifyTotalBytesProcessed.isSupported = true;
                            deviceStats->sasStatistics.verifyTotalBytesProcessed.isValueValid = true;
                            //check if thresholds supported, etc
                            deviceStats->sasStatistics.verifyTotalBytesProcessed.thresholdNotificationEnabled = tempLogBuf[iter + 2] & BIT4;//ETC bit
                            if (tempLogBuf[iter + 2] & BIT4)
                            {
                                switch ((tempLogBuf[iter + 2] & (BIT2 | BIT3)) >> 2)
                                {
                                case 3:
                                    deviceStats->sasStatistics.verifyTotalBytesProcessed.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_GREATER;
                                    break;
                                case 2:
                                    deviceStats->sasStatistics.verifyTotalBytesProcessed.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL;
                                    break;
                                case 1:
                                    deviceStats->sasStatistics.verifyTotalBytesProcessed.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL;
                                    break;
                                case 0:
                                default:
                                    deviceStats->sasStatistics.verifyTotalBytesProcessed.threshType = THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE;
                                    break;
                                }
                            }
                            switch (parameterLength)
                            {
                            case 1://single byte
                                deviceStats->sasStatistics.verifyTotalBytesProcessed.statisticValue = tempLogBuf[iter + 4];
                                break;
                            case 2://word
                                deviceStats->sasStatistics.verifyTotalBytesProcessed.statisticValue = M_BytesTo2ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5]);
                                break;
                            case 4://double word
                                deviceStats->sasStatistics.verifyTotalBytesProcessed.statisticValue = M_BytesTo4ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
                                break;
                            case 8://quad word
                                deviceStats->sasStatistics.verifyTotalBytesProcessed.statisticValue = M_BytesTo8ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7], tempLogBuf[iter + 8], tempLogBuf[iter + 9], tempLogBuf[iter + 10], tempLogBuf[iter + 11]);
                                break;
                            default://don't bother trying to read the data since it's in a more complicated format to read than we care to handle in this code right now
                                deviceStats->sasStatistics.verifyTotalBytesProcessed.isValueValid = false;
                                break;
                            }
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            break;
                        case 6://total uncorrected errors
                            deviceStats->sasStatistics.verifyTotalUncorrectedErrors.isSupported = true;
                            deviceStats->sasStatistics.verifyTotalUncorrectedErrors.isValueValid = true;
                            //check if thresholds supported, etc
                            deviceStats->sasStatistics.verifyTotalUncorrectedErrors.thresholdNotificationEnabled = tempLogBuf[iter + 2] & BIT4;//ETC bit
                            if (tempLogBuf[iter + 2] & BIT4)
                            {
                                switch ((tempLogBuf[iter + 2] & (BIT2 | BIT3)) >> 2)
                                {
                                case 3:
                                    deviceStats->sasStatistics.verifyTotalUncorrectedErrors.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_GREATER;
                                    break;
                                case 2:
                                    deviceStats->sasStatistics.verifyTotalUncorrectedErrors.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL;
                                    break;
                                case 1:
                                    deviceStats->sasStatistics.verifyTotalUncorrectedErrors.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL;
                                    break;
                                case 0:
                                default:
                                    deviceStats->sasStatistics.verifyTotalUncorrectedErrors.threshType = THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE;
                                    break;
                                }
                            }
                            switch (parameterLength)
                            {
                            case 1://single byte
                                deviceStats->sasStatistics.verifyTotalUncorrectedErrors.statisticValue = tempLogBuf[iter + 4];
                                break;
                            case 2://word
                                deviceStats->sasStatistics.verifyTotalUncorrectedErrors.statisticValue = M_BytesTo2ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5]);
                                break;
                            case 4://double word
                                deviceStats->sasStatistics.verifyTotalUncorrectedErrors.statisticValue = M_BytesTo4ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
                                break;
                            case 8://quad word
                                deviceStats->sasStatistics.verifyTotalUncorrectedErrors.statisticValue = M_BytesTo8ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7], tempLogBuf[iter + 8], tempLogBuf[iter + 9], tempLogBuf[iter + 10], tempLogBuf[iter + 11]);
                                break;
                            default://don't bother trying to read the data since it's in a more complicated format to read than we care to handle in this code right now
                                deviceStats->sasStatistics.verifyTotalUncorrectedErrors.isValueValid = false;
                                break;
                            }
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            break;
                        default:
                            break;
                        }
                        if (parameterLength == 0)
                        {
                            break;
                        }
                    }
                    //thresholds
                    memset(tempLogBuf, 0, LEGACY_DRIVE_SEC_SIZE);
                    if (SUCCESS == scsi_Log_Sense_Cmd(device, false, LPC_THRESHOLD_VALUES, pageCode, subpageCode, 0x0000, tempLogBuf, LEGACY_DRIVE_SEC_SIZE))
                    {
                        uint16_t pageLength = M_BytesTo2ByteValue(tempLogBuf[2], tempLogBuf[3]);
                        uint8_t parameterLength = 0;
                        //loop through the data and gather the data from each parameter we care about getting.
                        for (uint16_t iter = 4; iter < pageLength && iter < LEGACY_DRIVE_SEC_SIZE; iter += (parameterLength + 4))
                        {
                            uint16_t parameterCode = M_BytesTo2ByteValue(tempLogBuf[iter], tempLogBuf[iter + 1]);
                            parameterLength = tempLogBuf[iter + 3];
                            switch (parameterCode)
                            {
                            case 0://Errors corrected without substantial delay
                                deviceStats->sasStatistics.verifyErrorsCorrectedWithoutSubstantialDelay.supportsNotification = true;
                                if (tempLogBuf[iter + 2] & BIT4)
                                {
                                    deviceStats->sasStatistics.verifyErrorsCorrectedWithoutSubstantialDelay.isThresholdValid = true;
                                    switch (parameterLength)
                                    {
                                    case 1://single byte
                                        deviceStats->sasStatistics.verifyErrorsCorrectedWithoutSubstantialDelay.threshold = tempLogBuf[iter + 4];
                                        break;
                                    case 2://word
                                        deviceStats->sasStatistics.verifyErrorsCorrectedWithoutSubstantialDelay.threshold = M_BytesTo2ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5]);
                                        break;
                                    case 4://double word
                                        deviceStats->sasStatistics.verifyErrorsCorrectedWithoutSubstantialDelay.threshold = M_BytesTo4ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
                                        break;
                                    case 8://quad word
                                        deviceStats->sasStatistics.verifyErrorsCorrectedWithoutSubstantialDelay.threshold = M_BytesTo8ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7], tempLogBuf[iter + 8], tempLogBuf[iter + 9], tempLogBuf[iter + 10], tempLogBuf[iter + 11]);
                                        break;
                                    default://don't bother trying to read the data since it's in a more complicated format to read than we care to handle in this code right now
                                        deviceStats->sasStatistics.verifyErrorsCorrectedWithoutSubstantialDelay.isThresholdValid = false;
                                        break;
                                    }
                                    scsi_Threshold_Comparison(&deviceStats->sasStatistics.verifyErrorsCorrectedWithoutSubstantialDelay);
                                }
                                break;
                            case 1://Errors corrected with possible delays
                                deviceStats->sasStatistics.verifyErrorsCorrectedWithPossibleDelays.supportsNotification = true;
                                if (tempLogBuf[iter + 2] & BIT4)
                                {
                                    deviceStats->sasStatistics.verifyErrorsCorrectedWithPossibleDelays.isThresholdValid = true;
                                    switch (parameterLength)
                                    {
                                    case 1://single byte
                                        deviceStats->sasStatistics.verifyErrorsCorrectedWithPossibleDelays.threshold = tempLogBuf[iter + 4];
                                        break;
                                    case 2://word
                                        deviceStats->sasStatistics.verifyErrorsCorrectedWithPossibleDelays.threshold = M_BytesTo2ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5]);
                                        break;
                                    case 4://double word
                                        deviceStats->sasStatistics.verifyErrorsCorrectedWithPossibleDelays.threshold = M_BytesTo4ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
                                        break;
                                    case 8://quad word
                                        deviceStats->sasStatistics.verifyErrorsCorrectedWithPossibleDelays.threshold = M_BytesTo8ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7], tempLogBuf[iter + 8], tempLogBuf[iter + 9], tempLogBuf[iter + 10], tempLogBuf[iter + 11]);
                                        break;
                                    default://don't bother trying to read the data since it's in a more complicated format to read than we care to handle in this code right now
                                        deviceStats->sasStatistics.verifyErrorsCorrectedWithPossibleDelays.isThresholdValid = false;
                                        break;
                                    }
                                    scsi_Threshold_Comparison(&deviceStats->sasStatistics.verifyErrorsCorrectedWithPossibleDelays);
                                }
                                break;
                            case 2://total
                                deviceStats->sasStatistics.verifyTotal.supportsNotification = true;
                                if (tempLogBuf[iter + 2] & BIT4)
                                {
                                    deviceStats->sasStatistics.verifyTotal.isThresholdValid = true;
                                    switch (parameterLength)
                                    {
                                    case 1://single byte
                                        deviceStats->sasStatistics.verifyTotal.threshold = tempLogBuf[iter + 4];
                                        break;
                                    case 2://word
                                        deviceStats->sasStatistics.verifyTotal.threshold = M_BytesTo2ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5]);
                                        break;
                                    case 4://double word
                                        deviceStats->sasStatistics.verifyTotal.threshold = M_BytesTo4ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
                                        break;
                                    case 8://quad word
                                        deviceStats->sasStatistics.verifyTotal.threshold = M_BytesTo8ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7], tempLogBuf[iter + 8], tempLogBuf[iter + 9], tempLogBuf[iter + 10], tempLogBuf[iter + 11]);
                                        break;
                                    default://don't bother trying to read the data since it's in a more complicated format to read than we care to handle in this code right now
                                        deviceStats->sasStatistics.verifyTotal.isThresholdValid = false;
                                        break;
                                    }
                                    scsi_Threshold_Comparison(&deviceStats->sasStatistics.verifyTotal);
                                }
                                break;
                            case 3://total errors corrected
                                deviceStats->sasStatistics.verifyErrorsCorrected.supportsNotification = true;
                                if (tempLogBuf[iter + 2] & BIT4)
                                {
                                    deviceStats->sasStatistics.verifyErrorsCorrected.isThresholdValid = true;
                                    switch (parameterLength)
                                    {
                                    case 1://single byte
                                        deviceStats->sasStatistics.verifyErrorsCorrected.threshold = tempLogBuf[iter + 4];
                                        break;
                                    case 2://word
                                        deviceStats->sasStatistics.verifyErrorsCorrected.threshold = M_BytesTo2ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5]);
                                        break;
                                    case 4://double word
                                        deviceStats->sasStatistics.verifyErrorsCorrected.threshold = M_BytesTo4ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
                                        break;
                                    case 8://quad word
                                        deviceStats->sasStatistics.verifyErrorsCorrected.threshold = M_BytesTo8ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7], tempLogBuf[iter + 8], tempLogBuf[iter + 9], tempLogBuf[iter + 10], tempLogBuf[iter + 11]);
                                        break;
                                    default://don't bother trying to read the data since it's in a more complicated format to read than we care to handle in this code right now
                                        deviceStats->sasStatistics.verifyErrorsCorrected.isThresholdValid = false;
                                        break;
                                    }
                                    scsi_Threshold_Comparison(&deviceStats->sasStatistics.verifyErrorsCorrected);
                                }
                                break;
                            case 4://total times correction algorithm processed
                                deviceStats->sasStatistics.verifyTotalTimeCorrectionAlgorithmProcessed.supportsNotification = true;
                                if (tempLogBuf[iter + 2] & BIT4)
                                {
                                    deviceStats->sasStatistics.verifyTotalTimeCorrectionAlgorithmProcessed.isThresholdValid = true;
                                    switch (parameterLength)
                                    {
                                    case 1://single byte
                                        deviceStats->sasStatistics.verifyTotalTimeCorrectionAlgorithmProcessed.threshold = tempLogBuf[iter + 4];
                                        break;
                                    case 2://word
                                        deviceStats->sasStatistics.verifyTotalTimeCorrectionAlgorithmProcessed.threshold = M_BytesTo2ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5]);
                                        break;
                                    case 4://double word
                                        deviceStats->sasStatistics.verifyTotalTimeCorrectionAlgorithmProcessed.threshold = M_BytesTo4ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
                                        break;
                                    case 8://quad word
                                        deviceStats->sasStatistics.verifyTotalTimeCorrectionAlgorithmProcessed.threshold = M_BytesTo8ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7], tempLogBuf[iter + 8], tempLogBuf[iter + 9], tempLogBuf[iter + 10], tempLogBuf[iter + 11]);
                                        break;
                                    default://don't bother trying to read the data since it's in a more complicated format to read than we care to handle in this code right now
                                        deviceStats->sasStatistics.verifyTotalTimeCorrectionAlgorithmProcessed.isThresholdValid = false;
                                        break;
                                    }
                                    scsi_Threshold_Comparison(&deviceStats->sasStatistics.verifyTotalTimeCorrectionAlgorithmProcessed);
                                }
                                break;
                            case 5://total bytes processed
                                deviceStats->sasStatistics.verifyTotalBytesProcessed.supportsNotification = true;
                                if (tempLogBuf[iter + 2] & BIT4)
                                {
                                    deviceStats->sasStatistics.verifyTotalBytesProcessed.isThresholdValid = true;
                                    switch (parameterLength)
                                    {
                                    case 1://single byte
                                        deviceStats->sasStatistics.verifyTotalBytesProcessed.threshold = tempLogBuf[iter + 4];
                                        break;
                                    case 2://word
                                        deviceStats->sasStatistics.verifyTotalBytesProcessed.threshold = M_BytesTo2ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5]);
                                        break;
                                    case 4://double word
                                        deviceStats->sasStatistics.verifyTotalBytesProcessed.threshold = M_BytesTo4ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
                                        break;
                                    case 8://quad word
                                        deviceStats->sasStatistics.verifyTotalBytesProcessed.threshold = M_BytesTo8ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7], tempLogBuf[iter + 8], tempLogBuf[iter + 9], tempLogBuf[iter + 10], tempLogBuf[iter + 11]);
                                        break;
                                    default://don't bother trying to read the data since it's in a more complicated format to read than we care to handle in this code right now
                                        deviceStats->sasStatistics.verifyTotalBytesProcessed.isThresholdValid = false;
                                        break;
                                    }
                                    scsi_Threshold_Comparison(&deviceStats->sasStatistics.verifyTotalBytesProcessed);
                                }
                                break;
                            case 6://total uncorrected errors
                                deviceStats->sasStatistics.verifyTotalUncorrectedErrors.supportsNotification = true;
                                if (tempLogBuf[iter + 2] & BIT4)
                                {
                                    deviceStats->sasStatistics.verifyTotalUncorrectedErrors.isThresholdValid = true;
                                    switch (parameterLength)
                                    {
                                    case 1://single byte
                                        deviceStats->sasStatistics.verifyTotalUncorrectedErrors.threshold = tempLogBuf[iter + 4];
                                        break;
                                    case 2://word
                                        deviceStats->sasStatistics.verifyTotalUncorrectedErrors.threshold = M_BytesTo2ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5]);
                                        break;
                                    case 4://double word
                                        deviceStats->sasStatistics.verifyTotalUncorrectedErrors.threshold = M_BytesTo4ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
                                        break;
                                    case 8://quad word
                                        deviceStats->sasStatistics.verifyTotalUncorrectedErrors.threshold = M_BytesTo8ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7], tempLogBuf[iter + 8], tempLogBuf[iter + 9], tempLogBuf[iter + 10], tempLogBuf[iter + 11]);
                                        break;
                                    default://don't bother trying to read the data since it's in a more complicated format to read than we care to handle in this code right now
                                        deviceStats->sasStatistics.verifyTotalUncorrectedErrors.isThresholdValid = false;
                                        break;
                                    }
                                    scsi_Threshold_Comparison(&deviceStats->sasStatistics.verifyTotalUncorrectedErrors);
                                }
                                break;
                            default:
                                break;
                            }
                            if (parameterLength == 0)
                            {
                                break;
                            }
                        }
                    }
                }
            }
            break;
        case LP_NON_MEDIUM_ERROR:
            if (subpageCode == 0)
            {
                memset(tempLogBuf, 0, LEGACY_DRIVE_SEC_SIZE);
                if (SUCCESS == scsi_Log_Sense_Cmd(device, false, LPC_CUMULATIVE_VALUES, pageCode, subpageCode, 0x0000, tempLogBuf, LEGACY_DRIVE_SEC_SIZE))
                {
                    deviceStats->sasStatistics.nonMediumErrorSupported = true;
                    uint16_t pageLength = M_BytesTo2ByteValue(tempLogBuf[2], tempLogBuf[3]);
                    uint8_t parameterLength = 0;
                    //loop through the data and gather the data from each parameter we care about getting.
                    for (uint16_t iter = 4; iter < pageLength && iter < LEGACY_DRIVE_SEC_SIZE; iter += (parameterLength + 4))
                    {
                        uint16_t parameterCode = M_BytesTo2ByteValue(tempLogBuf[iter], tempLogBuf[iter + 1]);
                        parameterLength = tempLogBuf[iter + 3];
                        switch (parameterCode)
                        {
                        case 0://non medium error count
                            deviceStats->sasStatistics.nonMediumErrorCount.isSupported = true;
                            deviceStats->sasStatistics.nonMediumErrorCount.isValueValid = true;
                            deviceStats->sasStatistics.nonMediumErrorCount.thresholdNotificationEnabled = tempLogBuf[iter + 2] & BIT4;//ETC bit
                            if (tempLogBuf[iter + 2] & BIT4)
                            {
                                switch ((tempLogBuf[iter + 2] & (BIT2 | BIT3)) >> 2)
                                {
                                case 3:
                                    deviceStats->sasStatistics.nonMediumErrorCount.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_GREATER;
                                    break;
                                case 2:
                                    deviceStats->sasStatistics.nonMediumErrorCount.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL;
                                    break;
                                case 1:
                                    deviceStats->sasStatistics.nonMediumErrorCount.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL;
                                    break;
                                case 0:
                                default:
                                    deviceStats->sasStatistics.nonMediumErrorCount.threshType = THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE;
                                    break;
                                }
                            }
                            switch (parameterLength)
                            {
                            case 1://single byte
                                deviceStats->sasStatistics.nonMediumErrorCount.statisticValue = tempLogBuf[iter + 4];
                                break;
                            case 2://word
                                deviceStats->sasStatistics.nonMediumErrorCount.statisticValue = M_BytesTo2ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5]);
                                break;
                            case 4://double word
                                deviceStats->sasStatistics.nonMediumErrorCount.statisticValue = M_BytesTo4ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
                                break;
                            case 8://quad word
                                deviceStats->sasStatistics.nonMediumErrorCount.statisticValue = M_BytesTo8ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7], tempLogBuf[iter + 8], tempLogBuf[iter + 9], tempLogBuf[iter + 10], tempLogBuf[iter + 11]);
                                break;
                            default://don't bother trying to read the data since it's in a more complicated format to read than we care to handle in this code right now
                                deviceStats->sasStatistics.nonMediumErrorCount.isValueValid = false;
                                break;
                            }
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            break;
                        default:
                            break;
                        }
                        if (parameterLength == 0)
                        {
                            break;
                        }
                    }
                    //thresholds
                    memset(tempLogBuf, 0, LEGACY_DRIVE_SEC_SIZE);
                    if (SUCCESS == scsi_Log_Sense_Cmd(device, false, LPC_THRESHOLD_VALUES, pageCode, subpageCode, 0x0000, tempLogBuf, LEGACY_DRIVE_SEC_SIZE))
                    {
                        uint16_t pageLength = M_BytesTo2ByteValue(tempLogBuf[2], tempLogBuf[3]);
                        uint8_t parameterLength = 0;
                        //loop through the data and gather the data from each parameter we care about getting.
                        for (uint16_t iter = 4; iter < pageLength && iter < LEGACY_DRIVE_SEC_SIZE; iter += (parameterLength + 4))
                        {
                            uint16_t parameterCode = M_BytesTo2ByteValue(tempLogBuf[iter], tempLogBuf[iter + 1]);
                            parameterLength = tempLogBuf[iter + 3];
                            switch (parameterCode)
                            {
                            case 0://non medium error count
                                deviceStats->sasStatistics.nonMediumErrorCount.supportsNotification = true;
                                if (tempLogBuf[iter + 2] & BIT4)
                                {
                                    deviceStats->sasStatistics.nonMediumErrorCount.isThresholdValid = true;
                                    switch (parameterLength)
                                    {
                                    case 1://single byte
                                        deviceStats->sasStatistics.nonMediumErrorCount.threshold = tempLogBuf[iter + 4];
                                        break;
                                    case 2://word
                                        deviceStats->sasStatistics.nonMediumErrorCount.threshold = M_BytesTo2ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5]);
                                        break;
                                    case 4://double word
                                        deviceStats->sasStatistics.nonMediumErrorCount.threshold = M_BytesTo4ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
                                        break;
                                    case 8://quad word
                                        deviceStats->sasStatistics.nonMediumErrorCount.threshold = M_BytesTo8ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7], tempLogBuf[iter + 8], tempLogBuf[iter + 9], tempLogBuf[iter + 10], tempLogBuf[iter + 11]);
                                        break;
                                    default://don't bother trying to read the data since it's in a more complicated format to read than we care to handle in this code right now
                                        deviceStats->sasStatistics.nonMediumErrorCount.isThresholdValid = false;
                                        break;
                                    }
                                    scsi_Threshold_Comparison(&deviceStats->sasStatistics.nonMediumErrorCount);
                                }
                                break;
                            default:
                                break;
                            }
                            if (parameterLength == 0)
                            {
                                break;
                            }
                        }
                    }
                }
            }
            break;
        case LP_FORMAT_STATUS_LOG_PAGE:
            if (subpageCode == 0)
            {
                memset(tempLogBuf, 0, LEGACY_DRIVE_SEC_SIZE);
                if (SUCCESS == scsi_Log_Sense_Cmd(device, false, LPC_CUMULATIVE_VALUES, pageCode, subpageCode, 0x0001, tempLogBuf, LEGACY_DRIVE_SEC_SIZE))
                {
                    deviceStats->sasStatistics.formatStatusSupported = true;
                    uint16_t pageLength = M_BytesTo2ByteValue(tempLogBuf[2], tempLogBuf[3]);
                    uint8_t parameterLength = 0;
                    //loop through the data and gather the data from each parameter we care about getting.
                    for (uint16_t iter = 4; iter < pageLength && iter < LEGACY_DRIVE_SEC_SIZE; iter += (parameterLength + 4))
                    {
                        uint16_t parameterCode = M_BytesTo2ByteValue(tempLogBuf[iter], tempLogBuf[iter + 1]);
                        parameterLength = tempLogBuf[iter + 3];
                        switch (parameterCode)
                        {
                        case 0://format data out
                            break;
                        case 1://grown defects during certification
                            deviceStats->sasStatistics.grownDefectsDuringCertification.isSupported = true;
                            deviceStats->sasStatistics.grownDefectsDuringCertification.isValueValid = true;
                            deviceStats->sasStatistics.grownDefectsDuringCertification.thresholdNotificationEnabled = tempLogBuf[iter + 2] & BIT4;//ETC bit
                            if (tempLogBuf[iter + 2] & BIT4)
                            {
                                switch ((tempLogBuf[iter + 2] & (BIT2 | BIT3)) >> 2)
                                {
                                case 3:
                                    deviceStats->sasStatistics.grownDefectsDuringCertification.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_GREATER;
                                    break;
                                case 2:
                                    deviceStats->sasStatistics.grownDefectsDuringCertification.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL;
                                    break;
                                case 1:
                                    deviceStats->sasStatistics.grownDefectsDuringCertification.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL;
                                    break;
                                case 0:
                                default:
                                    deviceStats->sasStatistics.grownDefectsDuringCertification.threshType = THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE;
                                    break;
                                }
                            }
                            switch (parameterLength)
                            {
                            case 1://single byte
                                deviceStats->sasStatistics.grownDefectsDuringCertification.statisticValue = tempLogBuf[iter + 4];
                                break;
                            case 2://word
                                deviceStats->sasStatistics.grownDefectsDuringCertification.statisticValue = M_BytesTo2ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5]);
                                break;
                            case 4://double word
                                deviceStats->sasStatistics.grownDefectsDuringCertification.statisticValue = M_BytesTo4ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
                                break;
                            case 8://quad word
                                deviceStats->sasStatistics.grownDefectsDuringCertification.statisticValue = M_BytesTo8ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7], tempLogBuf[iter + 8], tempLogBuf[iter + 9], tempLogBuf[iter + 10], tempLogBuf[iter + 11]);
                                break;
                            default://don't bother trying to read the data since it's in a more complicated format to read than we care to handle in this code right now
                                deviceStats->sasStatistics.grownDefectsDuringCertification.isValueValid = false;
                                break;
                            }
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            break;
                        case 2://total blocks reassigned during format
                            deviceStats->sasStatistics.totalBlocksReassignedDuringFormat.isSupported = true;
                            deviceStats->sasStatistics.totalBlocksReassignedDuringFormat.isValueValid = true;
                            deviceStats->sasStatistics.totalBlocksReassignedDuringFormat.thresholdNotificationEnabled = tempLogBuf[iter + 2] & BIT4;//ETC bit
                            if (tempLogBuf[iter + 2] & BIT4)
                            {
                                switch ((tempLogBuf[iter + 2] & (BIT2 | BIT3)) >> 2)
                                {
                                case 3:
                                    deviceStats->sasStatistics.totalBlocksReassignedDuringFormat.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_GREATER;
                                    break;
                                case 2:
                                    deviceStats->sasStatistics.totalBlocksReassignedDuringFormat.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL;
                                    break;
                                case 1:
                                    deviceStats->sasStatistics.totalBlocksReassignedDuringFormat.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL;
                                    break;
                                case 0:
                                default:
                                    deviceStats->sasStatistics.totalBlocksReassignedDuringFormat.threshType = THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE;
                                    break;
                                }
                            }
                            switch (parameterLength)
                            {
                            case 1://single byte
                                deviceStats->sasStatistics.totalBlocksReassignedDuringFormat.statisticValue = tempLogBuf[iter + 4];
                                break;
                            case 2://word
                                deviceStats->sasStatistics.totalBlocksReassignedDuringFormat.statisticValue = M_BytesTo2ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5]);
                                break;
                            case 4://double word
                                deviceStats->sasStatistics.totalBlocksReassignedDuringFormat.statisticValue = M_BytesTo4ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
                                break;
                            case 8://quad word
                                deviceStats->sasStatistics.totalBlocksReassignedDuringFormat.statisticValue = M_BytesTo8ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7], tempLogBuf[iter + 8], tempLogBuf[iter + 9], tempLogBuf[iter + 10], tempLogBuf[iter + 11]);
                                break;
                            default://don't bother trying to read the data since it's in a more complicated format to read than we care to handle in this code right now
                                deviceStats->sasStatistics.totalBlocksReassignedDuringFormat.isValueValid = false;
                                break;
                            }
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            break;
                        case 3://total new blocks reassigned
                            deviceStats->sasStatistics.totalNewBlocksReassigned.isSupported = true;
                            deviceStats->sasStatistics.totalNewBlocksReassigned.isValueValid = true;
                            deviceStats->sasStatistics.totalNewBlocksReassigned.thresholdNotificationEnabled = tempLogBuf[iter + 2] & BIT4;//ETC bit
                            if (tempLogBuf[iter + 2] & BIT4)
                            {
                                switch ((tempLogBuf[iter + 2] & (BIT2 | BIT3)) >> 2)
                                {
                                case 3:
                                    deviceStats->sasStatistics.totalNewBlocksReassigned.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_GREATER;
                                    break;
                                case 2:
                                    deviceStats->sasStatistics.totalNewBlocksReassigned.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL;
                                    break;
                                case 1:
                                    deviceStats->sasStatistics.totalNewBlocksReassigned.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL;
                                    break;
                                case 0:
                                default:
                                    deviceStats->sasStatistics.totalNewBlocksReassigned.threshType = THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE;
                                    break;
                                }
                            }
                            switch (parameterLength)
                            {
                            case 1://single byte
                                deviceStats->sasStatistics.totalNewBlocksReassigned.statisticValue = tempLogBuf[iter + 4];
                                break;
                            case 2://word
                                deviceStats->sasStatistics.totalNewBlocksReassigned.statisticValue = M_BytesTo2ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5]);
                                break;
                            case 4://double word
                                deviceStats->sasStatistics.totalNewBlocksReassigned.statisticValue = M_BytesTo4ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
                                break;
                            case 8://quad word
                                deviceStats->sasStatistics.totalNewBlocksReassigned.statisticValue = M_BytesTo8ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7], tempLogBuf[iter + 8], tempLogBuf[iter + 9], tempLogBuf[iter + 10], tempLogBuf[iter + 11]);
                                break;
                            default://don't bother trying to read the data since it's in a more complicated format to read than we care to handle in this code right now
                                deviceStats->sasStatistics.totalNewBlocksReassigned.isValueValid = false;
                                break;
                            }
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            break;
                        case 4://power on minutes since last format
                            deviceStats->sasStatistics.powerOnMinutesSinceFormat.isSupported = true;
                            deviceStats->sasStatistics.powerOnMinutesSinceFormat.isValueValid = true;
                            deviceStats->sasStatistics.powerOnMinutesSinceFormat.thresholdNotificationEnabled = tempLogBuf[iter + 2] & BIT4;//ETC bit
                            if (tempLogBuf[iter + 2] & BIT4)
                            {
                                switch ((tempLogBuf[iter + 2] & (BIT2 | BIT3)) >> 2)
                                {
                                case 3:
                                    deviceStats->sasStatistics.powerOnMinutesSinceFormat.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_GREATER;
                                    break;
                                case 2:
                                    deviceStats->sasStatistics.powerOnMinutesSinceFormat.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL;
                                    break;
                                case 1:
                                    deviceStats->sasStatistics.powerOnMinutesSinceFormat.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL;
                                    break;
                                case 0:
                                default:
                                    deviceStats->sasStatistics.powerOnMinutesSinceFormat.threshType = THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE;
                                    break;
                                }
                            }
                            switch (parameterLength)
                            {
                            case 1://single byte
                                deviceStats->sasStatistics.powerOnMinutesSinceFormat.statisticValue = tempLogBuf[iter + 4];
                                break;
                            case 2://word
                                deviceStats->sasStatistics.powerOnMinutesSinceFormat.statisticValue = M_BytesTo2ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5]);
                                break;
                            case 4://double word
                                deviceStats->sasStatistics.powerOnMinutesSinceFormat.statisticValue = M_BytesTo4ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
                                break;
                            case 8://quad word
                                deviceStats->sasStatistics.powerOnMinutesSinceFormat.statisticValue = M_BytesTo8ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7], tempLogBuf[iter + 8], tempLogBuf[iter + 9], tempLogBuf[iter + 10], tempLogBuf[iter + 11]);
                                break;
                            default://don't bother trying to read the data since it's in a more complicated format to read than we care to handle in this code right now
                                deviceStats->sasStatistics.powerOnMinutesSinceFormat.isValueValid = false;
                                break;
                            }
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            break;
                        default:
                            break;
                        }
                        if (parameterLength == 0)
                        {
                            break;
                        }
                    }
                    //thresholds
                    memset(tempLogBuf, 0, LEGACY_DRIVE_SEC_SIZE);
                    if (SUCCESS == scsi_Log_Sense_Cmd(device, false, LPC_THRESHOLD_VALUES, pageCode, subpageCode, 0x0001, tempLogBuf, LEGACY_DRIVE_SEC_SIZE))
                    {
                        uint16_t pageLength = M_BytesTo2ByteValue(tempLogBuf[2], tempLogBuf[3]);
                        uint8_t parameterLength = 0;
                        //loop through the data and gather the data from each parameter we care about getting.
                        for (uint16_t iter = 4; iter < pageLength && iter < LEGACY_DRIVE_SEC_SIZE; iter += (parameterLength + 4))
                        {
                            uint16_t parameterCode = M_BytesTo2ByteValue(tempLogBuf[iter], tempLogBuf[iter + 1]);
                            parameterLength = tempLogBuf[iter + 3];
                            switch (parameterCode)
                            {
                            case 0://format data out
                                break;
                            case 1://grown defects during certification
                                deviceStats->sasStatistics.grownDefectsDuringCertification.supportsNotification = true;
                                if (tempLogBuf[iter + 2] & BIT4)
                                {
                                    deviceStats->sasStatistics.grownDefectsDuringCertification.isThresholdValid = true;
                                    switch (parameterLength)
                                    {
                                    case 1://single byte
                                        deviceStats->sasStatistics.grownDefectsDuringCertification.threshold = tempLogBuf[iter + 4];
                                        break;
                                    case 2://word
                                        deviceStats->sasStatistics.grownDefectsDuringCertification.threshold = M_BytesTo2ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5]);
                                        break;
                                    case 4://double word
                                        deviceStats->sasStatistics.grownDefectsDuringCertification.threshold = M_BytesTo4ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
                                        break;
                                    case 8://quad word
                                        deviceStats->sasStatistics.grownDefectsDuringCertification.threshold = M_BytesTo8ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7], tempLogBuf[iter + 8], tempLogBuf[iter + 9], tempLogBuf[iter + 10], tempLogBuf[iter + 11]);
                                        break;
                                    default://don't bother trying to read the data since it's in a more complicated format to read than we care to handle in this code right now
                                        deviceStats->sasStatistics.grownDefectsDuringCertification.isThresholdValid = false;
                                        break;
                                    }
                                    scsi_Threshold_Comparison(&deviceStats->sasStatistics.grownDefectsDuringCertification);
                                }
                                break;
                            case 2://total blocks reassigned during format
                                deviceStats->sasStatistics.totalBlocksReassignedDuringFormat.supportsNotification = true;
                                if (tempLogBuf[iter + 2] & BIT4)
                                {
                                    deviceStats->sasStatistics.totalBlocksReassignedDuringFormat.isThresholdValid = true;
                                    switch (parameterLength)
                                    {
                                    case 1://single byte
                                        deviceStats->sasStatistics.totalBlocksReassignedDuringFormat.threshold = tempLogBuf[iter + 4];
                                        break;
                                    case 2://word
                                        deviceStats->sasStatistics.totalBlocksReassignedDuringFormat.threshold = M_BytesTo2ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5]);
                                        break;
                                    case 4://double word
                                        deviceStats->sasStatistics.totalBlocksReassignedDuringFormat.threshold = M_BytesTo4ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
                                        break;
                                    case 8://quad word
                                        deviceStats->sasStatistics.totalBlocksReassignedDuringFormat.threshold = M_BytesTo8ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7], tempLogBuf[iter + 8], tempLogBuf[iter + 9], tempLogBuf[iter + 10], tempLogBuf[iter + 11]);
                                        break;
                                    default://don't bother trying to read the data since it's in a more complicated format to read than we care to handle in this code right now
                                        deviceStats->sasStatistics.totalBlocksReassignedDuringFormat.isThresholdValid = false;
                                        break;
                                    }
                                    scsi_Threshold_Comparison(&deviceStats->sasStatistics.totalBlocksReassignedDuringFormat);
                                }
                                break;
                            case 3://total new blocks reassigned
                                deviceStats->sasStatistics.totalNewBlocksReassigned.supportsNotification = true;
                                if (tempLogBuf[iter + 2] & BIT4)
                                {
                                    deviceStats->sasStatistics.totalNewBlocksReassigned.isThresholdValid = true;
                                    switch (parameterLength)
                                    {
                                    case 1://single byte
                                        deviceStats->sasStatistics.totalNewBlocksReassigned.threshold = tempLogBuf[iter + 4];
                                        break;
                                    case 2://word
                                        deviceStats->sasStatistics.totalNewBlocksReassigned.threshold = M_BytesTo2ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5]);
                                        break;
                                    case 4://double word
                                        deviceStats->sasStatistics.totalNewBlocksReassigned.threshold = M_BytesTo4ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
                                        break;
                                    case 8://quad word
                                        deviceStats->sasStatistics.totalNewBlocksReassigned.threshold = M_BytesTo8ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7], tempLogBuf[iter + 8], tempLogBuf[iter + 9], tempLogBuf[iter + 10], tempLogBuf[iter + 11]);
                                        break;
                                    default://don't bother trying to read the data since it's in a more complicated format to read than we care to handle in this code right now
                                        deviceStats->sasStatistics.totalNewBlocksReassigned.isThresholdValid = false;
                                        break;
                                    }
                                    scsi_Threshold_Comparison(&deviceStats->sasStatistics.totalNewBlocksReassigned);
                                }
                                break;
                            case 4://power on minutes since format
                                deviceStats->sasStatistics.powerOnMinutesSinceFormat.supportsNotification = true;
                                if (tempLogBuf[iter + 2] & BIT4)
                                {
                                    deviceStats->sasStatistics.powerOnMinutesSinceFormat.isThresholdValid = true;
                                    switch (parameterLength)
                                    {
                                    case 1://single byte
                                        deviceStats->sasStatistics.powerOnMinutesSinceFormat.threshold = tempLogBuf[iter + 4];
                                        break;
                                    case 2://word
                                        deviceStats->sasStatistics.powerOnMinutesSinceFormat.threshold = M_BytesTo2ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5]);
                                        break;
                                    case 4://double word
                                        deviceStats->sasStatistics.powerOnMinutesSinceFormat.threshold = M_BytesTo4ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
                                        break;
                                    case 8://quad word
                                        deviceStats->sasStatistics.powerOnMinutesSinceFormat.threshold = M_BytesTo8ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7], tempLogBuf[iter + 8], tempLogBuf[iter + 9], tempLogBuf[iter + 10], tempLogBuf[iter + 11]);
                                        break;
                                    default://don't bother trying to read the data since it's in a more complicated format to read than we care to handle in this code right now
                                        deviceStats->sasStatistics.powerOnMinutesSinceFormat.isThresholdValid = false;
                                        break;
                                    }
                                    scsi_Threshold_Comparison(&deviceStats->sasStatistics.powerOnMinutesSinceFormat);
                                }
                                break;
                            default:
                                break;
                            }
                            if (parameterLength == 0)
                            {
                                break;
                            }
                        }
                    }
                }
            }
            break;
        case LP_LOGICAL_BLOCK_PROVISIONING:
            if (subpageCode == 0)
            {
                memset(tempLogBuf, 0, LEGACY_DRIVE_SEC_SIZE);
                if (SUCCESS == scsi_Log_Sense_Cmd(device, false, LPC_CUMULATIVE_VALUES, pageCode, subpageCode, 0x0001, tempLogBuf, LEGACY_DRIVE_SEC_SIZE))
                {
                    deviceStats->sasStatistics.logicalBlockProvisioningSupported = true;
                    uint16_t pageLength = M_BytesTo2ByteValue(tempLogBuf[2], tempLogBuf[3]);
                    uint8_t parameterLength = 0;
                    //loop through the data and gather the data from each parameter we care about getting.
                    for (uint16_t iter = 4; iter < pageLength && iter < LEGACY_DRIVE_SEC_SIZE; iter += (parameterLength + 4))
                    {
                        uint16_t parameterCode = M_BytesTo2ByteValue(tempLogBuf[iter], tempLogBuf[iter + 1]);
                        parameterLength = tempLogBuf[iter + 3];
                        switch (parameterCode)
                        {
                        case 1://available LBA Mapping Resource Count
                            deviceStats->sasStatistics.availableLBAMappingresourceCount.isSupported = true;
                            deviceStats->sasStatistics.availableLBAMappingresourceCount.isValueValid = true;
                            deviceStats->sasStatistics.availableLBAMappingresourceCount.thresholdNotificationEnabled = tempLogBuf[iter + 2] & BIT4;//ETC bit
                            if (tempLogBuf[iter + 2] & BIT4)
                            {
                                switch ((tempLogBuf[iter + 2] & (BIT2 | BIT3)) >> 2)
                                {
                                case 3:
                                    deviceStats->sasStatistics.availableLBAMappingresourceCount.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_GREATER;
                                    break;
                                case 2:
                                    deviceStats->sasStatistics.availableLBAMappingresourceCount.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL;
                                    break;
                                case 1:
                                    deviceStats->sasStatistics.availableLBAMappingresourceCount.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL;
                                    break;
                                case 0:
                                default:
                                    deviceStats->sasStatistics.availableLBAMappingresourceCount.threshType = THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE;
                                    break;
                                }
                            }
                            deviceStats->sasStatistics.availableLBAMappingresourceCount.statisticValue = M_BytesTo4ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
                            //todo: ?should we |= the "scope" field to the statistic value? - TJE
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            break;
                        case 2://used LBA mapping resource count
                            deviceStats->sasStatistics.usedLBAMappingResourceCount.isSupported = true;
                            deviceStats->sasStatistics.usedLBAMappingResourceCount.isValueValid = true;
                            deviceStats->sasStatistics.usedLBAMappingResourceCount.thresholdNotificationEnabled = tempLogBuf[iter + 2] & BIT4;//ETC bit
                            if (tempLogBuf[iter + 2] & BIT4)
                            {
                                switch ((tempLogBuf[iter + 2] & (BIT2 | BIT3)) >> 2)
                                {
                                case 3:
                                    deviceStats->sasStatistics.usedLBAMappingResourceCount.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_GREATER;
                                    break;
                                case 2:
                                    deviceStats->sasStatistics.usedLBAMappingResourceCount.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL;
                                    break;
                                case 1:
                                    deviceStats->sasStatistics.usedLBAMappingResourceCount.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL;
                                    break;
                                case 0:
                                default:
                                    deviceStats->sasStatistics.usedLBAMappingResourceCount.threshType = THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE;
                                    break;
                                }
                            }
                            deviceStats->sasStatistics.usedLBAMappingResourceCount.statisticValue = M_BytesTo4ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
                            //todo: ?should we |= the "scope" field to the statistic value? - TJE
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            break;
                        case 3://available provisioning resource percentage
                            deviceStats->sasStatistics.availableProvisioningResourcePercentage.isSupported = true;
                            deviceStats->sasStatistics.availableProvisioningResourcePercentage.isValueValid = true;
                            deviceStats->sasStatistics.availableProvisioningResourcePercentage.thresholdNotificationEnabled = tempLogBuf[iter + 2] & BIT4;//ETC bit
                            if (tempLogBuf[iter + 2] & BIT4)
                            {
                                switch ((tempLogBuf[iter + 2] & (BIT2 | BIT3)) >> 2)
                                {
                                case 3:
                                    deviceStats->sasStatistics.availableProvisioningResourcePercentage.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_GREATER;
                                    break;
                                case 2:
                                    deviceStats->sasStatistics.availableProvisioningResourcePercentage.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL;
                                    break;
                                case 1:
                                    deviceStats->sasStatistics.availableProvisioningResourcePercentage.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL;
                                    break;
                                case 0:
                                default:
                                    deviceStats->sasStatistics.availableProvisioningResourcePercentage.threshType = THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE;
                                    break;
                                }
                            }
                            deviceStats->sasStatistics.availableProvisioningResourcePercentage.statisticValue = M_BytesTo2ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5]);
                            //todo: ?should we |= the "scope" field to the statistic value? - TJE
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            break;
                        case 0x100://de-duplicated LBA resource count
                            deviceStats->sasStatistics.deduplicatedLBAResourceCount.isSupported = true;
                            deviceStats->sasStatistics.deduplicatedLBAResourceCount.isValueValid = true;
                            deviceStats->sasStatistics.deduplicatedLBAResourceCount.thresholdNotificationEnabled = tempLogBuf[iter + 2] & BIT4;//ETC bit
                            if (tempLogBuf[iter + 2] & BIT4)
                            {
                                switch ((tempLogBuf[iter + 2] & (BIT2 | BIT3)) >> 2)
                                {
                                case 3:
                                    deviceStats->sasStatistics.deduplicatedLBAResourceCount.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_GREATER;
                                    break;
                                case 2:
                                    deviceStats->sasStatistics.deduplicatedLBAResourceCount.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL;
                                    break;
                                case 1:
                                    deviceStats->sasStatistics.deduplicatedLBAResourceCount.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL;
                                    break;
                                case 0:
                                default:
                                    deviceStats->sasStatistics.deduplicatedLBAResourceCount.threshType = THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE;
                                    break;
                                }
                            }
                            deviceStats->sasStatistics.deduplicatedLBAResourceCount.statisticValue = M_BytesTo4ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
                            //todo: ?should we |= the "scope" field to the statistic value? - TJE
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            break;
                        case 0x101://compressed LBA resource count
                            deviceStats->sasStatistics.compressedLBAResourceCount.isSupported = true;
                            deviceStats->sasStatistics.compressedLBAResourceCount.isValueValid = true;
                            deviceStats->sasStatistics.compressedLBAResourceCount.thresholdNotificationEnabled = tempLogBuf[iter + 2] & BIT4;//ETC bit
                            if (tempLogBuf[iter + 2] & BIT4)
                            {
                                switch ((tempLogBuf[iter + 2] & (BIT2 | BIT3)) >> 2)
                                {
                                case 3:
                                    deviceStats->sasStatistics.compressedLBAResourceCount.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_GREATER;
                                    break;
                                case 2:
                                    deviceStats->sasStatistics.compressedLBAResourceCount.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL;
                                    break;
                                case 1:
                                    deviceStats->sasStatistics.compressedLBAResourceCount.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL;
                                    break;
                                case 0:
                                default:
                                    deviceStats->sasStatistics.compressedLBAResourceCount.threshType = THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE;
                                    break;
                                }
                            }
                            deviceStats->sasStatistics.compressedLBAResourceCount.statisticValue = M_BytesTo4ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
                            //todo: ?should we |= the "scope" field to the statistic value? - TJE
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            break;
                        case 0x102://total efficiency LBA resource count
                            deviceStats->sasStatistics.totalEfficiencyLBAResourceCount.isSupported = true;
                            deviceStats->sasStatistics.totalEfficiencyLBAResourceCount.isValueValid = true;
                            deviceStats->sasStatistics.totalEfficiencyLBAResourceCount.thresholdNotificationEnabled = tempLogBuf[iter + 2] & BIT4;//ETC bit
                            if (tempLogBuf[iter + 2] & BIT4)
                            {
                                switch ((tempLogBuf[iter + 2] & (BIT2 | BIT3)) >> 2)
                                {
                                case 3:
                                    deviceStats->sasStatistics.totalEfficiencyLBAResourceCount.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_GREATER;
                                    break;
                                case 2:
                                    deviceStats->sasStatistics.totalEfficiencyLBAResourceCount.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL;
                                    break;
                                case 1:
                                    deviceStats->sasStatistics.totalEfficiencyLBAResourceCount.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL;
                                    break;
                                case 0:
                                default:
                                    deviceStats->sasStatistics.totalEfficiencyLBAResourceCount.threshType = THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE;
                                    break;
                                }
                            }
                            deviceStats->sasStatistics.totalEfficiencyLBAResourceCount.statisticValue = M_BytesTo4ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
                            //todo: ?should we |= the "scope" field to the statistic value? - TJE
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            break;
                        default:
                            break;
                        }
                        if (parameterLength == 0)
                        {
                            break;
                        }
                    }
                    //thresholds
                    memset(tempLogBuf, 0, LEGACY_DRIVE_SEC_SIZE);
                    if (SUCCESS == scsi_Log_Sense_Cmd(device, false, LPC_THRESHOLD_VALUES, pageCode, subpageCode, 0x0001, tempLogBuf, LEGACY_DRIVE_SEC_SIZE))
                    {
                        uint16_t pageLength = M_BytesTo2ByteValue(tempLogBuf[2], tempLogBuf[3]);
                        uint8_t parameterLength = 0;
                        //loop through the data and gather the data from each parameter we care about getting.
                        for (uint16_t iter = 4; iter < pageLength && iter < LEGACY_DRIVE_SEC_SIZE; iter += (parameterLength + 4))
                        {
                            uint16_t parameterCode = M_BytesTo2ByteValue(tempLogBuf[iter], tempLogBuf[iter + 1]);
                            parameterLength = tempLogBuf[iter + 3];
                            switch (parameterCode)
                            {
                            case 1://available LBA mapping resource count
                                deviceStats->sasStatistics.availableLBAMappingresourceCount.supportsNotification = true;
                                if (tempLogBuf[iter + 2] & BIT4)
                                {
                                    deviceStats->sasStatistics.availableLBAMappingresourceCount.isThresholdValid = true;
                                    deviceStats->sasStatistics.availableLBAMappingresourceCount.threshold = M_BytesTo4ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
                                    scsi_Threshold_Comparison(&deviceStats->sasStatistics.availableLBAMappingresourceCount);
                                }
                                break;
                            case 2://used LBA mapping resource count
                                deviceStats->sasStatistics.usedLBAMappingResourceCount.supportsNotification = true;
                                if (tempLogBuf[iter + 2] & BIT4)
                                {
                                    deviceStats->sasStatistics.usedLBAMappingResourceCount.isThresholdValid = true;
                                    deviceStats->sasStatistics.usedLBAMappingResourceCount.threshold = M_BytesTo4ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
                                    scsi_Threshold_Comparison(&deviceStats->sasStatistics.usedLBAMappingResourceCount);
                                }
                                break;
                            case 3://available provisinging resource percentage
                                deviceStats->sasStatistics.availableProvisioningResourcePercentage.supportsNotification = true;
                                if (tempLogBuf[iter + 2] & BIT4)
                                {
                                    deviceStats->sasStatistics.availableProvisioningResourcePercentage.isThresholdValid = true;
                                    deviceStats->sasStatistics.availableProvisioningResourcePercentage.threshold = M_BytesTo4ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
                                    scsi_Threshold_Comparison(&deviceStats->sasStatistics.availableProvisioningResourcePercentage);
                                }
                                break;
                            case 0x100://De-duplicated LBA resource count
                                deviceStats->sasStatistics.deduplicatedLBAResourceCount.supportsNotification = true;
                                if (tempLogBuf[iter + 2] & BIT4)
                                {
                                    deviceStats->sasStatistics.deduplicatedLBAResourceCount.isThresholdValid = true;
                                    deviceStats->sasStatistics.deduplicatedLBAResourceCount.threshold = M_BytesTo4ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
                                    scsi_Threshold_Comparison(&deviceStats->sasStatistics.deduplicatedLBAResourceCount);
                                }
                                break;
                            case 0x101://compressed LBA resource count
                                deviceStats->sasStatistics.compressedLBAResourceCount.supportsNotification = true;
                                if (tempLogBuf[iter + 2] & BIT4)
                                {
                                    deviceStats->sasStatistics.compressedLBAResourceCount.isThresholdValid = true;
                                    deviceStats->sasStatistics.compressedLBAResourceCount.threshold = M_BytesTo4ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
                                    scsi_Threshold_Comparison(&deviceStats->sasStatistics.compressedLBAResourceCount);
                                }
                                break;
                            case 0x102://total efficiency LBA resource count
                                deviceStats->sasStatistics.totalEfficiencyLBAResourceCount.supportsNotification = true;
                                if (tempLogBuf[iter + 2] & BIT4)
                                {
                                    deviceStats->sasStatistics.totalEfficiencyLBAResourceCount.isThresholdValid = true;
                                    deviceStats->sasStatistics.totalEfficiencyLBAResourceCount.threshold = M_BytesTo4ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
                                    scsi_Threshold_Comparison(&deviceStats->sasStatistics.totalEfficiencyLBAResourceCount);
                                }
                                break;
                            default:
                                break;
                            }
                            if (parameterLength == 0)
                            {
                                break;
                            }
                        }
                    }
                }
            }
            break;
        case LP_TEMPERATURE://also environmental reporting
            switch (subpageCode)
            {
            case 0://temperature
                memset(tempLogBuf, 0, LEGACY_DRIVE_SEC_SIZE);
                if (SUCCESS == scsi_Log_Sense_Cmd(device, false, LPC_CUMULATIVE_VALUES, pageCode, subpageCode, 0x0000, tempLogBuf, LEGACY_DRIVE_SEC_SIZE))
                {
                    deviceStats->sasStatistics.temperatureSupported = true;
                    uint16_t pageLength = M_BytesTo2ByteValue(tempLogBuf[2], tempLogBuf[3]);
                    uint8_t parameterLength = 0;
                    //loop through the data and gather the data from each parameter we care about getting.
                    for (uint16_t iter = 4; iter < pageLength && iter < LEGACY_DRIVE_SEC_SIZE; iter += (parameterLength + 4))
                    {
                        uint16_t parameterCode = M_BytesTo2ByteValue(tempLogBuf[iter], tempLogBuf[iter + 1]);
                        parameterLength = tempLogBuf[iter + 3];
                        switch (parameterCode)
                        {
                        case 0://temperature
                            deviceStats->sasStatistics.temperature.isSupported = true;
                            deviceStats->sasStatistics.temperature.isValueValid = true;
                            deviceStats->sasStatistics.temperature.thresholdNotificationEnabled = tempLogBuf[iter + 2] & BIT4;//ETC bit
                            if (tempLogBuf[iter + 2] & BIT4)
                            {
                                switch ((tempLogBuf[iter + 2] & (BIT2 | BIT3)) >> 2)
                                {
                                case 3:
                                    deviceStats->sasStatistics.temperature.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_GREATER;
                                    break;
                                case 2:
                                    deviceStats->sasStatistics.temperature.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL;
                                    break;
                                case 1:
                                    deviceStats->sasStatistics.temperature.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL;
                                    break;
                                case 0:
                                default:
                                    deviceStats->sasStatistics.temperature.threshType = THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE;
                                    break;
                                }
                            }
                            deviceStats->sasStatistics.temperature.statisticValue = tempLogBuf[iter + 5];
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            break;
                        case 1://reference temperature
                            deviceStats->sasStatistics.referenceTemperature.isSupported = true;
                            deviceStats->sasStatistics.referenceTemperature.isValueValid = true;
                            deviceStats->sasStatistics.referenceTemperature.thresholdNotificationEnabled = tempLogBuf[iter + 2] & BIT4;//ETC bit
                            if (tempLogBuf[iter + 2] & BIT4)
                            {
                                switch ((tempLogBuf[iter + 2] & (BIT2 | BIT3)) >> 2)
                                {
                                case 3:
                                    deviceStats->sasStatistics.referenceTemperature.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_GREATER;
                                    break;
                                case 2:
                                    deviceStats->sasStatistics.referenceTemperature.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL;
                                    break;
                                case 1:
                                    deviceStats->sasStatistics.referenceTemperature.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL;
                                    break;
                                case 0:
                                default:
                                    deviceStats->sasStatistics.referenceTemperature.threshType = THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE;
                                    break;
                                }
                            }
                            deviceStats->sasStatistics.referenceTemperature.statisticValue = tempLogBuf[iter + 5];
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            break;
                        default:
                            break;
                        }
                        if (parameterLength == 0)
                        {
                            break;
                        }
                    }
                    //thresholds
                    memset(tempLogBuf, 0, LEGACY_DRIVE_SEC_SIZE);
                    if (SUCCESS == scsi_Log_Sense_Cmd(device, false, LPC_THRESHOLD_VALUES, pageCode, subpageCode, 0x0000, tempLogBuf, LEGACY_DRIVE_SEC_SIZE))
                    {
                        uint16_t pageLength = M_BytesTo2ByteValue(tempLogBuf[2], tempLogBuf[3]);
                        uint8_t parameterLength = 0;
                        //loop through the data and gather the data from each parameter we care about getting.
                        for (uint16_t iter = 4; iter < pageLength && iter < LEGACY_DRIVE_SEC_SIZE; iter += (parameterLength + 4))
                        {
                            uint16_t parameterCode = M_BytesTo2ByteValue(tempLogBuf[iter], tempLogBuf[iter + 1]);
                            parameterLength = tempLogBuf[iter + 3];
                            switch (parameterCode)
                            {
                            case 0://temperature
                                deviceStats->sasStatistics.temperature.supportsNotification = true;
                                if (tempLogBuf[iter + 2] & BIT4)
                                {
                                    deviceStats->sasStatistics.temperature.isThresholdValid = true;
                                    deviceStats->sasStatistics.temperature.threshold = tempLogBuf[iter + 5];
                                    scsi_Threshold_Comparison(&deviceStats->sasStatistics.temperature);
                                }
                                break;
                            case 1://reference temperature
                                deviceStats->sasStatistics.referenceTemperature.supportsNotification = true;
                                if (tempLogBuf[iter + 2] & BIT4)
                                {
                                    deviceStats->sasStatistics.referenceTemperature.isThresholdValid = true;
                                    deviceStats->sasStatistics.referenceTemperature.threshold = tempLogBuf[iter + 5];
                                    scsi_Threshold_Comparison(&deviceStats->sasStatistics.referenceTemperature);
                                }
                                break;
                            default:
                                break;
                            }
                            if (parameterLength == 0)
                            {
                                break;
                            }
                        }
                    }
                }
                break;
            case 1://environmental reporting
                memset(tempLogBuf, 0, LEGACY_DRIVE_SEC_SIZE);
                if (SUCCESS == scsi_Log_Sense_Cmd(device, false, LPC_CUMULATIVE_VALUES, pageCode, subpageCode, 0x0000, tempLogBuf, LEGACY_DRIVE_SEC_SIZE))
                {
                    deviceStats->sasStatistics.environmentReportingSupported = true;
                    uint16_t pageLength = M_BytesTo2ByteValue(tempLogBuf[2], tempLogBuf[3]);
                    uint8_t parameterLength = 0;
                    //loop through the data and gather the data from each parameter we care about getting.
                    for (uint16_t iter = 4; iter < pageLength && iter < LEGACY_DRIVE_SEC_SIZE; iter += (parameterLength + 4))
                    {
                        uint16_t parameterCode = M_BytesTo2ByteValue(tempLogBuf[iter], tempLogBuf[iter + 1]);
                        parameterLength = tempLogBuf[iter + 3];
                        switch (parameterCode)
                        {
                        case 0://temperature report. (note: parameters 0000-00FF are for each temperature location reported...we are only going to care about the first one right now...)-TJE
                            if (tempLogBuf[iter + 2] & BIT4)
                            {
                                deviceStats->sasStatistics.currentTemperature.thresholdNotificationEnabled = true;//ETC bit
                                deviceStats->sasStatistics.lifetimeMaximumTemperature.thresholdNotificationEnabled = true;//ETC bit
                                deviceStats->sasStatistics.lifetimeMinimumTemperature.thresholdNotificationEnabled = true;//ETC bit
                                deviceStats->sasStatistics.maximumTemperatureSincePowerOn.thresholdNotificationEnabled = true;//ETC bit
                                deviceStats->sasStatistics.minimumTemperatureSincePowerOn.thresholdNotificationEnabled = true;//ETC bit
                                switch ((tempLogBuf[iter + 2] & (BIT2 | BIT3)) >> 2)
                                {
                                case 3:
                                    deviceStats->sasStatistics.currentTemperature.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_GREATER;
                                    deviceStats->sasStatistics.lifetimeMaximumTemperature.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_GREATER;
                                    deviceStats->sasStatistics.lifetimeMinimumTemperature.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_GREATER;
                                    deviceStats->sasStatistics.maximumTemperatureSincePowerOn.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_GREATER;
                                    deviceStats->sasStatistics.minimumTemperatureSincePowerOn.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_GREATER;
                                    break;
                                case 2:
                                    deviceStats->sasStatistics.currentTemperature.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL;
                                    deviceStats->sasStatistics.lifetimeMaximumTemperature.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL;
                                    deviceStats->sasStatistics.lifetimeMinimumTemperature.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL;
                                    deviceStats->sasStatistics.maximumTemperatureSincePowerOn.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL;
                                    deviceStats->sasStatistics.minimumTemperatureSincePowerOn.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL;
                                    break;
                                case 1:
                                    deviceStats->sasStatistics.currentTemperature.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL;
                                    deviceStats->sasStatistics.lifetimeMaximumTemperature.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL;
                                    deviceStats->sasStatistics.lifetimeMinimumTemperature.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL;
                                    deviceStats->sasStatistics.maximumTemperatureSincePowerOn.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL;
                                    deviceStats->sasStatistics.minimumTemperatureSincePowerOn.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL;
                                    break;
                                case 0:
                                default:
                                    deviceStats->sasStatistics.currentTemperature.threshType = THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE;
                                    deviceStats->sasStatistics.lifetimeMaximumTemperature.threshType = THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE;
                                    deviceStats->sasStatistics.lifetimeMinimumTemperature.threshType = THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE;
                                    deviceStats->sasStatistics.maximumTemperatureSincePowerOn.threshType = THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE;
                                    deviceStats->sasStatistics.minimumTemperatureSincePowerOn.threshType = THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE;
                                    break;
                                }
                            }
                            //current temperature
                            deviceStats->sasStatistics.currentTemperature.isSupported = true;
                            deviceStats->sasStatistics.currentTemperature.isValueValid = true;
                            deviceStats->sasStatistics.currentTemperature.statisticValue = tempLogBuf[iter + 5];
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            //lifetime max temp
                            deviceStats->sasStatistics.lifetimeMaximumTemperature.isSupported = true;
                            deviceStats->sasStatistics.lifetimeMaximumTemperature.isValueValid = true;
                            deviceStats->sasStatistics.lifetimeMaximumTemperature.statisticValue = tempLogBuf[iter + 6];
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            //lifetime min temp
                            deviceStats->sasStatistics.lifetimeMinimumTemperature.isSupported = true;
                            deviceStats->sasStatistics.lifetimeMinimumTemperature.isValueValid = true;
                            deviceStats->sasStatistics.lifetimeMinimumTemperature.statisticValue = tempLogBuf[iter + 7];
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            //max temp since PO
                            deviceStats->sasStatistics.maximumTemperatureSincePowerOn.isSupported = true;
                            deviceStats->sasStatistics.maximumTemperatureSincePowerOn.isValueValid = true;
                            deviceStats->sasStatistics.maximumTemperatureSincePowerOn.statisticValue = tempLogBuf[iter + 8];
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            //min temp since PO
                            deviceStats->sasStatistics.minimumTemperatureSincePowerOn.isSupported = true;
                            deviceStats->sasStatistics.minimumTemperatureSincePowerOn.isValueValid = true;
                            deviceStats->sasStatistics.minimumTemperatureSincePowerOn.statisticValue = tempLogBuf[iter + 9];
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            break;
                        case 0x100://humidity report. (note: parameters 0100-01FF are for each humidity location reported...we are only going to care about the first one right now...)-TJE
                            if (tempLogBuf[iter + 2] & BIT4)
                            {
                                deviceStats->sasStatistics.currentRelativeHumidity.thresholdNotificationEnabled = true;//ETC bit
                                deviceStats->sasStatistics.lifetimeMaximumRelativeHumidity.thresholdNotificationEnabled = true;//ETC bit
                                deviceStats->sasStatistics.lifetimeMinumumRelativeHumidity.thresholdNotificationEnabled = true;//ETC bit
                                deviceStats->sasStatistics.maximumRelativeHumiditySincePoweron.thresholdNotificationEnabled = true;//ETC bit
                                deviceStats->sasStatistics.minimumRelativeHumiditySincePoweron.thresholdNotificationEnabled = true;//ETC bit
                                switch ((tempLogBuf[iter + 2] & (BIT2 | BIT3)) >> 2)
                                {
                                case 3:
                                    deviceStats->sasStatistics.currentRelativeHumidity.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_GREATER;
                                    deviceStats->sasStatistics.lifetimeMaximumRelativeHumidity.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_GREATER;
                                    deviceStats->sasStatistics.lifetimeMinumumRelativeHumidity.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_GREATER;
                                    deviceStats->sasStatistics.maximumRelativeHumiditySincePoweron.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_GREATER;
                                    deviceStats->sasStatistics.minimumRelativeHumiditySincePoweron.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_GREATER;
                                    break;
                                case 2:
                                    deviceStats->sasStatistics.currentRelativeHumidity.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL;
                                    deviceStats->sasStatistics.lifetimeMaximumRelativeHumidity.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL;
                                    deviceStats->sasStatistics.lifetimeMinumumRelativeHumidity.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL;
                                    deviceStats->sasStatistics.maximumRelativeHumiditySincePoweron.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL;
                                    deviceStats->sasStatistics.minimumRelativeHumiditySincePoweron.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL;
                                    break;
                                case 1:
                                    deviceStats->sasStatistics.currentRelativeHumidity.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL;
                                    deviceStats->sasStatistics.lifetimeMaximumRelativeHumidity.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL;
                                    deviceStats->sasStatistics.lifetimeMinumumRelativeHumidity.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL;
                                    deviceStats->sasStatistics.maximumRelativeHumiditySincePoweron.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL;
                                    deviceStats->sasStatistics.minimumRelativeHumiditySincePoweron.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL;
                                    break;
                                case 0:
                                default:
                                    deviceStats->sasStatistics.currentRelativeHumidity.threshType = THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE;
                                    deviceStats->sasStatistics.lifetimeMaximumRelativeHumidity.threshType = THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE;
                                    deviceStats->sasStatistics.lifetimeMinumumRelativeHumidity.threshType = THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE;
                                    deviceStats->sasStatistics.maximumRelativeHumiditySincePoweron.threshType = THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE;
                                    deviceStats->sasStatistics.minimumRelativeHumiditySincePoweron.threshType = THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE;
                                    break;
                                }
                            }
                            //current humidity
                            deviceStats->sasStatistics.currentRelativeHumidity.isSupported = true;
                            deviceStats->sasStatistics.currentRelativeHumidity.isValueValid = true;
                            deviceStats->sasStatistics.currentRelativeHumidity.statisticValue = tempLogBuf[iter + 5];
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            //lifetime max humidity
                            deviceStats->sasStatistics.lifetimeMaximumRelativeHumidity.isSupported = true;
                            deviceStats->sasStatistics.lifetimeMaximumRelativeHumidity.isValueValid = true;
                            deviceStats->sasStatistics.lifetimeMaximumRelativeHumidity.statisticValue = tempLogBuf[iter + 6];
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            //lifetime min humidity
                            deviceStats->sasStatistics.lifetimeMinumumRelativeHumidity.isSupported = true;
                            deviceStats->sasStatistics.lifetimeMinumumRelativeHumidity.isValueValid = true;
                            deviceStats->sasStatistics.lifetimeMinumumRelativeHumidity.statisticValue = tempLogBuf[iter + 7];
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            //max humidity since PO
                            deviceStats->sasStatistics.maximumRelativeHumiditySincePoweron.isSupported = true;
                            deviceStats->sasStatistics.maximumRelativeHumiditySincePoweron.isValueValid = true;
                            deviceStats->sasStatistics.maximumRelativeHumiditySincePoweron.statisticValue = tempLogBuf[iter + 8];
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            //min humidity since PO
                            deviceStats->sasStatistics.minimumRelativeHumiditySincePoweron.isSupported = true;
                            deviceStats->sasStatistics.minimumRelativeHumiditySincePoweron.isValueValid = true;
                            deviceStats->sasStatistics.minimumRelativeHumiditySincePoweron.statisticValue = tempLogBuf[iter + 9];
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            break;
                        default:
                            break;
                        }
                        if (parameterLength == 0)
                        {
                            break;
                        }
                    }
                    //todo: add thresholds
                }
                break;
            case 2://environmental limits
                memset(tempLogBuf, 0, LEGACY_DRIVE_SEC_SIZE);
                if (SUCCESS == scsi_Log_Sense_Cmd(device, false, LPC_CUMULATIVE_VALUES, pageCode, subpageCode, 0x0000, tempLogBuf, LEGACY_DRIVE_SEC_SIZE))
                {
                    deviceStats->sasStatistics.environmentReportingSupported = true;
                    uint16_t pageLength = M_BytesTo2ByteValue(tempLogBuf[2], tempLogBuf[3]);
                    uint8_t parameterLength = 0;
                    //loop through the data and gather the data from each parameter we care about getting.
                    for (uint16_t iter = 4; iter < pageLength && iter < LEGACY_DRIVE_SEC_SIZE; iter += (parameterLength + 4))
                    {
                        uint16_t parameterCode = M_BytesTo2ByteValue(tempLogBuf[iter], tempLogBuf[iter + 1]);
                        parameterLength = tempLogBuf[iter + 3];
                        switch (parameterCode)
                        {
                        case 0://temperature limits. (note: parameters 0000-00FF are for each temperature location reported...we are only going to care about the first one right now...)-TJE
                            if (tempLogBuf[iter + 2] & BIT4)
                            {
                                deviceStats->sasStatistics.highCriticalTemperatureLimitTrigger.thresholdNotificationEnabled = true;//ETC bit
                                deviceStats->sasStatistics.highCriticalTemperatureLimitReset.thresholdNotificationEnabled = true;//ETC bit
                                deviceStats->sasStatistics.lowCriticalTemperatureLimitReset.thresholdNotificationEnabled = true;//ETC bit
                                deviceStats->sasStatistics.lowCriticalTemperatureLimitTrigger.thresholdNotificationEnabled = true;//ETC bit
                                deviceStats->sasStatistics.highOperatingTemperatureLimitReset.thresholdNotificationEnabled = true;//ETC bit
                                deviceStats->sasStatistics.highOperatingTemperatureLimitReset.thresholdNotificationEnabled = true;//ETC bit
                                deviceStats->sasStatistics.lowOperatingTemperatureLimitReset.thresholdNotificationEnabled = true;//ETC bit
                                deviceStats->sasStatistics.lowOperatingTemperatureLimitTrigger.thresholdNotificationEnabled = true;//ETC bit
                                switch ((tempLogBuf[iter + 2] & (BIT2 | BIT3)) >> 2)
                                {
                                case 3:
                                    deviceStats->sasStatistics.highCriticalTemperatureLimitTrigger.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_GREATER;
                                    deviceStats->sasStatistics.highCriticalTemperatureLimitReset.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_GREATER;
                                    deviceStats->sasStatistics.lowCriticalTemperatureLimitReset.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_GREATER;
                                    deviceStats->sasStatistics.lowCriticalTemperatureLimitTrigger.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_GREATER;
                                    deviceStats->sasStatistics.highOperatingTemperatureLimitTrigger.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_GREATER;
                                    deviceStats->sasStatistics.highOperatingTemperatureLimitReset.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_GREATER;
                                    deviceStats->sasStatistics.lowOperatingTemperatureLimitReset.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_GREATER;
                                    deviceStats->sasStatistics.lowOperatingTemperatureLimitTrigger.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_GREATER;
                                    break;
                                case 2:
                                    deviceStats->sasStatistics.highCriticalTemperatureLimitTrigger.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL;
                                    deviceStats->sasStatistics.highCriticalTemperatureLimitReset.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL;
                                    deviceStats->sasStatistics.lowCriticalTemperatureLimitReset.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL;
                                    deviceStats->sasStatistics.lowCriticalTemperatureLimitTrigger.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL;
                                    deviceStats->sasStatistics.highOperatingTemperatureLimitTrigger.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL;
                                    deviceStats->sasStatistics.highOperatingTemperatureLimitReset.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL;
                                    deviceStats->sasStatistics.lowOperatingTemperatureLimitReset.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL;
                                    deviceStats->sasStatistics.lowOperatingTemperatureLimitTrigger.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL;
                                    break;
                                case 1:
                                    deviceStats->sasStatistics.highCriticalTemperatureLimitTrigger.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL;
                                    deviceStats->sasStatistics.highCriticalTemperatureLimitReset.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL;
                                    deviceStats->sasStatistics.lowCriticalTemperatureLimitReset.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL;
                                    deviceStats->sasStatistics.lowCriticalTemperatureLimitTrigger.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL;
                                    deviceStats->sasStatistics.highOperatingTemperatureLimitTrigger.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL;
                                    deviceStats->sasStatistics.highOperatingTemperatureLimitReset.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL;
                                    deviceStats->sasStatistics.lowOperatingTemperatureLimitReset.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL;
                                    deviceStats->sasStatistics.lowOperatingTemperatureLimitTrigger.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL;
                                    break;
                                case 0:
                                default:
                                    deviceStats->sasStatistics.highCriticalTemperatureLimitTrigger.threshType = THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE;
                                    deviceStats->sasStatistics.highCriticalTemperatureLimitReset.threshType = THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE;
                                    deviceStats->sasStatistics.lowCriticalTemperatureLimitReset.threshType = THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE;
                                    deviceStats->sasStatistics.lowCriticalTemperatureLimitTrigger.threshType = THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE;
                                    deviceStats->sasStatistics.highOperatingTemperatureLimitTrigger.threshType = THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE;
                                    deviceStats->sasStatistics.highOperatingTemperatureLimitReset.threshType = THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE;
                                    deviceStats->sasStatistics.lowOperatingTemperatureLimitReset.threshType = THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE;
                                    deviceStats->sasStatistics.lowOperatingTemperatureLimitTrigger.threshType = THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE;
                                    break;
                                }
                            }
                            //
                            deviceStats->sasStatistics.highCriticalTemperatureLimitTrigger.isSupported = true;
                            deviceStats->sasStatistics.highCriticalTemperatureLimitTrigger.isValueValid = true;
                            deviceStats->sasStatistics.highCriticalTemperatureLimitTrigger.statisticValue = tempLogBuf[iter + 4];
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            //
                            deviceStats->sasStatistics.highCriticalTemperatureLimitReset.isSupported = true;
                            deviceStats->sasStatistics.highCriticalTemperatureLimitReset.isValueValid = true;
                            deviceStats->sasStatistics.highCriticalTemperatureLimitReset.statisticValue = tempLogBuf[iter + 5];
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            //
                            deviceStats->sasStatistics.lowCriticalTemperatureLimitReset.isSupported = true;
                            deviceStats->sasStatistics.lowCriticalTemperatureLimitReset.isValueValid = true;
                            deviceStats->sasStatistics.lowCriticalTemperatureLimitReset.statisticValue = tempLogBuf[iter + 6];
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            //
                            deviceStats->sasStatistics.lowCriticalTemperatureLimitTrigger.isSupported = true;
                            deviceStats->sasStatistics.lowCriticalTemperatureLimitTrigger.isValueValid = true;
                            deviceStats->sasStatistics.lowCriticalTemperatureLimitTrigger.statisticValue = tempLogBuf[iter + 7];
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            //
                            deviceStats->sasStatistics.highOperatingTemperatureLimitTrigger.isSupported = true;
                            deviceStats->sasStatistics.highOperatingTemperatureLimitTrigger.isValueValid = true;
                            deviceStats->sasStatistics.highOperatingTemperatureLimitTrigger.statisticValue = tempLogBuf[iter + 8];
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            //
                            deviceStats->sasStatistics.highOperatingTemperatureLimitReset.isSupported = true;
                            deviceStats->sasStatistics.highOperatingTemperatureLimitReset.isValueValid = true;
                            deviceStats->sasStatistics.highOperatingTemperatureLimitReset.statisticValue = tempLogBuf[iter + 9];
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            //
                            deviceStats->sasStatistics.lowOperatingTemperatureLimitReset.isSupported = true;
                            deviceStats->sasStatistics.lowOperatingTemperatureLimitReset.isValueValid = true;
                            deviceStats->sasStatistics.lowOperatingTemperatureLimitReset.statisticValue = tempLogBuf[iter + 10];
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            //
                            deviceStats->sasStatistics.lowOperatingTemperatureLimitTrigger.isSupported = true;
                            deviceStats->sasStatistics.lowOperatingTemperatureLimitTrigger.isValueValid = true;
                            deviceStats->sasStatistics.lowOperatingTemperatureLimitTrigger.statisticValue = tempLogBuf[iter + 11];
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            break;
                        case 0x100://humidity limits. (note: parameters 0100-01FF are for each humidity location reported...we are only going to care about the first one right now...)-TJE
                            //
                            deviceStats->sasStatistics.highCriticalHumidityLimitTrigger.isSupported = true;
                            deviceStats->sasStatistics.highCriticalHumidityLimitTrigger.isValueValid = true;
                            deviceStats->sasStatistics.highCriticalHumidityLimitTrigger.statisticValue = tempLogBuf[iter + 4];
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            //
                            deviceStats->sasStatistics.highCriticalHumidityLimitReset.isSupported = true;
                            deviceStats->sasStatistics.highCriticalHumidityLimitReset.isValueValid = true;
                            deviceStats->sasStatistics.highCriticalHumidityLimitReset.statisticValue = tempLogBuf[iter + 5];
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            //
                            deviceStats->sasStatistics.lowCriticalHumidityLimitReset.isSupported = true;
                            deviceStats->sasStatistics.lowCriticalHumidityLimitReset.isValueValid = true;
                            deviceStats->sasStatistics.lowCriticalHumidityLimitReset.statisticValue = tempLogBuf[iter + 6];
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            //
                            deviceStats->sasStatistics.lowCriticalHumidityLimitTrigger.isSupported = true;
                            deviceStats->sasStatistics.lowCriticalHumidityLimitTrigger.isValueValid = true;
                            deviceStats->sasStatistics.lowCriticalHumidityLimitTrigger.statisticValue = tempLogBuf[iter + 7];
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            //
                            deviceStats->sasStatistics.highOperatingHumidityLimitTrigger.isSupported = true;
                            deviceStats->sasStatistics.highOperatingHumidityLimitTrigger.isValueValid = true;
                            deviceStats->sasStatistics.highOperatingHumidityLimitTrigger.statisticValue = tempLogBuf[iter + 8];
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            //
                            deviceStats->sasStatistics.highOperatingHumidityLimitReset.isSupported = true;
                            deviceStats->sasStatistics.highOperatingHumidityLimitReset.isValueValid = true;
                            deviceStats->sasStatistics.highOperatingHumidityLimitReset.statisticValue = tempLogBuf[iter + 9];
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            //
                            deviceStats->sasStatistics.lowOperatingHumidityLimitReset.isSupported = true;
                            deviceStats->sasStatistics.lowOperatingHumidityLimitReset.isValueValid = true;
                            deviceStats->sasStatistics.lowOperatingHumidityLimitReset.statisticValue = tempLogBuf[iter + 10];
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            //
                            deviceStats->sasStatistics.lowOperatingHumidityLimitTrigger.isSupported = true;
                            deviceStats->sasStatistics.lowOperatingHumidityLimitTrigger.isValueValid = true;
                            deviceStats->sasStatistics.lowOperatingHumidityLimitTrigger.statisticValue = tempLogBuf[iter + 11];
                            ++deviceStats->sasStatistics.statisticsPopulated;
                        default:
                            break;
                        }
                        if (parameterLength == 0)
                        {
                            break;
                        }
                    }
                    //todo: add thresholds
                }
                break;
            default:
                break;
            }
            break;
        case LP_START_STOP_CYCLE_COUNTER:
            switch (subpageCode)
            {
            case 0://start stop cycle counter
                memset(tempLogBuf, 0, LEGACY_DRIVE_SEC_SIZE);
                if (SUCCESS == scsi_Log_Sense_Cmd(device, false, LPC_CUMULATIVE_VALUES, pageCode, subpageCode, 0x0001, tempLogBuf, LEGACY_DRIVE_SEC_SIZE))
                {
                    deviceStats->sasStatistics.startStopCycleCounterSupported = true;
                    uint16_t pageLength = M_BytesTo2ByteValue(tempLogBuf[2], tempLogBuf[3]);
                    uint8_t parameterLength = 0;
                    //loop through the data and gather the data from each parameter we care about getting.
                    for (uint16_t iter = 4; iter < pageLength && iter < LEGACY_DRIVE_SEC_SIZE; iter += (parameterLength + 4))
                    {
                        uint16_t parameterCode = M_BytesTo2ByteValue(tempLogBuf[iter], tempLogBuf[iter + 1]);
                        parameterLength = tempLogBuf[iter + 3];
                        switch (parameterCode)
                        {
                        case 1://date of manufacture
                            //set into the buffer as week-year and parse it out that way later.
                            deviceStats->sasStatistics.dateOfManufacture.isSupported = true;
                            deviceStats->sasStatistics.dateOfManufacture.isValueValid = true;
                            deviceStats->sasStatistics.dateOfManufacture.thresholdNotificationEnabled = tempLogBuf[iter + 2] & BIT4;//ETC bit
                            if (tempLogBuf[iter + 2] & BIT4)
                            {
                                switch ((tempLogBuf[iter + 2] & (BIT2 | BIT3)) >> 2)
                                {
                                case 3:
                                    deviceStats->sasStatistics.dateOfManufacture.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_GREATER;
                                    break;
                                case 2:
                                    deviceStats->sasStatistics.dateOfManufacture.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL;
                                    break;
                                case 1:
                                    deviceStats->sasStatistics.dateOfManufacture.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL;
                                    break;
                                case 0:
                                default:
                                    deviceStats->sasStatistics.dateOfManufacture.threshType = THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE;
                                    break;
                                }
                            }
                            deviceStats->sasStatistics.dateOfManufacture.statisticValue = M_BytesTo4ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
                            deviceStats->sasStatistics.dateOfManufacture.statisticValue |= (uint64_t)M_BytesTo2ByteValue(tempLogBuf[iter + 8], tempLogBuf[iter + 9]) << 32;
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            break;
                        case 2://accounting date
                            //set into the buffer as week-year and parse it out that way later.
                            deviceStats->sasStatistics.accountingDate.isSupported = true;
                            deviceStats->sasStatistics.accountingDate.isValueValid = true;
                            deviceStats->sasStatistics.accountingDate.thresholdNotificationEnabled = tempLogBuf[iter + 2] & BIT4;//ETC bit
                            if (tempLogBuf[iter + 2] & BIT4)
                            {
                                switch ((tempLogBuf[iter + 2] & (BIT2 | BIT3)) >> 2)
                                {
                                case 3:
                                    deviceStats->sasStatistics.accountingDate.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_GREATER;
                                    break;
                                case 2:
                                    deviceStats->sasStatistics.accountingDate.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL;
                                    break;
                                case 1:
                                    deviceStats->sasStatistics.accountingDate.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL;
                                    break;
                                case 0:
                                default:
                                    deviceStats->sasStatistics.accountingDate.threshType = THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE;
                                    break;
                                }
                            }
                            deviceStats->sasStatistics.accountingDate.statisticValue = M_BytesTo4ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
                            deviceStats->sasStatistics.accountingDate.statisticValue |= (uint64_t)M_BytesTo2ByteValue(tempLogBuf[iter + 8], tempLogBuf[iter + 9]) << 32;
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            break;
                        case 3://specified cycle count over device lifetime
                            deviceStats->sasStatistics.specifiedCycleCountOverDeviceLifetime.isSupported = true;
                            deviceStats->sasStatistics.specifiedCycleCountOverDeviceLifetime.isValueValid = true;
                            deviceStats->sasStatistics.specifiedCycleCountOverDeviceLifetime.thresholdNotificationEnabled = tempLogBuf[iter + 2] & BIT4;//ETC bit
                            if (tempLogBuf[iter + 2] & BIT4)
                            {
                                switch ((tempLogBuf[iter + 2] & (BIT2 | BIT3)) >> 2)
                                {
                                case 3:
                                    deviceStats->sasStatistics.specifiedCycleCountOverDeviceLifetime.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_GREATER;
                                    break;
                                case 2:
                                    deviceStats->sasStatistics.specifiedCycleCountOverDeviceLifetime.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL;
                                    break;
                                case 1:
                                    deviceStats->sasStatistics.specifiedCycleCountOverDeviceLifetime.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL;
                                    break;
                                case 0:
                                default:
                                    deviceStats->sasStatistics.specifiedCycleCountOverDeviceLifetime.threshType = THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE;
                                    break;
                                }
                            }
                            deviceStats->sasStatistics.specifiedCycleCountOverDeviceLifetime.statisticValue = M_BytesTo4ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            break;
                        case 4://accumulated start-stop cycles
                            deviceStats->sasStatistics.accumulatedStartStopCycles.isSupported = true;
                            deviceStats->sasStatistics.accumulatedStartStopCycles.isValueValid = true;
                            deviceStats->sasStatistics.accumulatedStartStopCycles.thresholdNotificationEnabled = tempLogBuf[iter + 2] & BIT4;//ETC bit
                            if (tempLogBuf[iter + 2] & BIT4)
                            {
                                switch ((tempLogBuf[iter + 2] & (BIT2 | BIT3)) >> 2)
                                {
                                case 3:
                                    deviceStats->sasStatistics.accumulatedStartStopCycles.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_GREATER;
                                    break;
                                case 2:
                                    deviceStats->sasStatistics.accumulatedStartStopCycles.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL;
                                    break;
                                case 1:
                                    deviceStats->sasStatistics.accumulatedStartStopCycles.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL;
                                    break;
                                case 0:
                                default:
                                    deviceStats->sasStatistics.accumulatedStartStopCycles.threshType = THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE;
                                    break;
                                }
                            }
                            deviceStats->sasStatistics.accumulatedStartStopCycles.statisticValue = M_BytesTo4ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            break;
                        case 5://specified load-unload count over device lifetime
                            deviceStats->sasStatistics.specifiedLoadUnloadCountOverDeviceLifetime.isSupported = true;
                            deviceStats->sasStatistics.specifiedLoadUnloadCountOverDeviceLifetime.isValueValid = true;
                            deviceStats->sasStatistics.specifiedLoadUnloadCountOverDeviceLifetime.thresholdNotificationEnabled = tempLogBuf[iter + 2] & BIT4;//ETC bit
                            if (tempLogBuf[iter + 2] & BIT4)
                            {
                                switch ((tempLogBuf[iter + 2] & (BIT2 | BIT3)) >> 2)
                                {
                                case 3:
                                    deviceStats->sasStatistics.specifiedLoadUnloadCountOverDeviceLifetime.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_GREATER;
                                    break;
                                case 2:
                                    deviceStats->sasStatistics.specifiedLoadUnloadCountOverDeviceLifetime.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL;
                                    break;
                                case 1:
                                    deviceStats->sasStatistics.specifiedLoadUnloadCountOverDeviceLifetime.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL;
                                    break;
                                case 0:
                                default:
                                    deviceStats->sasStatistics.specifiedLoadUnloadCountOverDeviceLifetime.threshType = THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE;
                                    break;
                                }
                            }
                            deviceStats->sasStatistics.specifiedLoadUnloadCountOverDeviceLifetime.statisticValue = M_BytesTo4ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            break;
                        case 6://accumulated load-unload cycles
                            deviceStats->sasStatistics.accumulatedLoadUnloadCycles.isSupported = true;
                            deviceStats->sasStatistics.accumulatedLoadUnloadCycles.isValueValid = true;
                            deviceStats->sasStatistics.accumulatedLoadUnloadCycles.thresholdNotificationEnabled = tempLogBuf[iter + 2] & BIT4;//ETC bit
                            if (tempLogBuf[iter + 2] & BIT4)
                            {
                                switch ((tempLogBuf[iter + 2] & (BIT2 | BIT3)) >> 2)
                                {
                                case 3:
                                    deviceStats->sasStatistics.accumulatedLoadUnloadCycles.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_GREATER;
                                    break;
                                case 2:
                                    deviceStats->sasStatistics.accumulatedLoadUnloadCycles.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL;
                                    break;
                                case 1:
                                    deviceStats->sasStatistics.accumulatedLoadUnloadCycles.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL;
                                    break;
                                case 0:
                                default:
                                    deviceStats->sasStatistics.accumulatedLoadUnloadCycles.threshType = THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE;
                                    break;
                                }
                            }
                            deviceStats->sasStatistics.accumulatedLoadUnloadCycles.statisticValue = M_BytesTo4ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            break;
                        default:
                            break;
                        }
                        if (parameterLength == 0)
                        {
                            break;
                        }
                    }
                    //thresholds
                    memset(tempLogBuf, 0, LEGACY_DRIVE_SEC_SIZE);
                    if (SUCCESS == scsi_Log_Sense_Cmd(device, false, LPC_THRESHOLD_VALUES, pageCode, subpageCode, 0x0001, tempLogBuf, LEGACY_DRIVE_SEC_SIZE))
                    {
                        uint16_t pageLength = M_BytesTo2ByteValue(tempLogBuf[2], tempLogBuf[3]);
                        uint8_t parameterLength = 0;
                        //loop through the data and gather the data from each parameter we care about getting.
                        for (uint16_t iter = 4; iter < pageLength && iter < LEGACY_DRIVE_SEC_SIZE; iter += (parameterLength + 4))
                        {
                            uint16_t parameterCode = M_BytesTo2ByteValue(tempLogBuf[iter], tempLogBuf[iter + 1]);
                            parameterLength = tempLogBuf[iter + 3];
                            switch (parameterCode)
                            {
                            case 1://date of manufacture
                                deviceStats->sasStatistics.dateOfManufacture.supportsNotification = true;
                                if (tempLogBuf[iter + 2] & BIT4)
                                {
                                    deviceStats->sasStatistics.dateOfManufacture.isThresholdValid = true;
                                    deviceStats->sasStatistics.dateOfManufacture.threshold = M_BytesTo4ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
                                    deviceStats->sasStatistics.dateOfManufacture.threshold |= (uint64_t)M_BytesTo2ByteValue(tempLogBuf[iter + 8], tempLogBuf[iter + 9]) << 32;
                                    scsi_Threshold_Comparison(&deviceStats->sasStatistics.dateOfManufacture);
                                }
                                break;
                            case 2://accounting date
                                deviceStats->sasStatistics.accountingDate.supportsNotification = true;
                                if (tempLogBuf[iter + 2] & BIT4)
                                {
                                    deviceStats->sasStatistics.accountingDate.isThresholdValid = true;
                                    deviceStats->sasStatistics.accountingDate.threshold = M_BytesTo4ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
                                    deviceStats->sasStatistics.accountingDate.threshold |= (uint64_t)M_BytesTo2ByteValue(tempLogBuf[iter + 8], tempLogBuf[iter + 9]) << 32;
                                    scsi_Threshold_Comparison(&deviceStats->sasStatistics.accountingDate);
                                }
                                break;
                            case 3://specified cycle count over device lifetime
                                deviceStats->sasStatistics.specifiedCycleCountOverDeviceLifetime.supportsNotification = true;
                                if (tempLogBuf[iter + 2] & BIT4)
                                {
                                    deviceStats->sasStatistics.specifiedCycleCountOverDeviceLifetime.isThresholdValid = true;
                                    deviceStats->sasStatistics.specifiedCycleCountOverDeviceLifetime.statisticValue = M_BytesTo4ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
                                    scsi_Threshold_Comparison(&deviceStats->sasStatistics.specifiedCycleCountOverDeviceLifetime);
                                }
                                break;
                            case 4://accumulated start-stop cycles
                                deviceStats->sasStatistics.accumulatedStartStopCycles.supportsNotification = true;
                                if (tempLogBuf[iter + 2] & BIT4)
                                {
                                    deviceStats->sasStatistics.accumulatedStartStopCycles.isThresholdValid = true;
                                    deviceStats->sasStatistics.accumulatedStartStopCycles.statisticValue = M_BytesTo4ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
                                    scsi_Threshold_Comparison(&deviceStats->sasStatistics.accumulatedStartStopCycles);
                                }
                                break;
                            case 5://specified load-unload count over device lifetime
                                deviceStats->sasStatistics.specifiedLoadUnloadCountOverDeviceLifetime.supportsNotification = true;
                                if (tempLogBuf[iter + 2] & BIT4)
                                {
                                    deviceStats->sasStatistics.specifiedLoadUnloadCountOverDeviceLifetime.isThresholdValid = true;
                                    deviceStats->sasStatistics.specifiedLoadUnloadCountOverDeviceLifetime.statisticValue = M_BytesTo4ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
                                    scsi_Threshold_Comparison(&deviceStats->sasStatistics.specifiedLoadUnloadCountOverDeviceLifetime);
                                }
                                break;
                            case 6://accumulated load-unload cycles
                                deviceStats->sasStatistics.accumulatedLoadUnloadCycles.supportsNotification = true;
                                if (tempLogBuf[iter + 2] & BIT4)
                                {
                                    deviceStats->sasStatistics.accumulatedLoadUnloadCycles.isThresholdValid = true;
                                    deviceStats->sasStatistics.accumulatedLoadUnloadCycles.statisticValue = M_BytesTo4ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
                                    scsi_Threshold_Comparison(&deviceStats->sasStatistics.accumulatedLoadUnloadCycles);
                                }
                                break;
                            default:
                                break;
                            }
                            if (parameterLength == 0)
                            {
                                break;
                            }
                        }
                    }
                }
                break;
            case 1://utilization
                memset(tempLogBuf, 0, LEGACY_DRIVE_SEC_SIZE);
                if (SUCCESS == scsi_Log_Sense_Cmd(device, false, LPC_CUMULATIVE_VALUES, pageCode, subpageCode, 0x0000, tempLogBuf, LEGACY_DRIVE_SEC_SIZE))
                {
                    deviceStats->sasStatistics.utilizationSupported = true;
                    uint16_t pageLength = M_BytesTo2ByteValue(tempLogBuf[2], tempLogBuf[3]);
                    uint8_t parameterLength = 0;
                    //loop through the data and gather the data from each parameter we care about getting.
                    for (uint16_t iter = 4; iter < pageLength && iter < LEGACY_DRIVE_SEC_SIZE; iter += (parameterLength + 4))
                    {
                        uint16_t parameterCode = M_BytesTo2ByteValue(tempLogBuf[iter], tempLogBuf[iter + 1]);
                        parameterLength = tempLogBuf[iter + 3];
                        switch (parameterCode)
                        {
                        case 0://workload utilization
                            deviceStats->sasStatistics.workloadUtilization.isSupported = true;
                            deviceStats->sasStatistics.workloadUtilization.isValueValid = true;
                            deviceStats->sasStatistics.workloadUtilization.thresholdNotificationEnabled = tempLogBuf[iter + 2] & BIT4;//ETC bit
                            if (tempLogBuf[iter + 2] & BIT4)
                            {
                                switch ((tempLogBuf[iter + 2] & (BIT2 | BIT3)) >> 2)
                                {
                                case 3:
                                    deviceStats->sasStatistics.workloadUtilization.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_GREATER;
                                    break;
                                case 2:
                                    deviceStats->sasStatistics.workloadUtilization.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL;
                                    break;
                                case 1:
                                    deviceStats->sasStatistics.workloadUtilization.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL;
                                    break;
                                case 0:
                                default:
                                    deviceStats->sasStatistics.workloadUtilization.threshType = THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE;
                                    break;
                                }
                            }
                            deviceStats->sasStatistics.workloadUtilization.statisticValue = M_BytesTo2ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5]);
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            break;
                        case 1://utilization usage rate based on date and time
                            deviceStats->sasStatistics.utilizationUsageRateBasedOnDateAndTime.isSupported = true;
                            deviceStats->sasStatistics.utilizationUsageRateBasedOnDateAndTime.isValueValid = true;
                            deviceStats->sasStatistics.utilizationUsageRateBasedOnDateAndTime.thresholdNotificationEnabled = tempLogBuf[iter + 2] & BIT4;//ETC bit
                            if (tempLogBuf[iter + 2] & BIT4)
                            {
                                switch ((tempLogBuf[iter + 2] & (BIT2 | BIT3)) >> 2)
                                {
                                case 3:
                                    deviceStats->sasStatistics.utilizationUsageRateBasedOnDateAndTime.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_GREATER;
                                    break;
                                case 2:
                                    deviceStats->sasStatistics.utilizationUsageRateBasedOnDateAndTime.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL;
                                    break;
                                case 1:
                                    deviceStats->sasStatistics.utilizationUsageRateBasedOnDateAndTime.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL;
                                    break;
                                case 0:
                                default:
                                    deviceStats->sasStatistics.utilizationUsageRateBasedOnDateAndTime.threshType = THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE;
                                    break;
                                }
                            }
                            deviceStats->sasStatistics.utilizationUsageRateBasedOnDateAndTime.statisticValue = tempLogBuf[iter + 4];
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            break;
                        default:
                            break;
                        }
                        if (parameterLength == 0)
                        {
                            break;
                        }
                    }
                    //thresholds
                    memset(tempLogBuf, 0, LEGACY_DRIVE_SEC_SIZE);
                    if (SUCCESS == scsi_Log_Sense_Cmd(device, false, LPC_THRESHOLD_VALUES, pageCode, subpageCode, 0x0000, tempLogBuf, LEGACY_DRIVE_SEC_SIZE))
                    {
                        uint16_t pageLength = M_BytesTo2ByteValue(tempLogBuf[2], tempLogBuf[3]);
                        uint8_t parameterLength = 0;
                        //loop through the data and gather the data from each parameter we care about getting.
                        for (uint16_t iter = 4; iter < pageLength && iter < LEGACY_DRIVE_SEC_SIZE; iter += (parameterLength + 4))
                        {
                            uint16_t parameterCode = M_BytesTo2ByteValue(tempLogBuf[iter], tempLogBuf[iter + 1]);
                            parameterLength = tempLogBuf[iter + 3];
                            switch (parameterCode)
                            {
                            case 1://workload utilization
                                deviceStats->sasStatistics.workloadUtilization.supportsNotification = true;
                                if (tempLogBuf[iter + 2] & BIT4)
                                {
                                    deviceStats->sasStatistics.workloadUtilization.isThresholdValid = true;
                                    deviceStats->sasStatistics.workloadUtilization.threshold = M_BytesTo2ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5]);
                                    scsi_Threshold_Comparison(&deviceStats->sasStatistics.workloadUtilization);
                                }
                                break;
                            case 2://utilization usage based on date and timestamp
                                deviceStats->sasStatistics.utilizationUsageRateBasedOnDateAndTime.supportsNotification = true;
                                if (tempLogBuf[iter + 2] & BIT4)
                                {
                                    deviceStats->sasStatistics.utilizationUsageRateBasedOnDateAndTime.isThresholdValid = true;
                                    deviceStats->sasStatistics.utilizationUsageRateBasedOnDateAndTime.threshold = tempLogBuf[iter + 4];
                                    scsi_Threshold_Comparison(&deviceStats->sasStatistics.utilizationUsageRateBasedOnDateAndTime);
                                }
                                break;
                            default:
                                break;
                            }
                            if (parameterLength == 0)
                            {
                                break;
                            }
                        }
                    }
                }
                break;
            default:
                break;
            }
            break;
        case LP_SOLID_STATE_MEDIA:
            if (subpageCode == 0)
            {
                memset(tempLogBuf, 0, LEGACY_DRIVE_SEC_SIZE);
                if (SUCCESS == scsi_Log_Sense_Cmd(device, false, LPC_CUMULATIVE_VALUES, pageCode, subpageCode, 0x0001, tempLogBuf, LEGACY_DRIVE_SEC_SIZE))
                {
                    deviceStats->sasStatistics.solidStateMediaSupported = true;
                    uint16_t pageLength = M_BytesTo2ByteValue(tempLogBuf[2], tempLogBuf[3]);
                    uint8_t parameterLength = 0;
                    //loop through the data and gather the data from each parameter we care about getting.
                    for (uint16_t iter = 4; iter < pageLength && iter < LEGACY_DRIVE_SEC_SIZE; iter += (parameterLength + 4))
                    {
                        uint16_t parameterCode = M_BytesTo2ByteValue(tempLogBuf[iter], tempLogBuf[iter + 1]);
                        parameterLength = tempLogBuf[iter + 3];
                        switch (parameterCode)
                        {
                        case 1://percent used endurance indicator
                            deviceStats->sasStatistics.percentUsedEndurance.isSupported = true;
                            deviceStats->sasStatistics.percentUsedEndurance.isValueValid = true;
                            deviceStats->sasStatistics.percentUsedEndurance.thresholdNotificationEnabled = tempLogBuf[iter + 2] & BIT4;//ETC bit
                            if (tempLogBuf[iter + 2] & BIT4)
                            {
                                switch ((tempLogBuf[iter + 2] & (BIT2 | BIT3)) >> 2)
                                {
                                case 3:
                                    deviceStats->sasStatistics.percentUsedEndurance.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_GREATER;
                                    break;
                                case 2:
                                    deviceStats->sasStatistics.percentUsedEndurance.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL;
                                    break;
                                case 1:
                                    deviceStats->sasStatistics.percentUsedEndurance.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL;
                                    break;
                                case 0:
                                default:
                                    deviceStats->sasStatistics.percentUsedEndurance.threshType = THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE;
                                    break;
                                }
                            }
                            deviceStats->sasStatistics.percentUsedEndurance.statisticValue = tempLogBuf[iter + 7];
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            break;
                        default:
                            break;
                        }
                        if (parameterLength == 0)
                        {
                            break;
                        }
                    }
                    //thresholds
                    memset(tempLogBuf, 0, LEGACY_DRIVE_SEC_SIZE);
                    if (SUCCESS == scsi_Log_Sense_Cmd(device, false, LPC_THRESHOLD_VALUES, pageCode, subpageCode, 0x0001, tempLogBuf, LEGACY_DRIVE_SEC_SIZE))
                    {
                        uint16_t pageLength = M_BytesTo2ByteValue(tempLogBuf[2], tempLogBuf[3]);
                        uint8_t parameterLength = 0;
                        //loop through the data and gather the data from each parameter we care about getting.
                        for (uint16_t iter = 4; iter < pageLength && iter < LEGACY_DRIVE_SEC_SIZE; iter += (parameterLength + 4))
                        {
                            uint16_t parameterCode = M_BytesTo2ByteValue(tempLogBuf[iter], tempLogBuf[iter + 1]);
                            parameterLength = tempLogBuf[iter + 3];
                            switch (parameterCode)
                            {
                            case 1://percent used endurance
                                deviceStats->sasStatistics.percentUsedEndurance.supportsNotification = true;
                                if (tempLogBuf[iter + 2] & BIT4)
                                {
                                    deviceStats->sasStatistics.percentUsedEndurance.isThresholdValid = true;
                                    deviceStats->sasStatistics.percentUsedEndurance.threshold = tempLogBuf[iter + 7];
                                    scsi_Threshold_Comparison(&deviceStats->sasStatistics.percentUsedEndurance);
                                }
                                break;
                            default:
                                break;
                            }
                            if (parameterLength == 0)
                            {
                                break;
                            }
                        }
                    }
                }
            }
            break;
        case LP_BACKGROUND_SCAN_RESULTS:
            switch (subpageCode)
            {
            case 0://background scan results
                memset(tempLogBuf, 0, LEGACY_DRIVE_SEC_SIZE);
                if (SUCCESS == scsi_Log_Sense_Cmd(device, false, LPC_CUMULATIVE_VALUES, pageCode, subpageCode, 0x0000, tempLogBuf, LEGACY_DRIVE_SEC_SIZE))
                {
                    deviceStats->sasStatistics.backgroundScanResultsSupported = true;
                    uint16_t pageLength = M_BytesTo2ByteValue(tempLogBuf[2], tempLogBuf[3]);
                    uint8_t parameterLength = 0;
                    //loop through the data and gather the data from each parameter we care about getting.
                    for (uint16_t iter = 4; iter < pageLength && iter < LEGACY_DRIVE_SEC_SIZE; iter += (parameterLength + 4))
                    {
                        uint16_t parameterCode = M_BytesTo2ByteValue(tempLogBuf[iter], tempLogBuf[iter + 1]);
                        parameterLength = tempLogBuf[iter + 3];
                        switch (parameterCode)
                        {
                        case 0://POM, # scans performed, bg scan progress, # bgms performed
                            //accumulated power on minutes
                            deviceStats->sasStatistics.accumulatedPowerOnMinutes.isSupported = true;
                            deviceStats->sasStatistics.accumulatedPowerOnMinutes.isValueValid = true;
                            deviceStats->sasStatistics.accumulatedPowerOnMinutes.statisticValue = M_BytesTo4ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            //number of background scans performed
                            deviceStats->sasStatistics.numberOfBackgroundScansPerformed.isSupported = true;
                            deviceStats->sasStatistics.numberOfBackgroundScansPerformed.isValueValid = true;
                            deviceStats->sasStatistics.numberOfBackgroundScansPerformed.statisticValue = M_BytesTo2ByteValue(tempLogBuf[iter + 10], tempLogBuf[iter + 11]);
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            //background scan progress- not sure if this is needed.

                            //number of background media scans performed
                            deviceStats->sasStatistics.numberOfBackgroundMediaScansPerformed.isSupported = true;
                            deviceStats->sasStatistics.numberOfBackgroundMediaScansPerformed.isValueValid = true;
                            deviceStats->sasStatistics.numberOfBackgroundMediaScansPerformed.statisticValue = M_BytesTo2ByteValue(tempLogBuf[iter + 14], tempLogBuf[iter + 15]);
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            break;
                        default:
                            break;
                        }
                        if (parameterLength == 0)
                        {
                            break;
                        }
                    }
                    //thresholds
                    memset(tempLogBuf, 0, LEGACY_DRIVE_SEC_SIZE);
                    if (SUCCESS == scsi_Log_Sense_Cmd(device, false, LPC_THRESHOLD_VALUES, pageCode, subpageCode, 0x0000, tempLogBuf, LEGACY_DRIVE_SEC_SIZE))
                    {
                        uint16_t pageLength = M_BytesTo2ByteValue(tempLogBuf[2], tempLogBuf[3]);
                        uint8_t parameterLength = 0;
                        //loop through the data and gather the data from each parameter we care about getting.
                        for (uint16_t iter = 4; iter < pageLength && iter < LEGACY_DRIVE_SEC_SIZE; iter += (parameterLength + 4))
                        {
                            uint16_t parameterCode = M_BytesTo2ByteValue(tempLogBuf[iter], tempLogBuf[iter + 1]);
                            parameterLength = tempLogBuf[iter + 3];
                            switch (parameterCode)
                            {
                            case 0://POM, # scans performed, bg scan progress, # bgms performed
                                deviceStats->sasStatistics.accumulatedPowerOnMinutes.supportsNotification = true;
                                deviceStats->sasStatistics.numberOfBackgroundScansPerformed.supportsNotification = true;
                                deviceStats->sasStatistics.numberOfBackgroundMediaScansPerformed.supportsNotification = true;
                                if (tempLogBuf[iter + 2] & BIT4)
                                {
                                    //accumulated power on minutes
                                    deviceStats->sasStatistics.accumulatedPowerOnMinutes.isThresholdValid = true;
                                    deviceStats->sasStatistics.accumulatedPowerOnMinutes.threshold = M_BytesTo4ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
                                    scsi_Threshold_Comparison(&deviceStats->sasStatistics.accumulatedPowerOnMinutes);
                                    //number of background scans performed
                                    deviceStats->sasStatistics.numberOfBackgroundScansPerformed.isThresholdValid = true;
                                    deviceStats->sasStatistics.numberOfBackgroundScansPerformed.threshold = M_BytesTo2ByteValue(tempLogBuf[iter + 10], tempLogBuf[iter + 11]);
                                    scsi_Threshold_Comparison(&deviceStats->sasStatistics.numberOfBackgroundScansPerformed);
                                    //background scan progress- not sure if this is needed.

                                    //number of background media scans performed
                                    deviceStats->sasStatistics.numberOfBackgroundMediaScansPerformed.isThresholdValid = true;
                                    deviceStats->sasStatistics.numberOfBackgroundMediaScansPerformed.threshold = M_BytesTo2ByteValue(tempLogBuf[iter + 14], tempLogBuf[iter + 15]);
                                    scsi_Threshold_Comparison(&deviceStats->sasStatistics.numberOfBackgroundMediaScansPerformed);
                                }
                                break;
                            default:
                                break;
                            }
                            if (parameterLength == 0)
                            {
                                break;
                            }
                        }
                    }
                }
                break;
            case 1://pending defects
                memset(tempLogBuf, 0, LEGACY_DRIVE_SEC_SIZE);
                if (SUCCESS == scsi_Log_Sense_Cmd(device, false, LPC_CUMULATIVE_VALUES, pageCode, subpageCode, 0x0000, tempLogBuf, LEGACY_DRIVE_SEC_SIZE))
                {
                    deviceStats->sasStatistics.pendingDefectsSupported = true;
                    uint16_t pageLength = M_BytesTo2ByteValue(tempLogBuf[2], tempLogBuf[3]);
                    uint8_t parameterLength = 0;
                    //loop through the data and gather the data from each parameter we care about getting.
                    for (uint16_t iter = 4; iter < pageLength && iter < LEGACY_DRIVE_SEC_SIZE; iter += (parameterLength + 4))
                    {
                        uint16_t parameterCode = M_BytesTo2ByteValue(tempLogBuf[iter], tempLogBuf[iter + 1]);
                        parameterLength = tempLogBuf[iter + 3];
                        switch (parameterCode)
                        {
                        case 0://pending defect count
                            deviceStats->sasStatistics.pendingDefectCount.isSupported = true;
                            deviceStats->sasStatistics.pendingDefectCount.isValueValid = true;
                            deviceStats->sasStatistics.pendingDefectCount.thresholdNotificationEnabled = tempLogBuf[iter + 2] & BIT4;//ETC bit
                            if (tempLogBuf[iter + 2] & BIT4)
                            {
                                switch ((tempLogBuf[iter + 2] & (BIT2 | BIT3)) >> 2)
                                {
                                case 3:
                                    deviceStats->sasStatistics.pendingDefectCount.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_GREATER;
                                    break;
                                case 2:
                                    deviceStats->sasStatistics.pendingDefectCount.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL;
                                    break;
                                case 1:
                                    deviceStats->sasStatistics.pendingDefectCount.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL;
                                    break;
                                case 0:
                                default:
                                    deviceStats->sasStatistics.pendingDefectCount.threshType = THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE;
                                    break;
                                }
                            }
                            deviceStats->sasStatistics.pendingDefectCount.statisticValue = M_BytesTo4ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            break;
                        default:
                            break;
                        }
                        if (parameterLength == 0)
                        {
                            break;
                        }
                    }
                    //thresholds
                    memset(tempLogBuf, 0, LEGACY_DRIVE_SEC_SIZE);
                    if (SUCCESS == scsi_Log_Sense_Cmd(device, false, LPC_THRESHOLD_VALUES, pageCode, subpageCode, 0x0000, tempLogBuf, LEGACY_DRIVE_SEC_SIZE))
                    {
                        uint16_t pageLength = M_BytesTo2ByteValue(tempLogBuf[2], tempLogBuf[3]);
                        uint8_t parameterLength = 0;
                        //loop through the data and gather the data from each parameter we care about getting.
                        for (uint16_t iter = 4; iter < pageLength && iter < LEGACY_DRIVE_SEC_SIZE; iter += (parameterLength + 4))
                        {
                            uint16_t parameterCode = M_BytesTo2ByteValue(tempLogBuf[iter], tempLogBuf[iter + 1]);
                            parameterLength = tempLogBuf[iter + 3];
                            switch (parameterCode)
                            {
                            case 0://pending defects count
                                deviceStats->sasStatistics.pendingDefectCount.supportsNotification = true;
                                if (tempLogBuf[iter + 2] & BIT4)
                                {
                                    deviceStats->sasStatistics.pendingDefectCount.isThresholdValid = true;
                                    deviceStats->sasStatistics.pendingDefectCount.threshold = M_BytesTo4ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
                                    scsi_Threshold_Comparison(&deviceStats->sasStatistics.pendingDefectCount);
                                }
                                break;
                            default:
                                break;
                            }
                            if (parameterLength == 0)
                            {
                                break;
                            }
                        }
                    }
                }
                break;
            case 2://background operaton
                break;
            case 3://lps misalignment
                memset(tempLogBuf, 0, LEGACY_DRIVE_SEC_SIZE);
                if (SUCCESS == scsi_Log_Sense_Cmd(device, false, LPC_CUMULATIVE_VALUES, pageCode, subpageCode, 0x0000, tempLogBuf, LEGACY_DRIVE_SEC_SIZE))
                {
                    deviceStats->sasStatistics.lpsMisalignmentSupported = true;
                    uint16_t pageLength = M_BytesTo2ByteValue(tempLogBuf[2], tempLogBuf[3]);
                    uint8_t parameterLength = 0;
                    //loop through the data and gather the data from each parameter we care about getting.
                    for (uint16_t iter = 4; iter < pageLength && iter < LEGACY_DRIVE_SEC_SIZE; iter += (parameterLength + 4))
                    {
                        uint16_t parameterCode = M_BytesTo2ByteValue(tempLogBuf[iter], tempLogBuf[iter + 1]);
                        parameterLength = tempLogBuf[iter + 3];
                        switch (parameterCode)
                        {
                        case 0://LPS misalignment count
                            deviceStats->sasStatistics.lpsMisalignmentCount.isSupported = true;
                            deviceStats->sasStatistics.lpsMisalignmentCount.isValueValid = true;
                            deviceStats->sasStatistics.lpsMisalignmentCount.thresholdNotificationEnabled = tempLogBuf[iter + 2] & BIT4;//ETC bit
                            if (tempLogBuf[iter + 2] & BIT4)
                            {
                                switch ((tempLogBuf[iter + 2] & (BIT2 | BIT3)) >> 2)
                                {
                                case 3:
                                    deviceStats->sasStatistics.lpsMisalignmentCount.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_GREATER;
                                    break;
                                case 2:
                                    deviceStats->sasStatistics.lpsMisalignmentCount.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL;
                                    break;
                                case 1:
                                    deviceStats->sasStatistics.lpsMisalignmentCount.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL;
                                    break;
                                case 0:
                                default:
                                    deviceStats->sasStatistics.lpsMisalignmentCount.threshType = THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE;
                                    break;
                                }
                            }
                            deviceStats->sasStatistics.lpsMisalignmentCount.statisticValue = M_BytesTo2ByteValue(tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            break;
                        default:
                            break;
                        }
                        if (parameterLength == 0)
                        {
                            break;
                        }
                    }
                    //thresholds
                    memset(tempLogBuf, 0, LEGACY_DRIVE_SEC_SIZE);
                    if (SUCCESS == scsi_Log_Sense_Cmd(device, false, LPC_THRESHOLD_VALUES, pageCode, subpageCode, 0x0000, tempLogBuf, LEGACY_DRIVE_SEC_SIZE))
                    {
                        uint16_t pageLength = M_BytesTo2ByteValue(tempLogBuf[2], tempLogBuf[3]);
                        uint8_t parameterLength = 0;
                        //loop through the data and gather the data from each parameter we care about getting.
                        for (uint16_t iter = 4; iter < pageLength && iter < LEGACY_DRIVE_SEC_SIZE; iter += (parameterLength + 4))
                        {
                            uint16_t parameterCode = M_BytesTo2ByteValue(tempLogBuf[iter], tempLogBuf[iter + 1]);
                            parameterLength = tempLogBuf[iter + 3];
                            switch (parameterCode)
                            {
                            case 0://LPS misalignment count
                                deviceStats->sasStatistics.lpsMisalignmentCount.supportsNotification = true;
                                if (tempLogBuf[iter + 2] & BIT4)
                                {
                                    deviceStats->sasStatistics.lpsMisalignmentCount.isThresholdValid = true;
                                    deviceStats->sasStatistics.lpsMisalignmentCount.threshold = M_BytesTo2ByteValue(tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
                                    scsi_Threshold_Comparison(&deviceStats->sasStatistics.lpsMisalignmentCount);
                                }
                                break;
                            default:
                                break;
                            }
                            if (parameterLength == 0)
                            {
                                break;
                            }
                        }
                    }
                }
                break;
            default:
                break;
            }
            break;
        case LP_NON_VOLITILE_CACHE:
            if (subpageCode == 0)
            {
                memset(tempLogBuf, 0, LEGACY_DRIVE_SEC_SIZE);
                if (SUCCESS == scsi_Log_Sense_Cmd(device, false, LPC_CUMULATIVE_VALUES, pageCode, subpageCode, 0x0000, tempLogBuf, LEGACY_DRIVE_SEC_SIZE))
                {
                    deviceStats->sasStatistics.nvCacheSupported = true;
                    uint16_t pageLength = M_BytesTo2ByteValue(tempLogBuf[2], tempLogBuf[3]);
                    uint8_t parameterLength = 0;
                    //loop through the data and gather the data from each parameter we care about getting.
                    for (uint16_t iter = 4; iter < pageLength && iter < LEGACY_DRIVE_SEC_SIZE; iter += (parameterLength + 4))
                    {
                        uint16_t parameterCode = M_BytesTo2ByteValue(tempLogBuf[iter], tempLogBuf[iter + 1]);
                        parameterLength = tempLogBuf[iter + 3];
                        switch (parameterCode)
                        {
                        case 0://remaining nonvolatile time
                            deviceStats->sasStatistics.remainingNonvolatileTime.isSupported = true;
                            deviceStats->sasStatistics.remainingNonvolatileTime.isValueValid = true;
                            deviceStats->sasStatistics.remainingNonvolatileTime.thresholdNotificationEnabled = tempLogBuf[iter + 2] & BIT4;//ETC bit
                            if (tempLogBuf[iter + 2] & BIT4)
                            {
                                switch ((tempLogBuf[iter + 2] & (BIT2 | BIT3)) >> 2)
                                {
                                case 3:
                                    deviceStats->sasStatistics.remainingNonvolatileTime.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_GREATER;
                                    break;
                                case 2:
                                    deviceStats->sasStatistics.remainingNonvolatileTime.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL;
                                    break;
                                case 1:
                                    deviceStats->sasStatistics.remainingNonvolatileTime.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL;
                                    break;
                                case 0:
                                default:
                                    deviceStats->sasStatistics.remainingNonvolatileTime.threshType = THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE;
                                    break;
                                }
                            }
                            deviceStats->sasStatistics.remainingNonvolatileTime.statisticValue = M_BytesTo4ByteValue(0, tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            break;
                        case 1://maximum nonvolatile time
                            deviceStats->sasStatistics.maximumNonvolatileTime.isSupported = true;
                            deviceStats->sasStatistics.maximumNonvolatileTime.isValueValid = true;
                            deviceStats->sasStatistics.maximumNonvolatileTime.thresholdNotificationEnabled = tempLogBuf[iter + 2] & BIT4;//ETC bit
                            if (tempLogBuf[iter + 2] & BIT4)
                            {
                                switch ((tempLogBuf[iter + 2] & (BIT2 | BIT3)) >> 2)
                                {
                                case 3:
                                    deviceStats->sasStatistics.maximumNonvolatileTime.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_GREATER;
                                    break;
                                case 2:
                                    deviceStats->sasStatistics.maximumNonvolatileTime.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL;
                                    break;
                                case 1:
                                    deviceStats->sasStatistics.maximumNonvolatileTime.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL;
                                    break;
                                case 0:
                                default:
                                    deviceStats->sasStatistics.maximumNonvolatileTime.threshType = THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE;
                                    break;
                                }
                            }
                            deviceStats->sasStatistics.maximumNonvolatileTime.statisticValue = M_BytesTo4ByteValue(0, tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            break;
                        default:
                            break;
                        }
                        if (parameterLength == 0)
                        {
                            break;
                        }
                    }
                    //thresholds
                    memset(tempLogBuf, 0, LEGACY_DRIVE_SEC_SIZE);
                    if (SUCCESS == scsi_Log_Sense_Cmd(device, false, LPC_THRESHOLD_VALUES, pageCode, subpageCode, 0x0000, tempLogBuf, LEGACY_DRIVE_SEC_SIZE))
                    {
                        uint16_t pageLength = M_BytesTo2ByteValue(tempLogBuf[2], tempLogBuf[3]);
                        uint8_t parameterLength = 0;
                        //loop through the data and gather the data from each parameter we care about getting.
                        for (uint16_t iter = 4; iter < pageLength && iter < LEGACY_DRIVE_SEC_SIZE; iter += (parameterLength + 4))
                        {
                            uint16_t parameterCode = M_BytesTo2ByteValue(tempLogBuf[iter], tempLogBuf[iter + 1]);
                            parameterLength = tempLogBuf[iter + 3];
                            switch (parameterCode)
                            {
                            case 0://remaining nonvolatile time
                                deviceStats->sasStatistics.remainingNonvolatileTime.supportsNotification = true;
                                if (tempLogBuf[iter + 2] & BIT4)
                                {
                                    deviceStats->sasStatistics.remainingNonvolatileTime.isThresholdValid = true;
                                    deviceStats->sasStatistics.remainingNonvolatileTime.threshold = M_BytesTo4ByteValue(0, tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
                                    scsi_Threshold_Comparison(&deviceStats->sasStatistics.remainingNonvolatileTime);
                                }
                                break;
                            case 1://maximum nonvolatile time
                                deviceStats->sasStatistics.maximumNonvolatileTime.supportsNotification = true;
                                if (tempLogBuf[iter + 2] & BIT4)
                                {
                                    deviceStats->sasStatistics.maximumNonvolatileTime.isThresholdValid = true;
                                    deviceStats->sasStatistics.maximumNonvolatileTime.threshold = M_BytesTo4ByteValue(0, tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
                                    scsi_Threshold_Comparison(&deviceStats->sasStatistics.maximumNonvolatileTime);
                                }
                                break;
                            default:
                                break;
                            }
                            if (parameterLength == 0)
                            {
                                break;
                            }
                        }
                    }
                }
            }
            break;
        case LP_GENERAL_STATISTICS_AND_PERFORMANCE:
            switch (subpageCode)
            {
            case 0://general statistics and performance
                memset(tempLogBuf, 0, LEGACY_DRIVE_SEC_SIZE);
                if (SUCCESS == scsi_Log_Sense_Cmd(device, false, LPC_CUMULATIVE_VALUES, pageCode, subpageCode, 0x0001, tempLogBuf, LEGACY_DRIVE_SEC_SIZE))
                {
                    deviceStats->sasStatistics.generalStatisticsAndPerformanceSupported = true;
                    uint16_t pageLength = M_BytesTo2ByteValue(tempLogBuf[2], tempLogBuf[3]);
                    uint8_t parameterLength = 0;
                    //loop through the data and gather the data from each parameter we care about getting.
                    for (uint16_t iter = 4; iter < pageLength && iter < LEGACY_DRIVE_SEC_SIZE; iter += (parameterLength + 4))
                    {
                        uint16_t parameterCode = M_BytesTo2ByteValue(tempLogBuf[iter], tempLogBuf[iter + 1]);
                        parameterLength = tempLogBuf[iter + 3];
                        switch (parameterCode)
                        {
                        case 1://general access statistics and performance
                            //thresholds for parameter
                            if (tempLogBuf[iter + 2] & BIT4)
                            {
                                deviceStats->sasStatistics.numberOfReadCommands.thresholdNotificationEnabled = true;//ETC bit
                                deviceStats->sasStatistics.numberOfWriteCommands.thresholdNotificationEnabled = true;//ETC bit
                                deviceStats->sasStatistics.numberOfLogicalBlocksReceived.thresholdNotificationEnabled = true;//ETC bit
                                deviceStats->sasStatistics.numberOfLogicalBlocksTransmitted.thresholdNotificationEnabled = true;//ETC bit
                                deviceStats->sasStatistics.readCommandProcessingIntervals.thresholdNotificationEnabled = true;//ETC bit
                                deviceStats->sasStatistics.writeCommandProcessingIntervals.thresholdNotificationEnabled = true;//ETC bit
                                deviceStats->sasStatistics.weightedNumberOfReadCommandsPlusWriteCommands.thresholdNotificationEnabled = true;//ETC bit
                                deviceStats->sasStatistics.weightedReadCommandProcessingPlusWriteCommandProcessing.thresholdNotificationEnabled = true;//ETC bit
                                switch ((tempLogBuf[iter + 2] & (BIT2 | BIT3)) >> 2)
                                {
                                case 3:
                                    deviceStats->sasStatistics.numberOfReadCommands.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_GREATER;
                                    deviceStats->sasStatistics.numberOfWriteCommands.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_GREATER;
                                    deviceStats->sasStatistics.numberOfLogicalBlocksReceived.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_GREATER;
                                    deviceStats->sasStatistics.numberOfLogicalBlocksTransmitted.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_GREATER;
                                    deviceStats->sasStatistics.readCommandProcessingIntervals.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_GREATER;
                                    deviceStats->sasStatistics.writeCommandProcessingIntervals.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_GREATER;
                                    deviceStats->sasStatistics.weightedNumberOfReadCommandsPlusWriteCommands.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_GREATER;
                                    deviceStats->sasStatistics.weightedReadCommandProcessingPlusWriteCommandProcessing.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_GREATER;
                                    break;
                                case 2:
                                    deviceStats->sasStatistics.numberOfReadCommands.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL;
                                    deviceStats->sasStatistics.numberOfWriteCommands.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL;
                                    deviceStats->sasStatistics.numberOfLogicalBlocksReceived.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL;
                                    deviceStats->sasStatistics.numberOfLogicalBlocksTransmitted.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL;
                                    deviceStats->sasStatistics.readCommandProcessingIntervals.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL;
                                    deviceStats->sasStatistics.writeCommandProcessingIntervals.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL;
                                    deviceStats->sasStatistics.weightedNumberOfReadCommandsPlusWriteCommands.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL;
                                    deviceStats->sasStatistics.weightedReadCommandProcessingPlusWriteCommandProcessing.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL;
                                    break;
                                case 1:
                                    deviceStats->sasStatistics.numberOfReadCommands.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL;
                                    deviceStats->sasStatistics.numberOfWriteCommands.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL;
                                    deviceStats->sasStatistics.numberOfLogicalBlocksReceived.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL;
                                    deviceStats->sasStatistics.numberOfLogicalBlocksTransmitted.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL;
                                    deviceStats->sasStatistics.readCommandProcessingIntervals.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL;
                                    deviceStats->sasStatistics.writeCommandProcessingIntervals.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL;
                                    deviceStats->sasStatistics.weightedNumberOfReadCommandsPlusWriteCommands.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL;
                                    deviceStats->sasStatistics.weightedReadCommandProcessingPlusWriteCommandProcessing.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL;
                                    break;
                                case 0:
                                default:
                                    deviceStats->sasStatistics.numberOfReadCommands.threshType = THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE;
                                    deviceStats->sasStatistics.numberOfWriteCommands.threshType = THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE;
                                    deviceStats->sasStatistics.numberOfLogicalBlocksReceived.threshType = THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE;
                                    deviceStats->sasStatistics.numberOfLogicalBlocksTransmitted.threshType = THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE;
                                    deviceStats->sasStatistics.readCommandProcessingIntervals.threshType = THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE;
                                    deviceStats->sasStatistics.writeCommandProcessingIntervals.threshType = THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE;
                                    deviceStats->sasStatistics.weightedNumberOfReadCommandsPlusWriteCommands.threshType = THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE;
                                    deviceStats->sasStatistics.weightedReadCommandProcessingPlusWriteCommandProcessing.threshType = THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE;
                                    break;
                                }
                            }
                            //number of read commands
                            deviceStats->sasStatistics.numberOfReadCommands.isSupported = true;
                            deviceStats->sasStatistics.numberOfReadCommands.isValueValid = true;
                            deviceStats->sasStatistics.numberOfReadCommands.statisticValue = M_BytesTo8ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7], tempLogBuf[iter + 8], tempLogBuf[iter + 9], tempLogBuf[iter + 10], tempLogBuf[iter + 11]);
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            //number of write commands
                            deviceStats->sasStatistics.numberOfWriteCommands.isSupported = true;
                            deviceStats->sasStatistics.numberOfWriteCommands.isValueValid = true;
                            deviceStats->sasStatistics.numberOfWriteCommands.statisticValue = M_BytesTo8ByteValue(tempLogBuf[iter + 12], tempLogBuf[iter + 13], tempLogBuf[iter + 14], tempLogBuf[iter + 15], tempLogBuf[iter + 16], tempLogBuf[iter + 17], tempLogBuf[iter + 18], tempLogBuf[iter + 19]);
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            //number of logical blocks received
                            deviceStats->sasStatistics.numberOfLogicalBlocksReceived.isSupported = true;
                            deviceStats->sasStatistics.numberOfLogicalBlocksReceived.isValueValid = true;
                            deviceStats->sasStatistics.numberOfLogicalBlocksReceived.statisticValue = M_BytesTo8ByteValue(tempLogBuf[iter + 20], tempLogBuf[iter + 21], tempLogBuf[iter + 22], tempLogBuf[iter + 23], tempLogBuf[iter + 24], tempLogBuf[iter + 25], tempLogBuf[iter + 26], tempLogBuf[iter + 27]);
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            //number of logical blocks transmitted
                            deviceStats->sasStatistics.numberOfLogicalBlocksTransmitted.isSupported = true;
                            deviceStats->sasStatistics.numberOfLogicalBlocksTransmitted.isValueValid = true;
                            deviceStats->sasStatistics.numberOfLogicalBlocksTransmitted.statisticValue = M_BytesTo8ByteValue(tempLogBuf[iter + 28], tempLogBuf[iter + 29], tempLogBuf[iter + 30], tempLogBuf[iter + 31], tempLogBuf[iter + 32], tempLogBuf[iter + 33], tempLogBuf[iter + 34], tempLogBuf[iter + 35]);
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            //read command processing intervals
                            deviceStats->sasStatistics.readCommandProcessingIntervals.isSupported = true;
                            deviceStats->sasStatistics.readCommandProcessingIntervals.isValueValid = true;
                            deviceStats->sasStatistics.readCommandProcessingIntervals.statisticValue = M_BytesTo8ByteValue(tempLogBuf[iter + 36], tempLogBuf[iter + 37], tempLogBuf[iter + 38], tempLogBuf[iter + 39], tempLogBuf[iter + 40], tempLogBuf[iter + 41], tempLogBuf[iter + 42], tempLogBuf[iter + 43]);
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            //write command processing intervals
                            deviceStats->sasStatistics.writeCommandProcessingIntervals.isSupported = true;
                            deviceStats->sasStatistics.writeCommandProcessingIntervals.isValueValid = true;
                            deviceStats->sasStatistics.writeCommandProcessingIntervals.statisticValue = M_BytesTo8ByteValue(tempLogBuf[iter + 44], tempLogBuf[iter + 45], tempLogBuf[iter + 46], tempLogBuf[iter + 47], tempLogBuf[iter + 48], tempLogBuf[iter + 49], tempLogBuf[iter + 50], tempLogBuf[iter + 51]);
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            //weighted number of read commands plus write commansd
                            deviceStats->sasStatistics.weightedNumberOfReadCommandsPlusWriteCommands.isSupported = true;
                            deviceStats->sasStatistics.weightedNumberOfReadCommandsPlusWriteCommands.isValueValid = true;
                            deviceStats->sasStatistics.weightedNumberOfReadCommandsPlusWriteCommands.statisticValue = M_BytesTo8ByteValue(tempLogBuf[iter + 52], tempLogBuf[iter + 53], tempLogBuf[iter + 54], tempLogBuf[iter + 55], tempLogBuf[iter + 56], tempLogBuf[iter + 57], tempLogBuf[iter + 58], tempLogBuf[iter + 59]);
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            //weighted number of read command processing plus write command processing
                            deviceStats->sasStatistics.weightedReadCommandProcessingPlusWriteCommandProcessing.isSupported = true;
                            deviceStats->sasStatistics.weightedReadCommandProcessingPlusWriteCommandProcessing.isValueValid = true;
                            deviceStats->sasStatistics.weightedReadCommandProcessingPlusWriteCommandProcessing.statisticValue = M_BytesTo8ByteValue(tempLogBuf[iter + 60], tempLogBuf[iter + 61], tempLogBuf[iter + 62], tempLogBuf[iter + 63], tempLogBuf[iter + 64], tempLogBuf[iter + 65], tempLogBuf[iter + 66], tempLogBuf[iter + 67]);
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            break;
                        case 2://idle time
                            deviceStats->sasStatistics.idleTimeIntervals.isSupported = true;
                            deviceStats->sasStatistics.idleTimeIntervals.isValueValid = true;
                            deviceStats->sasStatistics.idleTimeIntervals.thresholdNotificationEnabled = tempLogBuf[iter + 2] & BIT4;//ETC bit
                            if (tempLogBuf[iter + 2] & BIT4)
                            {
                                switch ((tempLogBuf[iter + 2] & (BIT2 | BIT3)) >> 2)
                                {
                                case 3:
                                    deviceStats->sasStatistics.idleTimeIntervals.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_GREATER;
                                    break;
                                case 2:
                                    deviceStats->sasStatistics.idleTimeIntervals.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL;
                                    break;
                                case 1:
                                    deviceStats->sasStatistics.idleTimeIntervals.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL;
                                    break;
                                case 0:
                                default:
                                    deviceStats->sasStatistics.idleTimeIntervals.threshType = THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE;
                                    break;
                                }
                            }
                            deviceStats->sasStatistics.idleTimeIntervals.statisticValue = M_BytesTo8ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7], tempLogBuf[iter + 8], tempLogBuf[iter + 9], tempLogBuf[iter + 10], tempLogBuf[iter + 11]);
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            break;
                        case 3://time interval
                            deviceStats->sasStatistics.timeIntervalDescriptor.isSupported = true;
                            deviceStats->sasStatistics.timeIntervalDescriptor.isValueValid = true;
                            deviceStats->sasStatistics.timeIntervalDescriptor.thresholdNotificationEnabled = tempLogBuf[iter + 2] & BIT4;//ETC bit
                            if (tempLogBuf[iter + 2] & BIT4)
                            {
                                switch ((tempLogBuf[iter + 2] & (BIT2 | BIT3)) >> 2)
                                {
                                case 3:
                                    deviceStats->sasStatistics.timeIntervalDescriptor.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_GREATER;
                                    break;
                                case 2:
                                    deviceStats->sasStatistics.timeIntervalDescriptor.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL;
                                    break;
                                case 1:
                                    deviceStats->sasStatistics.timeIntervalDescriptor.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL;
                                    break;
                                case 0:
                                default:
                                    deviceStats->sasStatistics.timeIntervalDescriptor.threshType = THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE;
                                    break;
                                }
                            }
                            deviceStats->sasStatistics.timeIntervalDescriptor.statisticValue = M_BytesTo8ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7], tempLogBuf[iter + 8], tempLogBuf[iter + 9], tempLogBuf[iter + 10], tempLogBuf[iter + 11]);
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            break;
                        case 4://force unit access statistics and performance
                            if (tempLogBuf[iter + 2] & BIT4)
                            {
                                deviceStats->sasStatistics.numberOfReadFUACommands.thresholdNotificationEnabled = true;//ETC bit
                                deviceStats->sasStatistics.numberOfWriteFUACommands.thresholdNotificationEnabled = true;//ETC bit
                                deviceStats->sasStatistics.numberOfReadFUANVCommands.thresholdNotificationEnabled = true;//ETC bit
                                deviceStats->sasStatistics.numberOfWriteFUANVCommands.thresholdNotificationEnabled = true;//ETC bit
                                deviceStats->sasStatistics.readFUACommandProcessingIntervals.thresholdNotificationEnabled = true;//ETC bit
                                deviceStats->sasStatistics.writeFUACommandProcessingIntervals.thresholdNotificationEnabled = true;//ETC bit
                                deviceStats->sasStatistics.readFUANVCommandProcessingIntervals.thresholdNotificationEnabled = true;//ETC bit
                                deviceStats->sasStatistics.writeFUANVCommandProcessingIntervals.thresholdNotificationEnabled = true;//ETC bit
                                switch ((tempLogBuf[iter + 2] & (BIT2 | BIT3)) >> 2)
                                {
                                case 3:
                                    deviceStats->sasStatistics.numberOfReadFUACommands.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_GREATER;
                                    deviceStats->sasStatistics.numberOfWriteFUACommands.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_GREATER;
                                    deviceStats->sasStatistics.numberOfReadFUANVCommands.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_GREATER;
                                    deviceStats->sasStatistics.numberOfWriteFUANVCommands.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_GREATER;
                                    deviceStats->sasStatistics.readFUACommandProcessingIntervals.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_GREATER;
                                    deviceStats->sasStatistics.writeFUACommandProcessingIntervals.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_GREATER;
                                    deviceStats->sasStatistics.readFUANVCommandProcessingIntervals.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_GREATER;
                                    deviceStats->sasStatistics.writeFUANVCommandProcessingIntervals.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_GREATER;
                                    break;
                                case 2:
                                    deviceStats->sasStatistics.numberOfReadFUACommands.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL;
                                    deviceStats->sasStatistics.numberOfWriteFUACommands.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL;
                                    deviceStats->sasStatistics.numberOfReadFUANVCommands.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL;
                                    deviceStats->sasStatistics.numberOfWriteFUANVCommands.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL;
                                    deviceStats->sasStatistics.readFUACommandProcessingIntervals.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL;
                                    deviceStats->sasStatistics.writeFUACommandProcessingIntervals.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL;
                                    deviceStats->sasStatistics.readFUANVCommandProcessingIntervals.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL;
                                    deviceStats->sasStatistics.writeFUANVCommandProcessingIntervals.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL;
                                    break;
                                case 1:
                                    deviceStats->sasStatistics.numberOfReadFUACommands.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL;
                                    deviceStats->sasStatistics.numberOfWriteFUACommands.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL;
                                    deviceStats->sasStatistics.numberOfReadFUANVCommands.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL;
                                    deviceStats->sasStatistics.numberOfWriteFUANVCommands.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL;
                                    deviceStats->sasStatistics.readFUACommandProcessingIntervals.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL;
                                    deviceStats->sasStatistics.writeFUACommandProcessingIntervals.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL;
                                    deviceStats->sasStatistics.readFUANVCommandProcessingIntervals.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL;
                                    deviceStats->sasStatistics.writeFUANVCommandProcessingIntervals.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL;
                                    break;
                                case 0:
                                default:
                                    deviceStats->sasStatistics.numberOfReadFUACommands.threshType = THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE;
                                    deviceStats->sasStatistics.numberOfWriteFUACommands.threshType = THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE;
                                    deviceStats->sasStatistics.numberOfReadFUANVCommands.threshType = THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE;
                                    deviceStats->sasStatistics.numberOfWriteFUANVCommands.threshType = THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE;
                                    deviceStats->sasStatistics.readFUACommandProcessingIntervals.threshType = THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE;
                                    deviceStats->sasStatistics.writeFUACommandProcessingIntervals.threshType = THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE;
                                    deviceStats->sasStatistics.readFUANVCommandProcessingIntervals.threshType = THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE;
                                    deviceStats->sasStatistics.writeFUANVCommandProcessingIntervals.threshType = THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE;
                                    break;
                                }
                            }
                            //number of read fua commands
                            deviceStats->sasStatistics.numberOfReadFUACommands.isSupported = true;
                            deviceStats->sasStatistics.numberOfReadFUACommands.isValueValid = true;
                            deviceStats->sasStatistics.numberOfReadFUACommands.statisticValue = M_BytesTo8ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7], tempLogBuf[iter + 8], tempLogBuf[iter + 9], tempLogBuf[iter + 10], tempLogBuf[iter + 11]);
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            //number of write fua commands
                            deviceStats->sasStatistics.numberOfWriteFUACommands.isSupported = true;
                            deviceStats->sasStatistics.numberOfWriteFUACommands.isValueValid = true;
                            deviceStats->sasStatistics.numberOfWriteFUACommands.statisticValue = M_BytesTo8ByteValue(tempLogBuf[iter + 12], tempLogBuf[iter + 13], tempLogBuf[iter + 14], tempLogBuf[iter + 15], tempLogBuf[iter + 16], tempLogBuf[iter + 17], tempLogBuf[iter + 18], tempLogBuf[iter + 19]);
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            //number of read fua_nv commands
                            deviceStats->sasStatistics.numberOfReadFUANVCommands.isSupported = true;
                            deviceStats->sasStatistics.numberOfReadFUANVCommands.isValueValid = true;
                            deviceStats->sasStatistics.numberOfReadFUANVCommands.statisticValue = M_BytesTo8ByteValue(tempLogBuf[iter + 20], tempLogBuf[iter + 21], tempLogBuf[iter + 22], tempLogBuf[iter + 23], tempLogBuf[iter + 24], tempLogBuf[iter + 25], tempLogBuf[iter + 26], tempLogBuf[iter + 27]);
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            //number of write fua_nv commands
                            deviceStats->sasStatistics.numberOfWriteFUANVCommands.isSupported = true;
                            deviceStats->sasStatistics.numberOfWriteFUANVCommands.isValueValid = true;
                            deviceStats->sasStatistics.numberOfWriteFUANVCommands.statisticValue = M_BytesTo8ByteValue(tempLogBuf[iter + 28], tempLogBuf[iter + 29], tempLogBuf[iter + 30], tempLogBuf[iter + 31], tempLogBuf[iter + 32], tempLogBuf[iter + 33], tempLogBuf[iter + 34], tempLogBuf[iter + 35]);
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            //read fua command processing intervals
                            deviceStats->sasStatistics.readFUACommandProcessingIntervals.isSupported = true;
                            deviceStats->sasStatistics.readFUACommandProcessingIntervals.isValueValid = true;
                            deviceStats->sasStatistics.readFUACommandProcessingIntervals.statisticValue = M_BytesTo8ByteValue(tempLogBuf[iter + 36], tempLogBuf[iter + 37], tempLogBuf[iter + 38], tempLogBuf[iter + 39], tempLogBuf[iter + 40], tempLogBuf[iter + 41], tempLogBuf[iter + 42], tempLogBuf[iter + 43]);
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            //write fua command processing intervals
                            deviceStats->sasStatistics.writeFUACommandProcessingIntervals.isSupported = true;
                            deviceStats->sasStatistics.writeFUACommandProcessingIntervals.isValueValid = true;
                            deviceStats->sasStatistics.writeFUACommandProcessingIntervals.statisticValue = M_BytesTo8ByteValue(tempLogBuf[iter + 44], tempLogBuf[iter + 45], tempLogBuf[iter + 46], tempLogBuf[iter + 47], tempLogBuf[iter + 48], tempLogBuf[iter + 49], tempLogBuf[iter + 50], tempLogBuf[iter + 51]);
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            //read fua_nv command processing intervals
                            deviceStats->sasStatistics.readFUANVCommandProcessingIntervals.isSupported = true;
                            deviceStats->sasStatistics.readFUANVCommandProcessingIntervals.isValueValid = true;
                            deviceStats->sasStatistics.readFUANVCommandProcessingIntervals.statisticValue = M_BytesTo8ByteValue(tempLogBuf[iter + 52], tempLogBuf[iter + 53], tempLogBuf[iter + 54], tempLogBuf[iter + 55], tempLogBuf[iter + 56], tempLogBuf[iter + 57], tempLogBuf[iter + 58], tempLogBuf[iter + 59]);
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            //write fua_nv command processing intervals
                            deviceStats->sasStatistics.writeFUANVCommandProcessingIntervals.isSupported = true;
                            deviceStats->sasStatistics.writeFUANVCommandProcessingIntervals.isValueValid = true;
                            deviceStats->sasStatistics.writeFUANVCommandProcessingIntervals.statisticValue = M_BytesTo8ByteValue(tempLogBuf[iter + 60], tempLogBuf[iter + 61], tempLogBuf[iter + 62], tempLogBuf[iter + 63], tempLogBuf[iter + 64], tempLogBuf[iter + 65], tempLogBuf[iter + 66], tempLogBuf[iter + 67]);
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            break;
                        default:
                            break;
                        }
                        if (parameterLength == 0)
                        {
                            break;
                        }
                    }
                    //thresholds
                    memset(tempLogBuf, 0, LEGACY_DRIVE_SEC_SIZE);
                    if (SUCCESS == scsi_Log_Sense_Cmd(device, false, LPC_THRESHOLD_VALUES, pageCode, subpageCode, 0x0001, tempLogBuf, LEGACY_DRIVE_SEC_SIZE))
                    {
                        uint16_t pageLength = M_BytesTo2ByteValue(tempLogBuf[2], tempLogBuf[3]);
                        uint8_t parameterLength = 0;
                        //loop through the data and gather the data from each parameter we care about getting.
                        for (uint16_t iter = 4; iter < pageLength && iter < LEGACY_DRIVE_SEC_SIZE; iter += (parameterLength + 4))
                        {
                            uint16_t parameterCode = M_BytesTo2ByteValue(tempLogBuf[iter], tempLogBuf[iter + 1]);
                            parameterLength = tempLogBuf[iter + 3];
                            switch (parameterCode)
                            {
                            case 1://general access statistics and performance
                                deviceStats->sasStatistics.numberOfReadCommands.supportsNotification = true;
                                deviceStats->sasStatistics.numberOfWriteCommands.supportsNotification = true;
                                deviceStats->sasStatistics.numberOfLogicalBlocksReceived.supportsNotification = true;
                                deviceStats->sasStatistics.numberOfLogicalBlocksTransmitted.supportsNotification = true;
                                deviceStats->sasStatistics.readCommandProcessingIntervals.supportsNotification = true;
                                deviceStats->sasStatistics.writeCommandProcessingIntervals.supportsNotification = true;
                                deviceStats->sasStatistics.weightedNumberOfReadCommandsPlusWriteCommands.supportsNotification = true;
                                deviceStats->sasStatistics.weightedReadCommandProcessingPlusWriteCommandProcessing.supportsNotification = true;
                                if (tempLogBuf[iter + 2] & BIT4)
                                {
                                    //number of read commands
                                    deviceStats->sasStatistics.numberOfReadCommands.isThresholdValid = true;
                                    deviceStats->sasStatistics.numberOfReadCommands.threshold = M_BytesTo8ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7], tempLogBuf[iter + 8], tempLogBuf[iter + 9], tempLogBuf[iter + 10], tempLogBuf[iter + 11]);
                                    scsi_Threshold_Comparison(&deviceStats->sasStatistics.numberOfReadCommands);
                                    //number of write commands
                                    deviceStats->sasStatistics.numberOfWriteCommands.isThresholdValid = true;
                                    deviceStats->sasStatistics.numberOfWriteCommands.threshold = M_BytesTo8ByteValue(tempLogBuf[iter + 12], tempLogBuf[iter + 13], tempLogBuf[iter + 14], tempLogBuf[iter + 15], tempLogBuf[iter + 16], tempLogBuf[iter + 17], tempLogBuf[iter + 18], tempLogBuf[iter + 19]);
                                    scsi_Threshold_Comparison(&deviceStats->sasStatistics.numberOfWriteCommands);
                                    //number of logical blocks received
                                    deviceStats->sasStatistics.numberOfLogicalBlocksReceived.isThresholdValid = true;
                                    deviceStats->sasStatistics.numberOfLogicalBlocksReceived.threshold = M_BytesTo8ByteValue(tempLogBuf[iter + 20], tempLogBuf[iter + 21], tempLogBuf[iter + 22], tempLogBuf[iter + 23], tempLogBuf[iter + 24], tempLogBuf[iter + 25], tempLogBuf[iter + 26], tempLogBuf[iter + 27]);
                                    scsi_Threshold_Comparison(&deviceStats->sasStatistics.numberOfLogicalBlocksReceived);
                                    //number of logical blocks transmitted
                                    deviceStats->sasStatistics.numberOfLogicalBlocksTransmitted.isThresholdValid = true;
                                    deviceStats->sasStatistics.numberOfLogicalBlocksTransmitted.threshold = M_BytesTo8ByteValue(tempLogBuf[iter + 28], tempLogBuf[iter + 29], tempLogBuf[iter + 30], tempLogBuf[iter + 31], tempLogBuf[iter + 32], tempLogBuf[iter + 33], tempLogBuf[iter + 34], tempLogBuf[iter + 35]);
                                    scsi_Threshold_Comparison(&deviceStats->sasStatistics.numberOfLogicalBlocksTransmitted);
                                    //read command processing intervals
                                    deviceStats->sasStatistics.readCommandProcessingIntervals.isThresholdValid = true;
                                    deviceStats->sasStatistics.readCommandProcessingIntervals.threshold = M_BytesTo8ByteValue(tempLogBuf[iter + 36], tempLogBuf[iter + 37], tempLogBuf[iter + 38], tempLogBuf[iter + 39], tempLogBuf[iter + 40], tempLogBuf[iter + 41], tempLogBuf[iter + 42], tempLogBuf[iter + 43]);
                                    scsi_Threshold_Comparison(&deviceStats->sasStatistics.readCommandProcessingIntervals);
                                    //write command processing intervals
                                    deviceStats->sasStatistics.writeCommandProcessingIntervals.isThresholdValid = true;
                                    deviceStats->sasStatistics.writeCommandProcessingIntervals.threshold = M_BytesTo8ByteValue(tempLogBuf[iter + 44], tempLogBuf[iter + 45], tempLogBuf[iter + 46], tempLogBuf[iter + 47], tempLogBuf[iter + 48], tempLogBuf[iter + 49], tempLogBuf[iter + 50], tempLogBuf[iter + 51]);
                                    scsi_Threshold_Comparison(&deviceStats->sasStatistics.writeCommandProcessingIntervals);
                                    //weighted number of read commands plus write commansd
                                    deviceStats->sasStatistics.weightedNumberOfReadCommandsPlusWriteCommands.isThresholdValid = true;
                                    deviceStats->sasStatistics.weightedNumberOfReadCommandsPlusWriteCommands.threshold = M_BytesTo8ByteValue(tempLogBuf[iter + 52], tempLogBuf[iter + 53], tempLogBuf[iter + 54], tempLogBuf[iter + 55], tempLogBuf[iter + 56], tempLogBuf[iter + 57], tempLogBuf[iter + 58], tempLogBuf[iter + 59]);
                                    scsi_Threshold_Comparison(&deviceStats->sasStatistics.weightedNumberOfReadCommandsPlusWriteCommands);
                                    //weighted number of read command processing plus write command processing
                                    deviceStats->sasStatistics.weightedReadCommandProcessingPlusWriteCommandProcessing.isThresholdValid = true;
                                    deviceStats->sasStatistics.weightedReadCommandProcessingPlusWriteCommandProcessing.threshold = M_BytesTo8ByteValue(tempLogBuf[iter + 60], tempLogBuf[iter + 61], tempLogBuf[iter + 62], tempLogBuf[iter + 63], tempLogBuf[iter + 64], tempLogBuf[iter + 65], tempLogBuf[iter + 66], tempLogBuf[iter + 67]);
                                    scsi_Threshold_Comparison(&deviceStats->sasStatistics.weightedReadCommandProcessingPlusWriteCommandProcessing);
                                }
                                break;
                            case 2://idle time
                                deviceStats->sasStatistics.idleTimeIntervals.supportsNotification = true;
                                if (tempLogBuf[iter + 2] & BIT4)
                                {
                                    deviceStats->sasStatistics.idleTimeIntervals.isThresholdValid = true;
                                    deviceStats->sasStatistics.idleTimeIntervals.threshold = M_BytesTo8ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7], tempLogBuf[iter + 8], tempLogBuf[iter + 9], tempLogBuf[iter + 10], tempLogBuf[iter + 11]);
                                    scsi_Threshold_Comparison(&deviceStats->sasStatistics.idleTimeIntervals);
                                }
                                break;
                            case 3://time interval
                                deviceStats->sasStatistics.timeIntervalDescriptor.supportsNotification = true;
                                if (tempLogBuf[iter + 2] & BIT4)
                                {
                                    deviceStats->sasStatistics.timeIntervalDescriptor.isThresholdValid = true;
                                    deviceStats->sasStatistics.timeIntervalDescriptor.threshold = M_BytesTo8ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7], tempLogBuf[iter + 8], tempLogBuf[iter + 9], tempLogBuf[iter + 10], tempLogBuf[iter + 11]);
                                    scsi_Threshold_Comparison(&deviceStats->sasStatistics.timeIntervalDescriptor);
                                }
                                break;
                            case 4://force unit access statistics and performance
                                deviceStats->sasStatistics.numberOfReadFUACommands.supportsNotification = true;
                                deviceStats->sasStatistics.numberOfWriteFUACommands.supportsNotification = true;
                                deviceStats->sasStatistics.numberOfReadFUANVCommands.supportsNotification = true;
                                deviceStats->sasStatistics.numberOfWriteFUANVCommands.supportsNotification = true;
                                deviceStats->sasStatistics.readFUACommandProcessingIntervals.supportsNotification = true;
                                deviceStats->sasStatistics.writeFUACommandProcessingIntervals.supportsNotification = true;
                                deviceStats->sasStatistics.readFUANVCommandProcessingIntervals.supportsNotification = true;
                                deviceStats->sasStatistics.writeFUANVCommandProcessingIntervals.supportsNotification = true;
                                if (tempLogBuf[iter + 2] & BIT4)
                                {
                                    //number of read fua commands
                                    deviceStats->sasStatistics.numberOfReadFUACommands.isThresholdValid = true;
                                    deviceStats->sasStatistics.numberOfReadFUACommands.threshold = M_BytesTo8ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7], tempLogBuf[iter + 8], tempLogBuf[iter + 9], tempLogBuf[iter + 10], tempLogBuf[iter + 11]);
                                    scsi_Threshold_Comparison(&deviceStats->sasStatistics.numberOfReadFUACommands);
                                    //number of write fua commands
                                    deviceStats->sasStatistics.numberOfWriteFUACommands.isThresholdValid = true;
                                    deviceStats->sasStatistics.numberOfWriteFUACommands.threshold = M_BytesTo8ByteValue(tempLogBuf[iter + 12], tempLogBuf[iter + 13], tempLogBuf[iter + 14], tempLogBuf[iter + 15], tempLogBuf[iter + 16], tempLogBuf[iter + 17], tempLogBuf[iter + 18], tempLogBuf[iter + 19]);
                                    scsi_Threshold_Comparison(&deviceStats->sasStatistics.numberOfWriteFUACommands);
                                    //number of read fua_nv commands
                                    deviceStats->sasStatistics.numberOfReadFUANVCommands.isThresholdValid = true;
                                    deviceStats->sasStatistics.numberOfReadFUANVCommands.threshold = M_BytesTo8ByteValue(tempLogBuf[iter + 20], tempLogBuf[iter + 21], tempLogBuf[iter + 22], tempLogBuf[iter + 23], tempLogBuf[iter + 24], tempLogBuf[iter + 25], tempLogBuf[iter + 26], tempLogBuf[iter + 27]);
                                    scsi_Threshold_Comparison(&deviceStats->sasStatistics.numberOfReadFUANVCommands);
                                    //number of write fua_nv commands
                                    deviceStats->sasStatistics.numberOfWriteFUANVCommands.isThresholdValid = true;
                                    deviceStats->sasStatistics.numberOfWriteFUANVCommands.threshold = M_BytesTo8ByteValue(tempLogBuf[iter + 28], tempLogBuf[iter + 29], tempLogBuf[iter + 30], tempLogBuf[iter + 31], tempLogBuf[iter + 32], tempLogBuf[iter + 33], tempLogBuf[iter + 34], tempLogBuf[iter + 35]);
                                    scsi_Threshold_Comparison(&deviceStats->sasStatistics.numberOfWriteFUANVCommands);
                                    //read fua command processing intervals
                                    deviceStats->sasStatistics.readFUACommandProcessingIntervals.isThresholdValid = true;
                                    deviceStats->sasStatistics.readFUACommandProcessingIntervals.threshold = M_BytesTo8ByteValue(tempLogBuf[iter + 36], tempLogBuf[iter + 37], tempLogBuf[iter + 38], tempLogBuf[iter + 39], tempLogBuf[iter + 40], tempLogBuf[iter + 41], tempLogBuf[iter + 42], tempLogBuf[iter + 43]);
                                    scsi_Threshold_Comparison(&deviceStats->sasStatistics.readFUACommandProcessingIntervals);
                                    //write fua command processing intervals
                                    deviceStats->sasStatistics.writeFUACommandProcessingIntervals.isThresholdValid = true;
                                    deviceStats->sasStatistics.writeFUACommandProcessingIntervals.threshold = M_BytesTo8ByteValue(tempLogBuf[iter + 44], tempLogBuf[iter + 45], tempLogBuf[iter + 46], tempLogBuf[iter + 47], tempLogBuf[iter + 48], tempLogBuf[iter + 49], tempLogBuf[iter + 50], tempLogBuf[iter + 51]);
                                    scsi_Threshold_Comparison(&deviceStats->sasStatistics.writeFUACommandProcessingIntervals);
                                    //read fua_nv command processing intervals
                                    deviceStats->sasStatistics.readFUANVCommandProcessingIntervals.isThresholdValid = true;
                                    deviceStats->sasStatistics.readFUANVCommandProcessingIntervals.threshold = M_BytesTo8ByteValue(tempLogBuf[iter + 52], tempLogBuf[iter + 53], tempLogBuf[iter + 54], tempLogBuf[iter + 55], tempLogBuf[iter + 56], tempLogBuf[iter + 57], tempLogBuf[iter + 58], tempLogBuf[iter + 59]);
                                    scsi_Threshold_Comparison(&deviceStats->sasStatistics.readFUANVCommandProcessingIntervals);
                                    //write fua_nv command processing intervals
                                    deviceStats->sasStatistics.writeFUANVCommandProcessingIntervals.isThresholdValid = true;
                                    deviceStats->sasStatistics.writeFUANVCommandProcessingIntervals.threshold = M_BytesTo8ByteValue(tempLogBuf[iter + 60], tempLogBuf[iter + 61], tempLogBuf[iter + 62], tempLogBuf[iter + 63], tempLogBuf[iter + 64], tempLogBuf[iter + 65], tempLogBuf[iter + 66], tempLogBuf[iter + 67]);
                                    scsi_Threshold_Comparison(&deviceStats->sasStatistics.writeFUANVCommandProcessingIntervals);
                                }
                                break;
                            default:
                                break;
                            }
                            if (parameterLength == 0)
                            {
                                break;
                            }
                        }
                    }

                }
                break;
                //group statistics (1 - 1f)
            case 0x20://cache memory statistics
                memset(tempLogBuf, 0, LEGACY_DRIVE_SEC_SIZE);
                if (SUCCESS == scsi_Log_Sense_Cmd(device, false, LPC_CUMULATIVE_VALUES, pageCode, subpageCode, 0x0001, tempLogBuf, LEGACY_DRIVE_SEC_SIZE))
                {
                    deviceStats->sasStatistics.cacheMemoryStatisticsSupported = true;
                    uint16_t pageLength = M_BytesTo2ByteValue(tempLogBuf[2], tempLogBuf[3]);
                    uint8_t parameterLength = 0;
                    //loop through the data and gather the data from each parameter we care about getting.
                    for (uint16_t iter = 4; iter < pageLength && iter < LEGACY_DRIVE_SEC_SIZE; iter += (parameterLength + 4))
                    {
                        uint16_t parameterCode = M_BytesTo2ByteValue(tempLogBuf[iter], tempLogBuf[iter + 1]);
                        parameterLength = tempLogBuf[iter + 3];
                        switch (parameterCode)
                        {
                        case 1://read cache memory hits
                            deviceStats->sasStatistics.readCacheMemoryHits.isSupported = true;
                            deviceStats->sasStatistics.readCacheMemoryHits.isValueValid = true;
                            deviceStats->sasStatistics.readCacheMemoryHits.thresholdNotificationEnabled = tempLogBuf[iter + 2] & BIT4;//ETC bit
                            if (tempLogBuf[iter + 2] & BIT4)
                            {
                                switch ((tempLogBuf[iter + 2] & (BIT2 | BIT3)) >> 2)
                                {
                                case 3:
                                    deviceStats->sasStatistics.readCacheMemoryHits.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_GREATER;
                                    break;
                                case 2:
                                    deviceStats->sasStatistics.readCacheMemoryHits.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL;
                                    break;
                                case 1:
                                    deviceStats->sasStatistics.readCacheMemoryHits.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL;
                                    break;
                                case 0:
                                default:
                                    deviceStats->sasStatistics.readCacheMemoryHits.threshType = THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE;
                                    break;
                                }
                            }
                            deviceStats->sasStatistics.readCacheMemoryHits.statisticValue = M_BytesTo4ByteValue(0, tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            break;
                        case 2://reads to cache memory
                            deviceStats->sasStatistics.readsToCacheMemory.isSupported = true;
                            deviceStats->sasStatistics.readsToCacheMemory.isValueValid = true;
                            deviceStats->sasStatistics.readsToCacheMemory.thresholdNotificationEnabled = tempLogBuf[iter + 2] & BIT4;//ETC bit
                            if (tempLogBuf[iter + 2] & BIT4)
                            {
                                switch ((tempLogBuf[iter + 2] & (BIT2 | BIT3)) >> 2)
                                {
                                case 3:
                                    deviceStats->sasStatistics.readsToCacheMemory.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_GREATER;
                                    break;
                                case 2:
                                    deviceStats->sasStatistics.readsToCacheMemory.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL;
                                    break;
                                case 1:
                                    deviceStats->sasStatistics.readsToCacheMemory.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL;
                                    break;
                                case 0:
                                default:
                                    deviceStats->sasStatistics.readsToCacheMemory.threshType = THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE;
                                    break;
                                }
                            }
                            deviceStats->sasStatistics.readsToCacheMemory.statisticValue = M_BytesTo4ByteValue(0, tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            break;
                        case 3://write cache memory hits
                            deviceStats->sasStatistics.writeCacheMemoryHits.isSupported = true;
                            deviceStats->sasStatistics.writeCacheMemoryHits.isValueValid = true;
                            deviceStats->sasStatistics.writeCacheMemoryHits.thresholdNotificationEnabled = tempLogBuf[iter + 2] & BIT4;//ETC bit
                            if (tempLogBuf[iter + 2] & BIT4)
                            {
                                switch ((tempLogBuf[iter + 2] & (BIT2 | BIT3)) >> 2)
                                {
                                case 3:
                                    deviceStats->sasStatistics.writeCacheMemoryHits.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_GREATER;
                                    break;
                                case 2:
                                    deviceStats->sasStatistics.writeCacheMemoryHits.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL;
                                    break;
                                case 1:
                                    deviceStats->sasStatistics.writeCacheMemoryHits.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL;
                                    break;
                                case 0:
                                default:
                                    deviceStats->sasStatistics.writeCacheMemoryHits.threshType = THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE;
                                    break;
                                }
                            }
                            deviceStats->sasStatistics.writeCacheMemoryHits.statisticValue = M_BytesTo4ByteValue(0, tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            break;
                        case 4://writes from cache memory
                            deviceStats->sasStatistics.writesFromCacheMemory.isSupported = true;
                            deviceStats->sasStatistics.writesFromCacheMemory.isValueValid = true;
                            deviceStats->sasStatistics.writesFromCacheMemory.thresholdNotificationEnabled = tempLogBuf[iter + 2] & BIT4;//ETC bit
                            if (tempLogBuf[iter + 2] & BIT4)
                            {
                                switch ((tempLogBuf[iter + 2] & (BIT2 | BIT3)) >> 2)
                                {
                                case 3:
                                    deviceStats->sasStatistics.writesFromCacheMemory.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_GREATER;
                                    break;
                                case 2:
                                    deviceStats->sasStatistics.writesFromCacheMemory.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL;
                                    break;
                                case 1:
                                    deviceStats->sasStatistics.writesFromCacheMemory.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL;
                                    break;
                                case 0:
                                default:
                                    deviceStats->sasStatistics.writesFromCacheMemory.threshType = THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE;
                                    break;
                                }
                            }
                            deviceStats->sasStatistics.writesFromCacheMemory.statisticValue = M_BytesTo4ByteValue(0, tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            break;
                        case 5://time from last hard reset
                            deviceStats->sasStatistics.timeFromLastHardReset.isSupported = true;
                            deviceStats->sasStatistics.timeFromLastHardReset.isValueValid = true;
                            deviceStats->sasStatistics.timeFromLastHardReset.thresholdNotificationEnabled = tempLogBuf[iter + 2] & BIT4;//ETC bit
                            if (tempLogBuf[iter + 2] & BIT4)
                            {
                                switch ((tempLogBuf[iter + 2] & (BIT2 | BIT3)) >> 2)
                                {
                                case 3:
                                    deviceStats->sasStatistics.timeFromLastHardReset.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_GREATER;
                                    break;
                                case 2:
                                    deviceStats->sasStatistics.timeFromLastHardReset.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL;
                                    break;
                                case 1:
                                    deviceStats->sasStatistics.timeFromLastHardReset.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL;
                                    break;
                                case 0:
                                default:
                                    deviceStats->sasStatistics.timeFromLastHardReset.threshType = THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE;
                                    break;
                                }
                            }
                            deviceStats->sasStatistics.timeFromLastHardReset.statisticValue = M_BytesTo4ByteValue(0, tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            break;
                        case 6://time interval
                            deviceStats->sasStatistics.cacheTimeInterval.isSupported = true;
                            deviceStats->sasStatistics.cacheTimeInterval.isValueValid = true;
                            deviceStats->sasStatistics.cacheTimeInterval.thresholdNotificationEnabled = tempLogBuf[iter + 2] & BIT4;//ETC bit
                            if (tempLogBuf[iter + 2] & BIT4)
                            {
                                switch ((tempLogBuf[iter + 2] & (BIT2 | BIT3)) >> 2)
                                {
                                case 3:
                                    deviceStats->sasStatistics.cacheTimeInterval.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_GREATER;
                                    break;
                                case 2:
                                    deviceStats->sasStatistics.cacheTimeInterval.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL;
                                    break;
                                case 1:
                                    deviceStats->sasStatistics.cacheTimeInterval.threshType = THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL;
                                    break;
                                case 0:
                                default:
                                    deviceStats->sasStatistics.cacheTimeInterval.threshType = THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE;
                                    break;
                                }
                            }
                            deviceStats->sasStatistics.cacheTimeInterval.statisticValue = M_BytesTo4ByteValue(0, tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            break;
                        default:
                            break;
                        }
                        if (parameterLength == 0)
                        {
                            break;
                        }
                    }
                    //thresholds
                    memset(tempLogBuf, 0, LEGACY_DRIVE_SEC_SIZE);
                    if (SUCCESS == scsi_Log_Sense_Cmd(device, false, LPC_THRESHOLD_VALUES, pageCode, subpageCode, 0x0001, tempLogBuf, LEGACY_DRIVE_SEC_SIZE))
                    {
                        uint16_t pageLength = M_BytesTo2ByteValue(tempLogBuf[2], tempLogBuf[3]);
                        uint8_t parameterLength = 0;
                        //loop through the data and gather the data from each parameter we care about getting.
                        for (uint16_t iter = 4; iter < pageLength && iter < LEGACY_DRIVE_SEC_SIZE; iter += (parameterLength + 4))
                        {
                            uint16_t parameterCode = M_BytesTo2ByteValue(tempLogBuf[iter], tempLogBuf[iter + 1]);
                            parameterLength = tempLogBuf[iter + 3];
                            switch (parameterCode)
                            {
                            case 1://read cache memory hits
                                deviceStats->sasStatistics.readCacheMemoryHits.supportsNotification = true;
                                if (tempLogBuf[iter + 2] & BIT4)
                                {
                                    deviceStats->sasStatistics.readCacheMemoryHits.isThresholdValid = true;
                                    deviceStats->sasStatistics.readCacheMemoryHits.threshold = M_BytesTo4ByteValue(0, tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
                                    scsi_Threshold_Comparison(&deviceStats->sasStatistics.readCacheMemoryHits);
                                }
                                break;
                            case 2://reads to cache memory
                                deviceStats->sasStatistics.readsToCacheMemory.supportsNotification = true;
                                if (tempLogBuf[iter + 2] & BIT4)
                                {
                                    deviceStats->sasStatistics.readsToCacheMemory.isThresholdValid = true;
                                    deviceStats->sasStatistics.readsToCacheMemory.threshold = M_BytesTo4ByteValue(0, tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
                                    scsi_Threshold_Comparison(&deviceStats->sasStatistics.readsToCacheMemory);
                                }
                                break;
                            case 3://write cache memory hits
                                deviceStats->sasStatistics.writeCacheMemoryHits.supportsNotification = true;
                                if (tempLogBuf[iter + 2] & BIT4)
                                {
                                    deviceStats->sasStatistics.writeCacheMemoryHits.isThresholdValid = true;
                                    deviceStats->sasStatistics.writeCacheMemoryHits.threshold = M_BytesTo4ByteValue(0, tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
                                    scsi_Threshold_Comparison(&deviceStats->sasStatistics.writeCacheMemoryHits);
                                }
                                break;
                            case 4://writes from cache memory
                                deviceStats->sasStatistics.writesFromCacheMemory.supportsNotification = true;
                                if (tempLogBuf[iter + 2] & BIT4)
                                {
                                    deviceStats->sasStatistics.writesFromCacheMemory.isThresholdValid = true;
                                    deviceStats->sasStatistics.writesFromCacheMemory.threshold = M_BytesTo4ByteValue(0, tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
                                    scsi_Threshold_Comparison(&deviceStats->sasStatistics.writesFromCacheMemory);
                                }
                                break;
                            case 5://time from last hard reset
                                deviceStats->sasStatistics.timeFromLastHardReset.supportsNotification = true;
                                if (tempLogBuf[iter + 2] & BIT4)
                                {
                                    deviceStats->sasStatistics.timeFromLastHardReset.isThresholdValid = true;
                                    deviceStats->sasStatistics.timeFromLastHardReset.threshold = M_BytesTo4ByteValue(0, tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
                                    scsi_Threshold_Comparison(&deviceStats->sasStatistics.timeFromLastHardReset);
                                }
                                break;
                            case 6://cache time interval
                                deviceStats->sasStatistics.cacheTimeInterval.supportsNotification = true;
                                if (tempLogBuf[iter + 2] & BIT4)
                                {
                                    deviceStats->sasStatistics.cacheTimeInterval.isThresholdValid = true;
                                    deviceStats->sasStatistics.cacheTimeInterval.threshold = M_BytesTo4ByteValue(0, tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
                                    scsi_Threshold_Comparison(&deviceStats->sasStatistics.cacheTimeInterval);
                                }
                                break;
                            default:
                                break;
                            }
                            if (parameterLength == 0)
                            {
                                break;
                            }
                        }
                    }
                }
            default:
                break;
            }
            break;
        case LP_ZONED_DEVICE_STATISTICS://subpage 1
            switch (subpageCode)
            {
            case 0x01://ZBD statistics
            {
                memset(tempLogBuf, 0, LEGACY_DRIVE_SEC_SIZE);
                if (SUCCESS == scsi_Log_Sense_Cmd(device, false, LPC_CUMULATIVE_VALUES, pageCode, subpageCode, 0x0001, tempLogBuf, LEGACY_DRIVE_SEC_SIZE))
                {
                    deviceStats->sasStatistics.cacheMemoryStatisticsSupported = true;
                    uint16_t pageLength = M_BytesTo2ByteValue(tempLogBuf[2], tempLogBuf[3]);
                    uint8_t parameterLength = 0;
                    //loop through the data and gather the data from each parameter we care about getting.
                    for (uint16_t iter = 4; iter < pageLength && iter < LEGACY_DRIVE_SEC_SIZE; iter += (parameterLength + 4))
                    {
                        uint16_t parameterCode = M_BytesTo2ByteValue(tempLogBuf[iter], tempLogBuf[iter + 1]);
                        parameterLength = tempLogBuf[iter + 3];
                        switch (parameterCode)
                        {
                        case 0://maximum open zones
                            deviceStats->sasStatistics.maximumOpenZones.isSupported = true;
                            deviceStats->sasStatistics.maximumOpenZones.isValueValid = true;
                            deviceStats->sasStatistics.maximumOpenZones.statisticValue = M_BytesTo8ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7], tempLogBuf[iter + 8], tempLogBuf[iter + 9], tempLogBuf[iter + 10], tempLogBuf[iter + 11]);
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            break;
                        case 1://maximum explicitly open zones
                            deviceStats->sasStatistics.maximumExplicitlyOpenZones.isSupported = true;
                            deviceStats->sasStatistics.maximumExplicitlyOpenZones.isValueValid = true;
                            deviceStats->sasStatistics.maximumExplicitlyOpenZones.statisticValue = M_BytesTo8ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7], tempLogBuf[iter + 8], tempLogBuf[iter + 9], tempLogBuf[iter + 10], tempLogBuf[iter + 11]);
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            break;
                        case 2://maximum implicitly open zones
                            deviceStats->sasStatistics.maximumImplicitlyOpenZones.isSupported = true;
                            deviceStats->sasStatistics.maximumImplicitlyOpenZones.isValueValid = true;
                            deviceStats->sasStatistics.maximumImplicitlyOpenZones.statisticValue = M_BytesTo8ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7], tempLogBuf[iter + 8], tempLogBuf[iter + 9], tempLogBuf[iter + 10], tempLogBuf[iter + 11]);
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            break;
                        case 3://minimum empty zones
                            deviceStats->sasStatistics.minimumEmptyZones.isSupported = true;
                            deviceStats->sasStatistics.minimumEmptyZones.isValueValid = true;
                            deviceStats->sasStatistics.minimumEmptyZones.statisticValue = M_BytesTo8ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7], tempLogBuf[iter + 8], tempLogBuf[iter + 9], tempLogBuf[iter + 10], tempLogBuf[iter + 11]);
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            break;
                        case 4://maximum non-sequential zones
                            deviceStats->sasStatistics.maximumNonSequentialZones.isSupported = true;
                            deviceStats->sasStatistics.maximumNonSequentialZones.isValueValid = true;
                            deviceStats->sasStatistics.maximumNonSequentialZones.statisticValue = M_BytesTo8ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7], tempLogBuf[iter + 8], tempLogBuf[iter + 9], tempLogBuf[iter + 10], tempLogBuf[iter + 11]);
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            break;
                        case 5://zones emptied
                            deviceStats->sasStatistics.zonesEmptied.isSupported = true;
                            deviceStats->sasStatistics.zonesEmptied.isValueValid = true;
                            deviceStats->sasStatistics.zonesEmptied.statisticValue = M_BytesTo8ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7], tempLogBuf[iter + 8], tempLogBuf[iter + 9], tempLogBuf[iter + 10], tempLogBuf[iter + 11]);
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            break;
                        case 6://suboptimal write commands
                            deviceStats->sasStatistics.suboptimalWriteCommands.isSupported = true;
                            deviceStats->sasStatistics.suboptimalWriteCommands.isValueValid = true;
                            deviceStats->sasStatistics.suboptimalWriteCommands.statisticValue = M_BytesTo8ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7], tempLogBuf[iter + 8], tempLogBuf[iter + 9], tempLogBuf[iter + 10], tempLogBuf[iter + 11]);
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            break;
                        case 7://commands exceeding optimal limit
                            deviceStats->sasStatistics.commandsExceedingOptimalLimit.isSupported = true;
                            deviceStats->sasStatistics.commandsExceedingOptimalLimit.isValueValid = true;
                            deviceStats->sasStatistics.commandsExceedingOptimalLimit.statisticValue = M_BytesTo8ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7], tempLogBuf[iter + 8], tempLogBuf[iter + 9], tempLogBuf[iter + 10], tempLogBuf[iter + 11]);
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            break;
                        case 8://failed explicit opens
                            deviceStats->sasStatistics.failedExplicitOpens.isSupported = true;
                            deviceStats->sasStatistics.failedExplicitOpens.isValueValid = true;
                            deviceStats->sasStatistics.failedExplicitOpens.statisticValue = M_BytesTo8ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7], tempLogBuf[iter + 8], tempLogBuf[iter + 9], tempLogBuf[iter + 10], tempLogBuf[iter + 11]);
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            break;
                        case 9://read rule violations
                            deviceStats->sasStatistics.readRuleViolations.isSupported = true;
                            deviceStats->sasStatistics.readRuleViolations.isValueValid = true;
                            deviceStats->sasStatistics.readRuleViolations.statisticValue = M_BytesTo8ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7], tempLogBuf[iter + 8], tempLogBuf[iter + 9], tempLogBuf[iter + 10], tempLogBuf[iter + 11]);
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            break;
                        case 10://write rule violations
                            deviceStats->sasStatistics.writeRuleViolations.isSupported = true;
                            deviceStats->sasStatistics.writeRuleViolations.isValueValid = true;
                            deviceStats->sasStatistics.writeRuleViolations.statisticValue = M_BytesTo8ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7], tempLogBuf[iter + 8], tempLogBuf[iter + 9], tempLogBuf[iter + 10], tempLogBuf[iter + 11]);
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            break;
                        default:
                            break;
                        }
                        if (parameterLength == 0)
                        {
                            break;
                        }
                    }
                }
                //Thresholds are not defined/obsolete so no need to read them or attempt to read them.
            }
                break;
            default:
                break;
            }
            break;
        default:
            break;
        }
    }
    //get the SAS timestamp
    memset(tempLogBuf, 0, LEGACY_DRIVE_SEC_SIZE);
    if (SUCCESS == scsi_Report_Timestamp(device, LEGACY_DRIVE_SEC_SIZE, tempLogBuf))
    {
        deviceStats->sasStatistics.timeStampSupported = true;
        deviceStats->sasStatistics.dateAndTimeTimestamp.isSupported = true;
        deviceStats->sasStatistics.dateAndTimeTimestamp.isValueValid = true;
        deviceStats->sasStatistics.dateAndTimeTimestamp.statisticValue = M_BytesTo8ByteValue(0, 0, tempLogBuf[4], tempLogBuf[5], tempLogBuf[6], tempLogBuf[7], tempLogBuf[8], tempLogBuf[9]);
    }
    return ret;
}

int get_DeviceStatistics(tDevice *device, ptrDeviceStatistics deviceStats)
{
    int ret = NOT_SUPPORTED;
    if (!deviceStats)
    {
        return BAD_PARAMETER;
    }
    if (device->drive_info.drive_type == ATA_DRIVE)
    {
        return get_ATA_DeviceStatistics(device, deviceStats);
    }
    else if (device->drive_info.drive_type == SCSI_DRIVE)
    {
        return get_SCSI_DeviceStatistics(device, deviceStats);
    }
    return ret;
}

void scsi_Threshold_Comparison(statistic *ptrStatistic)
{
    if (ptrStatistic)
    {
        if (ptrStatistic->isThresholdValid && ptrStatistic->thresholdNotificationEnabled && ptrStatistic->supportsNotification)
        {
            switch (ptrStatistic->threshType)
            {
            case THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL:
                if (ptrStatistic->statisticValue == ptrStatistic->threshold)
                {
                    ptrStatistic->monitoredConditionMet = true;
                }
                break;
            case THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL:
                if (ptrStatistic->statisticValue != ptrStatistic->threshold)
                {
                    ptrStatistic->monitoredConditionMet = true;
                }
                break;
            case THRESHOLD_TYPE_TRIGGER_WHEN_GREATER:
                if (ptrStatistic->statisticValue > ptrStatistic->threshold)
                {
                    ptrStatistic->monitoredConditionMet = true;
                }
                break;
            case THRESHOLD_TYPE_NO_TRIGGER:
            case THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE:
            default:
                break;
            }
        }
    }
    return;
}

void print_Count_Statistic(statistic theStatistic, char *statisticName, char *statisticUnit)
{
    if (theStatistic.isSupported)
    {
        char displayThreshold[30] = { 0 };
        if (theStatistic.monitoredConditionMet)
        {
            printf("!");
        }
        else if (theStatistic.isThresholdValid)
        {
            printf("*");
        }
        else if (theStatistic.supportsNotification)
        {
            printf("-");
        }
        else
        {
            printf(" ");
        }
        printf("%-60s", statisticName);
        if (theStatistic.isThresholdValid)
        {
            switch (theStatistic.threshType)
            {
            case THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE:
                sprintf(displayThreshold, "%"PRIu64" (Always Trigger)", theStatistic.threshold);
                break;
            case THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL:
                sprintf(displayThreshold, "=%"PRIu64, theStatistic.threshold);
                break;
            case THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL:
                sprintf(displayThreshold, "!=%"PRIu64, theStatistic.threshold);
                break;
            case THRESHOLD_TYPE_TRIGGER_WHEN_GREATER:
                sprintf(displayThreshold, ">%"PRIu64, theStatistic.threshold);
                break;
            case THRESHOLD_TYPE_TRIGGER_WHEN_LESS:
                sprintf(displayThreshold, "<%"PRIu64, theStatistic.threshold);
                break;
            case THRESHOLD_TYPE_NO_TRIGGER:
            default:
                sprintf(displayThreshold, "%"PRIu64, theStatistic.threshold);
                break;
            }
        }
        else
        {
            sprintf(displayThreshold, "N/A");
        }
        printf(" %-16s ", displayThreshold);
        if (theStatistic.isValueValid)
        {
            printf("%"PRIu64, theStatistic.statisticValue);
            if (statisticUnit)
            {
                printf(" %s", statisticUnit);
            }
        }
        else
        {
            printf("Invalid");
        }
        printf("\n");
    }
}

void print_Workload_Utilization_Statistic(statistic theStatistic, char *statisticName)
{
    if (theStatistic.isSupported)
    {
        char displayThreshold[30] = { 0 };
        if (theStatistic.monitoredConditionMet)
        {
            printf("!");
        }
        else if (theStatistic.isThresholdValid)
        {
            printf("*");
        }
        else if (theStatistic.supportsNotification)
        {
            printf("-");
        }
        else
        {
            printf(" ");
        }
        printf("%-60s", statisticName);
        if (theStatistic.isThresholdValid)
        {
            switch (theStatistic.threshType)
            {
            case THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE:
                sprintf(displayThreshold, "%"PRIu64" (Always Trigger)", theStatistic.threshold);
                break;
            case THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL:
                sprintf(displayThreshold, "=%"PRIu64, theStatistic.threshold);
                break;
            case THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL:
                sprintf(displayThreshold, "!=%"PRIu64, theStatistic.threshold);
                break;
            case THRESHOLD_TYPE_TRIGGER_WHEN_GREATER:
                sprintf(displayThreshold, ">%"PRIu64, theStatistic.threshold);
                break;
            case THRESHOLD_TYPE_TRIGGER_WHEN_LESS:
                sprintf(displayThreshold, "<%"PRIu64, theStatistic.threshold);
                break;
            case THRESHOLD_TYPE_NO_TRIGGER:
            default:
                sprintf(displayThreshold, "%"PRIu64, theStatistic.threshold);
                break;
            }
        }
        else
        {
            sprintf(displayThreshold, "N/A");
        }
        printf(" %-16s ", displayThreshold);
        if (theStatistic.isValueValid)
        {
            if (theStatistic.statisticValue != 65535)
            {
                double workloadUtilization = (double)theStatistic.statisticValue;
                workloadUtilization *= 0.01;//convert to fractional percentage
                printf("%0.02f%%", workloadUtilization);
            }
            else
            {
                printf(">655.34%%");
            }
        }
        else
        {
            printf("Invalid");
        }
        printf("\n");
    }
}

void print_Utilization_Usage_Rate_Statistic(statistic theStatistic, char *statisticName)
{
    if (theStatistic.isSupported)
    {
        char displayThreshold[30] = { 0 };
        if (theStatistic.monitoredConditionMet)
        {
            printf("!");
        }
        else if (theStatistic.isThresholdValid)
        {
            printf("*");
        }
        else if (theStatistic.supportsNotification)
        {
            printf("-");
        }
        else
        {
            printf(" ");
        }
        printf("%-60s", statisticName);
        if (theStatistic.isThresholdValid)
        {
            switch (theStatistic.threshType)
            {
            case THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE:
                sprintf(displayThreshold, "%"PRIu64" (Always Trigger)", theStatistic.threshold);
                break;
            case THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL:
                sprintf(displayThreshold, "=%"PRIu64, theStatistic.threshold);
                break;
            case THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL:
                sprintf(displayThreshold, "!=%"PRIu64, theStatistic.threshold);
                break;
            case THRESHOLD_TYPE_TRIGGER_WHEN_GREATER:
                sprintf(displayThreshold, ">%"PRIu64, theStatistic.threshold);
                break;
            case THRESHOLD_TYPE_TRIGGER_WHEN_LESS:
                sprintf(displayThreshold, "<%"PRIu64, theStatistic.threshold);
                break;
            case THRESHOLD_TYPE_NO_TRIGGER:
            default:
                sprintf(displayThreshold, "%"PRIu64, theStatistic.threshold);
                break;
            }
        }
        else
        {
            sprintf(displayThreshold, "N/A");
        }
        printf(" %-16s ", displayThreshold);
        if (theStatistic.isValueValid)
        {
            uint8_t utilizationUsageRate = M_Byte0(theStatistic.statisticValue);
            uint8_t rateValidity = M_Byte5(theStatistic.statisticValue);
            uint8_t rateBasis = M_Nibble9(theStatistic.statisticValue);
            switch (rateValidity)
            {
            case 0://valid
                if (utilizationUsageRate == 255)
                {
                    printf(">254%%");
                }
                else
                {
                    printf("%"PRIu8"%%", utilizationUsageRate);
                }
                switch (rateBasis)
                {
                case 0://since manufacture
                    printf(" since manufacture");
                    break;
                case 4://since power on reset
                    printf(" since power on reset");
                    break;
                case 8://power on hours
                    printf(" for POH");
                    break;
                case 0xF://undetermined
                default:
                    break;
                }
                break;
            case 0x10://invalid due to insufficient info
                printf("Invalid - insufficient info collected");
                break;
            case 0x81://unreasonable due to date and time timestamp
                printf("Unreasonable due to date and time timestamp");
                break;
            case 0xFF:
            default://invalid for unknown reason
                printf("Invalid for unknown reason");
                break;
            }
        }
        else
        {
            printf("Invalid");
        }
        printf("\n");
    }
}

void print_Resource_Availability_Statistic(statistic theStatistic, char *statisticName)
{
    if (theStatistic.isSupported)
    {
        char displayThreshold[30] = { 0 };
        if (theStatistic.monitoredConditionMet)
        {
            printf("!");
        }
        else if (theStatistic.isThresholdValid)
        {
            printf("*");
        }
        else if (theStatistic.supportsNotification)
        {
            printf("-");
        }
        else
        {
            printf(" ");
        }
        printf("%-60s", statisticName);
        if (theStatistic.isThresholdValid)
        {
            switch (theStatistic.threshType)
            {
            case THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE:
                sprintf(displayThreshold, "%"PRIu64" (Always Trigger)", theStatistic.threshold);
                break;
            case THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL:
                sprintf(displayThreshold, "=%"PRIu64, theStatistic.threshold);
                break;
            case THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL:
                sprintf(displayThreshold, "!=%"PRIu64, theStatistic.threshold);
                break;
            case THRESHOLD_TYPE_TRIGGER_WHEN_GREATER:
                sprintf(displayThreshold, ">%"PRIu64, theStatistic.threshold);
                break;
            case THRESHOLD_TYPE_TRIGGER_WHEN_LESS:
                sprintf(displayThreshold, "<%"PRIu64, theStatistic.threshold);
                break;
            case THRESHOLD_TYPE_NO_TRIGGER:
            default:
                sprintf(displayThreshold, "%"PRIu64, theStatistic.threshold);
                break;
            }
        }
        else
        {
            sprintf(displayThreshold, "N/A");
        }
        printf(" %-16s ", displayThreshold);
        if (theStatistic.isValueValid)
        {
            double fractionAvailable = (double)M_Word0(theStatistic.statisticValue) / 65535.0;
            printf("%0.02f%% Available", fractionAvailable);
        }
        else
        {
            printf("Invalid");
        }
        printf("\n");
    }
}

void print_Random_Write_Resources_Used_Statistic(statistic theStatistic, char *statisticName)
{
    if (theStatistic.isSupported)
    {
        char displayThreshold[30] = { 0 };
        if (theStatistic.monitoredConditionMet)
        {
            printf("!");
        }
        else if (theStatistic.isThresholdValid)
        {
            printf("*");
        }
        else if (theStatistic.supportsNotification)
        {
            printf("-");
        }
        else
        {
            printf(" ");
        }
        printf("%-60s", statisticName);
        if (theStatistic.isThresholdValid)
        {
            switch (theStatistic.threshType)
            {
            case THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE:
                sprintf(displayThreshold, "%"PRIu64" (Always Trigger)", theStatistic.threshold);
                break;
            case THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL:
                sprintf(displayThreshold, "=%"PRIu64, theStatistic.threshold);
                break;
            case THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL:
                sprintf(displayThreshold, "!=%"PRIu64, theStatistic.threshold);
                break;
            case THRESHOLD_TYPE_TRIGGER_WHEN_GREATER:
                sprintf(displayThreshold, ">%"PRIu64, theStatistic.threshold);
                break;
            case THRESHOLD_TYPE_TRIGGER_WHEN_LESS:
                sprintf(displayThreshold, "<%"PRIu64, theStatistic.threshold);
                break;
            case THRESHOLD_TYPE_NO_TRIGGER:
            default:
                sprintf(displayThreshold, "%"PRIu64, theStatistic.threshold);
                break;
            }
        }
        else
        {
            sprintf(displayThreshold, "N/A");
        }
        printf(" %-16s ", displayThreshold);
        if (theStatistic.isValueValid)
        {
            uint8_t resourceValue = M_Byte0(theStatistic.statisticValue);
            if (resourceValue >= 0 && resourceValue <= 0x7F)
            {
                printf("Within nominal bounds (%" PRIX8 "h)", resourceValue);
            }
            else
            {
                printf("Exceeds nominal bounds (%" PRIX8 "h)", resourceValue);
            }
        }
        else
        {
            printf("Invalid");
        }
        printf("\n");
    }
}

void print_Non_Volatile_Time_Statistic(statistic theStatistic, char *statisticName)
{
    if (theStatistic.isSupported)
    {
        char displayThreshold[30] = { 0 };
        if (theStatistic.monitoredConditionMet)
        {
            printf("!");
        }
        else if (theStatistic.isThresholdValid)
        {
            printf("*");
        }
        else if (theStatistic.supportsNotification)
        {
            printf("-");
        }
        else
        {
            printf(" ");
        }
        printf("%-60s", statisticName);
        if (theStatistic.isThresholdValid)
        {
            switch (theStatistic.threshType)
            {
            case THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE:
                sprintf(displayThreshold, "%"PRIu64" (Always Trigger)", theStatistic.threshold);
                break;
            case THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL:
                sprintf(displayThreshold, "=%"PRIu64, theStatistic.threshold);
                break;
            case THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL:
                sprintf(displayThreshold, "!=%"PRIu64, theStatistic.threshold);
                break;
            case THRESHOLD_TYPE_TRIGGER_WHEN_GREATER:
                sprintf(displayThreshold, ">%"PRIu64, theStatistic.threshold);
                break;
            case THRESHOLD_TYPE_TRIGGER_WHEN_LESS:
                sprintf(displayThreshold, "<%"PRIu64, theStatistic.threshold);
                break;
            case THRESHOLD_TYPE_NO_TRIGGER:
            default:
                sprintf(displayThreshold, "%"PRIu64, theStatistic.threshold);
                break;
            }
        }
        else
        {
            sprintf(displayThreshold, "N/A");
        }
        printf(" %-16s ", displayThreshold);
        if (theStatistic.isValueValid)
        {
            switch (theStatistic.statisticValue)
            {
            case 0:
                printf("Volatile");
                break;
            case 1:
                printf("Nonvolatile for unknown time");
                break;
            case 0xFFFFFF:
                printf("Nonvolatile indefinitely");
                break;
            default://time in minutes
                printf("Nonvolatile for %"PRIu64"m", theStatistic.statisticValue);
                break;
            }
        }
        else
        {
            printf("Invalid");
        }
        printf("\n");
    }
}

void print_Temperature_Statistic(statistic theStatistic, char *statisticName)
{
    if (theStatistic.isSupported)
    {
        char displayThreshold[30] = { 0 };
        if (theStatistic.monitoredConditionMet)
        {
            printf("!");
        }
        else if (theStatistic.isThresholdValid)
        {
            printf("*");
        }
        else if (theStatistic.supportsNotification)
        {
            printf("-");
        }
        else
        {
            printf(" ");
        }
        printf("%-60s", statisticName);
        if (theStatistic.isThresholdValid)
        {
            switch (theStatistic.threshType)
            {
            case THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE:
                sprintf(displayThreshold, "%"PRIu64" (Always Trigger)", theStatistic.threshold);
                break;
            case THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL:
                sprintf(displayThreshold, "=%"PRIu64, theStatistic.threshold);
                break;
            case THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL:
                sprintf(displayThreshold, "!=%"PRIu64, theStatistic.threshold);
                break;
            case THRESHOLD_TYPE_TRIGGER_WHEN_GREATER:
                sprintf(displayThreshold, ">%"PRIu64, theStatistic.threshold);
                break;
            case THRESHOLD_TYPE_TRIGGER_WHEN_LESS:
                sprintf(displayThreshold, "<%"PRIu64, theStatistic.threshold);
                break;
            case THRESHOLD_TYPE_NO_TRIGGER:
            default:
                sprintf(displayThreshold, "%"PRIu64, theStatistic.threshold);
                break;
            }
        }
        else
        {
            sprintf(displayThreshold, "N/A");
        }
        printf(" %-16s ", displayThreshold);
        if (theStatistic.isValueValid)
        {
            printf("%"PRId8" C", (int8_t)theStatistic.statisticValue);
        }
        else
        {
            printf("Invalid");
        }
        printf("\n");
    }
}

void print_Date_And_Time_Timestamp_Statistic(statistic theStatistic, char *statisticName)
{
    if (theStatistic.isSupported)
    {
        char displayThreshold[30] = { 0 };
        if (theStatistic.monitoredConditionMet)
        {
            printf("!");
        }
        else if (theStatistic.isThresholdValid)
        {
            printf("*");
        }
        else if (theStatistic.supportsNotification)
        {
            printf("-");
        }
        else
        {
            printf(" ");
        }
        printf("%-60s", statisticName);
        if (theStatistic.isThresholdValid)
        {
            switch (theStatistic.threshType)
            {
            case THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE:
                sprintf(displayThreshold, "%"PRIu64" (Always Trigger)", theStatistic.threshold);
                break;
            case THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL:
                sprintf(displayThreshold, "=%"PRIu64, theStatistic.threshold);
                break;
            case THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL:
                sprintf(displayThreshold, "!=%"PRIu64, theStatistic.threshold);
                break;
            case THRESHOLD_TYPE_TRIGGER_WHEN_GREATER:
                sprintf(displayThreshold, ">%"PRIu64, theStatistic.threshold);
                break;
            case THRESHOLD_TYPE_TRIGGER_WHEN_LESS:
                sprintf(displayThreshold, "<%"PRIu64, theStatistic.threshold);
                break;
            case THRESHOLD_TYPE_NO_TRIGGER:
            default:
                sprintf(displayThreshold, "%"PRIu64, theStatistic.threshold);
                break;
            }
        }
        else
        {
            sprintf(displayThreshold, "N/A");
        }
        printf(" %-16s ", displayThreshold);
        if (theStatistic.isValueValid)
        {
            uint8_t years = 0, days = 0, hours = 0, minutes = 0, seconds = 0;
            //this is reported in milliseconds...convert to other displayable.
            uint64_t statisticSeconds = theStatistic.statisticValue / 1000;
            convert_Seconds_To_Displayable_Time(statisticSeconds, &years, &days, &hours, &minutes, &seconds);
            print_Time_To_Screen(&years, &days, &hours, &minutes, &seconds);
        }
        else
        {
            printf("Invalid");
        }
        printf("\n");
    }
}
//the statistic value must be a time in minutes for this function
void print_Time_Minutes_Statistic(statistic theStatistic, char *statisticName)
{
    if (theStatistic.isSupported)
    {
        char displayThreshold[30] = { 0 };
        if (theStatistic.monitoredConditionMet)
        {
            printf("!");
        }
        else if (theStatistic.isThresholdValid)
        {
            printf("*");
        }
        else if (theStatistic.supportsNotification)
        {
            printf("-");
        }
        else
        {
            printf(" ");
        }
        printf("%-60s", statisticName);
        if (theStatistic.isThresholdValid)
        {
            switch (theStatistic.threshType)
            {
            case THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE:
                sprintf(displayThreshold, "%"PRIu64" (Always Trigger)", theStatistic.threshold);
                break;
            case THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL:
                sprintf(displayThreshold, "=%"PRIu64, theStatistic.threshold);
                break;
            case THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL:
                sprintf(displayThreshold, "!=%"PRIu64, theStatistic.threshold);
                break;
            case THRESHOLD_TYPE_TRIGGER_WHEN_GREATER:
                sprintf(displayThreshold, ">%"PRIu64, theStatistic.threshold);
                break;
            case THRESHOLD_TYPE_TRIGGER_WHEN_LESS:
                sprintf(displayThreshold, "<%"PRIu64, theStatistic.threshold);
                break;
            case THRESHOLD_TYPE_NO_TRIGGER:
            default:
                sprintf(displayThreshold, "%"PRIu64, theStatistic.threshold);
                break;
            }
        }
        else
        {
            sprintf(displayThreshold, "N/A");
        }
        printf(" %-16s", displayThreshold);
        if (theStatistic.isValueValid)
        {
            //this is reported in minutes...convert to other displayable.
            uint64_t statisticMinutes = theStatistic.statisticValue * 60;
            if (statisticMinutes > 0)
            {
                uint8_t years = 0, days = 0, hours = 0, minutes = 0, seconds = 0;
                convert_Seconds_To_Displayable_Time(statisticMinutes, &years, &days, &hours, &minutes, &seconds);
                print_Time_To_Screen(&years, &days, &hours, &minutes, &seconds);
            }
            else
            {
                printf(" 0 minutes");
            }
        }
        else
        {
            printf("Invalid");
        }
        printf("\n");
    }
}

//for accounting date and date of manufacture
void print_SCSI_Date_Statistic(statistic theStatistic, char *statisticName)
{
    if (theStatistic.isSupported)
    {
        char displayThreshold[30] = { 0 };
        if (theStatistic.monitoredConditionMet)
        {
            printf("!");
        }
        else if (theStatistic.isThresholdValid)
        {
            printf("*");
        }
        else if (theStatistic.supportsNotification)
        {
            printf("-");
        }
        else
        {
            printf(" ");
        }
        printf("%-60s", statisticName);
        if (theStatistic.isThresholdValid)
        {
            switch (theStatistic.threshType)
            {
            case THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE:
                sprintf(displayThreshold, "%"PRIu64" (Always Trigger)", theStatistic.threshold);
                break;
            case THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL:
                sprintf(displayThreshold, "=%"PRIu64, theStatistic.threshold);
                break;
            case THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL:
                sprintf(displayThreshold, "!=%"PRIu64, theStatistic.threshold);
                break;
            case THRESHOLD_TYPE_TRIGGER_WHEN_GREATER:
                sprintf(displayThreshold, ">%"PRIu64, theStatistic.threshold);
                break;
            case THRESHOLD_TYPE_TRIGGER_WHEN_LESS:
                sprintf(displayThreshold, "<%"PRIu64, theStatistic.threshold);
                break;
            case THRESHOLD_TYPE_NO_TRIGGER:
            default:
                sprintf(displayThreshold, "%"PRIu64, theStatistic.threshold);
                break;
            }
        }
        else
        {
            sprintf(displayThreshold, "N/A");
        }
        printf(" %-16s ", displayThreshold);
        if (theStatistic.isValueValid)
        {
            char year[5] = { M_Byte3(theStatistic.statisticValue), M_Byte2(theStatistic.statisticValue), M_Byte1(theStatistic.statisticValue), M_Byte0(theStatistic.statisticValue), 0 };
            char week[3] = { M_Byte5(theStatistic.statisticValue), M_Byte4(theStatistic.statisticValue), 0 };
            if (strcmp(year, "    ") == 0 && strcmp(week, "  ") == 0)
            {
                printf("Not set");
            }
            else
            {
                printf("Week %s, %s", week, year);
            }
        }
        else
        {
            printf("Invalid");
        }
        printf("\n");
    }
}

void print_SCSI_Time_Interval_Statistic(statistic theStatistic, char *statisticName)
{
    if (theStatistic.isSupported)
    {
        char displayThreshold[30] = { 0 };
        if (theStatistic.monitoredConditionMet)
        {
            printf("!");
        }
        else if (theStatistic.isThresholdValid)
        {
            printf("*");
        }
        else if (theStatistic.supportsNotification)
        {
            printf("-");
        }
        else
        {
            printf(" ");
        }
        printf("%-60s", statisticName);
        if (theStatistic.isThresholdValid)
        {
            switch (theStatistic.threshType)
            {
            case THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE:
                sprintf(displayThreshold, "%"PRIu64" (Always Trigger)", theStatistic.threshold);
                break;
            case THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL:
                sprintf(displayThreshold, "=%"PRIu64, theStatistic.threshold);
                break;
            case THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL:
                sprintf(displayThreshold, "!=%"PRIu64, theStatistic.threshold);
                break;
            case THRESHOLD_TYPE_TRIGGER_WHEN_GREATER:
                sprintf(displayThreshold, ">%"PRIu64, theStatistic.threshold);
                break;
            case THRESHOLD_TYPE_TRIGGER_WHEN_LESS:
                sprintf(displayThreshold, "<%"PRIu64, theStatistic.threshold);
                break;
            case THRESHOLD_TYPE_NO_TRIGGER:
            default:
                sprintf(displayThreshold, "%"PRIu64, theStatistic.threshold);
                break;
            }
        }
        else
        {
            sprintf(displayThreshold, "N/A");
        }
        printf(" %-16s ", displayThreshold);
        if (theStatistic.isValueValid)
        {
            uint32_t exponent = M_DoubleWord0(theStatistic.statisticValue);
            uint32_t integer = M_DoubleWord1(theStatistic.statisticValue);
            //now byteswap the double words to get the correct endianness (for LSB machines)
            byte_Swap_32(&exponent);
            byte_Swap_32(&integer);
            printf("%"PRIu32" ", integer);
            switch (exponent)
            {
            case 1://deci
                printf("deci seconds");
                break;
            case 2://centi
                printf("centi seconds");
                break;
            case 3://milli
                printf("milli seconds");
                break;
            case 6://micro
                printf("micro seconds");
                break;
            case 9://nano
                printf("nano seconds");
                break;
            case 12://pico
                printf("pico seconds");
                break;
            case 15://femto
                printf("femto seconds");
                break;
            case 18://atto
                printf("atto seconds");
                break;
            default:
                printf("Error: Unknown exponent value\n");
                break;
            }
        }
        else
        {
            printf("Invalid");
        }
        printf("\n");
    }
}

void print_Humidity_Statistic(statistic theStatistic, char *statisticName)
{
    if (theStatistic.isSupported)
    {
        char displayThreshold[30] = { 0 };
        if (theStatistic.monitoredConditionMet)
        {
            printf("!");
        }
        else if (theStatistic.isThresholdValid)
        {
            printf("*");
        }
        else if (theStatistic.supportsNotification)
        {
            printf("-");
        }
        else
        {
            printf(" ");
        }
        printf("%-60s", statisticName);
        if (theStatistic.isThresholdValid)
        {
            switch (theStatistic.threshType)
            {
            case THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE:
                sprintf(displayThreshold, "%"PRIu64" (Always Trigger)", theStatistic.threshold);
                break;
            case THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL:
                sprintf(displayThreshold, "=%"PRIu64, theStatistic.threshold);
                break;
            case THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL:
                sprintf(displayThreshold, "!=%"PRIu64, theStatistic.threshold);
                break;
            case THRESHOLD_TYPE_TRIGGER_WHEN_GREATER:
                sprintf(displayThreshold, ">%"PRIu64, theStatistic.threshold);
                break;
            case THRESHOLD_TYPE_TRIGGER_WHEN_LESS:
                sprintf(displayThreshold, "<%"PRIu64, theStatistic.threshold);
                break;
            case THRESHOLD_TYPE_NO_TRIGGER:
            default:
                sprintf(displayThreshold, "%"PRIu64, theStatistic.threshold);
                break;
            }
        }
        else
        {
            sprintf(displayThreshold, "N/A");
        }
        printf(" %-16s ", displayThreshold);
        if (theStatistic.isValueValid)
        {
            if (/*theStatistic.statisticValue >= 0 &&*/ theStatistic.statisticValue <= 100)
            {
                printf("%"PRIu8"", (uint8_t)theStatistic.statisticValue);
            }
            else if (theStatistic.statisticValue == 255)
            {
                printf("No valid relative humidity");
            }
            else
            {
                printf("Reserved value reported");
            }
        }
        else
        {
            printf("Invalid");
        }
        printf("\n");
    }
}

int print_ATA_DeviceStatistics(tDevice *device, ptrDeviceStatistics deviceStats)
{
    int ret = SUCCESS;
    if (!deviceStats)
    {
        return MEMORY_FAILURE;
    }    
    printf("===Device Statistics===\n");
    printf("\t* = condition monitored with threshold (DSN Feature)\n");
    printf("\t! = monitored condition met\n");
    printf("\t- = supports notification (DSN Feature)\n");
    printf(" %-60s %-16s %-16s\n", "Statistic Name:", "Threshold:", "Value:");
    if (deviceStats->sataStatistics.generalStatisticsSupported)
    {
        printf("\n---General Statistics---\n");
        print_Count_Statistic(deviceStats->sataStatistics.lifetimePoweronResets, "LifeTime Power-On Resets", NULL);
        print_Count_Statistic(deviceStats->sataStatistics.powerOnHours, "Power-On Hours", "hours");
        print_Count_Statistic(deviceStats->sataStatistics.logicalSectorsWritten, "Logical Sectors Written", NULL);
        print_Count_Statistic(deviceStats->sataStatistics.numberOfWriteCommands, "Number Of Write Commands", NULL);
        print_Count_Statistic(deviceStats->sataStatistics.logicalSectorsRead, "Logical Sectors Read", NULL);
        print_Count_Statistic(deviceStats->sataStatistics.numberOfReadCommands, "Number Of Read Commands", NULL);
        print_Date_And_Time_Timestamp_Statistic(deviceStats->sataStatistics.dateAndTimeTimestamp, "Date And Time Timestamp");
        print_Count_Statistic(deviceStats->sataStatistics.pendingErrorCount, "Pending Error Count", NULL);
        print_Workload_Utilization_Statistic(deviceStats->sataStatistics.workloadUtilization, "Workload Utilization");
        print_Utilization_Usage_Rate_Statistic(deviceStats->sataStatistics.utilizationUsageRate, "Utilization Usage Rate");
        print_Resource_Availability_Statistic(deviceStats->sataStatistics.resourceAvailability, "Resource Availability");
        print_Random_Write_Resources_Used_Statistic(deviceStats->sataStatistics.randomWriteResourcesUsed, "Random Write Resources Used");
    }
    if (deviceStats->sataStatistics.freeFallStatisticsSupported)
    {
        printf("\n---Free Fall Statistics---\n");
        print_Count_Statistic(deviceStats->sataStatistics.numberOfFreeFallEventsDetected, "Number Of Free-Fall Events Detected", NULL);
        print_Count_Statistic(deviceStats->sataStatistics.overlimitShockEvents, "Overlimit Shock Events", NULL);
    }
    if (deviceStats->sataStatistics.rotatingMediaStatisticsSupported)
    {
        printf("\n---Rotating Media Statistics---\n");
        print_Count_Statistic(deviceStats->sataStatistics.spindleMotorPoweronHours, "Spindle Motor Power-On Hours", "hours");
        print_Count_Statistic(deviceStats->sataStatistics.headFlyingHours, "Head Flying Hours", "hours");
        print_Count_Statistic(deviceStats->sataStatistics.headLoadEvents, "Head Load Events", NULL);
        print_Count_Statistic(deviceStats->sataStatistics.numberOfReallocatedLogicalSectors, "Number Of Reallocated Logical Sectors", NULL);
        print_Count_Statistic(deviceStats->sataStatistics.readRecoveryAttempts, "Read Recovery Attempts", NULL);
        print_Count_Statistic(deviceStats->sataStatistics.numberOfMechanicalStartFailures, "Number Of Mechanical Start Failures", NULL);
        print_Count_Statistic(deviceStats->sataStatistics.numberOfReallocationCandidateLogicalSectors, "Number Of Reallocation Candidate Logical Sectors", NULL);
        print_Count_Statistic(deviceStats->sataStatistics.numberOfHighPriorityUnloadEvents, "Number Of High Priority Unload Events", NULL);
    }
    if (deviceStats->sataStatistics.generalErrorsStatisticsSupported)
    {
        printf("\n---General Errors Statistics---\n");
        print_Count_Statistic(deviceStats->sataStatistics.numberOfReportedUncorrectableErrors, "Number Of Reported Uncorrectable Errors", NULL);
        print_Count_Statistic(deviceStats->sataStatistics.numberOfResetsBetweenCommandAcceptanceAndCommandCompletion, "Number Of Resets Between Command Acceptance and Completion", NULL);
        print_Count_Statistic(deviceStats->sataStatistics.physicalElementStatusChanged, "Physical Element Status Changed", NULL);
    }
    if (deviceStats->sataStatistics.temperatureStatisticsSupported)
    {
        printf("\n---Temperature Statistics---\n");
        print_Temperature_Statistic(deviceStats->sataStatistics.currentTemperature, "Current Temperature");
        print_Temperature_Statistic(deviceStats->sataStatistics.averageShortTermTemperature, "Average Short Term Temperature");
        print_Temperature_Statistic(deviceStats->sataStatistics.averageLongTermTemperature, "Average Long Term Temperature");
        print_Temperature_Statistic(deviceStats->sataStatistics.highestTemperature, "Highest Temperature");
        print_Temperature_Statistic(deviceStats->sataStatistics.lowestTemperature, "Lowest Temperature");
        print_Temperature_Statistic(deviceStats->sataStatistics.highestAverageShortTermTemperature, "Highest Average Short Term Temperature");
        print_Temperature_Statistic(deviceStats->sataStatistics.lowestAverageShortTermTemperature, "Lowest Average Short Term Temperature");
        print_Temperature_Statistic(deviceStats->sataStatistics.highestAverageLongTermTemperature, "Highest Average Long Term Temperature");
        print_Temperature_Statistic(deviceStats->sataStatistics.lowestAverageLongTermTemperature, "Lowest Average Long Term Temperature");
        print_Time_Minutes_Statistic(deviceStats->sataStatistics.timeInOverTemperature, "Time In Over Temperature");
        print_Temperature_Statistic(deviceStats->sataStatistics.specifiedMaximumOperatingTemperature, "Specified Maximum Operating Temperature");
        print_Time_Minutes_Statistic(deviceStats->sataStatistics.timeInUnderTemperature, "Time In Under Temperature");
        print_Temperature_Statistic(deviceStats->sataStatistics.specifiedMinimumOperatingTemperature, "Specified Minimum Operating Temperature");
    }
    if (deviceStats->sataStatistics.transportStatisticsSupported)
    {
        printf("\n---Transport Statistics---\n");
        print_Count_Statistic(deviceStats->sataStatistics.numberOfHardwareResets, "Number Of Hardware Resets", NULL);
        print_Count_Statistic(deviceStats->sataStatistics.numberOfASREvents, "Number Of ASR Events", NULL);
        print_Count_Statistic(deviceStats->sataStatistics.numberOfInterfaceCRCErrors, "Number Of Interface CRC Errors", NULL);
    }
    if (deviceStats->sataStatistics.ssdStatisticsSupported)
    {
        printf("\n---Solid State Device Statistics---\n");
        print_Count_Statistic(deviceStats->sataStatistics.percentageUsedIndicator, "Percent Used Indicator", "%");
    }
    if (deviceStats->sataStatistics.zonedDeviceStatisticsSupported)
    {
        printf("\n---Zoned Device Statistics---\n");
        print_Count_Statistic(deviceStats->sataStatistics.maximumOpenZones, "Maximum Open Zones", NULL);
        print_Count_Statistic(deviceStats->sataStatistics.maximumExplicitlyOpenZones, "Maximum Explicitly Open Zones", NULL);
        print_Count_Statistic(deviceStats->sataStatistics.maximumImplicitlyOpenZones, "Maximum Implicitly Open Zones", NULL);
        print_Count_Statistic(deviceStats->sataStatistics.minimumEmptyZones, "Minumum Empty Zones", NULL);
        print_Count_Statistic(deviceStats->sataStatistics.maximumNonSequentialZones, "Maximum Non-sequential Zones", NULL);
        print_Count_Statistic(deviceStats->sataStatistics.zonesEmptied, "Zones Emptied", NULL);
        print_Count_Statistic(deviceStats->sataStatistics.suboptimalWriteCommands, "Suboptimal Write Commands", NULL);
        print_Count_Statistic(deviceStats->sataStatistics.commandsExceedingOptimalLimit, "Commands Exceeding Optimal Limit", NULL);
        print_Count_Statistic(deviceStats->sataStatistics.failedExplicitOpens, "Failed Explicit Opens", NULL);
        print_Count_Statistic(deviceStats->sataStatistics.readRuleViolations, "Read Rule Violations", NULL);
        print_Count_Statistic(deviceStats->sataStatistics.writeRuleViolations, "Write Rule Violations", NULL);
    }
    if (deviceStats->sataStatistics.vendorSpecificStatisticsSupported)
    {
        if (SEAGATE == is_Seagate_Family(device))
        {
            printf("\n---Seagate Specific Statistics---\n");
        }
        else
        {
            printf("\n---Vendor Specific Statistics---\n");
        }
        for (uint8_t vendorSpecificIter = 0, statisticsFound = 0; vendorSpecificIter < 64 && statisticsFound < deviceStats->sataStatistics.vendorSpecificStatisticsPopulated; ++vendorSpecificIter)
        {
            char statisticName[64] = { 0 };
            if (SEAGATE == is_Seagate_Family(device))
            {
                switch (vendorSpecificIter + 1)
                {
                case 1://pressure
                    sprintf(statisticName, "Pressure Min/Max Reached");
                    break;
                default:
                    sprintf(statisticName, "Vendor Specific Statistic %"PRIu8, vendorSpecificIter + 1);
                    break;
                }
            }
            else
            {
                sprintf(statisticName, "Vendor Specific Statistic %"PRIu8, vendorSpecificIter + 1);
            }
            if (deviceStats->sataStatistics.vendorSpecificStatistics[vendorSpecificIter].isSupported)
            {
                print_Count_Statistic(deviceStats->sataStatistics.vendorSpecificStatistics[vendorSpecificIter], statisticName, NULL);
                ++statisticsFound;
            }
        }
    }
    return ret;
}

int print_SCSI_DeviceStatistics(tDevice *device, ptrDeviceStatistics deviceStats)
{
    int ret = SUCCESS;
    if (!deviceStats)
    {
        return MEMORY_FAILURE;
    }
    ret = SUCCESS;
    printf("===Device Statistics===\n");
    printf("\t* = condition monitored with threshold (RLEC Feature)\n");
    printf("\t! = monitored condition met (Requires Threshold to be set and comparison enabled)\n");
    printf("\t- = supports notification (requires log page thresholds to be supported)\n");
    printf(" %-60s %-16s %-16s\n", "Statistic Name:", "Threshold:", "Value:");
    if (deviceStats->sasStatistics.writeErrorCountersSupported)
    {
        printf("\n---Write Error Counters---\n");
        print_Count_Statistic(deviceStats->sasStatistics.writeErrorsCorrectedWithoutSubstantialDelay, "Write Errors Corrected Without Substantial Delay", NULL);
        print_Count_Statistic(deviceStats->sasStatistics.writeErrorsCorrectedWithPossibleDelays, "Write Errors Corrected With Possible Delay", NULL);
        print_Count_Statistic(deviceStats->sasStatistics.writeTotal, "Write Total", NULL);
        print_Count_Statistic(deviceStats->sasStatistics.writeErrorsCorrected, "Write Errors Corrected", NULL);
        print_Count_Statistic(deviceStats->sasStatistics.writeTotalTimeCorrectionAlgorithmProcessed, "Write Total Times Corrective Algorithm Processed", NULL);
        print_Count_Statistic(deviceStats->sasStatistics.writeTotalBytesProcessed, "Write Total Bytes Processed", NULL);
        print_Count_Statistic(deviceStats->sasStatistics.writeTotalUncorrectedErrors, "Write Total Uncorrected Errors", NULL);
    }
    if (deviceStats->sasStatistics.readErrorCountersSupported)
    {
        printf("\n---Read Error Counters---\n");
        print_Count_Statistic(deviceStats->sasStatistics.readErrorsCorrectedWithPossibleDelays, "Read Errors Corrected With Possible Delay", NULL);
        print_Count_Statistic(deviceStats->sasStatistics.readTotal, "Read Total", NULL);
        print_Count_Statistic(deviceStats->sasStatistics.readErrorsCorrected, "Read Errors Corrected", NULL);
        print_Count_Statistic(deviceStats->sasStatistics.readTotalTimeCorrectionAlgorithmProcessed, "Read Total Times Corrective Algorithm Processed", NULL);
        print_Count_Statistic(deviceStats->sasStatistics.readTotalBytesProcessed, "Read Total Bytes Processed", NULL);
        print_Count_Statistic(deviceStats->sasStatistics.readTotalUncorrectedErrors, "Read Total Uncorrected Errors", NULL);
    }
    if (deviceStats->sasStatistics.readReverseErrorCountersSupported)
    {
        printf("\n---Read Reverse Error Counters---\n");
        print_Count_Statistic(deviceStats->sasStatistics.readReverseErrorsCorrectedWithoutSubstantialDelay, "Read Reverse Errors Corrected Without Substantial Delay", NULL);
        print_Count_Statistic(deviceStats->sasStatistics.readReverseErrorsCorrectedWithPossibleDelays, "Read Reverse Errors Corrected With Possible Delay", NULL);
        print_Count_Statistic(deviceStats->sasStatistics.readReverseTotal, "Read Reverse Total", NULL);
        print_Count_Statistic(deviceStats->sasStatistics.readReverseErrorsCorrected, "Read Reverse Errors Corrected", NULL);
        print_Count_Statistic(deviceStats->sasStatistics.readReverseTotalTimeCorrectionAlgorithmProcessed, "Read Reverse Total Times Corrective Algorithm Processed", NULL);
        print_Count_Statistic(deviceStats->sasStatistics.readReverseTotalBytesProcessed, "Read Reverse Total Bytes Processed", NULL);
        print_Count_Statistic(deviceStats->sasStatistics.readReverseTotalUncorrectedErrors, "Read Reverse Total Uncorrected Errors", NULL);
    }
    if (deviceStats->sasStatistics.verifyErrorCountersSupported)
    {
        printf("\n---Verify Error Counters---\n");
        print_Count_Statistic(deviceStats->sasStatistics.verifyErrorsCorrectedWithoutSubstantialDelay, "Verify Errors Corrected Without Substantial Delay", NULL);
        print_Count_Statistic(deviceStats->sasStatistics.verifyErrorsCorrectedWithPossibleDelays, "Verify Errors Corrected With Possible Delay", NULL);
        print_Count_Statistic(deviceStats->sasStatistics.verifyTotal, "Verify Total", NULL);
        print_Count_Statistic(deviceStats->sasStatistics.verifyErrorsCorrected, "Verify Errors Corrected", NULL);
        print_Count_Statistic(deviceStats->sasStatistics.verifyTotalTimeCorrectionAlgorithmProcessed, "Verify Total Times Corrective Algorithm Processed", NULL);
        print_Count_Statistic(deviceStats->sasStatistics.verifyTotalBytesProcessed, "Verify Total Bytes Processed", NULL);
        print_Count_Statistic(deviceStats->sasStatistics.verifyTotalUncorrectedErrors, "Verify Total Uncorrected Errors", NULL);
    }
    if (deviceStats->sasStatistics.nonMediumErrorSupported)
    {
        printf("\n---Non Medium Error---\n");
        print_Count_Statistic(deviceStats->sasStatistics.nonMediumErrorCount, "Non-Medium Error Count", NULL);
    }
    if (deviceStats->sasStatistics.formatStatusSupported)
    {
        printf("\n---Format Status---\n");
        print_Count_Statistic(deviceStats->sasStatistics.grownDefectsDuringCertification, "Grown Defects During Certification", NULL);
        print_Count_Statistic(deviceStats->sasStatistics.totalBlocksReassignedDuringFormat, "Total Blocks Reassigned During Format", NULL);
        print_Count_Statistic(deviceStats->sasStatistics.totalNewBlocksReassigned, "Total New Blocks Reassigned", NULL);
        print_Count_Statistic(deviceStats->sasStatistics.powerOnMinutesSinceFormat, "Power On Minutes Since Last Format", "minutes");
    }
    if (deviceStats->sasStatistics.logicalBlockProvisioningSupported)
    {
        printf("\n---Logical Block Provisioning---\n");
        print_Count_Statistic(deviceStats->sasStatistics.availableLBAMappingresourceCount, "Available LBA Mapping Resource Count", NULL);
        print_Count_Statistic(deviceStats->sasStatistics.usedLBAMappingResourceCount, "Used LBA Mapping Resource Count", NULL);
        print_Count_Statistic(deviceStats->sasStatistics.availableProvisioningResourcePercentage, "Available Provisioning Resource Percentage", "%");
        print_Count_Statistic(deviceStats->sasStatistics.deduplicatedLBAResourceCount, "De-duplicted LBA Resource Count", NULL);
        print_Count_Statistic(deviceStats->sasStatistics.compressedLBAResourceCount, "Compressed LBA Resource Count", NULL);
        print_Count_Statistic(deviceStats->sasStatistics.totalEfficiencyLBAResourceCount, "Total Efficiency LBA Resource Count", NULL);
    }
    if (deviceStats->sasStatistics.temperatureSupported)
    {
        printf("\n---Temperature---\n");
        print_Temperature_Statistic(deviceStats->sasStatistics.temperature, "Temperature");
        print_Temperature_Statistic(deviceStats->sasStatistics.referenceTemperature, "Reference Temperature");
    }
    if (deviceStats->sasStatistics.environmentReportingSupported)
    {
        printf("\n---Environmental Reporting---\n");
        print_Temperature_Statistic(deviceStats->sasStatistics.currentTemperature, "Temperature");
        print_Temperature_Statistic(deviceStats->sasStatistics.lifetimeMaximumTemperature, "Lifetime Maximum Temperature");
        print_Temperature_Statistic(deviceStats->sasStatistics.lifetimeMinimumTemperature, "Lifetime Minimum Temperature");
        print_Temperature_Statistic(deviceStats->sasStatistics.maximumTemperatureSincePowerOn, "Maximum Temperature Since Power On");
        print_Temperature_Statistic(deviceStats->sasStatistics.minimumTemperatureSincePowerOn, "Minimum Temperature Since Power On");
        print_Humidity_Statistic(deviceStats->sasStatistics.currentRelativeHumidity, "Relative Humidity");
        print_Humidity_Statistic(deviceStats->sasStatistics.lifetimeMaximumRelativeHumidity, "Lifetime Maximum Relative Humidity");
        print_Humidity_Statistic(deviceStats->sasStatistics.lifetimeMinumumRelativeHumidity, "Lifetime Minimum Relative Humidity");
        print_Humidity_Statistic(deviceStats->sasStatistics.maximumRelativeHumiditySincePoweron, "Maximum Relative Humidity Since Power On");
        print_Humidity_Statistic(deviceStats->sasStatistics.minimumRelativeHumiditySincePoweron, "Minimum Relative Humidity Since Power On");
    }
    if (deviceStats->sasStatistics.environmentReportingSupported)
    {
        printf("\n---Environmental Limits---\n");
        print_Temperature_Statistic(deviceStats->sasStatistics.highCriticalTemperatureLimitTrigger, "High Critical Temperature Limit Trigger");
        print_Temperature_Statistic(deviceStats->sasStatistics.highCriticalTemperatureLimitReset, "High Critical Temperature Limit Reset");
        print_Temperature_Statistic(deviceStats->sasStatistics.lowCriticalTemperatureLimitReset, "Low Critical Temperature Limit Reset");
        print_Temperature_Statistic(deviceStats->sasStatistics.lowCriticalTemperatureLimitTrigger, "Low Critical Temperature Limit Trigger");
        print_Temperature_Statistic(deviceStats->sasStatistics.highOperatingTemperatureLimitTrigger, "High Operating Temperature Limit Trigger");
        print_Temperature_Statistic(deviceStats->sasStatistics.highOperatingTemperatureLimitReset, "High Operating Temperature Limit Reset");
        print_Temperature_Statistic(deviceStats->sasStatistics.lowOperatingTemperatureLimitReset, "Low Operating Temperature Limit Reset");
        print_Temperature_Statistic(deviceStats->sasStatistics.lowOperatingTemperatureLimitTrigger, "Low Operating Temperature Limit Trigger");
        print_Humidity_Statistic(deviceStats->sasStatistics.highCriticalHumidityLimitTrigger, "High Critical Relative Humidity Limit Trigger");
        print_Humidity_Statistic(deviceStats->sasStatistics.highCriticalHumidityLimitReset, "High Critical Relative Humidity Limit Reset");
        print_Humidity_Statistic(deviceStats->sasStatistics.lowCriticalHumidityLimitReset, "Low Critical Relative Humidity Limit Reset");
        print_Humidity_Statistic(deviceStats->sasStatistics.lowCriticalHumidityLimitTrigger, "Low Critical Relative Humidity Limit Trigger");
        print_Humidity_Statistic(deviceStats->sasStatistics.highOperatingHumidityLimitTrigger, "High Operating Relative Humidity Limit Trigger");
        print_Humidity_Statistic(deviceStats->sasStatistics.highOperatingHumidityLimitReset, "High Operating Relative Humidity Limit Reset");
        print_Humidity_Statistic(deviceStats->sasStatistics.lowOperatingHumidityLimitReset, "Low Operating Relative Humidity Limit Reset");
        print_Humidity_Statistic(deviceStats->sasStatistics.lowOperatingHumidityLimitTrigger, "Low Operating Relative Humidity Limit Trigger");
    }
    if (deviceStats->sasStatistics.startStopCycleCounterSupported)
    {
        printf("\n---Start-Stop Cycle Counter---\n");
        print_SCSI_Date_Statistic(deviceStats->sasStatistics.dateOfManufacture, "Date Of Manufacture");
        print_SCSI_Date_Statistic(deviceStats->sasStatistics.accountingDate, "Accounting Date");
        print_Count_Statistic(deviceStats->sasStatistics.specifiedCycleCountOverDeviceLifetime, "Specified Cycle Count Over Device Lifetime", NULL);
        print_Count_Statistic(deviceStats->sasStatistics.accumulatedStartStopCycles, "Accumulated Start-Stop Cycles", NULL);
        print_Count_Statistic(deviceStats->sasStatistics.specifiedLoadUnloadCountOverDeviceLifetime, "Specified Load-Unload Count Over Device Lifetime", NULL);
        print_Count_Statistic(deviceStats->sasStatistics.accumulatedLoadUnloadCycles, "Accumulated Load-Unload Cycles", NULL);
    }
    if (deviceStats->sasStatistics.utilizationSupported)
    {
        printf("\n---Utilization---\n");
        print_Workload_Utilization_Statistic(deviceStats->sasStatistics.workloadUtilization, "Workload Utilization");
        print_Utilization_Usage_Rate_Statistic(deviceStats->sasStatistics.utilizationUsageRateBasedOnDateAndTime, "Utilization Usage Rate");
    }
    if (deviceStats->sasStatistics.solidStateMediaSupported)
    {
        printf("\n---Solid State Media---\n");
        print_Count_Statistic(deviceStats->sasStatistics.percentUsedEndurance, "Percent Used Endurance", "%");
    }
    if (deviceStats->sasStatistics.backgroundScanResultsSupported)
    {
        printf("\n---Background Scan Results---\n");
        print_Count_Statistic(deviceStats->sasStatistics.accumulatedPowerOnMinutes, "Accumulated Power On Minutes", "minutes");
        print_Count_Statistic(deviceStats->sasStatistics.numberOfBackgroundScansPerformed, "Number Of Background Scans Performed", NULL);
        print_Count_Statistic(deviceStats->sasStatistics.numberOfBackgroundMediaScansPerformed, "Number Of Background Media Scans Performed", NULL);
    }
    if (deviceStats->sasStatistics.pendingDefectsSupported)
    {
        printf("\n---Pending Defects---\n");
        print_Count_Statistic(deviceStats->sasStatistics.pendingDefectCount, "Pending Defect Count", NULL);
    }
    if (deviceStats->sasStatistics.lpsMisalignmentSupported)
    {
        printf("\n---LPS Misalignment---\n");
        print_Count_Statistic(deviceStats->sasStatistics.lpsMisalignmentCount, "LPS Misalignment Count", NULL);
    }
    if (deviceStats->sasStatistics.nvCacheSupported)
    {
        printf("\n---Non-Volatile Cache---\n");
        print_Non_Volatile_Time_Statistic(deviceStats->sasStatistics.remainingNonvolatileTime, "Remaining Non-Volatile Time");
        print_Non_Volatile_Time_Statistic(deviceStats->sasStatistics.maximumNonvolatileTime, "Maximum Non-Volatile Time");
    }
    if (deviceStats->sasStatistics.generalStatisticsAndPerformanceSupported)
    {
        printf("\n---General Statistics And Performance---\n");
        print_Count_Statistic(deviceStats->sasStatistics.numberOfReadCommands, "Number Of Read Commands", NULL);
        print_Count_Statistic(deviceStats->sasStatistics.numberOfWriteCommands, "Number Of Write Commands", NULL);
        print_Count_Statistic(deviceStats->sasStatistics.numberOfLogicalBlocksReceived, "Number Of Logical Blocks Received", NULL);
        print_Count_Statistic(deviceStats->sasStatistics.numberOfLogicalBlocksTransmitted, "Number Of Logical Blocks Transmitted", NULL);
        print_Count_Statistic(deviceStats->sasStatistics.readCommandProcessingIntervals, "Read Command Processing Intervals", NULL);
        print_Count_Statistic(deviceStats->sasStatistics.writeCommandProcessingIntervals, "Write Command Processing Intervals", NULL);
        print_Count_Statistic(deviceStats->sasStatistics.weightedNumberOfReadCommandsPlusWriteCommands, "Weighted Number Of Read Commands Plus Write Commands", NULL);
        print_Count_Statistic(deviceStats->sasStatistics.weightedReadCommandProcessingPlusWriteCommandProcessing, "Weighted Number Of Read Command Processing Plus Write Command Processing", NULL);
        print_Count_Statistic(deviceStats->sasStatistics.idleTimeIntervals, "Idle Time Intervals", NULL);
        print_SCSI_Time_Interval_Statistic(deviceStats->sasStatistics.timeIntervalDescriptor, "Time Interval Desriptor");
        print_Count_Statistic(deviceStats->sasStatistics.numberOfReadFUACommands, "Number Of Read FUA Commands", NULL);
        print_Count_Statistic(deviceStats->sasStatistics.numberOfWriteFUACommands, "Number Of Write FUA Commands", NULL);
        print_Count_Statistic(deviceStats->sasStatistics.numberOfReadFUANVCommands, "Number Of Read FUA NV Commands", NULL);
        print_Count_Statistic(deviceStats->sasStatistics.numberOfWriteFUANVCommands, "Number Of Write FUA NV Commands", NULL);
        print_Count_Statistic(deviceStats->sasStatistics.readFUACommandProcessingIntervals, "Read FUA Command Processing Intervals", NULL);
        print_Count_Statistic(deviceStats->sasStatistics.writeFUACommandProcessingIntervals, "Write FUA Command Processing Intervals", NULL);
        print_Count_Statistic(deviceStats->sasStatistics.readFUANVCommandProcessingIntervals, "Read FUA NV Command Processing Intervals", NULL);
        print_Count_Statistic(deviceStats->sasStatistics.writeFUANVCommandProcessingIntervals, "Write FUA NV Command Processing Intervals", NULL);
    }
    if (deviceStats->sasStatistics.cacheMemoryStatisticsSupported)
    {
        printf("\n---Cache Memory Statistics---\n");
        print_Count_Statistic(deviceStats->sasStatistics.readCacheMemoryHits, "Read Cache Memory Hits", NULL);
        print_Count_Statistic(deviceStats->sasStatistics.readsToCacheMemory, "Reads To Cache Memory", NULL);
        print_Count_Statistic(deviceStats->sasStatistics.writeCacheMemoryHits, "Write Cache Memory Hits", NULL);
        print_Count_Statistic(deviceStats->sasStatistics.writesFromCacheMemory, "Writes From Cache Memory", NULL);
        print_Count_Statistic(deviceStats->sasStatistics.timeFromLastHardReset, "Last Hard Reset Intervals", NULL);
        print_SCSI_Time_Interval_Statistic(deviceStats->sasStatistics.cacheTimeInterval, "Cache Memory Time Interval");
    }
    if (deviceStats->sasStatistics.timeStampSupported)
    {
        printf("\n---Timestamp---\n");
        print_Date_And_Time_Timestamp_Statistic(deviceStats->sasStatistics.dateAndTimeTimestamp, "Date And Time Timestamp");
    }
    if (deviceStats->sasStatistics.zonedDeviceStatisticsSupported)
    {
        printf("\n---Zoned Device Statistics---\n");
        print_Count_Statistic(deviceStats->sasStatistics.maximumOpenZones, "Maximum Open Zones", NULL);
        print_Count_Statistic(deviceStats->sasStatistics.maximumExplicitlyOpenZones, "Maximum Explicitly Open Zones", NULL);
        print_Count_Statistic(deviceStats->sasStatistics.maximumImplicitlyOpenZones, "Maximum Implicitly Open Zones", NULL);
        print_Count_Statistic(deviceStats->sasStatistics.minimumEmptyZones, "Minumum Empty Zones", NULL);
        print_Count_Statistic(deviceStats->sasStatistics.maximumNonSequentialZones, "Maximum Non-sequential Zones", NULL);
        print_Count_Statistic(deviceStats->sasStatistics.zonesEmptied, "Zones Emptied", NULL);
        print_Count_Statistic(deviceStats->sasStatistics.suboptimalWriteCommands, "Suboptimal Write Commands", NULL);
        print_Count_Statistic(deviceStats->sasStatistics.commandsExceedingOptimalLimit, "Commands Exceeding Optimal Limit", NULL);
        print_Count_Statistic(deviceStats->sasStatistics.failedExplicitOpens, "Failed Explicit Opens", NULL);
        print_Count_Statistic(deviceStats->sasStatistics.readRuleViolations, "Read Rule Violations", NULL);
        print_Count_Statistic(deviceStats->sasStatistics.writeRuleViolations, "Write Rule Violations", NULL);
    }
    return ret;
}

int print_DeviceStatistics(tDevice *device, ptrDeviceStatistics deviceStats)
{
    int ret = NOT_SUPPORTED;
    if (!deviceStats)
    {
        return MEMORY_FAILURE;
    }
    //as I write this I'm going to try and keep ATA and SCSI having the same printout format, but that may need to change...-TJE
    if (device->drive_info.drive_type == ATA_DRIVE)
    {
        return print_ATA_DeviceStatistics(device, deviceStats);
    }
    else if (device->drive_info.drive_type == SCSI_DRIVE)
    {
        return print_SCSI_DeviceStatistics(device, deviceStats);
    }
    return ret;
}
