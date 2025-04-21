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
// \file device_statistics.c
// \brief This file defines the functions related to getting/displaying device statistics

#include "bit_manip.h"
#include "code_attributes.h"
#include "common_types.h"
#include "error_translation.h"
#include "io_utils.h"
#include "math_utils.h"
#include "memory_safety.h"
#include "precision_timer.h"
#include "string_utils.h"
#include "time_utils.h"
#include "type_conversion.h"

#include "device_statistics.h"
#include "logs.h"

M_NONNULL_PARAM_LIST(1)
static M_INLINE statistic* dev_stat_general_offset_map(ptrDeviceStatistics deviceStats, uint16_t byteOffsetOnPage)
{
    statistic* stat = M_NULLPTR;
    switch (M_STATIC_CAST(eDevStatsGeneralOffsets, byteOffsetOnPage))
    {
    case ATA_DEV_STAT_GENERAL_LIFETIME_POR:
        stat = &deviceStats->sataStatistics.lifetimePoweronResets;
        break;
    case ATA_DEV_STAT_GENERAL_POH:
        stat = &deviceStats->sataStatistics.powerOnHours;
        break;
    case ATA_DEV_STAT_GENERAL_LBA_WRITTEN:
        stat = &deviceStats->sataStatistics.logicalSectorsWritten;
        break;
    case ATA_DEV_STAT_GENERAL_NUM_WRITE_CMDS:
        stat = &deviceStats->sataStatistics.numberOfWriteCommands;
        break;
    case ATA_DEV_STAT_GENERAL_LBA_READ:
        stat = &deviceStats->sataStatistics.logicalSectorsRead;
        break;
    case ATA_DEV_STAT_GENERAL_NUM_READ_CMDS:
        stat = &deviceStats->sataStatistics.numberOfReadCommands;
        break;
    case ATA_DEV_STAT_GENERAL_DATE_AND_TIME_TIMESTAMP:
        stat = &deviceStats->sataStatistics.dateAndTimeTimestamp;
        break;
    case ATA_DEV_STAT_GENERAL_PENDING_ERR_CNT:
        stat = &deviceStats->sataStatistics.pendingErrorCount;
        break;
    case ATA_DEV_STAT_GENERAL_WORKLOAD_UTIL:
        stat = &deviceStats->sataStatistics.workloadUtilization;
        break;
    case ATA_DEV_STAT_GENERAL_UTIL_USAGE_RATE:
        stat = &deviceStats->sataStatistics.utilizationUsageRate;
        break;
    case ATA_DEV_STAT_GENERAL_RESOURCE_AVAIL:
        stat = &deviceStats->sataStatistics.resourceAvailability;
        break;
    case ATA_DEV_STAT_GENERAL_RAND_WRITE_RESOURCE_USED:
        stat = &deviceStats->sataStatistics.randomWriteResourcesUsed;
        break;
    }
    return stat;
}

M_NONNULL_PARAM_LIST(1)
static M_INLINE statistic* dev_stat_freefall_offset_map(ptrDeviceStatistics deviceStats, uint16_t byteOffsetOnPage)
{
    statistic* stat = M_NULLPTR;
    switch (M_STATIC_CAST(eDevStatsFreeFallOffset, byteOffsetOnPage))
    {
    case ATA_DEV_STAT_FREEFALL_NUM_FREEFALL_EVENTS:
        stat = &deviceStats->sataStatistics.numberOfFreeFallEventsDetected;
        break;
    case ATA_DEV_STAT_FREEFALL_OVERLIM_SHOCK_EVENT:
        stat = &deviceStats->sataStatistics.overlimitShockEvents;
        break;
    }
    return stat;
}

M_NONNULL_PARAM_LIST(1)
static M_INLINE statistic* dev_stat_rotating_offset_map(ptrDeviceStatistics deviceStats, uint16_t byteOffsetOnPage)
{
    statistic* stat = M_NULLPTR;
    switch (M_STATIC_CAST(eDevStatsRotatingOffset, byteOffsetOnPage))
    {
    case ATA_DEV_STAT_ROTATING_SPINDLE_MOTOR_POH:
        stat = &deviceStats->sataStatistics.spindleMotorPoweronHours;
        break;
    case ATA_DEV_STAT_ROTATING_HEAD_FLYING_HOURS:
        stat = &deviceStats->sataStatistics.headFlyingHours;
        break;
    case ATA_DEV_STAT_ROTATING_HEAD_LOAD_EVENTS:
        stat = &deviceStats->sataStatistics.headLoadEvents;
        break;
    case ATA_DEV_STAT_ROTATING_NUM_REALLOCATED_LBA:
        stat = &deviceStats->sataStatistics.numberOfReallocatedLogicalSectors;
        break;
    case ATA_DEV_STAT_ROTATING_READ_RECOVERY_ATTEMPTS:
        stat = &deviceStats->sataStatistics.readRecoveryAttempts;
        break;
    case ATA_DEV_STAT_ROTATING_NUM_MECH_START_FAILURE:
        stat = &deviceStats->sataStatistics.numberOfMechanicalStartFailures;
        break;
    case ATA_DEV_STAT_ROTATING_NUM_REALLOCATION_CANDIDATE_LBA:
        stat = &deviceStats->sataStatistics.numberOfReallocationCandidateLogicalSectors;
        break;
    case ATA_DEV_STAT_ROTATING_NUM_HIGH_PRIO_UNLOAD_EVENTS:
        stat = &deviceStats->sataStatistics.numberOfHighPriorityUnloadEvents;
        break;
    }
    return stat;
}

M_NONNULL_PARAM_LIST(1)
static M_INLINE statistic* dev_stat_generallerror_offset_map(ptrDeviceStatistics deviceStats, uint16_t byteOffsetOnPage)
{
    statistic* stat = M_NULLPTR;
    switch (M_STATIC_CAST(eDevStatsGeneralErrorOffset, byteOffsetOnPage))
    {
    case ATA_DEV_STAT_GENERR_NUM_REPORTED_UNCOR_ERR:
        stat = &deviceStats->sataStatistics.numberOfReportedUncorrectableErrors;
        break;
    case ATA_DEV_STAT_GENERR_NUM_RESETS_BETWEEN_CMD_ACCEPT_AND_COMPLETE:
        stat = &deviceStats->sataStatistics.numberOfResetsBetweenCommandAcceptanceAndCommandCompletion;
        break;
    case ATA_DEV_STAT_GENERR_PHYSICAL_ELEMENT_STATUS_CHANGE:
        stat = &deviceStats->sataStatistics.physicalElementStatusChanged;
        break;
    }
    return stat;
}

M_NONNULL_PARAM_LIST(1)
static M_INLINE statistic* dev_stat_temperature_offset_map(ptrDeviceStatistics deviceStats, uint16_t byteOffsetOnPage)
{
    statistic* stat = M_NULLPTR;
    switch (M_STATIC_CAST(eDevStatsTemperatureOffset, byteOffsetOnPage))
    {
    case ATA_DEV_STAT_TEMP_CURRENT_TEMP:
        stat = &deviceStats->sataStatistics.currentTemperature;
        break;
    case ATA_DEV_STAT_TEMP_AVG_SHORT_TEMP:
        stat = &deviceStats->sataStatistics.averageShortTermTemperature;
        break;
    case ATA_DEV_STAT_TEMP_AVG_LONG_TEMP:
        stat = &deviceStats->sataStatistics.averageLongTermTemperature;
        break;
    case ATA_DEV_STAT_TEMP_HIGHEST_TEMP:
        stat = &deviceStats->sataStatistics.highestTemperature;
        break;
    case ATA_DEV_STAT_TEMP_LOWEST_TEMP:
        stat = &deviceStats->sataStatistics.lowestTemperature;
        break;
    case ATA_DEV_STAT_TEMP_HIGH_AVG_SHORT_TEMP:
        stat = &deviceStats->sataStatistics.highestAverageShortTermTemperature;
        break;
    case ATA_DEV_STAT_TEMP_LOW_AVG_SHORT_TEMP:
        stat = &deviceStats->sataStatistics.lowestAverageShortTermTemperature;
        break;
    case ATA_DEV_STAT_TEMP_HIGH_AVG_LONG_TEMP:
        stat = &deviceStats->sataStatistics.highestAverageLongTermTemperature;
        break;
    case ATA_DEV_STAT_TEMP_LOW_AVG_LONG_TEMP:
        stat = &deviceStats->sataStatistics.lowestAverageLongTermTemperature;
        break;
    case ATA_DEV_STAT_TEMP_TIME_OVER_TEMP:
        stat = &deviceStats->sataStatistics.timeInOverTemperature;
        break;
    case ATA_DEV_STAT_TEMP_SPEC_MAX_TEMP:
        stat = &deviceStats->sataStatistics.specifiedMaximumOperatingTemperature;
        break;
    case ATA_DEV_STAT_TEMP_TIME_UNDER_TEMP:
        stat = &deviceStats->sataStatistics.timeInUnderTemperature;
        break;
    case ATA_DEV_STAT_TEMP_SPEC_MIN_TEMP:
        stat = &deviceStats->sataStatistics.specifiedMinimumOperatingTemperature;
        break;
    }
    return stat;
}

M_NONNULL_PARAM_LIST(1)
static M_INLINE statistic* dev_stat_transport_offset_map(ptrDeviceStatistics deviceStats, uint16_t byteOffsetOnPage)
{
    statistic* stat = M_NULLPTR;
    switch (M_STATIC_CAST(eDevStatsTransportOffset, byteOffsetOnPage))
    {
    case ATA_DEV_STAT_TRANSPORT_NUM_HARD_RESET:
        stat = &deviceStats->sataStatistics.numberOfHardwareResets;
        break;
    case ATA_DEV_STAT_TRANSPORT_NUM_ASR_EVENTS:
        stat = &deviceStats->sataStatistics.numberOfASREvents;
        break;
    case ATA_DEV_STAT_TRANSPORT_NUM_CRC_ERRORS:
        stat = &deviceStats->sataStatistics.numberOfInterfaceCRCErrors;
        break;
    }
    return stat;
}

M_NONNULL_PARAM_LIST(1)
static M_INLINE statistic* dev_stat_ssd_offset_map(ptrDeviceStatistics deviceStats, uint16_t byteOffsetOnPage)
{
    statistic* stat = M_NULLPTR;
    switch (M_STATIC_CAST(eDevStatsSSDOffset, byteOffsetOnPage))
    {
    case ATA_DEV_STAT_SSD_ENDURANCE:
        stat = &deviceStats->sataStatistics.percentageUsedIndicator;
        break;
    }
    return stat;
}

M_NONNULL_PARAM_LIST(1)
static M_INLINE statistic* dev_stat_zoned_offset_map(ptrDeviceStatistics deviceStats, uint16_t byteOffsetOnPage)
{
    statistic* stat = M_NULLPTR;
    switch (M_STATIC_CAST(eDevStatsZonedOffset, byteOffsetOnPage))
    {
    case ATA_DEV_STAT_ZONED_MAX_OPEN_ZONES:
        stat = &deviceStats->sataStatistics.maximumOpenZones;
        break;
    case ATA_DEV_STAT_ZONED_MAX_EXPLICIT_OPEN_ZONES:
        stat = &deviceStats->sataStatistics.maximumExplicitlyOpenZones;
        break;
    case ATA_DEV_STAT_ZONED_MAX_IMPLICIT_OPEN_ZONES:
        stat = &deviceStats->sataStatistics.maximumImplicitlyOpenZones;
        break;
    case ATA_DEV_STAT_ZONED_MIN_EMPTY_ZONES:
        stat = &deviceStats->sataStatistics.minimumEmptyZones;
        break;
    case ATA_DEV_STAT_ZONED_MAX_NON_SEQ_ZONES:
        stat = &deviceStats->sataStatistics.maximumNonSequentialZones;
        break;
    case ATA_DEV_STAT_ZONED_ZONES_EMPTIED:
        stat = &deviceStats->sataStatistics.zonesEmptied;
        break;
    case ATA_DEV_STAT_ZONED_SUBOPTIMAL_WRITE_CMD:
        stat = &deviceStats->sataStatistics.suboptimalWriteCommands;
        break;
    case ATA_DEV_STAT_ZONED_CMD_EXCEED_OPTIMAL_LIM:
        stat = &deviceStats->sataStatistics.commandsExceedingOptimalLimit;
        break;
    case ATA_DEV_STAT_ZONED_FAILED_EXPLICIT_OPEN:
        stat = &deviceStats->sataStatistics.failedExplicitOpens;
        break;
    case ATA_DEV_STAT_ZONED_READ_RULE_VIOLATIONS:
        stat = &deviceStats->sataStatistics.readRuleViolations;
        break;
    case ATA_DEV_STAT_ZONED_WRITE_RULE_VIOLATIONS:
        stat = &deviceStats->sataStatistics.writeRuleViolations;
        break;
    case ATA_DEV_STAT_ZONED_MAX_IMPLICIT_OPEN_SEQ_OR_BEF_REQ_ZONES:
        stat = &deviceStats->sataStatistics.maximumImplicitOpenSequentialOrBeforeRequiredZones;
        break;
    }
    return stat;
}

static statistic* dev_stat_cdl_0_1_offset_map(ptrDeviceStatistics deviceStats, uint16_t byteOffsetOnPage)
{
    statistic* stat = M_NULLPTR;
    switch (M_STATIC_CAST(eDevStatsCDL_0_1_Offset, byteOffsetOnPage))
    {
    case ATA_DEV_STAT_CDL_LOWEST_ACHIEVABLE_CMD_DUR:
        stat = &deviceStats->sataStatistics.lowestAchievableCommandDuration;
        break;
    // Range 0 for STAT_A
    case ATA_DEV_STAT_CDL_RANGE0_STAT_A_R1:
        stat = &deviceStats->sataStatistics.cdlRange[0].groupA.readPolicy[0];
        break;
    case ATA_DEV_STAT_CDL_RANGE0_STAT_A_R2:
        stat = &deviceStats->sataStatistics.cdlRange[0].groupA.readPolicy[1];
        break;
    case ATA_DEV_STAT_CDL_RANGE0_STAT_A_R3:
        stat = &deviceStats->sataStatistics.cdlRange[0].groupA.readPolicy[2];
        break;
    case ATA_DEV_STAT_CDL_RANGE0_STAT_A_R4:
        stat = &deviceStats->sataStatistics.cdlRange[0].groupA.readPolicy[3];
        break;
    case ATA_DEV_STAT_CDL_RANGE0_STAT_A_R5:
        stat = &deviceStats->sataStatistics.cdlRange[0].groupA.readPolicy[4];
        break;
    case ATA_DEV_STAT_CDL_RANGE0_STAT_A_R6:
        stat = &deviceStats->sataStatistics.cdlRange[0].groupA.readPolicy[5];
        break;
    case ATA_DEV_STAT_CDL_RANGE0_STAT_A_R7:
        stat = &deviceStats->sataStatistics.cdlRange[0].groupA.readPolicy[6];
        break;
    case ATA_DEV_STAT_CDL_RANGE0_STAT_A_W1:
        stat = &deviceStats->sataStatistics.cdlRange[0].groupA.writePolicy[0];
        break;
    case ATA_DEV_STAT_CDL_RANGE0_STAT_A_W2:
        stat = &deviceStats->sataStatistics.cdlRange[0].groupA.writePolicy[1];
        break;
    case ATA_DEV_STAT_CDL_RANGE0_STAT_A_W3:
        stat = &deviceStats->sataStatistics.cdlRange[0].groupA.writePolicy[2];
        break;
    case ATA_DEV_STAT_CDL_RANGE0_STAT_A_W4:
        stat = &deviceStats->sataStatistics.cdlRange[0].groupA.writePolicy[3];
        break;
    case ATA_DEV_STAT_CDL_RANGE0_STAT_A_W5:
        stat = &deviceStats->sataStatistics.cdlRange[0].groupA.writePolicy[4];
        break;
    case ATA_DEV_STAT_CDL_RANGE0_STAT_A_W6:
        stat = &deviceStats->sataStatistics.cdlRange[0].groupA.writePolicy[5];
        break;
    case ATA_DEV_STAT_CDL_RANGE0_STAT_A_W7:
        stat = &deviceStats->sataStatistics.cdlRange[0].groupA.writePolicy[6];
        break;

    // Range 0 for STAT_B
    case ATA_DEV_STAT_CDL_RANGE0_STAT_B_R1:
        stat = &deviceStats->sataStatistics.cdlRange[0].groupB.readPolicy[0];
        break;
    case ATA_DEV_STAT_CDL_RANGE0_STAT_B_R2:
        stat = &deviceStats->sataStatistics.cdlRange[0].groupB.readPolicy[1];
        break;
    case ATA_DEV_STAT_CDL_RANGE0_STAT_B_R3:
        stat = &deviceStats->sataStatistics.cdlRange[0].groupB.readPolicy[2];
        break;
    case ATA_DEV_STAT_CDL_RANGE0_STAT_B_R4:
        stat = &deviceStats->sataStatistics.cdlRange[0].groupB.readPolicy[3];
        break;
    case ATA_DEV_STAT_CDL_RANGE0_STAT_B_R5:
        stat = &deviceStats->sataStatistics.cdlRange[0].groupB.readPolicy[4];
        break;
    case ATA_DEV_STAT_CDL_RANGE0_STAT_B_R6:
        stat = &deviceStats->sataStatistics.cdlRange[0].groupB.readPolicy[5];
        break;
    case ATA_DEV_STAT_CDL_RANGE0_STAT_B_R7:
        stat = &deviceStats->sataStatistics.cdlRange[0].groupB.readPolicy[6];
        break;
    case ATA_DEV_STAT_CDL_RANGE0_STAT_B_W1:
        stat = &deviceStats->sataStatistics.cdlRange[0].groupB.writePolicy[0];
        break;
    case ATA_DEV_STAT_CDL_RANGE0_STAT_B_W2:
        stat = &deviceStats->sataStatistics.cdlRange[0].groupB.writePolicy[1];
        break;
    case ATA_DEV_STAT_CDL_RANGE0_STAT_B_W3:
        stat = &deviceStats->sataStatistics.cdlRange[0].groupB.writePolicy[2];
        break;
    case ATA_DEV_STAT_CDL_RANGE0_STAT_B_W4:
        stat = &deviceStats->sataStatistics.cdlRange[0].groupB.writePolicy[3];
        break;
    case ATA_DEV_STAT_CDL_RANGE0_STAT_B_W5:
        stat = &deviceStats->sataStatistics.cdlRange[0].groupB.writePolicy[4];
        break;
    case ATA_DEV_STAT_CDL_RANGE0_STAT_B_W6:
        stat = &deviceStats->sataStatistics.cdlRange[0].groupB.writePolicy[5];
        break;
    case ATA_DEV_STAT_CDL_RANGE0_STAT_B_W7:
        stat = &deviceStats->sataStatistics.cdlRange[0].groupB.writePolicy[6];
        break;

    // Range 1 for STAT_A
    case ATA_DEV_STAT_CDL_RANGE1_STAT_A_R1:
        stat = &deviceStats->sataStatistics.cdlRange[1].groupA.readPolicy[0];
        break;
    case ATA_DEV_STAT_CDL_RANGE1_STAT_A_R2:
        stat = &deviceStats->sataStatistics.cdlRange[1].groupA.readPolicy[1];
        break;
    case ATA_DEV_STAT_CDL_RANGE1_STAT_A_R3:
        stat = &deviceStats->sataStatistics.cdlRange[1].groupA.readPolicy[2];
        break;
    case ATA_DEV_STAT_CDL_RANGE1_STAT_A_R4:
        stat = &deviceStats->sataStatistics.cdlRange[1].groupA.readPolicy[3];
        break;
    case ATA_DEV_STAT_CDL_RANGE1_STAT_A_R5:
        stat = &deviceStats->sataStatistics.cdlRange[1].groupA.readPolicy[4];
        break;
    case ATA_DEV_STAT_CDL_RANGE1_STAT_A_R6:
        stat = &deviceStats->sataStatistics.cdlRange[1].groupA.readPolicy[5];
        break;
    case ATA_DEV_STAT_CDL_RANGE1_STAT_A_R7:
        stat = &deviceStats->sataStatistics.cdlRange[1].groupA.readPolicy[6];
        break;
    case ATA_DEV_STAT_CDL_RANGE1_STAT_A_W1:
        stat = &deviceStats->sataStatistics.cdlRange[1].groupA.writePolicy[0];
        break;
    case ATA_DEV_STAT_CDL_RANGE1_STAT_A_W2:
        stat = &deviceStats->sataStatistics.cdlRange[1].groupA.writePolicy[1];
        break;
    case ATA_DEV_STAT_CDL_RANGE1_STAT_A_W3:
        stat = &deviceStats->sataStatistics.cdlRange[1].groupA.writePolicy[2];
        break;
    case ATA_DEV_STAT_CDL_RANGE1_STAT_A_W4:
        stat = &deviceStats->sataStatistics.cdlRange[1].groupA.writePolicy[3];
        break;
    case ATA_DEV_STAT_CDL_RANGE1_STAT_A_W5:
        stat = &deviceStats->sataStatistics.cdlRange[1].groupA.writePolicy[4];
        break;
    case ATA_DEV_STAT_CDL_RANGE1_STAT_A_W6:
        stat = &deviceStats->sataStatistics.cdlRange[1].groupA.writePolicy[5];
        break;
    case ATA_DEV_STAT_CDL_RANGE1_STAT_A_W7:
        stat = &deviceStats->sataStatistics.cdlRange[1].groupA.writePolicy[6];
        break;

    // Range 1 for STAT_B
    case ATA_DEV_STAT_CDL_RANGE1_STAT_B_R1:
        stat = &deviceStats->sataStatistics.cdlRange[1].groupB.readPolicy[0];
        break;
    case ATA_DEV_STAT_CDL_RANGE1_STAT_B_R2:
        stat = &deviceStats->sataStatistics.cdlRange[1].groupB.readPolicy[1];
        break;
    case ATA_DEV_STAT_CDL_RANGE1_STAT_B_R3:
        stat = &deviceStats->sataStatistics.cdlRange[1].groupB.readPolicy[2];
        break;
    case ATA_DEV_STAT_CDL_RANGE1_STAT_B_R4:
        stat = &deviceStats->sataStatistics.cdlRange[1].groupB.readPolicy[3];
        break;
    case ATA_DEV_STAT_CDL_RANGE1_STAT_B_R5:
        stat = &deviceStats->sataStatistics.cdlRange[1].groupB.readPolicy[4];
        break;
    case ATA_DEV_STAT_CDL_RANGE1_STAT_B_R6:
        stat = &deviceStats->sataStatistics.cdlRange[1].groupB.readPolicy[5];
        break;
    case ATA_DEV_STAT_CDL_RANGE1_STAT_B_R7:
        stat = &deviceStats->sataStatistics.cdlRange[1].groupB.readPolicy[6];
        break;
    case ATA_DEV_STAT_CDL_RANGE1_STAT_B_W1:
        stat = &deviceStats->sataStatistics.cdlRange[1].groupB.writePolicy[0];
        break;
    case ATA_DEV_STAT_CDL_RANGE1_STAT_B_W2:
        stat = &deviceStats->sataStatistics.cdlRange[1].groupB.writePolicy[1];
        break;
    case ATA_DEV_STAT_CDL_RANGE1_STAT_B_W3:
        stat = &deviceStats->sataStatistics.cdlRange[1].groupB.writePolicy[2];
        break;
    case ATA_DEV_STAT_CDL_RANGE1_STAT_B_W4:
        stat = &deviceStats->sataStatistics.cdlRange[1].groupB.writePolicy[3];
        break;
    case ATA_DEV_STAT_CDL_RANGE1_STAT_B_W5:
        stat = &deviceStats->sataStatistics.cdlRange[1].groupB.writePolicy[4];
        break;
    case ATA_DEV_STAT_CDL_RANGE1_STAT_B_W6:
        stat = &deviceStats->sataStatistics.cdlRange[1].groupB.writePolicy[5];
        break;
    case ATA_DEV_STAT_CDL_RANGE1_STAT_B_W7:
        stat = &deviceStats->sataStatistics.cdlRange[1].groupB.writePolicy[6];
        break;
    }
    return stat;
}

static statistic* dev_stat_cdl_2_3_offset_map(ptrDeviceStatistics deviceStats, uint16_t byteOffsetOnPage)
{
    statistic* stat = M_NULLPTR;
    switch (M_STATIC_CAST(eDevStatsCDL_2_3_Offset, byteOffsetOnPage))
    {
        // Range 2 for STAT_A
    case ATA_DEV_STAT_CDL_RANGE2_STAT_A_R1:
        stat = &deviceStats->sataStatistics.cdlRange[2].groupA.readPolicy[0];
        break;
    case ATA_DEV_STAT_CDL_RANGE2_STAT_A_R2:
        stat = &deviceStats->sataStatistics.cdlRange[2].groupA.readPolicy[1];
        break;
    case ATA_DEV_STAT_CDL_RANGE2_STAT_A_R3:
        stat = &deviceStats->sataStatistics.cdlRange[2].groupA.readPolicy[2];
        break;
    case ATA_DEV_STAT_CDL_RANGE2_STAT_A_R4:
        stat = &deviceStats->sataStatistics.cdlRange[2].groupA.readPolicy[3];
        break;
    case ATA_DEV_STAT_CDL_RANGE2_STAT_A_R5:
        stat = &deviceStats->sataStatistics.cdlRange[2].groupA.readPolicy[4];
        break;
    case ATA_DEV_STAT_CDL_RANGE2_STAT_A_R6:
        stat = &deviceStats->sataStatistics.cdlRange[2].groupA.readPolicy[5];
        break;
    case ATA_DEV_STAT_CDL_RANGE2_STAT_A_R7:
        stat = &deviceStats->sataStatistics.cdlRange[2].groupA.readPolicy[6];
        break;
    case ATA_DEV_STAT_CDL_RANGE2_STAT_A_W1:
        stat = &deviceStats->sataStatistics.cdlRange[2].groupA.writePolicy[0];
        break;
    case ATA_DEV_STAT_CDL_RANGE2_STAT_A_W2:
        stat = &deviceStats->sataStatistics.cdlRange[2].groupA.writePolicy[1];
        break;
    case ATA_DEV_STAT_CDL_RANGE2_STAT_A_W3:
        stat = &deviceStats->sataStatistics.cdlRange[2].groupA.writePolicy[2];
        break;
    case ATA_DEV_STAT_CDL_RANGE2_STAT_A_W4:
        stat = &deviceStats->sataStatistics.cdlRange[2].groupA.writePolicy[3];
        break;
    case ATA_DEV_STAT_CDL_RANGE2_STAT_A_W5:
        stat = &deviceStats->sataStatistics.cdlRange[2].groupA.writePolicy[4];
        break;
    case ATA_DEV_STAT_CDL_RANGE2_STAT_A_W6:
        stat = &deviceStats->sataStatistics.cdlRange[2].groupA.writePolicy[5];
        break;
    case ATA_DEV_STAT_CDL_RANGE2_STAT_A_W7:
        stat = &deviceStats->sataStatistics.cdlRange[2].groupA.writePolicy[6];
        break;

    // Range 2 for STAT_B
    case ATA_DEV_STAT_CDL_RANGE2_STAT_B_R1:
        stat = &deviceStats->sataStatistics.cdlRange[2].groupB.readPolicy[0];
        break;
    case ATA_DEV_STAT_CDL_RANGE2_STAT_B_R2:
        stat = &deviceStats->sataStatistics.cdlRange[2].groupB.readPolicy[1];
        break;
    case ATA_DEV_STAT_CDL_RANGE2_STAT_B_R3:
        stat = &deviceStats->sataStatistics.cdlRange[2].groupB.readPolicy[2];
        break;
    case ATA_DEV_STAT_CDL_RANGE2_STAT_B_R4:
        stat = &deviceStats->sataStatistics.cdlRange[2].groupB.readPolicy[3];
        break;
    case ATA_DEV_STAT_CDL_RANGE2_STAT_B_R5:
        stat = &deviceStats->sataStatistics.cdlRange[2].groupB.readPolicy[4];
        break;
    case ATA_DEV_STAT_CDL_RANGE2_STAT_B_R6:
        stat = &deviceStats->sataStatistics.cdlRange[2].groupB.readPolicy[5];
        break;
    case ATA_DEV_STAT_CDL_RANGE2_STAT_B_R7:
        stat = &deviceStats->sataStatistics.cdlRange[2].groupB.readPolicy[6];
        break;
    case ATA_DEV_STAT_CDL_RANGE2_STAT_B_W1:
        stat = &deviceStats->sataStatistics.cdlRange[2].groupB.writePolicy[0];
        break;
    case ATA_DEV_STAT_CDL_RANGE2_STAT_B_W2:
        stat = &deviceStats->sataStatistics.cdlRange[2].groupB.writePolicy[1];
        break;
    case ATA_DEV_STAT_CDL_RANGE2_STAT_B_W3:
        stat = &deviceStats->sataStatistics.cdlRange[2].groupB.writePolicy[2];
        break;
    case ATA_DEV_STAT_CDL_RANGE2_STAT_B_W4:
        stat = &deviceStats->sataStatistics.cdlRange[2].groupB.writePolicy[3];
        break;
    case ATA_DEV_STAT_CDL_RANGE2_STAT_B_W5:
        stat = &deviceStats->sataStatistics.cdlRange[2].groupB.writePolicy[4];
        break;
    case ATA_DEV_STAT_CDL_RANGE2_STAT_B_W6:
        stat = &deviceStats->sataStatistics.cdlRange[2].groupB.writePolicy[5];
        break;
    case ATA_DEV_STAT_CDL_RANGE2_STAT_B_W7:
        stat = &deviceStats->sataStatistics.cdlRange[2].groupB.writePolicy[6];
        break;

    // Range 3 for STAT_A
    case ATA_DEV_STAT_CDL_RANGE3_STAT_A_R1:
        stat = &deviceStats->sataStatistics.cdlRange[3].groupA.readPolicy[0];
        break;
    case ATA_DEV_STAT_CDL_RANGE3_STAT_A_R2:
        stat = &deviceStats->sataStatistics.cdlRange[3].groupA.readPolicy[1];
        break;
    case ATA_DEV_STAT_CDL_RANGE3_STAT_A_R3:
        stat = &deviceStats->sataStatistics.cdlRange[3].groupA.readPolicy[2];
        break;
    case ATA_DEV_STAT_CDL_RANGE3_STAT_A_R4:
        stat = &deviceStats->sataStatistics.cdlRange[3].groupA.readPolicy[3];
        break;
    case ATA_DEV_STAT_CDL_RANGE3_STAT_A_R5:
        stat = &deviceStats->sataStatistics.cdlRange[3].groupA.readPolicy[4];
        break;
    case ATA_DEV_STAT_CDL_RANGE3_STAT_A_R6:
        stat = &deviceStats->sataStatistics.cdlRange[3].groupA.readPolicy[5];
        break;
    case ATA_DEV_STAT_CDL_RANGE3_STAT_A_R7:
        stat = &deviceStats->sataStatistics.cdlRange[3].groupA.readPolicy[6];
        break;
    case ATA_DEV_STAT_CDL_RANGE3_STAT_A_W1:
        stat = &deviceStats->sataStatistics.cdlRange[3].groupA.writePolicy[0];
        break;
    case ATA_DEV_STAT_CDL_RANGE3_STAT_A_W2:
        stat = &deviceStats->sataStatistics.cdlRange[3].groupA.writePolicy[1];
        break;
    case ATA_DEV_STAT_CDL_RANGE3_STAT_A_W3:
        stat = &deviceStats->sataStatistics.cdlRange[3].groupA.writePolicy[2];
        break;
    case ATA_DEV_STAT_CDL_RANGE3_STAT_A_W4:
        stat = &deviceStats->sataStatistics.cdlRange[3].groupA.writePolicy[3];
        break;
    case ATA_DEV_STAT_CDL_RANGE3_STAT_A_W5:
        stat = &deviceStats->sataStatistics.cdlRange[3].groupA.writePolicy[4];
        break;
    case ATA_DEV_STAT_CDL_RANGE3_STAT_A_W6:
        stat = &deviceStats->sataStatistics.cdlRange[3].groupA.writePolicy[5];
        break;
    case ATA_DEV_STAT_CDL_RANGE3_STAT_A_W7:
        stat = &deviceStats->sataStatistics.cdlRange[3].groupA.writePolicy[6];
        break;
    // Range 3 for STAT_B
    case ATA_DEV_STAT_CDL_RANGE3_STAT_B_R1:
        stat = &deviceStats->sataStatistics.cdlRange[3].groupB.readPolicy[0];
        break;
    case ATA_DEV_STAT_CDL_RANGE3_STAT_B_R2:
        stat = &deviceStats->sataStatistics.cdlRange[3].groupB.readPolicy[1];
        break;
    case ATA_DEV_STAT_CDL_RANGE3_STAT_B_R3:
        stat = &deviceStats->sataStatistics.cdlRange[3].groupB.readPolicy[2];
        break;
    case ATA_DEV_STAT_CDL_RANGE3_STAT_B_R4:
        stat = &deviceStats->sataStatistics.cdlRange[3].groupB.readPolicy[3];
        break;
    case ATA_DEV_STAT_CDL_RANGE3_STAT_B_R5:
        stat = &deviceStats->sataStatistics.cdlRange[3].groupB.readPolicy[4];
        break;
    case ATA_DEV_STAT_CDL_RANGE3_STAT_B_R6:
        stat = &deviceStats->sataStatistics.cdlRange[3].groupB.readPolicy[5];
        break;
    case ATA_DEV_STAT_CDL_RANGE3_STAT_B_R7:
        stat = &deviceStats->sataStatistics.cdlRange[3].groupB.readPolicy[6];
        break;
    case ATA_DEV_STAT_CDL_RANGE3_STAT_B_W1:
        stat = &deviceStats->sataStatistics.cdlRange[3].groupB.writePolicy[0];
        break;
    case ATA_DEV_STAT_CDL_RANGE3_STAT_B_W2:
        stat = &deviceStats->sataStatistics.cdlRange[3].groupB.writePolicy[1];
        break;
    case ATA_DEV_STAT_CDL_RANGE3_STAT_B_W3:
        stat = &deviceStats->sataStatistics.cdlRange[3].groupB.writePolicy[2];
        break;
    case ATA_DEV_STAT_CDL_RANGE3_STAT_B_W4:
        stat = &deviceStats->sataStatistics.cdlRange[3].groupB.writePolicy[3];
        break;
    case ATA_DEV_STAT_CDL_RANGE3_STAT_B_W5:
        stat = &deviceStats->sataStatistics.cdlRange[3].groupB.writePolicy[4];
        break;
    case ATA_DEV_STAT_CDL_RANGE3_STAT_B_W6:
        stat = &deviceStats->sataStatistics.cdlRange[3].groupB.writePolicy[5];
        break;
    case ATA_DEV_STAT_CDL_RANGE3_STAT_B_W7:
        stat = &deviceStats->sataStatistics.cdlRange[3].groupB.writePolicy[6];
        break;
    }
    return stat;
}

// this is ued to determine which device statistic is being talked about by the DSN log on ata
// TODO: Make enum of all stat offsets on each page so it is easy to make sure all cases are handled correctly
M_NONNULL_PARAM_LIST(1)
static statistic* dev_stat_page_offset_map(ptrDeviceStatistics deviceStats,
                                           uint8_t             ataDevStatPage,
                                           uint16_t            byteOffsetOnPage)
{
    statistic* stat = M_NULLPTR;
    switch (M_STATIC_CAST(eDeviceStatisticsLog, ataDevStatPage))
    {
    case ATA_DEVICE_STATS_LOG_LIST:
        break;
    case ATA_DEVICE_STATS_LOG_GENERAL:
        stat = dev_stat_general_offset_map(deviceStats, byteOffsetOnPage);
        break;
    case ATA_DEVICE_STATS_LOG_FREE_FALL:
        stat = dev_stat_freefall_offset_map(deviceStats, byteOffsetOnPage);
        break;
    case ATA_DEVICE_STATS_LOG_ROTATING_MEDIA:
        stat = dev_stat_rotating_offset_map(deviceStats, byteOffsetOnPage);
        break;
    case ATA_DEVICE_STATS_LOG_GEN_ERR:
        stat = dev_stat_generallerror_offset_map(deviceStats, byteOffsetOnPage);
        break;
    case ATA_DEVICE_STATS_LOG_TEMP:
        stat = dev_stat_temperature_offset_map(deviceStats, byteOffsetOnPage);
        break;
    case ATA_DEVICE_STATS_LOG_TRANSPORT:
        stat = dev_stat_transport_offset_map(deviceStats, byteOffsetOnPage);
        break;
    case ATA_DEVICE_STATS_LOG_SSD:
        stat = dev_stat_ssd_offset_map(deviceStats, byteOffsetOnPage);
        break;
    case ATA_DEVICE_STATS_LOG_ZONED_DEVICE:
        stat = dev_stat_zoned_offset_map(deviceStats, byteOffsetOnPage);
        break;
    case ATA_DEVICE_STATS_LOG_CDL_LBA_RANGE_0_1:
        stat = dev_stat_cdl_0_1_offset_map(deviceStats, byteOffsetOnPage);
        break;
    case ATA_DEVICE_STATS_LOG_CDL_LBA_RANGE_2_3:
        stat = dev_stat_cdl_2_3_offset_map(deviceStats, byteOffsetOnPage);
        break;
    case ATA_DEVICE_STATS_LOG_VENDOR_SPECIFIC:
        // slightly different than case by case here -TJE
        stat = &deviceStats->sataStatistics.vendorSpecificStatistics[(byteOffsetOnPage / UINT8_C(8)) - 1];
        break;
    }
    return stat;
}

M_NONNULL_PARAM_LIST(1) M_PARAM_RW(1) void scsi_Threshold_Comparison(statistic* ptrStatistic); // prototype

static void set_ATA_Dev_Stat_Notification_Info(uint64_t statisticCondition, statistic* stat)
{
    if (stat != M_NULLPTR)
    {
        // device statistics condition definition:
        // Bits 63:56 = DSN Condition Flags (below)
        // Bit 63 = notification enabled
        // Bits 62:60 = value comparison type
        //   000b = does not trigger on any update
        //   001b = triggers on every update of the statistics value
        //   010b = triggers on the device statistic value equal to the threshold value
        //   011b = triggers on the device statistic value less than the threshold value
        //   100b = triggers on the device statistic value greater than the threshold value
        // Bit 59 = non-validity trigger
        // Bit 58 = validity trigger
        uint8_t dsnConditionFlags   = M_Byte7(statisticCondition);
        bool    notificationEnabled = dsnConditionFlags & BIT7;
        uint8_t comparisonType      = M_Nibble1(dsnConditionFlags) & 0x03;
        bool    nonValidityTrigger  = dsnConditionFlags & BIT3;
        bool    validityTrigger     = dsnConditionFlags & BIT2;
        // Bits 55:0 = Threshold Value
        uint64_t thresholdValue            = statisticCondition & UINT64_C(0x00FFFFFFFFFFFFFF); // removing byte 7
        stat->isThresholdValid             = true;
        stat->thresholdNotificationEnabled = notificationEnabled;
        stat->threshType                   = C_CAST(eThresholdType, comparisonType);
        stat->nonValidityTrigger           = nonValidityTrigger;
        stat->validityTrigger              = validityTrigger;
        stat->threshold                    = thresholdValue;
    }
}

// NOTE: call le64 to host on qword when passing in to keep this simpler!
M_PARAM_WO(2)
static bool set_ATA_Dev_Stat_Info(uint64_t qword, statistic* stat)
{
    bool statisticPopulated = false;
    if (stat != M_NULLPTR)
    {
        if (qword & ATA_DEV_STATS_STATISTIC_SUPPORTED_BIT)
        {
            stat->isSupported                = true;
            stat->isValueValid               = M_ToBool(qword & ATA_DEV_STATS_VALID_VALUE_BIT);
            stat->isNormalized               = M_ToBool(qword & ATA_DEV_STATS_NORMALIZED_STAT_BIT);
            stat->supportsNotification       = M_ToBool(qword & ATA_DEV_STATS_SUPPORTS_DSN);
            stat->monitoredConditionMet      = M_ToBool(qword & ATA_DEV_STATS_MONITORED_CONDITION_MET);
            stat->supportsReadThenInitialize = M_ToBool(qword & ATA_DEV_STATS_READ_THEN_INIT_SUPPORTED);
            stat->statisticValue = get_bit_range_uint64(qword, ATA_DEV_STATS_VALUE_MSB, ATA_DEV_STATS_VALUE_LSB);
            statisticPopulated   = true;
        }
        else
        {
            stat->isSupported = false;
        }
    }
    return statisticPopulated;
}

static eReturnValues get_ATA_DeviceStatistics(tDevice* device, ptrDeviceStatistics deviceStats)
{
    eReturnValues ret = NOT_SUPPORTED;
    if (deviceStats == M_NULLPTR)
    {
        return BAD_PARAMETER;
    }
    uint32_t deviceStatsSize              = UINT32_C(0);
    uint32_t deviceStatsNotificationsSize = UINT32_C(0);
    // need to get the device statistics log
    if (SUCCESS == get_ATA_Log_Size(device, ATA_LOG_DEVICE_STATISTICS, &deviceStatsSize, true, true) &&
        deviceStatsSize > UINT32_C(0))
    {
        bool     dsnFeatureSupported = M_ToBool(le16_to_host(device->drive_info.IdentifyData.ata.Word119) & BIT9);
        bool     dsnFeatureEnabled   = M_ToBool(le16_to_host(device->drive_info.IdentifyData.ata.Word120) & BIT9);
        uint8_t* deviceStatsLog      = M_REINTERPRET_CAST(
            uint8_t*, safe_calloc_aligned(deviceStatsSize, sizeof(uint8_t), device->os_info.minimumAlignment));
        if (deviceStatsLog == M_NULLPTR)
        {
            return MEMORY_FAILURE;
        }
        // this is to get the threshold stuff
        if (dsnFeatureSupported && dsnFeatureEnabled &&
            SUCCESS == get_ATA_Log_Size(device, ATA_LOG_DEVICE_STATISTICS_NOTIFICATION, &deviceStatsNotificationsSize,
                                        true, false) &&
            deviceStatsNotificationsSize > UINT32_C(0))
        {
            uint8_t* devStatsNotificationsLog =
                M_REINTERPRET_CAST(uint8_t*, safe_calloc_aligned(deviceStatsNotificationsSize, sizeof(uint8_t),
                                                                 device->os_info.minimumAlignment));
            if (SUCCESS == get_ATA_Log(device, ATA_LOG_DEVICE_STATISTICS_NOTIFICATION, M_NULLPTR, M_NULLPTR, true,
                                       false, true, devStatsNotificationsLog, deviceStatsNotificationsSize, M_NULLPTR,
                                       0, 0))
            {
                // Start at page 1 since we want all the details, not just the summary from page 0
                // increment by 2 qwords and go through each statistic and it's condition individually
                for (uint32_t offset = LEGACY_DRIVE_SEC_SIZE; offset < deviceStatsNotificationsSize; offset += 16)
                {
                    uint64_t statisticLocation =
                        M_BytesTo8ByteValue(devStatsNotificationsLog[offset + 7], devStatsNotificationsLog[offset + 6],
                                            devStatsNotificationsLog[offset + 5], devStatsNotificationsLog[offset + 4],
                                            devStatsNotificationsLog[offset + 3], devStatsNotificationsLog[offset + 2],
                                            devStatsNotificationsLog[offset + 1], devStatsNotificationsLog[offset]);
                    uint64_t statisticCondition = M_BytesTo8ByteValue(
                        devStatsNotificationsLog[offset + 15], devStatsNotificationsLog[offset + 14],
                        devStatsNotificationsLog[offset + 13], devStatsNotificationsLog[offset + 12],
                        devStatsNotificationsLog[offset + 11], devStatsNotificationsLog[offset + 10],
                        devStatsNotificationsLog[offset + 9], devStatsNotificationsLog[offset + 8]);
                    uint8_t statisticLogPage    = M_Byte3(statisticLocation);
                    uint8_t statisticByteOffset = M_Byte0(statisticLocation);
                    set_ATA_Dev_Stat_Notification_Info(
                        statisticCondition,
                        dev_stat_page_offset_map(deviceStats, statisticLogPage, statisticByteOffset));
                }
            }
            safe_free_aligned(&devStatsNotificationsLog);
        }
        if (SUCCESS == get_ATA_Log(device, ATA_LOG_DEVICE_STATISTICS, M_NULLPTR, M_NULLPTR, true, true, true,
                                   deviceStatsLog, deviceStatsSize, M_NULLPTR, 0, 0))
        {
            uint32_t  offset                 = UINT32_C(0); // start offset 1 sector to get to the general statistics
            uint64_t* qwordPtrDeviceStatsLog = M_NULLPTR;
            ret                              = SUCCESS;
            for (uint8_t pageIter = UINT8_C(0); pageIter < deviceStatsLog[ATA_DEV_STATS_SUP_PG_LIST_LEN_OFFSET];
                 ++pageIter)
            {
                uint8_t statisticPage = deviceStatsLog[ATA_DEV_STATS_SUP_PG_LIST_OFFSET + pageIter];
                offset                = statisticPage * LEGACY_DRIVE_SEC_SIZE;
                if (offset > deviceStatsSize)
                {
                    // this exists for the hack loop above
                    break;
                }
                qwordPtrDeviceStatsLog = C_CAST(uint64_t*, &deviceStatsLog[offset]);
                switch (statisticPage)
                {
                case ATA_DEVICE_STATS_LOG_LIST: // supported pages page...
                    continue;
                    break;
                case ATA_DEVICE_STATS_LOG_GENERAL: // general statistics
                    if (ATA_DEVICE_STATS_LOG_GENERAL == M_Byte2(le64_to_host(qwordPtrDeviceStatsLog[0])))
                    {
                        deviceStats->sataStatistics.generalStatisticsSupported = true;
                    }
                    else
                    {
                        continue;
                    }
                    break;
                case ATA_DEVICE_STATS_LOG_FREE_FALL: // free fall statistics
                    if (ATA_DEVICE_STATS_LOG_FREE_FALL == M_Byte2(le64_to_host(qwordPtrDeviceStatsLog[0])))
                    {
                        deviceStats->sataStatistics.freeFallStatisticsSupported = true;
                    }
                    else
                    {
                        continue;
                    }
                    break;
                case ATA_DEVICE_STATS_LOG_ROTATING_MEDIA: // rotating media statistics
                    if (ATA_DEVICE_STATS_LOG_ROTATING_MEDIA == M_Byte2(le64_to_host(qwordPtrDeviceStatsLog[0])))
                    {
                        deviceStats->sataStatistics.rotatingMediaStatisticsSupported = true;
                    }
                    else
                    {
                        continue;
                    }
                    break;
                case ATA_DEVICE_STATS_LOG_GEN_ERR: // general errors statistics
                    if (ATA_DEVICE_STATS_LOG_GEN_ERR == M_Byte2(le64_to_host(qwordPtrDeviceStatsLog[0])))
                    {
                        deviceStats->sataStatistics.generalErrorsStatisticsSupported = true;
                    }
                    else
                    {
                        continue;
                    }
                    break;
                case ATA_DEVICE_STATS_LOG_TEMP: // temperature statistics
                    if (ATA_DEVICE_STATS_LOG_TEMP == M_Byte2(le64_to_host(qwordPtrDeviceStatsLog[0])))
                    {
                        deviceStats->sataStatistics.temperatureStatisticsSupported = true;
                    }
                    else
                    {
                        continue;
                    }
                    break;
                case ATA_DEVICE_STATS_LOG_TRANSPORT: // transport statistics
                    if (ATA_DEVICE_STATS_LOG_TRANSPORT == M_Byte2(le64_to_host(qwordPtrDeviceStatsLog[0])))
                    {
                        deviceStats->sataStatistics.transportStatisticsSupported = true;
                    }
                    else
                    {
                        continue;
                    }
                    break;
                case ATA_DEVICE_STATS_LOG_SSD: // solid state device statistics
                    if (ATA_DEVICE_STATS_LOG_SSD == M_Byte2(le64_to_host(qwordPtrDeviceStatsLog[0])))
                    {
                        deviceStats->sataStatistics.ssdStatisticsSupported = true;
                    }
                    else
                    {
                        continue;
                    }
                    break;
                case ATA_DEVICE_STATS_LOG_ZONED_DEVICE: // ZAC statistics
                    if (ATA_DEVICE_STATS_LOG_ZONED_DEVICE == M_Byte2(le64_to_host(qwordPtrDeviceStatsLog[0])))
                    {
                        deviceStats->sataStatistics.zonedDeviceStatisticsSupported = true;
                    }
                    else
                    {
                        continue;
                    }
                    break;
                case ATA_DEVICE_STATS_LOG_CDL_LBA_RANGE_0_1:
                    if (ATA_DEVICE_STATS_LOG_CDL_LBA_RANGE_0_1 == M_Byte2(le64_to_host(qwordPtrDeviceStatsLog[0])))
                    {
                        deviceStats->sataStatistics.cdlStatisticsSupported = true;
                    }
                    else
                    {
                        continue;
                    }
                    break;
                case ATA_DEVICE_STATS_LOG_CDL_LBA_RANGE_2_3:
                    if (ATA_DEVICE_STATS_LOG_CDL_LBA_RANGE_2_3 == M_Byte2(le64_to_host(qwordPtrDeviceStatsLog[0])))
                    {
                        deviceStats->sataStatistics.cdlStatisticsSupported = true;
                    }
                    else
                    {
                        continue;
                    }
                    break;
                case ATA_DEVICE_STATS_LOG_VENDOR_SPECIFIC: // vendor specific
                    if (ATA_DEVICE_STATS_LOG_VENDOR_SPECIFIC == M_Byte2(le64_to_host(qwordPtrDeviceStatsLog[0])))
                    {
                        deviceStats->sataStatistics.vendorSpecificStatisticsSupported = true;
                    }
                    else
                    {
                        continue;
                    }
                    break;
                default:
                    continue;
                    break;
                }

                for (uint16_t statisticOffset = UINT16_C(8); statisticOffset < LEGACY_DRIVE_SEC_SIZE;
                     statisticOffset += UINT16_C(8))
                {
                    // TODO: Need to adjust min/max field offsets based on the attribute. For now selecting all 48
                    // possible bits seems ok.
                    //       Need more testing and to come back to this again later.
                    uint16_t statisticNumberOnPage = statisticOffset / UINT16_C(8);
                    if (set_ATA_Dev_Stat_Info(le64_to_host(qwordPtrDeviceStatsLog[statisticNumberOnPage]),
                                              dev_stat_page_offset_map(deviceStats, statisticPage, statisticOffset)))
                    {
                        ++deviceStats->sataStatistics.statisticsPopulated;
                        if (statisticPage == ATA_DEVICE_STATS_LOG_VENDOR_SPECIFIC)
                        {
                            ++deviceStats->sataStatistics.vendorSpecificStatisticsPopulated;
                        }
                        else if (statisticPage == ATA_DEVICE_STATS_LOG_CDL_LBA_RANGE_0_1)
                        {
                            if (statisticOffset >= ATA_DEV_STAT_CDL_RANGE1_STAT_A_R1)
                            {
                                deviceStats->sataStatistics.cdlStatisticRanges = 2;
                            }
                            else if (statisticOffset >= ATA_DEV_STAT_CDL_RANGE0_STAT_A_R1)
                            {
                                deviceStats->sataStatistics.cdlStatisticRanges = 1;
                            }
                        }
                        else if (statisticPage == ATA_DEVICE_STATS_LOG_CDL_LBA_RANGE_2_3)
                        {
                            if (statisticOffset >= ATA_DEV_STAT_CDL_RANGE3_STAT_A_R1)
                            {
                                deviceStats->sataStatistics.cdlStatisticRanges = 4;
                            }
                            else if (statisticOffset >= ATA_DEV_STAT_CDL_RANGE2_STAT_A_R1)
                            {
                                deviceStats->sataStatistics.cdlStatisticRanges = 3;
                            }
                        }
                    }
                }
            }
        }
        safe_free_aligned(&deviceStatsLog);
    }
    return ret;
}

static eReturnValues get_SCSI_DeviceStatistics(tDevice* device, ptrDeviceStatistics deviceStats)
{
    eReturnValues ret = NOT_SUPPORTED;
    if (deviceStats == M_NULLPTR)
    {
        return BAD_PARAMETER;
    }
    DECLARE_ZERO_INIT_ARRAY(uint8_t, supportedLogPages, LEGACY_DRIVE_SEC_SIZE);
    // read list of supported logs, the with that list we'll populate the statistics data
    bool dummyUpLogPages   = false;
    bool subpagesSupported = true;
    if (SUCCESS != scsi_Log_Sense_Cmd(device, false, LPC_CUMULATIVE_VALUES, LP_SUPPORTED_LOG_PAGES_AND_SUBPAGES, 0xFF,
                                      0, supportedLogPages, LEGACY_DRIVE_SEC_SIZE))
    {
        // either device doesn't support logs, or it just doesn't support subpages, so let's try reading the list of
        // supported pages (no subpages) before saying we need to dummy up the list
        if (SUCCESS != scsi_Log_Sense_Cmd(device, false, LPC_CUMULATIVE_VALUES, LP_SUPPORTED_LOG_PAGES, 0, 0,
                                          supportedLogPages, LEGACY_DRIVE_SEC_SIZE))
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
        // memcmp to make sure we weren't given zeros
        DECLARE_ZERO_INIT_ARRAY(uint8_t, zeroMem, LEGACY_DRIVE_SEC_SIZE);
        if (memcmp(zeroMem, supportedLogPages, LEGACY_DRIVE_SEC_SIZE) == 0)
        {
            dummyUpLogPages = true;
        }
    }
    // this is really a work-around for USB drives since some DO support pages, but the don't actually list them (same
    // as the VPD pages above). Most USB drives don't work though - TJE
    if (dummyUpLogPages)
    {
        subpagesSupported = true;
        safe_memset(supportedLogPages, LEGACY_DRIVE_SEC_SIZE, 0, LEGACY_DRIVE_SEC_SIZE);
        supportedLogPages[0] = 0;
        supportedLogPages[1] = 0;
        // page length
        supportedLogPages[2] = 0;
        supportedLogPages[3] = 0x29; // <---increment me when adding a new dummy page below
        // descriptors (2 bytes per page for pages + subpage format)) if you add a new page here, make the page length
        // above bigger
        supportedLogPages[4]  = LP_SUPPORTED_LOG_PAGES; // just to be correct/accurate
        supportedLogPages[5]  = 0;                      // subpage
        supportedLogPages[6]  = LP_SUPPORTED_LOG_PAGES_AND_SUBPAGES;
        supportedLogPages[7]  = 0xFF; // supported subpages
        supportedLogPages[8]  = LP_WRITE_ERROR_COUNTERS;
        supportedLogPages[9]  = 0; // subpage
        supportedLogPages[10] = LP_READ_ERROR_COUNTERS;
        supportedLogPages[11] = 0; // subpage
        supportedLogPages[12] = LP_READ_REVERSE_ERROR_COUNTERS;
        supportedLogPages[13] = 0; // subpage
        supportedLogPages[14] = LP_VERIFY_ERROR_COUNTERS;
        supportedLogPages[15] = 0; // subpage
        supportedLogPages[16] = LP_NON_MEDIUM_ERROR;
        supportedLogPages[17] = 0; // subpage
        supportedLogPages[18] = LP_FORMAT_STATUS_LOG_PAGE;
        supportedLogPages[19] = 0; // subpage
        supportedLogPages[20] = LP_LOGICAL_BLOCK_PROVISIONING;
        supportedLogPages[21] = 0; // subpage
        supportedLogPages[22] = LP_TEMPERATURE;
        supportedLogPages[23] = 0; // subpage
        supportedLogPages[24] = LP_ENVIRONMENTAL_REPORTING;
        supportedLogPages[25] = 0x01; // subpage (page number is same as temperature)
        supportedLogPages[26] = LP_ENVIRONMENTAL_LIMITS;
        supportedLogPages[27] = 0x02; // subpage (page number is same as temperature)
        supportedLogPages[28] = LP_START_STOP_CYCLE_COUNTER;
        supportedLogPages[29] = 0;
        supportedLogPages[30] = LP_UTILIZATION;
        supportedLogPages[31] = 0x01; // subpage (page number is same as start stop cycle counter)
        supportedLogPages[32] = LP_SOLID_STATE_MEDIA;
        supportedLogPages[33] = 0; // subpage
        supportedLogPages[34] = LP_BACKGROUND_SCAN_RESULTS;
        supportedLogPages[35] = 0; // subpage
        supportedLogPages[36] = LP_PENDING_DEFECTS;
        supportedLogPages[37] = 0x01; // subpage (page number is same as background scan results)
        supportedLogPages[38] = LP_LPS_MISALLIGNMENT;
        supportedLogPages[39] = 0x03; // subpage (page number is same as background scan results)
        supportedLogPages[40] = LP_NON_VOLITILE_CACHE;
        supportedLogPages[41] = 0; // subpage
        supportedLogPages[42] = LP_GENERAL_STATISTICS_AND_PERFORMANCE;
        supportedLogPages[43] = 0; // subpage
        supportedLogPages[44] = LP_CACHE_MEMORY_STATISTICS;
        supportedLogPages[45] = 0x20; // subpage (page number is same as general statistics and performance)
    }
    uint32_t logPageIter = LOG_PAGE_HEADER_LENGTH; // log page descriptors start on offset 4 and are 2 bytes long each
    uint16_t supportedPagesLength = M_BytesTo2ByteValue(supportedLogPages[2], supportedLogPages[3]);
    uint8_t  incrementAmount      = subpagesSupported ? 2 : 1;
    DECLARE_ZERO_INIT_ARRAY(uint8_t, tempLogBuf, LEGACY_DRIVE_SEC_SIZE);
    for (; logPageIter <
           M_Min(M_STATIC_CAST(uint32_t, supportedPagesLength) + LOG_PAGE_HEADER_LENGTH, LEGACY_DRIVE_SEC_SIZE);
         logPageIter += incrementAmount)
    {
        uint8_t pageCode    = supportedLogPages[logPageIter] & 0x3F; // outer switch statement
        uint8_t subpageCode = UINT8_C(0);
        if (subpagesSupported)
        {
            subpageCode = supportedLogPages[logPageIter + 1]; // inner switch statement
        }
        switch (pageCode)
        {
        case LP_WRITE_ERROR_COUNTERS:
            if (subpageCode == 0)
            {
                safe_memset(tempLogBuf, LEGACY_DRIVE_SEC_SIZE, 0, LEGACY_DRIVE_SEC_SIZE);
                if (SUCCESS == scsi_Log_Sense_Cmd(device, false, LPC_CUMULATIVE_VALUES, pageCode, subpageCode, 0x0000,
                                                  tempLogBuf, LEGACY_DRIVE_SEC_SIZE))
                {
                    deviceStats->sasStatistics.writeErrorCountersSupported = true;
                    uint16_t pageLength      = M_BytesTo2ByteValue(tempLogBuf[2], tempLogBuf[3]);
                    uint8_t  parameterLength = UINT8_C(0);
                    // loop through the data and gather the data from each parameter we care about getting.
                    for (uint16_t iter = UINT16_C(4); iter < pageLength && iter < LEGACY_DRIVE_SEC_SIZE;
                         iter += (C_CAST(uint16_t, parameterLength + UINT16_C(4))))
                    {
                        uint16_t parameterCode = M_BytesTo2ByteValue(tempLogBuf[iter], tempLogBuf[iter + 1]);
                        parameterLength        = tempLogBuf[iter + 3];
                        switch (parameterCode)
                        {
                        case 0: // Errors corrected without substantial delay
                            deviceStats->sasStatistics.writeErrorsCorrectedWithoutSubstantialDelay.isSupported  = true;
                            deviceStats->sasStatistics.writeErrorsCorrectedWithoutSubstantialDelay.isValueValid = true;
                            // check if thresholds supported, etc
                            deviceStats->sasStatistics.writeErrorsCorrectedWithoutSubstantialDelay
                                .thresholdNotificationEnabled = tempLogBuf[iter + 2] & BIT4; // ETC bit
                            if (tempLogBuf[iter + 2] & BIT4)
                            {
                                switch ((tempLogBuf[iter + 2] & (BIT2 | BIT3)) >> 2)
                                {
                                case 3:
                                    deviceStats->sasStatistics.writeErrorsCorrectedWithoutSubstantialDelay.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_GREATER;
                                    break;
                                case 2:
                                    deviceStats->sasStatistics.writeErrorsCorrectedWithoutSubstantialDelay.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL;
                                    break;
                                case 1:
                                    deviceStats->sasStatistics.writeErrorsCorrectedWithoutSubstantialDelay.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL;
                                    break;
                                case 0:
                                default:
                                    deviceStats->sasStatistics.writeErrorsCorrectedWithoutSubstantialDelay.threshType =
                                        THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE;
                                    break;
                                }
                            }
                            switch (parameterLength)
                            {
                            case 1: // single byte
                                deviceStats->sasStatistics.writeErrorsCorrectedWithoutSubstantialDelay.statisticValue =
                                    tempLogBuf[iter + 4];
                                break;
                            case 2: // word
                                deviceStats->sasStatistics.writeErrorsCorrectedWithoutSubstantialDelay.statisticValue =
                                    M_BytesTo2ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5]);
                                break;
                            case 4: // double word
                                deviceStats->sasStatistics.writeErrorsCorrectedWithoutSubstantialDelay.statisticValue =
                                    M_BytesTo4ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5],
                                                        tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
                                break;
                            case 8: // quad word
                                deviceStats->sasStatistics.writeErrorsCorrectedWithoutSubstantialDelay.statisticValue =
                                    M_BytesTo8ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5],
                                                        tempLogBuf[iter + 6], tempLogBuf[iter + 7],
                                                        tempLogBuf[iter + 8], tempLogBuf[iter + 9],
                                                        tempLogBuf[iter + 10], tempLogBuf[iter + 11]);
                                break;
                            default: // don't bother trying to read the data since it's in a more complicated format to
                                     // read than we care to handle in this code right now
                                deviceStats->sasStatistics.writeErrorsCorrectedWithoutSubstantialDelay.isValueValid =
                                    false;
                                break;
                            }
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            break;
                        case 1: // Errors corrected with possible delays
                            deviceStats->sasStatistics.writeErrorsCorrectedWithPossibleDelays.isSupported  = true;
                            deviceStats->sasStatistics.writeErrorsCorrectedWithPossibleDelays.isValueValid = true;
                            // check if thresholds supported, etc
                            deviceStats->sasStatistics.writeErrorsCorrectedWithPossibleDelays
                                .thresholdNotificationEnabled = tempLogBuf[iter + 2] & BIT4; // ETC bit
                            if (tempLogBuf[iter + 2] & BIT4)
                            {
                                switch ((tempLogBuf[iter + 2] & (BIT2 | BIT3)) >> 2)
                                {
                                case 3:
                                    deviceStats->sasStatistics.writeErrorsCorrectedWithPossibleDelays.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_GREATER;
                                    break;
                                case 2:
                                    deviceStats->sasStatistics.writeErrorsCorrectedWithPossibleDelays.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL;
                                    break;
                                case 1:
                                    deviceStats->sasStatistics.writeErrorsCorrectedWithPossibleDelays.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL;
                                    break;
                                case 0:
                                default:
                                    deviceStats->sasStatistics.writeErrorsCorrectedWithPossibleDelays.threshType =
                                        THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE;
                                    break;
                                }
                            }
                            switch (parameterLength)
                            {
                            case 1: // single byte
                                deviceStats->sasStatistics.writeErrorsCorrectedWithPossibleDelays.statisticValue =
                                    tempLogBuf[iter + 4];
                                break;
                            case 2: // word
                                deviceStats->sasStatistics.writeErrorsCorrectedWithPossibleDelays.statisticValue =
                                    M_BytesTo2ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5]);
                                break;
                            case 4: // double word
                                deviceStats->sasStatistics.writeErrorsCorrectedWithPossibleDelays.statisticValue =
                                    M_BytesTo4ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5],
                                                        tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
                                break;
                            case 8: // quad word
                                deviceStats->sasStatistics.writeErrorsCorrectedWithPossibleDelays.statisticValue =
                                    M_BytesTo8ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5],
                                                        tempLogBuf[iter + 6], tempLogBuf[iter + 7],
                                                        tempLogBuf[iter + 8], tempLogBuf[iter + 9],
                                                        tempLogBuf[iter + 10], tempLogBuf[iter + 11]);
                                break;
                            default: // don't bother trying to read the data since it's in a more complicated format to
                                     // read than we care to handle in this code right now
                                deviceStats->sasStatistics.writeErrorsCorrectedWithPossibleDelays.isValueValid = false;
                                break;
                            }
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            break;
                        case 2: // total rewrites
                            deviceStats->sasStatistics.writeTotalReWrites.isSupported  = true;
                            deviceStats->sasStatistics.writeTotalReWrites.isValueValid = true;
                            // check if thresholds supported, etc
                            deviceStats->sasStatistics.writeTotalReWrites.thresholdNotificationEnabled =
                                tempLogBuf[iter + 2] & BIT4; // ETC bit
                            if (tempLogBuf[iter + 2] & BIT4)
                            {
                                switch ((tempLogBuf[iter + 2] & (BIT2 | BIT3)) >> 2)
                                {
                                case 3:
                                    deviceStats->sasStatistics.writeTotalReWrites.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_GREATER;
                                    break;
                                case 2:
                                    deviceStats->sasStatistics.writeTotalReWrites.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL;
                                    break;
                                case 1:
                                    deviceStats->sasStatistics.writeTotalReWrites.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL;
                                    break;
                                case 0:
                                default:
                                    deviceStats->sasStatistics.writeTotalReWrites.threshType =
                                        THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE;
                                    break;
                                }
                            }
                            switch (parameterLength)
                            {
                            case 1: // single byte
                                deviceStats->sasStatistics.writeTotalReWrites.statisticValue = tempLogBuf[iter + 4];
                                break;
                            case 2: // word
                                deviceStats->sasStatistics.writeTotalReWrites.statisticValue =
                                    M_BytesTo2ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5]);
                                break;
                            case 4: // double word
                                deviceStats->sasStatistics.writeTotalReWrites.statisticValue =
                                    M_BytesTo4ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5],
                                                        tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
                                break;
                            case 8: // quad word
                                deviceStats->sasStatistics.writeTotalReWrites.statisticValue = M_BytesTo8ByteValue(
                                    tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6],
                                    tempLogBuf[iter + 7], tempLogBuf[iter + 8], tempLogBuf[iter + 9],
                                    tempLogBuf[iter + 10], tempLogBuf[iter + 11]);
                                break;
                            default: // don't bother trying to read the data since it's in a more complicated format to
                                     // read than we care to handle in this code right now
                                deviceStats->sasStatistics.writeTotalReWrites.isValueValid = false;
                                break;
                            }
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            break;
                        case 3: // total errors corrected
                            deviceStats->sasStatistics.writeErrorsCorrected.isSupported  = true;
                            deviceStats->sasStatistics.writeErrorsCorrected.isValueValid = true;
                            // check if thresholds supported, etc
                            deviceStats->sasStatistics.writeErrorsCorrected.thresholdNotificationEnabled =
                                tempLogBuf[iter + 2] & BIT4; // ETC bit
                            if (tempLogBuf[iter + 2] & BIT4)
                            {
                                switch ((tempLogBuf[iter + 2] & (BIT2 | BIT3)) >> 2)
                                {
                                case 3:
                                    deviceStats->sasStatistics.writeErrorsCorrected.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_GREATER;
                                    break;
                                case 2:
                                    deviceStats->sasStatistics.writeErrorsCorrected.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL;
                                    break;
                                case 1:
                                    deviceStats->sasStatistics.writeErrorsCorrected.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL;
                                    break;
                                case 0:
                                default:
                                    deviceStats->sasStatistics.writeErrorsCorrected.threshType =
                                        THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE;
                                    break;
                                }
                            }
                            switch (parameterLength)
                            {
                            case 1: // single byte
                                deviceStats->sasStatistics.writeErrorsCorrected.statisticValue = tempLogBuf[iter + 4];
                                break;
                            case 2: // word
                                deviceStats->sasStatistics.writeErrorsCorrected.statisticValue =
                                    M_BytesTo2ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5]);
                                break;
                            case 4: // double word
                                deviceStats->sasStatistics.writeErrorsCorrected.statisticValue =
                                    M_BytesTo4ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5],
                                                        tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
                                break;
                            case 8: // quad word
                                deviceStats->sasStatistics.writeErrorsCorrected.statisticValue = M_BytesTo8ByteValue(
                                    tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6],
                                    tempLogBuf[iter + 7], tempLogBuf[iter + 8], tempLogBuf[iter + 9],
                                    tempLogBuf[iter + 10], tempLogBuf[iter + 11]);
                                break;
                            default: // don't bother trying to read the data since it's in a more complicated format to
                                     // read than we care to handle in this code right now
                                deviceStats->sasStatistics.writeErrorsCorrected.isValueValid = false;
                                break;
                            }
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            break;
                        case 4: // total times correction algorithm processed
                            deviceStats->sasStatistics.writeTotalTimeCorrectionAlgorithmProcessed.isSupported  = true;
                            deviceStats->sasStatistics.writeTotalTimeCorrectionAlgorithmProcessed.isValueValid = true;
                            // check if thresholds supported, etc
                            deviceStats->sasStatistics.writeTotalTimeCorrectionAlgorithmProcessed
                                .thresholdNotificationEnabled = tempLogBuf[iter + 2] & BIT4; // ETC bit
                            if (tempLogBuf[iter + 2] & BIT4)
                            {
                                switch ((tempLogBuf[iter + 2] & (BIT2 | BIT3)) >> 2)
                                {
                                case 3:
                                    deviceStats->sasStatistics.writeTotalTimeCorrectionAlgorithmProcessed.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_GREATER;
                                    break;
                                case 2:
                                    deviceStats->sasStatistics.writeTotalTimeCorrectionAlgorithmProcessed.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL;
                                    break;
                                case 1:
                                    deviceStats->sasStatistics.writeTotalTimeCorrectionAlgorithmProcessed.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL;
                                    break;
                                case 0:
                                default:
                                    deviceStats->sasStatistics.writeTotalTimeCorrectionAlgorithmProcessed.threshType =
                                        THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE;
                                    break;
                                }
                            }
                            switch (parameterLength)
                            {
                            case 1: // single byte
                                deviceStats->sasStatistics.writeTotalTimeCorrectionAlgorithmProcessed.statisticValue =
                                    tempLogBuf[iter + 4];
                                break;
                            case 2: // word
                                deviceStats->sasStatistics.writeTotalTimeCorrectionAlgorithmProcessed.statisticValue =
                                    M_BytesTo2ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5]);
                                break;
                            case 4: // double word
                                deviceStats->sasStatistics.writeTotalTimeCorrectionAlgorithmProcessed.statisticValue =
                                    M_BytesTo4ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5],
                                                        tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
                                break;
                            case 8: // quad word
                                deviceStats->sasStatistics.writeTotalTimeCorrectionAlgorithmProcessed.statisticValue =
                                    M_BytesTo8ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5],
                                                        tempLogBuf[iter + 6], tempLogBuf[iter + 7],
                                                        tempLogBuf[iter + 8], tempLogBuf[iter + 9],
                                                        tempLogBuf[iter + 10], tempLogBuf[iter + 11]);
                                break;
                            default: // don't bother trying to read the data since it's in a more complicated format to
                                     // read than we care to handle in this code right now
                                deviceStats->sasStatistics.writeTotalTimeCorrectionAlgorithmProcessed.isValueValid =
                                    false;
                                break;
                            }
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            break;
                        case 5: // total bytes processed
                            deviceStats->sasStatistics.writeTotalBytesProcessed.isSupported  = true;
                            deviceStats->sasStatistics.writeTotalBytesProcessed.isValueValid = true;
                            // check if thresholds supported, etc
                            deviceStats->sasStatistics.writeTotalBytesProcessed.thresholdNotificationEnabled =
                                tempLogBuf[iter + 2] & BIT4; // ETC bit
                            if (tempLogBuf[iter + 2] & BIT4)
                            {
                                switch ((tempLogBuf[iter + 2] & (BIT2 | BIT3)) >> 2)
                                {
                                case 3:
                                    deviceStats->sasStatistics.writeTotalBytesProcessed.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_GREATER;
                                    break;
                                case 2:
                                    deviceStats->sasStatistics.writeTotalBytesProcessed.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL;
                                    break;
                                case 1:
                                    deviceStats->sasStatistics.writeTotalBytesProcessed.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL;
                                    break;
                                case 0:
                                default:
                                    deviceStats->sasStatistics.writeTotalBytesProcessed.threshType =
                                        THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE;
                                    break;
                                }
                            }
                            switch (parameterLength)
                            {
                            case 1: // single byte
                                deviceStats->sasStatistics.writeTotalBytesProcessed.statisticValue =
                                    tempLogBuf[iter + 4];
                                break;
                            case 2: // word
                                deviceStats->sasStatistics.writeTotalBytesProcessed.statisticValue =
                                    M_BytesTo2ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5]);
                                break;
                            case 4: // double word
                                deviceStats->sasStatistics.writeTotalBytesProcessed.statisticValue =
                                    M_BytesTo4ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5],
                                                        tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
                                break;
                            case 8: // quad word
                                deviceStats->sasStatistics.writeTotalBytesProcessed.statisticValue =
                                    M_BytesTo8ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5],
                                                        tempLogBuf[iter + 6], tempLogBuf[iter + 7],
                                                        tempLogBuf[iter + 8], tempLogBuf[iter + 9],
                                                        tempLogBuf[iter + 10], tempLogBuf[iter + 11]);
                                break;
                            default: // don't bother trying to read the data since it's in a more complicated format to
                                     // read than we care to handle in this code right now
                                deviceStats->sasStatistics.writeTotalBytesProcessed.isValueValid = false;
                                break;
                            }
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            break;
                        case 6: // total uncorrected errors
                            deviceStats->sasStatistics.writeTotalUncorrectedErrors.isSupported  = true;
                            deviceStats->sasStatistics.writeTotalUncorrectedErrors.isValueValid = true;
                            // check if thresholds supported, etc
                            deviceStats->sasStatistics.writeTotalUncorrectedErrors.thresholdNotificationEnabled =
                                tempLogBuf[iter + 2] & BIT4; // ETC bit
                            if (tempLogBuf[iter + 2] & BIT4)
                            {
                                switch ((tempLogBuf[iter + 2] & (BIT2 | BIT3)) >> 2)
                                {
                                case 3:
                                    deviceStats->sasStatistics.writeTotalUncorrectedErrors.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_GREATER;
                                    break;
                                case 2:
                                    deviceStats->sasStatistics.writeTotalUncorrectedErrors.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL;
                                    break;
                                case 1:
                                    deviceStats->sasStatistics.writeTotalUncorrectedErrors.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL;
                                    break;
                                case 0:
                                default:
                                    deviceStats->sasStatistics.writeTotalUncorrectedErrors.threshType =
                                        THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE;
                                    break;
                                }
                            }
                            switch (parameterLength)
                            {
                            case 1: // single byte
                                deviceStats->sasStatistics.writeTotalUncorrectedErrors.statisticValue =
                                    tempLogBuf[iter + 4];
                                break;
                            case 2: // word
                                deviceStats->sasStatistics.writeTotalUncorrectedErrors.statisticValue =
                                    M_BytesTo2ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5]);
                                break;
                            case 4: // double word
                                deviceStats->sasStatistics.writeTotalUncorrectedErrors.statisticValue =
                                    M_BytesTo4ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5],
                                                        tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
                                break;
                            case 8: // quad word
                                deviceStats->sasStatistics.writeTotalUncorrectedErrors.statisticValue =
                                    M_BytesTo8ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5],
                                                        tempLogBuf[iter + 6], tempLogBuf[iter + 7],
                                                        tempLogBuf[iter + 8], tempLogBuf[iter + 9],
                                                        tempLogBuf[iter + 10], tempLogBuf[iter + 11]);
                                break;
                            default: // don't bother trying to read the data since it's in a more complicated format to
                                     // read than we care to handle in this code right now
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
                    // thresholds
                    safe_memset(tempLogBuf, LEGACY_DRIVE_SEC_SIZE, 0, LEGACY_DRIVE_SEC_SIZE);
                    if (SUCCESS == scsi_Log_Sense_Cmd(device, false, LPC_THRESHOLD_VALUES, pageCode, subpageCode,
                                                      0x0000, tempLogBuf, LEGACY_DRIVE_SEC_SIZE))
                    {
                        pageLength      = M_BytesTo2ByteValue(tempLogBuf[2], tempLogBuf[3]);
                        parameterLength = 0;
                        // loop through the data and gather the data from each parameter we care about getting.
                        for (uint16_t iter = UINT16_C(4); iter < pageLength && iter < LEGACY_DRIVE_SEC_SIZE;
                             iter += (C_CAST(uint16_t, parameterLength + UINT16_C(4))))
                        {
                            uint16_t parameterCode = M_BytesTo2ByteValue(tempLogBuf[iter], tempLogBuf[iter + 1]);
                            parameterLength        = tempLogBuf[iter + 3];
                            switch (parameterCode)
                            {
                            case 0: // Errors corrected without substantial delay
                                deviceStats->sasStatistics.writeErrorsCorrectedWithoutSubstantialDelay
                                    .supportsNotification = true;
                                if (tempLogBuf[iter + 2] & BIT4)
                                {
                                    deviceStats->sasStatistics.writeErrorsCorrectedWithoutSubstantialDelay
                                        .isThresholdValid = true;
                                    switch (parameterLength)
                                    {
                                    case 1: // single byte
                                        deviceStats->sasStatistics.writeErrorsCorrectedWithoutSubstantialDelay
                                            .threshold = tempLogBuf[iter + 4];
                                        break;
                                    case 2: // word
                                        deviceStats->sasStatistics.writeErrorsCorrectedWithoutSubstantialDelay
                                            .threshold =
                                            M_BytesTo2ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5]);
                                        break;
                                    case 4: // double word
                                        deviceStats->sasStatistics.writeErrorsCorrectedWithoutSubstantialDelay
                                            .threshold =
                                            M_BytesTo4ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5],
                                                                tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
                                        break;
                                    case 8: // quad word
                                        deviceStats->sasStatistics.writeErrorsCorrectedWithoutSubstantialDelay
                                            .threshold = M_BytesTo8ByteValue(
                                            tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6],
                                            tempLogBuf[iter + 7], tempLogBuf[iter + 8], tempLogBuf[iter + 9],
                                            tempLogBuf[iter + 10], tempLogBuf[iter + 11]);
                                        break;
                                    default: // don't bother trying to read the data since it's in a more complicated
                                             // format to read than we care to handle in this code right now
                                        deviceStats->sasStatistics.writeErrorsCorrectedWithoutSubstantialDelay
                                            .isThresholdValid = false;
                                        break;
                                    }
                                    scsi_Threshold_Comparison(
                                        &deviceStats->sasStatistics.writeErrorsCorrectedWithoutSubstantialDelay);
                                }
                                break;
                            case 1: // Errors corrected with possible delays
                                deviceStats->sasStatistics.writeErrorsCorrectedWithPossibleDelays.supportsNotification =
                                    true;
                                if (tempLogBuf[iter + 2] & BIT4)
                                {
                                    deviceStats->sasStatistics.writeErrorsCorrectedWithPossibleDelays.isThresholdValid =
                                        true;
                                    switch (parameterLength)
                                    {
                                    case 1: // single byte
                                        deviceStats->sasStatistics.writeErrorsCorrectedWithPossibleDelays.threshold =
                                            tempLogBuf[iter + 4];
                                        break;
                                    case 2: // word
                                        deviceStats->sasStatistics.writeErrorsCorrectedWithPossibleDelays.threshold =
                                            M_BytesTo2ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5]);
                                        break;
                                    case 4: // double word
                                        deviceStats->sasStatistics.writeErrorsCorrectedWithPossibleDelays.threshold =
                                            M_BytesTo4ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5],
                                                                tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
                                        break;
                                    case 8: // quad word
                                        deviceStats->sasStatistics.writeErrorsCorrectedWithPossibleDelays.threshold =
                                            M_BytesTo8ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5],
                                                                tempLogBuf[iter + 6], tempLogBuf[iter + 7],
                                                                tempLogBuf[iter + 8], tempLogBuf[iter + 9],
                                                                tempLogBuf[iter + 10], tempLogBuf[iter + 11]);
                                        break;
                                    default: // don't bother trying to read the data since it's in a more complicated
                                             // format to read than we care to handle in this code right now
                                        deviceStats->sasStatistics.writeErrorsCorrectedWithPossibleDelays
                                            .isThresholdValid = false;
                                        break;
                                    }
                                    scsi_Threshold_Comparison(
                                        &deviceStats->sasStatistics.writeErrorsCorrectedWithPossibleDelays);
                                }
                                break;
                            case 2: // total
                                deviceStats->sasStatistics.writeTotalReWrites.supportsNotification = true;
                                if (tempLogBuf[iter + 2] & BIT4)
                                {
                                    deviceStats->sasStatistics.writeTotalReWrites.isThresholdValid = true;
                                    switch (parameterLength)
                                    {
                                    case 1: // single byte
                                        deviceStats->sasStatistics.writeTotalReWrites.threshold = tempLogBuf[iter + 4];
                                        break;
                                    case 2: // word
                                        deviceStats->sasStatistics.writeTotalReWrites.threshold =
                                            M_BytesTo2ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5]);
                                        break;
                                    case 4: // double word
                                        deviceStats->sasStatistics.writeTotalReWrites.threshold =
                                            M_BytesTo4ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5],
                                                                tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
                                        break;
                                    case 8: // quad word
                                        deviceStats->sasStatistics.writeTotalReWrites.threshold = M_BytesTo8ByteValue(
                                            tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6],
                                            tempLogBuf[iter + 7], tempLogBuf[iter + 8], tempLogBuf[iter + 9],
                                            tempLogBuf[iter + 10], tempLogBuf[iter + 11]);
                                        break;
                                    default: // don't bother trying to read the data since it's in a more complicated
                                             // format to read than we care to handle in this code right now
                                        deviceStats->sasStatistics.writeTotalReWrites.isThresholdValid = false;
                                        break;
                                    }
                                    scsi_Threshold_Comparison(&deviceStats->sasStatistics.writeTotalReWrites);
                                }
                                break;
                            case 3: // total errors corrected
                                deviceStats->sasStatistics.writeErrorsCorrected.supportsNotification = true;
                                if (tempLogBuf[iter + 2] & BIT4)
                                {
                                    deviceStats->sasStatistics.writeErrorsCorrected.isThresholdValid = true;
                                    switch (parameterLength)
                                    {
                                    case 1: // single byte
                                        deviceStats->sasStatistics.writeErrorsCorrected.threshold =
                                            tempLogBuf[iter + 4];
                                        break;
                                    case 2: // word
                                        deviceStats->sasStatistics.writeErrorsCorrected.threshold =
                                            M_BytesTo2ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5]);
                                        break;
                                    case 4: // double word
                                        deviceStats->sasStatistics.writeErrorsCorrected.threshold =
                                            M_BytesTo4ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5],
                                                                tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
                                        break;
                                    case 8: // quad word
                                        deviceStats->sasStatistics.writeErrorsCorrected.threshold = M_BytesTo8ByteValue(
                                            tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6],
                                            tempLogBuf[iter + 7], tempLogBuf[iter + 8], tempLogBuf[iter + 9],
                                            tempLogBuf[iter + 10], tempLogBuf[iter + 11]);
                                        break;
                                    default: // don't bother trying to read the data since it's in a more complicated
                                             // format to read than we care to handle in this code right now
                                        deviceStats->sasStatistics.writeErrorsCorrected.isThresholdValid = false;
                                        break;
                                    }
                                    scsi_Threshold_Comparison(&deviceStats->sasStatistics.writeErrorsCorrected);
                                }
                                break;
                            case 4: // total times correction algorithm processed
                                deviceStats->sasStatistics.writeTotalTimeCorrectionAlgorithmProcessed
                                    .supportsNotification = true;
                                if (tempLogBuf[iter + 2] & BIT4)
                                {
                                    deviceStats->sasStatistics.writeTotalTimeCorrectionAlgorithmProcessed
                                        .isThresholdValid = true;
                                    switch (parameterLength)
                                    {
                                    case 1: // single byte
                                        deviceStats->sasStatistics.writeTotalTimeCorrectionAlgorithmProcessed
                                            .threshold = tempLogBuf[iter + 4];
                                        break;
                                    case 2: // word
                                        deviceStats->sasStatistics.writeTotalTimeCorrectionAlgorithmProcessed
                                            .threshold =
                                            M_BytesTo2ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5]);
                                        break;
                                    case 4: // double word
                                        deviceStats->sasStatistics.writeTotalTimeCorrectionAlgorithmProcessed
                                            .threshold =
                                            M_BytesTo4ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5],
                                                                tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
                                        break;
                                    case 8: // quad word
                                        deviceStats->sasStatistics.writeTotalTimeCorrectionAlgorithmProcessed
                                            .threshold = M_BytesTo8ByteValue(
                                            tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6],
                                            tempLogBuf[iter + 7], tempLogBuf[iter + 8], tempLogBuf[iter + 9],
                                            tempLogBuf[iter + 10], tempLogBuf[iter + 11]);
                                        break;
                                    default: // don't bother trying to read the data since it's in a more complicated
                                             // format to read than we care to handle in this code right now
                                        deviceStats->sasStatistics.writeTotalTimeCorrectionAlgorithmProcessed
                                            .isThresholdValid = false;
                                        break;
                                    }
                                    scsi_Threshold_Comparison(
                                        &deviceStats->sasStatistics.writeTotalTimeCorrectionAlgorithmProcessed);
                                }
                                break;
                            case 5: // total bytes processed
                                deviceStats->sasStatistics.writeTotalBytesProcessed.supportsNotification = true;
                                if (tempLogBuf[iter + 2] & BIT4)
                                {
                                    deviceStats->sasStatistics.writeTotalBytesProcessed.isThresholdValid = true;
                                    switch (parameterLength)
                                    {
                                    case 1: // single byte
                                        deviceStats->sasStatistics.writeTotalBytesProcessed.threshold =
                                            tempLogBuf[iter + 4];
                                        break;
                                    case 2: // word
                                        deviceStats->sasStatistics.writeTotalBytesProcessed.threshold =
                                            M_BytesTo2ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5]);
                                        break;
                                    case 4: // double word
                                        deviceStats->sasStatistics.writeTotalBytesProcessed.threshold =
                                            M_BytesTo4ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5],
                                                                tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
                                        break;
                                    case 8: // quad word
                                        deviceStats->sasStatistics.writeTotalBytesProcessed.threshold =
                                            M_BytesTo8ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5],
                                                                tempLogBuf[iter + 6], tempLogBuf[iter + 7],
                                                                tempLogBuf[iter + 8], tempLogBuf[iter + 9],
                                                                tempLogBuf[iter + 10], tempLogBuf[iter + 11]);
                                        break;
                                    default: // don't bother trying to read the data since it's in a more complicated
                                             // format to read than we care to handle in this code right now
                                        deviceStats->sasStatistics.writeTotalBytesProcessed.isThresholdValid = false;
                                        break;
                                    }
                                    scsi_Threshold_Comparison(&deviceStats->sasStatistics.writeTotalBytesProcessed);
                                }
                                break;
                            case 6: // total uncorrected errors
                                deviceStats->sasStatistics.writeTotalUncorrectedErrors.supportsNotification = true;
                                if (tempLogBuf[iter + 2] & BIT4)
                                {
                                    deviceStats->sasStatistics.writeTotalUncorrectedErrors.isThresholdValid = true;
                                    switch (parameterLength)
                                    {
                                    case 1: // single byte
                                        deviceStats->sasStatistics.writeTotalUncorrectedErrors.threshold =
                                            tempLogBuf[iter + 4];
                                        break;
                                    case 2: // word
                                        deviceStats->sasStatistics.writeTotalUncorrectedErrors.threshold =
                                            M_BytesTo2ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5]);
                                        break;
                                    case 4: // double word
                                        deviceStats->sasStatistics.writeTotalUncorrectedErrors.threshold =
                                            M_BytesTo4ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5],
                                                                tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
                                        break;
                                    case 8: // quad word
                                        deviceStats->sasStatistics.writeTotalUncorrectedErrors.threshold =
                                            M_BytesTo8ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5],
                                                                tempLogBuf[iter + 6], tempLogBuf[iter + 7],
                                                                tempLogBuf[iter + 8], tempLogBuf[iter + 9],
                                                                tempLogBuf[iter + 10], tempLogBuf[iter + 11]);
                                        break;
                                    default: // don't bother trying to read the data since it's in a more complicated
                                             // format to read than we care to handle in this code right now
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
                safe_memset(tempLogBuf, LEGACY_DRIVE_SEC_SIZE, 0, LEGACY_DRIVE_SEC_SIZE);
                if (SUCCESS == scsi_Log_Sense_Cmd(device, false, LPC_CUMULATIVE_VALUES, pageCode, subpageCode, 0x0000,
                                                  tempLogBuf, LEGACY_DRIVE_SEC_SIZE))
                {
                    deviceStats->sasStatistics.readErrorCountersSupported = true;
                    uint16_t pageLength      = M_BytesTo2ByteValue(tempLogBuf[2], tempLogBuf[3]);
                    uint8_t  parameterLength = UINT8_C(0);
                    // loop through the data and gather the data from each parameter we care about getting.
                    for (uint16_t iter = UINT16_C(4); iter < pageLength && iter < LEGACY_DRIVE_SEC_SIZE;
                         iter += (C_CAST(uint16_t, parameterLength + UINT16_C(4))))
                    {
                        uint16_t parameterCode = M_BytesTo2ByteValue(tempLogBuf[iter], tempLogBuf[iter + 1]);
                        parameterLength        = tempLogBuf[iter + 3];
                        switch (parameterCode)
                        {
                        case 0: // Errors corrected without substantial delay
                            deviceStats->sasStatistics.readErrorsCorrectedWithoutSubstantialDelay.isSupported  = true;
                            deviceStats->sasStatistics.readErrorsCorrectedWithoutSubstantialDelay.isValueValid = true;
                            // check if thresholds supported, etc
                            deviceStats->sasStatistics.readErrorsCorrectedWithoutSubstantialDelay
                                .thresholdNotificationEnabled = tempLogBuf[iter + 2] & BIT4; // ETC bit
                            if (tempLogBuf[iter + 2] & BIT4)
                            {
                                switch ((tempLogBuf[iter + 2] & (BIT2 | BIT3)) >> 2)
                                {
                                case 3:
                                    deviceStats->sasStatistics.readErrorsCorrectedWithoutSubstantialDelay.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_GREATER;
                                    break;
                                case 2:
                                    deviceStats->sasStatistics.readErrorsCorrectedWithoutSubstantialDelay.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL;
                                    break;
                                case 1:
                                    deviceStats->sasStatistics.readErrorsCorrectedWithoutSubstantialDelay.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL;
                                    break;
                                case 0:
                                default:
                                    deviceStats->sasStatistics.readErrorsCorrectedWithoutSubstantialDelay.threshType =
                                        THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE;
                                    break;
                                }
                            }
                            switch (parameterLength)
                            {
                            case 1: // single byte
                                deviceStats->sasStatistics.readErrorsCorrectedWithoutSubstantialDelay.statisticValue =
                                    tempLogBuf[iter + 4];
                                break;
                            case 2: // word
                                deviceStats->sasStatistics.readErrorsCorrectedWithoutSubstantialDelay.statisticValue =
                                    M_BytesTo2ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5]);
                                break;
                            case 4: // double word
                                deviceStats->sasStatistics.readErrorsCorrectedWithoutSubstantialDelay.statisticValue =
                                    M_BytesTo4ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5],
                                                        tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
                                break;
                            case 8: // quad word
                                deviceStats->sasStatistics.readErrorsCorrectedWithoutSubstantialDelay.statisticValue =
                                    M_BytesTo8ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5],
                                                        tempLogBuf[iter + 6], tempLogBuf[iter + 7],
                                                        tempLogBuf[iter + 8], tempLogBuf[iter + 9],
                                                        tempLogBuf[iter + 10], tempLogBuf[iter + 11]);
                                break;
                            default: // don't bother trying to read the data since it's in a more complicated format to
                                     // read than we care to handle in this code right now
                                deviceStats->sasStatistics.readErrorsCorrectedWithoutSubstantialDelay.isValueValid =
                                    false;
                                break;
                            }
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            break;
                        case 1: // Errors corrected with possible delays
                            deviceStats->sasStatistics.readErrorsCorrectedWithPossibleDelays.isSupported  = true;
                            deviceStats->sasStatistics.readErrorsCorrectedWithPossibleDelays.isValueValid = true;
                            // check if thresholds supported, etc
                            deviceStats->sasStatistics.readErrorsCorrectedWithPossibleDelays
                                .thresholdNotificationEnabled = tempLogBuf[iter + 2] & BIT4; // ETC bit
                            if (tempLogBuf[iter + 2] & BIT4)
                            {
                                switch ((tempLogBuf[iter + 2] & (BIT2 | BIT3)) >> 2)
                                {
                                case 3:
                                    deviceStats->sasStatistics.readErrorsCorrectedWithPossibleDelays.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_GREATER;
                                    break;
                                case 2:
                                    deviceStats->sasStatistics.readErrorsCorrectedWithPossibleDelays.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL;
                                    break;
                                case 1:
                                    deviceStats->sasStatistics.readErrorsCorrectedWithPossibleDelays.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL;
                                    break;
                                case 0:
                                default:
                                    deviceStats->sasStatistics.readErrorsCorrectedWithPossibleDelays.threshType =
                                        THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE;
                                    break;
                                }
                            }
                            switch (parameterLength)
                            {
                            case 1: // single byte
                                deviceStats->sasStatistics.readErrorsCorrectedWithPossibleDelays.statisticValue =
                                    tempLogBuf[iter + 4];
                                break;
                            case 2: // word
                                deviceStats->sasStatistics.readErrorsCorrectedWithPossibleDelays.statisticValue =
                                    M_BytesTo2ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5]);
                                break;
                            case 4: // double word
                                deviceStats->sasStatistics.readErrorsCorrectedWithPossibleDelays.statisticValue =
                                    M_BytesTo4ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5],
                                                        tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
                                break;
                            case 8: // quad word
                                deviceStats->sasStatistics.readErrorsCorrectedWithPossibleDelays.statisticValue =
                                    M_BytesTo8ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5],
                                                        tempLogBuf[iter + 6], tempLogBuf[iter + 7],
                                                        tempLogBuf[iter + 8], tempLogBuf[iter + 9],
                                                        tempLogBuf[iter + 10], tempLogBuf[iter + 11]);
                                break;
                            default: // don't bother trying to read the data since it's in a more complicated format to
                                     // read than we care to handle in this code right now
                                deviceStats->sasStatistics.readErrorsCorrectedWithPossibleDelays.isValueValid = false;
                                break;
                            }
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            break;
                        case 2: // total
                            deviceStats->sasStatistics.readTotalRereads.isSupported  = true;
                            deviceStats->sasStatistics.readTotalRereads.isValueValid = true;
                            // check if thresholds supported, etc
                            deviceStats->sasStatistics.readTotalRereads.thresholdNotificationEnabled =
                                tempLogBuf[iter + 2] & BIT4; // ETC bit
                            if (tempLogBuf[iter + 2] & BIT4)
                            {
                                switch ((tempLogBuf[iter + 2] & (BIT2 | BIT3)) >> 2)
                                {
                                case 3:
                                    deviceStats->sasStatistics.readTotalRereads.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_GREATER;
                                    break;
                                case 2:
                                    deviceStats->sasStatistics.readTotalRereads.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL;
                                    break;
                                case 1:
                                    deviceStats->sasStatistics.readTotalRereads.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL;
                                    break;
                                case 0:
                                default:
                                    deviceStats->sasStatistics.readTotalRereads.threshType =
                                        THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE;
                                    break;
                                }
                            }
                            switch (parameterLength)
                            {
                            case 1: // single byte
                                deviceStats->sasStatistics.readTotalRereads.statisticValue = tempLogBuf[iter + 4];
                                break;
                            case 2: // word
                                deviceStats->sasStatistics.readTotalRereads.statisticValue =
                                    M_BytesTo2ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5]);
                                break;
                            case 4: // double word
                                deviceStats->sasStatistics.readTotalRereads.statisticValue =
                                    M_BytesTo4ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5],
                                                        tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
                                break;
                            case 8: // quad word
                                deviceStats->sasStatistics.readTotalRereads.statisticValue = M_BytesTo8ByteValue(
                                    tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6],
                                    tempLogBuf[iter + 7], tempLogBuf[iter + 8], tempLogBuf[iter + 9],
                                    tempLogBuf[iter + 10], tempLogBuf[iter + 11]);
                                break;
                            default: // don't bother trying to read the data since it's in a more complicated format to
                                     // read than we care to handle in this code right now
                                deviceStats->sasStatistics.readTotalRereads.isValueValid = false;
                                break;
                            }
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            break;
                        case 3: // total errors corrected
                            deviceStats->sasStatistics.readErrorsCorrected.isSupported  = true;
                            deviceStats->sasStatistics.readErrorsCorrected.isValueValid = true;
                            // check if thresholds supported, etc
                            deviceStats->sasStatistics.readErrorsCorrected.thresholdNotificationEnabled =
                                tempLogBuf[iter + 2] & BIT4; // ETC bit
                            if (tempLogBuf[iter + 2] & BIT4)
                            {
                                switch ((tempLogBuf[iter + 2] & (BIT2 | BIT3)) >> 2)
                                {
                                case 3:
                                    deviceStats->sasStatistics.readErrorsCorrected.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_GREATER;
                                    break;
                                case 2:
                                    deviceStats->sasStatistics.readErrorsCorrected.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL;
                                    break;
                                case 1:
                                    deviceStats->sasStatistics.readErrorsCorrected.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL;
                                    break;
                                case 0:
                                default:
                                    deviceStats->sasStatistics.readErrorsCorrected.threshType =
                                        THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE;
                                    break;
                                }
                            }
                            switch (parameterLength)
                            {
                            case 1: // single byte
                                deviceStats->sasStatistics.readErrorsCorrected.statisticValue = tempLogBuf[iter + 4];
                                break;
                            case 2: // word
                                deviceStats->sasStatistics.readErrorsCorrected.statisticValue =
                                    M_BytesTo2ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5]);
                                break;
                            case 4: // double word
                                deviceStats->sasStatistics.readErrorsCorrected.statisticValue =
                                    M_BytesTo4ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5],
                                                        tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
                                break;
                            case 8: // quad word
                                deviceStats->sasStatistics.readErrorsCorrected.statisticValue = M_BytesTo8ByteValue(
                                    tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6],
                                    tempLogBuf[iter + 7], tempLogBuf[iter + 8], tempLogBuf[iter + 9],
                                    tempLogBuf[iter + 10], tempLogBuf[iter + 11]);
                                break;
                            default: // don't bother trying to read the data since it's in a more complicated format to
                                     // read than we care to handle in this code right now
                                deviceStats->sasStatistics.readErrorsCorrected.isValueValid = false;
                                break;
                            }
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            break;
                        case 4: // total times correction algorithm processed
                            deviceStats->sasStatistics.readTotalTimeCorrectionAlgorithmProcessed.isSupported  = true;
                            deviceStats->sasStatistics.readTotalTimeCorrectionAlgorithmProcessed.isValueValid = true;
                            // check if thresholds supported, etc
                            deviceStats->sasStatistics.readTotalTimeCorrectionAlgorithmProcessed
                                .thresholdNotificationEnabled = tempLogBuf[iter + 2] & BIT4; // ETC bit
                            if (tempLogBuf[iter + 2] & BIT4)
                            {
                                switch ((tempLogBuf[iter + 2] & (BIT2 | BIT3)) >> 2)
                                {
                                case 3:
                                    deviceStats->sasStatistics.readTotalTimeCorrectionAlgorithmProcessed.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_GREATER;
                                    break;
                                case 2:
                                    deviceStats->sasStatistics.readTotalTimeCorrectionAlgorithmProcessed.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL;
                                    break;
                                case 1:
                                    deviceStats->sasStatistics.readTotalTimeCorrectionAlgorithmProcessed.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL;
                                    break;
                                case 0:
                                default:
                                    deviceStats->sasStatistics.readTotalTimeCorrectionAlgorithmProcessed.threshType =
                                        THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE;
                                    break;
                                }
                            }
                            switch (parameterLength)
                            {
                            case 1: // single byte
                                deviceStats->sasStatistics.readTotalTimeCorrectionAlgorithmProcessed.statisticValue =
                                    tempLogBuf[iter + 4];
                                break;
                            case 2: // word
                                deviceStats->sasStatistics.readTotalTimeCorrectionAlgorithmProcessed.statisticValue =
                                    M_BytesTo2ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5]);
                                break;
                            case 4: // double word
                                deviceStats->sasStatistics.readTotalTimeCorrectionAlgorithmProcessed.statisticValue =
                                    M_BytesTo4ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5],
                                                        tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
                                break;
                            case 8: // quad word
                                deviceStats->sasStatistics.readTotalTimeCorrectionAlgorithmProcessed.statisticValue =
                                    M_BytesTo8ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5],
                                                        tempLogBuf[iter + 6], tempLogBuf[iter + 7],
                                                        tempLogBuf[iter + 8], tempLogBuf[iter + 9],
                                                        tempLogBuf[iter + 10], tempLogBuf[iter + 11]);
                                break;
                            default: // don't bother trying to read the data since it's in a more complicated format to
                                     // read than we care to handle in this code right now
                                deviceStats->sasStatistics.readTotalTimeCorrectionAlgorithmProcessed.isValueValid =
                                    false;
                                break;
                            }
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            break;
                        case 5: // total bytes processed
                            deviceStats->sasStatistics.readTotalBytesProcessed.isSupported  = true;
                            deviceStats->sasStatistics.readTotalBytesProcessed.isValueValid = true;
                            // check if thresholds supported, etc
                            deviceStats->sasStatistics.readTotalBytesProcessed.thresholdNotificationEnabled =
                                tempLogBuf[iter + 2] & BIT4; // ETC bit
                            if (tempLogBuf[iter + 2] & BIT4)
                            {
                                switch ((tempLogBuf[iter + 2] & (BIT2 | BIT3)) >> 2)
                                {
                                case 3:
                                    deviceStats->sasStatistics.readTotalBytesProcessed.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_GREATER;
                                    break;
                                case 2:
                                    deviceStats->sasStatistics.readTotalBytesProcessed.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL;
                                    break;
                                case 1:
                                    deviceStats->sasStatistics.readTotalBytesProcessed.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL;
                                    break;
                                case 0:
                                default:
                                    deviceStats->sasStatistics.readTotalBytesProcessed.threshType =
                                        THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE;
                                    break;
                                }
                            }
                            switch (parameterLength)
                            {
                            case 1: // single byte
                                deviceStats->sasStatistics.readTotalBytesProcessed.statisticValue =
                                    tempLogBuf[iter + 4];
                                break;
                            case 2: // word
                                deviceStats->sasStatistics.readTotalBytesProcessed.statisticValue =
                                    M_BytesTo2ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5]);
                                break;
                            case 4: // double word
                                deviceStats->sasStatistics.readTotalBytesProcessed.statisticValue =
                                    M_BytesTo4ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5],
                                                        tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
                                break;
                            case 8: // quad word
                                deviceStats->sasStatistics.readTotalBytesProcessed.statisticValue = M_BytesTo8ByteValue(
                                    tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6],
                                    tempLogBuf[iter + 7], tempLogBuf[iter + 8], tempLogBuf[iter + 9],
                                    tempLogBuf[iter + 10], tempLogBuf[iter + 11]);
                                break;
                            default: // don't bother trying to read the data since it's in a more complicated format to
                                     // read than we care to handle in this code right now
                                deviceStats->sasStatistics.readTotalBytesProcessed.isValueValid = false;
                                break;
                            }
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            break;
                        case 6: // total uncorrected errors
                            deviceStats->sasStatistics.readTotalUncorrectedErrors.isSupported  = true;
                            deviceStats->sasStatistics.readTotalUncorrectedErrors.isValueValid = true;
                            // check if thresholds supported, etc
                            deviceStats->sasStatistics.readTotalUncorrectedErrors.thresholdNotificationEnabled =
                                tempLogBuf[iter + 2] & BIT4; // ETC bit
                            if (tempLogBuf[iter + 2] & BIT4)
                            {
                                switch ((tempLogBuf[iter + 2] & (BIT2 | BIT3)) >> 2)
                                {
                                case 3:
                                    deviceStats->sasStatistics.readTotalUncorrectedErrors.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_GREATER;
                                    break;
                                case 2:
                                    deviceStats->sasStatistics.readTotalUncorrectedErrors.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL;
                                    break;
                                case 1:
                                    deviceStats->sasStatistics.readTotalUncorrectedErrors.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL;
                                    break;
                                case 0:
                                default:
                                    deviceStats->sasStatistics.readTotalUncorrectedErrors.threshType =
                                        THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE;
                                    break;
                                }
                            }
                            switch (parameterLength)
                            {
                            case 1: // single byte
                                deviceStats->sasStatistics.readTotalUncorrectedErrors.statisticValue =
                                    tempLogBuf[iter + 4];
                                break;
                            case 2: // word
                                deviceStats->sasStatistics.readTotalUncorrectedErrors.statisticValue =
                                    M_BytesTo2ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5]);
                                break;
                            case 4: // double word
                                deviceStats->sasStatistics.readTotalUncorrectedErrors.statisticValue =
                                    M_BytesTo4ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5],
                                                        tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
                                break;
                            case 8: // quad word
                                deviceStats->sasStatistics.readTotalUncorrectedErrors.statisticValue =
                                    M_BytesTo8ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5],
                                                        tempLogBuf[iter + 6], tempLogBuf[iter + 7],
                                                        tempLogBuf[iter + 8], tempLogBuf[iter + 9],
                                                        tempLogBuf[iter + 10], tempLogBuf[iter + 11]);
                                break;
                            default: // don't bother trying to read the data since it's in a more complicated format to
                                     // read than we care to handle in this code right now
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
                    // thresholds
                    safe_memset(tempLogBuf, LEGACY_DRIVE_SEC_SIZE, 0, LEGACY_DRIVE_SEC_SIZE);
                    if (SUCCESS == scsi_Log_Sense_Cmd(device, false, LPC_THRESHOLD_VALUES, pageCode, subpageCode,
                                                      0x0000, tempLogBuf, LEGACY_DRIVE_SEC_SIZE))
                    {
                        pageLength      = M_BytesTo2ByteValue(tempLogBuf[2], tempLogBuf[3]);
                        parameterLength = 0;
                        // loop through the data and gather the data from each parameter we care about getting.
                        for (uint16_t iter = UINT16_C(4); iter < pageLength && iter < LEGACY_DRIVE_SEC_SIZE;
                             iter += (C_CAST(uint16_t, parameterLength + UINT16_C(4))))
                        {
                            uint16_t parameterCode = M_BytesTo2ByteValue(tempLogBuf[iter], tempLogBuf[iter + 1]);
                            parameterLength        = tempLogBuf[iter + 3];
                            switch (parameterCode)
                            {
                            case 0: // Errors corrected without substantial delay
                                deviceStats->sasStatistics.readErrorsCorrectedWithoutSubstantialDelay
                                    .supportsNotification = true;
                                if (tempLogBuf[iter + 2] & BIT4)
                                {
                                    deviceStats->sasStatistics.readErrorsCorrectedWithoutSubstantialDelay
                                        .isThresholdValid = true;
                                    switch (parameterLength)
                                    {
                                    case 1: // single byte
                                        deviceStats->sasStatistics.readErrorsCorrectedWithoutSubstantialDelay
                                            .threshold = tempLogBuf[iter + 4];
                                        break;
                                    case 2: // word
                                        deviceStats->sasStatistics.readErrorsCorrectedWithoutSubstantialDelay
                                            .threshold =
                                            M_BytesTo2ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5]);
                                        break;
                                    case 4: // double word
                                        deviceStats->sasStatistics.readErrorsCorrectedWithoutSubstantialDelay
                                            .threshold =
                                            M_BytesTo4ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5],
                                                                tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
                                        break;
                                    case 8: // quad word
                                        deviceStats->sasStatistics.readErrorsCorrectedWithoutSubstantialDelay
                                            .threshold = M_BytesTo8ByteValue(
                                            tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6],
                                            tempLogBuf[iter + 7], tempLogBuf[iter + 8], tempLogBuf[iter + 9],
                                            tempLogBuf[iter + 10], tempLogBuf[iter + 11]);
                                        break;
                                    default: // don't bother trying to read the data since it's in a more complicated
                                             // format to read than we care to handle in this code right now
                                        deviceStats->sasStatistics.readErrorsCorrectedWithoutSubstantialDelay
                                            .isThresholdValid = false;
                                        break;
                                    }
                                    scsi_Threshold_Comparison(
                                        &deviceStats->sasStatistics.readErrorsCorrectedWithoutSubstantialDelay);
                                }
                                break;
                            case 1: // Errors corrected with possible delays
                                deviceStats->sasStatistics.readErrorsCorrectedWithPossibleDelays.supportsNotification =
                                    true;
                                if (tempLogBuf[iter + 2] & BIT4)
                                {
                                    deviceStats->sasStatistics.readErrorsCorrectedWithPossibleDelays.isThresholdValid =
                                        true;
                                    switch (parameterLength)
                                    {
                                    case 1: // single byte
                                        deviceStats->sasStatistics.readErrorsCorrectedWithPossibleDelays.threshold =
                                            tempLogBuf[iter + 4];
                                        break;
                                    case 2: // word
                                        deviceStats->sasStatistics.readErrorsCorrectedWithPossibleDelays.threshold =
                                            M_BytesTo2ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5]);
                                        break;
                                    case 4: // double word
                                        deviceStats->sasStatistics.readErrorsCorrectedWithPossibleDelays.threshold =
                                            M_BytesTo4ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5],
                                                                tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
                                        break;
                                    case 8: // quad word
                                        deviceStats->sasStatistics.readErrorsCorrectedWithPossibleDelays.threshold =
                                            M_BytesTo8ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5],
                                                                tempLogBuf[iter + 6], tempLogBuf[iter + 7],
                                                                tempLogBuf[iter + 8], tempLogBuf[iter + 9],
                                                                tempLogBuf[iter + 10], tempLogBuf[iter + 11]);
                                        break;
                                    default: // don't bother trying to read the data since it's in a more complicated
                                             // format to read than we care to handle in this code right now
                                        deviceStats->sasStatistics.readErrorsCorrectedWithPossibleDelays
                                            .isThresholdValid = false;
                                        break;
                                    }
                                    scsi_Threshold_Comparison(
                                        &deviceStats->sasStatistics.readErrorsCorrectedWithPossibleDelays);
                                }
                                break;
                            case 2: // total
                                deviceStats->sasStatistics.readTotalRereads.supportsNotification = true;
                                if (tempLogBuf[iter + 2] & BIT4)
                                {
                                    deviceStats->sasStatistics.readTotalRereads.isThresholdValid = true;
                                    switch (parameterLength)
                                    {
                                    case 1: // single byte
                                        deviceStats->sasStatistics.readTotalRereads.threshold = tempLogBuf[iter + 4];
                                        break;
                                    case 2: // word
                                        deviceStats->sasStatistics.readTotalRereads.threshold =
                                            M_BytesTo2ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5]);
                                        break;
                                    case 4: // double word
                                        deviceStats->sasStatistics.readTotalRereads.threshold =
                                            M_BytesTo4ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5],
                                                                tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
                                        break;
                                    case 8: // quad word
                                        deviceStats->sasStatistics.readTotalRereads.threshold = M_BytesTo8ByteValue(
                                            tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6],
                                            tempLogBuf[iter + 7], tempLogBuf[iter + 8], tempLogBuf[iter + 9],
                                            tempLogBuf[iter + 10], tempLogBuf[iter + 11]);
                                        break;
                                    default: // don't bother trying to read the data since it's in a more complicated
                                             // format to read than we care to handle in this code right now
                                        deviceStats->sasStatistics.readTotalRereads.isThresholdValid = false;
                                        break;
                                    }
                                    scsi_Threshold_Comparison(&deviceStats->sasStatistics.readTotalRereads);
                                }
                                break;
                            case 3: // total errors corrected
                                deviceStats->sasStatistics.readErrorsCorrected.supportsNotification = true;
                                if (tempLogBuf[iter + 2] & BIT4)
                                {
                                    deviceStats->sasStatistics.readErrorsCorrected.isThresholdValid = true;
                                    switch (parameterLength)
                                    {
                                    case 1: // single byte
                                        deviceStats->sasStatistics.readErrorsCorrected.threshold = tempLogBuf[iter + 4];
                                        break;
                                    case 2: // word
                                        deviceStats->sasStatistics.readErrorsCorrected.threshold =
                                            M_BytesTo2ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5]);
                                        break;
                                    case 4: // double word
                                        deviceStats->sasStatistics.readErrorsCorrected.threshold =
                                            M_BytesTo4ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5],
                                                                tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
                                        break;
                                    case 8: // quad word
                                        deviceStats->sasStatistics.readErrorsCorrected.threshold = M_BytesTo8ByteValue(
                                            tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6],
                                            tempLogBuf[iter + 7], tempLogBuf[iter + 8], tempLogBuf[iter + 9],
                                            tempLogBuf[iter + 10], tempLogBuf[iter + 11]);
                                        break;
                                    default: // don't bother trying to read the data since it's in a more complicated
                                             // format to read than we care to handle in this code right now
                                        deviceStats->sasStatistics.readErrorsCorrected.isThresholdValid = false;
                                        break;
                                    }
                                    scsi_Threshold_Comparison(&deviceStats->sasStatistics.readErrorsCorrected);
                                }
                                break;
                            case 4: // total times correction algorithm processed
                                deviceStats->sasStatistics.readTotalTimeCorrectionAlgorithmProcessed
                                    .supportsNotification = true;
                                if (tempLogBuf[iter + 2] & BIT4)
                                {
                                    deviceStats->sasStatistics.readTotalTimeCorrectionAlgorithmProcessed
                                        .isThresholdValid = true;
                                    switch (parameterLength)
                                    {
                                    case 1: // single byte
                                        deviceStats->sasStatistics.readTotalTimeCorrectionAlgorithmProcessed.threshold =
                                            tempLogBuf[iter + 4];
                                        break;
                                    case 2: // word
                                        deviceStats->sasStatistics.readTotalTimeCorrectionAlgorithmProcessed.threshold =
                                            M_BytesTo2ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5]);
                                        break;
                                    case 4: // double word
                                        deviceStats->sasStatistics.readTotalTimeCorrectionAlgorithmProcessed.threshold =
                                            M_BytesTo4ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5],
                                                                tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
                                        break;
                                    case 8: // quad word
                                        deviceStats->sasStatistics.readTotalTimeCorrectionAlgorithmProcessed.threshold =
                                            M_BytesTo8ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5],
                                                                tempLogBuf[iter + 6], tempLogBuf[iter + 7],
                                                                tempLogBuf[iter + 8], tempLogBuf[iter + 9],
                                                                tempLogBuf[iter + 10], tempLogBuf[iter + 11]);
                                        break;
                                    default: // don't bother trying to read the data since it's in a more complicated
                                             // format to read than we care to handle in this code right now
                                        deviceStats->sasStatistics.readTotalTimeCorrectionAlgorithmProcessed
                                            .isThresholdValid = false;
                                        break;
                                    }
                                    scsi_Threshold_Comparison(
                                        &deviceStats->sasStatistics.readTotalTimeCorrectionAlgorithmProcessed);
                                }
                                break;
                            case 5: // total bytes processed
                                deviceStats->sasStatistics.readTotalBytesProcessed.supportsNotification = true;
                                if (tempLogBuf[iter + 2] & BIT4)
                                {
                                    deviceStats->sasStatistics.readTotalBytesProcessed.isThresholdValid = true;
                                    switch (parameterLength)
                                    {
                                    case 1: // single byte
                                        deviceStats->sasStatistics.readTotalBytesProcessed.threshold =
                                            tempLogBuf[iter + 4];
                                        break;
                                    case 2: // word
                                        deviceStats->sasStatistics.readTotalBytesProcessed.threshold =
                                            M_BytesTo2ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5]);
                                        break;
                                    case 4: // double word
                                        deviceStats->sasStatistics.readTotalBytesProcessed.threshold =
                                            M_BytesTo4ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5],
                                                                tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
                                        break;
                                    case 8: // quad word
                                        deviceStats->sasStatistics.readTotalBytesProcessed.threshold =
                                            M_BytesTo8ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5],
                                                                tempLogBuf[iter + 6], tempLogBuf[iter + 7],
                                                                tempLogBuf[iter + 8], tempLogBuf[iter + 9],
                                                                tempLogBuf[iter + 10], tempLogBuf[iter + 11]);
                                        break;
                                    default: // don't bother trying to read the data since it's in a more complicated
                                             // format to read than we care to handle in this code right now
                                        deviceStats->sasStatistics.readTotalBytesProcessed.isThresholdValid = false;
                                        break;
                                    }
                                    scsi_Threshold_Comparison(&deviceStats->sasStatistics.readTotalBytesProcessed);
                                }
                                break;
                            case 6: // total uncorrected errors
                                deviceStats->sasStatistics.readTotalUncorrectedErrors.supportsNotification = true;
                                if (tempLogBuf[iter + 2] & BIT4)
                                {
                                    deviceStats->sasStatistics.readTotalUncorrectedErrors.isThresholdValid = true;
                                    switch (parameterLength)
                                    {
                                    case 1: // single byte
                                        deviceStats->sasStatistics.readTotalUncorrectedErrors.threshold =
                                            tempLogBuf[iter + 4];
                                        break;
                                    case 2: // word
                                        deviceStats->sasStatistics.readTotalUncorrectedErrors.threshold =
                                            M_BytesTo2ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5]);
                                        break;
                                    case 4: // double word
                                        deviceStats->sasStatistics.readTotalUncorrectedErrors.threshold =
                                            M_BytesTo4ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5],
                                                                tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
                                        break;
                                    case 8: // quad word
                                        deviceStats->sasStatistics.readTotalUncorrectedErrors.threshold =
                                            M_BytesTo8ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5],
                                                                tempLogBuf[iter + 6], tempLogBuf[iter + 7],
                                                                tempLogBuf[iter + 8], tempLogBuf[iter + 9],
                                                                tempLogBuf[iter + 10], tempLogBuf[iter + 11]);
                                        break;
                                    default: // don't bother trying to read the data since it's in a more complicated
                                             // format to read than we care to handle in this code right now
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
                safe_memset(tempLogBuf, LEGACY_DRIVE_SEC_SIZE, 0, LEGACY_DRIVE_SEC_SIZE);
                if (SUCCESS == scsi_Log_Sense_Cmd(device, false, LPC_CUMULATIVE_VALUES, pageCode, subpageCode, 0x0000,
                                                  tempLogBuf, LEGACY_DRIVE_SEC_SIZE))
                {
                    deviceStats->sasStatistics.readReverseErrorCountersSupported = true;
                    uint16_t pageLength      = M_BytesTo2ByteValue(tempLogBuf[2], tempLogBuf[3]);
                    uint8_t  parameterLength = UINT8_C(0);
                    // loop through the data and gather the data from each parameter we care about getting.
                    for (uint16_t iter = UINT16_C(4); iter < pageLength && iter < LEGACY_DRIVE_SEC_SIZE;
                         iter += (C_CAST(uint16_t, parameterLength + UINT16_C(4))))
                    {
                        uint16_t parameterCode = M_BytesTo2ByteValue(tempLogBuf[iter], tempLogBuf[iter + 1]);
                        parameterLength        = tempLogBuf[iter + 3];
                        switch (parameterCode)
                        {
                        case 0: // Errors corrected without substantial delay
                            deviceStats->sasStatistics.readReverseErrorsCorrectedWithoutSubstantialDelay.isSupported =
                                true;
                            deviceStats->sasStatistics.readReverseErrorsCorrectedWithoutSubstantialDelay.isValueValid =
                                true;
                            // check if thresholds supported, etc
                            deviceStats->sasStatistics.readReverseErrorsCorrectedWithoutSubstantialDelay
                                .thresholdNotificationEnabled = tempLogBuf[iter + 2] & BIT4; // ETC bit
                            if (tempLogBuf[iter + 2] & BIT4)
                            {
                                switch ((tempLogBuf[iter + 2] & (BIT2 | BIT3)) >> 2)
                                {
                                case 3:
                                    deviceStats->sasStatistics.readReverseErrorsCorrectedWithoutSubstantialDelay
                                        .threshType = THRESHOLD_TYPE_TRIGGER_WHEN_GREATER;
                                    break;
                                case 2:
                                    deviceStats->sasStatistics.readReverseErrorsCorrectedWithoutSubstantialDelay
                                        .threshType = THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL;
                                    break;
                                case 1:
                                    deviceStats->sasStatistics.readReverseErrorsCorrectedWithoutSubstantialDelay
                                        .threshType = THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL;
                                    break;
                                case 0:
                                default:
                                    deviceStats->sasStatistics.readReverseErrorsCorrectedWithoutSubstantialDelay
                                        .threshType = THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE;
                                    break;
                                }
                            }
                            switch (parameterLength)
                            {
                            case 1: // single byte
                                deviceStats->sasStatistics.readReverseErrorsCorrectedWithoutSubstantialDelay
                                    .statisticValue = tempLogBuf[iter + 4];
                                break;
                            case 2: // word
                                deviceStats->sasStatistics.readReverseErrorsCorrectedWithoutSubstantialDelay
                                    .statisticValue = M_BytesTo2ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5]);
                                break;
                            case 4: // double word
                                deviceStats->sasStatistics.readReverseErrorsCorrectedWithoutSubstantialDelay
                                    .statisticValue = M_BytesTo4ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5],
                                                                          tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
                                break;
                            case 8: // quad word
                                deviceStats->sasStatistics.readReverseErrorsCorrectedWithoutSubstantialDelay
                                    .statisticValue = M_BytesTo8ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5],
                                                                          tempLogBuf[iter + 6], tempLogBuf[iter + 7],
                                                                          tempLogBuf[iter + 8], tempLogBuf[iter + 9],
                                                                          tempLogBuf[iter + 10], tempLogBuf[iter + 11]);
                                break;
                            default: // don't bother trying to read the data since it's in a more complicated format to
                                     // read than we care to handle in this code right now
                                deviceStats->sasStatistics.readReverseErrorsCorrectedWithoutSubstantialDelay
                                    .isValueValid = false;
                                break;
                            }
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            break;
                        case 1: // Errors corrected with possible delays
                            deviceStats->sasStatistics.readReverseErrorsCorrectedWithPossibleDelays.isSupported  = true;
                            deviceStats->sasStatistics.readReverseErrorsCorrectedWithPossibleDelays.isValueValid = true;
                            // check if thresholds supported, etc
                            deviceStats->sasStatistics.readReverseErrorsCorrectedWithPossibleDelays
                                .thresholdNotificationEnabled = tempLogBuf[iter + 2] & BIT4; // ETC bit
                            if (tempLogBuf[iter + 2] & BIT4)
                            {
                                switch ((tempLogBuf[iter + 2] & (BIT2 | BIT3)) >> 2)
                                {
                                case 3:
                                    deviceStats->sasStatistics.readReverseErrorsCorrectedWithPossibleDelays.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_GREATER;
                                    break;
                                case 2:
                                    deviceStats->sasStatistics.readReverseErrorsCorrectedWithPossibleDelays.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL;
                                    break;
                                case 1:
                                    deviceStats->sasStatistics.readReverseErrorsCorrectedWithPossibleDelays.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL;
                                    break;
                                case 0:
                                default:
                                    deviceStats->sasStatistics.readReverseErrorsCorrectedWithPossibleDelays.threshType =
                                        THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE;
                                    break;
                                }
                            }
                            switch (parameterLength)
                            {
                            case 1: // single byte
                                deviceStats->sasStatistics.readReverseErrorsCorrectedWithPossibleDelays.statisticValue =
                                    tempLogBuf[iter + 4];
                                break;
                            case 2: // word
                                deviceStats->sasStatistics.readReverseErrorsCorrectedWithPossibleDelays.statisticValue =
                                    M_BytesTo2ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5]);
                                break;
                            case 4: // double word
                                deviceStats->sasStatistics.readReverseErrorsCorrectedWithPossibleDelays.statisticValue =
                                    M_BytesTo4ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5],
                                                        tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
                                break;
                            case 8: // quad word
                                deviceStats->sasStatistics.readReverseErrorsCorrectedWithPossibleDelays.statisticValue =
                                    M_BytesTo8ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5],
                                                        tempLogBuf[iter + 6], tempLogBuf[iter + 7],
                                                        tempLogBuf[iter + 8], tempLogBuf[iter + 9],
                                                        tempLogBuf[iter + 10], tempLogBuf[iter + 11]);
                                break;
                            default: // don't bother trying to read the data since it's in a more complicated format to
                                     // read than we care to handle in this code right now
                                deviceStats->sasStatistics.readReverseErrorsCorrectedWithPossibleDelays.isValueValid =
                                    false;
                                break;
                            }
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            break;
                        case 2: // total
                            deviceStats->sasStatistics.readReverseTotalReReads.isSupported  = true;
                            deviceStats->sasStatistics.readReverseTotalReReads.isValueValid = true;
                            // check if thresholds supported, etc
                            deviceStats->sasStatistics.readReverseTotalReReads.thresholdNotificationEnabled =
                                tempLogBuf[iter + 2] & BIT4; // ETC bit
                            if (tempLogBuf[iter + 2] & BIT4)
                            {
                                switch ((tempLogBuf[iter + 2] & (BIT2 | BIT3)) >> 2)
                                {
                                case 3:
                                    deviceStats->sasStatistics.readReverseTotalReReads.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_GREATER;
                                    break;
                                case 2:
                                    deviceStats->sasStatistics.readReverseTotalReReads.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL;
                                    break;
                                case 1:
                                    deviceStats->sasStatistics.readReverseTotalReReads.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL;
                                    break;
                                case 0:
                                default:
                                    deviceStats->sasStatistics.readReverseTotalReReads.threshType =
                                        THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE;
                                    break;
                                }
                            }
                            switch (parameterLength)
                            {
                            case 1: // single byte
                                deviceStats->sasStatistics.readReverseTotalReReads.statisticValue =
                                    tempLogBuf[iter + 4];
                                break;
                            case 2: // word
                                deviceStats->sasStatistics.readReverseTotalReReads.statisticValue =
                                    M_BytesTo2ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5]);
                                break;
                            case 4: // double word
                                deviceStats->sasStatistics.readReverseTotalReReads.statisticValue =
                                    M_BytesTo4ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5],
                                                        tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
                                break;
                            case 8: // quad word
                                deviceStats->sasStatistics.readReverseTotalReReads.statisticValue = M_BytesTo8ByteValue(
                                    tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6],
                                    tempLogBuf[iter + 7], tempLogBuf[iter + 8], tempLogBuf[iter + 9],
                                    tempLogBuf[iter + 10], tempLogBuf[iter + 11]);
                                break;
                            default: // don't bother trying to read the data since it's in a more complicated format to
                                     // read than we care to handle in this code right now
                                deviceStats->sasStatistics.readReverseTotalReReads.isValueValid = false;
                                break;
                            }
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            break;
                        case 3: // total errors corrected
                            deviceStats->sasStatistics.readReverseErrorsCorrected.isSupported  = true;
                            deviceStats->sasStatistics.readReverseErrorsCorrected.isValueValid = true;
                            // check if thresholds supported, etc
                            deviceStats->sasStatistics.readReverseErrorsCorrected.thresholdNotificationEnabled =
                                tempLogBuf[iter + 2] & BIT4; // ETC bit
                            if (tempLogBuf[iter + 2] & BIT4)
                            {
                                switch ((tempLogBuf[iter + 2] & (BIT2 | BIT3)) >> 2)
                                {
                                case 3:
                                    deviceStats->sasStatistics.readReverseErrorsCorrected.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_GREATER;
                                    break;
                                case 2:
                                    deviceStats->sasStatistics.readReverseErrorsCorrected.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL;
                                    break;
                                case 1:
                                    deviceStats->sasStatistics.readReverseErrorsCorrected.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL;
                                    break;
                                case 0:
                                default:
                                    deviceStats->sasStatistics.readReverseErrorsCorrected.threshType =
                                        THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE;
                                    break;
                                }
                            }
                            switch (parameterLength)
                            {
                            case 1: // single byte
                                deviceStats->sasStatistics.readReverseErrorsCorrected.statisticValue =
                                    tempLogBuf[iter + 4];
                                break;
                            case 2: // word
                                deviceStats->sasStatistics.readReverseErrorsCorrected.statisticValue =
                                    M_BytesTo2ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5]);
                                break;
                            case 4: // double word
                                deviceStats->sasStatistics.readReverseErrorsCorrected.statisticValue =
                                    M_BytesTo4ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5],
                                                        tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
                                break;
                            case 8: // quad word
                                deviceStats->sasStatistics.readReverseErrorsCorrected.statisticValue =
                                    M_BytesTo8ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5],
                                                        tempLogBuf[iter + 6], tempLogBuf[iter + 7],
                                                        tempLogBuf[iter + 8], tempLogBuf[iter + 9],
                                                        tempLogBuf[iter + 10], tempLogBuf[iter + 11]);
                                break;
                            default: // don't bother trying to read the data since it's in a more complicated format to
                                     // read than we care to handle in this code right now
                                deviceStats->sasStatistics.readReverseErrorsCorrected.isValueValid = false;
                                break;
                            }
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            break;
                        case 4: // total times correction algorithm processed
                            deviceStats->sasStatistics.readReverseTotalTimeCorrectionAlgorithmProcessed.isSupported =
                                true;
                            deviceStats->sasStatistics.readReverseTotalTimeCorrectionAlgorithmProcessed.isValueValid =
                                true;
                            // check if thresholds supported, etc
                            deviceStats->sasStatistics.readReverseTotalTimeCorrectionAlgorithmProcessed
                                .thresholdNotificationEnabled = tempLogBuf[iter + 2] & BIT4; // ETC bit
                            if (tempLogBuf[iter + 2] & BIT4)
                            {
                                switch ((tempLogBuf[iter + 2] & (BIT2 | BIT3)) >> 2)
                                {
                                case 3:
                                    deviceStats->sasStatistics.readReverseTotalTimeCorrectionAlgorithmProcessed
                                        .threshType = THRESHOLD_TYPE_TRIGGER_WHEN_GREATER;
                                    break;
                                case 2:
                                    deviceStats->sasStatistics.readReverseTotalTimeCorrectionAlgorithmProcessed
                                        .threshType = THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL;
                                    break;
                                case 1:
                                    deviceStats->sasStatistics.readReverseTotalTimeCorrectionAlgorithmProcessed
                                        .threshType = THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL;
                                    break;
                                case 0:
                                default:
                                    deviceStats->sasStatistics.readReverseTotalTimeCorrectionAlgorithmProcessed
                                        .threshType = THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE;
                                    break;
                                }
                            }
                            switch (parameterLength)
                            {
                            case 1: // single byte
                                deviceStats->sasStatistics.readReverseTotalTimeCorrectionAlgorithmProcessed
                                    .statisticValue = tempLogBuf[iter + 4];
                                break;
                            case 2: // word
                                deviceStats->sasStatistics.readReverseTotalTimeCorrectionAlgorithmProcessed
                                    .statisticValue = M_BytesTo2ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5]);
                                break;
                            case 4: // double word
                                deviceStats->sasStatistics.readReverseTotalTimeCorrectionAlgorithmProcessed
                                    .statisticValue = M_BytesTo4ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5],
                                                                          tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
                                break;
                            case 8: // quad word
                                deviceStats->sasStatistics.readReverseTotalTimeCorrectionAlgorithmProcessed
                                    .statisticValue = M_BytesTo8ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5],
                                                                          tempLogBuf[iter + 6], tempLogBuf[iter + 7],
                                                                          tempLogBuf[iter + 8], tempLogBuf[iter + 9],
                                                                          tempLogBuf[iter + 10], tempLogBuf[iter + 11]);
                                break;
                            default: // don't bother trying to read the data since it's in a more complicated format to
                                     // read than we care to handle in this code right now
                                deviceStats->sasStatistics.readReverseTotalTimeCorrectionAlgorithmProcessed
                                    .isValueValid = false;
                                break;
                            }
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            break;
                        case 5: // total bytes processed
                            deviceStats->sasStatistics.readReverseTotalBytesProcessed.isSupported  = true;
                            deviceStats->sasStatistics.readReverseTotalBytesProcessed.isValueValid = true;
                            // check if thresholds supported, etc
                            deviceStats->sasStatistics.readReverseTotalBytesProcessed.thresholdNotificationEnabled =
                                tempLogBuf[iter + 2] & BIT4; // ETC bit
                            if (tempLogBuf[iter + 2] & BIT4)
                            {
                                switch ((tempLogBuf[iter + 2] & (BIT2 | BIT3)) >> 2)
                                {
                                case 3:
                                    deviceStats->sasStatistics.readReverseTotalBytesProcessed.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_GREATER;
                                    break;
                                case 2:
                                    deviceStats->sasStatistics.readReverseTotalBytesProcessed.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL;
                                    break;
                                case 1:
                                    deviceStats->sasStatistics.readReverseTotalBytesProcessed.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL;
                                    break;
                                case 0:
                                default:
                                    deviceStats->sasStatistics.readReverseTotalBytesProcessed.threshType =
                                        THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE;
                                    break;
                                }
                            }
                            switch (parameterLength)
                            {
                            case 1: // single byte
                                deviceStats->sasStatistics.readReverseTotalBytesProcessed.statisticValue =
                                    tempLogBuf[iter + 4];
                                break;
                            case 2: // word
                                deviceStats->sasStatistics.readReverseTotalBytesProcessed.statisticValue =
                                    M_BytesTo2ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5]);
                                break;
                            case 4: // double word
                                deviceStats->sasStatistics.readReverseTotalBytesProcessed.statisticValue =
                                    M_BytesTo4ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5],
                                                        tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
                                break;
                            case 8: // quad word
                                deviceStats->sasStatistics.readReverseTotalBytesProcessed.statisticValue =
                                    M_BytesTo8ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5],
                                                        tempLogBuf[iter + 6], tempLogBuf[iter + 7],
                                                        tempLogBuf[iter + 8], tempLogBuf[iter + 9],
                                                        tempLogBuf[iter + 10], tempLogBuf[iter + 11]);
                                break;
                            default: // don't bother trying to read the data since it's in a more complicated format to
                                     // read than we care to handle in this code right now
                                deviceStats->sasStatistics.readReverseTotalBytesProcessed.isValueValid = false;
                                break;
                            }
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            break;
                        case 6: // total uncorrected errors
                            deviceStats->sasStatistics.readReverseTotalUncorrectedErrors.isSupported  = true;
                            deviceStats->sasStatistics.readReverseTotalUncorrectedErrors.isValueValid = true;
                            // check if thresholds supported, etc
                            deviceStats->sasStatistics.readReverseTotalUncorrectedErrors.thresholdNotificationEnabled =
                                tempLogBuf[iter + 2] & BIT4; // ETC bit
                            if (tempLogBuf[iter + 2] & BIT4)
                            {
                                switch ((tempLogBuf[iter + 2] & (BIT2 | BIT3)) >> 2)
                                {
                                case 3:
                                    deviceStats->sasStatistics.readReverseTotalUncorrectedErrors.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_GREATER;
                                    break;
                                case 2:
                                    deviceStats->sasStatistics.readReverseTotalUncorrectedErrors.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL;
                                    break;
                                case 1:
                                    deviceStats->sasStatistics.readReverseTotalUncorrectedErrors.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL;
                                    break;
                                case 0:
                                default:
                                    deviceStats->sasStatistics.readReverseTotalUncorrectedErrors.threshType =
                                        THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE;
                                    break;
                                }
                            }
                            switch (parameterLength)
                            {
                            case 1: // single byte
                                deviceStats->sasStatistics.readReverseTotalUncorrectedErrors.statisticValue =
                                    tempLogBuf[iter + 4];
                                break;
                            case 2: // word
                                deviceStats->sasStatistics.readReverseTotalUncorrectedErrors.statisticValue =
                                    M_BytesTo2ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5]);
                                break;
                            case 4: // double word
                                deviceStats->sasStatistics.readReverseTotalUncorrectedErrors.statisticValue =
                                    M_BytesTo4ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5],
                                                        tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
                                break;
                            case 8: // quad word
                                deviceStats->sasStatistics.readReverseTotalUncorrectedErrors.statisticValue =
                                    M_BytesTo8ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5],
                                                        tempLogBuf[iter + 6], tempLogBuf[iter + 7],
                                                        tempLogBuf[iter + 8], tempLogBuf[iter + 9],
                                                        tempLogBuf[iter + 10], tempLogBuf[iter + 11]);
                                break;
                            default: // don't bother trying to read the data since it's in a more complicated format to
                                     // read than we care to handle in this code right now
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
                    // thresholds
                    safe_memset(tempLogBuf, LEGACY_DRIVE_SEC_SIZE, 0, LEGACY_DRIVE_SEC_SIZE);
                    if (SUCCESS == scsi_Log_Sense_Cmd(device, false, LPC_THRESHOLD_VALUES, pageCode, subpageCode,
                                                      0x0000, tempLogBuf, LEGACY_DRIVE_SEC_SIZE))
                    {
                        pageLength      = M_BytesTo2ByteValue(tempLogBuf[2], tempLogBuf[3]);
                        parameterLength = 0;
                        // loop through the data and gather the data from each parameter we care about getting.
                        for (uint16_t iter = UINT16_C(4); iter < pageLength && iter < LEGACY_DRIVE_SEC_SIZE;
                             iter += (C_CAST(uint16_t, parameterLength + UINT16_C(4))))
                        {
                            uint16_t parameterCode = M_BytesTo2ByteValue(tempLogBuf[iter], tempLogBuf[iter + 1]);
                            parameterLength        = tempLogBuf[iter + 3];
                            switch (parameterCode)
                            {
                            case 0: // Errors corrected without substantial delay
                                deviceStats->sasStatistics.readReverseErrorsCorrectedWithoutSubstantialDelay
                                    .supportsNotification = true;
                                if (tempLogBuf[iter + 2] & BIT4)
                                {
                                    deviceStats->sasStatistics.readReverseErrorsCorrectedWithoutSubstantialDelay
                                        .isThresholdValid = true;
                                    switch (parameterLength)
                                    {
                                    case 1: // single byte
                                        deviceStats->sasStatistics.readReverseErrorsCorrectedWithoutSubstantialDelay
                                            .threshold = tempLogBuf[iter + 4];
                                        break;
                                    case 2: // word
                                        deviceStats->sasStatistics.readReverseErrorsCorrectedWithoutSubstantialDelay
                                            .threshold =
                                            M_BytesTo2ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5]);
                                        break;
                                    case 4: // double word
                                        deviceStats->sasStatistics.readReverseErrorsCorrectedWithoutSubstantialDelay
                                            .threshold =
                                            M_BytesTo4ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5],
                                                                tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
                                        break;
                                    case 8: // quad word
                                        deviceStats->sasStatistics.readReverseErrorsCorrectedWithoutSubstantialDelay
                                            .threshold = M_BytesTo8ByteValue(
                                            tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6],
                                            tempLogBuf[iter + 7], tempLogBuf[iter + 8], tempLogBuf[iter + 9],
                                            tempLogBuf[iter + 10], tempLogBuf[iter + 11]);
                                        break;
                                    default: // don't bother trying to read the data since it's in a more complicated
                                             // format to read than we care to handle in this code right now
                                        deviceStats->sasStatistics.readReverseErrorsCorrectedWithoutSubstantialDelay
                                            .isThresholdValid = false;
                                        break;
                                    }
                                    scsi_Threshold_Comparison(
                                        &deviceStats->sasStatistics.readReverseErrorsCorrectedWithoutSubstantialDelay);
                                }
                                break;
                            case 1: // Errors corrected with possible delays
                                deviceStats->sasStatistics.readReverseErrorsCorrectedWithPossibleDelays
                                    .supportsNotification = true;
                                if (tempLogBuf[iter + 2] & BIT4)
                                {
                                    deviceStats->sasStatistics.readReverseErrorsCorrectedWithPossibleDelays
                                        .isThresholdValid = true;
                                    switch (parameterLength)
                                    {
                                    case 1: // single byte
                                        deviceStats->sasStatistics.readReverseErrorsCorrectedWithPossibleDelays
                                            .threshold = tempLogBuf[iter + 4];
                                        break;
                                    case 2: // word
                                        deviceStats->sasStatistics.readReverseErrorsCorrectedWithPossibleDelays
                                            .threshold =
                                            M_BytesTo2ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5]);
                                        break;
                                    case 4: // double word
                                        deviceStats->sasStatistics.readReverseErrorsCorrectedWithPossibleDelays
                                            .threshold =
                                            M_BytesTo4ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5],
                                                                tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
                                        break;
                                    case 8: // quad word
                                        deviceStats->sasStatistics.readReverseErrorsCorrectedWithPossibleDelays
                                            .threshold = M_BytesTo8ByteValue(
                                            tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6],
                                            tempLogBuf[iter + 7], tempLogBuf[iter + 8], tempLogBuf[iter + 9],
                                            tempLogBuf[iter + 10], tempLogBuf[iter + 11]);
                                        break;
                                    default: // don't bother trying to read the data since it's in a more complicated
                                             // format to read than we care to handle in this code right now
                                        deviceStats->sasStatistics.readReverseErrorsCorrectedWithPossibleDelays
                                            .isThresholdValid = false;
                                        break;
                                    }
                                    scsi_Threshold_Comparison(
                                        &deviceStats->sasStatistics.readReverseErrorsCorrectedWithPossibleDelays);
                                }
                                break;
                            case 2: // total
                                deviceStats->sasStatistics.readReverseTotalReReads.supportsNotification = true;
                                if (tempLogBuf[iter + 2] & BIT4)
                                {
                                    deviceStats->sasStatistics.readReverseTotalReReads.isThresholdValid = true;
                                    switch (parameterLength)
                                    {
                                    case 1: // single byte
                                        deviceStats->sasStatistics.readReverseTotalReReads.threshold =
                                            tempLogBuf[iter + 4];
                                        break;
                                    case 2: // word
                                        deviceStats->sasStatistics.readReverseTotalReReads.threshold =
                                            M_BytesTo2ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5]);
                                        break;
                                    case 4: // double word
                                        deviceStats->sasStatistics.readReverseTotalReReads.threshold =
                                            M_BytesTo4ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5],
                                                                tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
                                        break;
                                    case 8: // quad word
                                        deviceStats->sasStatistics.readReverseTotalReReads.threshold =
                                            M_BytesTo8ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5],
                                                                tempLogBuf[iter + 6], tempLogBuf[iter + 7],
                                                                tempLogBuf[iter + 8], tempLogBuf[iter + 9],
                                                                tempLogBuf[iter + 10], tempLogBuf[iter + 11]);
                                        break;
                                    default: // don't bother trying to read the data since it's in a more complicated
                                             // format to read than we care to handle in this code right now
                                        deviceStats->sasStatistics.readReverseTotalReReads.isThresholdValid = false;
                                        break;
                                    }
                                    scsi_Threshold_Comparison(&deviceStats->sasStatistics.readReverseTotalReReads);
                                }
                                break;
                            case 3: // total errors corrected
                                deviceStats->sasStatistics.readReverseErrorsCorrected.supportsNotification = true;
                                if (tempLogBuf[iter + 2] & BIT4)
                                {
                                    deviceStats->sasStatistics.readReverseErrorsCorrected.isThresholdValid = true;
                                    switch (parameterLength)
                                    {
                                    case 1: // single byte
                                        deviceStats->sasStatistics.readReverseErrorsCorrected.threshold =
                                            tempLogBuf[iter + 4];
                                        break;
                                    case 2: // word
                                        deviceStats->sasStatistics.readReverseErrorsCorrected.threshold =
                                            M_BytesTo2ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5]);
                                        break;
                                    case 4: // double word
                                        deviceStats->sasStatistics.readReverseErrorsCorrected.threshold =
                                            M_BytesTo4ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5],
                                                                tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
                                        break;
                                    case 8: // quad word
                                        deviceStats->sasStatistics.readReverseErrorsCorrected.threshold =
                                            M_BytesTo8ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5],
                                                                tempLogBuf[iter + 6], tempLogBuf[iter + 7],
                                                                tempLogBuf[iter + 8], tempLogBuf[iter + 9],
                                                                tempLogBuf[iter + 10], tempLogBuf[iter + 11]);
                                        break;
                                    default: // don't bother trying to read the data since it's in a more complicated
                                             // format to read than we care to handle in this code right now
                                        deviceStats->sasStatistics.readReverseErrorsCorrected.isThresholdValid = false;
                                        break;
                                    }
                                    scsi_Threshold_Comparison(&deviceStats->sasStatistics.readReverseErrorsCorrected);
                                }
                                break;
                            case 4: // total times correction algorithm processed
                                deviceStats->sasStatistics.readReverseTotalTimeCorrectionAlgorithmProcessed
                                    .supportsNotification = true;
                                if (tempLogBuf[iter + 2] & BIT4)
                                {
                                    deviceStats->sasStatistics.readReverseTotalTimeCorrectionAlgorithmProcessed
                                        .isThresholdValid = true;
                                    switch (parameterLength)
                                    {
                                    case 1: // single byte
                                        deviceStats->sasStatistics.readReverseTotalTimeCorrectionAlgorithmProcessed
                                            .threshold = tempLogBuf[iter + 4];
                                        break;
                                    case 2: // word
                                        deviceStats->sasStatistics.readReverseTotalTimeCorrectionAlgorithmProcessed
                                            .threshold =
                                            M_BytesTo2ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5]);
                                        break;
                                    case 4: // double word
                                        deviceStats->sasStatistics.readReverseTotalTimeCorrectionAlgorithmProcessed
                                            .threshold =
                                            M_BytesTo4ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5],
                                                                tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
                                        break;
                                    case 8: // quad word
                                        deviceStats->sasStatistics.readReverseTotalTimeCorrectionAlgorithmProcessed
                                            .threshold = M_BytesTo8ByteValue(
                                            tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6],
                                            tempLogBuf[iter + 7], tempLogBuf[iter + 8], tempLogBuf[iter + 9],
                                            tempLogBuf[iter + 10], tempLogBuf[iter + 11]);
                                        break;
                                    default: // don't bother trying to read the data since it's in a more complicated
                                             // format to read than we care to handle in this code right now
                                        deviceStats->sasStatistics.readReverseTotalTimeCorrectionAlgorithmProcessed
                                            .isThresholdValid = false;
                                        break;
                                    }
                                    scsi_Threshold_Comparison(
                                        &deviceStats->sasStatistics.readReverseTotalTimeCorrectionAlgorithmProcessed);
                                }
                                break;
                            case 5: // total bytes processed
                                deviceStats->sasStatistics.readReverseTotalBytesProcessed.supportsNotification = true;
                                if (tempLogBuf[iter + 2] & BIT4)
                                {
                                    deviceStats->sasStatistics.readReverseTotalBytesProcessed.isThresholdValid = true;
                                    switch (parameterLength)
                                    {
                                    case 1: // single byte
                                        deviceStats->sasStatistics.readReverseTotalBytesProcessed.threshold =
                                            tempLogBuf[iter + 4];
                                        break;
                                    case 2: // word
                                        deviceStats->sasStatistics.readReverseTotalBytesProcessed.threshold =
                                            M_BytesTo2ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5]);
                                        break;
                                    case 4: // double word
                                        deviceStats->sasStatistics.readReverseTotalBytesProcessed.threshold =
                                            M_BytesTo4ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5],
                                                                tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
                                        break;
                                    case 8: // quad word
                                        deviceStats->sasStatistics.readReverseTotalBytesProcessed.threshold =
                                            M_BytesTo8ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5],
                                                                tempLogBuf[iter + 6], tempLogBuf[iter + 7],
                                                                tempLogBuf[iter + 8], tempLogBuf[iter + 9],
                                                                tempLogBuf[iter + 10], tempLogBuf[iter + 11]);
                                        break;
                                    default: // don't bother trying to read the data since it's in a more complicated
                                             // format to read than we care to handle in this code right now
                                        deviceStats->sasStatistics.readReverseTotalBytesProcessed.isThresholdValid =
                                            false;
                                        break;
                                    }
                                    scsi_Threshold_Comparison(
                                        &deviceStats->sasStatistics.readReverseTotalBytesProcessed);
                                }
                                break;
                            case 6: // total uncorrected errors
                                deviceStats->sasStatistics.readReverseTotalUncorrectedErrors.supportsNotification =
                                    true;
                                if (tempLogBuf[iter + 2] & BIT4)
                                {
                                    deviceStats->sasStatistics.readReverseTotalUncorrectedErrors.isThresholdValid =
                                        true;
                                    switch (parameterLength)
                                    {
                                    case 1: // single byte
                                        deviceStats->sasStatistics.readReverseTotalUncorrectedErrors.threshold =
                                            tempLogBuf[iter + 4];
                                        break;
                                    case 2: // word
                                        deviceStats->sasStatistics.readReverseTotalUncorrectedErrors.threshold =
                                            M_BytesTo2ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5]);
                                        break;
                                    case 4: // double word
                                        deviceStats->sasStatistics.readReverseTotalUncorrectedErrors.threshold =
                                            M_BytesTo4ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5],
                                                                tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
                                        break;
                                    case 8: // quad word
                                        deviceStats->sasStatistics.readReverseTotalUncorrectedErrors.threshold =
                                            M_BytesTo8ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5],
                                                                tempLogBuf[iter + 6], tempLogBuf[iter + 7],
                                                                tempLogBuf[iter + 8], tempLogBuf[iter + 9],
                                                                tempLogBuf[iter + 10], tempLogBuf[iter + 11]);
                                        break;
                                    default: // don't bother trying to read the data since it's in a more complicated
                                             // format to read than we care to handle in this code right now
                                        deviceStats->sasStatistics.readReverseTotalUncorrectedErrors.isThresholdValid =
                                            false;
                                        break;
                                    }
                                    scsi_Threshold_Comparison(
                                        &deviceStats->sasStatistics.readReverseTotalUncorrectedErrors);
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
                safe_memset(tempLogBuf, LEGACY_DRIVE_SEC_SIZE, 0, LEGACY_DRIVE_SEC_SIZE);
                if (SUCCESS == scsi_Log_Sense_Cmd(device, false, LPC_CUMULATIVE_VALUES, pageCode, subpageCode, 0x0000,
                                                  tempLogBuf, LEGACY_DRIVE_SEC_SIZE))
                {
                    deviceStats->sasStatistics.verifyErrorCountersSupported = true;
                    uint16_t pageLength      = M_BytesTo2ByteValue(tempLogBuf[2], tempLogBuf[3]);
                    uint8_t  parameterLength = UINT8_C(0);
                    // loop through the data and gather the data from each parameter we care about getting.
                    for (uint16_t iter = UINT16_C(4); iter < pageLength && iter < LEGACY_DRIVE_SEC_SIZE;
                         iter += (C_CAST(uint16_t, parameterLength + UINT16_C(4))))
                    {
                        uint16_t parameterCode = M_BytesTo2ByteValue(tempLogBuf[iter], tempLogBuf[iter + 1]);
                        parameterLength        = tempLogBuf[iter + 3];
                        switch (parameterCode)
                        {
                        case 0: // Errors corrected without substantial delay
                            deviceStats->sasStatistics.verifyErrorsCorrectedWithoutSubstantialDelay.isSupported  = true;
                            deviceStats->sasStatistics.verifyErrorsCorrectedWithoutSubstantialDelay.isValueValid = true;
                            // check if thresholds supported, etc
                            deviceStats->sasStatistics.verifyErrorsCorrectedWithoutSubstantialDelay
                                .thresholdNotificationEnabled = tempLogBuf[iter + 2] & BIT4; // ETC bit
                            if (tempLogBuf[iter + 2] & BIT4)
                            {
                                switch ((tempLogBuf[iter + 2] & (BIT2 | BIT3)) >> 2)
                                {
                                case 3:
                                    deviceStats->sasStatistics.verifyErrorsCorrectedWithoutSubstantialDelay.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_GREATER;
                                    break;
                                case 2:
                                    deviceStats->sasStatistics.verifyErrorsCorrectedWithoutSubstantialDelay.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL;
                                    break;
                                case 1:
                                    deviceStats->sasStatistics.verifyErrorsCorrectedWithoutSubstantialDelay.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL;
                                    break;
                                case 0:
                                default:
                                    deviceStats->sasStatistics.verifyErrorsCorrectedWithoutSubstantialDelay.threshType =
                                        THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE;
                                    break;
                                }
                            }
                            switch (parameterLength)
                            {
                            case 1: // single byte
                                deviceStats->sasStatistics.verifyErrorsCorrectedWithoutSubstantialDelay.statisticValue =
                                    tempLogBuf[iter + 4];
                                break;
                            case 2: // word
                                deviceStats->sasStatistics.verifyErrorsCorrectedWithoutSubstantialDelay.statisticValue =
                                    M_BytesTo2ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5]);
                                break;
                            case 4: // double word
                                deviceStats->sasStatistics.verifyErrorsCorrectedWithoutSubstantialDelay.statisticValue =
                                    M_BytesTo4ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5],
                                                        tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
                                break;
                            case 8: // quad word
                                deviceStats->sasStatistics.verifyErrorsCorrectedWithoutSubstantialDelay.statisticValue =
                                    M_BytesTo8ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5],
                                                        tempLogBuf[iter + 6], tempLogBuf[iter + 7],
                                                        tempLogBuf[iter + 8], tempLogBuf[iter + 9],
                                                        tempLogBuf[iter + 10], tempLogBuf[iter + 11]);
                                break;
                            default: // don't bother trying to read the data since it's in a more complicated format to
                                     // read than we care to handle in this code right now
                                deviceStats->sasStatistics.verifyErrorsCorrectedWithoutSubstantialDelay.isValueValid =
                                    false;
                                break;
                            }
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            break;
                        case 1: // Errors corrected with possible delays
                            deviceStats->sasStatistics.verifyErrorsCorrectedWithPossibleDelays.isSupported  = true;
                            deviceStats->sasStatistics.verifyErrorsCorrectedWithPossibleDelays.isValueValid = true;
                            // check if thresholds supported, etc
                            deviceStats->sasStatistics.verifyErrorsCorrectedWithPossibleDelays
                                .thresholdNotificationEnabled = tempLogBuf[iter + 2] & BIT4; // ETC bit
                            if (tempLogBuf[iter + 2] & BIT4)
                            {
                                switch ((tempLogBuf[iter + 2] & (BIT2 | BIT3)) >> 2)
                                {
                                case 3:
                                    deviceStats->sasStatistics.verifyErrorsCorrectedWithPossibleDelays.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_GREATER;
                                    break;
                                case 2:
                                    deviceStats->sasStatistics.verifyErrorsCorrectedWithPossibleDelays.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL;
                                    break;
                                case 1:
                                    deviceStats->sasStatistics.verifyErrorsCorrectedWithPossibleDelays.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL;
                                    break;
                                case 0:
                                default:
                                    deviceStats->sasStatistics.verifyErrorsCorrectedWithPossibleDelays.threshType =
                                        THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE;
                                    break;
                                }
                            }
                            switch (parameterLength)
                            {
                            case 1: // single byte
                                deviceStats->sasStatistics.verifyErrorsCorrectedWithPossibleDelays.statisticValue =
                                    tempLogBuf[iter + 4];
                                break;
                            case 2: // word
                                deviceStats->sasStatistics.verifyErrorsCorrectedWithPossibleDelays.statisticValue =
                                    M_BytesTo2ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5]);
                                break;
                            case 4: // double word
                                deviceStats->sasStatistics.verifyErrorsCorrectedWithPossibleDelays.statisticValue =
                                    M_BytesTo4ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5],
                                                        tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
                                break;
                            case 8: // quad word
                                deviceStats->sasStatistics.verifyErrorsCorrectedWithPossibleDelays.statisticValue =
                                    M_BytesTo8ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5],
                                                        tempLogBuf[iter + 6], tempLogBuf[iter + 7],
                                                        tempLogBuf[iter + 8], tempLogBuf[iter + 9],
                                                        tempLogBuf[iter + 10], tempLogBuf[iter + 11]);
                                break;
                            default: // don't bother trying to read the data since it's in a more complicated format to
                                     // read than we care to handle in this code right now
                                deviceStats->sasStatistics.verifyErrorsCorrectedWithPossibleDelays.isValueValid = false;
                                break;
                            }
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            break;
                        case 2: // total revverifies
                            deviceStats->sasStatistics.verifyTotalReVerifies.isSupported  = true;
                            deviceStats->sasStatistics.verifyTotalReVerifies.isValueValid = true;
                            // check if thresholds supported, etc
                            deviceStats->sasStatistics.verifyTotalReVerifies.thresholdNotificationEnabled =
                                tempLogBuf[iter + 2] & BIT4; // ETC bit
                            if (tempLogBuf[iter + 2] & BIT4)
                            {
                                switch ((tempLogBuf[iter + 2] & (BIT2 | BIT3)) >> 2)
                                {
                                case 3:
                                    deviceStats->sasStatistics.verifyTotalReVerifies.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_GREATER;
                                    break;
                                case 2:
                                    deviceStats->sasStatistics.verifyTotalReVerifies.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL;
                                    break;
                                case 1:
                                    deviceStats->sasStatistics.verifyTotalReVerifies.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL;
                                    break;
                                case 0:
                                default:
                                    deviceStats->sasStatistics.verifyTotalReVerifies.threshType =
                                        THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE;
                                    break;
                                }
                            }
                            switch (parameterLength)
                            {
                            case 1: // single byte
                                deviceStats->sasStatistics.verifyTotalReVerifies.statisticValue = tempLogBuf[iter + 4];
                                break;
                            case 2: // word
                                deviceStats->sasStatistics.verifyTotalReVerifies.statisticValue =
                                    M_BytesTo2ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5]);
                                break;
                            case 4: // double word
                                deviceStats->sasStatistics.verifyTotalReVerifies.statisticValue =
                                    M_BytesTo4ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5],
                                                        tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
                                break;
                            case 8: // quad word
                                deviceStats->sasStatistics.verifyTotalReVerifies.statisticValue = M_BytesTo8ByteValue(
                                    tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6],
                                    tempLogBuf[iter + 7], tempLogBuf[iter + 8], tempLogBuf[iter + 9],
                                    tempLogBuf[iter + 10], tempLogBuf[iter + 11]);
                                break;
                            default: // don't bother trying to read the data since it's in a more complicated format to
                                     // read than we care to handle in this code right now
                                deviceStats->sasStatistics.verifyTotalReVerifies.isValueValid = false;
                                break;
                            }
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            break;
                        case 3: // total errors corrected
                            deviceStats->sasStatistics.verifyErrorsCorrected.isSupported  = true;
                            deviceStats->sasStatistics.verifyErrorsCorrected.isValueValid = true;
                            // check if thresholds supported, etc
                            deviceStats->sasStatistics.verifyErrorsCorrected.thresholdNotificationEnabled =
                                tempLogBuf[iter + 2] & BIT4; // ETC bit
                            if (tempLogBuf[iter + 2] & BIT4)
                            {
                                switch ((tempLogBuf[iter + 2] & (BIT2 | BIT3)) >> 2)
                                {
                                case 3:
                                    deviceStats->sasStatistics.verifyErrorsCorrected.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_GREATER;
                                    break;
                                case 2:
                                    deviceStats->sasStatistics.verifyErrorsCorrected.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL;
                                    break;
                                case 1:
                                    deviceStats->sasStatistics.verifyErrorsCorrected.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL;
                                    break;
                                case 0:
                                default:
                                    deviceStats->sasStatistics.verifyErrorsCorrected.threshType =
                                        THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE;
                                    break;
                                }
                            }
                            switch (parameterLength)
                            {
                            case 1: // single byte
                                deviceStats->sasStatistics.verifyErrorsCorrected.statisticValue = tempLogBuf[iter + 4];
                                break;
                            case 2: // word
                                deviceStats->sasStatistics.verifyErrorsCorrected.statisticValue =
                                    M_BytesTo2ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5]);
                                break;
                            case 4: // double word
                                deviceStats->sasStatistics.verifyErrorsCorrected.statisticValue =
                                    M_BytesTo4ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5],
                                                        tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
                                break;
                            case 8: // quad word
                                deviceStats->sasStatistics.verifyErrorsCorrected.statisticValue = M_BytesTo8ByteValue(
                                    tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6],
                                    tempLogBuf[iter + 7], tempLogBuf[iter + 8], tempLogBuf[iter + 9],
                                    tempLogBuf[iter + 10], tempLogBuf[iter + 11]);
                                break;
                            default: // don't bother trying to read the data since it's in a more complicated format to
                                     // read than we care to handle in this code right now
                                deviceStats->sasStatistics.verifyErrorsCorrected.isValueValid = false;
                                break;
                            }
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            break;
                        case 4: // total times correction algorithm processed
                            deviceStats->sasStatistics.verifyTotalTimeCorrectionAlgorithmProcessed.isSupported  = true;
                            deviceStats->sasStatistics.verifyTotalTimeCorrectionAlgorithmProcessed.isValueValid = true;
                            // check if thresholds supported, etc
                            deviceStats->sasStatistics.verifyTotalTimeCorrectionAlgorithmProcessed
                                .thresholdNotificationEnabled = tempLogBuf[iter + 2] & BIT4; // ETC bit
                            if (tempLogBuf[iter + 2] & BIT4)
                            {
                                switch ((tempLogBuf[iter + 2] & (BIT2 | BIT3)) >> 2)
                                {
                                case 3:
                                    deviceStats->sasStatistics.verifyTotalTimeCorrectionAlgorithmProcessed.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_GREATER;
                                    break;
                                case 2:
                                    deviceStats->sasStatistics.verifyTotalTimeCorrectionAlgorithmProcessed.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL;
                                    break;
                                case 1:
                                    deviceStats->sasStatistics.verifyTotalTimeCorrectionAlgorithmProcessed.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL;
                                    break;
                                case 0:
                                default:
                                    deviceStats->sasStatistics.verifyTotalTimeCorrectionAlgorithmProcessed.threshType =
                                        THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE;
                                    break;
                                }
                            }
                            switch (parameterLength)
                            {
                            case 1: // single byte
                                deviceStats->sasStatistics.verifyTotalTimeCorrectionAlgorithmProcessed.statisticValue =
                                    tempLogBuf[iter + 4];
                                break;
                            case 2: // word
                                deviceStats->sasStatistics.verifyTotalTimeCorrectionAlgorithmProcessed.statisticValue =
                                    M_BytesTo2ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5]);
                                break;
                            case 4: // double word
                                deviceStats->sasStatistics.verifyTotalTimeCorrectionAlgorithmProcessed.statisticValue =
                                    M_BytesTo4ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5],
                                                        tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
                                break;
                            case 8: // quad word
                                deviceStats->sasStatistics.verifyTotalTimeCorrectionAlgorithmProcessed.statisticValue =
                                    M_BytesTo8ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5],
                                                        tempLogBuf[iter + 6], tempLogBuf[iter + 7],
                                                        tempLogBuf[iter + 8], tempLogBuf[iter + 9],
                                                        tempLogBuf[iter + 10], tempLogBuf[iter + 11]);
                                break;
                            default: // don't bother trying to read the data since it's in a more complicated format to
                                     // read than we care to handle in this code right now
                                deviceStats->sasStatistics.verifyTotalTimeCorrectionAlgorithmProcessed.isValueValid =
                                    false;
                                break;
                            }
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            break;
                        case 5: // total bytes processed
                            deviceStats->sasStatistics.verifyTotalBytesProcessed.isSupported  = true;
                            deviceStats->sasStatistics.verifyTotalBytesProcessed.isValueValid = true;
                            // check if thresholds supported, etc
                            deviceStats->sasStatistics.verifyTotalBytesProcessed.thresholdNotificationEnabled =
                                tempLogBuf[iter + 2] & BIT4; // ETC bit
                            if (tempLogBuf[iter + 2] & BIT4)
                            {
                                switch ((tempLogBuf[iter + 2] & (BIT2 | BIT3)) >> 2)
                                {
                                case 3:
                                    deviceStats->sasStatistics.verifyTotalBytesProcessed.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_GREATER;
                                    break;
                                case 2:
                                    deviceStats->sasStatistics.verifyTotalBytesProcessed.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL;
                                    break;
                                case 1:
                                    deviceStats->sasStatistics.verifyTotalBytesProcessed.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL;
                                    break;
                                case 0:
                                default:
                                    deviceStats->sasStatistics.verifyTotalBytesProcessed.threshType =
                                        THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE;
                                    break;
                                }
                            }
                            switch (parameterLength)
                            {
                            case 1: // single byte
                                deviceStats->sasStatistics.verifyTotalBytesProcessed.statisticValue =
                                    tempLogBuf[iter + 4];
                                break;
                            case 2: // word
                                deviceStats->sasStatistics.verifyTotalBytesProcessed.statisticValue =
                                    M_BytesTo2ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5]);
                                break;
                            case 4: // double word
                                deviceStats->sasStatistics.verifyTotalBytesProcessed.statisticValue =
                                    M_BytesTo4ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5],
                                                        tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
                                break;
                            case 8: // quad word
                                deviceStats->sasStatistics.verifyTotalBytesProcessed.statisticValue =
                                    M_BytesTo8ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5],
                                                        tempLogBuf[iter + 6], tempLogBuf[iter + 7],
                                                        tempLogBuf[iter + 8], tempLogBuf[iter + 9],
                                                        tempLogBuf[iter + 10], tempLogBuf[iter + 11]);
                                break;
                            default: // don't bother trying to read the data since it's in a more complicated format to
                                     // read than we care to handle in this code right now
                                deviceStats->sasStatistics.verifyTotalBytesProcessed.isValueValid = false;
                                break;
                            }
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            break;
                        case 6: // total uncorrected errors
                            deviceStats->sasStatistics.verifyTotalUncorrectedErrors.isSupported  = true;
                            deviceStats->sasStatistics.verifyTotalUncorrectedErrors.isValueValid = true;
                            // check if thresholds supported, etc
                            deviceStats->sasStatistics.verifyTotalUncorrectedErrors.thresholdNotificationEnabled =
                                tempLogBuf[iter + 2] & BIT4; // ETC bit
                            if (tempLogBuf[iter + 2] & BIT4)
                            {
                                switch ((tempLogBuf[iter + 2] & (BIT2 | BIT3)) >> 2)
                                {
                                case 3:
                                    deviceStats->sasStatistics.verifyTotalUncorrectedErrors.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_GREATER;
                                    break;
                                case 2:
                                    deviceStats->sasStatistics.verifyTotalUncorrectedErrors.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL;
                                    break;
                                case 1:
                                    deviceStats->sasStatistics.verifyTotalUncorrectedErrors.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL;
                                    break;
                                case 0:
                                default:
                                    deviceStats->sasStatistics.verifyTotalUncorrectedErrors.threshType =
                                        THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE;
                                    break;
                                }
                            }
                            switch (parameterLength)
                            {
                            case 1: // single byte
                                deviceStats->sasStatistics.verifyTotalUncorrectedErrors.statisticValue =
                                    tempLogBuf[iter + 4];
                                break;
                            case 2: // word
                                deviceStats->sasStatistics.verifyTotalUncorrectedErrors.statisticValue =
                                    M_BytesTo2ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5]);
                                break;
                            case 4: // double word
                                deviceStats->sasStatistics.verifyTotalUncorrectedErrors.statisticValue =
                                    M_BytesTo4ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5],
                                                        tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
                                break;
                            case 8: // quad word
                                deviceStats->sasStatistics.verifyTotalUncorrectedErrors.statisticValue =
                                    M_BytesTo8ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5],
                                                        tempLogBuf[iter + 6], tempLogBuf[iter + 7],
                                                        tempLogBuf[iter + 8], tempLogBuf[iter + 9],
                                                        tempLogBuf[iter + 10], tempLogBuf[iter + 11]);
                                break;
                            default: // don't bother trying to read the data since it's in a more complicated format to
                                     // read than we care to handle in this code right now
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
                    // thresholds
                    safe_memset(tempLogBuf, LEGACY_DRIVE_SEC_SIZE, 0, LEGACY_DRIVE_SEC_SIZE);
                    if (SUCCESS == scsi_Log_Sense_Cmd(device, false, LPC_THRESHOLD_VALUES, pageCode, subpageCode,
                                                      0x0000, tempLogBuf, LEGACY_DRIVE_SEC_SIZE))
                    {
                        pageLength      = M_BytesTo2ByteValue(tempLogBuf[2], tempLogBuf[3]);
                        parameterLength = 0;
                        // loop through the data and gather the data from each parameter we care about getting.
                        for (uint16_t iter = UINT16_C(4); iter < pageLength && iter < LEGACY_DRIVE_SEC_SIZE;
                             iter += (C_CAST(uint16_t, parameterLength + UINT16_C(4))))
                        {
                            uint16_t parameterCode = M_BytesTo2ByteValue(tempLogBuf[iter], tempLogBuf[iter + 1]);
                            parameterLength        = tempLogBuf[iter + 3];
                            switch (parameterCode)
                            {
                            case 0: // Errors corrected without substantial delay
                                deviceStats->sasStatistics.verifyErrorsCorrectedWithoutSubstantialDelay
                                    .supportsNotification = true;
                                if (tempLogBuf[iter + 2] & BIT4)
                                {
                                    deviceStats->sasStatistics.verifyErrorsCorrectedWithoutSubstantialDelay
                                        .isThresholdValid = true;
                                    switch (parameterLength)
                                    {
                                    case 1: // single byte
                                        deviceStats->sasStatistics.verifyErrorsCorrectedWithoutSubstantialDelay
                                            .threshold = tempLogBuf[iter + 4];
                                        break;
                                    case 2: // word
                                        deviceStats->sasStatistics.verifyErrorsCorrectedWithoutSubstantialDelay
                                            .threshold =
                                            M_BytesTo2ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5]);
                                        break;
                                    case 4: // double word
                                        deviceStats->sasStatistics.verifyErrorsCorrectedWithoutSubstantialDelay
                                            .threshold =
                                            M_BytesTo4ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5],
                                                                tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
                                        break;
                                    case 8: // quad word
                                        deviceStats->sasStatistics.verifyErrorsCorrectedWithoutSubstantialDelay
                                            .threshold = M_BytesTo8ByteValue(
                                            tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6],
                                            tempLogBuf[iter + 7], tempLogBuf[iter + 8], tempLogBuf[iter + 9],
                                            tempLogBuf[iter + 10], tempLogBuf[iter + 11]);
                                        break;
                                    default: // don't bother trying to read the data since it's in a more complicated
                                             // format to read than we care to handle in this code right now
                                        deviceStats->sasStatistics.verifyErrorsCorrectedWithoutSubstantialDelay
                                            .isThresholdValid = false;
                                        break;
                                    }
                                    scsi_Threshold_Comparison(
                                        &deviceStats->sasStatistics.verifyErrorsCorrectedWithoutSubstantialDelay);
                                }
                                break;
                            case 1: // Errors corrected with possible delays
                                deviceStats->sasStatistics.verifyErrorsCorrectedWithPossibleDelays
                                    .supportsNotification = true;
                                if (tempLogBuf[iter + 2] & BIT4)
                                {
                                    deviceStats->sasStatistics.verifyErrorsCorrectedWithPossibleDelays
                                        .isThresholdValid = true;
                                    switch (parameterLength)
                                    {
                                    case 1: // single byte
                                        deviceStats->sasStatistics.verifyErrorsCorrectedWithPossibleDelays.threshold =
                                            tempLogBuf[iter + 4];
                                        break;
                                    case 2: // word
                                        deviceStats->sasStatistics.verifyErrorsCorrectedWithPossibleDelays.threshold =
                                            M_BytesTo2ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5]);
                                        break;
                                    case 4: // double word
                                        deviceStats->sasStatistics.verifyErrorsCorrectedWithPossibleDelays.threshold =
                                            M_BytesTo4ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5],
                                                                tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
                                        break;
                                    case 8: // quad word
                                        deviceStats->sasStatistics.verifyErrorsCorrectedWithPossibleDelays.threshold =
                                            M_BytesTo8ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5],
                                                                tempLogBuf[iter + 6], tempLogBuf[iter + 7],
                                                                tempLogBuf[iter + 8], tempLogBuf[iter + 9],
                                                                tempLogBuf[iter + 10], tempLogBuf[iter + 11]);
                                        break;
                                    default: // don't bother trying to read the data since it's in a more complicated
                                             // format to read than we care to handle in this code right now
                                        deviceStats->sasStatistics.verifyErrorsCorrectedWithPossibleDelays
                                            .isThresholdValid = false;
                                        break;
                                    }
                                    scsi_Threshold_Comparison(
                                        &deviceStats->sasStatistics.verifyErrorsCorrectedWithPossibleDelays);
                                }
                                break;
                            case 2: // total reverifies
                                deviceStats->sasStatistics.verifyTotalReVerifies.supportsNotification = true;
                                if (tempLogBuf[iter + 2] & BIT4)
                                {
                                    deviceStats->sasStatistics.verifyTotalReVerifies.isThresholdValid = true;
                                    switch (parameterLength)
                                    {
                                    case 1: // single byte
                                        deviceStats->sasStatistics.verifyTotalReVerifies.threshold =
                                            tempLogBuf[iter + 4];
                                        break;
                                    case 2: // word
                                        deviceStats->sasStatistics.verifyTotalReVerifies.threshold =
                                            M_BytesTo2ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5]);
                                        break;
                                    case 4: // double word
                                        deviceStats->sasStatistics.verifyTotalReVerifies.threshold =
                                            M_BytesTo4ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5],
                                                                tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
                                        break;
                                    case 8: // quad word
                                        deviceStats->sasStatistics.verifyTotalReVerifies.threshold =
                                            M_BytesTo8ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5],
                                                                tempLogBuf[iter + 6], tempLogBuf[iter + 7],
                                                                tempLogBuf[iter + 8], tempLogBuf[iter + 9],
                                                                tempLogBuf[iter + 10], tempLogBuf[iter + 11]);
                                        break;
                                    default: // don't bother trying to read the data since it's in a more complicated
                                             // format to read than we care to handle in this code right now
                                        deviceStats->sasStatistics.verifyTotalReVerifies.isThresholdValid = false;
                                        break;
                                    }
                                    scsi_Threshold_Comparison(&deviceStats->sasStatistics.verifyTotalReVerifies);
                                }
                                break;
                            case 3: // total errors corrected
                                deviceStats->sasStatistics.verifyErrorsCorrected.supportsNotification = true;
                                if (tempLogBuf[iter + 2] & BIT4)
                                {
                                    deviceStats->sasStatistics.verifyErrorsCorrected.isThresholdValid = true;
                                    switch (parameterLength)
                                    {
                                    case 1: // single byte
                                        deviceStats->sasStatistics.verifyErrorsCorrected.threshold =
                                            tempLogBuf[iter + 4];
                                        break;
                                    case 2: // word
                                        deviceStats->sasStatistics.verifyErrorsCorrected.threshold =
                                            M_BytesTo2ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5]);
                                        break;
                                    case 4: // double word
                                        deviceStats->sasStatistics.verifyErrorsCorrected.threshold =
                                            M_BytesTo4ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5],
                                                                tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
                                        break;
                                    case 8: // quad word
                                        deviceStats->sasStatistics.verifyErrorsCorrected.threshold =
                                            M_BytesTo8ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5],
                                                                tempLogBuf[iter + 6], tempLogBuf[iter + 7],
                                                                tempLogBuf[iter + 8], tempLogBuf[iter + 9],
                                                                tempLogBuf[iter + 10], tempLogBuf[iter + 11]);
                                        break;
                                    default: // don't bother trying to read the data since it's in a more complicated
                                             // format to read than we care to handle in this code right now
                                        deviceStats->sasStatistics.verifyErrorsCorrected.isThresholdValid = false;
                                        break;
                                    }
                                    scsi_Threshold_Comparison(&deviceStats->sasStatistics.verifyErrorsCorrected);
                                }
                                break;
                            case 4: // total times correction algorithm processed
                                deviceStats->sasStatistics.verifyTotalTimeCorrectionAlgorithmProcessed
                                    .supportsNotification = true;
                                if (tempLogBuf[iter + 2] & BIT4)
                                {
                                    deviceStats->sasStatistics.verifyTotalTimeCorrectionAlgorithmProcessed
                                        .isThresholdValid = true;
                                    switch (parameterLength)
                                    {
                                    case 1: // single byte
                                        deviceStats->sasStatistics.verifyTotalTimeCorrectionAlgorithmProcessed
                                            .threshold = tempLogBuf[iter + 4];
                                        break;
                                    case 2: // word
                                        deviceStats->sasStatistics.verifyTotalTimeCorrectionAlgorithmProcessed
                                            .threshold =
                                            M_BytesTo2ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5]);
                                        break;
                                    case 4: // double word
                                        deviceStats->sasStatistics.verifyTotalTimeCorrectionAlgorithmProcessed
                                            .threshold =
                                            M_BytesTo4ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5],
                                                                tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
                                        break;
                                    case 8: // quad word
                                        deviceStats->sasStatistics.verifyTotalTimeCorrectionAlgorithmProcessed
                                            .threshold = M_BytesTo8ByteValue(
                                            tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6],
                                            tempLogBuf[iter + 7], tempLogBuf[iter + 8], tempLogBuf[iter + 9],
                                            tempLogBuf[iter + 10], tempLogBuf[iter + 11]);
                                        break;
                                    default: // don't bother trying to read the data since it's in a more complicated
                                             // format to read than we care to handle in this code right now
                                        deviceStats->sasStatistics.verifyTotalTimeCorrectionAlgorithmProcessed
                                            .isThresholdValid = false;
                                        break;
                                    }
                                    scsi_Threshold_Comparison(
                                        &deviceStats->sasStatistics.verifyTotalTimeCorrectionAlgorithmProcessed);
                                }
                                break;
                            case 5: // total bytes processed
                                deviceStats->sasStatistics.verifyTotalBytesProcessed.supportsNotification = true;
                                if (tempLogBuf[iter + 2] & BIT4)
                                {
                                    deviceStats->sasStatistics.verifyTotalBytesProcessed.isThresholdValid = true;
                                    switch (parameterLength)
                                    {
                                    case 1: // single byte
                                        deviceStats->sasStatistics.verifyTotalBytesProcessed.threshold =
                                            tempLogBuf[iter + 4];
                                        break;
                                    case 2: // word
                                        deviceStats->sasStatistics.verifyTotalBytesProcessed.threshold =
                                            M_BytesTo2ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5]);
                                        break;
                                    case 4: // double word
                                        deviceStats->sasStatistics.verifyTotalBytesProcessed.threshold =
                                            M_BytesTo4ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5],
                                                                tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
                                        break;
                                    case 8: // quad word
                                        deviceStats->sasStatistics.verifyTotalBytesProcessed.threshold =
                                            M_BytesTo8ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5],
                                                                tempLogBuf[iter + 6], tempLogBuf[iter + 7],
                                                                tempLogBuf[iter + 8], tempLogBuf[iter + 9],
                                                                tempLogBuf[iter + 10], tempLogBuf[iter + 11]);
                                        break;
                                    default: // don't bother trying to read the data since it's in a more complicated
                                             // format to read than we care to handle in this code right now
                                        deviceStats->sasStatistics.verifyTotalBytesProcessed.isThresholdValid = false;
                                        break;
                                    }
                                    scsi_Threshold_Comparison(&deviceStats->sasStatistics.verifyTotalBytesProcessed);
                                }
                                break;
                            case 6: // total uncorrected errors
                                deviceStats->sasStatistics.verifyTotalUncorrectedErrors.supportsNotification = true;
                                if (tempLogBuf[iter + 2] & BIT4)
                                {
                                    deviceStats->sasStatistics.verifyTotalUncorrectedErrors.isThresholdValid = true;
                                    switch (parameterLength)
                                    {
                                    case 1: // single byte
                                        deviceStats->sasStatistics.verifyTotalUncorrectedErrors.threshold =
                                            tempLogBuf[iter + 4];
                                        break;
                                    case 2: // word
                                        deviceStats->sasStatistics.verifyTotalUncorrectedErrors.threshold =
                                            M_BytesTo2ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5]);
                                        break;
                                    case 4: // double word
                                        deviceStats->sasStatistics.verifyTotalUncorrectedErrors.threshold =
                                            M_BytesTo4ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5],
                                                                tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
                                        break;
                                    case 8: // quad word
                                        deviceStats->sasStatistics.verifyTotalUncorrectedErrors.threshold =
                                            M_BytesTo8ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5],
                                                                tempLogBuf[iter + 6], tempLogBuf[iter + 7],
                                                                tempLogBuf[iter + 8], tempLogBuf[iter + 9],
                                                                tempLogBuf[iter + 10], tempLogBuf[iter + 11]);
                                        break;
                                    default: // don't bother trying to read the data since it's in a more complicated
                                             // format to read than we care to handle in this code right now
                                        deviceStats->sasStatistics.verifyTotalUncorrectedErrors.isThresholdValid =
                                            false;
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
                safe_memset(tempLogBuf, LEGACY_DRIVE_SEC_SIZE, 0, LEGACY_DRIVE_SEC_SIZE);
                if (SUCCESS == scsi_Log_Sense_Cmd(device, false, LPC_CUMULATIVE_VALUES, pageCode, subpageCode, 0x0000,
                                                  tempLogBuf, LEGACY_DRIVE_SEC_SIZE))
                {
                    deviceStats->sasStatistics.nonMediumErrorSupported = true;
                    uint16_t pageLength      = M_BytesTo2ByteValue(tempLogBuf[2], tempLogBuf[3]);
                    uint8_t  parameterLength = UINT8_C(0);
                    // loop through the data and gather the data from each parameter we care about getting.
                    for (uint16_t iter = UINT16_C(4); iter < pageLength && iter < LEGACY_DRIVE_SEC_SIZE;
                         iter += (C_CAST(uint16_t, parameterLength + UINT16_C(4))))
                    {
                        uint16_t parameterCode = M_BytesTo2ByteValue(tempLogBuf[iter], tempLogBuf[iter + 1]);
                        parameterLength        = tempLogBuf[iter + 3];
                        switch (parameterCode)
                        {
                        case 0: // non medium error count
                            deviceStats->sasStatistics.nonMediumErrorCount.isSupported  = true;
                            deviceStats->sasStatistics.nonMediumErrorCount.isValueValid = true;
                            deviceStats->sasStatistics.nonMediumErrorCount.thresholdNotificationEnabled =
                                tempLogBuf[iter + 2] & BIT4; // ETC bit
                            if (tempLogBuf[iter + 2] & BIT4)
                            {
                                switch ((tempLogBuf[iter + 2] & (BIT2 | BIT3)) >> 2)
                                {
                                case 3:
                                    deviceStats->sasStatistics.nonMediumErrorCount.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_GREATER;
                                    break;
                                case 2:
                                    deviceStats->sasStatistics.nonMediumErrorCount.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL;
                                    break;
                                case 1:
                                    deviceStats->sasStatistics.nonMediumErrorCount.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL;
                                    break;
                                case 0:
                                default:
                                    deviceStats->sasStatistics.nonMediumErrorCount.threshType =
                                        THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE;
                                    break;
                                }
                            }
                            switch (parameterLength)
                            {
                            case 1: // single byte
                                deviceStats->sasStatistics.nonMediumErrorCount.statisticValue = tempLogBuf[iter + 4];
                                break;
                            case 2: // word
                                deviceStats->sasStatistics.nonMediumErrorCount.statisticValue =
                                    M_BytesTo2ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5]);
                                break;
                            case 4: // double word
                                deviceStats->sasStatistics.nonMediumErrorCount.statisticValue =
                                    M_BytesTo4ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5],
                                                        tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
                                break;
                            case 8: // quad word
                                deviceStats->sasStatistics.nonMediumErrorCount.statisticValue = M_BytesTo8ByteValue(
                                    tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6],
                                    tempLogBuf[iter + 7], tempLogBuf[iter + 8], tempLogBuf[iter + 9],
                                    tempLogBuf[iter + 10], tempLogBuf[iter + 11]);
                                break;
                            default: // don't bother trying to read the data since it's in a more complicated format to
                                     // read than we care to handle in this code right now
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
                    // thresholds
                    safe_memset(tempLogBuf, LEGACY_DRIVE_SEC_SIZE, 0, LEGACY_DRIVE_SEC_SIZE);
                    if (SUCCESS == scsi_Log_Sense_Cmd(device, false, LPC_THRESHOLD_VALUES, pageCode, subpageCode,
                                                      0x0000, tempLogBuf, LEGACY_DRIVE_SEC_SIZE))
                    {
                        pageLength      = M_BytesTo2ByteValue(tempLogBuf[2], tempLogBuf[3]);
                        parameterLength = 0;
                        // loop through the data and gather the data from each parameter we care about getting.
                        for (uint16_t iter = UINT16_C(4); iter < pageLength && iter < LEGACY_DRIVE_SEC_SIZE;
                             iter += (C_CAST(uint16_t, parameterLength + UINT16_C(4))))
                        {
                            uint16_t parameterCode = M_BytesTo2ByteValue(tempLogBuf[iter], tempLogBuf[iter + 1]);
                            parameterLength        = tempLogBuf[iter + 3];
                            switch (parameterCode)
                            {
                            case 0: // non medium error count
                                deviceStats->sasStatistics.nonMediumErrorCount.supportsNotification = true;
                                if (tempLogBuf[iter + 2] & BIT4)
                                {
                                    deviceStats->sasStatistics.nonMediumErrorCount.isThresholdValid = true;
                                    switch (parameterLength)
                                    {
                                    case 1: // single byte
                                        deviceStats->sasStatistics.nonMediumErrorCount.threshold = tempLogBuf[iter + 4];
                                        break;
                                    case 2: // word
                                        deviceStats->sasStatistics.nonMediumErrorCount.threshold =
                                            M_BytesTo2ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5]);
                                        break;
                                    case 4: // double word
                                        deviceStats->sasStatistics.nonMediumErrorCount.threshold =
                                            M_BytesTo4ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5],
                                                                tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
                                        break;
                                    case 8: // quad word
                                        deviceStats->sasStatistics.nonMediumErrorCount.threshold = M_BytesTo8ByteValue(
                                            tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6],
                                            tempLogBuf[iter + 7], tempLogBuf[iter + 8], tempLogBuf[iter + 9],
                                            tempLogBuf[iter + 10], tempLogBuf[iter + 11]);
                                        break;
                                    default: // don't bother trying to read the data since it's in a more complicated
                                             // format to read than we care to handle in this code right now
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
                safe_memset(tempLogBuf, LEGACY_DRIVE_SEC_SIZE, 0, LEGACY_DRIVE_SEC_SIZE);
                if (SUCCESS == scsi_Log_Sense_Cmd(device, false, LPC_CUMULATIVE_VALUES, pageCode, subpageCode, 0x0001,
                                                  tempLogBuf, LEGACY_DRIVE_SEC_SIZE))
                {
                    deviceStats->sasStatistics.formatStatusSupported = true;
                    uint16_t pageLength      = M_BytesTo2ByteValue(tempLogBuf[2], tempLogBuf[3]);
                    uint8_t  parameterLength = UINT8_C(0);
                    // loop through the data and gather the data from each parameter we care about getting.
                    for (uint16_t iter = UINT16_C(4); iter < pageLength && iter < LEGACY_DRIVE_SEC_SIZE;
                         iter += (C_CAST(uint16_t, parameterLength + UINT16_C(4))))
                    {
                        uint16_t parameterCode = M_BytesTo2ByteValue(tempLogBuf[iter], tempLogBuf[iter + 1]);
                        parameterLength        = tempLogBuf[iter + 3];
                        switch (parameterCode)
                        {
                        case 0: // format data out
                            break;
                        case 1: // grown defects during certification
                            deviceStats->sasStatistics.grownDefectsDuringCertification.isSupported  = true;
                            deviceStats->sasStatistics.grownDefectsDuringCertification.isValueValid = true;
                            deviceStats->sasStatistics.grownDefectsDuringCertification.thresholdNotificationEnabled =
                                tempLogBuf[iter + 2] & BIT4; // ETC bit
                            if (tempLogBuf[iter + 2] & BIT4)
                            {
                                switch ((tempLogBuf[iter + 2] & (BIT2 | BIT3)) >> 2)
                                {
                                case 3:
                                    deviceStats->sasStatistics.grownDefectsDuringCertification.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_GREATER;
                                    break;
                                case 2:
                                    deviceStats->sasStatistics.grownDefectsDuringCertification.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL;
                                    break;
                                case 1:
                                    deviceStats->sasStatistics.grownDefectsDuringCertification.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL;
                                    break;
                                case 0:
                                default:
                                    deviceStats->sasStatistics.grownDefectsDuringCertification.threshType =
                                        THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE;
                                    break;
                                }
                            }
                            switch (parameterLength)
                            {
                            case 1: // single byte
                                deviceStats->sasStatistics.grownDefectsDuringCertification.statisticValue =
                                    tempLogBuf[iter + 4];
                                if (deviceStats->sasStatistics.grownDefectsDuringCertification.statisticValue ==
                                    UINT8_MAX)
                                {
                                    deviceStats->sasStatistics.grownDefectsDuringCertification.isValueValid = false;
                                }
                                break;
                            case 2: // word
                                deviceStats->sasStatistics.grownDefectsDuringCertification.statisticValue =
                                    M_BytesTo2ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5]);
                                if (deviceStats->sasStatistics.grownDefectsDuringCertification.statisticValue ==
                                    UINT16_MAX)
                                {
                                    deviceStats->sasStatistics.grownDefectsDuringCertification.isValueValid = false;
                                }
                                break;
                            case 4: // double word
                                deviceStats->sasStatistics.grownDefectsDuringCertification.statisticValue =
                                    M_BytesTo4ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5],
                                                        tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
                                if (deviceStats->sasStatistics.grownDefectsDuringCertification.statisticValue ==
                                    UINT32_MAX)
                                {
                                    deviceStats->sasStatistics.grownDefectsDuringCertification.isValueValid = false;
                                }
                                break;
                            case 8: // quad word
                                deviceStats->sasStatistics.grownDefectsDuringCertification.statisticValue =
                                    M_BytesTo8ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5],
                                                        tempLogBuf[iter + 6], tempLogBuf[iter + 7],
                                                        tempLogBuf[iter + 8], tempLogBuf[iter + 9],
                                                        tempLogBuf[iter + 10], tempLogBuf[iter + 11]);
                                if (deviceStats->sasStatistics.grownDefectsDuringCertification.statisticValue ==
                                    UINT64_MAX)
                                {
                                    deviceStats->sasStatistics.grownDefectsDuringCertification.isValueValid = false;
                                }
                                break;
                            default: // don't bother trying to read the data since it's in a more complicated format to
                                     // read than we care to handle in this code right now
                                deviceStats->sasStatistics.grownDefectsDuringCertification.isValueValid = false;
                                break;
                            }
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            break;
                        case 2: // total blocks reassigned during format
                            deviceStats->sasStatistics.totalBlocksReassignedDuringFormat.isSupported  = true;
                            deviceStats->sasStatistics.totalBlocksReassignedDuringFormat.isValueValid = true;
                            deviceStats->sasStatistics.totalBlocksReassignedDuringFormat.thresholdNotificationEnabled =
                                tempLogBuf[iter + 2] & BIT4; // ETC bit
                            if (tempLogBuf[iter + 2] & BIT4)
                            {
                                switch ((tempLogBuf[iter + 2] & (BIT2 | BIT3)) >> 2)
                                {
                                case 3:
                                    deviceStats->sasStatistics.totalBlocksReassignedDuringFormat.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_GREATER;
                                    break;
                                case 2:
                                    deviceStats->sasStatistics.totalBlocksReassignedDuringFormat.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL;
                                    break;
                                case 1:
                                    deviceStats->sasStatistics.totalBlocksReassignedDuringFormat.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL;
                                    break;
                                case 0:
                                default:
                                    deviceStats->sasStatistics.totalBlocksReassignedDuringFormat.threshType =
                                        THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE;
                                    break;
                                }
                            }
                            switch (parameterLength)
                            {
                            case 1: // single byte
                                deviceStats->sasStatistics.totalBlocksReassignedDuringFormat.statisticValue =
                                    tempLogBuf[iter + 4];
                                if (deviceStats->sasStatistics.totalBlocksReassignedDuringFormat.statisticValue ==
                                    UINT8_MAX)
                                {
                                    deviceStats->sasStatistics.totalBlocksReassignedDuringFormat.isValueValid = false;
                                }
                                break;
                            case 2: // word
                                deviceStats->sasStatistics.totalBlocksReassignedDuringFormat.statisticValue =
                                    M_BytesTo2ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5]);
                                if (deviceStats->sasStatistics.totalBlocksReassignedDuringFormat.statisticValue ==
                                    UINT16_MAX)
                                {
                                    deviceStats->sasStatistics.totalBlocksReassignedDuringFormat.isValueValid = false;
                                }
                                break;
                            case 4: // double word
                                deviceStats->sasStatistics.totalBlocksReassignedDuringFormat.statisticValue =
                                    M_BytesTo4ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5],
                                                        tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
                                if (deviceStats->sasStatistics.totalBlocksReassignedDuringFormat.statisticValue ==
                                    UINT32_MAX)
                                {
                                    deviceStats->sasStatistics.totalBlocksReassignedDuringFormat.isValueValid = false;
                                }
                                break;
                            case 8: // quad word
                                deviceStats->sasStatistics.totalBlocksReassignedDuringFormat.statisticValue =
                                    M_BytesTo8ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5],
                                                        tempLogBuf[iter + 6], tempLogBuf[iter + 7],
                                                        tempLogBuf[iter + 8], tempLogBuf[iter + 9],
                                                        tempLogBuf[iter + 10], tempLogBuf[iter + 11]);
                                if (deviceStats->sasStatistics.totalBlocksReassignedDuringFormat.statisticValue ==
                                    UINT64_MAX)
                                {
                                    deviceStats->sasStatistics.totalBlocksReassignedDuringFormat.isValueValid = false;
                                }
                                break;
                            default: // don't bother trying to read the data since it's in a more complicated format to
                                     // read than we care to handle in this code right now
                                deviceStats->sasStatistics.totalBlocksReassignedDuringFormat.isValueValid = false;
                                break;
                            }
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            break;
                        case 3: // total new blocks reassigned
                            deviceStats->sasStatistics.totalNewBlocksReassigned.isSupported  = true;
                            deviceStats->sasStatistics.totalNewBlocksReassigned.isValueValid = true;
                            deviceStats->sasStatistics.totalNewBlocksReassigned.thresholdNotificationEnabled =
                                tempLogBuf[iter + 2] & BIT4; // ETC bit
                            if (tempLogBuf[iter + 2] & BIT4)
                            {
                                switch ((tempLogBuf[iter + 2] & (BIT2 | BIT3)) >> 2)
                                {
                                case 3:
                                    deviceStats->sasStatistics.totalNewBlocksReassigned.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_GREATER;
                                    break;
                                case 2:
                                    deviceStats->sasStatistics.totalNewBlocksReassigned.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL;
                                    break;
                                case 1:
                                    deviceStats->sasStatistics.totalNewBlocksReassigned.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL;
                                    break;
                                case 0:
                                default:
                                    deviceStats->sasStatistics.totalNewBlocksReassigned.threshType =
                                        THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE;
                                    break;
                                }
                            }
                            switch (parameterLength)
                            {
                            case 1: // single byte
                                deviceStats->sasStatistics.totalNewBlocksReassigned.statisticValue =
                                    tempLogBuf[iter + 4];
                                if (deviceStats->sasStatistics.totalNewBlocksReassigned.statisticValue == UINT8_MAX)
                                {
                                    deviceStats->sasStatistics.totalNewBlocksReassigned.isValueValid = false;
                                }
                                break;
                            case 2: // word
                                deviceStats->sasStatistics.totalNewBlocksReassigned.statisticValue =
                                    M_BytesTo2ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5]);
                                if (deviceStats->sasStatistics.totalNewBlocksReassigned.statisticValue == UINT16_MAX)
                                {
                                    deviceStats->sasStatistics.totalNewBlocksReassigned.isValueValid = false;
                                }
                                break;
                            case 4: // double word
                                deviceStats->sasStatistics.totalNewBlocksReassigned.statisticValue =
                                    M_BytesTo4ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5],
                                                        tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
                                if (deviceStats->sasStatistics.totalNewBlocksReassigned.statisticValue == UINT32_MAX)
                                {
                                    deviceStats->sasStatistics.totalNewBlocksReassigned.isValueValid = false;
                                }
                                break;
                            case 8: // quad word
                                deviceStats->sasStatistics.totalNewBlocksReassigned.statisticValue =
                                    M_BytesTo8ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5],
                                                        tempLogBuf[iter + 6], tempLogBuf[iter + 7],
                                                        tempLogBuf[iter + 8], tempLogBuf[iter + 9],
                                                        tempLogBuf[iter + 10], tempLogBuf[iter + 11]);
                                if (deviceStats->sasStatistics.totalNewBlocksReassigned.statisticValue == UINT64_MAX)
                                {
                                    deviceStats->sasStatistics.totalNewBlocksReassigned.isValueValid = false;
                                }
                                break;
                            default: // don't bother trying to read the data since it's in a more complicated format to
                                     // read than we care to handle in this code right now
                                deviceStats->sasStatistics.totalNewBlocksReassigned.isValueValid = false;
                                break;
                            }
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            break;
                        case 4: // power on minutes since last format
                            deviceStats->sasStatistics.powerOnMinutesSinceFormat.isSupported  = true;
                            deviceStats->sasStatistics.powerOnMinutesSinceFormat.isValueValid = true;
                            deviceStats->sasStatistics.powerOnMinutesSinceFormat.thresholdNotificationEnabled =
                                tempLogBuf[iter + 2] & BIT4; // ETC bit
                            if (tempLogBuf[iter + 2] & BIT4)
                            {
                                switch ((tempLogBuf[iter + 2] & (BIT2 | BIT3)) >> 2)
                                {
                                case 3:
                                    deviceStats->sasStatistics.powerOnMinutesSinceFormat.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_GREATER;
                                    break;
                                case 2:
                                    deviceStats->sasStatistics.powerOnMinutesSinceFormat.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL;
                                    break;
                                case 1:
                                    deviceStats->sasStatistics.powerOnMinutesSinceFormat.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL;
                                    break;
                                case 0:
                                default:
                                    deviceStats->sasStatistics.powerOnMinutesSinceFormat.threshType =
                                        THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE;
                                    break;
                                }
                            }
                            switch (parameterLength)
                            {
                            case 1: // single byte
                                deviceStats->sasStatistics.powerOnMinutesSinceFormat.statisticValue =
                                    tempLogBuf[iter + 4];
                                if (deviceStats->sasStatistics.powerOnMinutesSinceFormat.statisticValue == UINT8_MAX)
                                {
                                    deviceStats->sasStatistics.powerOnMinutesSinceFormat.isValueValid = false;
                                }
                                break;
                            case 2: // word
                                deviceStats->sasStatistics.powerOnMinutesSinceFormat.statisticValue =
                                    M_BytesTo2ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5]);
                                if (deviceStats->sasStatistics.powerOnMinutesSinceFormat.statisticValue == UINT16_MAX)
                                {
                                    deviceStats->sasStatistics.powerOnMinutesSinceFormat.isValueValid = false;
                                }
                                break;
                            case 4: // double word
                                deviceStats->sasStatistics.powerOnMinutesSinceFormat.statisticValue =
                                    M_BytesTo4ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5],
                                                        tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
                                if (deviceStats->sasStatistics.powerOnMinutesSinceFormat.statisticValue == UINT32_MAX)
                                {
                                    deviceStats->sasStatistics.powerOnMinutesSinceFormat.isValueValid = false;
                                }
                                break;
                            case 8: // quad word
                                deviceStats->sasStatistics.powerOnMinutesSinceFormat.statisticValue =
                                    M_BytesTo8ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5],
                                                        tempLogBuf[iter + 6], tempLogBuf[iter + 7],
                                                        tempLogBuf[iter + 8], tempLogBuf[iter + 9],
                                                        tempLogBuf[iter + 10], tempLogBuf[iter + 11]);
                                if (deviceStats->sasStatistics.powerOnMinutesSinceFormat.statisticValue == UINT64_MAX)
                                {
                                    deviceStats->sasStatistics.powerOnMinutesSinceFormat.isValueValid = false;
                                }
                                break;
                            default: // don't bother trying to read the data since it's in a more complicated format to
                                     // read than we care to handle in this code right now
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
                    // thresholds
                    safe_memset(tempLogBuf, LEGACY_DRIVE_SEC_SIZE, 0, LEGACY_DRIVE_SEC_SIZE);
                    if (SUCCESS == scsi_Log_Sense_Cmd(device, false, LPC_THRESHOLD_VALUES, pageCode, subpageCode,
                                                      0x0001, tempLogBuf, LEGACY_DRIVE_SEC_SIZE))
                    {
                        pageLength      = M_BytesTo2ByteValue(tempLogBuf[2], tempLogBuf[3]);
                        parameterLength = 0;
                        // loop through the data and gather the data from each parameter we care about getting.
                        for (uint16_t iter = UINT16_C(4); iter < pageLength && iter < LEGACY_DRIVE_SEC_SIZE;
                             iter += (C_CAST(uint16_t, parameterLength + UINT16_C(4))))
                        {
                            uint16_t parameterCode = M_BytesTo2ByteValue(tempLogBuf[iter], tempLogBuf[iter + 1]);
                            parameterLength        = tempLogBuf[iter + 3];
                            switch (parameterCode)
                            {
                            case 0: // format data out
                                break;
                            case 1: // grown defects during certification
                                deviceStats->sasStatistics.grownDefectsDuringCertification.supportsNotification = true;
                                if (tempLogBuf[iter + 2] & BIT4)
                                {
                                    deviceStats->sasStatistics.grownDefectsDuringCertification.isThresholdValid = true;
                                    switch (parameterLength)
                                    {
                                    case 1: // single byte
                                        deviceStats->sasStatistics.grownDefectsDuringCertification.threshold =
                                            tempLogBuf[iter + 4];
                                        break;
                                    case 2: // word
                                        deviceStats->sasStatistics.grownDefectsDuringCertification.threshold =
                                            M_BytesTo2ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5]);
                                        break;
                                    case 4: // double word
                                        deviceStats->sasStatistics.grownDefectsDuringCertification.threshold =
                                            M_BytesTo4ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5],
                                                                tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
                                        break;
                                    case 8: // quad word
                                        deviceStats->sasStatistics.grownDefectsDuringCertification.threshold =
                                            M_BytesTo8ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5],
                                                                tempLogBuf[iter + 6], tempLogBuf[iter + 7],
                                                                tempLogBuf[iter + 8], tempLogBuf[iter + 9],
                                                                tempLogBuf[iter + 10], tempLogBuf[iter + 11]);
                                        break;
                                    default: // don't bother trying to read the data since it's in a more complicated
                                             // format to read than we care to handle in this code right now
                                        deviceStats->sasStatistics.grownDefectsDuringCertification.isThresholdValid =
                                            false;
                                        break;
                                    }
                                    scsi_Threshold_Comparison(
                                        &deviceStats->sasStatistics.grownDefectsDuringCertification);
                                }
                                break;
                            case 2: // total blocks reassigned during format
                                deviceStats->sasStatistics.totalBlocksReassignedDuringFormat.supportsNotification =
                                    true;
                                if (tempLogBuf[iter + 2] & BIT4)
                                {
                                    deviceStats->sasStatistics.totalBlocksReassignedDuringFormat.isThresholdValid =
                                        true;
                                    switch (parameterLength)
                                    {
                                    case 1: // single byte
                                        deviceStats->sasStatistics.totalBlocksReassignedDuringFormat.threshold =
                                            tempLogBuf[iter + 4];
                                        break;
                                    case 2: // word
                                        deviceStats->sasStatistics.totalBlocksReassignedDuringFormat.threshold =
                                            M_BytesTo2ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5]);
                                        break;
                                    case 4: // double word
                                        deviceStats->sasStatistics.totalBlocksReassignedDuringFormat.threshold =
                                            M_BytesTo4ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5],
                                                                tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
                                        break;
                                    case 8: // quad word
                                        deviceStats->sasStatistics.totalBlocksReassignedDuringFormat.threshold =
                                            M_BytesTo8ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5],
                                                                tempLogBuf[iter + 6], tempLogBuf[iter + 7],
                                                                tempLogBuf[iter + 8], tempLogBuf[iter + 9],
                                                                tempLogBuf[iter + 10], tempLogBuf[iter + 11]);
                                        break;
                                    default: // don't bother trying to read the data since it's in a more complicated
                                             // format to read than we care to handle in this code right now
                                        deviceStats->sasStatistics.totalBlocksReassignedDuringFormat.isThresholdValid =
                                            false;
                                        break;
                                    }
                                    scsi_Threshold_Comparison(
                                        &deviceStats->sasStatistics.totalBlocksReassignedDuringFormat);
                                }
                                break;
                            case 3: // total new blocks reassigned
                                deviceStats->sasStatistics.totalNewBlocksReassigned.supportsNotification = true;
                                if (tempLogBuf[iter + 2] & BIT4)
                                {
                                    deviceStats->sasStatistics.totalNewBlocksReassigned.isThresholdValid = true;
                                    switch (parameterLength)
                                    {
                                    case 1: // single byte
                                        deviceStats->sasStatistics.totalNewBlocksReassigned.threshold =
                                            tempLogBuf[iter + 4];
                                        break;
                                    case 2: // word
                                        deviceStats->sasStatistics.totalNewBlocksReassigned.threshold =
                                            M_BytesTo2ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5]);
                                        break;
                                    case 4: // double word
                                        deviceStats->sasStatistics.totalNewBlocksReassigned.threshold =
                                            M_BytesTo4ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5],
                                                                tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
                                        break;
                                    case 8: // quad word
                                        deviceStats->sasStatistics.totalNewBlocksReassigned.threshold =
                                            M_BytesTo8ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5],
                                                                tempLogBuf[iter + 6], tempLogBuf[iter + 7],
                                                                tempLogBuf[iter + 8], tempLogBuf[iter + 9],
                                                                tempLogBuf[iter + 10], tempLogBuf[iter + 11]);
                                        break;
                                    default: // don't bother trying to read the data since it's in a more complicated
                                             // format to read than we care to handle in this code right now
                                        deviceStats->sasStatistics.totalNewBlocksReassigned.isThresholdValid = false;
                                        break;
                                    }
                                    scsi_Threshold_Comparison(&deviceStats->sasStatistics.totalNewBlocksReassigned);
                                }
                                break;
                            case 4: // power on minutes since format
                                deviceStats->sasStatistics.powerOnMinutesSinceFormat.supportsNotification = true;
                                if (tempLogBuf[iter + 2] & BIT4)
                                {
                                    deviceStats->sasStatistics.powerOnMinutesSinceFormat.isThresholdValid = true;
                                    switch (parameterLength)
                                    {
                                    case 1: // single byte
                                        deviceStats->sasStatistics.powerOnMinutesSinceFormat.threshold =
                                            tempLogBuf[iter + 4];
                                        break;
                                    case 2: // word
                                        deviceStats->sasStatistics.powerOnMinutesSinceFormat.threshold =
                                            M_BytesTo2ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5]);
                                        break;
                                    case 4: // double word
                                        deviceStats->sasStatistics.powerOnMinutesSinceFormat.threshold =
                                            M_BytesTo4ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5],
                                                                tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
                                        break;
                                    case 8: // quad word
                                        deviceStats->sasStatistics.powerOnMinutesSinceFormat.threshold =
                                            M_BytesTo8ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5],
                                                                tempLogBuf[iter + 6], tempLogBuf[iter + 7],
                                                                tempLogBuf[iter + 8], tempLogBuf[iter + 9],
                                                                tempLogBuf[iter + 10], tempLogBuf[iter + 11]);
                                        break;
                                    default: // don't bother trying to read the data since it's in a more complicated
                                             // format to read than we care to handle in this code right now
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
                safe_memset(tempLogBuf, LEGACY_DRIVE_SEC_SIZE, 0, LEGACY_DRIVE_SEC_SIZE);
                if (SUCCESS == scsi_Log_Sense_Cmd(device, false, LPC_CUMULATIVE_VALUES, pageCode, subpageCode, 0x0001,
                                                  tempLogBuf, LEGACY_DRIVE_SEC_SIZE))
                {
                    deviceStats->sasStatistics.logicalBlockProvisioningSupported = true;
                    uint16_t pageLength      = M_BytesTo2ByteValue(tempLogBuf[2], tempLogBuf[3]);
                    uint8_t  parameterLength = UINT8_C(0);
                    // loop through the data and gather the data from each parameter we care about getting.
                    for (uint16_t iter = UINT16_C(4); iter < pageLength && iter < LEGACY_DRIVE_SEC_SIZE;
                         iter += (C_CAST(uint16_t, parameterLength + UINT16_C(4))))
                    {
                        uint16_t parameterCode = M_BytesTo2ByteValue(tempLogBuf[iter], tempLogBuf[iter + 1]);
                        parameterLength        = tempLogBuf[iter + 3];
                        switch (parameterCode)
                        {
                        case 1: // available LBA Mapping Resource Count
                            deviceStats->sasStatistics.availableLBAMappingresourceCount.isSupported  = true;
                            deviceStats->sasStatistics.availableLBAMappingresourceCount.isValueValid = true;
                            deviceStats->sasStatistics.availableLBAMappingresourceCount.thresholdNotificationEnabled =
                                tempLogBuf[iter + 2] & BIT4; // ETC bit
                            if (tempLogBuf[iter + 2] & BIT4)
                            {
                                switch ((tempLogBuf[iter + 2] & (BIT2 | BIT3)) >> 2)
                                {
                                case 3:
                                    deviceStats->sasStatistics.availableLBAMappingresourceCount.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_GREATER;
                                    break;
                                case 2:
                                    deviceStats->sasStatistics.availableLBAMappingresourceCount.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL;
                                    break;
                                case 1:
                                    deviceStats->sasStatistics.availableLBAMappingresourceCount.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL;
                                    break;
                                case 0:
                                default:
                                    deviceStats->sasStatistics.availableLBAMappingresourceCount.threshType =
                                        THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE;
                                    break;
                                }
                            }
                            deviceStats->sasStatistics.availableLBAMappingresourceCount.statisticValue =
                                M_BytesTo4ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6],
                                                    tempLogBuf[iter + 7]);
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            break;
                        case 2: // used LBA mapping resource count
                            deviceStats->sasStatistics.usedLBAMappingResourceCount.isSupported  = true;
                            deviceStats->sasStatistics.usedLBAMappingResourceCount.isValueValid = true;
                            deviceStats->sasStatistics.usedLBAMappingResourceCount.thresholdNotificationEnabled =
                                tempLogBuf[iter + 2] & BIT4; // ETC bit
                            if (tempLogBuf[iter + 2] & BIT4)
                            {
                                switch ((tempLogBuf[iter + 2] & (BIT2 | BIT3)) >> 2)
                                {
                                case 3:
                                    deviceStats->sasStatistics.usedLBAMappingResourceCount.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_GREATER;
                                    break;
                                case 2:
                                    deviceStats->sasStatistics.usedLBAMappingResourceCount.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL;
                                    break;
                                case 1:
                                    deviceStats->sasStatistics.usedLBAMappingResourceCount.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL;
                                    break;
                                case 0:
                                default:
                                    deviceStats->sasStatistics.usedLBAMappingResourceCount.threshType =
                                        THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE;
                                    break;
                                }
                            }
                            deviceStats->sasStatistics.usedLBAMappingResourceCount.statisticValue = M_BytesTo4ByteValue(
                                tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            break;
                        case 3: // available provisioning resource percentage
                            deviceStats->sasStatistics.availableProvisioningResourcePercentage.isSupported  = true;
                            deviceStats->sasStatistics.availableProvisioningResourcePercentage.isValueValid = true;
                            deviceStats->sasStatistics.availableProvisioningResourcePercentage
                                .thresholdNotificationEnabled = tempLogBuf[iter + 2] & BIT4; // ETC bit
                            if (tempLogBuf[iter + 2] & BIT4)
                            {
                                switch ((tempLogBuf[iter + 2] & (BIT2 | BIT3)) >> 2)
                                {
                                case 3:
                                    deviceStats->sasStatistics.availableProvisioningResourcePercentage.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_GREATER;
                                    break;
                                case 2:
                                    deviceStats->sasStatistics.availableProvisioningResourcePercentage.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL;
                                    break;
                                case 1:
                                    deviceStats->sasStatistics.availableProvisioningResourcePercentage.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL;
                                    break;
                                case 0:
                                default:
                                    deviceStats->sasStatistics.availableProvisioningResourcePercentage.threshType =
                                        THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE;
                                    break;
                                }
                            }
                            deviceStats->sasStatistics.availableProvisioningResourcePercentage.statisticValue =
                                M_BytesTo2ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5]);
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            break;
                        case 0x100: // de-duplicated LBA resource count
                            deviceStats->sasStatistics.deduplicatedLBAResourceCount.isSupported  = true;
                            deviceStats->sasStatistics.deduplicatedLBAResourceCount.isValueValid = true;
                            deviceStats->sasStatistics.deduplicatedLBAResourceCount.thresholdNotificationEnabled =
                                tempLogBuf[iter + 2] & BIT4; // ETC bit
                            if (tempLogBuf[iter + 2] & BIT4)
                            {
                                switch ((tempLogBuf[iter + 2] & (BIT2 | BIT3)) >> 2)
                                {
                                case 3:
                                    deviceStats->sasStatistics.deduplicatedLBAResourceCount.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_GREATER;
                                    break;
                                case 2:
                                    deviceStats->sasStatistics.deduplicatedLBAResourceCount.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL;
                                    break;
                                case 1:
                                    deviceStats->sasStatistics.deduplicatedLBAResourceCount.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL;
                                    break;
                                case 0:
                                default:
                                    deviceStats->sasStatistics.deduplicatedLBAResourceCount.threshType =
                                        THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE;
                                    break;
                                }
                            }
                            deviceStats->sasStatistics.deduplicatedLBAResourceCount.statisticValue =
                                M_BytesTo4ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6],
                                                    tempLogBuf[iter + 7]);
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            break;
                        case 0x101: // compressed LBA resource count
                            deviceStats->sasStatistics.compressedLBAResourceCount.isSupported  = true;
                            deviceStats->sasStatistics.compressedLBAResourceCount.isValueValid = true;
                            deviceStats->sasStatistics.compressedLBAResourceCount.thresholdNotificationEnabled =
                                tempLogBuf[iter + 2] & BIT4; // ETC bit
                            if (tempLogBuf[iter + 2] & BIT4)
                            {
                                switch ((tempLogBuf[iter + 2] & (BIT2 | BIT3)) >> 2)
                                {
                                case 3:
                                    deviceStats->sasStatistics.compressedLBAResourceCount.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_GREATER;
                                    break;
                                case 2:
                                    deviceStats->sasStatistics.compressedLBAResourceCount.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL;
                                    break;
                                case 1:
                                    deviceStats->sasStatistics.compressedLBAResourceCount.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL;
                                    break;
                                case 0:
                                default:
                                    deviceStats->sasStatistics.compressedLBAResourceCount.threshType =
                                        THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE;
                                    break;
                                }
                            }
                            deviceStats->sasStatistics.compressedLBAResourceCount.statisticValue = M_BytesTo4ByteValue(
                                tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            break;
                        case 0x102: // total efficiency LBA resource count
                            deviceStats->sasStatistics.totalEfficiencyLBAResourceCount.isSupported  = true;
                            deviceStats->sasStatistics.totalEfficiencyLBAResourceCount.isValueValid = true;
                            deviceStats->sasStatistics.totalEfficiencyLBAResourceCount.thresholdNotificationEnabled =
                                tempLogBuf[iter + 2] & BIT4; // ETC bit
                            if (tempLogBuf[iter + 2] & BIT4)
                            {
                                switch ((tempLogBuf[iter + 2] & (BIT2 | BIT3)) >> 2)
                                {
                                case 3:
                                    deviceStats->sasStatistics.totalEfficiencyLBAResourceCount.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_GREATER;
                                    break;
                                case 2:
                                    deviceStats->sasStatistics.totalEfficiencyLBAResourceCount.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL;
                                    break;
                                case 1:
                                    deviceStats->sasStatistics.totalEfficiencyLBAResourceCount.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL;
                                    break;
                                case 0:
                                default:
                                    deviceStats->sasStatistics.totalEfficiencyLBAResourceCount.threshType =
                                        THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE;
                                    break;
                                }
                            }
                            deviceStats->sasStatistics.totalEfficiencyLBAResourceCount.statisticValue =
                                M_BytesTo4ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6],
                                                    tempLogBuf[iter + 7]);
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
                    // thresholds
                    safe_memset(tempLogBuf, LEGACY_DRIVE_SEC_SIZE, 0, LEGACY_DRIVE_SEC_SIZE);
                    if (SUCCESS == scsi_Log_Sense_Cmd(device, false, LPC_THRESHOLD_VALUES, pageCode, subpageCode,
                                                      0x0001, tempLogBuf, LEGACY_DRIVE_SEC_SIZE))
                    {
                        pageLength      = M_BytesTo2ByteValue(tempLogBuf[2], tempLogBuf[3]);
                        parameterLength = 0;
                        // loop through the data and gather the data from each parameter we care about getting.
                        for (uint16_t iter = UINT16_C(4); iter < pageLength && iter < LEGACY_DRIVE_SEC_SIZE;
                             iter += (C_CAST(uint16_t, parameterLength + UINT16_C(4))))
                        {
                            uint16_t parameterCode = M_BytesTo2ByteValue(tempLogBuf[iter], tempLogBuf[iter + 1]);
                            parameterLength        = tempLogBuf[iter + 3];
                            switch (parameterCode)
                            {
                            case 1: // available LBA mapping resource count
                                deviceStats->sasStatistics.availableLBAMappingresourceCount.supportsNotification = true;
                                if (tempLogBuf[iter + 2] & BIT4)
                                {
                                    deviceStats->sasStatistics.availableLBAMappingresourceCount.isThresholdValid = true;
                                    deviceStats->sasStatistics.availableLBAMappingresourceCount.threshold =
                                        M_BytesTo4ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5],
                                                            tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
                                    scsi_Threshold_Comparison(
                                        &deviceStats->sasStatistics.availableLBAMappingresourceCount);
                                }
                                break;
                            case 2: // used LBA mapping resource count
                                deviceStats->sasStatistics.usedLBAMappingResourceCount.supportsNotification = true;
                                if (tempLogBuf[iter + 2] & BIT4)
                                {
                                    deviceStats->sasStatistics.usedLBAMappingResourceCount.isThresholdValid = true;
                                    deviceStats->sasStatistics.usedLBAMappingResourceCount.threshold =
                                        M_BytesTo4ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5],
                                                            tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
                                    scsi_Threshold_Comparison(&deviceStats->sasStatistics.usedLBAMappingResourceCount);
                                }
                                break;
                            case 3: // available provisinging resource percentage
                                deviceStats->sasStatistics.availableProvisioningResourcePercentage
                                    .supportsNotification = true;
                                if (tempLogBuf[iter + 2] & BIT4)
                                {
                                    deviceStats->sasStatistics.availableProvisioningResourcePercentage
                                        .isThresholdValid = true;
                                    deviceStats->sasStatistics.availableProvisioningResourcePercentage.threshold =
                                        M_BytesTo4ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5],
                                                            tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
                                    scsi_Threshold_Comparison(
                                        &deviceStats->sasStatistics.availableProvisioningResourcePercentage);
                                }
                                break;
                            case 0x100: // De-duplicated LBA resource count
                                deviceStats->sasStatistics.deduplicatedLBAResourceCount.supportsNotification = true;
                                if (tempLogBuf[iter + 2] & BIT4)
                                {
                                    deviceStats->sasStatistics.deduplicatedLBAResourceCount.isThresholdValid = true;
                                    deviceStats->sasStatistics.deduplicatedLBAResourceCount.threshold =
                                        M_BytesTo4ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5],
                                                            tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
                                    scsi_Threshold_Comparison(&deviceStats->sasStatistics.deduplicatedLBAResourceCount);
                                }
                                break;
                            case 0x101: // compressed LBA resource count
                                deviceStats->sasStatistics.compressedLBAResourceCount.supportsNotification = true;
                                if (tempLogBuf[iter + 2] & BIT4)
                                {
                                    deviceStats->sasStatistics.compressedLBAResourceCount.isThresholdValid = true;
                                    deviceStats->sasStatistics.compressedLBAResourceCount.threshold =
                                        M_BytesTo4ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5],
                                                            tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
                                    scsi_Threshold_Comparison(&deviceStats->sasStatistics.compressedLBAResourceCount);
                                }
                                break;
                            case 0x102: // total efficiency LBA resource count
                                deviceStats->sasStatistics.totalEfficiencyLBAResourceCount.supportsNotification = true;
                                if (tempLogBuf[iter + 2] & BIT4)
                                {
                                    deviceStats->sasStatistics.totalEfficiencyLBAResourceCount.isThresholdValid = true;
                                    deviceStats->sasStatistics.totalEfficiencyLBAResourceCount.threshold =
                                        M_BytesTo4ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5],
                                                            tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
                                    scsi_Threshold_Comparison(
                                        &deviceStats->sasStatistics.totalEfficiencyLBAResourceCount);
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
        case LP_TEMPERATURE: // also environmental reporting
            switch (subpageCode)
            {
            case 0: // temperature
                safe_memset(tempLogBuf, LEGACY_DRIVE_SEC_SIZE, 0, LEGACY_DRIVE_SEC_SIZE);
                if (SUCCESS == scsi_Log_Sense_Cmd(device, false, LPC_CUMULATIVE_VALUES, pageCode, subpageCode, 0x0000,
                                                  tempLogBuf, LEGACY_DRIVE_SEC_SIZE))
                {
                    deviceStats->sasStatistics.temperatureSupported = true;
                    uint16_t pageLength                             = M_BytesTo2ByteValue(tempLogBuf[2], tempLogBuf[3]);
                    uint8_t  parameterLength                        = UINT8_C(0);
                    // loop through the data and gather the data from each parameter we care about getting.
                    for (uint16_t iter = UINT16_C(4); iter < pageLength && iter < LEGACY_DRIVE_SEC_SIZE;
                         iter += (C_CAST(uint16_t, parameterLength + UINT16_C(4))))
                    {
                        uint16_t parameterCode = M_BytesTo2ByteValue(tempLogBuf[iter], tempLogBuf[iter + 1]);
                        parameterLength        = tempLogBuf[iter + 3];
                        switch (parameterCode)
                        {
                        case 0: // temperature
                            deviceStats->sasStatistics.temperature.isSupported  = true;
                            deviceStats->sasStatistics.temperature.isValueValid = true;
                            deviceStats->sasStatistics.temperature.thresholdNotificationEnabled =
                                tempLogBuf[iter + 2] & BIT4; // ETC bit
                            if (tempLogBuf[iter + 2] & BIT4)
                            {
                                switch ((tempLogBuf[iter + 2] & (BIT2 | BIT3)) >> 2)
                                {
                                case 3:
                                    deviceStats->sasStatistics.temperature.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_GREATER;
                                    break;
                                case 2:
                                    deviceStats->sasStatistics.temperature.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL;
                                    break;
                                case 1:
                                    deviceStats->sasStatistics.temperature.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL;
                                    break;
                                case 0:
                                default:
                                    deviceStats->sasStatistics.temperature.threshType =
                                        THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE;
                                    break;
                                }
                            }
                            deviceStats->sasStatistics.temperature.statisticValue = tempLogBuf[iter + 5];
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            break;
                        case 1: // reference temperature
                            deviceStats->sasStatistics.referenceTemperature.isSupported  = true;
                            deviceStats->sasStatistics.referenceTemperature.isValueValid = true;
                            deviceStats->sasStatistics.referenceTemperature.thresholdNotificationEnabled =
                                tempLogBuf[iter + 2] & BIT4; // ETC bit
                            if (tempLogBuf[iter + 2] & BIT4)
                            {
                                switch ((tempLogBuf[iter + 2] & (BIT2 | BIT3)) >> 2)
                                {
                                case 3:
                                    deviceStats->sasStatistics.referenceTemperature.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_GREATER;
                                    break;
                                case 2:
                                    deviceStats->sasStatistics.referenceTemperature.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL;
                                    break;
                                case 1:
                                    deviceStats->sasStatistics.referenceTemperature.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL;
                                    break;
                                case 0:
                                default:
                                    deviceStats->sasStatistics.referenceTemperature.threshType =
                                        THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE;
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
                    // thresholds
                    safe_memset(tempLogBuf, LEGACY_DRIVE_SEC_SIZE, 0, LEGACY_DRIVE_SEC_SIZE);
                    if (SUCCESS == scsi_Log_Sense_Cmd(device, false, LPC_THRESHOLD_VALUES, pageCode, subpageCode,
                                                      0x0000, tempLogBuf, LEGACY_DRIVE_SEC_SIZE))
                    {
                        pageLength      = M_BytesTo2ByteValue(tempLogBuf[2], tempLogBuf[3]);
                        parameterLength = 0;
                        // loop through the data and gather the data from each parameter we care about getting.
                        for (uint16_t iter = UINT16_C(4); iter < pageLength && iter < LEGACY_DRIVE_SEC_SIZE;
                             iter += (C_CAST(uint16_t, parameterLength + UINT16_C(4))))
                        {
                            uint16_t parameterCode = M_BytesTo2ByteValue(tempLogBuf[iter], tempLogBuf[iter + 1]);
                            parameterLength        = tempLogBuf[iter + 3];
                            switch (parameterCode)
                            {
                            case 0: // temperature
                                deviceStats->sasStatistics.temperature.supportsNotification = true;
                                if (tempLogBuf[iter + 2] & BIT4)
                                {
                                    deviceStats->sasStatistics.temperature.isThresholdValid = true;
                                    deviceStats->sasStatistics.temperature.threshold        = tempLogBuf[iter + 5];
                                    scsi_Threshold_Comparison(&deviceStats->sasStatistics.temperature);
                                }
                                break;
                            case 1: // reference temperature
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
            case 1: // environmental reporting
                safe_memset(tempLogBuf, LEGACY_DRIVE_SEC_SIZE, 0, LEGACY_DRIVE_SEC_SIZE);
                if (SUCCESS == scsi_Log_Sense_Cmd(device, false, LPC_CUMULATIVE_VALUES, pageCode, subpageCode, 0x0000,
                                                  tempLogBuf, LEGACY_DRIVE_SEC_SIZE))
                {
                    deviceStats->sasStatistics.environmentReportingSupported = true;
                    uint16_t pageLength      = M_BytesTo2ByteValue(tempLogBuf[2], tempLogBuf[3]);
                    uint8_t  parameterLength = UINT8_C(0);
                    // loop through the data and gather the data from each parameter we care about getting.
                    for (uint16_t iter = UINT16_C(4); iter < pageLength && iter < LEGACY_DRIVE_SEC_SIZE;
                         iter += (C_CAST(uint16_t, parameterLength + UINT16_C(4))))
                    {
                        uint16_t parameterCode = M_BytesTo2ByteValue(tempLogBuf[iter], tempLogBuf[iter + 1]);
                        parameterLength        = tempLogBuf[iter + 3];
                        switch (parameterCode)
                        {
                        case 0: // temperature report. (note: parameters 0000-00FF are for each temperature location
                                // reported...we are only going to care about the first one right now...)-TJE
                            if (tempLogBuf[iter + 2] & BIT4)
                            {
                                deviceStats->sasStatistics.currentTemperature.thresholdNotificationEnabled =
                                    true; // ETC bit
                                deviceStats->sasStatistics.lifetimeMaximumTemperature.thresholdNotificationEnabled =
                                    true; // ETC bit
                                deviceStats->sasStatistics.lifetimeMinimumTemperature.thresholdNotificationEnabled =
                                    true; // ETC bit
                                deviceStats->sasStatistics.maximumTemperatureSincePowerOn.thresholdNotificationEnabled =
                                    true; // ETC bit
                                deviceStats->sasStatistics.minimumTemperatureSincePowerOn.thresholdNotificationEnabled =
                                    true; // ETC bit
                                switch ((tempLogBuf[iter + 2] & (BIT2 | BIT3)) >> 2)
                                {
                                case 3:
                                    deviceStats->sasStatistics.currentTemperature.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_GREATER;
                                    deviceStats->sasStatistics.lifetimeMaximumTemperature.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_GREATER;
                                    deviceStats->sasStatistics.lifetimeMinimumTemperature.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_GREATER;
                                    deviceStats->sasStatistics.maximumTemperatureSincePowerOn.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_GREATER;
                                    deviceStats->sasStatistics.minimumTemperatureSincePowerOn.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_GREATER;
                                    break;
                                case 2:
                                    deviceStats->sasStatistics.currentTemperature.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL;
                                    deviceStats->sasStatistics.lifetimeMaximumTemperature.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL;
                                    deviceStats->sasStatistics.lifetimeMinimumTemperature.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL;
                                    deviceStats->sasStatistics.maximumTemperatureSincePowerOn.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL;
                                    deviceStats->sasStatistics.minimumTemperatureSincePowerOn.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL;
                                    break;
                                case 1:
                                    deviceStats->sasStatistics.currentTemperature.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL;
                                    deviceStats->sasStatistics.lifetimeMaximumTemperature.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL;
                                    deviceStats->sasStatistics.lifetimeMinimumTemperature.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL;
                                    deviceStats->sasStatistics.maximumTemperatureSincePowerOn.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL;
                                    deviceStats->sasStatistics.minimumTemperatureSincePowerOn.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL;
                                    break;
                                case 0:
                                default:
                                    deviceStats->sasStatistics.currentTemperature.threshType =
                                        THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE;
                                    deviceStats->sasStatistics.lifetimeMaximumTemperature.threshType =
                                        THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE;
                                    deviceStats->sasStatistics.lifetimeMinimumTemperature.threshType =
                                        THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE;
                                    deviceStats->sasStatistics.maximumTemperatureSincePowerOn.threshType =
                                        THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE;
                                    deviceStats->sasStatistics.minimumTemperatureSincePowerOn.threshType =
                                        THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE;
                                    break;
                                }
                            }
                            // current temperature
                            deviceStats->sasStatistics.currentTemperature.isSupported    = true;
                            deviceStats->sasStatistics.currentTemperature.isValueValid   = true;
                            deviceStats->sasStatistics.currentTemperature.statisticValue = tempLogBuf[iter + 5];
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            // lifetime max temp
                            deviceStats->sasStatistics.lifetimeMaximumTemperature.isSupported    = true;
                            deviceStats->sasStatistics.lifetimeMaximumTemperature.isValueValid   = true;
                            deviceStats->sasStatistics.lifetimeMaximumTemperature.statisticValue = tempLogBuf[iter + 6];
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            // lifetime min temp
                            deviceStats->sasStatistics.lifetimeMinimumTemperature.isSupported    = true;
                            deviceStats->sasStatistics.lifetimeMinimumTemperature.isValueValid   = true;
                            deviceStats->sasStatistics.lifetimeMinimumTemperature.statisticValue = tempLogBuf[iter + 7];
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            // max temp since PO
                            deviceStats->sasStatistics.maximumTemperatureSincePowerOn.isSupported  = true;
                            deviceStats->sasStatistics.maximumTemperatureSincePowerOn.isValueValid = true;
                            deviceStats->sasStatistics.maximumTemperatureSincePowerOn.statisticValue =
                                tempLogBuf[iter + 8];
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            // min temp since PO
                            deviceStats->sasStatistics.minimumTemperatureSincePowerOn.isSupported  = true;
                            deviceStats->sasStatistics.minimumTemperatureSincePowerOn.isValueValid = true;
                            deviceStats->sasStatistics.minimumTemperatureSincePowerOn.statisticValue =
                                tempLogBuf[iter + 9];
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            if (parameterLength > 6 && get_bit_range_uint8(tempLogBuf[iter + 4], 1, 0) == 1)
                            {
                                deviceStats->sasStatistics.maximumOtherTemperature.isSupported  = true;
                                deviceStats->sasStatistics.maximumOtherTemperature.isValueValid = true;
                                deviceStats->sasStatistics.maximumOtherTemperature.statisticValue =
                                    tempLogBuf[iter + 10];
                                ++deviceStats->sasStatistics.statisticsPopulated;
                                deviceStats->sasStatistics.minimumOtherTemperature.isSupported  = true;
                                deviceStats->sasStatistics.minimumOtherTemperature.isValueValid = true;
                                deviceStats->sasStatistics.minimumOtherTemperature.statisticValue =
                                    tempLogBuf[iter + 11];
                                ++deviceStats->sasStatistics.statisticsPopulated;
                            }
                            break;
                        case 0x100: // humidity report. (note: parameters 0100-01FF are for each humidity location
                                    // reported...we are only going to care about the first one right now...)-TJE
                            if (tempLogBuf[iter + 2] & BIT4)
                            {
                                deviceStats->sasStatistics.currentRelativeHumidity.thresholdNotificationEnabled =
                                    true; // ETC bit
                                deviceStats->sasStatistics.lifetimeMaximumRelativeHumidity
                                    .thresholdNotificationEnabled = true; // ETC bit
                                deviceStats->sasStatistics.lifetimeMinumumRelativeHumidity
                                    .thresholdNotificationEnabled = true; // ETC bit
                                deviceStats->sasStatistics.maximumRelativeHumiditySincePoweron
                                    .thresholdNotificationEnabled = true; // ETC bit
                                deviceStats->sasStatistics.minimumRelativeHumiditySincePoweron
                                    .thresholdNotificationEnabled = true; // ETC bit
                                switch ((tempLogBuf[iter + 2] & (BIT2 | BIT3)) >> 2)
                                {
                                case 3:
                                    deviceStats->sasStatistics.currentRelativeHumidity.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_GREATER;
                                    deviceStats->sasStatistics.lifetimeMaximumRelativeHumidity.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_GREATER;
                                    deviceStats->sasStatistics.lifetimeMinumumRelativeHumidity.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_GREATER;
                                    deviceStats->sasStatistics.maximumRelativeHumiditySincePoweron.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_GREATER;
                                    deviceStats->sasStatistics.minimumRelativeHumiditySincePoweron.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_GREATER;
                                    break;
                                case 2:
                                    deviceStats->sasStatistics.currentRelativeHumidity.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL;
                                    deviceStats->sasStatistics.lifetimeMaximumRelativeHumidity.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL;
                                    deviceStats->sasStatistics.lifetimeMinumumRelativeHumidity.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL;
                                    deviceStats->sasStatistics.maximumRelativeHumiditySincePoweron.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL;
                                    deviceStats->sasStatistics.minimumRelativeHumiditySincePoweron.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL;
                                    break;
                                case 1:
                                    deviceStats->sasStatistics.currentRelativeHumidity.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL;
                                    deviceStats->sasStatistics.lifetimeMaximumRelativeHumidity.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL;
                                    deviceStats->sasStatistics.lifetimeMinumumRelativeHumidity.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL;
                                    deviceStats->sasStatistics.maximumRelativeHumiditySincePoweron.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL;
                                    deviceStats->sasStatistics.minimumRelativeHumiditySincePoweron.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL;
                                    break;
                                case 0:
                                default:
                                    deviceStats->sasStatistics.currentRelativeHumidity.threshType =
                                        THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE;
                                    deviceStats->sasStatistics.lifetimeMaximumRelativeHumidity.threshType =
                                        THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE;
                                    deviceStats->sasStatistics.lifetimeMinumumRelativeHumidity.threshType =
                                        THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE;
                                    deviceStats->sasStatistics.maximumRelativeHumiditySincePoweron.threshType =
                                        THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE;
                                    deviceStats->sasStatistics.minimumRelativeHumiditySincePoweron.threshType =
                                        THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE;
                                    break;
                                }
                            }
                            // current humidity
                            deviceStats->sasStatistics.currentRelativeHumidity.isSupported    = true;
                            deviceStats->sasStatistics.currentRelativeHumidity.isValueValid   = true;
                            deviceStats->sasStatistics.currentRelativeHumidity.statisticValue = tempLogBuf[iter + 5];
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            // lifetime max humidity
                            deviceStats->sasStatistics.lifetimeMaximumRelativeHumidity.isSupported  = true;
                            deviceStats->sasStatistics.lifetimeMaximumRelativeHumidity.isValueValid = true;
                            deviceStats->sasStatistics.lifetimeMaximumRelativeHumidity.statisticValue =
                                tempLogBuf[iter + 6];
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            // lifetime min humidity
                            deviceStats->sasStatistics.lifetimeMinumumRelativeHumidity.isSupported  = true;
                            deviceStats->sasStatistics.lifetimeMinumumRelativeHumidity.isValueValid = true;
                            deviceStats->sasStatistics.lifetimeMinumumRelativeHumidity.statisticValue =
                                tempLogBuf[iter + 7];
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            // max humidity since PO
                            deviceStats->sasStatistics.maximumRelativeHumiditySincePoweron.isSupported  = true;
                            deviceStats->sasStatistics.maximumRelativeHumiditySincePoweron.isValueValid = true;
                            deviceStats->sasStatistics.maximumRelativeHumiditySincePoweron.statisticValue =
                                tempLogBuf[iter + 8];
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            // min humidity since PO
                            deviceStats->sasStatistics.minimumRelativeHumiditySincePoweron.isSupported  = true;
                            deviceStats->sasStatistics.minimumRelativeHumiditySincePoweron.isValueValid = true;
                            deviceStats->sasStatistics.minimumRelativeHumiditySincePoweron.statisticValue =
                                tempLogBuf[iter + 9];
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            if (parameterLength > 6 && get_bit_range_uint8(tempLogBuf[iter + 4], 1, 0) == 1)
                            {
                                deviceStats->sasStatistics.maximumOtherRelativeHumidity.isSupported  = true;
                                deviceStats->sasStatistics.maximumOtherRelativeHumidity.isValueValid = true;
                                deviceStats->sasStatistics.maximumOtherRelativeHumidity.statisticValue =
                                    tempLogBuf[iter + 10];
                                ++deviceStats->sasStatistics.statisticsPopulated;
                                deviceStats->sasStatistics.minimumOtherRelativeHumidity.isSupported  = true;
                                deviceStats->sasStatistics.minimumOtherRelativeHumidity.isValueValid = true;
                                deviceStats->sasStatistics.minimumOtherRelativeHumidity.statisticValue =
                                    tempLogBuf[iter + 11];
                                ++deviceStats->sasStatistics.statisticsPopulated;
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
                    // todo: add thresholds
                }
                break;
            case 2: // environmental limits
                safe_memset(tempLogBuf, LEGACY_DRIVE_SEC_SIZE, 0, LEGACY_DRIVE_SEC_SIZE);
                if (SUCCESS == scsi_Log_Sense_Cmd(device, false, LPC_CUMULATIVE_VALUES, pageCode, subpageCode, 0x0000,
                                                  tempLogBuf, LEGACY_DRIVE_SEC_SIZE))
                {
                    deviceStats->sasStatistics.environmentReportingSupported = true;
                    uint16_t pageLength      = M_BytesTo2ByteValue(tempLogBuf[2], tempLogBuf[3]);
                    uint8_t  parameterLength = UINT8_C(0);
                    // loop through the data and gather the data from each parameter we care about getting.
                    for (uint16_t iter = UINT16_C(4); iter < pageLength && iter < LEGACY_DRIVE_SEC_SIZE;
                         iter += (C_CAST(uint16_t, parameterLength + UINT16_C(4))))
                    {
                        uint16_t parameterCode = M_BytesTo2ByteValue(tempLogBuf[iter], tempLogBuf[iter + 1]);
                        parameterLength        = tempLogBuf[iter + 3];
                        switch (parameterCode)
                        {
                        case 0: // temperature limits. (note: parameters 0000-00FF are for each temperature location
                                // reported...we are only going to care about the first one right now...)-TJE
                            if (tempLogBuf[iter + 2] & BIT4)
                            {
                                deviceStats->sasStatistics.highCriticalTemperatureLimitTrigger
                                    .thresholdNotificationEnabled = true; // ETC bit
                                deviceStats->sasStatistics.highCriticalTemperatureLimitReset
                                    .thresholdNotificationEnabled = true; // ETC bit
                                deviceStats->sasStatistics.lowCriticalTemperatureLimitReset
                                    .thresholdNotificationEnabled = true; // ETC bit
                                deviceStats->sasStatistics.lowCriticalTemperatureLimitTrigger
                                    .thresholdNotificationEnabled = true; // ETC bit
                                deviceStats->sasStatistics.highOperatingTemperatureLimitReset
                                    .thresholdNotificationEnabled = true; // ETC bit
                                deviceStats->sasStatistics.highOperatingTemperatureLimitReset
                                    .thresholdNotificationEnabled = true; // ETC bit
                                deviceStats->sasStatistics.lowOperatingTemperatureLimitReset
                                    .thresholdNotificationEnabled = true; // ETC bit
                                deviceStats->sasStatistics.lowOperatingTemperatureLimitTrigger
                                    .thresholdNotificationEnabled = true; // ETC bit
                                switch ((tempLogBuf[iter + 2] & (BIT2 | BIT3)) >> 2)
                                {
                                case 3:
                                    deviceStats->sasStatistics.highCriticalTemperatureLimitTrigger.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_GREATER;
                                    deviceStats->sasStatistics.highCriticalTemperatureLimitReset.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_GREATER;
                                    deviceStats->sasStatistics.lowCriticalTemperatureLimitReset.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_GREATER;
                                    deviceStats->sasStatistics.lowCriticalTemperatureLimitTrigger.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_GREATER;
                                    deviceStats->sasStatistics.highOperatingTemperatureLimitTrigger.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_GREATER;
                                    deviceStats->sasStatistics.highOperatingTemperatureLimitReset.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_GREATER;
                                    deviceStats->sasStatistics.lowOperatingTemperatureLimitReset.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_GREATER;
                                    deviceStats->sasStatistics.lowOperatingTemperatureLimitTrigger.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_GREATER;
                                    break;
                                case 2:
                                    deviceStats->sasStatistics.highCriticalTemperatureLimitTrigger.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL;
                                    deviceStats->sasStatistics.highCriticalTemperatureLimitReset.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL;
                                    deviceStats->sasStatistics.lowCriticalTemperatureLimitReset.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL;
                                    deviceStats->sasStatistics.lowCriticalTemperatureLimitTrigger.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL;
                                    deviceStats->sasStatistics.highOperatingTemperatureLimitTrigger.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL;
                                    deviceStats->sasStatistics.highOperatingTemperatureLimitReset.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL;
                                    deviceStats->sasStatistics.lowOperatingTemperatureLimitReset.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL;
                                    deviceStats->sasStatistics.lowOperatingTemperatureLimitTrigger.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL;
                                    break;
                                case 1:
                                    deviceStats->sasStatistics.highCriticalTemperatureLimitTrigger.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL;
                                    deviceStats->sasStatistics.highCriticalTemperatureLimitReset.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL;
                                    deviceStats->sasStatistics.lowCriticalTemperatureLimitReset.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL;
                                    deviceStats->sasStatistics.lowCriticalTemperatureLimitTrigger.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL;
                                    deviceStats->sasStatistics.highOperatingTemperatureLimitTrigger.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL;
                                    deviceStats->sasStatistics.highOperatingTemperatureLimitReset.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL;
                                    deviceStats->sasStatistics.lowOperatingTemperatureLimitReset.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL;
                                    deviceStats->sasStatistics.lowOperatingTemperatureLimitTrigger.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL;
                                    break;
                                case 0:
                                default:
                                    deviceStats->sasStatistics.highCriticalTemperatureLimitTrigger.threshType =
                                        THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE;
                                    deviceStats->sasStatistics.highCriticalTemperatureLimitReset.threshType =
                                        THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE;
                                    deviceStats->sasStatistics.lowCriticalTemperatureLimitReset.threshType =
                                        THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE;
                                    deviceStats->sasStatistics.lowCriticalTemperatureLimitTrigger.threshType =
                                        THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE;
                                    deviceStats->sasStatistics.highOperatingTemperatureLimitTrigger.threshType =
                                        THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE;
                                    deviceStats->sasStatistics.highOperatingTemperatureLimitReset.threshType =
                                        THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE;
                                    deviceStats->sasStatistics.lowOperatingTemperatureLimitReset.threshType =
                                        THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE;
                                    deviceStats->sasStatistics.lowOperatingTemperatureLimitTrigger.threshType =
                                        THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE;
                                    break;
                                }
                            }
                            //
                            deviceStats->sasStatistics.highCriticalTemperatureLimitTrigger.isSupported  = true;
                            deviceStats->sasStatistics.highCriticalTemperatureLimitTrigger.isValueValid = true;
                            deviceStats->sasStatistics.highCriticalTemperatureLimitTrigger.statisticValue =
                                tempLogBuf[iter + 4];
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            //
                            deviceStats->sasStatistics.highCriticalTemperatureLimitReset.isSupported  = true;
                            deviceStats->sasStatistics.highCriticalTemperatureLimitReset.isValueValid = true;
                            deviceStats->sasStatistics.highCriticalTemperatureLimitReset.statisticValue =
                                tempLogBuf[iter + 5];
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            //
                            deviceStats->sasStatistics.lowCriticalTemperatureLimitReset.isSupported  = true;
                            deviceStats->sasStatistics.lowCriticalTemperatureLimitReset.isValueValid = true;
                            deviceStats->sasStatistics.lowCriticalTemperatureLimitReset.statisticValue =
                                tempLogBuf[iter + 6];
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            //
                            deviceStats->sasStatistics.lowCriticalTemperatureLimitTrigger.isSupported  = true;
                            deviceStats->sasStatistics.lowCriticalTemperatureLimitTrigger.isValueValid = true;
                            deviceStats->sasStatistics.lowCriticalTemperatureLimitTrigger.statisticValue =
                                tempLogBuf[iter + 7];
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            //
                            deviceStats->sasStatistics.highOperatingTemperatureLimitTrigger.isSupported  = true;
                            deviceStats->sasStatistics.highOperatingTemperatureLimitTrigger.isValueValid = true;
                            deviceStats->sasStatistics.highOperatingTemperatureLimitTrigger.statisticValue =
                                tempLogBuf[iter + 8];
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            //
                            deviceStats->sasStatistics.highOperatingTemperatureLimitReset.isSupported  = true;
                            deviceStats->sasStatistics.highOperatingTemperatureLimitReset.isValueValid = true;
                            deviceStats->sasStatistics.highOperatingTemperatureLimitReset.statisticValue =
                                tempLogBuf[iter + 9];
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            //
                            deviceStats->sasStatistics.lowOperatingTemperatureLimitReset.isSupported  = true;
                            deviceStats->sasStatistics.lowOperatingTemperatureLimitReset.isValueValid = true;
                            deviceStats->sasStatistics.lowOperatingTemperatureLimitReset.statisticValue =
                                tempLogBuf[iter + 10];
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            //
                            deviceStats->sasStatistics.lowOperatingTemperatureLimitTrigger.isSupported  = true;
                            deviceStats->sasStatistics.lowOperatingTemperatureLimitTrigger.isValueValid = true;
                            deviceStats->sasStatistics.lowOperatingTemperatureLimitTrigger.statisticValue =
                                tempLogBuf[iter + 11];
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            break;
                        case 0x100: // humidity limits. (note: parameters 0100-01FF are for each humidity location
                                    // reported...we are only going to care about the first one right now...)-TJE
                            //
                            deviceStats->sasStatistics.highCriticalHumidityLimitTrigger.isSupported  = true;
                            deviceStats->sasStatistics.highCriticalHumidityLimitTrigger.isValueValid = true;
                            deviceStats->sasStatistics.highCriticalHumidityLimitTrigger.statisticValue =
                                tempLogBuf[iter + 4];
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            //
                            deviceStats->sasStatistics.highCriticalHumidityLimitReset.isSupported  = true;
                            deviceStats->sasStatistics.highCriticalHumidityLimitReset.isValueValid = true;
                            deviceStats->sasStatistics.highCriticalHumidityLimitReset.statisticValue =
                                tempLogBuf[iter + 5];
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            //
                            deviceStats->sasStatistics.lowCriticalHumidityLimitReset.isSupported  = true;
                            deviceStats->sasStatistics.lowCriticalHumidityLimitReset.isValueValid = true;
                            deviceStats->sasStatistics.lowCriticalHumidityLimitReset.statisticValue =
                                tempLogBuf[iter + 6];
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            //
                            deviceStats->sasStatistics.lowCriticalHumidityLimitTrigger.isSupported  = true;
                            deviceStats->sasStatistics.lowCriticalHumidityLimitTrigger.isValueValid = true;
                            deviceStats->sasStatistics.lowCriticalHumidityLimitTrigger.statisticValue =
                                tempLogBuf[iter + 7];
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            //
                            deviceStats->sasStatistics.highOperatingHumidityLimitTrigger.isSupported  = true;
                            deviceStats->sasStatistics.highOperatingHumidityLimitTrigger.isValueValid = true;
                            deviceStats->sasStatistics.highOperatingHumidityLimitTrigger.statisticValue =
                                tempLogBuf[iter + 8];
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            //
                            deviceStats->sasStatistics.highOperatingHumidityLimitReset.isSupported  = true;
                            deviceStats->sasStatistics.highOperatingHumidityLimitReset.isValueValid = true;
                            deviceStats->sasStatistics.highOperatingHumidityLimitReset.statisticValue =
                                tempLogBuf[iter + 9];
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            //
                            deviceStats->sasStatistics.lowOperatingHumidityLimitReset.isSupported  = true;
                            deviceStats->sasStatistics.lowOperatingHumidityLimitReset.isValueValid = true;
                            deviceStats->sasStatistics.lowOperatingHumidityLimitReset.statisticValue =
                                tempLogBuf[iter + 10];
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            //
                            deviceStats->sasStatistics.lowOperatingHumidityLimitTrigger.isSupported  = true;
                            deviceStats->sasStatistics.lowOperatingHumidityLimitTrigger.isValueValid = true;
                            deviceStats->sasStatistics.lowOperatingHumidityLimitTrigger.statisticValue =
                                tempLogBuf[iter + 11];
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
                    // todo: add thresholds
                }
                break;
            default:
                break;
            }
            break;
        case LP_START_STOP_CYCLE_COUNTER:
            switch (subpageCode)
            {
            case 0: // start stop cycle counter
                safe_memset(tempLogBuf, LEGACY_DRIVE_SEC_SIZE, 0, LEGACY_DRIVE_SEC_SIZE);
                if (SUCCESS == scsi_Log_Sense_Cmd(device, false, LPC_CUMULATIVE_VALUES, pageCode, subpageCode, 0x0001,
                                                  tempLogBuf, LEGACY_DRIVE_SEC_SIZE))
                {
                    deviceStats->sasStatistics.startStopCycleCounterSupported = true;
                    uint16_t pageLength      = M_BytesTo2ByteValue(tempLogBuf[2], tempLogBuf[3]);
                    uint8_t  parameterLength = UINT8_C(0);
                    // loop through the data and gather the data from each parameter we care about getting.
                    for (uint16_t iter = UINT16_C(4); iter < pageLength && iter < LEGACY_DRIVE_SEC_SIZE;
                         iter += (C_CAST(uint16_t, parameterLength + UINT16_C(4))))
                    {
                        uint16_t parameterCode = M_BytesTo2ByteValue(tempLogBuf[iter], tempLogBuf[iter + 1]);
                        parameterLength        = tempLogBuf[iter + 3];
                        switch (parameterCode)
                        {
                        case 1: // date of manufacture
                            // set into the buffer as week-year and parse it out that way later.
                            deviceStats->sasStatistics.dateOfManufacture.isSupported  = true;
                            deviceStats->sasStatistics.dateOfManufacture.isValueValid = true;
                            deviceStats->sasStatistics.dateOfManufacture.thresholdNotificationEnabled =
                                tempLogBuf[iter + 2] & BIT4; // ETC bit
                            if (tempLogBuf[iter + 2] & BIT4)
                            {
                                switch ((tempLogBuf[iter + 2] & (BIT2 | BIT3)) >> 2)
                                {
                                case 3:
                                    deviceStats->sasStatistics.dateOfManufacture.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_GREATER;
                                    break;
                                case 2:
                                    deviceStats->sasStatistics.dateOfManufacture.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL;
                                    break;
                                case 1:
                                    deviceStats->sasStatistics.dateOfManufacture.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL;
                                    break;
                                case 0:
                                default:
                                    deviceStats->sasStatistics.dateOfManufacture.threshType =
                                        THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE;
                                    break;
                                }
                            }
                            deviceStats->sasStatistics.dateOfManufacture.statisticValue = M_BytesTo4ByteValue(
                                tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
                            deviceStats->sasStatistics.dateOfManufacture.statisticValue |=
                                C_CAST(uint64_t, M_BytesTo2ByteValue(tempLogBuf[iter + 8], tempLogBuf[iter + 9])) << 32;
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            break;
                        case 2: // accounting date
                            // set into the buffer as week-year and parse it out that way later.
                            deviceStats->sasStatistics.accountingDate.isSupported  = true;
                            deviceStats->sasStatistics.accountingDate.isValueValid = true;
                            deviceStats->sasStatistics.accountingDate.thresholdNotificationEnabled =
                                tempLogBuf[iter + 2] & BIT4; // ETC bit
                            if (tempLogBuf[iter + 2] & BIT4)
                            {
                                switch ((tempLogBuf[iter + 2] & (BIT2 | BIT3)) >> 2)
                                {
                                case 3:
                                    deviceStats->sasStatistics.accountingDate.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_GREATER;
                                    break;
                                case 2:
                                    deviceStats->sasStatistics.accountingDate.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL;
                                    break;
                                case 1:
                                    deviceStats->sasStatistics.accountingDate.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL;
                                    break;
                                case 0:
                                default:
                                    deviceStats->sasStatistics.accountingDate.threshType =
                                        THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE;
                                    break;
                                }
                            }
                            deviceStats->sasStatistics.accountingDate.statisticValue = M_BytesTo4ByteValue(
                                tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
                            deviceStats->sasStatistics.accountingDate.statisticValue |=
                                C_CAST(uint64_t, M_BytesTo2ByteValue(tempLogBuf[iter + 8], tempLogBuf[iter + 9])) << 32;
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            break;
                        case 3: // specified cycle count over device lifetime
                            deviceStats->sasStatistics.specifiedCycleCountOverDeviceLifetime.isSupported  = true;
                            deviceStats->sasStatistics.specifiedCycleCountOverDeviceLifetime.isValueValid = true;
                            deviceStats->sasStatistics.specifiedCycleCountOverDeviceLifetime
                                .thresholdNotificationEnabled = tempLogBuf[iter + 2] & BIT4; // ETC bit
                            if (tempLogBuf[iter + 2] & BIT4)
                            {
                                switch ((tempLogBuf[iter + 2] & (BIT2 | BIT3)) >> 2)
                                {
                                case 3:
                                    deviceStats->sasStatistics.specifiedCycleCountOverDeviceLifetime.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_GREATER;
                                    break;
                                case 2:
                                    deviceStats->sasStatistics.specifiedCycleCountOverDeviceLifetime.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL;
                                    break;
                                case 1:
                                    deviceStats->sasStatistics.specifiedCycleCountOverDeviceLifetime.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL;
                                    break;
                                case 0:
                                default:
                                    deviceStats->sasStatistics.specifiedCycleCountOverDeviceLifetime.threshType =
                                        THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE;
                                    break;
                                }
                            }
                            deviceStats->sasStatistics.specifiedCycleCountOverDeviceLifetime.statisticValue =
                                M_BytesTo4ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6],
                                                    tempLogBuf[iter + 7]);
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            break;
                        case 4: // accumulated start-stop cycles
                            deviceStats->sasStatistics.accumulatedStartStopCycles.isSupported  = true;
                            deviceStats->sasStatistics.accumulatedStartStopCycles.isValueValid = true;
                            deviceStats->sasStatistics.accumulatedStartStopCycles.thresholdNotificationEnabled =
                                tempLogBuf[iter + 2] & BIT4; // ETC bit
                            if (tempLogBuf[iter + 2] & BIT4)
                            {
                                switch ((tempLogBuf[iter + 2] & (BIT2 | BIT3)) >> 2)
                                {
                                case 3:
                                    deviceStats->sasStatistics.accumulatedStartStopCycles.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_GREATER;
                                    break;
                                case 2:
                                    deviceStats->sasStatistics.accumulatedStartStopCycles.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL;
                                    break;
                                case 1:
                                    deviceStats->sasStatistics.accumulatedStartStopCycles.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL;
                                    break;
                                case 0:
                                default:
                                    deviceStats->sasStatistics.accumulatedStartStopCycles.threshType =
                                        THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE;
                                    break;
                                }
                            }
                            deviceStats->sasStatistics.accumulatedStartStopCycles.statisticValue = M_BytesTo4ByteValue(
                                tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            break;
                        case 5: // specified load-unload count over device lifetime
                            deviceStats->sasStatistics.specifiedLoadUnloadCountOverDeviceLifetime.isSupported  = true;
                            deviceStats->sasStatistics.specifiedLoadUnloadCountOverDeviceLifetime.isValueValid = true;
                            deviceStats->sasStatistics.specifiedLoadUnloadCountOverDeviceLifetime
                                .thresholdNotificationEnabled = tempLogBuf[iter + 2] & BIT4; // ETC bit
                            if (tempLogBuf[iter + 2] & BIT4)
                            {
                                switch ((tempLogBuf[iter + 2] & (BIT2 | BIT3)) >> 2)
                                {
                                case 3:
                                    deviceStats->sasStatistics.specifiedLoadUnloadCountOverDeviceLifetime.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_GREATER;
                                    break;
                                case 2:
                                    deviceStats->sasStatistics.specifiedLoadUnloadCountOverDeviceLifetime.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL;
                                    break;
                                case 1:
                                    deviceStats->sasStatistics.specifiedLoadUnloadCountOverDeviceLifetime.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL;
                                    break;
                                case 0:
                                default:
                                    deviceStats->sasStatistics.specifiedLoadUnloadCountOverDeviceLifetime.threshType =
                                        THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE;
                                    break;
                                }
                            }
                            deviceStats->sasStatistics.specifiedLoadUnloadCountOverDeviceLifetime.statisticValue =
                                M_BytesTo4ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6],
                                                    tempLogBuf[iter + 7]);
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            break;
                        case 6: // accumulated load-unload cycles
                            deviceStats->sasStatistics.accumulatedLoadUnloadCycles.isSupported  = true;
                            deviceStats->sasStatistics.accumulatedLoadUnloadCycles.isValueValid = true;
                            deviceStats->sasStatistics.accumulatedLoadUnloadCycles.thresholdNotificationEnabled =
                                tempLogBuf[iter + 2] & BIT4; // ETC bit
                            if (tempLogBuf[iter + 2] & BIT4)
                            {
                                switch ((tempLogBuf[iter + 2] & (BIT2 | BIT3)) >> 2)
                                {
                                case 3:
                                    deviceStats->sasStatistics.accumulatedLoadUnloadCycles.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_GREATER;
                                    break;
                                case 2:
                                    deviceStats->sasStatistics.accumulatedLoadUnloadCycles.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL;
                                    break;
                                case 1:
                                    deviceStats->sasStatistics.accumulatedLoadUnloadCycles.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL;
                                    break;
                                case 0:
                                default:
                                    deviceStats->sasStatistics.accumulatedLoadUnloadCycles.threshType =
                                        THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE;
                                    break;
                                }
                            }
                            deviceStats->sasStatistics.accumulatedLoadUnloadCycles.statisticValue = M_BytesTo4ByteValue(
                                tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
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
                    // thresholds
                    safe_memset(tempLogBuf, LEGACY_DRIVE_SEC_SIZE, 0, LEGACY_DRIVE_SEC_SIZE);
                    if (SUCCESS == scsi_Log_Sense_Cmd(device, false, LPC_THRESHOLD_VALUES, pageCode, subpageCode,
                                                      0x0001, tempLogBuf, LEGACY_DRIVE_SEC_SIZE))
                    {
                        pageLength      = M_BytesTo2ByteValue(tempLogBuf[2], tempLogBuf[3]);
                        parameterLength = 0;
                        // loop through the data and gather the data from each parameter we care about getting.
                        for (uint16_t iter = UINT16_C(4); iter < pageLength && iter < LEGACY_DRIVE_SEC_SIZE;
                             iter += (C_CAST(uint16_t, parameterLength + UINT16_C(4))))
                        {
                            uint16_t parameterCode = M_BytesTo2ByteValue(tempLogBuf[iter], tempLogBuf[iter + 1]);
                            parameterLength        = tempLogBuf[iter + 3];
                            switch (parameterCode)
                            {
                            case 1: // date of manufacture
                                deviceStats->sasStatistics.dateOfManufacture.supportsNotification = true;
                                if (tempLogBuf[iter + 2] & BIT4)
                                {
                                    deviceStats->sasStatistics.dateOfManufacture.isThresholdValid = true;
                                    deviceStats->sasStatistics.dateOfManufacture.threshold =
                                        M_BytesTo4ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5],
                                                            tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
                                    deviceStats->sasStatistics.dateOfManufacture.threshold |=
                                        C_CAST(uint64_t,
                                               M_BytesTo2ByteValue(tempLogBuf[iter + 8], tempLogBuf[iter + 9]))
                                        << 32;
                                    scsi_Threshold_Comparison(&deviceStats->sasStatistics.dateOfManufacture);
                                }
                                break;
                            case 2: // accounting date
                                deviceStats->sasStatistics.accountingDate.supportsNotification = true;
                                if (tempLogBuf[iter + 2] & BIT4)
                                {
                                    deviceStats->sasStatistics.accountingDate.isThresholdValid = true;
                                    deviceStats->sasStatistics.accountingDate.threshold =
                                        M_BytesTo4ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5],
                                                            tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
                                    deviceStats->sasStatistics.accountingDate.threshold |=
                                        C_CAST(uint64_t,
                                               M_BytesTo2ByteValue(tempLogBuf[iter + 8], tempLogBuf[iter + 9]))
                                        << 32;
                                    scsi_Threshold_Comparison(&deviceStats->sasStatistics.accountingDate);
                                }
                                break;
                            case 3: // specified cycle count over device lifetime
                                deviceStats->sasStatistics.specifiedCycleCountOverDeviceLifetime.supportsNotification =
                                    true;
                                if (tempLogBuf[iter + 2] & BIT4)
                                {
                                    deviceStats->sasStatistics.specifiedCycleCountOverDeviceLifetime.isThresholdValid =
                                        true;
                                    deviceStats->sasStatistics.specifiedCycleCountOverDeviceLifetime.statisticValue =
                                        M_BytesTo4ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5],
                                                            tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
                                    scsi_Threshold_Comparison(
                                        &deviceStats->sasStatistics.specifiedCycleCountOverDeviceLifetime);
                                }
                                break;
                            case 4: // accumulated start-stop cycles
                                deviceStats->sasStatistics.accumulatedStartStopCycles.supportsNotification = true;
                                if (tempLogBuf[iter + 2] & BIT4)
                                {
                                    deviceStats->sasStatistics.accumulatedStartStopCycles.isThresholdValid = true;
                                    deviceStats->sasStatistics.accumulatedStartStopCycles.statisticValue =
                                        M_BytesTo4ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5],
                                                            tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
                                    scsi_Threshold_Comparison(&deviceStats->sasStatistics.accumulatedStartStopCycles);
                                }
                                break;
                            case 5: // specified load-unload count over device lifetime
                                deviceStats->sasStatistics.specifiedLoadUnloadCountOverDeviceLifetime
                                    .supportsNotification = true;
                                if (tempLogBuf[iter + 2] & BIT4)
                                {
                                    deviceStats->sasStatistics.specifiedLoadUnloadCountOverDeviceLifetime
                                        .isThresholdValid = true;
                                    deviceStats->sasStatistics.specifiedLoadUnloadCountOverDeviceLifetime
                                        .statisticValue =
                                        M_BytesTo4ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5],
                                                            tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
                                    scsi_Threshold_Comparison(
                                        &deviceStats->sasStatistics.specifiedLoadUnloadCountOverDeviceLifetime);
                                }
                                break;
                            case 6: // accumulated load-unload cycles
                                deviceStats->sasStatistics.accumulatedLoadUnloadCycles.supportsNotification = true;
                                if (tempLogBuf[iter + 2] & BIT4)
                                {
                                    deviceStats->sasStatistics.accumulatedLoadUnloadCycles.isThresholdValid = true;
                                    deviceStats->sasStatistics.accumulatedLoadUnloadCycles.statisticValue =
                                        M_BytesTo4ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5],
                                                            tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
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
            case 1: // utilization
                safe_memset(tempLogBuf, LEGACY_DRIVE_SEC_SIZE, 0, LEGACY_DRIVE_SEC_SIZE);
                if (SUCCESS == scsi_Log_Sense_Cmd(device, false, LPC_CUMULATIVE_VALUES, pageCode, subpageCode, 0x0000,
                                                  tempLogBuf, LEGACY_DRIVE_SEC_SIZE))
                {
                    deviceStats->sasStatistics.utilizationSupported = true;
                    uint16_t pageLength                             = M_BytesTo2ByteValue(tempLogBuf[2], tempLogBuf[3]);
                    uint8_t  parameterLength                        = UINT8_C(0);
                    // loop through the data and gather the data from each parameter we care about getting.
                    for (uint16_t iter = UINT16_C(4); iter < pageLength && iter < LEGACY_DRIVE_SEC_SIZE;
                         iter += (C_CAST(uint16_t, parameterLength + UINT16_C(4))))
                    {
                        uint16_t parameterCode = M_BytesTo2ByteValue(tempLogBuf[iter], tempLogBuf[iter + 1]);
                        parameterLength        = tempLogBuf[iter + 3];
                        switch (parameterCode)
                        {
                        case 0: // workload utilization
                            deviceStats->sasStatistics.workloadUtilization.isSupported  = true;
                            deviceStats->sasStatistics.workloadUtilization.isValueValid = true;
                            deviceStats->sasStatistics.workloadUtilization.thresholdNotificationEnabled =
                                tempLogBuf[iter + 2] & BIT4; // ETC bit
                            if (tempLogBuf[iter + 2] & BIT4)
                            {
                                switch ((tempLogBuf[iter + 2] & (BIT2 | BIT3)) >> 2)
                                {
                                case 3:
                                    deviceStats->sasStatistics.workloadUtilization.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_GREATER;
                                    break;
                                case 2:
                                    deviceStats->sasStatistics.workloadUtilization.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL;
                                    break;
                                case 1:
                                    deviceStats->sasStatistics.workloadUtilization.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL;
                                    break;
                                case 0:
                                default:
                                    deviceStats->sasStatistics.workloadUtilization.threshType =
                                        THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE;
                                    break;
                                }
                            }
                            deviceStats->sasStatistics.workloadUtilization.statisticValue =
                                M_BytesTo2ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5]);
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            break;
                        case 1: // utilization usage rate based on date and time
                            deviceStats->sasStatistics.utilizationUsageRateBasedOnDateAndTime.isSupported  = true;
                            deviceStats->sasStatistics.utilizationUsageRateBasedOnDateAndTime.isValueValid = true;
                            deviceStats->sasStatistics.utilizationUsageRateBasedOnDateAndTime
                                .thresholdNotificationEnabled = tempLogBuf[iter + 2] & BIT4; // ETC bit
                            if (tempLogBuf[iter + 2] & BIT4)
                            {
                                switch ((tempLogBuf[iter + 2] & (BIT2 | BIT3)) >> 2)
                                {
                                case 3:
                                    deviceStats->sasStatistics.utilizationUsageRateBasedOnDateAndTime.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_GREATER;
                                    break;
                                case 2:
                                    deviceStats->sasStatistics.utilizationUsageRateBasedOnDateAndTime.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL;
                                    break;
                                case 1:
                                    deviceStats->sasStatistics.utilizationUsageRateBasedOnDateAndTime.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL;
                                    break;
                                case 0:
                                default:
                                    deviceStats->sasStatistics.utilizationUsageRateBasedOnDateAndTime.threshType =
                                        THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE;
                                    break;
                                }
                            }
                            deviceStats->sasStatistics.utilizationUsageRateBasedOnDateAndTime.statisticValue =
                                tempLogBuf[iter + 4];
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
                    // thresholds
                    safe_memset(tempLogBuf, LEGACY_DRIVE_SEC_SIZE, 0, LEGACY_DRIVE_SEC_SIZE);
                    if (SUCCESS == scsi_Log_Sense_Cmd(device, false, LPC_THRESHOLD_VALUES, pageCode, subpageCode,
                                                      0x0000, tempLogBuf, LEGACY_DRIVE_SEC_SIZE))
                    {
                        pageLength      = M_BytesTo2ByteValue(tempLogBuf[2], tempLogBuf[3]);
                        parameterLength = 0;
                        // loop through the data and gather the data from each parameter we care about getting.
                        for (uint16_t iter = UINT16_C(4); iter < pageLength && iter < LEGACY_DRIVE_SEC_SIZE;
                             iter += (C_CAST(uint16_t, parameterLength + UINT16_C(4))))
                        {
                            uint16_t parameterCode = M_BytesTo2ByteValue(tempLogBuf[iter], tempLogBuf[iter + 1]);
                            parameterLength        = tempLogBuf[iter + 3];
                            switch (parameterCode)
                            {
                            case 1: // workload utilization
                                deviceStats->sasStatistics.workloadUtilization.supportsNotification = true;
                                if (tempLogBuf[iter + 2] & BIT4)
                                {
                                    deviceStats->sasStatistics.workloadUtilization.isThresholdValid = true;
                                    deviceStats->sasStatistics.workloadUtilization.threshold =
                                        M_BytesTo2ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5]);
                                    scsi_Threshold_Comparison(&deviceStats->sasStatistics.workloadUtilization);
                                }
                                break;
                            case 2: // utilization usage based on date and timestamp
                                deviceStats->sasStatistics.utilizationUsageRateBasedOnDateAndTime.supportsNotification =
                                    true;
                                if (tempLogBuf[iter + 2] & BIT4)
                                {
                                    deviceStats->sasStatistics.utilizationUsageRateBasedOnDateAndTime.isThresholdValid =
                                        true;
                                    deviceStats->sasStatistics.utilizationUsageRateBasedOnDateAndTime.threshold =
                                        tempLogBuf[iter + 4];
                                    scsi_Threshold_Comparison(
                                        &deviceStats->sasStatistics.utilizationUsageRateBasedOnDateAndTime);
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
                safe_memset(tempLogBuf, LEGACY_DRIVE_SEC_SIZE, 0, LEGACY_DRIVE_SEC_SIZE);
                if (SUCCESS == scsi_Log_Sense_Cmd(device, false, LPC_CUMULATIVE_VALUES, pageCode, subpageCode, 0x0001,
                                                  tempLogBuf, LEGACY_DRIVE_SEC_SIZE))
                {
                    deviceStats->sasStatistics.solidStateMediaSupported = true;
                    uint16_t pageLength      = M_BytesTo2ByteValue(tempLogBuf[2], tempLogBuf[3]);
                    uint8_t  parameterLength = UINT8_C(0);
                    // loop through the data and gather the data from each parameter we care about getting.
                    for (uint16_t iter = UINT16_C(4); iter < pageLength && iter < LEGACY_DRIVE_SEC_SIZE;
                         iter += (C_CAST(uint16_t, parameterLength + UINT16_C(4))))
                    {
                        uint16_t parameterCode = M_BytesTo2ByteValue(tempLogBuf[iter], tempLogBuf[iter + 1]);
                        parameterLength        = tempLogBuf[iter + 3];
                        switch (parameterCode)
                        {
                        case 1: // percent used endurance indicator
                            deviceStats->sasStatistics.percentUsedEndurance.isSupported  = true;
                            deviceStats->sasStatistics.percentUsedEndurance.isValueValid = true;
                            deviceStats->sasStatistics.percentUsedEndurance.thresholdNotificationEnabled =
                                tempLogBuf[iter + 2] & BIT4; // ETC bit
                            if (tempLogBuf[iter + 2] & BIT4)
                            {
                                switch ((tempLogBuf[iter + 2] & (BIT2 | BIT3)) >> 2)
                                {
                                case 3:
                                    deviceStats->sasStatistics.percentUsedEndurance.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_GREATER;
                                    break;
                                case 2:
                                    deviceStats->sasStatistics.percentUsedEndurance.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL;
                                    break;
                                case 1:
                                    deviceStats->sasStatistics.percentUsedEndurance.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL;
                                    break;
                                case 0:
                                default:
                                    deviceStats->sasStatistics.percentUsedEndurance.threshType =
                                        THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE;
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
                    // thresholds
                    safe_memset(tempLogBuf, LEGACY_DRIVE_SEC_SIZE, 0, LEGACY_DRIVE_SEC_SIZE);
                    if (SUCCESS == scsi_Log_Sense_Cmd(device, false, LPC_THRESHOLD_VALUES, pageCode, subpageCode,
                                                      0x0001, tempLogBuf, LEGACY_DRIVE_SEC_SIZE))
                    {
                        pageLength      = M_BytesTo2ByteValue(tempLogBuf[2], tempLogBuf[3]);
                        parameterLength = 0;
                        // loop through the data and gather the data from each parameter we care about getting.
                        for (uint16_t iter = UINT16_C(4); iter < pageLength && iter < LEGACY_DRIVE_SEC_SIZE;
                             iter += (C_CAST(uint16_t, parameterLength + UINT16_C(4))))
                        {
                            uint16_t parameterCode = M_BytesTo2ByteValue(tempLogBuf[iter], tempLogBuf[iter + 1]);
                            parameterLength        = tempLogBuf[iter + 3];
                            switch (parameterCode)
                            {
                            case 1: // percent used endurance
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
            case 0: // background scan results
                safe_memset(tempLogBuf, LEGACY_DRIVE_SEC_SIZE, 0, LEGACY_DRIVE_SEC_SIZE);
                if (SUCCESS == scsi_Log_Sense_Cmd(device, false, LPC_CUMULATIVE_VALUES, pageCode, subpageCode, 0x0000,
                                                  tempLogBuf, LEGACY_DRIVE_SEC_SIZE))
                {
                    deviceStats->sasStatistics.backgroundScanResultsSupported = true;
                    uint16_t pageLength      = M_BytesTo2ByteValue(tempLogBuf[2], tempLogBuf[3]);
                    uint8_t  parameterLength = UINT8_C(0);
                    // loop through the data and gather the data from each parameter we care about getting.
                    for (uint16_t iter = UINT16_C(4); iter < pageLength && iter < LEGACY_DRIVE_SEC_SIZE;
                         iter += (C_CAST(uint16_t, parameterLength + UINT16_C(4))))
                    {
                        uint16_t parameterCode = M_BytesTo2ByteValue(tempLogBuf[iter], tempLogBuf[iter + 1]);
                        parameterLength        = tempLogBuf[iter + 3];
                        switch (parameterCode)
                        {
                        case 0: // POM, # scans performed, bg scan progress, # bgms performed
                            // accumulated power on minutes
                            deviceStats->sasStatistics.accumulatedPowerOnMinutes.isSupported    = true;
                            deviceStats->sasStatistics.accumulatedPowerOnMinutes.isValueValid   = true;
                            deviceStats->sasStatistics.accumulatedPowerOnMinutes.statisticValue = M_BytesTo4ByteValue(
                                tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            // number of background scans performed
                            deviceStats->sasStatistics.numberOfBackgroundScansPerformed.isSupported  = true;
                            deviceStats->sasStatistics.numberOfBackgroundScansPerformed.isValueValid = true;
                            deviceStats->sasStatistics.numberOfBackgroundScansPerformed.statisticValue =
                                M_BytesTo2ByteValue(tempLogBuf[iter + 10], tempLogBuf[iter + 11]);
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            // background scan progress- not sure if this is needed.

                            // number of background media scans performed
                            deviceStats->sasStatistics.numberOfBackgroundMediaScansPerformed.isSupported  = true;
                            deviceStats->sasStatistics.numberOfBackgroundMediaScansPerformed.isValueValid = true;
                            deviceStats->sasStatistics.numberOfBackgroundMediaScansPerformed.statisticValue =
                                M_BytesTo2ByteValue(tempLogBuf[iter + 14], tempLogBuf[iter + 15]);
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
                    // thresholds
                    safe_memset(tempLogBuf, LEGACY_DRIVE_SEC_SIZE, 0, LEGACY_DRIVE_SEC_SIZE);
                    if (SUCCESS == scsi_Log_Sense_Cmd(device, false, LPC_THRESHOLD_VALUES, pageCode, subpageCode,
                                                      0x0000, tempLogBuf, LEGACY_DRIVE_SEC_SIZE))
                    {
                        pageLength      = M_BytesTo2ByteValue(tempLogBuf[2], tempLogBuf[3]);
                        parameterLength = 0;
                        // loop through the data and gather the data from each parameter we care about getting.
                        for (uint16_t iter = UINT16_C(4); iter < pageLength && iter < LEGACY_DRIVE_SEC_SIZE;
                             iter += (C_CAST(uint16_t, parameterLength + UINT16_C(4))))
                        {
                            uint16_t parameterCode = M_BytesTo2ByteValue(tempLogBuf[iter], tempLogBuf[iter + 1]);
                            parameterLength        = tempLogBuf[iter + 3];
                            switch (parameterCode)
                            {
                            case 0: // POM, # scans performed, bg scan progress, # bgms performed
                                deviceStats->sasStatistics.accumulatedPowerOnMinutes.supportsNotification        = true;
                                deviceStats->sasStatistics.numberOfBackgroundScansPerformed.supportsNotification = true;
                                deviceStats->sasStatistics.numberOfBackgroundMediaScansPerformed.supportsNotification =
                                    true;
                                if (tempLogBuf[iter + 2] & BIT4)
                                {
                                    // accumulated power on minutes
                                    deviceStats->sasStatistics.accumulatedPowerOnMinutes.isThresholdValid = true;
                                    deviceStats->sasStatistics.accumulatedPowerOnMinutes.threshold =
                                        M_BytesTo4ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5],
                                                            tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
                                    scsi_Threshold_Comparison(&deviceStats->sasStatistics.accumulatedPowerOnMinutes);
                                    // number of background scans performed
                                    deviceStats->sasStatistics.numberOfBackgroundScansPerformed.isThresholdValid = true;
                                    deviceStats->sasStatistics.numberOfBackgroundScansPerformed.threshold =
                                        M_BytesTo2ByteValue(tempLogBuf[iter + 10], tempLogBuf[iter + 11]);
                                    scsi_Threshold_Comparison(
                                        &deviceStats->sasStatistics.numberOfBackgroundScansPerformed);
                                    // background scan progress- not sure if this is needed.

                                    // number of background media scans performed
                                    deviceStats->sasStatistics.numberOfBackgroundMediaScansPerformed.isThresholdValid =
                                        true;
                                    deviceStats->sasStatistics.numberOfBackgroundMediaScansPerformed.threshold =
                                        M_BytesTo2ByteValue(tempLogBuf[iter + 14], tempLogBuf[iter + 15]);
                                    scsi_Threshold_Comparison(
                                        &deviceStats->sasStatistics.numberOfBackgroundMediaScansPerformed);
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
            case 1: // pending defects
                safe_memset(tempLogBuf, LEGACY_DRIVE_SEC_SIZE, 0, LEGACY_DRIVE_SEC_SIZE);
                if (SUCCESS == scsi_Log_Sense_Cmd(device, false, LPC_CUMULATIVE_VALUES, pageCode, subpageCode, 0x0000,
                                                  tempLogBuf, LEGACY_DRIVE_SEC_SIZE))
                {
                    deviceStats->sasStatistics.pendingDefectsSupported = true;
                    uint16_t pageLength      = M_BytesTo2ByteValue(tempLogBuf[2], tempLogBuf[3]);
                    uint8_t  parameterLength = UINT8_C(0);
                    // loop through the data and gather the data from each parameter we care about getting.
                    for (uint16_t iter = UINT16_C(4); iter < pageLength && iter < LEGACY_DRIVE_SEC_SIZE;
                         iter += (C_CAST(uint16_t, parameterLength + UINT16_C(4))))
                    {
                        uint16_t parameterCode = M_BytesTo2ByteValue(tempLogBuf[iter], tempLogBuf[iter + 1]);
                        parameterLength        = tempLogBuf[iter + 3];
                        switch (parameterCode)
                        {
                        case 0: // pending defect count
                            deviceStats->sasStatistics.pendingDefectCount.isSupported  = true;
                            deviceStats->sasStatistics.pendingDefectCount.isValueValid = true;
                            deviceStats->sasStatistics.pendingDefectCount.thresholdNotificationEnabled =
                                tempLogBuf[iter + 2] & BIT4; // ETC bit
                            if (tempLogBuf[iter + 2] & BIT4)
                            {
                                switch ((tempLogBuf[iter + 2] & (BIT2 | BIT3)) >> 2)
                                {
                                case 3:
                                    deviceStats->sasStatistics.pendingDefectCount.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_GREATER;
                                    break;
                                case 2:
                                    deviceStats->sasStatistics.pendingDefectCount.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL;
                                    break;
                                case 1:
                                    deviceStats->sasStatistics.pendingDefectCount.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL;
                                    break;
                                case 0:
                                default:
                                    deviceStats->sasStatistics.pendingDefectCount.threshType =
                                        THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE;
                                    break;
                                }
                            }
                            deviceStats->sasStatistics.pendingDefectCount.statisticValue = M_BytesTo4ByteValue(
                                tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
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
                    // thresholds
                    safe_memset(tempLogBuf, LEGACY_DRIVE_SEC_SIZE, 0, LEGACY_DRIVE_SEC_SIZE);
                    if (SUCCESS == scsi_Log_Sense_Cmd(device, false, LPC_THRESHOLD_VALUES, pageCode, subpageCode,
                                                      0x0000, tempLogBuf, LEGACY_DRIVE_SEC_SIZE))
                    {
                        pageLength      = M_BytesTo2ByteValue(tempLogBuf[2], tempLogBuf[3]);
                        parameterLength = 0;
                        // loop through the data and gather the data from each parameter we care about getting.
                        for (uint16_t iter = UINT16_C(4); iter < pageLength && iter < LEGACY_DRIVE_SEC_SIZE;
                             iter += (C_CAST(uint16_t, parameterLength + UINT16_C(4))))
                        {
                            uint16_t parameterCode = M_BytesTo2ByteValue(tempLogBuf[iter], tempLogBuf[iter + 1]);
                            parameterLength        = tempLogBuf[iter + 3];
                            switch (parameterCode)
                            {
                            case 0: // pending defects count
                                deviceStats->sasStatistics.pendingDefectCount.supportsNotification = true;
                                if (tempLogBuf[iter + 2] & BIT4)
                                {
                                    deviceStats->sasStatistics.pendingDefectCount.isThresholdValid = true;
                                    deviceStats->sasStatistics.pendingDefectCount.threshold =
                                        M_BytesTo4ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5],
                                                            tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
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
            case 2: // background operaton
                break;
            case 3: // lps misalignment
                safe_memset(tempLogBuf, LEGACY_DRIVE_SEC_SIZE, 0, LEGACY_DRIVE_SEC_SIZE);
                if (SUCCESS == scsi_Log_Sense_Cmd(device, false, LPC_CUMULATIVE_VALUES, pageCode, subpageCode, 0x0000,
                                                  tempLogBuf, LEGACY_DRIVE_SEC_SIZE))
                {
                    deviceStats->sasStatistics.lpsMisalignmentSupported = true;
                    uint16_t pageLength      = M_BytesTo2ByteValue(tempLogBuf[2], tempLogBuf[3]);
                    uint8_t  parameterLength = UINT8_C(0);
                    // loop through the data and gather the data from each parameter we care about getting.
                    for (uint16_t iter = UINT16_C(4); iter < pageLength && iter < LEGACY_DRIVE_SEC_SIZE;
                         iter += (C_CAST(uint16_t, parameterLength + UINT16_C(4))))
                    {
                        uint16_t parameterCode = M_BytesTo2ByteValue(tempLogBuf[iter], tempLogBuf[iter + 1]);
                        parameterLength        = tempLogBuf[iter + 3];
                        switch (parameterCode)
                        {
                        case 0: // LPS misalignment count
                            deviceStats->sasStatistics.lpsMisalignmentCount.isSupported  = true;
                            deviceStats->sasStatistics.lpsMisalignmentCount.isValueValid = true;
                            deviceStats->sasStatistics.lpsMisalignmentCount.thresholdNotificationEnabled =
                                tempLogBuf[iter + 2] & BIT4; // ETC bit
                            if (tempLogBuf[iter + 2] & BIT4)
                            {
                                switch ((tempLogBuf[iter + 2] & (BIT2 | BIT3)) >> 2)
                                {
                                case 3:
                                    deviceStats->sasStatistics.lpsMisalignmentCount.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_GREATER;
                                    break;
                                case 2:
                                    deviceStats->sasStatistics.lpsMisalignmentCount.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL;
                                    break;
                                case 1:
                                    deviceStats->sasStatistics.lpsMisalignmentCount.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL;
                                    break;
                                case 0:
                                default:
                                    deviceStats->sasStatistics.lpsMisalignmentCount.threshType =
                                        THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE;
                                    break;
                                }
                            }
                            deviceStats->sasStatistics.lpsMisalignmentCount.statisticValue =
                                M_BytesTo2ByteValue(tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
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
                    // thresholds
                    safe_memset(tempLogBuf, LEGACY_DRIVE_SEC_SIZE, 0, LEGACY_DRIVE_SEC_SIZE);
                    if (SUCCESS == scsi_Log_Sense_Cmd(device, false, LPC_THRESHOLD_VALUES, pageCode, subpageCode,
                                                      0x0000, tempLogBuf, LEGACY_DRIVE_SEC_SIZE))
                    {
                        pageLength      = M_BytesTo2ByteValue(tempLogBuf[2], tempLogBuf[3]);
                        parameterLength = 0;
                        // loop through the data and gather the data from each parameter we care about getting.
                        for (uint16_t iter = UINT16_C(4); iter < pageLength && iter < LEGACY_DRIVE_SEC_SIZE;
                             iter += (C_CAST(uint16_t, parameterLength + UINT16_C(4))))
                        {
                            uint16_t parameterCode = M_BytesTo2ByteValue(tempLogBuf[iter], tempLogBuf[iter + 1]);
                            parameterLength        = tempLogBuf[iter + 3];
                            switch (parameterCode)
                            {
                            case 0: // LPS misalignment count
                                deviceStats->sasStatistics.lpsMisalignmentCount.supportsNotification = true;
                                if (tempLogBuf[iter + 2] & BIT4)
                                {
                                    deviceStats->sasStatistics.lpsMisalignmentCount.isThresholdValid = true;
                                    deviceStats->sasStatistics.lpsMisalignmentCount.threshold =
                                        M_BytesTo2ByteValue(tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
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
                safe_memset(tempLogBuf, LEGACY_DRIVE_SEC_SIZE, 0, LEGACY_DRIVE_SEC_SIZE);
                if (SUCCESS == scsi_Log_Sense_Cmd(device, false, LPC_CUMULATIVE_VALUES, pageCode, subpageCode, 0x0000,
                                                  tempLogBuf, LEGACY_DRIVE_SEC_SIZE))
                {
                    deviceStats->sasStatistics.nvCacheSupported = true;
                    uint16_t pageLength                         = M_BytesTo2ByteValue(tempLogBuf[2], tempLogBuf[3]);
                    uint8_t  parameterLength                    = UINT8_C(0);
                    // loop through the data and gather the data from each parameter we care about getting.
                    for (uint16_t iter = UINT16_C(4); iter < pageLength && iter < LEGACY_DRIVE_SEC_SIZE;
                         iter += (C_CAST(uint16_t, parameterLength + UINT16_C(4))))
                    {
                        uint16_t parameterCode = M_BytesTo2ByteValue(tempLogBuf[iter], tempLogBuf[iter + 1]);
                        parameterLength        = tempLogBuf[iter + 3];
                        switch (parameterCode)
                        {
                        case 0: // remaining nonvolatile time
                            deviceStats->sasStatistics.remainingNonvolatileTime.isSupported  = true;
                            deviceStats->sasStatistics.remainingNonvolatileTime.isValueValid = true;
                            deviceStats->sasStatistics.remainingNonvolatileTime.thresholdNotificationEnabled =
                                tempLogBuf[iter + 2] & BIT4; // ETC bit
                            if (tempLogBuf[iter + 2] & BIT4)
                            {
                                switch ((tempLogBuf[iter + 2] & (BIT2 | BIT3)) >> 2)
                                {
                                case 3:
                                    deviceStats->sasStatistics.remainingNonvolatileTime.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_GREATER;
                                    break;
                                case 2:
                                    deviceStats->sasStatistics.remainingNonvolatileTime.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL;
                                    break;
                                case 1:
                                    deviceStats->sasStatistics.remainingNonvolatileTime.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL;
                                    break;
                                case 0:
                                default:
                                    deviceStats->sasStatistics.remainingNonvolatileTime.threshType =
                                        THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE;
                                    break;
                                }
                            }
                            deviceStats->sasStatistics.remainingNonvolatileTime.statisticValue = M_BytesTo4ByteValue(
                                0, tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            break;
                        case 1: // maximum nonvolatile time
                            deviceStats->sasStatistics.maximumNonvolatileTime.isSupported  = true;
                            deviceStats->sasStatistics.maximumNonvolatileTime.isValueValid = true;
                            deviceStats->sasStatistics.maximumNonvolatileTime.thresholdNotificationEnabled =
                                tempLogBuf[iter + 2] & BIT4; // ETC bit
                            if (tempLogBuf[iter + 2] & BIT4)
                            {
                                switch ((tempLogBuf[iter + 2] & (BIT2 | BIT3)) >> 2)
                                {
                                case 3:
                                    deviceStats->sasStatistics.maximumNonvolatileTime.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_GREATER;
                                    break;
                                case 2:
                                    deviceStats->sasStatistics.maximumNonvolatileTime.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL;
                                    break;
                                case 1:
                                    deviceStats->sasStatistics.maximumNonvolatileTime.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL;
                                    break;
                                case 0:
                                default:
                                    deviceStats->sasStatistics.maximumNonvolatileTime.threshType =
                                        THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE;
                                    break;
                                }
                            }
                            deviceStats->sasStatistics.maximumNonvolatileTime.statisticValue = M_BytesTo4ByteValue(
                                0, tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
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
                    // thresholds
                    safe_memset(tempLogBuf, LEGACY_DRIVE_SEC_SIZE, 0, LEGACY_DRIVE_SEC_SIZE);
                    if (SUCCESS == scsi_Log_Sense_Cmd(device, false, LPC_THRESHOLD_VALUES, pageCode, subpageCode,
                                                      0x0000, tempLogBuf, LEGACY_DRIVE_SEC_SIZE))
                    {
                        pageLength      = M_BytesTo2ByteValue(tempLogBuf[2], tempLogBuf[3]);
                        parameterLength = 0;
                        // loop through the data and gather the data from each parameter we care about getting.
                        for (uint16_t iter = UINT16_C(4); iter < pageLength && iter < LEGACY_DRIVE_SEC_SIZE;
                             iter += (C_CAST(uint16_t, parameterLength + UINT16_C(4))))
                        {
                            uint16_t parameterCode = M_BytesTo2ByteValue(tempLogBuf[iter], tempLogBuf[iter + 1]);
                            parameterLength        = tempLogBuf[iter + 3];
                            switch (parameterCode)
                            {
                            case 0: // remaining nonvolatile time
                                deviceStats->sasStatistics.remainingNonvolatileTime.supportsNotification = true;
                                if (tempLogBuf[iter + 2] & BIT4)
                                {
                                    deviceStats->sasStatistics.remainingNonvolatileTime.isThresholdValid = true;
                                    deviceStats->sasStatistics.remainingNonvolatileTime.threshold = M_BytesTo4ByteValue(
                                        0, tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
                                    scsi_Threshold_Comparison(&deviceStats->sasStatistics.remainingNonvolatileTime);
                                }
                                break;
                            case 1: // maximum nonvolatile time
                                deviceStats->sasStatistics.maximumNonvolatileTime.supportsNotification = true;
                                if (tempLogBuf[iter + 2] & BIT4)
                                {
                                    deviceStats->sasStatistics.maximumNonvolatileTime.isThresholdValid = true;
                                    deviceStats->sasStatistics.maximumNonvolatileTime.threshold = M_BytesTo4ByteValue(
                                        0, tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
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
            case 0: // general statistics and performance
                safe_memset(tempLogBuf, LEGACY_DRIVE_SEC_SIZE, 0, LEGACY_DRIVE_SEC_SIZE);
                if (SUCCESS == scsi_Log_Sense_Cmd(device, false, LPC_CUMULATIVE_VALUES, pageCode, subpageCode, 0x0001,
                                                  tempLogBuf, LEGACY_DRIVE_SEC_SIZE))
                {
                    deviceStats->sasStatistics.generalStatisticsAndPerformanceSupported = true;
                    uint16_t pageLength      = M_BytesTo2ByteValue(tempLogBuf[2], tempLogBuf[3]);
                    uint8_t  parameterLength = UINT8_C(0);
                    // loop through the data and gather the data from each parameter we care about getting.
                    for (uint16_t iter = UINT16_C(4); iter < pageLength && iter < LEGACY_DRIVE_SEC_SIZE;
                         iter += (C_CAST(uint16_t, parameterLength + UINT16_C(4))))
                    {
                        uint16_t parameterCode = M_BytesTo2ByteValue(tempLogBuf[iter], tempLogBuf[iter + 1]);
                        parameterLength        = tempLogBuf[iter + 3];
                        switch (parameterCode)
                        {
                        case 1: // general access statistics and performance
                            // thresholds for parameter
                            if (tempLogBuf[iter + 2] & BIT4)
                            {
                                deviceStats->sasStatistics.numberOfReadCommands.thresholdNotificationEnabled =
                                    true; // ETC bit
                                deviceStats->sasStatistics.numberOfWriteCommands.thresholdNotificationEnabled =
                                    true; // ETC bit
                                deviceStats->sasStatistics.numberOfLogicalBlocksReceived.thresholdNotificationEnabled =
                                    true; // ETC bit
                                deviceStats->sasStatistics.numberOfLogicalBlocksTransmitted
                                    .thresholdNotificationEnabled = true; // ETC bit
                                deviceStats->sasStatistics.readCommandProcessingIntervals.thresholdNotificationEnabled =
                                    true; // ETC bit
                                deviceStats->sasStatistics.writeCommandProcessingIntervals
                                    .thresholdNotificationEnabled = true; // ETC bit
                                deviceStats->sasStatistics.weightedNumberOfReadCommandsPlusWriteCommands
                                    .thresholdNotificationEnabled = true; // ETC bit
                                deviceStats->sasStatistics.weightedReadCommandProcessingPlusWriteCommandProcessing
                                    .thresholdNotificationEnabled = true; // ETC bit
                                switch ((tempLogBuf[iter + 2] & (BIT2 | BIT3)) >> 2)
                                {
                                case 3:
                                    deviceStats->sasStatistics.numberOfReadCommands.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_GREATER;
                                    deviceStats->sasStatistics.numberOfWriteCommands.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_GREATER;
                                    deviceStats->sasStatistics.numberOfLogicalBlocksReceived.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_GREATER;
                                    deviceStats->sasStatistics.numberOfLogicalBlocksTransmitted.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_GREATER;
                                    deviceStats->sasStatistics.readCommandProcessingIntervals.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_GREATER;
                                    deviceStats->sasStatistics.writeCommandProcessingIntervals.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_GREATER;
                                    deviceStats->sasStatistics.weightedNumberOfReadCommandsPlusWriteCommands
                                        .threshType = THRESHOLD_TYPE_TRIGGER_WHEN_GREATER;
                                    deviceStats->sasStatistics.weightedReadCommandProcessingPlusWriteCommandProcessing
                                        .threshType = THRESHOLD_TYPE_TRIGGER_WHEN_GREATER;
                                    break;
                                case 2:
                                    deviceStats->sasStatistics.numberOfReadCommands.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL;
                                    deviceStats->sasStatistics.numberOfWriteCommands.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL;
                                    deviceStats->sasStatistics.numberOfLogicalBlocksReceived.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL;
                                    deviceStats->sasStatistics.numberOfLogicalBlocksTransmitted.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL;
                                    deviceStats->sasStatistics.readCommandProcessingIntervals.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL;
                                    deviceStats->sasStatistics.writeCommandProcessingIntervals.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL;
                                    deviceStats->sasStatistics.weightedNumberOfReadCommandsPlusWriteCommands
                                        .threshType = THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL;
                                    deviceStats->sasStatistics.weightedReadCommandProcessingPlusWriteCommandProcessing
                                        .threshType = THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL;
                                    break;
                                case 1:
                                    deviceStats->sasStatistics.numberOfReadCommands.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL;
                                    deviceStats->sasStatistics.numberOfWriteCommands.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL;
                                    deviceStats->sasStatistics.numberOfLogicalBlocksReceived.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL;
                                    deviceStats->sasStatistics.numberOfLogicalBlocksTransmitted.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL;
                                    deviceStats->sasStatistics.readCommandProcessingIntervals.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL;
                                    deviceStats->sasStatistics.writeCommandProcessingIntervals.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL;
                                    deviceStats->sasStatistics.weightedNumberOfReadCommandsPlusWriteCommands
                                        .threshType = THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL;
                                    deviceStats->sasStatistics.weightedReadCommandProcessingPlusWriteCommandProcessing
                                        .threshType = THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL;
                                    break;
                                case 0:
                                default:
                                    deviceStats->sasStatistics.numberOfReadCommands.threshType =
                                        THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE;
                                    deviceStats->sasStatistics.numberOfWriteCommands.threshType =
                                        THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE;
                                    deviceStats->sasStatistics.numberOfLogicalBlocksReceived.threshType =
                                        THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE;
                                    deviceStats->sasStatistics.numberOfLogicalBlocksTransmitted.threshType =
                                        THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE;
                                    deviceStats->sasStatistics.readCommandProcessingIntervals.threshType =
                                        THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE;
                                    deviceStats->sasStatistics.writeCommandProcessingIntervals.threshType =
                                        THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE;
                                    deviceStats->sasStatistics.weightedNumberOfReadCommandsPlusWriteCommands
                                        .threshType = THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE;
                                    deviceStats->sasStatistics.weightedReadCommandProcessingPlusWriteCommandProcessing
                                        .threshType = THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE;
                                    break;
                                }
                            }
                            // number of read commands
                            deviceStats->sasStatistics.numberOfReadCommands.isSupported  = true;
                            deviceStats->sasStatistics.numberOfReadCommands.isValueValid = true;
                            deviceStats->sasStatistics.numberOfReadCommands.statisticValue =
                                M_BytesTo8ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6],
                                                    tempLogBuf[iter + 7], tempLogBuf[iter + 8], tempLogBuf[iter + 9],
                                                    tempLogBuf[iter + 10], tempLogBuf[iter + 11]);
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            // number of write commands
                            deviceStats->sasStatistics.numberOfWriteCommands.isSupported  = true;
                            deviceStats->sasStatistics.numberOfWriteCommands.isValueValid = true;
                            deviceStats->sasStatistics.numberOfWriteCommands.statisticValue =
                                M_BytesTo8ByteValue(tempLogBuf[iter + 12], tempLogBuf[iter + 13], tempLogBuf[iter + 14],
                                                    tempLogBuf[iter + 15], tempLogBuf[iter + 16], tempLogBuf[iter + 17],
                                                    tempLogBuf[iter + 18], tempLogBuf[iter + 19]);
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            // number of logical blocks received
                            deviceStats->sasStatistics.numberOfLogicalBlocksReceived.isSupported  = true;
                            deviceStats->sasStatistics.numberOfLogicalBlocksReceived.isValueValid = true;
                            deviceStats->sasStatistics.numberOfLogicalBlocksReceived.statisticValue =
                                M_BytesTo8ByteValue(tempLogBuf[iter + 20], tempLogBuf[iter + 21], tempLogBuf[iter + 22],
                                                    tempLogBuf[iter + 23], tempLogBuf[iter + 24], tempLogBuf[iter + 25],
                                                    tempLogBuf[iter + 26], tempLogBuf[iter + 27]);
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            // number of logical blocks transmitted
                            deviceStats->sasStatistics.numberOfLogicalBlocksTransmitted.isSupported  = true;
                            deviceStats->sasStatistics.numberOfLogicalBlocksTransmitted.isValueValid = true;
                            deviceStats->sasStatistics.numberOfLogicalBlocksTransmitted.statisticValue =
                                M_BytesTo8ByteValue(tempLogBuf[iter + 28], tempLogBuf[iter + 29], tempLogBuf[iter + 30],
                                                    tempLogBuf[iter + 31], tempLogBuf[iter + 32], tempLogBuf[iter + 33],
                                                    tempLogBuf[iter + 34], tempLogBuf[iter + 35]);
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            // read command processing intervals
                            deviceStats->sasStatistics.readCommandProcessingIntervals.isSupported  = true;
                            deviceStats->sasStatistics.readCommandProcessingIntervals.isValueValid = true;
                            deviceStats->sasStatistics.readCommandProcessingIntervals.statisticValue =
                                M_BytesTo8ByteValue(tempLogBuf[iter + 36], tempLogBuf[iter + 37], tempLogBuf[iter + 38],
                                                    tempLogBuf[iter + 39], tempLogBuf[iter + 40], tempLogBuf[iter + 41],
                                                    tempLogBuf[iter + 42], tempLogBuf[iter + 43]);
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            // write command processing intervals
                            deviceStats->sasStatistics.writeCommandProcessingIntervals.isSupported  = true;
                            deviceStats->sasStatistics.writeCommandProcessingIntervals.isValueValid = true;
                            deviceStats->sasStatistics.writeCommandProcessingIntervals.statisticValue =
                                M_BytesTo8ByteValue(tempLogBuf[iter + 44], tempLogBuf[iter + 45], tempLogBuf[iter + 46],
                                                    tempLogBuf[iter + 47], tempLogBuf[iter + 48], tempLogBuf[iter + 49],
                                                    tempLogBuf[iter + 50], tempLogBuf[iter + 51]);
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            // weighted number of read commands plus write commansd
                            deviceStats->sasStatistics.weightedNumberOfReadCommandsPlusWriteCommands.isSupported = true;
                            deviceStats->sasStatistics.weightedNumberOfReadCommandsPlusWriteCommands.isValueValid =
                                true;
                            deviceStats->sasStatistics.weightedNumberOfReadCommandsPlusWriteCommands.statisticValue =
                                M_BytesTo8ByteValue(tempLogBuf[iter + 52], tempLogBuf[iter + 53], tempLogBuf[iter + 54],
                                                    tempLogBuf[iter + 55], tempLogBuf[iter + 56], tempLogBuf[iter + 57],
                                                    tempLogBuf[iter + 58], tempLogBuf[iter + 59]);
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            // weighted number of read command processing plus write command processing
                            deviceStats->sasStatistics.weightedReadCommandProcessingPlusWriteCommandProcessing
                                .isSupported = true;
                            deviceStats->sasStatistics.weightedReadCommandProcessingPlusWriteCommandProcessing
                                .isValueValid = true;
                            deviceStats->sasStatistics.weightedReadCommandProcessingPlusWriteCommandProcessing
                                .statisticValue =
                                M_BytesTo8ByteValue(tempLogBuf[iter + 60], tempLogBuf[iter + 61], tempLogBuf[iter + 62],
                                                    tempLogBuf[iter + 63], tempLogBuf[iter + 64], tempLogBuf[iter + 65],
                                                    tempLogBuf[iter + 66], tempLogBuf[iter + 67]);
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            break;
                        case 2: // idle time
                            deviceStats->sasStatistics.idleTimeIntervals.isSupported  = true;
                            deviceStats->sasStatistics.idleTimeIntervals.isValueValid = true;
                            deviceStats->sasStatistics.idleTimeIntervals.thresholdNotificationEnabled =
                                tempLogBuf[iter + 2] & BIT4; // ETC bit
                            if (tempLogBuf[iter + 2] & BIT4)
                            {
                                switch ((tempLogBuf[iter + 2] & (BIT2 | BIT3)) >> 2)
                                {
                                case 3:
                                    deviceStats->sasStatistics.idleTimeIntervals.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_GREATER;
                                    break;
                                case 2:
                                    deviceStats->sasStatistics.idleTimeIntervals.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL;
                                    break;
                                case 1:
                                    deviceStats->sasStatistics.idleTimeIntervals.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL;
                                    break;
                                case 0:
                                default:
                                    deviceStats->sasStatistics.idleTimeIntervals.threshType =
                                        THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE;
                                    break;
                                }
                            }
                            deviceStats->sasStatistics.idleTimeIntervals.statisticValue =
                                M_BytesTo8ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6],
                                                    tempLogBuf[iter + 7], tempLogBuf[iter + 8], tempLogBuf[iter + 9],
                                                    tempLogBuf[iter + 10], tempLogBuf[iter + 11]);
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            break;
                        case 3: // time interval
                            deviceStats->sasStatistics.timeIntervalDescriptor.isSupported  = true;
                            deviceStats->sasStatistics.timeIntervalDescriptor.isValueValid = true;
                            deviceStats->sasStatistics.timeIntervalDescriptor.thresholdNotificationEnabled =
                                tempLogBuf[iter + 2] & BIT4; // ETC bit
                            if (tempLogBuf[iter + 2] & BIT4)
                            {
                                switch ((tempLogBuf[iter + 2] & (BIT2 | BIT3)) >> 2)
                                {
                                case 3:
                                    deviceStats->sasStatistics.timeIntervalDescriptor.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_GREATER;
                                    break;
                                case 2:
                                    deviceStats->sasStatistics.timeIntervalDescriptor.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL;
                                    break;
                                case 1:
                                    deviceStats->sasStatistics.timeIntervalDescriptor.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL;
                                    break;
                                case 0:
                                default:
                                    deviceStats->sasStatistics.timeIntervalDescriptor.threshType =
                                        THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE;
                                    break;
                                }
                            }
                            deviceStats->sasStatistics.timeIntervalDescriptor.statisticValue =
                                M_BytesTo8ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6],
                                                    tempLogBuf[iter + 7], tempLogBuf[iter + 8], tempLogBuf[iter + 9],
                                                    tempLogBuf[iter + 10], tempLogBuf[iter + 11]);
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            break;
                        case 4: // force unit access statistics and performance
                            if (tempLogBuf[iter + 2] & BIT4)
                            {
                                deviceStats->sasStatistics.numberOfReadFUACommands.thresholdNotificationEnabled =
                                    true; // ETC bit
                                deviceStats->sasStatistics.numberOfWriteFUACommands.thresholdNotificationEnabled =
                                    true; // ETC bit
                                deviceStats->sasStatistics.numberOfReadFUANVCommands.thresholdNotificationEnabled =
                                    true; // ETC bit
                                deviceStats->sasStatistics.numberOfWriteFUANVCommands.thresholdNotificationEnabled =
                                    true; // ETC bit
                                deviceStats->sasStatistics.readFUACommandProcessingIntervals
                                    .thresholdNotificationEnabled = true; // ETC bit
                                deviceStats->sasStatistics.writeFUACommandProcessingIntervals
                                    .thresholdNotificationEnabled = true; // ETC bit
                                deviceStats->sasStatistics.readFUANVCommandProcessingIntervals
                                    .thresholdNotificationEnabled = true; // ETC bit
                                deviceStats->sasStatistics.writeFUANVCommandProcessingIntervals
                                    .thresholdNotificationEnabled = true; // ETC bit
                                switch ((tempLogBuf[iter + 2] & (BIT2 | BIT3)) >> 2)
                                {
                                case 3:
                                    deviceStats->sasStatistics.numberOfReadFUACommands.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_GREATER;
                                    deviceStats->sasStatistics.numberOfWriteFUACommands.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_GREATER;
                                    deviceStats->sasStatistics.numberOfReadFUANVCommands.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_GREATER;
                                    deviceStats->sasStatistics.numberOfWriteFUANVCommands.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_GREATER;
                                    deviceStats->sasStatistics.readFUACommandProcessingIntervals.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_GREATER;
                                    deviceStats->sasStatistics.writeFUACommandProcessingIntervals.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_GREATER;
                                    deviceStats->sasStatistics.readFUANVCommandProcessingIntervals.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_GREATER;
                                    deviceStats->sasStatistics.writeFUANVCommandProcessingIntervals.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_GREATER;
                                    break;
                                case 2:
                                    deviceStats->sasStatistics.numberOfReadFUACommands.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL;
                                    deviceStats->sasStatistics.numberOfWriteFUACommands.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL;
                                    deviceStats->sasStatistics.numberOfReadFUANVCommands.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL;
                                    deviceStats->sasStatistics.numberOfWriteFUANVCommands.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL;
                                    deviceStats->sasStatistics.readFUACommandProcessingIntervals.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL;
                                    deviceStats->sasStatistics.writeFUACommandProcessingIntervals.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL;
                                    deviceStats->sasStatistics.readFUANVCommandProcessingIntervals.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL;
                                    deviceStats->sasStatistics.writeFUANVCommandProcessingIntervals.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL;
                                    break;
                                case 1:
                                    deviceStats->sasStatistics.numberOfReadFUACommands.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL;
                                    deviceStats->sasStatistics.numberOfWriteFUACommands.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL;
                                    deviceStats->sasStatistics.numberOfReadFUANVCommands.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL;
                                    deviceStats->sasStatistics.numberOfWriteFUANVCommands.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL;
                                    deviceStats->sasStatistics.readFUACommandProcessingIntervals.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL;
                                    deviceStats->sasStatistics.writeFUACommandProcessingIntervals.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL;
                                    deviceStats->sasStatistics.readFUANVCommandProcessingIntervals.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL;
                                    deviceStats->sasStatistics.writeFUANVCommandProcessingIntervals.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL;
                                    break;
                                case 0:
                                default:
                                    deviceStats->sasStatistics.numberOfReadFUACommands.threshType =
                                        THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE;
                                    deviceStats->sasStatistics.numberOfWriteFUACommands.threshType =
                                        THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE;
                                    deviceStats->sasStatistics.numberOfReadFUANVCommands.threshType =
                                        THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE;
                                    deviceStats->sasStatistics.numberOfWriteFUANVCommands.threshType =
                                        THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE;
                                    deviceStats->sasStatistics.readFUACommandProcessingIntervals.threshType =
                                        THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE;
                                    deviceStats->sasStatistics.writeFUACommandProcessingIntervals.threshType =
                                        THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE;
                                    deviceStats->sasStatistics.readFUANVCommandProcessingIntervals.threshType =
                                        THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE;
                                    deviceStats->sasStatistics.writeFUANVCommandProcessingIntervals.threshType =
                                        THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE;
                                    break;
                                }
                            }
                            // number of read fua commands
                            deviceStats->sasStatistics.numberOfReadFUACommands.isSupported  = true;
                            deviceStats->sasStatistics.numberOfReadFUACommands.isValueValid = true;
                            deviceStats->sasStatistics.numberOfReadFUACommands.statisticValue =
                                M_BytesTo8ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6],
                                                    tempLogBuf[iter + 7], tempLogBuf[iter + 8], tempLogBuf[iter + 9],
                                                    tempLogBuf[iter + 10], tempLogBuf[iter + 11]);
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            // number of write fua commands
                            deviceStats->sasStatistics.numberOfWriteFUACommands.isSupported  = true;
                            deviceStats->sasStatistics.numberOfWriteFUACommands.isValueValid = true;
                            deviceStats->sasStatistics.numberOfWriteFUACommands.statisticValue =
                                M_BytesTo8ByteValue(tempLogBuf[iter + 12], tempLogBuf[iter + 13], tempLogBuf[iter + 14],
                                                    tempLogBuf[iter + 15], tempLogBuf[iter + 16], tempLogBuf[iter + 17],
                                                    tempLogBuf[iter + 18], tempLogBuf[iter + 19]);
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            // number of read fua_nv commands
                            deviceStats->sasStatistics.numberOfReadFUANVCommands.isSupported  = true;
                            deviceStats->sasStatistics.numberOfReadFUANVCommands.isValueValid = true;
                            deviceStats->sasStatistics.numberOfReadFUANVCommands.statisticValue =
                                M_BytesTo8ByteValue(tempLogBuf[iter + 20], tempLogBuf[iter + 21], tempLogBuf[iter + 22],
                                                    tempLogBuf[iter + 23], tempLogBuf[iter + 24], tempLogBuf[iter + 25],
                                                    tempLogBuf[iter + 26], tempLogBuf[iter + 27]);
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            // number of write fua_nv commands
                            deviceStats->sasStatistics.numberOfWriteFUANVCommands.isSupported  = true;
                            deviceStats->sasStatistics.numberOfWriteFUANVCommands.isValueValid = true;
                            deviceStats->sasStatistics.numberOfWriteFUANVCommands.statisticValue =
                                M_BytesTo8ByteValue(tempLogBuf[iter + 28], tempLogBuf[iter + 29], tempLogBuf[iter + 30],
                                                    tempLogBuf[iter + 31], tempLogBuf[iter + 32], tempLogBuf[iter + 33],
                                                    tempLogBuf[iter + 34], tempLogBuf[iter + 35]);
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            // read fua command processing intervals
                            deviceStats->sasStatistics.readFUACommandProcessingIntervals.isSupported  = true;
                            deviceStats->sasStatistics.readFUACommandProcessingIntervals.isValueValid = true;
                            deviceStats->sasStatistics.readFUACommandProcessingIntervals.statisticValue =
                                M_BytesTo8ByteValue(tempLogBuf[iter + 36], tempLogBuf[iter + 37], tempLogBuf[iter + 38],
                                                    tempLogBuf[iter + 39], tempLogBuf[iter + 40], tempLogBuf[iter + 41],
                                                    tempLogBuf[iter + 42], tempLogBuf[iter + 43]);
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            // write fua command processing intervals
                            deviceStats->sasStatistics.writeFUACommandProcessingIntervals.isSupported  = true;
                            deviceStats->sasStatistics.writeFUACommandProcessingIntervals.isValueValid = true;
                            deviceStats->sasStatistics.writeFUACommandProcessingIntervals.statisticValue =
                                M_BytesTo8ByteValue(tempLogBuf[iter + 44], tempLogBuf[iter + 45], tempLogBuf[iter + 46],
                                                    tempLogBuf[iter + 47], tempLogBuf[iter + 48], tempLogBuf[iter + 49],
                                                    tempLogBuf[iter + 50], tempLogBuf[iter + 51]);
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            // read fua_nv command processing intervals
                            deviceStats->sasStatistics.readFUANVCommandProcessingIntervals.isSupported  = true;
                            deviceStats->sasStatistics.readFUANVCommandProcessingIntervals.isValueValid = true;
                            deviceStats->sasStatistics.readFUANVCommandProcessingIntervals.statisticValue =
                                M_BytesTo8ByteValue(tempLogBuf[iter + 52], tempLogBuf[iter + 53], tempLogBuf[iter + 54],
                                                    tempLogBuf[iter + 55], tempLogBuf[iter + 56], tempLogBuf[iter + 57],
                                                    tempLogBuf[iter + 58], tempLogBuf[iter + 59]);
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            // write fua_nv command processing intervals
                            deviceStats->sasStatistics.writeFUANVCommandProcessingIntervals.isSupported  = true;
                            deviceStats->sasStatistics.writeFUANVCommandProcessingIntervals.isValueValid = true;
                            deviceStats->sasStatistics.writeFUANVCommandProcessingIntervals.statisticValue =
                                M_BytesTo8ByteValue(tempLogBuf[iter + 60], tempLogBuf[iter + 61], tempLogBuf[iter + 62],
                                                    tempLogBuf[iter + 63], tempLogBuf[iter + 64], tempLogBuf[iter + 65],
                                                    tempLogBuf[iter + 66], tempLogBuf[iter + 67]);
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
                    // thresholds
                    safe_memset(tempLogBuf, LEGACY_DRIVE_SEC_SIZE, 0, LEGACY_DRIVE_SEC_SIZE);
                    if (SUCCESS == scsi_Log_Sense_Cmd(device, false, LPC_THRESHOLD_VALUES, pageCode, subpageCode,
                                                      0x0001, tempLogBuf, LEGACY_DRIVE_SEC_SIZE))
                    {
                        pageLength      = M_BytesTo2ByteValue(tempLogBuf[2], tempLogBuf[3]);
                        parameterLength = 0;
                        // loop through the data and gather the data from each parameter we care about getting.
                        for (uint16_t iter = UINT16_C(4); iter < pageLength && iter < LEGACY_DRIVE_SEC_SIZE;
                             iter += (C_CAST(uint16_t, parameterLength + UINT16_C(4))))
                        {
                            uint16_t parameterCode = M_BytesTo2ByteValue(tempLogBuf[iter], tempLogBuf[iter + 1]);
                            parameterLength        = tempLogBuf[iter + 3];
                            switch (parameterCode)
                            {
                            case 1: // general access statistics and performance
                                deviceStats->sasStatistics.numberOfReadCommands.supportsNotification             = true;
                                deviceStats->sasStatistics.numberOfWriteCommands.supportsNotification            = true;
                                deviceStats->sasStatistics.numberOfLogicalBlocksReceived.supportsNotification    = true;
                                deviceStats->sasStatistics.numberOfLogicalBlocksTransmitted.supportsNotification = true;
                                deviceStats->sasStatistics.readCommandProcessingIntervals.supportsNotification   = true;
                                deviceStats->sasStatistics.writeCommandProcessingIntervals.supportsNotification  = true;
                                deviceStats->sasStatistics.weightedNumberOfReadCommandsPlusWriteCommands
                                    .supportsNotification = true;
                                deviceStats->sasStatistics.weightedReadCommandProcessingPlusWriteCommandProcessing
                                    .supportsNotification = true;
                                if (tempLogBuf[iter + 2] & BIT4)
                                {
                                    // number of read commands
                                    deviceStats->sasStatistics.numberOfReadCommands.isThresholdValid = true;
                                    deviceStats->sasStatistics.numberOfReadCommands.threshold = M_BytesTo8ByteValue(
                                        tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6],
                                        tempLogBuf[iter + 7], tempLogBuf[iter + 8], tempLogBuf[iter + 9],
                                        tempLogBuf[iter + 10], tempLogBuf[iter + 11]);
                                    scsi_Threshold_Comparison(&deviceStats->sasStatistics.numberOfReadCommands);
                                    // number of write commands
                                    deviceStats->sasStatistics.numberOfWriteCommands.isThresholdValid = true;
                                    deviceStats->sasStatistics.numberOfWriteCommands.threshold = M_BytesTo8ByteValue(
                                        tempLogBuf[iter + 12], tempLogBuf[iter + 13], tempLogBuf[iter + 14],
                                        tempLogBuf[iter + 15], tempLogBuf[iter + 16], tempLogBuf[iter + 17],
                                        tempLogBuf[iter + 18], tempLogBuf[iter + 19]);
                                    scsi_Threshold_Comparison(&deviceStats->sasStatistics.numberOfWriteCommands);
                                    // number of logical blocks received
                                    deviceStats->sasStatistics.numberOfLogicalBlocksReceived.isThresholdValid = true;
                                    deviceStats->sasStatistics.numberOfLogicalBlocksReceived.threshold =
                                        M_BytesTo8ByteValue(tempLogBuf[iter + 20], tempLogBuf[iter + 21],
                                                            tempLogBuf[iter + 22], tempLogBuf[iter + 23],
                                                            tempLogBuf[iter + 24], tempLogBuf[iter + 25],
                                                            tempLogBuf[iter + 26], tempLogBuf[iter + 27]);
                                    scsi_Threshold_Comparison(
                                        &deviceStats->sasStatistics.numberOfLogicalBlocksReceived);
                                    // number of logical blocks transmitted
                                    deviceStats->sasStatistics.numberOfLogicalBlocksTransmitted.isThresholdValid = true;
                                    deviceStats->sasStatistics.numberOfLogicalBlocksTransmitted.threshold =
                                        M_BytesTo8ByteValue(tempLogBuf[iter + 28], tempLogBuf[iter + 29],
                                                            tempLogBuf[iter + 30], tempLogBuf[iter + 31],
                                                            tempLogBuf[iter + 32], tempLogBuf[iter + 33],
                                                            tempLogBuf[iter + 34], tempLogBuf[iter + 35]);
                                    scsi_Threshold_Comparison(
                                        &deviceStats->sasStatistics.numberOfLogicalBlocksTransmitted);
                                    // read command processing intervals
                                    deviceStats->sasStatistics.readCommandProcessingIntervals.isThresholdValid = true;
                                    deviceStats->sasStatistics.readCommandProcessingIntervals.threshold =
                                        M_BytesTo8ByteValue(tempLogBuf[iter + 36], tempLogBuf[iter + 37],
                                                            tempLogBuf[iter + 38], tempLogBuf[iter + 39],
                                                            tempLogBuf[iter + 40], tempLogBuf[iter + 41],
                                                            tempLogBuf[iter + 42], tempLogBuf[iter + 43]);
                                    scsi_Threshold_Comparison(
                                        &deviceStats->sasStatistics.readCommandProcessingIntervals);
                                    // write command processing intervals
                                    deviceStats->sasStatistics.writeCommandProcessingIntervals.isThresholdValid = true;
                                    deviceStats->sasStatistics.writeCommandProcessingIntervals.threshold =
                                        M_BytesTo8ByteValue(tempLogBuf[iter + 44], tempLogBuf[iter + 45],
                                                            tempLogBuf[iter + 46], tempLogBuf[iter + 47],
                                                            tempLogBuf[iter + 48], tempLogBuf[iter + 49],
                                                            tempLogBuf[iter + 50], tempLogBuf[iter + 51]);
                                    scsi_Threshold_Comparison(
                                        &deviceStats->sasStatistics.writeCommandProcessingIntervals);
                                    // weighted number of read commands plus write commansd
                                    deviceStats->sasStatistics.weightedNumberOfReadCommandsPlusWriteCommands
                                        .isThresholdValid = true;
                                    deviceStats->sasStatistics.weightedNumberOfReadCommandsPlusWriteCommands.threshold =
                                        M_BytesTo8ByteValue(tempLogBuf[iter + 52], tempLogBuf[iter + 53],
                                                            tempLogBuf[iter + 54], tempLogBuf[iter + 55],
                                                            tempLogBuf[iter + 56], tempLogBuf[iter + 57],
                                                            tempLogBuf[iter + 58], tempLogBuf[iter + 59]);
                                    scsi_Threshold_Comparison(
                                        &deviceStats->sasStatistics.weightedNumberOfReadCommandsPlusWriteCommands);
                                    // weighted number of read command processing plus write command processing
                                    deviceStats->sasStatistics.weightedReadCommandProcessingPlusWriteCommandProcessing
                                        .isThresholdValid = true;
                                    deviceStats->sasStatistics.weightedReadCommandProcessingPlusWriteCommandProcessing
                                        .threshold = M_BytesTo8ByteValue(tempLogBuf[iter + 60], tempLogBuf[iter + 61],
                                                                         tempLogBuf[iter + 62], tempLogBuf[iter + 63],
                                                                         tempLogBuf[iter + 64], tempLogBuf[iter + 65],
                                                                         tempLogBuf[iter + 66], tempLogBuf[iter + 67]);
                                    scsi_Threshold_Comparison(
                                        &deviceStats->sasStatistics
                                             .weightedReadCommandProcessingPlusWriteCommandProcessing);
                                }
                                break;
                            case 2: // idle time
                                deviceStats->sasStatistics.idleTimeIntervals.supportsNotification = true;
                                if (tempLogBuf[iter + 2] & BIT4)
                                {
                                    deviceStats->sasStatistics.idleTimeIntervals.isThresholdValid = true;
                                    deviceStats->sasStatistics.idleTimeIntervals.threshold        = M_BytesTo8ByteValue(
                                        tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6],
                                        tempLogBuf[iter + 7], tempLogBuf[iter + 8], tempLogBuf[iter + 9],
                                        tempLogBuf[iter + 10], tempLogBuf[iter + 11]);
                                    scsi_Threshold_Comparison(&deviceStats->sasStatistics.idleTimeIntervals);
                                }
                                break;
                            case 3: // time interval
                                deviceStats->sasStatistics.timeIntervalDescriptor.supportsNotification = true;
                                if (tempLogBuf[iter + 2] & BIT4)
                                {
                                    deviceStats->sasStatistics.timeIntervalDescriptor.isThresholdValid = true;
                                    deviceStats->sasStatistics.timeIntervalDescriptor.threshold = M_BytesTo8ByteValue(
                                        tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6],
                                        tempLogBuf[iter + 7], tempLogBuf[iter + 8], tempLogBuf[iter + 9],
                                        tempLogBuf[iter + 10], tempLogBuf[iter + 11]);
                                    scsi_Threshold_Comparison(&deviceStats->sasStatistics.timeIntervalDescriptor);
                                }
                                break;
                            case 4: // force unit access statistics and performance
                                deviceStats->sasStatistics.numberOfReadFUACommands.supportsNotification    = true;
                                deviceStats->sasStatistics.numberOfWriteFUACommands.supportsNotification   = true;
                                deviceStats->sasStatistics.numberOfReadFUANVCommands.supportsNotification  = true;
                                deviceStats->sasStatistics.numberOfWriteFUANVCommands.supportsNotification = true;
                                deviceStats->sasStatistics.readFUACommandProcessingIntervals.supportsNotification =
                                    true;
                                deviceStats->sasStatistics.writeFUACommandProcessingIntervals.supportsNotification =
                                    true;
                                deviceStats->sasStatistics.readFUANVCommandProcessingIntervals.supportsNotification =
                                    true;
                                deviceStats->sasStatistics.writeFUANVCommandProcessingIntervals.supportsNotification =
                                    true;
                                if (tempLogBuf[iter + 2] & BIT4)
                                {
                                    // number of read fua commands
                                    deviceStats->sasStatistics.numberOfReadFUACommands.isThresholdValid = true;
                                    deviceStats->sasStatistics.numberOfReadFUACommands.threshold = M_BytesTo8ByteValue(
                                        tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6],
                                        tempLogBuf[iter + 7], tempLogBuf[iter + 8], tempLogBuf[iter + 9],
                                        tempLogBuf[iter + 10], tempLogBuf[iter + 11]);
                                    scsi_Threshold_Comparison(&deviceStats->sasStatistics.numberOfReadFUACommands);
                                    // number of write fua commands
                                    deviceStats->sasStatistics.numberOfWriteFUACommands.isThresholdValid = true;
                                    deviceStats->sasStatistics.numberOfWriteFUACommands.threshold = M_BytesTo8ByteValue(
                                        tempLogBuf[iter + 12], tempLogBuf[iter + 13], tempLogBuf[iter + 14],
                                        tempLogBuf[iter + 15], tempLogBuf[iter + 16], tempLogBuf[iter + 17],
                                        tempLogBuf[iter + 18], tempLogBuf[iter + 19]);
                                    scsi_Threshold_Comparison(&deviceStats->sasStatistics.numberOfWriteFUACommands);
                                    // number of read fua_nv commands
                                    deviceStats->sasStatistics.numberOfReadFUANVCommands.isThresholdValid = true;
                                    deviceStats->sasStatistics.numberOfReadFUANVCommands.threshold =
                                        M_BytesTo8ByteValue(tempLogBuf[iter + 20], tempLogBuf[iter + 21],
                                                            tempLogBuf[iter + 22], tempLogBuf[iter + 23],
                                                            tempLogBuf[iter + 24], tempLogBuf[iter + 25],
                                                            tempLogBuf[iter + 26], tempLogBuf[iter + 27]);
                                    scsi_Threshold_Comparison(&deviceStats->sasStatistics.numberOfReadFUANVCommands);
                                    // number of write fua_nv commands
                                    deviceStats->sasStatistics.numberOfWriteFUANVCommands.isThresholdValid = true;
                                    deviceStats->sasStatistics.numberOfWriteFUANVCommands.threshold =
                                        M_BytesTo8ByteValue(tempLogBuf[iter + 28], tempLogBuf[iter + 29],
                                                            tempLogBuf[iter + 30], tempLogBuf[iter + 31],
                                                            tempLogBuf[iter + 32], tempLogBuf[iter + 33],
                                                            tempLogBuf[iter + 34], tempLogBuf[iter + 35]);
                                    scsi_Threshold_Comparison(&deviceStats->sasStatistics.numberOfWriteFUANVCommands);
                                    // read fua command processing intervals
                                    deviceStats->sasStatistics.readFUACommandProcessingIntervals.isThresholdValid =
                                        true;
                                    deviceStats->sasStatistics.readFUACommandProcessingIntervals.threshold =
                                        M_BytesTo8ByteValue(tempLogBuf[iter + 36], tempLogBuf[iter + 37],
                                                            tempLogBuf[iter + 38], tempLogBuf[iter + 39],
                                                            tempLogBuf[iter + 40], tempLogBuf[iter + 41],
                                                            tempLogBuf[iter + 42], tempLogBuf[iter + 43]);
                                    scsi_Threshold_Comparison(
                                        &deviceStats->sasStatistics.readFUACommandProcessingIntervals);
                                    // write fua command processing intervals
                                    deviceStats->sasStatistics.writeFUACommandProcessingIntervals.isThresholdValid =
                                        true;
                                    deviceStats->sasStatistics.writeFUACommandProcessingIntervals.threshold =
                                        M_BytesTo8ByteValue(tempLogBuf[iter + 44], tempLogBuf[iter + 45],
                                                            tempLogBuf[iter + 46], tempLogBuf[iter + 47],
                                                            tempLogBuf[iter + 48], tempLogBuf[iter + 49],
                                                            tempLogBuf[iter + 50], tempLogBuf[iter + 51]);
                                    scsi_Threshold_Comparison(
                                        &deviceStats->sasStatistics.writeFUACommandProcessingIntervals);
                                    // read fua_nv command processing intervals
                                    deviceStats->sasStatistics.readFUANVCommandProcessingIntervals.isThresholdValid =
                                        true;
                                    deviceStats->sasStatistics.readFUANVCommandProcessingIntervals.threshold =
                                        M_BytesTo8ByteValue(tempLogBuf[iter + 52], tempLogBuf[iter + 53],
                                                            tempLogBuf[iter + 54], tempLogBuf[iter + 55],
                                                            tempLogBuf[iter + 56], tempLogBuf[iter + 57],
                                                            tempLogBuf[iter + 58], tempLogBuf[iter + 59]);
                                    scsi_Threshold_Comparison(
                                        &deviceStats->sasStatistics.readFUANVCommandProcessingIntervals);
                                    // write fua_nv command processing intervals
                                    deviceStats->sasStatistics.writeFUANVCommandProcessingIntervals.isThresholdValid =
                                        true;
                                    deviceStats->sasStatistics.writeFUANVCommandProcessingIntervals.threshold =
                                        M_BytesTo8ByteValue(tempLogBuf[iter + 60], tempLogBuf[iter + 61],
                                                            tempLogBuf[iter + 62], tempLogBuf[iter + 63],
                                                            tempLogBuf[iter + 64], tempLogBuf[iter + 65],
                                                            tempLogBuf[iter + 66], tempLogBuf[iter + 67]);
                                    scsi_Threshold_Comparison(
                                        &deviceStats->sasStatistics.writeFUANVCommandProcessingIntervals);
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
                // group statistics (1 - 1f)
            case 0x20: // cache memory statistics
                safe_memset(tempLogBuf, LEGACY_DRIVE_SEC_SIZE, 0, LEGACY_DRIVE_SEC_SIZE);
                if (SUCCESS == scsi_Log_Sense_Cmd(device, false, LPC_CUMULATIVE_VALUES, pageCode, subpageCode, 0x0001,
                                                  tempLogBuf, LEGACY_DRIVE_SEC_SIZE))
                {
                    deviceStats->sasStatistics.cacheMemoryStatisticsSupported = true;
                    uint16_t pageLength      = M_BytesTo2ByteValue(tempLogBuf[2], tempLogBuf[3]);
                    uint8_t  parameterLength = UINT8_C(0);
                    // loop through the data and gather the data from each parameter we care about getting.
                    for (uint16_t iter = UINT16_C(4); iter < pageLength && iter < LEGACY_DRIVE_SEC_SIZE;
                         iter += (C_CAST(uint16_t, parameterLength + UINT16_C(4))))
                    {
                        uint16_t parameterCode = M_BytesTo2ByteValue(tempLogBuf[iter], tempLogBuf[iter + 1]);
                        parameterLength        = tempLogBuf[iter + 3];
                        switch (parameterCode)
                        {
                        case 1: // read cache memory hits
                            deviceStats->sasStatistics.readCacheMemoryHits.isSupported  = true;
                            deviceStats->sasStatistics.readCacheMemoryHits.isValueValid = true;
                            deviceStats->sasStatistics.readCacheMemoryHits.thresholdNotificationEnabled =
                                tempLogBuf[iter + 2] & BIT4; // ETC bit
                            if (tempLogBuf[iter + 2] & BIT4)
                            {
                                switch ((tempLogBuf[iter + 2] & (BIT2 | BIT3)) >> 2)
                                {
                                case 3:
                                    deviceStats->sasStatistics.readCacheMemoryHits.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_GREATER;
                                    break;
                                case 2:
                                    deviceStats->sasStatistics.readCacheMemoryHits.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL;
                                    break;
                                case 1:
                                    deviceStats->sasStatistics.readCacheMemoryHits.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL;
                                    break;
                                case 0:
                                default:
                                    deviceStats->sasStatistics.readCacheMemoryHits.threshType =
                                        THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE;
                                    break;
                                }
                            }
                            deviceStats->sasStatistics.readCacheMemoryHits.statisticValue = M_BytesTo4ByteValue(
                                0, tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            break;
                        case 2: // reads to cache memory
                            deviceStats->sasStatistics.readsToCacheMemory.isSupported  = true;
                            deviceStats->sasStatistics.readsToCacheMemory.isValueValid = true;
                            deviceStats->sasStatistics.readsToCacheMemory.thresholdNotificationEnabled =
                                tempLogBuf[iter + 2] & BIT4; // ETC bit
                            if (tempLogBuf[iter + 2] & BIT4)
                            {
                                switch ((tempLogBuf[iter + 2] & (BIT2 | BIT3)) >> 2)
                                {
                                case 3:
                                    deviceStats->sasStatistics.readsToCacheMemory.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_GREATER;
                                    break;
                                case 2:
                                    deviceStats->sasStatistics.readsToCacheMemory.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL;
                                    break;
                                case 1:
                                    deviceStats->sasStatistics.readsToCacheMemory.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL;
                                    break;
                                case 0:
                                default:
                                    deviceStats->sasStatistics.readsToCacheMemory.threshType =
                                        THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE;
                                    break;
                                }
                            }
                            deviceStats->sasStatistics.readsToCacheMemory.statisticValue = M_BytesTo4ByteValue(
                                0, tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            break;
                        case 3: // write cache memory hits
                            deviceStats->sasStatistics.writeCacheMemoryHits.isSupported  = true;
                            deviceStats->sasStatistics.writeCacheMemoryHits.isValueValid = true;
                            deviceStats->sasStatistics.writeCacheMemoryHits.thresholdNotificationEnabled =
                                tempLogBuf[iter + 2] & BIT4; // ETC bit
                            if (tempLogBuf[iter + 2] & BIT4)
                            {
                                switch ((tempLogBuf[iter + 2] & (BIT2 | BIT3)) >> 2)
                                {
                                case 3:
                                    deviceStats->sasStatistics.writeCacheMemoryHits.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_GREATER;
                                    break;
                                case 2:
                                    deviceStats->sasStatistics.writeCacheMemoryHits.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL;
                                    break;
                                case 1:
                                    deviceStats->sasStatistics.writeCacheMemoryHits.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL;
                                    break;
                                case 0:
                                default:
                                    deviceStats->sasStatistics.writeCacheMemoryHits.threshType =
                                        THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE;
                                    break;
                                }
                            }
                            deviceStats->sasStatistics.writeCacheMemoryHits.statisticValue = M_BytesTo4ByteValue(
                                0, tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            break;
                        case 4: // writes from cache memory
                            deviceStats->sasStatistics.writesFromCacheMemory.isSupported  = true;
                            deviceStats->sasStatistics.writesFromCacheMemory.isValueValid = true;
                            deviceStats->sasStatistics.writesFromCacheMemory.thresholdNotificationEnabled =
                                tempLogBuf[iter + 2] & BIT4; // ETC bit
                            if (tempLogBuf[iter + 2] & BIT4)
                            {
                                switch ((tempLogBuf[iter + 2] & (BIT2 | BIT3)) >> 2)
                                {
                                case 3:
                                    deviceStats->sasStatistics.writesFromCacheMemory.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_GREATER;
                                    break;
                                case 2:
                                    deviceStats->sasStatistics.writesFromCacheMemory.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL;
                                    break;
                                case 1:
                                    deviceStats->sasStatistics.writesFromCacheMemory.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL;
                                    break;
                                case 0:
                                default:
                                    deviceStats->sasStatistics.writesFromCacheMemory.threshType =
                                        THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE;
                                    break;
                                }
                            }
                            deviceStats->sasStatistics.writesFromCacheMemory.statisticValue = M_BytesTo4ByteValue(
                                0, tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            break;
                        case 5: // time from last hard reset
                            deviceStats->sasStatistics.timeFromLastHardReset.isSupported  = true;
                            deviceStats->sasStatistics.timeFromLastHardReset.isValueValid = true;
                            deviceStats->sasStatistics.timeFromLastHardReset.thresholdNotificationEnabled =
                                tempLogBuf[iter + 2] & BIT4; // ETC bit
                            if (tempLogBuf[iter + 2] & BIT4)
                            {
                                switch ((tempLogBuf[iter + 2] & (BIT2 | BIT3)) >> 2)
                                {
                                case 3:
                                    deviceStats->sasStatistics.timeFromLastHardReset.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_GREATER;
                                    break;
                                case 2:
                                    deviceStats->sasStatistics.timeFromLastHardReset.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL;
                                    break;
                                case 1:
                                    deviceStats->sasStatistics.timeFromLastHardReset.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL;
                                    break;
                                case 0:
                                default:
                                    deviceStats->sasStatistics.timeFromLastHardReset.threshType =
                                        THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE;
                                    break;
                                }
                            }
                            deviceStats->sasStatistics.timeFromLastHardReset.statisticValue = M_BytesTo4ByteValue(
                                0, tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            break;
                        case 6: // time interval
                            deviceStats->sasStatistics.cacheTimeInterval.isSupported  = true;
                            deviceStats->sasStatistics.cacheTimeInterval.isValueValid = true;
                            deviceStats->sasStatistics.cacheTimeInterval.thresholdNotificationEnabled =
                                tempLogBuf[iter + 2] & BIT4; // ETC bit
                            if (tempLogBuf[iter + 2] & BIT4)
                            {
                                switch ((tempLogBuf[iter + 2] & (BIT2 | BIT3)) >> 2)
                                {
                                case 3:
                                    deviceStats->sasStatistics.cacheTimeInterval.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_GREATER;
                                    break;
                                case 2:
                                    deviceStats->sasStatistics.cacheTimeInterval.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL;
                                    break;
                                case 1:
                                    deviceStats->sasStatistics.cacheTimeInterval.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL;
                                    break;
                                case 0:
                                default:
                                    deviceStats->sasStatistics.cacheTimeInterval.threshType =
                                        THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE;
                                    break;
                                }
                            }
                            deviceStats->sasStatistics.cacheTimeInterval.statisticValue = M_BytesTo4ByteValue(
                                0, tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
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
                    // thresholds
                    safe_memset(tempLogBuf, LEGACY_DRIVE_SEC_SIZE, 0, LEGACY_DRIVE_SEC_SIZE);
                    if (SUCCESS == scsi_Log_Sense_Cmd(device, false, LPC_THRESHOLD_VALUES, pageCode, subpageCode,
                                                      0x0001, tempLogBuf, LEGACY_DRIVE_SEC_SIZE))
                    {
                        pageLength      = M_BytesTo2ByteValue(tempLogBuf[2], tempLogBuf[3]);
                        parameterLength = 0;
                        // loop through the data and gather the data from each parameter we care about getting.
                        for (uint16_t iter = UINT16_C(4); iter < pageLength && iter < LEGACY_DRIVE_SEC_SIZE;
                             iter += (C_CAST(uint16_t, parameterLength + UINT16_C(4))))
                        {
                            uint16_t parameterCode = M_BytesTo2ByteValue(tempLogBuf[iter], tempLogBuf[iter + 1]);
                            parameterLength        = tempLogBuf[iter + 3];
                            switch (parameterCode)
                            {
                            case 1: // read cache memory hits
                                deviceStats->sasStatistics.readCacheMemoryHits.supportsNotification = true;
                                if (tempLogBuf[iter + 2] & BIT4)
                                {
                                    deviceStats->sasStatistics.readCacheMemoryHits.isThresholdValid = true;
                                    deviceStats->sasStatistics.readCacheMemoryHits.threshold = M_BytesTo4ByteValue(
                                        0, tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
                                    scsi_Threshold_Comparison(&deviceStats->sasStatistics.readCacheMemoryHits);
                                }
                                break;
                            case 2: // reads to cache memory
                                deviceStats->sasStatistics.readsToCacheMemory.supportsNotification = true;
                                if (tempLogBuf[iter + 2] & BIT4)
                                {
                                    deviceStats->sasStatistics.readsToCacheMemory.isThresholdValid = true;
                                    deviceStats->sasStatistics.readsToCacheMemory.threshold = M_BytesTo4ByteValue(
                                        0, tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
                                    scsi_Threshold_Comparison(&deviceStats->sasStatistics.readsToCacheMemory);
                                }
                                break;
                            case 3: // write cache memory hits
                                deviceStats->sasStatistics.writeCacheMemoryHits.supportsNotification = true;
                                if (tempLogBuf[iter + 2] & BIT4)
                                {
                                    deviceStats->sasStatistics.writeCacheMemoryHits.isThresholdValid = true;
                                    deviceStats->sasStatistics.writeCacheMemoryHits.threshold = M_BytesTo4ByteValue(
                                        0, tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
                                    scsi_Threshold_Comparison(&deviceStats->sasStatistics.writeCacheMemoryHits);
                                }
                                break;
                            case 4: // writes from cache memory
                                deviceStats->sasStatistics.writesFromCacheMemory.supportsNotification = true;
                                if (tempLogBuf[iter + 2] & BIT4)
                                {
                                    deviceStats->sasStatistics.writesFromCacheMemory.isThresholdValid = true;
                                    deviceStats->sasStatistics.writesFromCacheMemory.threshold = M_BytesTo4ByteValue(
                                        0, tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
                                    scsi_Threshold_Comparison(&deviceStats->sasStatistics.writesFromCacheMemory);
                                }
                                break;
                            case 5: // time from last hard reset
                                deviceStats->sasStatistics.timeFromLastHardReset.supportsNotification = true;
                                if (tempLogBuf[iter + 2] & BIT4)
                                {
                                    deviceStats->sasStatistics.timeFromLastHardReset.isThresholdValid = true;
                                    deviceStats->sasStatistics.timeFromLastHardReset.threshold = M_BytesTo4ByteValue(
                                        0, tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
                                    scsi_Threshold_Comparison(&deviceStats->sasStatistics.timeFromLastHardReset);
                                }
                                break;
                            case 6: // cache time interval
                                deviceStats->sasStatistics.cacheTimeInterval.supportsNotification = true;
                                if (tempLogBuf[iter + 2] & BIT4)
                                {
                                    deviceStats->sasStatistics.cacheTimeInterval.isThresholdValid = true;
                                    deviceStats->sasStatistics.cacheTimeInterval.threshold        = M_BytesTo4ByteValue(
                                        0, tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
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
                break;
            default:
                break;
            }
            break;
        case LP_ZONED_DEVICE_STATISTICS: // subpage 1
            switch (subpageCode)
            {
            case 0x01: // ZBD statistics
            {
                safe_memset(tempLogBuf, LEGACY_DRIVE_SEC_SIZE, 0, LEGACY_DRIVE_SEC_SIZE);
                if (SUCCESS == scsi_Log_Sense_Cmd(device, false, LPC_CUMULATIVE_VALUES, pageCode, subpageCode, 0x0001,
                                                  tempLogBuf, LEGACY_DRIVE_SEC_SIZE))
                {
                    deviceStats->sasStatistics.cacheMemoryStatisticsSupported = true;
                    uint16_t pageLength      = M_BytesTo2ByteValue(tempLogBuf[2], tempLogBuf[3]);
                    uint8_t  parameterLength = UINT8_C(0);
                    // loop through the data and gather the data from each parameter we care about getting.
                    for (uint16_t iter = UINT16_C(4); iter < pageLength && iter < LEGACY_DRIVE_SEC_SIZE;
                         iter += (C_CAST(uint16_t, parameterLength + UINT16_C(4))))
                    {
                        uint16_t parameterCode = M_BytesTo2ByteValue(tempLogBuf[iter], tempLogBuf[iter + 1]);
                        parameterLength        = tempLogBuf[iter + 3];
                        switch (parameterCode)
                        {
                        case 0: // maximum open zones
                            deviceStats->sasStatistics.maximumOpenZones.isSupported  = true;
                            deviceStats->sasStatistics.maximumOpenZones.isValueValid = true;
                            deviceStats->sasStatistics.maximumOpenZones.statisticValue =
                                M_BytesTo8ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6],
                                                    tempLogBuf[iter + 7], tempLogBuf[iter + 8], tempLogBuf[iter + 9],
                                                    tempLogBuf[iter + 10], tempLogBuf[iter + 11]);
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            break;
                        case 1: // maximum explicitly open zones
                            deviceStats->sasStatistics.maximumExplicitlyOpenZones.isSupported  = true;
                            deviceStats->sasStatistics.maximumExplicitlyOpenZones.isValueValid = true;
                            deviceStats->sasStatistics.maximumExplicitlyOpenZones.statisticValue =
                                M_BytesTo8ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6],
                                                    tempLogBuf[iter + 7], tempLogBuf[iter + 8], tempLogBuf[iter + 9],
                                                    tempLogBuf[iter + 10], tempLogBuf[iter + 11]);
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            break;
                        case 2: // maximum implicitly open zones
                            deviceStats->sasStatistics.maximumImplicitlyOpenZones.isSupported  = true;
                            deviceStats->sasStatistics.maximumImplicitlyOpenZones.isValueValid = true;
                            deviceStats->sasStatistics.maximumImplicitlyOpenZones.statisticValue =
                                M_BytesTo8ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6],
                                                    tempLogBuf[iter + 7], tempLogBuf[iter + 8], tempLogBuf[iter + 9],
                                                    tempLogBuf[iter + 10], tempLogBuf[iter + 11]);
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            break;
                        case 3: // minimum empty zones
                            deviceStats->sasStatistics.minimumEmptyZones.isSupported  = true;
                            deviceStats->sasStatistics.minimumEmptyZones.isValueValid = true;
                            deviceStats->sasStatistics.minimumEmptyZones.statisticValue =
                                M_BytesTo8ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6],
                                                    tempLogBuf[iter + 7], tempLogBuf[iter + 8], tempLogBuf[iter + 9],
                                                    tempLogBuf[iter + 10], tempLogBuf[iter + 11]);
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            break;
                        case 4: // maximum non-sequential zones
                            deviceStats->sasStatistics.maximumNonSequentialZones.isSupported  = true;
                            deviceStats->sasStatistics.maximumNonSequentialZones.isValueValid = true;
                            deviceStats->sasStatistics.maximumNonSequentialZones.statisticValue =
                                M_BytesTo8ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6],
                                                    tempLogBuf[iter + 7], tempLogBuf[iter + 8], tempLogBuf[iter + 9],
                                                    tempLogBuf[iter + 10], tempLogBuf[iter + 11]);
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            break;
                        case 5: // zones emptied
                            deviceStats->sasStatistics.zonesEmptied.isSupported  = true;
                            deviceStats->sasStatistics.zonesEmptied.isValueValid = true;
                            deviceStats->sasStatistics.zonesEmptied.statisticValue =
                                M_BytesTo8ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6],
                                                    tempLogBuf[iter + 7], tempLogBuf[iter + 8], tempLogBuf[iter + 9],
                                                    tempLogBuf[iter + 10], tempLogBuf[iter + 11]);
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            break;
                        case 6: // suboptimal write commands
                            deviceStats->sasStatistics.suboptimalWriteCommands.isSupported  = true;
                            deviceStats->sasStatistics.suboptimalWriteCommands.isValueValid = true;
                            deviceStats->sasStatistics.suboptimalWriteCommands.statisticValue =
                                M_BytesTo8ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6],
                                                    tempLogBuf[iter + 7], tempLogBuf[iter + 8], tempLogBuf[iter + 9],
                                                    tempLogBuf[iter + 10], tempLogBuf[iter + 11]);
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            break;
                        case 7: // commands exceeding optimal limit
                            deviceStats->sasStatistics.commandsExceedingOptimalLimit.isSupported  = true;
                            deviceStats->sasStatistics.commandsExceedingOptimalLimit.isValueValid = true;
                            deviceStats->sasStatistics.commandsExceedingOptimalLimit.statisticValue =
                                M_BytesTo8ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6],
                                                    tempLogBuf[iter + 7], tempLogBuf[iter + 8], tempLogBuf[iter + 9],
                                                    tempLogBuf[iter + 10], tempLogBuf[iter + 11]);
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            break;
                        case 8: // failed explicit opens
                            deviceStats->sasStatistics.failedExplicitOpens.isSupported  = true;
                            deviceStats->sasStatistics.failedExplicitOpens.isValueValid = true;
                            deviceStats->sasStatistics.failedExplicitOpens.statisticValue =
                                M_BytesTo8ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6],
                                                    tempLogBuf[iter + 7], tempLogBuf[iter + 8], tempLogBuf[iter + 9],
                                                    tempLogBuf[iter + 10], tempLogBuf[iter + 11]);
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            break;
                        case 9: // read rule violations
                            deviceStats->sasStatistics.readRuleViolations.isSupported  = true;
                            deviceStats->sasStatistics.readRuleViolations.isValueValid = true;
                            deviceStats->sasStatistics.readRuleViolations.statisticValue =
                                M_BytesTo8ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6],
                                                    tempLogBuf[iter + 7], tempLogBuf[iter + 8], tempLogBuf[iter + 9],
                                                    tempLogBuf[iter + 10], tempLogBuf[iter + 11]);
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            break;
                        case 10: // write rule violations
                            deviceStats->sasStatistics.writeRuleViolations.isSupported  = true;
                            deviceStats->sasStatistics.writeRuleViolations.isValueValid = true;
                            deviceStats->sasStatistics.writeRuleViolations.statisticValue =
                                M_BytesTo8ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6],
                                                    tempLogBuf[iter + 7], tempLogBuf[iter + 8], tempLogBuf[iter + 9],
                                                    tempLogBuf[iter + 10], tempLogBuf[iter + 11]);
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            break;
                        case 11: // Maximum implicitly open sequential or before required zones
                            deviceStats->sasStatistics.maxImplicitlyOpenSeqOrBeforeReqZones.isSupported  = true;
                            deviceStats->sasStatistics.maxImplicitlyOpenSeqOrBeforeReqZones.isValueValid = true;
                            deviceStats->sasStatistics.maxImplicitlyOpenSeqOrBeforeReqZones.statisticValue =
                                M_BytesTo8ByteValue(tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6],
                                                    tempLogBuf[iter + 7], tempLogBuf[iter + 8], tempLogBuf[iter + 9],
                                                    tempLogBuf[iter + 10], tempLogBuf[iter + 11]);
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
                // Thresholds are not defined/obsolete so no need to read them or attempt to read them.
            }
            break;
            default:
                break;
            }
            break;
        case LP_POWER_CONDITIONS_TRANSITIONS:
            switch (subpageCode)
            {
            case 0:
                safe_memset(tempLogBuf, LEGACY_DRIVE_SEC_SIZE, 0, LEGACY_DRIVE_SEC_SIZE);
                if (SUCCESS == scsi_Log_Sense_Cmd(device, false, LPC_CUMULATIVE_VALUES, pageCode, subpageCode, 0x0000,
                                                  tempLogBuf, LEGACY_DRIVE_SEC_SIZE))
                {
                    deviceStats->sasStatistics.powerConditionTransitionsSupported = true;
                    uint16_t pageLength      = M_BytesTo2ByteValue(tempLogBuf[2], tempLogBuf[3]);
                    uint8_t  parameterLength = UINT8_C(0);
                    // loop through the data and gather the data from each parameter we care about getting.
                    for (uint16_t iter = UINT16_C(4); iter < pageLength && iter < LEGACY_DRIVE_SEC_SIZE;
                         iter += (C_CAST(uint16_t, parameterLength + UINT16_C(4))))
                    {
                        uint16_t parameterCode = M_BytesTo2ByteValue(tempLogBuf[iter], tempLogBuf[iter + 1]);
                        parameterLength        = tempLogBuf[iter + 3];
                        switch (parameterCode)
                        {
                        case 1: // transitions to active
                            deviceStats->sasStatistics.transitionsToActive.isSupported  = true;
                            deviceStats->sasStatistics.transitionsToActive.isValueValid = true;
                            deviceStats->sasStatistics.transitionsToActive.thresholdNotificationEnabled =
                                tempLogBuf[iter + 2] & BIT4; // ETC bit
                            if (tempLogBuf[iter + 2] & BIT4)
                            {
                                switch ((tempLogBuf[iter + 2] & (BIT2 | BIT3)) >> 2)
                                {
                                case 3:
                                    deviceStats->sasStatistics.transitionsToActive.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_GREATER;
                                    break;
                                case 2:
                                    deviceStats->sasStatistics.transitionsToActive.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL;
                                    break;
                                case 1:
                                    deviceStats->sasStatistics.transitionsToActive.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL;
                                    break;
                                case 0:
                                default:
                                    deviceStats->sasStatistics.transitionsToActive.threshType =
                                        THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE;
                                    break;
                                }
                            }
                            deviceStats->sasStatistics.transitionsToActive.statisticValue = M_BytesTo4ByteValue(
                                tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            break;
                        case 2: // transitions to idle a
                            deviceStats->sasStatistics.transitionsToIdleA.isSupported  = true;
                            deviceStats->sasStatistics.transitionsToIdleA.isValueValid = true;
                            deviceStats->sasStatistics.transitionsToIdleA.thresholdNotificationEnabled =
                                tempLogBuf[iter + 2] & BIT4; // ETC bit
                            if (tempLogBuf[iter + 2] & BIT4)
                            {
                                switch ((tempLogBuf[iter + 2] & (BIT2 | BIT3)) >> 2)
                                {
                                case 3:
                                    deviceStats->sasStatistics.transitionsToIdleA.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_GREATER;
                                    break;
                                case 2:
                                    deviceStats->sasStatistics.transitionsToIdleA.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL;
                                    break;
                                case 1:
                                    deviceStats->sasStatistics.transitionsToIdleA.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL;
                                    break;
                                case 0:
                                default:
                                    deviceStats->sasStatistics.transitionsToIdleA.threshType =
                                        THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE;
                                    break;
                                }
                            }
                            deviceStats->sasStatistics.transitionsToIdleA.statisticValue = M_BytesTo4ByteValue(
                                tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            break;
                        case 3: // transitions to idle b
                            deviceStats->sasStatistics.transitionsToIdleB.isSupported  = true;
                            deviceStats->sasStatistics.transitionsToIdleB.isValueValid = true;
                            deviceStats->sasStatistics.transitionsToIdleB.thresholdNotificationEnabled =
                                tempLogBuf[iter + 2] & BIT4; // ETC bit
                            if (tempLogBuf[iter + 2] & BIT4)
                            {
                                switch ((tempLogBuf[iter + 2] & (BIT2 | BIT3)) >> 2)
                                {
                                case 3:
                                    deviceStats->sasStatistics.transitionsToIdleB.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_GREATER;
                                    break;
                                case 2:
                                    deviceStats->sasStatistics.transitionsToIdleB.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL;
                                    break;
                                case 1:
                                    deviceStats->sasStatistics.transitionsToIdleB.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL;
                                    break;
                                case 0:
                                default:
                                    deviceStats->sasStatistics.transitionsToIdleB.threshType =
                                        THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE;
                                    break;
                                }
                            }
                            deviceStats->sasStatistics.transitionsToIdleB.statisticValue = M_BytesTo4ByteValue(
                                tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            break;
                        case 4: // transitions to idle c
                            deviceStats->sasStatistics.transitionsToIdleC.isSupported  = true;
                            deviceStats->sasStatistics.transitionsToIdleC.isValueValid = true;
                            deviceStats->sasStatistics.transitionsToIdleC.thresholdNotificationEnabled =
                                tempLogBuf[iter + 2] & BIT4; // ETC bit
                            if (tempLogBuf[iter + 2] & BIT4)
                            {
                                switch ((tempLogBuf[iter + 2] & (BIT2 | BIT3)) >> 2)
                                {
                                case 3:
                                    deviceStats->sasStatistics.transitionsToIdleC.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_GREATER;
                                    break;
                                case 2:
                                    deviceStats->sasStatistics.transitionsToIdleC.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL;
                                    break;
                                case 1:
                                    deviceStats->sasStatistics.transitionsToIdleC.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL;
                                    break;
                                case 0:
                                default:
                                    deviceStats->sasStatistics.transitionsToIdleC.threshType =
                                        THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE;
                                    break;
                                }
                            }
                            deviceStats->sasStatistics.transitionsToIdleC.statisticValue = M_BytesTo4ByteValue(
                                tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            break;
                        case 8: // transitions to standby z
                            deviceStats->sasStatistics.transitionsToStandbyZ.isSupported  = true;
                            deviceStats->sasStatistics.transitionsToStandbyZ.isValueValid = true;
                            deviceStats->sasStatistics.transitionsToStandbyZ.thresholdNotificationEnabled =
                                tempLogBuf[iter + 2] & BIT4; // ETC bit
                            if (tempLogBuf[iter + 2] & BIT4)
                            {
                                switch ((tempLogBuf[iter + 2] & (BIT2 | BIT3)) >> 2)
                                {
                                case 3:
                                    deviceStats->sasStatistics.transitionsToStandbyZ.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_GREATER;
                                    break;
                                case 2:
                                    deviceStats->sasStatistics.transitionsToStandbyZ.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL;
                                    break;
                                case 1:
                                    deviceStats->sasStatistics.transitionsToStandbyZ.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL;
                                    break;
                                case 0:
                                default:
                                    deviceStats->sasStatistics.transitionsToStandbyZ.threshType =
                                        THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE;
                                    break;
                                }
                            }
                            deviceStats->sasStatistics.transitionsToStandbyZ.statisticValue = M_BytesTo4ByteValue(
                                tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
                            ++deviceStats->sasStatistics.statisticsPopulated;
                            break;
                        case 9: // transitions to standby y
                            deviceStats->sasStatistics.transitionsToStandbyY.isSupported  = true;
                            deviceStats->sasStatistics.transitionsToStandbyY.isValueValid = true;
                            deviceStats->sasStatistics.transitionsToStandbyY.thresholdNotificationEnabled =
                                tempLogBuf[iter + 2] & BIT4; // ETC bit
                            if (tempLogBuf[iter + 2] & BIT4)
                            {
                                switch ((tempLogBuf[iter + 2] & (BIT2 | BIT3)) >> 2)
                                {
                                case 3:
                                    deviceStats->sasStatistics.transitionsToStandbyY.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_GREATER;
                                    break;
                                case 2:
                                    deviceStats->sasStatistics.transitionsToStandbyY.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL;
                                    break;
                                case 1:
                                    deviceStats->sasStatistics.transitionsToStandbyY.threshType =
                                        THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL;
                                    break;
                                case 0:
                                default:
                                    deviceStats->sasStatistics.transitionsToStandbyY.threshType =
                                        THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE;
                                    break;
                                }
                            }
                            deviceStats->sasStatistics.transitionsToStandbyY.statisticValue = M_BytesTo4ByteValue(
                                tempLogBuf[iter + 4], tempLogBuf[iter + 5], tempLogBuf[iter + 6], tempLogBuf[iter + 7]);
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
                break;
            default:
                break;
            }
            break;
        case LP_PROTOCOL_SPECIFIC_PORT:
            switch (subpageCode)
            {
            case 0:
                // NOTE: This page is currently setup for SAS SSP
                //       I am not aware of other transports implementing this page at this time - TJE
                // This page is read in a 64k size to make sure we get as much as possible in a single command.
                {
                    uint16_t protocolSpecificDataLength = UINT16_MAX;
                    uint8_t* protSpData =
                        M_REINTERPRET_CAST(uint8_t*, safe_calloc_aligned(protocolSpecificDataLength, sizeof(uint8_t),
                                                                         device->os_info.minimumAlignment));
                    if (protSpData != M_NULLPTR)
                    {
                        if (SUCCESS == scsi_Log_Sense_Cmd(device, false, LPC_CUMULATIVE_VALUES,
                                                          LP_PROTOCOL_SPECIFIC_PORT, 0, 0, protSpData,
                                                          protocolSpecificDataLength))
                        {
                            // mimimum page length for a SAS drive assuming only 1 port and 1 phy is 64B. Each
                            // additional port adds a minimum of another 60 bytes
                            uint32_t pageLength =
                                M_BytesTo2ByteValue(protSpData[2], protSpData[3]) + LOG_PAGE_HEADER_LENGTH;
                            uint16_t parameterLength = UINT16_C(4);
                            uint16_t portCounter     = UINT16_C(0);
                            for (uint32_t offset = UINT32_C(4);
                                 offset < pageLength && portCounter < SAS_STATISTICS_MAX_PORTS &&
                                 offset < protocolSpecificDataLength;
                                 offset += parameterLength + 4, ++portCounter)
                            {
                                uint16_t parameterCode =
                                    M_BytesTo2ByteValue(protSpData[offset + 0], protSpData[offset + 1]);
                                parameterLength =
                                    protSpData[offset +
                                               3]; // 4 bytes for the length of the header for the parameter code
                                if (parameterLength > 0)
                                {
                                    uint8_t protocolIdentifier = M_Nibble0(protSpData[offset + 4]);
                                    if (protocolIdentifier == SCSI_PROTOCOL_ID_SAS)
                                    {
                                        uint8_t  numberOfPhys        = protSpData[offset + 7];
                                        uint32_t phyOffset           = offset + 8;
                                        uint8_t  phyDescriptorLength = UINT8_C(0);
                                        uint8_t  phyCounter          = UINT8_C(0);
                                        deviceStats->sasStatistics.protocolSpecificStatisticsSupported = true;
                                        deviceStats->sasStatistics.protocolStatisticsType              = STAT_PROT_SAS;
                                        deviceStats->sasStatistics.sasProtStats
                                            .sasStatsPerPort[deviceStats->sasStatistics.sasProtStats.portCount]
                                            .portID = parameterCode;
                                        deviceStats->sasStatistics.sasProtStats
                                            .sasStatsPerPort[deviceStats->sasStatistics.sasProtStats.portCount]
                                            .sasProtStatsValid = true;
                                        for (uint8_t phyIter = UINT8_C(0);
                                             phyIter < numberOfPhys && phyOffset < pageLength &&
                                             phyCounter < SAS_STATISTICS_MAX_PHYS;
                                             ++phyIter, phyOffset += phyDescriptorLength + 4, ++phyCounter)
                                        {
                                            // now at the actual phy data, so we can read what we want to report
                                            phyDescriptorLength = protSpData[phyOffset + 3];
                                            if (phyDescriptorLength > 0)
                                            {
                                                deviceStats->sasStatistics.sasProtStats
                                                    .sasStatsPerPort[deviceStats->sasStatistics.sasProtStats.portCount]
                                                    .perPhy[deviceStats->sasStatistics.sasProtStats
                                                                .sasStatsPerPort[deviceStats->sasStatistics.sasProtStats
                                                                                     .portCount]
                                                                .phyCount]
                                                    .sasPhyStatsValid = true;
                                                deviceStats->sasStatistics.sasProtStats
                                                    .sasStatsPerPort[deviceStats->sasStatistics.sasProtStats.portCount]
                                                    .perPhy[deviceStats->sasStatistics.sasProtStats
                                                                .sasStatsPerPort[deviceStats->sasStatistics.sasProtStats
                                                                                     .portCount]
                                                                .phyCount]
                                                    .phyID = protSpData[phyOffset + 1];
                                                deviceStats->sasStatistics.sasProtStats
                                                    .sasStatsPerPort[deviceStats->sasStatistics.sasProtStats.portCount]
                                                    .perPhy[deviceStats->sasStatistics.sasProtStats
                                                                .sasStatsPerPort[deviceStats->sasStatistics.sasProtStats
                                                                                     .portCount]
                                                                .phyCount]
                                                    .invalidDWORDCount.isSupported = true;
                                                deviceStats->sasStatistics.sasProtStats
                                                    .sasStatsPerPort[deviceStats->sasStatistics.sasProtStats.portCount]
                                                    .perPhy[deviceStats->sasStatistics.sasProtStats
                                                                .sasStatsPerPort[deviceStats->sasStatistics.sasProtStats
                                                                                     .portCount]
                                                                .phyCount]
                                                    .invalidDWORDCount.isValueValid = true;
                                                deviceStats->sasStatistics.sasProtStats
                                                    .sasStatsPerPort[deviceStats->sasStatistics.sasProtStats.portCount]
                                                    .perPhy[deviceStats->sasStatistics.sasProtStats
                                                                .sasStatsPerPort[deviceStats->sasStatistics.sasProtStats
                                                                                     .portCount]
                                                                .phyCount]
                                                    .invalidDWORDCount.statisticValue = M_BytesTo4ByteValue(
                                                    protSpData[phyOffset + 32], protSpData[phyOffset + 33],
                                                    protSpData[phyOffset + 34], protSpData[phyOffset + 35]);
                                                ++deviceStats->sasStatistics.statisticsPopulated;
                                                deviceStats->sasStatistics.sasProtStats
                                                    .sasStatsPerPort[deviceStats->sasStatistics.sasProtStats.portCount]
                                                    .perPhy[deviceStats->sasStatistics.sasProtStats
                                                                .sasStatsPerPort[deviceStats->sasStatistics.sasProtStats
                                                                                     .portCount]
                                                                .phyCount]
                                                    .runningDisparityErrorCount.isSupported = true;
                                                deviceStats->sasStatistics.sasProtStats
                                                    .sasStatsPerPort[deviceStats->sasStatistics.sasProtStats.portCount]
                                                    .perPhy[deviceStats->sasStatistics.sasProtStats
                                                                .sasStatsPerPort[deviceStats->sasStatistics.sasProtStats
                                                                                     .portCount]
                                                                .phyCount]
                                                    .runningDisparityErrorCount.isValueValid = true;
                                                deviceStats->sasStatistics.sasProtStats
                                                    .sasStatsPerPort[deviceStats->sasStatistics.sasProtStats.portCount]
                                                    .perPhy[deviceStats->sasStatistics.sasProtStats
                                                                .sasStatsPerPort[deviceStats->sasStatistics.sasProtStats
                                                                                     .portCount]
                                                                .phyCount]
                                                    .runningDisparityErrorCount.statisticValue = M_BytesTo4ByteValue(
                                                    protSpData[phyOffset + 36], protSpData[phyOffset + 37],
                                                    protSpData[phyOffset + 38], protSpData[phyOffset + 39]);
                                                ++deviceStats->sasStatistics.statisticsPopulated;
                                                deviceStats->sasStatistics.sasProtStats
                                                    .sasStatsPerPort[deviceStats->sasStatistics.sasProtStats.portCount]
                                                    .perPhy[deviceStats->sasStatistics.sasProtStats
                                                                .sasStatsPerPort[deviceStats->sasStatistics.sasProtStats
                                                                                     .portCount]
                                                                .phyCount]
                                                    .lossOfDWORDSynchronizationCount.isSupported = true;
                                                deviceStats->sasStatistics.sasProtStats
                                                    .sasStatsPerPort[deviceStats->sasStatistics.sasProtStats.portCount]
                                                    .perPhy[deviceStats->sasStatistics.sasProtStats
                                                                .sasStatsPerPort[deviceStats->sasStatistics.sasProtStats
                                                                                     .portCount]
                                                                .phyCount]
                                                    .lossOfDWORDSynchronizationCount.isValueValid = true;
                                                deviceStats->sasStatistics.sasProtStats
                                                    .sasStatsPerPort[deviceStats->sasStatistics.sasProtStats.portCount]
                                                    .perPhy[deviceStats->sasStatistics.sasProtStats
                                                                .sasStatsPerPort[deviceStats->sasStatistics.sasProtStats
                                                                                     .portCount]
                                                                .phyCount]
                                                    .lossOfDWORDSynchronizationCount.statisticValue =
                                                    M_BytesTo4ByteValue(
                                                        protSpData[phyOffset + 40], protSpData[phyOffset + 41],
                                                        protSpData[phyOffset + 42], protSpData[phyOffset + 43]);
                                                ++deviceStats->sasStatistics.statisticsPopulated;
                                                deviceStats->sasStatistics.sasProtStats
                                                    .sasStatsPerPort[deviceStats->sasStatistics.sasProtStats.portCount]
                                                    .perPhy[deviceStats->sasStatistics.sasProtStats
                                                                .sasStatsPerPort[deviceStats->sasStatistics.sasProtStats
                                                                                     .portCount]
                                                                .phyCount]
                                                    .phyResetProblemCount.isSupported = true;
                                                deviceStats->sasStatistics.sasProtStats
                                                    .sasStatsPerPort[deviceStats->sasStatistics.sasProtStats.portCount]
                                                    .perPhy[deviceStats->sasStatistics.sasProtStats
                                                                .sasStatsPerPort[deviceStats->sasStatistics.sasProtStats
                                                                                     .portCount]
                                                                .phyCount]
                                                    .phyResetProblemCount.isValueValid = true;
                                                deviceStats->sasStatistics.sasProtStats
                                                    .sasStatsPerPort[deviceStats->sasStatistics.sasProtStats.portCount]
                                                    .perPhy[deviceStats->sasStatistics.sasProtStats
                                                                .sasStatsPerPort[deviceStats->sasStatistics.sasProtStats
                                                                                     .portCount]
                                                                .phyCount]
                                                    .phyResetProblemCount.statisticValue = M_BytesTo4ByteValue(
                                                    protSpData[phyOffset + 44], protSpData[phyOffset + 45],
                                                    protSpData[phyOffset + 46], protSpData[phyOffset + 47]);
                                                ++deviceStats->sasStatistics.statisticsPopulated;
                                                ++deviceStats->sasStatistics.sasProtStats
                                                      .sasStatsPerPort[deviceStats->sasStatistics.sasProtStats
                                                                           .portCount]
                                                      .phyCount;
                                                // Phy event descriptors? Not sure this is needed right now -TJE
                                                //       Events would be yet another loop depending on how many are
                                                //       reported.
                                            }
                                            else
                                            {
                                                continue;
                                            }
                                        }
                                        ++deviceStats->sasStatistics.sasProtStats.portCount;
                                    }
                                }
                                else
                                {
                                    // parameters without a length mean move on to the next one since no additional data
                                    // was provided.
                                    continue;
                                }
                            }
                        }
                        safe_free_aligned(&protSpData);
                    }
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
    // get the SAS timestamp
    safe_memset(tempLogBuf, LEGACY_DRIVE_SEC_SIZE, 0, LEGACY_DRIVE_SEC_SIZE);
    if (SUCCESS == scsi_Report_Timestamp(device, LEGACY_DRIVE_SEC_SIZE, tempLogBuf))
    {
        deviceStats->sasStatistics.timeStampSupported                  = true;
        deviceStats->sasStatistics.dateAndTimeTimestamp.isSupported    = true;
        deviceStats->sasStatistics.dateAndTimeTimestamp.isValueValid   = true;
        deviceStats->sasStatistics.dateAndTimeTimestamp.statisticValue = M_BytesTo8ByteValue(
            0, 0, tempLogBuf[4], tempLogBuf[5], tempLogBuf[6], tempLogBuf[7], tempLogBuf[8], tempLogBuf[9]);
    }
    // Get the Grown list count
    bool                    gotGrownDefectCount = false;
    eSCSIAddressDescriptors defectFormat        = AD_SHORT_BLOCK_FORMAT_ADDRESS_DESCRIPTOR;
    eReturnValues           defectRet           = SUCCESS;
    if (device->drive_info.deviceMaxLba > UINT32_MAX)
    {
        defectFormat = AD_LONG_BLOCK_FORMAT_ADDRESS_DESCRIPTOR;
    }
    if (!is_SSD(device))
    {
        // this should work on just about any HDD
        defectFormat = AD_PHYSICAL_SECTOR_FORMAT_ADDRESS_DESCRIPTOR;
    }
    while (!gotGrownDefectCount)
    {
        // This loop is so that we can retry with different formats if it does not work the first time - TJE
        // Attempt LBA mode, then attempt pchs, then call it quits if neither works.
        // If the drive has a large LBA (>32b max) then use extended formats, otherwise use short formats
        // NOTE: SBC2 and later added extended formats
        uint32_t defectListLength = UINT32_C(0);
        safe_memset(tempLogBuf, LEGACY_DRIVE_SEC_SIZE, 0, LEGACY_DRIVE_SEC_SIZE);
        if (device->drive_info.scsiVersion > SCSI_VERSION_SCSI2 &&
            (defectRet = scsi_Read_Defect_Data_12(device, false, true, C_CAST(uint8_t, defectFormat), 0, 8,
                                                  tempLogBuf)) == SUCCESS)
        {
            gotGrownDefectCount = true;
            defectListLength    = M_BytesTo4ByteValue(tempLogBuf[4], tempLogBuf[5], tempLogBuf[6], tempLogBuf[7]);
        }
        else
        {
            defectRet = scsi_Read_Defect_Data_10(device, false, true, C_CAST(uint8_t, defectFormat), 4, tempLogBuf);
            if (defectRet == SUCCESS)
            {
                gotGrownDefectCount = true;
                defectListLength    = M_BytesTo2ByteValue(tempLogBuf[2], tempLogBuf[3]);
            }
        }
        if (defectRet != SUCCESS && !gotGrownDefectCount)
        {
            break;
        }
        else
        {
            deviceStats->sasStatistics.defectStatisticsSupported = true;
            deviceStats->sasStatistics.grownDefects.isSupported  = true;
            ++deviceStats->sasStatistics.statisticsPopulated;
            switch (defectFormat)
            {
            case AD_SHORT_BLOCK_FORMAT_ADDRESS_DESCRIPTOR:
                deviceStats->sasStatistics.grownDefects.isValueValid   = true;
                deviceStats->sasStatistics.grownDefects.statisticValue = defectListLength / 4;
                break;
            case AD_PHYSICAL_SECTOR_FORMAT_ADDRESS_DESCRIPTOR:
            case AD_LONG_BLOCK_FORMAT_ADDRESS_DESCRIPTOR:
            case AD_EXTENDED_PHYSICAL_SECTOR_FORMAT_ADDRESS_DESCRIPTOR:
                deviceStats->sasStatistics.grownDefects.isValueValid   = true;
                deviceStats->sasStatistics.grownDefects.statisticValue = defectListLength / 8;
                break;
            default:
                break;
            }
        }
    }
    // Get the primary list count
    // most likely the primary list in block format won't work, but trying it anyways as a first step - TJE
    bool gotPrimaryDefectCount = false;
    defectFormat               = AD_SHORT_BLOCK_FORMAT_ADDRESS_DESCRIPTOR;
    defectRet                  = SUCCESS;
    if (device->drive_info.deviceMaxLba > UINT32_MAX)
    {
        defectFormat = AD_LONG_BLOCK_FORMAT_ADDRESS_DESCRIPTOR;
    }
    while (!gotPrimaryDefectCount)
    {
        // This loop is so that we can retry with different formats if it does not work the first time - TJE
        // Attempt LBA mode, then attempt pchs, then call it quits if neither works.
        // If the drive has a large LBA (>32b max) then use extended formats, otherwise use short formats
        // NOTE: SBC2 and later added extended formats
        uint32_t defectListLength = UINT32_C(0);
        safe_memset(tempLogBuf, LEGACY_DRIVE_SEC_SIZE, 0, LEGACY_DRIVE_SEC_SIZE);
        if (device->drive_info.scsiVersion > SCSI_VERSION_SCSI2 &&
            (defectRet = scsi_Read_Defect_Data_12(device, true, false, C_CAST(uint8_t, defectFormat), 0, 8,
                                                  tempLogBuf)) == SUCCESS)
        {
            gotPrimaryDefectCount = true;
            defectListLength      = M_BytesTo4ByteValue(tempLogBuf[4], tempLogBuf[5], tempLogBuf[6], tempLogBuf[7]);
        }
        else
        {
            defectRet = scsi_Read_Defect_Data_10(device, true, false, C_CAST(uint8_t, defectFormat), 4, tempLogBuf);
            if (defectRet == SUCCESS)
            {
                gotPrimaryDefectCount = true;
                defectListLength      = M_BytesTo2ByteValue(tempLogBuf[2], tempLogBuf[3]);
            }
        }
        if (defectRet != SUCCESS && !gotPrimaryDefectCount)
        {
            if (defectFormat == AD_SHORT_BLOCK_FORMAT_ADDRESS_DESCRIPTOR)
            {
                defectFormat = AD_PHYSICAL_SECTOR_FORMAT_ADDRESS_DESCRIPTOR;
            }
            else if (defectFormat == AD_LONG_BLOCK_FORMAT_ADDRESS_DESCRIPTOR)
            {
                defectFormat = AD_EXTENDED_PHYSICAL_SECTOR_FORMAT_ADDRESS_DESCRIPTOR;
            }
            else if (defectFormat == AD_PHYSICAL_SECTOR_FORMAT_ADDRESS_DESCRIPTOR)
            {
                // special case to restart the loop again with long address types in case short are not supported, but
                // it isn't a high capacity devices
                defectFormat = AD_LONG_BLOCK_FORMAT_ADDRESS_DESCRIPTOR;
            }
            else
            {
                break;
            }
        }
        else
        {
            deviceStats->sasStatistics.defectStatisticsSupported  = true;
            deviceStats->sasStatistics.primaryDefects.isSupported = true;
            ++deviceStats->sasStatistics.statisticsPopulated;
            switch (defectFormat)
            {
            case AD_SHORT_BLOCK_FORMAT_ADDRESS_DESCRIPTOR:
                deviceStats->sasStatistics.primaryDefects.isValueValid   = true;
                deviceStats->sasStatistics.primaryDefects.statisticValue = defectListLength / 4;
                break;
            case AD_PHYSICAL_SECTOR_FORMAT_ADDRESS_DESCRIPTOR:
            case AD_LONG_BLOCK_FORMAT_ADDRESS_DESCRIPTOR:
            case AD_EXTENDED_PHYSICAL_SECTOR_FORMAT_ADDRESS_DESCRIPTOR:
                deviceStats->sasStatistics.primaryDefects.isValueValid   = true;
                deviceStats->sasStatistics.primaryDefects.statisticValue = defectListLength / 8;
                break;
            default:
                break;
            }
        }
    }
    return ret;
}

eReturnValues get_DeviceStatistics(tDevice* device, ptrDeviceStatistics deviceStats)
{
    eReturnValues ret = NOT_SUPPORTED;
    DISABLE_NONNULL_COMPARE
    if (deviceStats == M_NULLPTR)
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
    RESTORE_NONNULL_COMPARE
    return ret;
}

void scsi_Threshold_Comparison(statistic* ptrStatistic)
{
    DISABLE_NONNULL_COMPARE
    if (ptrStatistic != M_NULLPTR)
    {
        if (ptrStatistic->isThresholdValid && ptrStatistic->thresholdNotificationEnabled &&
            ptrStatistic->supportsNotification)
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
    RESTORE_NONNULL_COMPARE
}

#define DEVICE_STATISTIC_FLAGS_LEN 4

static M_INLINE void print_Statistic_Flags(statistic theStatistic)
{
    DECLARE_ZERO_INIT_ARRAY(char, statisticFlags, DEVICE_STATISTIC_FLAGS_LEN + 1);
    if (theStatistic.monitoredConditionMet)
    {
        statisticFlags[0] = '!';
    }
    else
    {
        statisticFlags[0] = ' ';
    }
    if (theStatistic.isThresholdValid)
    {
        statisticFlags[1] = '*';
    }
    else
    {
        statisticFlags[1] = ' ';
    }
    if (theStatistic.supportsNotification)
    {
        statisticFlags[2] = '-';
    }
    else
    {
        statisticFlags[2] = ' ';
    }
    if (theStatistic.supportsReadThenInitialize)
    {
        statisticFlags[3] = '^';
    }
    else
    {
        statisticFlags[3] = ' ';
    }
    printf("%s", statisticFlags);
}

#define DEVICE_STATISTICS_DISPLAY_THRESHOLD_STRING_LENGTH 30

static void print_Count_Statistic(statistic theStatistic, const char* statisticName, const char* statisticUnit)
{
    if (theStatistic.isSupported)
    {
        DECLARE_ZERO_INIT_ARRAY(char, displayThreshold, DEVICE_STATISTICS_DISPLAY_THRESHOLD_STRING_LENGTH);
        print_Statistic_Flags(theStatistic);
        printf("%-60s", statisticName);
        if (theStatistic.isThresholdValid)
        {
            switch (theStatistic.threshType)
            {
            case THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE:
                snprintf_err_handle(displayThreshold, DEVICE_STATISTICS_DISPLAY_THRESHOLD_STRING_LENGTH,
                                    "%" PRIu64 " (Always Trigger)", theStatistic.threshold);
                break;
            case THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL:
                snprintf_err_handle(displayThreshold, DEVICE_STATISTICS_DISPLAY_THRESHOLD_STRING_LENGTH, "=%" PRIu64,
                                    theStatistic.threshold);
                break;
            case THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL:
                snprintf_err_handle(displayThreshold, DEVICE_STATISTICS_DISPLAY_THRESHOLD_STRING_LENGTH, "!=%" PRIu64,
                                    theStatistic.threshold);
                break;
            case THRESHOLD_TYPE_TRIGGER_WHEN_GREATER:
                snprintf_err_handle(displayThreshold, DEVICE_STATISTICS_DISPLAY_THRESHOLD_STRING_LENGTH, ">%" PRIu64,
                                    theStatistic.threshold);
                break;
            case THRESHOLD_TYPE_TRIGGER_WHEN_LESS:
                snprintf_err_handle(displayThreshold, DEVICE_STATISTICS_DISPLAY_THRESHOLD_STRING_LENGTH, "<%" PRIu64,
                                    theStatistic.threshold);
                break;
            case THRESHOLD_TYPE_NO_TRIGGER:
            default:
                snprintf_err_handle(displayThreshold, DEVICE_STATISTICS_DISPLAY_THRESHOLD_STRING_LENGTH, "%" PRIu64,
                                    theStatistic.threshold);
                break;
            }
        }
        else
        {
            snprintf_err_handle(displayThreshold, DEVICE_STATISTICS_DISPLAY_THRESHOLD_STRING_LENGTH, "N/A");
        }
        printf(" %-16s ", displayThreshold);
        if (theStatistic.isValueValid)
        {
            printf("%" PRIu64, theStatistic.statisticValue);
            if (statisticUnit != M_NULLPTR)
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

static void print_Workload_Utilization_Statistic(statistic theStatistic, const char* statisticName)
{
    if (theStatistic.isSupported)
    {
        DECLARE_ZERO_INIT_ARRAY(char, displayThreshold, DEVICE_STATISTICS_DISPLAY_THRESHOLD_STRING_LENGTH);
        print_Statistic_Flags(theStatistic);
        printf("%-60s", statisticName);
        if (theStatistic.isThresholdValid)
        {
            switch (theStatistic.threshType)
            {
            case THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE:
                snprintf_err_handle(displayThreshold, DEVICE_STATISTICS_DISPLAY_THRESHOLD_STRING_LENGTH,
                                    "%" PRIu64 " (Always Trigger)", theStatistic.threshold);
                break;
            case THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL:
                snprintf_err_handle(displayThreshold, DEVICE_STATISTICS_DISPLAY_THRESHOLD_STRING_LENGTH, "=%" PRIu64,
                                    theStatistic.threshold);
                break;
            case THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL:
                snprintf_err_handle(displayThreshold, DEVICE_STATISTICS_DISPLAY_THRESHOLD_STRING_LENGTH, "!=%" PRIu64,
                                    theStatistic.threshold);
                break;
            case THRESHOLD_TYPE_TRIGGER_WHEN_GREATER:
                snprintf_err_handle(displayThreshold, DEVICE_STATISTICS_DISPLAY_THRESHOLD_STRING_LENGTH, ">%" PRIu64,
                                    theStatistic.threshold);
                break;
            case THRESHOLD_TYPE_TRIGGER_WHEN_LESS:
                snprintf_err_handle(displayThreshold, DEVICE_STATISTICS_DISPLAY_THRESHOLD_STRING_LENGTH, "<%" PRIu64,
                                    theStatistic.threshold);
                break;
            case THRESHOLD_TYPE_NO_TRIGGER:
            default:
                snprintf_err_handle(displayThreshold, DEVICE_STATISTICS_DISPLAY_THRESHOLD_STRING_LENGTH, "%" PRIu64,
                                    theStatistic.threshold);
                break;
            }
        }
        else
        {
            snprintf_err_handle(displayThreshold, DEVICE_STATISTICS_DISPLAY_THRESHOLD_STRING_LENGTH, "N/A");
        }
        printf(" %-16s ", displayThreshold);
        if (theStatistic.isValueValid)
        {
            if (theStatistic.statisticValue != 65535)
            {
                double workloadUtilization = C_CAST(double, theStatistic.statisticValue);
                workloadUtilization *= 0.01; // convert to fractional percentage
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

static void print_Utilization_Usage_Rate_Statistic(statistic theStatistic, const char* statisticName)
{
    if (theStatistic.isSupported)
    {
        DECLARE_ZERO_INIT_ARRAY(char, displayThreshold, DEVICE_STATISTICS_DISPLAY_THRESHOLD_STRING_LENGTH);
        print_Statistic_Flags(theStatistic);
        printf("%-60s", statisticName);
        if (theStatistic.isThresholdValid)
        {
            switch (theStatistic.threshType)
            {
            case THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE:
                snprintf_err_handle(displayThreshold, DEVICE_STATISTICS_DISPLAY_THRESHOLD_STRING_LENGTH,
                                    "%" PRIu64 " (Always Trigger)", theStatistic.threshold);
                break;
            case THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL:
                snprintf_err_handle(displayThreshold, DEVICE_STATISTICS_DISPLAY_THRESHOLD_STRING_LENGTH, "=%" PRIu64,
                                    theStatistic.threshold);
                break;
            case THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL:
                snprintf_err_handle(displayThreshold, DEVICE_STATISTICS_DISPLAY_THRESHOLD_STRING_LENGTH, "!=%" PRIu64,
                                    theStatistic.threshold);
                break;
            case THRESHOLD_TYPE_TRIGGER_WHEN_GREATER:
                snprintf_err_handle(displayThreshold, DEVICE_STATISTICS_DISPLAY_THRESHOLD_STRING_LENGTH, ">%" PRIu64,
                                    theStatistic.threshold);
                break;
            case THRESHOLD_TYPE_TRIGGER_WHEN_LESS:
                snprintf_err_handle(displayThreshold, DEVICE_STATISTICS_DISPLAY_THRESHOLD_STRING_LENGTH, "<%" PRIu64,
                                    theStatistic.threshold);
                break;
            case THRESHOLD_TYPE_NO_TRIGGER:
            default:
                snprintf_err_handle(displayThreshold, DEVICE_STATISTICS_DISPLAY_THRESHOLD_STRING_LENGTH, "%" PRIu64,
                                    theStatistic.threshold);
                break;
            }
        }
        else
        {
            snprintf_err_handle(displayThreshold, DEVICE_STATISTICS_DISPLAY_THRESHOLD_STRING_LENGTH, "N/A");
        }
        printf(" %-16s ", displayThreshold);
        if (theStatistic.isValueValid)
        {
            uint8_t utilizationUsageRate = M_Byte0(theStatistic.statisticValue);
            uint8_t rateValidity         = M_Byte5(theStatistic.statisticValue);
            uint8_t rateBasis            = M_Nibble9(theStatistic.statisticValue);
            switch (rateValidity)
            {
            case 0: // valid
                if (utilizationUsageRate == 255)
                {
                    printf(">254%%");
                }
                else
                {
                    printf("%" PRIu8 "%%", utilizationUsageRate);
                }
                switch (rateBasis)
                {
                case 0: // since manufacture
                    printf(" since manufacture");
                    break;
                case 4: // since power on reset
                    printf(" since power on reset");
                    break;
                case 8: // power on hours
                    printf(" for POH");
                    break;
                case 0xF: // undetermined
                default:
                    break;
                }
                break;
            case 0x10: // invalid due to insufficient info
                printf("Invalid - insufficient info collected");
                break;
            case 0x81: // unreasonable due to date and time timestamp
                printf("Unreasonable due to date and time timestamp");
                break;
            case 0xFF:
            default: // invalid for unknown reason
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

static void print_Resource_Availability_Statistic(statistic theStatistic, const char* statisticName)
{
    if (theStatistic.isSupported)
    {
        DECLARE_ZERO_INIT_ARRAY(char, displayThreshold, DEVICE_STATISTICS_DISPLAY_THRESHOLD_STRING_LENGTH);
        print_Statistic_Flags(theStatistic);
        printf("%-60s", statisticName);
        if (theStatistic.isThresholdValid)
        {
            switch (theStatistic.threshType)
            {
            case THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE:
                snprintf_err_handle(displayThreshold, DEVICE_STATISTICS_DISPLAY_THRESHOLD_STRING_LENGTH,
                                    "%" PRIu64 " (Always Trigger)", theStatistic.threshold);
                break;
            case THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL:
                snprintf_err_handle(displayThreshold, DEVICE_STATISTICS_DISPLAY_THRESHOLD_STRING_LENGTH, "=%" PRIu64,
                                    theStatistic.threshold);
                break;
            case THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL:
                snprintf_err_handle(displayThreshold, DEVICE_STATISTICS_DISPLAY_THRESHOLD_STRING_LENGTH, "!=%" PRIu64,
                                    theStatistic.threshold);
                break;
            case THRESHOLD_TYPE_TRIGGER_WHEN_GREATER:
                snprintf_err_handle(displayThreshold, DEVICE_STATISTICS_DISPLAY_THRESHOLD_STRING_LENGTH, ">%" PRIu64,
                                    theStatistic.threshold);
                break;
            case THRESHOLD_TYPE_TRIGGER_WHEN_LESS:
                snprintf_err_handle(displayThreshold, DEVICE_STATISTICS_DISPLAY_THRESHOLD_STRING_LENGTH, "<%" PRIu64,
                                    theStatistic.threshold);
                break;
            case THRESHOLD_TYPE_NO_TRIGGER:
            default:
                snprintf_err_handle(displayThreshold, DEVICE_STATISTICS_DISPLAY_THRESHOLD_STRING_LENGTH, "%" PRIu64,
                                    theStatistic.threshold);
                break;
            }
        }
        else
        {
            snprintf_err_handle(displayThreshold, DEVICE_STATISTICS_DISPLAY_THRESHOLD_STRING_LENGTH, "N/A");
        }
        printf(" %-16s ", displayThreshold);
        if (theStatistic.isValueValid)
        {
            double fractionAvailable = C_CAST(double, M_Word0(theStatistic.statisticValue)) / 65535.0;
            printf("%0.02f%% Available", fractionAvailable);
        }
        else
        {
            printf("Invalid");
        }
        printf("\n");
    }
}

static void print_Random_Write_Resources_Used_Statistic(statistic theStatistic, const char* statisticName)
{
    if (theStatistic.isSupported)
    {
        DECLARE_ZERO_INIT_ARRAY(char, displayThreshold, DEVICE_STATISTICS_DISPLAY_THRESHOLD_STRING_LENGTH);
        print_Statistic_Flags(theStatistic);
        printf("%-60s", statisticName);
        if (theStatistic.isThresholdValid)
        {
            switch (theStatistic.threshType)
            {
            case THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE:
                snprintf_err_handle(displayThreshold, DEVICE_STATISTICS_DISPLAY_THRESHOLD_STRING_LENGTH,
                                    "%" PRIu64 " (Always Trigger)", theStatistic.threshold);
                break;
            case THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL:
                snprintf_err_handle(displayThreshold, DEVICE_STATISTICS_DISPLAY_THRESHOLD_STRING_LENGTH, "=%" PRIu64,
                                    theStatistic.threshold);
                break;
            case THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL:
                snprintf_err_handle(displayThreshold, DEVICE_STATISTICS_DISPLAY_THRESHOLD_STRING_LENGTH, "!=%" PRIu64,
                                    theStatistic.threshold);
                break;
            case THRESHOLD_TYPE_TRIGGER_WHEN_GREATER:
                snprintf_err_handle(displayThreshold, DEVICE_STATISTICS_DISPLAY_THRESHOLD_STRING_LENGTH, ">%" PRIu64,
                                    theStatistic.threshold);
                break;
            case THRESHOLD_TYPE_TRIGGER_WHEN_LESS:
                snprintf_err_handle(displayThreshold, DEVICE_STATISTICS_DISPLAY_THRESHOLD_STRING_LENGTH, "<%" PRIu64,
                                    theStatistic.threshold);
                break;
            case THRESHOLD_TYPE_NO_TRIGGER:
            default:
                snprintf_err_handle(displayThreshold, DEVICE_STATISTICS_DISPLAY_THRESHOLD_STRING_LENGTH, "%" PRIu64,
                                    theStatistic.threshold);
                break;
            }
        }
        else
        {
            snprintf_err_handle(displayThreshold, DEVICE_STATISTICS_DISPLAY_THRESHOLD_STRING_LENGTH, "N/A");
        }
        printf(" %-16s ", displayThreshold);
        if (theStatistic.isValueValid)
        {
            uint8_t resourceValue = M_Byte0(theStatistic.statisticValue);
            if (/* resourceValue >= 0 && */ resourceValue <= 0x7F)
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

static void print_Non_Volatile_Time_Statistic(statistic theStatistic, const char* statisticName)
{
    if (theStatistic.isSupported)
    {
        DECLARE_ZERO_INIT_ARRAY(char, displayThreshold, DEVICE_STATISTICS_DISPLAY_THRESHOLD_STRING_LENGTH);
        print_Statistic_Flags(theStatistic);
        printf("%-60s", statisticName);
        if (theStatistic.isThresholdValid)
        {
            switch (theStatistic.threshType)
            {
            case THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE:
                snprintf_err_handle(displayThreshold, DEVICE_STATISTICS_DISPLAY_THRESHOLD_STRING_LENGTH,
                                    "%" PRIu64 " (Always Trigger)", theStatistic.threshold);
                break;
            case THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL:
                snprintf_err_handle(displayThreshold, DEVICE_STATISTICS_DISPLAY_THRESHOLD_STRING_LENGTH, "=%" PRIu64,
                                    theStatistic.threshold);
                break;
            case THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL:
                snprintf_err_handle(displayThreshold, DEVICE_STATISTICS_DISPLAY_THRESHOLD_STRING_LENGTH, "!=%" PRIu64,
                                    theStatistic.threshold);
                break;
            case THRESHOLD_TYPE_TRIGGER_WHEN_GREATER:
                snprintf_err_handle(displayThreshold, DEVICE_STATISTICS_DISPLAY_THRESHOLD_STRING_LENGTH, ">%" PRIu64,
                                    theStatistic.threshold);
                break;
            case THRESHOLD_TYPE_TRIGGER_WHEN_LESS:
                snprintf_err_handle(displayThreshold, DEVICE_STATISTICS_DISPLAY_THRESHOLD_STRING_LENGTH, "<%" PRIu64,
                                    theStatistic.threshold);
                break;
            case THRESHOLD_TYPE_NO_TRIGGER:
            default:
                snprintf_err_handle(displayThreshold, DEVICE_STATISTICS_DISPLAY_THRESHOLD_STRING_LENGTH, "%" PRIu64,
                                    theStatistic.threshold);
                break;
            }
        }
        else
        {
            snprintf_err_handle(displayThreshold, DEVICE_STATISTICS_DISPLAY_THRESHOLD_STRING_LENGTH, "N/A");
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
            default: // time in minutes
                printf("Nonvolatile for %" PRIu64 "m", theStatistic.statisticValue);
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

static void print_Temperature_Statistic(statistic theStatistic, const char* statisticName)
{
    if (theStatistic.isSupported)
    {
        DECLARE_ZERO_INIT_ARRAY(char, displayThreshold, DEVICE_STATISTICS_DISPLAY_THRESHOLD_STRING_LENGTH);
        print_Statistic_Flags(theStatistic);
        printf("%-60s", statisticName);
        if (theStatistic.isThresholdValid)
        {
            switch (theStatistic.threshType)
            {
            case THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE:
                snprintf_err_handle(displayThreshold, DEVICE_STATISTICS_DISPLAY_THRESHOLD_STRING_LENGTH,
                                    "%" PRIu64 " (Always Trigger)", theStatistic.threshold);
                break;
            case THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL:
                snprintf_err_handle(displayThreshold, DEVICE_STATISTICS_DISPLAY_THRESHOLD_STRING_LENGTH, "=%" PRIu64,
                                    theStatistic.threshold);
                break;
            case THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL:
                snprintf_err_handle(displayThreshold, DEVICE_STATISTICS_DISPLAY_THRESHOLD_STRING_LENGTH, "!=%" PRIu64,
                                    theStatistic.threshold);
                break;
            case THRESHOLD_TYPE_TRIGGER_WHEN_GREATER:
                snprintf_err_handle(displayThreshold, DEVICE_STATISTICS_DISPLAY_THRESHOLD_STRING_LENGTH, ">%" PRIu64,
                                    theStatistic.threshold);
                break;
            case THRESHOLD_TYPE_TRIGGER_WHEN_LESS:
                snprintf_err_handle(displayThreshold, DEVICE_STATISTICS_DISPLAY_THRESHOLD_STRING_LENGTH, "<%" PRIu64,
                                    theStatistic.threshold);
                break;
            case THRESHOLD_TYPE_NO_TRIGGER:
            default:
                snprintf_err_handle(displayThreshold, DEVICE_STATISTICS_DISPLAY_THRESHOLD_STRING_LENGTH, "%" PRIu64,
                                    theStatistic.threshold);
                break;
            }
        }
        else
        {
            snprintf_err_handle(displayThreshold, DEVICE_STATISTICS_DISPLAY_THRESHOLD_STRING_LENGTH, "N/A");
        }
        printf(" %-16s ", displayThreshold);
        if (theStatistic.isValueValid)
        {
            printf("%" PRId8 " C", C_CAST(int8_t, theStatistic.statisticValue));
        }
        else
        {
            printf("Invalid");
        }
        printf("\n");
    }
}

static void print_Date_And_Time_Timestamp_Statistic(statistic theStatistic, const char* statisticName)
{
    if (theStatistic.isSupported)
    {
        DECLARE_ZERO_INIT_ARRAY(char, displayThreshold, DEVICE_STATISTICS_DISPLAY_THRESHOLD_STRING_LENGTH);
        print_Statistic_Flags(theStatistic);
        printf("%-60s", statisticName);
        if (theStatistic.isThresholdValid)
        {
            switch (theStatistic.threshType)
            {
            case THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE:
                snprintf_err_handle(displayThreshold, DEVICE_STATISTICS_DISPLAY_THRESHOLD_STRING_LENGTH,
                                    "%" PRIu64 " (Always Trigger)", theStatistic.threshold);
                break;
            case THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL:
                snprintf_err_handle(displayThreshold, DEVICE_STATISTICS_DISPLAY_THRESHOLD_STRING_LENGTH, "=%" PRIu64,
                                    theStatistic.threshold);
                break;
            case THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL:
                snprintf_err_handle(displayThreshold, DEVICE_STATISTICS_DISPLAY_THRESHOLD_STRING_LENGTH, "!=%" PRIu64,
                                    theStatistic.threshold);
                break;
            case THRESHOLD_TYPE_TRIGGER_WHEN_GREATER:
                snprintf_err_handle(displayThreshold, DEVICE_STATISTICS_DISPLAY_THRESHOLD_STRING_LENGTH, ">%" PRIu64,
                                    theStatistic.threshold);
                break;
            case THRESHOLD_TYPE_TRIGGER_WHEN_LESS:
                snprintf_err_handle(displayThreshold, DEVICE_STATISTICS_DISPLAY_THRESHOLD_STRING_LENGTH, "<%" PRIu64,
                                    theStatistic.threshold);
                break;
            case THRESHOLD_TYPE_NO_TRIGGER:
            default:
                snprintf_err_handle(displayThreshold, DEVICE_STATISTICS_DISPLAY_THRESHOLD_STRING_LENGTH, "%" PRIu64,
                                    theStatistic.threshold);
                break;
            }
        }
        else
        {
            snprintf_err_handle(displayThreshold, DEVICE_STATISTICS_DISPLAY_THRESHOLD_STRING_LENGTH, "N/A");
        }
        printf(" %-16s ", displayThreshold);
        if (theStatistic.isValueValid)
        {
            struct tm time;
            DECLARE_ZERO_INIT_ARRAY(char, timestr, TIME_STRING_LENGTH);
            safe_memset(&time, sizeof(struct tm), 0, sizeof(struct tm));
            eConstraintHandler handler = set_Constraint_Handler(ERR_IGNORE);
            if (0 == safe_asctime(timestr, TIME_STRING_LENGTH,
                                  milliseconds_Since_Unix_Epoch_To_Struct_TM(theStatistic.statisticValue, &time)))
            {
                printf("%s", timestr);
            }
            else
            {
                printf("Error converting time\n");
            }
            set_Constraint_Handler(handler);
        }
        else if (theStatistic.statisticValue > UINT64_C(0))
        {
            // ACS-6 says this may report POH in milliseconds until first date and time timestamp command is sent.
            // Through observation it seems that if the "valid" bit is not set, then this is what gets reported -TJE
            printf("%" PRIu64 " power on ms\n", theStatistic.statisticValue);
        }
        else
        {
            printf("Invalid");
        }
        printf("\n");
    }
}
// the statistic value must be a time in minutes for this function
static void print_Time_Minutes_Statistic(statistic theStatistic, const char* statisticName)
{
    if (theStatistic.isSupported)
    {
        DECLARE_ZERO_INIT_ARRAY(char, displayThreshold, DEVICE_STATISTICS_DISPLAY_THRESHOLD_STRING_LENGTH);
        print_Statistic_Flags(theStatistic);
        printf("%-60s", statisticName);
        if (theStatistic.isThresholdValid)
        {
            switch (theStatistic.threshType)
            {
            case THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE:
                snprintf_err_handle(displayThreshold, DEVICE_STATISTICS_DISPLAY_THRESHOLD_STRING_LENGTH,
                                    "%" PRIu64 " (Always Trigger)", theStatistic.threshold);
                break;
            case THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL:
                snprintf_err_handle(displayThreshold, DEVICE_STATISTICS_DISPLAY_THRESHOLD_STRING_LENGTH, "=%" PRIu64,
                                    theStatistic.threshold);
                break;
            case THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL:
                snprintf_err_handle(displayThreshold, DEVICE_STATISTICS_DISPLAY_THRESHOLD_STRING_LENGTH, "!=%" PRIu64,
                                    theStatistic.threshold);
                break;
            case THRESHOLD_TYPE_TRIGGER_WHEN_GREATER:
                snprintf_err_handle(displayThreshold, DEVICE_STATISTICS_DISPLAY_THRESHOLD_STRING_LENGTH, ">%" PRIu64,
                                    theStatistic.threshold);
                break;
            case THRESHOLD_TYPE_TRIGGER_WHEN_LESS:
                snprintf_err_handle(displayThreshold, DEVICE_STATISTICS_DISPLAY_THRESHOLD_STRING_LENGTH, "<%" PRIu64,
                                    theStatistic.threshold);
                break;
            case THRESHOLD_TYPE_NO_TRIGGER:
            default:
                snprintf_err_handle(displayThreshold, DEVICE_STATISTICS_DISPLAY_THRESHOLD_STRING_LENGTH, "%" PRIu64,
                                    theStatistic.threshold);
                break;
            }
        }
        else
        {
            snprintf_err_handle(displayThreshold, DEVICE_STATISTICS_DISPLAY_THRESHOLD_STRING_LENGTH, "N/A");
        }
        printf(" %-16s", displayThreshold);
        if (theStatistic.isValueValid)
        {
            // this is reported in minutes...convert to other displayable.
            uint64_t statisticMinutes = theStatistic.statisticValue * UINT64_C(60);
            if (statisticMinutes > 0)
            {
                uint16_t days    = UINT16_C(0);
                uint8_t  years   = UINT8_C(0);
                uint8_t  hours   = UINT8_C(0);
                uint8_t  minutes = UINT8_C(0);
                uint8_t  seconds = UINT8_C(0);
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

static void print_Time_Microseconds_Statistic(statistic theStatistic, const char* statisticName)
{
    if (theStatistic.isSupported)
    {
        DECLARE_ZERO_INIT_ARRAY(char, displayThreshold, DEVICE_STATISTICS_DISPLAY_THRESHOLD_STRING_LENGTH);
        print_Statistic_Flags(theStatistic);
        printf("%-60s", statisticName);
        if (theStatistic.isThresholdValid)
        {
            switch (theStatistic.threshType)
            {
            case THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE:
                snprintf_err_handle(displayThreshold, DEVICE_STATISTICS_DISPLAY_THRESHOLD_STRING_LENGTH,
                                    "%" PRIu64 " (Always Trigger)", theStatistic.threshold);
                break;
            case THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL:
                snprintf_err_handle(displayThreshold, DEVICE_STATISTICS_DISPLAY_THRESHOLD_STRING_LENGTH, "=%" PRIu64,
                                    theStatistic.threshold);
                break;
            case THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL:
                snprintf_err_handle(displayThreshold, DEVICE_STATISTICS_DISPLAY_THRESHOLD_STRING_LENGTH, "!=%" PRIu64,
                                    theStatistic.threshold);
                break;
            case THRESHOLD_TYPE_TRIGGER_WHEN_GREATER:
                snprintf_err_handle(displayThreshold, DEVICE_STATISTICS_DISPLAY_THRESHOLD_STRING_LENGTH, ">%" PRIu64,
                                    theStatistic.threshold);
                break;
            case THRESHOLD_TYPE_TRIGGER_WHEN_LESS:
                snprintf_err_handle(displayThreshold, DEVICE_STATISTICS_DISPLAY_THRESHOLD_STRING_LENGTH, "<%" PRIu64,
                                    theStatistic.threshold);
                break;
            case THRESHOLD_TYPE_NO_TRIGGER:
            default:
                snprintf_err_handle(displayThreshold, DEVICE_STATISTICS_DISPLAY_THRESHOLD_STRING_LENGTH, "%" PRIu64,
                                    theStatistic.threshold);
                break;
            }
        }
        else
        {
            snprintf_err_handle(displayThreshold, DEVICE_STATISTICS_DISPLAY_THRESHOLD_STRING_LENGTH, "N/A");
        }
        printf(" %-16s", displayThreshold);
        if (theStatistic.isValueValid)
        {
            printf("%" PRIu64 " us", theStatistic.statisticValue);
        }
        else
        {
            printf("Invalid");
        }
        printf("\n");
    }
}

// for accounting date and date of manufacture
static void print_SCSI_Date_Statistic(statistic theStatistic, const char* statisticName)
{
    if (theStatistic.isSupported)
    {
        DECLARE_ZERO_INIT_ARRAY(char, displayThreshold, DEVICE_STATISTICS_DISPLAY_THRESHOLD_STRING_LENGTH);
        print_Statistic_Flags(theStatistic);
        printf("%-60s", statisticName);
        if (theStatistic.isThresholdValid)
        {
            switch (theStatistic.threshType)
            {
            case THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE:
                snprintf_err_handle(displayThreshold, DEVICE_STATISTICS_DISPLAY_THRESHOLD_STRING_LENGTH,
                                    "%" PRIu64 " (Always Trigger)", theStatistic.threshold);
                break;
            case THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL:
                snprintf_err_handle(displayThreshold, DEVICE_STATISTICS_DISPLAY_THRESHOLD_STRING_LENGTH, "=%" PRIu64,
                                    theStatistic.threshold);
                break;
            case THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL:
                snprintf_err_handle(displayThreshold, DEVICE_STATISTICS_DISPLAY_THRESHOLD_STRING_LENGTH, "!=%" PRIu64,
                                    theStatistic.threshold);
                break;
            case THRESHOLD_TYPE_TRIGGER_WHEN_GREATER:
                snprintf_err_handle(displayThreshold, DEVICE_STATISTICS_DISPLAY_THRESHOLD_STRING_LENGTH, ">%" PRIu64,
                                    theStatistic.threshold);
                break;
            case THRESHOLD_TYPE_TRIGGER_WHEN_LESS:
                snprintf_err_handle(displayThreshold, DEVICE_STATISTICS_DISPLAY_THRESHOLD_STRING_LENGTH, "<%" PRIu64,
                                    theStatistic.threshold);
                break;
            case THRESHOLD_TYPE_NO_TRIGGER:
            default:
                snprintf_err_handle(displayThreshold, DEVICE_STATISTICS_DISPLAY_THRESHOLD_STRING_LENGTH, "%" PRIu64,
                                    theStatistic.threshold);
                break;
            }
        }
        else
        {
            snprintf_err_handle(displayThreshold, DEVICE_STATISTICS_DISPLAY_THRESHOLD_STRING_LENGTH, "N/A");
        }
        printf(" %-16s ", displayThreshold);
        if (theStatistic.isValueValid)
        {
            DECLARE_ZERO_INIT_ARRAY(char, week, 3);
            DECLARE_ZERO_INIT_ARRAY(char, year, 5);
            year[0] = C_CAST(char, M_Byte3(theStatistic.statisticValue));
            year[1] = C_CAST(char, M_Byte2(theStatistic.statisticValue));
            year[2] = C_CAST(char, M_Byte1(theStatistic.statisticValue));
            year[3] = C_CAST(char, M_Byte0(theStatistic.statisticValue));
            year[4] = '\0';
            week[0] = C_CAST(char, M_Byte5(theStatistic.statisticValue));
            week[1] = C_CAST(char, M_Byte4(theStatistic.statisticValue));
            week[2] = '\0';
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

static void print_SCSI_Time_Interval_Statistic(statistic theStatistic, const char* statisticName)
{
    if (theStatistic.isSupported)
    {
        DECLARE_ZERO_INIT_ARRAY(char, displayThreshold, DEVICE_STATISTICS_DISPLAY_THRESHOLD_STRING_LENGTH);
        print_Statistic_Flags(theStatistic);
        printf("%-60s", statisticName);
        if (theStatistic.isThresholdValid)
        {
            switch (theStatistic.threshType)
            {
            case THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE:
                snprintf_err_handle(displayThreshold, DEVICE_STATISTICS_DISPLAY_THRESHOLD_STRING_LENGTH,
                                    "%" PRIu64 " (Always Trigger)", theStatistic.threshold);
                break;
            case THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL:
                snprintf_err_handle(displayThreshold, DEVICE_STATISTICS_DISPLAY_THRESHOLD_STRING_LENGTH, "=%" PRIu64,
                                    theStatistic.threshold);
                break;
            case THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL:
                snprintf_err_handle(displayThreshold, DEVICE_STATISTICS_DISPLAY_THRESHOLD_STRING_LENGTH, "!=%" PRIu64,
                                    theStatistic.threshold);
                break;
            case THRESHOLD_TYPE_TRIGGER_WHEN_GREATER:
                snprintf_err_handle(displayThreshold, DEVICE_STATISTICS_DISPLAY_THRESHOLD_STRING_LENGTH, ">%" PRIu64,
                                    theStatistic.threshold);
                break;
            case THRESHOLD_TYPE_TRIGGER_WHEN_LESS:
                snprintf_err_handle(displayThreshold, DEVICE_STATISTICS_DISPLAY_THRESHOLD_STRING_LENGTH, "<%" PRIu64,
                                    theStatistic.threshold);
                break;
            case THRESHOLD_TYPE_NO_TRIGGER:
            default:
                snprintf_err_handle(displayThreshold, DEVICE_STATISTICS_DISPLAY_THRESHOLD_STRING_LENGTH, "%" PRIu64,
                                    theStatistic.threshold);
                break;
            }
        }
        else
        {
            snprintf_err_handle(displayThreshold, DEVICE_STATISTICS_DISPLAY_THRESHOLD_STRING_LENGTH, "N/A");
        }
        printf(" %-16s ", displayThreshold);
        if (theStatistic.isValueValid)
        {
            uint32_t exponent = M_DoubleWord0(theStatistic.statisticValue);
            uint32_t integer  = M_DoubleWord1(theStatistic.statisticValue);
            // now byteswap the double words to get the correct endianness (for LSB machines)
            byte_Swap_32(&exponent);
            byte_Swap_32(&integer);
            printf("%" PRIu32 " ", integer);
            switch (exponent)
            {
            case 1: // deci
                printf("deci seconds");
                break;
            case 2: // centi
                printf("centi seconds");
                break;
            case 3: // milli
                printf("milli seconds");
                break;
            case 6: // micro
                printf("micro seconds");
                break;
            case 9: // nano
                printf("nano seconds");
                break;
            case 12: // pico
                printf("pico seconds");
                break;
            case 15: // femto
                printf("femto seconds");
                break;
            case 18: // atto
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

// This is a different function to be more specific to SAS environmental limits/reporting pages
static void print_Environmental_Temperature_Statistic(statistic theStatistic, const char* statisticName, bool isLimit)
{
    if (theStatistic.isSupported)
    {
        DECLARE_ZERO_INIT_ARRAY(char, displayThreshold, DEVICE_STATISTICS_DISPLAY_THRESHOLD_STRING_LENGTH);
        print_Statistic_Flags(theStatistic);
        printf("%-60s", statisticName);
        if (theStatistic.isThresholdValid)
        {
            switch (theStatistic.threshType)
            {
            case THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE:
                snprintf_err_handle(displayThreshold, DEVICE_STATISTICS_DISPLAY_THRESHOLD_STRING_LENGTH,
                                    "%" PRIu64 " (Always Trigger)", theStatistic.threshold);
                break;
            case THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL:
                snprintf_err_handle(displayThreshold, DEVICE_STATISTICS_DISPLAY_THRESHOLD_STRING_LENGTH, "=%" PRIu64,
                                    theStatistic.threshold);
                break;
            case THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL:
                snprintf_err_handle(displayThreshold, DEVICE_STATISTICS_DISPLAY_THRESHOLD_STRING_LENGTH, "!=%" PRIu64,
                                    theStatistic.threshold);
                break;
            case THRESHOLD_TYPE_TRIGGER_WHEN_GREATER:
                snprintf_err_handle(displayThreshold, DEVICE_STATISTICS_DISPLAY_THRESHOLD_STRING_LENGTH, ">%" PRIu64,
                                    theStatistic.threshold);
                break;
            case THRESHOLD_TYPE_TRIGGER_WHEN_LESS:
                snprintf_err_handle(displayThreshold, DEVICE_STATISTICS_DISPLAY_THRESHOLD_STRING_LENGTH, "<%" PRIu64,
                                    theStatistic.threshold);
                break;
            case THRESHOLD_TYPE_NO_TRIGGER:
            default:
                snprintf_err_handle(displayThreshold, DEVICE_STATISTICS_DISPLAY_THRESHOLD_STRING_LENGTH, "%" PRIu64,
                                    theStatistic.threshold);
                break;
            }
        }
        else
        {
            snprintf_err_handle(displayThreshold, DEVICE_STATISTICS_DISPLAY_THRESHOLD_STRING_LENGTH, "N/A");
        }
        printf(" %-16s ", displayThreshold);
        if (theStatistic.isValueValid)
        {
            int8_t temperatureValue = C_CAST(int8_t, theStatistic.statisticValue);
            if (temperatureValue == -128)
            {
                if (isLimit)
                {
                    printf("No Temperature Limit");
                }
                else
                {
                    printf("No Valid Temperature");
                }
            }
            else
            {
                printf("%" PRId8 " C", temperatureValue);
            }
        }
        else
        {
            printf("Invalid");
        }
        printf("\n");
    }
}

static void print_Humidity_Statistic(statistic theStatistic, const char* statisticName, bool isLimit)
{
    if (theStatistic.isSupported)
    {
        DECLARE_ZERO_INIT_ARRAY(char, displayThreshold, DEVICE_STATISTICS_DISPLAY_THRESHOLD_STRING_LENGTH);
        print_Statistic_Flags(theStatistic);
        printf("%-60s", statisticName);
        if (theStatistic.isThresholdValid)
        {
            switch (theStatistic.threshType)
            {
            case THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE:
                snprintf_err_handle(displayThreshold, DEVICE_STATISTICS_DISPLAY_THRESHOLD_STRING_LENGTH,
                                    "%" PRIu64 " (Always Trigger)", theStatistic.threshold);
                break;
            case THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL:
                snprintf_err_handle(displayThreshold, DEVICE_STATISTICS_DISPLAY_THRESHOLD_STRING_LENGTH, "=%" PRIu64,
                                    theStatistic.threshold);
                break;
            case THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL:
                snprintf_err_handle(displayThreshold, DEVICE_STATISTICS_DISPLAY_THRESHOLD_STRING_LENGTH, "!=%" PRIu64,
                                    theStatistic.threshold);
                break;
            case THRESHOLD_TYPE_TRIGGER_WHEN_GREATER:
                snprintf_err_handle(displayThreshold, DEVICE_STATISTICS_DISPLAY_THRESHOLD_STRING_LENGTH, ">%" PRIu64,
                                    theStatistic.threshold);
                break;
            case THRESHOLD_TYPE_TRIGGER_WHEN_LESS:
                snprintf_err_handle(displayThreshold, DEVICE_STATISTICS_DISPLAY_THRESHOLD_STRING_LENGTH, "<%" PRIu64,
                                    theStatistic.threshold);
                break;
            case THRESHOLD_TYPE_NO_TRIGGER:
            default:
                snprintf_err_handle(displayThreshold, DEVICE_STATISTICS_DISPLAY_THRESHOLD_STRING_LENGTH, "%" PRIu64,
                                    theStatistic.threshold);
                break;
            }
        }
        else
        {
            snprintf_err_handle(displayThreshold, DEVICE_STATISTICS_DISPLAY_THRESHOLD_STRING_LENGTH, "N/A");
        }
        printf(" %-16s ", displayThreshold);
        if (theStatistic.isValueValid)
        {
            if (/*theStatistic.statisticValue >= 0 &&*/ theStatistic.statisticValue <= 100)
            {
                printf("%" PRIu8 "", C_CAST(uint8_t, theStatistic.statisticValue));
            }
            else if (theStatistic.statisticValue == 255)
            {
                if (isLimit)
                {
                    printf("No relative humidity limit");
                }
                else
                {
                    printf("No valid relative humidity");
                }
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

static eReturnValues print_ATA_DeviceStatistics(tDevice* device, ptrDeviceStatistics deviceStats)
{
    eReturnValues ret = SUCCESS;
    if (deviceStats == M_NULLPTR)
    {
        return MEMORY_FAILURE;
    }
    DECLARE_ZERO_INIT_ARRAY(char, flagPad, DEVICE_STATISTIC_FLAGS_LEN + 1);
    safe_memset(flagPad, DEVICE_STATISTIC_FLAGS_LEN + 1, ' ', DEVICE_STATISTIC_FLAGS_LEN);
    printf("===Device Statistics===\n");
    printf("\t* = condition monitored with threshold (DSN Feature)\n");
    printf("\t! = monitored condition met\n");
    printf("\t- = supports notification (DSN Feature)\n");
    printf("\t^ = supports reinitialization/reset\n");
    printf("%s%-60s %-16s %-16s\n", flagPad, "Statistic Name:", "Threshold:", "Value:");
    if (deviceStats->sataStatistics.generalStatisticsSupported)
    {
        printf("\n---General Statistics---\n");
        print_Count_Statistic(deviceStats->sataStatistics.lifetimePoweronResets, "LifeTime Power-On Resets", M_NULLPTR);
        print_Count_Statistic(deviceStats->sataStatistics.powerOnHours, "Power-On Hours", "hours");
        print_Count_Statistic(deviceStats->sataStatistics.logicalSectorsWritten, "Logical Sectors Written", M_NULLPTR);
        print_Count_Statistic(deviceStats->sataStatistics.numberOfWriteCommands, "Number Of Write Commands", M_NULLPTR);
        print_Count_Statistic(deviceStats->sataStatistics.logicalSectorsRead, "Logical Sectors Read", M_NULLPTR);
        print_Count_Statistic(deviceStats->sataStatistics.numberOfReadCommands, "Number Of Read Commands", M_NULLPTR);
        print_Date_And_Time_Timestamp_Statistic(deviceStats->sataStatistics.dateAndTimeTimestamp,
                                                "Date And Time Timestamp");
        print_Count_Statistic(deviceStats->sataStatistics.pendingErrorCount, "Pending Error Count", M_NULLPTR);
        print_Workload_Utilization_Statistic(deviceStats->sataStatistics.workloadUtilization, "Workload Utilization");
        print_Utilization_Usage_Rate_Statistic(deviceStats->sataStatistics.utilizationUsageRate,
                                               "Utilization Usage Rate");
        print_Resource_Availability_Statistic(deviceStats->sataStatistics.resourceAvailability,
                                              "Resource Availability");
        print_Random_Write_Resources_Used_Statistic(deviceStats->sataStatistics.randomWriteResourcesUsed,
                                                    "Random Write Resources Used");
    }
    if (deviceStats->sataStatistics.freeFallStatisticsSupported)
    {
        printf("\n---Free Fall Statistics---\n");
        print_Count_Statistic(deviceStats->sataStatistics.numberOfFreeFallEventsDetected,
                              "Number Of Free-Fall Events Detected", M_NULLPTR);
        print_Count_Statistic(deviceStats->sataStatistics.overlimitShockEvents, "Overlimit Shock Events", M_NULLPTR);
    }
    if (deviceStats->sataStatistics.rotatingMediaStatisticsSupported)
    {
        printf("\n---Rotating Media Statistics---\n");
        print_Count_Statistic(deviceStats->sataStatistics.spindleMotorPoweronHours, "Spindle Motor Power-On Hours",
                              "hours");
        print_Count_Statistic(deviceStats->sataStatistics.headFlyingHours, "Head Flying Hours", "hours");
        print_Count_Statistic(deviceStats->sataStatistics.headLoadEvents, "Head Load Events", M_NULLPTR);
        print_Count_Statistic(
            deviceStats->sataStatistics.numberOfReallocatedLogicalSectors, "Number Of Reallocated Logical Sectors",
            deviceStats->sataStatistics.numberOfReallocatedLogicalSectors.isNormalized ? "%" : M_NULLPTR);
        print_Count_Statistic(deviceStats->sataStatistics.readRecoveryAttempts, "Read Recovery Attempts", M_NULLPTR);
        print_Count_Statistic(deviceStats->sataStatistics.numberOfMechanicalStartFailures,
                              "Number Of Mechanical Start Failures", M_NULLPTR);
        print_Count_Statistic(deviceStats->sataStatistics.numberOfReallocationCandidateLogicalSectors,
                              "Number Of Reallocation Candidate Logical Sectors", M_NULLPTR);
        print_Count_Statistic(deviceStats->sataStatistics.numberOfHighPriorityUnloadEvents,
                              "Number Of High Priority Unload Events", M_NULLPTR);
    }
    if (deviceStats->sataStatistics.generalErrorsStatisticsSupported)
    {
        printf("\n---General Errors Statistics---\n");
        print_Count_Statistic(deviceStats->sataStatistics.numberOfReportedUncorrectableErrors,
                              "Number Of Reported Uncorrectable Errors", M_NULLPTR);
        print_Count_Statistic(deviceStats->sataStatistics.numberOfResetsBetweenCommandAcceptanceAndCommandCompletion,
                              "Number Of Resets Between Command Acceptance and Completion", M_NULLPTR);
        print_Count_Statistic(deviceStats->sataStatistics.physicalElementStatusChanged,
                              "Physical Element Status Changed", M_NULLPTR);
    }
    if (deviceStats->sataStatistics.temperatureStatisticsSupported)
    {
        printf("\n---Temperature Statistics---\n");
        print_Temperature_Statistic(deviceStats->sataStatistics.currentTemperature, "Current Temperature");
        print_Temperature_Statistic(deviceStats->sataStatistics.averageShortTermTemperature,
                                    "Average Short Term Temperature");
        print_Temperature_Statistic(deviceStats->sataStatistics.averageLongTermTemperature,
                                    "Average Long Term Temperature");
        print_Temperature_Statistic(deviceStats->sataStatistics.highestTemperature, "Highest Temperature");
        print_Temperature_Statistic(deviceStats->sataStatistics.lowestTemperature, "Lowest Temperature");
        print_Temperature_Statistic(deviceStats->sataStatistics.highestAverageShortTermTemperature,
                                    "Highest Average Short Term Temperature");
        print_Temperature_Statistic(deviceStats->sataStatistics.lowestAverageShortTermTemperature,
                                    "Lowest Average Short Term Temperature");
        print_Temperature_Statistic(deviceStats->sataStatistics.highestAverageLongTermTemperature,
                                    "Highest Average Long Term Temperature");
        print_Temperature_Statistic(deviceStats->sataStatistics.lowestAverageLongTermTemperature,
                                    "Lowest Average Long Term Temperature");
        print_Time_Minutes_Statistic(deviceStats->sataStatistics.timeInOverTemperature, "Time In Over Temperature");
        print_Temperature_Statistic(deviceStats->sataStatistics.specifiedMaximumOperatingTemperature,
                                    "Specified Maximum Operating Temperature");
        print_Time_Minutes_Statistic(deviceStats->sataStatistics.timeInUnderTemperature, "Time In Under Temperature");
        print_Temperature_Statistic(deviceStats->sataStatistics.specifiedMinimumOperatingTemperature,
                                    "Specified Minimum Operating Temperature");
    }
    if (deviceStats->sataStatistics.transportStatisticsSupported)
    {
        printf("\n---Transport Statistics---\n");
        print_Count_Statistic(deviceStats->sataStatistics.numberOfHardwareResets, "Number Of Hardware Resets",
                              M_NULLPTR);
        print_Count_Statistic(deviceStats->sataStatistics.numberOfASREvents, "Number Of ASR Events", M_NULLPTR);
        print_Count_Statistic(deviceStats->sataStatistics.numberOfInterfaceCRCErrors, "Number Of Interface CRC Errors",
                              M_NULLPTR);
    }
    if (deviceStats->sataStatistics.ssdStatisticsSupported)
    {
        printf("\n---Solid State Device Statistics---\n");
        print_Count_Statistic(deviceStats->sataStatistics.percentageUsedIndicator, "Percent Used Indicator", "%");
    }
    if (deviceStats->sataStatistics.zonedDeviceStatisticsSupported)
    {
        printf("\n---Zoned Device Statistics---\n");
        print_Count_Statistic(deviceStats->sataStatistics.maximumOpenZones, "Maximum Open Zones", M_NULLPTR);
        print_Count_Statistic(deviceStats->sataStatistics.maximumExplicitlyOpenZones, "Maximum Explicitly Open Zones",
                              M_NULLPTR);
        print_Count_Statistic(deviceStats->sataStatistics.maximumImplicitlyOpenZones, "Maximum Implicitly Open Zones",
                              M_NULLPTR);
        print_Count_Statistic(deviceStats->sataStatistics.minimumEmptyZones, "Minumum Empty Zones", M_NULLPTR);
        print_Count_Statistic(deviceStats->sataStatistics.maximumNonSequentialZones, "Maximum Non-sequential Zones",
                              M_NULLPTR);
        print_Count_Statistic(deviceStats->sataStatistics.zonesEmptied, "Zones Emptied", M_NULLPTR);
        print_Count_Statistic(deviceStats->sataStatistics.suboptimalWriteCommands, "Suboptimal Write Commands",
                              M_NULLPTR);
        print_Count_Statistic(deviceStats->sataStatistics.commandsExceedingOptimalLimit,
                              "Commands Exceeding Optimal Limit", M_NULLPTR);
        print_Count_Statistic(deviceStats->sataStatistics.failedExplicitOpens, "Failed Explicit Opens", M_NULLPTR);
        print_Count_Statistic(deviceStats->sataStatistics.readRuleViolations, "Read Rule Violations", M_NULLPTR);
        print_Count_Statistic(deviceStats->sataStatistics.writeRuleViolations, "Write Rule Violations", M_NULLPTR);
        print_Count_Statistic(deviceStats->sataStatistics.maximumImplicitOpenSequentialOrBeforeRequiredZones,
                              "Max Implicitly Open Sequential or Before Required Zones", M_NULLPTR);
    }
    if (deviceStats->sataStatistics.cdlStatisticsSupported)
    {
        printf("\n---Command Duration Limit Statistics---\n");
        print_Time_Microseconds_Statistic(deviceStats->sataStatistics.lowestAchievableCommandDuration,
                                          "Lowest Achievable Command Duration");
        // These are a bit more complicated of a structure, so printing is handled differently
        // This is due to so much reuse of statistic formatting is in the spec that this was easier to handle this way.
        for (uint8_t rangeIter = UINT8_C(0);
             rangeIter < deviceStats->sataStatistics.cdlStatisticRanges && rangeIter < MAX_CDL_STATISTIC_RANGES;
             ++rangeIter)
        {
#define RANGE_ID_STR_LEN 16
            DECLARE_ZERO_INIT_ARRAY(char, rangeID, RANGE_ID_STR_LEN);
            if (deviceStats->sataStatistics.cdlStatisticRanges > 1)
            {
                // Only print out the per-range info when multiple ranges are supported.
                // Otherwise these represent the whole device
                snprintf_err_handle(rangeID, RANGE_ID_STR_LEN, "Range %" PRIu8, rangeIter);
            }
            else
            {
                snprintf_err_handle(rangeID, RANGE_ID_STR_LEN, "Device");
            }
#define CDL_POLICY_STR_LEN 60
            // Loop through r1-r7 stat a
            for (uint8_t policyIter = UINT8_C(0); policyIter < MAX_CDL_RW_POLICIES; ++policyIter)
            {
                DECLARE_ZERO_INIT_ARRAY(char, policyName, CDL_POLICY_STR_LEN);
                snprintf_err_handle(policyName, CDL_POLICY_STR_LEN, "%s Read Policy %" PRIu8 " Stat A", rangeID,
                                    policyIter);
                print_Count_Statistic(deviceStats->sataStatistics.cdlRange[rangeIter].groupA.readPolicy[policyIter],
                                      policyName, "Invocations");
            }
            // loop through w1-w7 stat a
            for (uint8_t policyIter = UINT8_C(0); policyIter < MAX_CDL_RW_POLICIES; ++policyIter)
            {
                DECLARE_ZERO_INIT_ARRAY(char, policyName, CDL_POLICY_STR_LEN);
                snprintf_err_handle(policyName, CDL_POLICY_STR_LEN, "%s Write Policy %" PRIu8 " Stat A", rangeID,
                                    policyIter);
                print_Count_Statistic(deviceStats->sataStatistics.cdlRange[rangeIter].groupA.writePolicy[policyIter],
                                      policyName, "Invocations");
            }
            // Loop through r1-r7 stat b
            for (uint8_t policyIter = UINT8_C(0); policyIter < MAX_CDL_RW_POLICIES; ++policyIter)
            {
                DECLARE_ZERO_INIT_ARRAY(char, policyName, CDL_POLICY_STR_LEN);
                snprintf_err_handle(policyName, CDL_POLICY_STR_LEN, "%s Read Policy %" PRIu8 " Stat B", rangeID,
                                    policyIter);
                print_Count_Statistic(deviceStats->sataStatistics.cdlRange[rangeIter].groupB.readPolicy[policyIter],
                                      policyName, "Invocations");
            }
            // loop through w1-w7 stat b
            for (uint8_t policyIter = UINT8_C(0); policyIter < MAX_CDL_RW_POLICIES; ++policyIter)
            {
                DECLARE_ZERO_INIT_ARRAY(char, policyName, CDL_POLICY_STR_LEN);
                snprintf_err_handle(policyName, CDL_POLICY_STR_LEN, "%s Write Policy %" PRIu8 " Stat B", rangeID,
                                    policyIter);
                print_Count_Statistic(deviceStats->sataStatistics.cdlRange[rangeIter].groupB.writePolicy[policyIter],
                                      policyName, "Invocations");
            }
        }
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
        for (uint8_t vendorSpecificIter = UINT8_C(0), statisticsFound = UINT8_C(0);
             vendorSpecificIter < UINT8_C(64) &&
             statisticsFound < deviceStats->sataStatistics.vendorSpecificStatisticsPopulated;
             ++vendorSpecificIter)
        {
#define VENDOR_UNIQUE_DEVICE_STATISTIC_NAME_STRING_LENGTH 64
            DECLARE_ZERO_INIT_ARRAY(char, statisticName, VENDOR_UNIQUE_DEVICE_STATISTIC_NAME_STRING_LENGTH);
            if (SEAGATE == is_Seagate_Family(device))
            {
                switch (vendorSpecificIter + 1)
                {
                case 5:
                    snprintf_err_handle(statisticName, VENDOR_UNIQUE_DEVICE_STATISTIC_NAME_STRING_LENGTH,
                                        "Servo Activation Stop Timestamp");
                    break;
                case 4:
                    snprintf_err_handle(statisticName, VENDOR_UNIQUE_DEVICE_STATISTIC_NAME_STRING_LENGTH,
                                        "Servo Activation Start Timestamp");
                    break;
                case 3:
                    snprintf_err_handle(statisticName, VENDOR_UNIQUE_DEVICE_STATISTIC_NAME_STRING_LENGTH,
                                        "Read Error Rate Head Failure Bit Map");
                    break;
                case 2:
                    snprintf_err_handle(statisticName, VENDOR_UNIQUE_DEVICE_STATISTIC_NAME_STRING_LENGTH,
                                        "Number of Servo Unloads");
                    break;
                case 1:
                    snprintf_err_handle(statisticName, VENDOR_UNIQUE_DEVICE_STATISTIC_NAME_STRING_LENGTH,
                                        "Pressure Min/Max Reached");
                    break;
                default:
                    snprintf_err_handle(statisticName, VENDOR_UNIQUE_DEVICE_STATISTIC_NAME_STRING_LENGTH,
                                        "Vendor Specific Statistic %" PRIu8, vendorSpecificIter + 1);
                    break;
                }
            }
            else
            {
                snprintf_err_handle(statisticName, VENDOR_UNIQUE_DEVICE_STATISTIC_NAME_STRING_LENGTH,
                                    "Vendor Specific Statistic %" PRIu8, vendorSpecificIter + 1);
            }
            if (deviceStats->sataStatistics.vendorSpecificStatistics[vendorSpecificIter].isSupported)
            {
                print_Count_Statistic(deviceStats->sataStatistics.vendorSpecificStatistics[vendorSpecificIter],
                                      statisticName, M_NULLPTR);
                ++statisticsFound;
            }
        }
    }
    return ret;
}

static eReturnValues print_SCSI_DeviceStatistics(M_ATTR_UNUSED tDevice* device, ptrDeviceStatistics deviceStats)
{
    eReturnValues ret = SUCCESS;
    if (deviceStats == M_NULLPTR)
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
        print_Count_Statistic(deviceStats->sasStatistics.writeErrorsCorrectedWithoutSubstantialDelay,
                              "Write Errors Corrected Without Substantial Delay", M_NULLPTR);
        print_Count_Statistic(deviceStats->sasStatistics.writeErrorsCorrectedWithPossibleDelays,
                              "Write Errors Corrected With Possible Delay", M_NULLPTR);
        print_Count_Statistic(deviceStats->sasStatistics.writeTotalReWrites, "Write Total Rewrites", M_NULLPTR);
        print_Count_Statistic(deviceStats->sasStatistics.writeErrorsCorrected, "Write Errors Corrected", M_NULLPTR);
        print_Count_Statistic(deviceStats->sasStatistics.writeTotalTimeCorrectionAlgorithmProcessed,
                              "Write Total Times Corrective Algorithm Processed", M_NULLPTR);
        print_Count_Statistic(deviceStats->sasStatistics.writeTotalBytesProcessed, "Write Total Bytes Processed",
                              M_NULLPTR);
        print_Count_Statistic(deviceStats->sasStatistics.writeTotalUncorrectedErrors, "Write Total Uncorrected Errors",
                              M_NULLPTR);
    }
    if (deviceStats->sasStatistics.readErrorCountersSupported)
    {
        printf("\n---Read Error Counters---\n");
        print_Count_Statistic(deviceStats->sasStatistics.readErrorsCorrectedWithPossibleDelays,
                              "Read Errors Corrected With Possible Delay", M_NULLPTR);
        print_Count_Statistic(deviceStats->sasStatistics.readTotalRereads, "Read Total Rereads", M_NULLPTR);
        print_Count_Statistic(deviceStats->sasStatistics.readErrorsCorrected, "Read Errors Corrected", M_NULLPTR);
        print_Count_Statistic(deviceStats->sasStatistics.readTotalTimeCorrectionAlgorithmProcessed,
                              "Read Total Times Corrective Algorithm Processed", M_NULLPTR);
        print_Count_Statistic(deviceStats->sasStatistics.readTotalBytesProcessed, "Read Total Bytes Processed",
                              M_NULLPTR);
        print_Count_Statistic(deviceStats->sasStatistics.readTotalUncorrectedErrors, "Read Total Uncorrected Errors",
                              M_NULLPTR);
    }
    if (deviceStats->sasStatistics.readReverseErrorCountersSupported)
    {
        printf("\n---Read Reverse Error Counters---\n");
        print_Count_Statistic(deviceStats->sasStatistics.readReverseErrorsCorrectedWithoutSubstantialDelay,
                              "Read Reverse Errors Corrected Without Substantial Delay", M_NULLPTR);
        print_Count_Statistic(deviceStats->sasStatistics.readReverseErrorsCorrectedWithPossibleDelays,
                              "Read Reverse Errors Corrected With Possible Delay", M_NULLPTR);
        print_Count_Statistic(deviceStats->sasStatistics.readReverseTotalReReads, "Read Reverse Total Rereads",
                              M_NULLPTR);
        print_Count_Statistic(deviceStats->sasStatistics.readReverseErrorsCorrected, "Read Reverse Errors Corrected",
                              M_NULLPTR);
        print_Count_Statistic(deviceStats->sasStatistics.readReverseTotalTimeCorrectionAlgorithmProcessed,
                              "Read Reverse Total Times Corrective Algorithm Processed", M_NULLPTR);
        print_Count_Statistic(deviceStats->sasStatistics.readReverseTotalBytesProcessed,
                              "Read Reverse Total Bytes Processed", M_NULLPTR);
        print_Count_Statistic(deviceStats->sasStatistics.readReverseTotalUncorrectedErrors,
                              "Read Reverse Total Uncorrected Errors", M_NULLPTR);
    }
    if (deviceStats->sasStatistics.verifyErrorCountersSupported)
    {
        printf("\n---Verify Error Counters---\n");
        print_Count_Statistic(deviceStats->sasStatistics.verifyErrorsCorrectedWithoutSubstantialDelay,
                              "Verify Errors Corrected Without Substantial Delay", M_NULLPTR);
        print_Count_Statistic(deviceStats->sasStatistics.verifyErrorsCorrectedWithPossibleDelays,
                              "Verify Errors Corrected With Possible Delay", M_NULLPTR);
        print_Count_Statistic(deviceStats->sasStatistics.verifyTotalReVerifies, "Verify Total Rereads", M_NULLPTR);
        print_Count_Statistic(deviceStats->sasStatistics.verifyErrorsCorrected, "Verify Errors Corrected", M_NULLPTR);
        print_Count_Statistic(deviceStats->sasStatistics.verifyTotalTimeCorrectionAlgorithmProcessed,
                              "Verify Total Times Corrective Algorithm Processed", M_NULLPTR);
        print_Count_Statistic(deviceStats->sasStatistics.verifyTotalBytesProcessed, "Verify Total Bytes Processed",
                              M_NULLPTR);
        print_Count_Statistic(deviceStats->sasStatistics.verifyTotalUncorrectedErrors,
                              "Verify Total Uncorrected Errors", M_NULLPTR);
    }
    if (deviceStats->sasStatistics.nonMediumErrorSupported)
    {
        printf("\n---Non Medium Error---\n");
        print_Count_Statistic(deviceStats->sasStatistics.nonMediumErrorCount, "Non-Medium Error Count", M_NULLPTR);
    }
    if (deviceStats->sasStatistics.formatStatusSupported)
    {
        printf("\n---Format Status---\n");
        print_Count_Statistic(deviceStats->sasStatistics.grownDefectsDuringCertification,
                              "Grown Defects During Certification", M_NULLPTR);
        print_Count_Statistic(deviceStats->sasStatistics.totalBlocksReassignedDuringFormat,
                              "Total Blocks Reassigned During Format", M_NULLPTR);
        print_Count_Statistic(deviceStats->sasStatistics.totalNewBlocksReassigned, "Total New Blocks Reassigned",
                              M_NULLPTR);
        print_Count_Statistic(deviceStats->sasStatistics.powerOnMinutesSinceFormat,
                              "Power On Minutes Since Last Format", "minutes");
    }
    if (deviceStats->sasStatistics.logicalBlockProvisioningSupported)
    {
        printf("\n---Logical Block Provisioning---\n");
        print_Count_Statistic(deviceStats->sasStatistics.availableLBAMappingresourceCount,
                              "Available LBA Mapping Resource Count", M_NULLPTR);
        print_Count_Statistic(deviceStats->sasStatistics.usedLBAMappingResourceCount, "Used LBA Mapping Resource Count",
                              M_NULLPTR);
        print_Count_Statistic(deviceStats->sasStatistics.availableProvisioningResourcePercentage,
                              "Available Provisioning Resource Percentage", "%");
        print_Count_Statistic(deviceStats->sasStatistics.deduplicatedLBAResourceCount,
                              "De-duplicted LBA Resource Count", M_NULLPTR);
        print_Count_Statistic(deviceStats->sasStatistics.compressedLBAResourceCount, "Compressed LBA Resource Count",
                              M_NULLPTR);
        print_Count_Statistic(deviceStats->sasStatistics.totalEfficiencyLBAResourceCount,
                              "Total Efficiency LBA Resource Count", M_NULLPTR);
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
        print_Environmental_Temperature_Statistic(deviceStats->sasStatistics.currentTemperature, "Temperature", false);
        print_Environmental_Temperature_Statistic(deviceStats->sasStatistics.lifetimeMaximumTemperature,
                                                  "Lifetime Maximum Temperature", false);
        print_Environmental_Temperature_Statistic(deviceStats->sasStatistics.lifetimeMinimumTemperature,
                                                  "Lifetime Minimum Temperature", false);
        print_Environmental_Temperature_Statistic(deviceStats->sasStatistics.maximumTemperatureSincePowerOn,
                                                  "Maximum Temperature Since Power On", false);
        print_Environmental_Temperature_Statistic(deviceStats->sasStatistics.minimumTemperatureSincePowerOn,
                                                  "Minimum Temperature Since Power On", false);
        print_Environmental_Temperature_Statistic(deviceStats->sasStatistics.maximumOtherTemperature,
                                                  "Maximum Other Temperature", false);
        print_Environmental_Temperature_Statistic(deviceStats->sasStatistics.minimumOtherTemperature,
                                                  "Minimum Other Temperature", false);
        print_Humidity_Statistic(deviceStats->sasStatistics.currentRelativeHumidity, "Relative Humidity", false);
        print_Humidity_Statistic(deviceStats->sasStatistics.lifetimeMaximumRelativeHumidity,
                                 "Lifetime Maximum Relative Humidity", false);
        print_Humidity_Statistic(deviceStats->sasStatistics.lifetimeMinumumRelativeHumidity,
                                 "Lifetime Minimum Relative Humidity", false);
        print_Humidity_Statistic(deviceStats->sasStatistics.maximumRelativeHumiditySincePoweron,
                                 "Maximum Relative Humidity Since Power On", false);
        print_Humidity_Statistic(deviceStats->sasStatistics.minimumRelativeHumiditySincePoweron,
                                 "Minimum Relative Humidity Since Power On", false);
        print_Humidity_Statistic(deviceStats->sasStatistics.maximumOtherRelativeHumidity,
                                 "Maximum Other Relative Humidity", false);
        print_Humidity_Statistic(deviceStats->sasStatistics.minimumOtherRelativeHumidity,
                                 "Minimum Other Relative Humidity", false);
    }
    if (deviceStats->sasStatistics.environmentReportingSupported)
    {
        printf("\n---Environmental Limits---\n");
        print_Environmental_Temperature_Statistic(deviceStats->sasStatistics.highCriticalTemperatureLimitTrigger,
                                                  "High Critical Temperature Limit Trigger", true);
        print_Environmental_Temperature_Statistic(deviceStats->sasStatistics.highCriticalTemperatureLimitReset,
                                                  "High Critical Temperature Limit Reset", true);
        print_Environmental_Temperature_Statistic(deviceStats->sasStatistics.lowCriticalTemperatureLimitReset,
                                                  "Low Critical Temperature Limit Reset", true);
        print_Environmental_Temperature_Statistic(deviceStats->sasStatistics.lowCriticalTemperatureLimitTrigger,
                                                  "Low Critical Temperature Limit Trigger", true);
        print_Environmental_Temperature_Statistic(deviceStats->sasStatistics.highOperatingTemperatureLimitTrigger,
                                                  "High Operating Temperature Limit Trigger", true);
        print_Environmental_Temperature_Statistic(deviceStats->sasStatistics.highOperatingTemperatureLimitReset,
                                                  "High Operating Temperature Limit Reset", true);
        print_Environmental_Temperature_Statistic(deviceStats->sasStatistics.lowOperatingTemperatureLimitReset,
                                                  "Low Operating Temperature Limit Reset", true);
        print_Environmental_Temperature_Statistic(deviceStats->sasStatistics.lowOperatingTemperatureLimitTrigger,
                                                  "Low Operating Temperature Limit Trigger", true);
        print_Humidity_Statistic(deviceStats->sasStatistics.highCriticalHumidityLimitTrigger,
                                 "High Critical Relative Humidity Limit Trigger", true);
        print_Humidity_Statistic(deviceStats->sasStatistics.highCriticalHumidityLimitReset,
                                 "High Critical Relative Humidity Limit Reset", true);
        print_Humidity_Statistic(deviceStats->sasStatistics.lowCriticalHumidityLimitReset,
                                 "Low Critical Relative Humidity Limit Reset", true);
        print_Humidity_Statistic(deviceStats->sasStatistics.lowCriticalHumidityLimitTrigger,
                                 "Low Critical Relative Humidity Limit Trigger", true);
        print_Humidity_Statistic(deviceStats->sasStatistics.highOperatingHumidityLimitTrigger,
                                 "High Operating Relative Humidity Limit Trigger", true);
        print_Humidity_Statistic(deviceStats->sasStatistics.highOperatingHumidityLimitReset,
                                 "High Operating Relative Humidity Limit Reset", true);
        print_Humidity_Statistic(deviceStats->sasStatistics.lowOperatingHumidityLimitReset,
                                 "Low Operating Relative Humidity Limit Reset", true);
        print_Humidity_Statistic(deviceStats->sasStatistics.lowOperatingHumidityLimitTrigger,
                                 "Low Operating Relative Humidity Limit Trigger", true);
    }
    if (deviceStats->sasStatistics.startStopCycleCounterSupported)
    {
        printf("\n---Start-Stop Cycle Counter---\n");
        print_SCSI_Date_Statistic(deviceStats->sasStatistics.dateOfManufacture, "Date Of Manufacture");
        print_SCSI_Date_Statistic(deviceStats->sasStatistics.accountingDate, "Accounting Date");
        print_Count_Statistic(deviceStats->sasStatistics.specifiedCycleCountOverDeviceLifetime,
                              "Specified Cycle Count Over Device Lifetime", M_NULLPTR);
        print_Count_Statistic(deviceStats->sasStatistics.accumulatedStartStopCycles, "Accumulated Start-Stop Cycles",
                              M_NULLPTR);
        print_Count_Statistic(deviceStats->sasStatistics.specifiedLoadUnloadCountOverDeviceLifetime,
                              "Specified Load-Unload Count Over Device Lifetime", M_NULLPTR);
        print_Count_Statistic(deviceStats->sasStatistics.accumulatedLoadUnloadCycles, "Accumulated Load-Unload Cycles",
                              M_NULLPTR);
    }
    if (deviceStats->sasStatistics.powerConditionTransitionsSupported)
    {
        printf("\n---Power Condition Transitions---\n");
        print_Count_Statistic(deviceStats->sasStatistics.transitionsToActive, "Accumulated Transitions to Active",
                              M_NULLPTR);
        print_Count_Statistic(deviceStats->sasStatistics.transitionsToIdleA, "Accumulated Transitions to Idle A",
                              M_NULLPTR);
        print_Count_Statistic(deviceStats->sasStatistics.transitionsToIdleB, "Accumulated Transitions to Idle B",
                              M_NULLPTR);
        print_Count_Statistic(deviceStats->sasStatistics.transitionsToIdleC, "Accumulated Transitions to Idle C",
                              M_NULLPTR);
        print_Count_Statistic(deviceStats->sasStatistics.transitionsToStandbyZ, "Accumulated Transitions to Standby Z",
                              M_NULLPTR);
        print_Count_Statistic(deviceStats->sasStatistics.transitionsToStandbyY, "Accumulated Transitions to Standby Y",
                              M_NULLPTR);
    }
    if (deviceStats->sasStatistics.utilizationSupported)
    {
        printf("\n---Utilization---\n");
        print_Workload_Utilization_Statistic(deviceStats->sasStatistics.workloadUtilization, "Workload Utilization");
        print_Utilization_Usage_Rate_Statistic(deviceStats->sasStatistics.utilizationUsageRateBasedOnDateAndTime,
                                               "Utilization Usage Rate");
    }
    if (deviceStats->sasStatistics.solidStateMediaSupported)
    {
        printf("\n---Solid State Media---\n");
        print_Count_Statistic(deviceStats->sasStatistics.percentUsedEndurance, "Percent Used Endurance", "%");
    }
    if (deviceStats->sasStatistics.backgroundScanResultsSupported)
    {
        printf("\n---Background Scan Results---\n");
        print_Count_Statistic(deviceStats->sasStatistics.accumulatedPowerOnMinutes, "Accumulated Power On Minutes",
                              "minutes");
        print_Count_Statistic(deviceStats->sasStatistics.numberOfBackgroundScansPerformed,
                              "Number Of Background Scans Performed", M_NULLPTR);
        print_Count_Statistic(deviceStats->sasStatistics.numberOfBackgroundMediaScansPerformed,
                              "Number Of Background Media Scans Performed", M_NULLPTR);
    }
    if (deviceStats->sasStatistics.defectStatisticsSupported)
    {
        printf("\n---Defect Statistics---\n");
        print_Count_Statistic(deviceStats->sasStatistics.grownDefects, "Grown Defects", M_NULLPTR);
        print_Count_Statistic(deviceStats->sasStatistics.primaryDefects, "Primary Defects", M_NULLPTR);
    }
    if (deviceStats->sasStatistics.pendingDefectsSupported)
    {
        printf("\n---Pending Defects---\n");
        print_Count_Statistic(deviceStats->sasStatistics.pendingDefectCount, "Pending Defect Count", M_NULLPTR);
    }
    if (deviceStats->sasStatistics.lpsMisalignmentSupported)
    {
        printf("\n---LPS Misalignment---\n");
        print_Count_Statistic(deviceStats->sasStatistics.lpsMisalignmentCount, "LPS Misalignment Count", M_NULLPTR);
    }
    if (deviceStats->sasStatistics.nvCacheSupported)
    {
        printf("\n---Non-Volatile Cache---\n");
        print_Non_Volatile_Time_Statistic(deviceStats->sasStatistics.remainingNonvolatileTime,
                                          "Remaining Non-Volatile Time");
        print_Non_Volatile_Time_Statistic(deviceStats->sasStatistics.maximumNonvolatileTime,
                                          "Maximum Non-Volatile Time");
    }
    if (deviceStats->sasStatistics.generalStatisticsAndPerformanceSupported)
    {
        printf("\n---General Statistics And Performance---\n");
        print_Count_Statistic(deviceStats->sasStatistics.numberOfReadCommands, "Number Of Read Commands", M_NULLPTR);
        print_Count_Statistic(deviceStats->sasStatistics.numberOfWriteCommands, "Number Of Write Commands", M_NULLPTR);
        print_Count_Statistic(deviceStats->sasStatistics.numberOfLogicalBlocksReceived,
                              "Number Of Logical Blocks Received", M_NULLPTR);
        print_Count_Statistic(deviceStats->sasStatistics.numberOfLogicalBlocksTransmitted,
                              "Number Of Logical Blocks Transmitted", M_NULLPTR);
        print_Count_Statistic(deviceStats->sasStatistics.readCommandProcessingIntervals,
                              "Read Command Processing Intervals", M_NULLPTR);
        print_Count_Statistic(deviceStats->sasStatistics.writeCommandProcessingIntervals,
                              "Write Command Processing Intervals", M_NULLPTR);
        print_Count_Statistic(deviceStats->sasStatistics.weightedNumberOfReadCommandsPlusWriteCommands,
                              "Weighted Number Of Read Commands Plus Write Commands", M_NULLPTR);
        print_Count_Statistic(deviceStats->sasStatistics.weightedReadCommandProcessingPlusWriteCommandProcessing,
                              "Weighted Number Of Read Command Processing Plus Write Command Processing", M_NULLPTR);
        print_Count_Statistic(deviceStats->sasStatistics.idleTimeIntervals, "Idle Time Intervals", M_NULLPTR);
        print_SCSI_Time_Interval_Statistic(deviceStats->sasStatistics.timeIntervalDescriptor,
                                           "Time Interval Desriptor");
        print_Count_Statistic(deviceStats->sasStatistics.numberOfReadFUACommands, "Number Of Read FUA Commands",
                              M_NULLPTR);
        print_Count_Statistic(deviceStats->sasStatistics.numberOfWriteFUACommands, "Number Of Write FUA Commands",
                              M_NULLPTR);
        print_Count_Statistic(deviceStats->sasStatistics.numberOfReadFUANVCommands, "Number Of Read FUA NV Commands",
                              M_NULLPTR);
        print_Count_Statistic(deviceStats->sasStatistics.numberOfWriteFUANVCommands, "Number Of Write FUA NV Commands",
                              M_NULLPTR);
        print_Count_Statistic(deviceStats->sasStatistics.readFUACommandProcessingIntervals,
                              "Read FUA Command Processing Intervals", M_NULLPTR);
        print_Count_Statistic(deviceStats->sasStatistics.writeFUACommandProcessingIntervals,
                              "Write FUA Command Processing Intervals", M_NULLPTR);
        print_Count_Statistic(deviceStats->sasStatistics.readFUANVCommandProcessingIntervals,
                              "Read FUA NV Command Processing Intervals", M_NULLPTR);
        print_Count_Statistic(deviceStats->sasStatistics.writeFUANVCommandProcessingIntervals,
                              "Write FUA NV Command Processing Intervals", M_NULLPTR);
    }
    if (deviceStats->sasStatistics.cacheMemoryStatisticsSupported)
    {
        printf("\n---Cache Memory Statistics---\n");
        print_Count_Statistic(deviceStats->sasStatistics.readCacheMemoryHits, "Read Cache Memory Hits", M_NULLPTR);
        print_Count_Statistic(deviceStats->sasStatistics.readsToCacheMemory, "Reads To Cache Memory", M_NULLPTR);
        print_Count_Statistic(deviceStats->sasStatistics.writeCacheMemoryHits, "Write Cache Memory Hits", M_NULLPTR);
        print_Count_Statistic(deviceStats->sasStatistics.writesFromCacheMemory, "Writes From Cache Memory", M_NULLPTR);
        print_Count_Statistic(deviceStats->sasStatistics.timeFromLastHardReset, "Last Hard Reset Intervals", M_NULLPTR);
        print_SCSI_Time_Interval_Statistic(deviceStats->sasStatistics.cacheTimeInterval, "Cache Memory Time Interval");
    }
    if (deviceStats->sasStatistics.timeStampSupported)
    {
        printf("\n---Timestamp---\n");
        print_Date_And_Time_Timestamp_Statistic(deviceStats->sasStatistics.dateAndTimeTimestamp,
                                                "Date And Time Timestamp");
    }
    if (deviceStats->sasStatistics.zonedDeviceStatisticsSupported)
    {
        printf("\n---Zoned Device Statistics---\n");
        print_Count_Statistic(deviceStats->sasStatistics.maximumOpenZones, "Maximum Open Zones", M_NULLPTR);
        print_Count_Statistic(deviceStats->sasStatistics.maximumExplicitlyOpenZones, "Maximum Explicitly Open Zones",
                              M_NULLPTR);
        print_Count_Statistic(deviceStats->sasStatistics.maximumImplicitlyOpenZones, "Maximum Implicitly Open Zones",
                              M_NULLPTR);
        print_Count_Statistic(deviceStats->sasStatistics.minimumEmptyZones, "Minumum Empty Zones", M_NULLPTR);
        print_Count_Statistic(deviceStats->sasStatistics.maximumNonSequentialZones, "Maximum Non-sequential Zones",
                              M_NULLPTR);
        print_Count_Statistic(deviceStats->sasStatistics.zonesEmptied, "Zones Emptied", M_NULLPTR);
        print_Count_Statistic(deviceStats->sasStatistics.suboptimalWriteCommands, "Suboptimal Write Commands",
                              M_NULLPTR);
        print_Count_Statistic(deviceStats->sasStatistics.commandsExceedingOptimalLimit,
                              "Commands Exceeding Optimal Limit", M_NULLPTR);
        print_Count_Statistic(deviceStats->sasStatistics.failedExplicitOpens, "Failed Explicit Opens", M_NULLPTR);
        print_Count_Statistic(deviceStats->sasStatistics.readRuleViolations, "Read Rule Violations", M_NULLPTR);
        print_Count_Statistic(deviceStats->sasStatistics.writeRuleViolations, "Write Rule Violations", M_NULLPTR);
        print_Count_Statistic(deviceStats->sasStatistics.maxImplicitlyOpenSeqOrBeforeReqZones,
                              "Maximum Implicitly Open Sequential Or Before Required Zones", M_NULLPTR);
    }
    if (deviceStats->sasStatistics.protocolSpecificStatisticsSupported)
    {
        if (deviceStats->sasStatistics.protocolStatisticsType == STAT_PROT_SAS)
        {
            printf("\n---SAS Protocol Statistics---\n");
            // SAS protocol can have multiple ports and multiple phys per port
            // So this needs to loop and output which port ID and phy ID each statistic is for
            for (uint16_t portIter = UINT16_C(0);
                 portIter < SAS_STATISTICS_MAX_PORTS && portIter < deviceStats->sasStatistics.sasProtStats.portCount;
                 ++portIter)
            {
                if (deviceStats->sasStatistics.sasProtStats.sasStatsPerPort[portIter].sasProtStatsValid)
                {
                    for (uint8_t phyIter = UINT8_C(0);
                         phyIter < SAS_STATISTICS_MAX_PHYS &&
                         phyIter < deviceStats->sasStatistics.sasProtStats.sasStatsPerPort[portIter].phyCount;
                         ++phyIter)
                    {
                        if (deviceStats->sasStatistics.sasProtStats.sasStatsPerPort[portIter]
                                .perPhy[phyIter]
                                .sasPhyStatsValid)
                        {
                            printf("\t--Port %" PRIu16 " - Phy %" PRIu16 "--\n",
                                   deviceStats->sasStatistics.sasProtStats.sasStatsPerPort[portIter].portID,
                                   deviceStats->sasStatistics.sasProtStats.sasStatsPerPort[portIter]
                                       .perPhy[phyIter]
                                       .phyID);
                            print_Count_Statistic(deviceStats->sasStatistics.sasProtStats.sasStatsPerPort[portIter]
                                                      .perPhy[phyIter]
                                                      .invalidDWORDCount,
                                                  "Invalid Dword Count", M_NULLPTR);
                            print_Count_Statistic(deviceStats->sasStatistics.sasProtStats.sasStatsPerPort[portIter]
                                                      .perPhy[phyIter]
                                                      .runningDisparityErrorCount,
                                                  "Running Disparit Error Count", M_NULLPTR);
                            print_Count_Statistic(deviceStats->sasStatistics.sasProtStats.sasStatsPerPort[portIter]
                                                      .perPhy[phyIter]
                                                      .lossOfDWORDSynchronizationCount,
                                                  "Loss of Dword Snchronization Count", M_NULLPTR);
                            print_Count_Statistic(deviceStats->sasStatistics.sasProtStats.sasStatsPerPort[portIter]
                                                      .perPhy[phyIter]
                                                      .phyResetProblemCount,
                                                  "Phy Reset Problem Count", M_NULLPTR);
                        }
                    }
                }
            }
        }
    }
    return ret;
}

eReturnValues print_DeviceStatistics(tDevice* device, ptrDeviceStatistics deviceStats)
{
    eReturnValues ret = NOT_SUPPORTED;
    DISABLE_NONNULL_COMPARE
    if (deviceStats == M_NULLPTR)
    {
        return MEMORY_FAILURE;
    }
    RESTORE_NONNULL_COMPARE
    // as I write this I'm going to try and keep ATA and SCSI having the same printout format, but that may need to
    // change...-TJE
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

static M_INLINE bool is_ATA_Timestamp_Supported(tDevice* device)
{
    bool supported = false;
    // This command is supported when the date and time timestamp statistic is supported
    DECLARE_ZERO_INIT_ARRAY(uint8_t, devStats, ATA_LOG_PAGE_LEN_BYTES);
    if (SUCCESS == send_ATA_Read_Log_Ext_Cmd(device, ATA_LOG_DEVICE_STATISTICS, 0, devStats, ATA_LOG_PAGE_LEN_BYTES, 0))
    {
        // Check that general statistics page is supported, then read that page and check if the timestamp statistic
        // is supported
        bool generalStatsSupported = false;
        for (uint8_t pageIter = UINT8_C(0); pageIter < devStats[ATA_DEV_STATS_SUP_PG_LIST_LEN_OFFSET]; ++pageIter)
        {
            if (devStats[ATA_DEV_STATS_SUP_PG_LIST_OFFSET + pageIter] == ATA_DEVICE_STATS_LOG_GENERAL)
            {
                generalStatsSupported = true;
                break;
            }
        }
        if (generalStatsSupported)
        {
            // Now read this page and find the timestamp statistic to make sure it is supported.
            safe_memset(devStats, ATA_LOG_PAGE_LEN_BYTES, 0, ATA_LOG_PAGE_LEN_BYTES);
            if (SUCCESS == send_ATA_Read_Log_Ext_Cmd(device, ATA_LOG_DEVICE_STATISTICS, ATA_DEVICE_STATS_LOG_GENERAL,
                                                     devStats, ATA_LOG_PAGE_LEN_BYTES, 0))
            {
                uint64_t* qwordPtr = M_REINTERPRET_CAST(uint64_t*, &devStats[0]);
                if (M_Byte2(le64_to_host(*qwordPtr)) == ATA_DEVICE_STATS_LOG_GENERAL &&
                    M_Word0(le64_to_host(*qwordPtr)) == ATA_DEV_STATS_VERSION_1)
                {
                    statistic dateAndTime;
                    safe_memset(&dateAndTime, sizeof(statistic), 0, sizeof(statistic));
                    if (set_ATA_Dev_Stat_Info(le64_to_host(*(qwordPtr + 7)), &dateAndTime))
                    {
                        supported = dateAndTime.isSupported;
                    }
                }
            }
        }
    }
    return supported;
}

static M_INLINE bool is_SCSI_Timestamp_Supported(tDevice* device)
{
    bool     supported = false;
    uint32_t ctrlexLen = UINT32_C(0);
    if (SUCCESS == get_SCSI_Mode_Page_Size(device, MPC_CURRENT_VALUES, MP_CONTROL, 0x01, &ctrlexLen))
    {
        uint8_t* mp = M_REINTERPRET_CAST(
            uint8_t*, safe_calloc_aligned(ctrlexLen, sizeof(uint8_t), device->os_info.minimumAlignment));
        if (mp != M_NULLPTR)
        {
            bool used6b = false;
            if (SUCCESS == get_SCSI_Mode_Page(device, MPC_CURRENT_VALUES, MP_CONTROL, 0x01, M_NULLPTR, M_NULLPTR, true,
                                              mp, ctrlexLen, M_NULLPTR, &used6b))
            {
                uint16_t modeDataLen = UINT16_C(0);
                uint16_t blkDescLen  = UINT16_C(0);
                get_SBC_Mode_Header_Blk_Desc_Fields(used6b, mp, ctrlexLen, &modeDataLen, M_NULLPTR, M_NULLPTR,
                                                    M_NULLPTR, &blkDescLen, M_NULLPTR, M_NULLPTR);
                uint32_t mpOffset = MODE_PARAMETER_HEADER_6_LEN + blkDescLen;
                if ((mp[mpOffset + 4] & BIT1) > 0) // SCSIP bit is set to 1
                {
                    supported = true;
                }
            }
            safe_free_aligned(&mp);
        }
    }
    return supported;
}

bool is_Timestamp_Supported(tDevice* device)
{
    bool supported = false;
    if (device->drive_info.drive_type == ATA_DRIVE)
    {
        supported = is_ATA_Timestamp_Supported(device);
    }
    else if (device->drive_info.drive_type == NVME_DRIVE)
    {
        if (le16_to_host(device->drive_info.IdentifyData.nvme.ctrl.oncs) & BIT6)
        {
            supported = true;
        }
    }
    else // scsi drive
    {
        supported = is_SCSI_Timestamp_Supported(device);
    }
    return supported;
}

eReturnValues set_Date_And_Time_Timestamp(tDevice* device)
{
    eReturnValues ret  = NOT_SUPPORTED;
    uint64_t      time = get_Milliseconds_Since_Unix_Epoch();
    if (device->drive_info.drive_type == ATA_DRIVE)
    {
        if (is_Timestamp_Supported(device))
        {
            ret = ata_Set_Date_And_Time(device, time);
        }
    }
    else if (device->drive_info.drive_type == SCSI_DRIVE)
    {
        // NOTE: Requires scsip bit on control extension mode page to be set to 1, otherwise you get an error
        if (is_Timestamp_Supported(device))
        {
            DECLARE_ZERO_INIT_ARRAY(uint8_t, timestampParam, 12);
            timestampParam[4] = M_Byte5(time);
            timestampParam[5] = M_Byte4(time);
            timestampParam[6] = M_Byte3(time);
            timestampParam[7] = M_Byte2(time);
            timestampParam[8] = M_Byte1(time);
            timestampParam[9] = M_Byte0(time);
            ret               = scsi_Set_Timestamp(device, SIZE_OF_STACK_ARRAY(timestampParam), timestampParam);
        }
    }
    else if (device->drive_info.drive_type == NVME_DRIVE)
    {
        if (is_Timestamp_Supported(device))
        {
            nvmeFeaturesCmdOpt setTimestamp;
            DECLARE_ZERO_INIT_ARRAY(uint8_t, timestampData, 8);
            safe_memset(&setTimestamp, sizeof(nvmeFeaturesCmdOpt), 0, sizeof(nvmeFeaturesCmdOpt));
            timestampData[0]             = M_Byte0(time);
            timestampData[1]             = M_Byte1(time);
            timestampData[2]             = M_Byte2(time);
            timestampData[3]             = M_Byte3(time);
            timestampData[4]             = M_Byte4(time);
            timestampData[5]             = M_Byte5(time);
            setTimestamp.dataLength      = SIZE_OF_STACK_ARRAY(timestampData);
            setTimestamp.dataPtr         = timestampData;
            setTimestamp.nsid            = NVME_ALL_NAMESPACES;
            setTimestamp.featSetGetValue = NVME_FEAT_TIMESTAMP_;
            ret                          = nvme_Set_Features(device, &setTimestamp);
        }
    }
    return ret;
}

// NOTE: If reinitializeRequest is the first page (page 0, list of supported pages), this means reset all pages - TJE
// Next enhancement: Compare the values read during reinitialization to reading again afterwards. Determine which
// statistics were reset to provide a list to share with the user
// NOTE: While this log can be read with smart read log, it can only be reinitialized with read log ext commands - TJE
eReturnValues ata_Device_Statistics_Reinitialize(tDevice* device, eDeviceStatisticsLog reinitializeRequest)
{
    eReturnValues ret = NOT_SUPPORTED;
    if (device->drive_info.drive_type == ATA_DRIVE)
    {
        ret = SUCCESS;
        if (reinitializeRequest == ATA_DEVICE_STATS_LOG_LIST)
        {
            // Reinitialize all pages
            uint32_t devStatsFullLen = UINT32_C(0);
            ret = get_ATA_Log_Size(device, ATA_LOG_DEVICE_STATISTICS, &devStatsFullLen, true, false);
            if (SUCCESS == ret)
            {
                uint8_t* devStats = M_REINTERPRET_CAST(
                    uint8_t*, calloc_aligned(devStatsFullLen, sizeof(uint8_t), device->os_info.minimumAlignment));
                if (devStats != M_NULLPTR)
                {
                    ret =
                        get_ATA_Log(device, ATA_LOG_DEVICE_STATISTICS, M_NULLPTR, M_NULLPTR, true, false, true,
                                    devStats, devStatsFullLen, M_NULLPTR, 0, ATA_DEV_STATS_READ_AND_REINITIALIZE_FEAT);
                    safe_free_aligned(&devStats);
                }
                else
                {
                    ret = MEMORY_FAILURE;
                }
            }
            else
            {
                ret = NOT_SUPPORTED;
            }
        }
        else
        {
            DECLARE_ZERO_INIT_ARRAY(uint8_t, devStats, ATA_LOG_PAGE_LEN_BYTES);
            ret = send_ATA_Read_Log_Ext_Cmd(device, ATA_LOG_DEVICE_STATISTICS, reinitializeRequest, devStats,
                                            ATA_LOG_PAGE_LEN_BYTES, ATA_DEV_STATS_READ_AND_REINITIALIZE_FEAT);
        }
    }
    return ret;
}
