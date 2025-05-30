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
// \file device_statistics.h
// \brief This file defines the functions related to getting/displaying device statistics

#pragma once

#include "operations_Common.h"

#if defined(__cplusplus)
extern "C"
{
#endif
    typedef enum eThresholdTypeEnum
    {
        THRESHOLD_TYPE_NO_TRIGGER               = 0,
        THRESHOLD_TYPE_ALWAYS_TRIGGER_ON_UPDATE = 1,
        THRESHOLD_TYPE_TRIGGER_WHEN_EQUAL       = 2,
        THRESHOLD_TYPE_TRIGGER_WHEN_LESS        = 3,
        THRESHOLD_TYPE_TRIGGER_WHEN_GREATER     = 4,
        THRESHOLD_TYPE_TRIGGER_WHEN_NOT_EQUAL, // added for SAS
        THRESHOLD_TYPE_RESERVED
    } eThresholdType;
    // This is written towards the SATA device statistics log, however all SAS statistics read should be able to be put
    // into here without too much issue.
    typedef struct s_statistic
    {
        bool           isSupported;
        bool           isValueValid;
        bool           isNormalized;
        bool           supportsNotification;
        bool           monitoredConditionMet;
        bool           supportsReadThenInitialize;
        bool           isThresholdValid;
        bool           thresholdNotificationEnabled;
        bool           nonValidityTrigger;
        bool           validityTrigger;
        eThresholdType threshType;
        uint64_t       statisticValue; // may need additional parsing depending on the statistic this represents
        uint64_t       threshold;      // may not be populated depending on drive support/code support
    } statistic;

#define MAX_CDL_RW_POLICIES 7

    typedef struct s_cdlStatisticGroup
    {
        statistic readPolicy[MAX_CDL_RW_POLICIES];
        statistic writePolicy[MAX_CDL_RW_POLICIES];
    } cdlStatisticGroup;

    typedef struct s_cdlStatistic
    {
        cdlStatisticGroup groupA;
        cdlStatisticGroup groupB;
    } cdlStatistic;

#define MAX_CDL_STATISTIC_RANGES 4
#define MAX_VENDOR_STATISTICS    64

    typedef struct s_sataDeviceStatistics
    {
        bool     generalStatisticsSupported;
        bool     freeFallStatisticsSupported;
        bool     rotatingMediaStatisticsSupported;
        bool     generalErrorsStatisticsSupported;
        bool     temperatureStatisticsSupported;
        bool     transportStatisticsSupported;
        bool     ssdStatisticsSupported;
        bool     zonedDeviceStatisticsSupported;
        bool     cdlStatisticsSupported;
        bool     vendorSpecificStatisticsSupported;
        uint16_t statisticsPopulated; // just a count of how many were populated...not any specific order
        // general statistics
        statistic lifetimePoweronResets;
        statistic powerOnHours;
        statistic logicalSectorsWritten;
        statistic numberOfWriteCommands;
        statistic logicalSectorsRead;
        statistic numberOfReadCommands;
        statistic dateAndTimeTimestamp;
        statistic pendingErrorCount;
        statistic workloadUtilization;
        statistic utilizationUsageRate;
        statistic resourceAvailability;
        statistic randomWriteResourcesUsed;
        // free fall statistics
        statistic numberOfFreeFallEventsDetected;
        statistic overlimitShockEvents;
        // rotating media statistics
        statistic spindleMotorPoweronHours;
        statistic headFlyingHours;
        statistic headLoadEvents;
        statistic numberOfReallocatedLogicalSectors;
        statistic readRecoveryAttempts;
        statistic numberOfMechanicalStartFailures;
        statistic numberOfReallocationCandidateLogicalSectors;
        statistic numberOfHighPriorityUnloadEvents;
        // general errors statistics
        statistic numberOfReportedUncorrectableErrors;
        statistic numberOfResetsBetweenCommandAcceptanceAndCommandCompletion;
        statistic physicalElementStatusChanged;
        // temperature statistics
        statistic currentTemperature;
        statistic averageShortTermTemperature;
        statistic averageLongTermTemperature;
        statistic highestTemperature;
        statistic lowestTemperature;
        statistic highestAverageShortTermTemperature;
        statistic lowestAverageShortTermTemperature;
        statistic highestAverageLongTermTemperature;
        statistic lowestAverageLongTermTemperature;
        statistic timeInOverTemperature;
        statistic specifiedMaximumOperatingTemperature;
        statistic timeInUnderTemperature;
        statistic specifiedMinimumOperatingTemperature;
        // transport statistics
        statistic numberOfHardwareResets;
        statistic numberOfASREvents;
        statistic numberOfInterfaceCRCErrors;
        // solid state device statistics
        statistic percentageUsedIndicator;
        // Zoned device statistics (ZAC2)
        statistic maximumOpenZones;
        statistic maximumExplicitlyOpenZones;
        statistic maximumImplicitlyOpenZones;
        statistic minimumEmptyZones;
        statistic maximumNonSequentialZones;
        statistic zonesEmptied;
        statistic suboptimalWriteCommands;
        statistic commandsExceedingOptimalLimit;
        statistic failedExplicitOpens;
        statistic readRuleViolations;
        statistic writeRuleViolations;
        statistic maximumImplicitOpenSequentialOrBeforeRequiredZones;
        // CDL Statistics
        // NOTE: These are a little complicated. They can apply to concurrent ranges (actuators) or whole device
        //       So ranges beyond zero require concurrent positioning log and support for separate statistics per range
        // Statistic A and B track different things depending on how they are configured in CDL
        // Should this output capture the reasons for each statistic to increment??? -TJE
        statistic    lowestAchievableCommandDuration;    // In ACS-5, but obsolete in ACS-6
        uint8_t      cdlStatisticRanges;                 // how many ranges were populated when reading CDL statistics
        cdlStatistic cdlRange[MAX_CDL_STATISTIC_RANGES]; // see cdlStatisticRanges for how many were populated - TJE
        // vendor specific
        uint8_t   vendorSpecificStatisticsPopulated;
        statistic vendorSpecificStatistics[MAX_VENDOR_STATISTICS];
    } sataDeviceStatistics;

    typedef enum eProtocolSpecificStatisticsTypeEnum
    {
        STAT_PROT_NONE, // no statistics available or reported
        STAT_PROT_SAS,  // SAS protocol specific port page info. Up to 2 ports and 2 phys?
        // Other protocol specific pages. I did not see one for SPI, SSA, SRP, Fibre Channel, UAS, or SOP. If these
        // other protocols add data to output, we can add it here. - TJE
    } eProtocolSpecificStatisticsType;

// setting maximum number of ports to 2 for now. This could change in the future, but is not super likely - TJE
#define SAS_STATISTICS_MAX_PORTS 2
// setting maximum phys to 2. Current drives have 1 phy per port, so this is more than necessary - TJE
#define SAS_STATISTICS_MAX_PHYS 2

    typedef struct s_sasProtocolStatisticsPhy
    {
        bool      sasPhyStatsValid;
        uint16_t  phyID;
        statistic invalidDWORDCount;
        statistic runningDisparityErrorCount;
        statistic lossOfDWORDSynchronizationCount;
        statistic phyResetProblemCount;
    } sasProtocolStatisticsPhy;

    typedef struct s_sasProtocolPortStatistics
    {
        bool                     sasProtStatsValid;
        uint8_t                  phyCount;
        uint16_t                 portID;
        sasProtocolStatisticsPhy perPhy[SAS_STATISTICS_MAX_PHYS];
    } sasProtocolPortStatistics;

    typedef struct s_sasProtocolStatistics
    {
        uint16_t                  portCount;
        sasProtocolPortStatistics sasStatsPerPort[SAS_STATISTICS_MAX_PORTS];
    } sasProtocolStatistics;

    typedef struct s_sasDeviceStatitics
    {
        bool     writeErrorCountersSupported;
        bool     readErrorCountersSupported;
        bool     readReverseErrorCountersSupported;
        bool     verifyErrorCountersSupported;
        bool     nonMediumErrorSupported;
        bool     formatStatusSupported;
        bool     logicalBlockProvisioningSupported;
        bool     temperatureSupported;
        bool     environmentReportingSupported;
        bool     environmentLimitsSupported;
        bool     startStopCycleCounterSupported;
        bool     utilizationSupported;
        bool     solidStateMediaSupported;
        bool     backgroundScanResultsSupported;
        bool     pendingDefectsSupported;
        bool     lpsMisalignmentSupported;
        bool     nvCacheSupported;
        bool     generalStatisticsAndPerformanceSupported;
        bool     cacheMemoryStatisticsSupported;
        bool     timeStampSupported;
        bool     zonedDeviceStatisticsSupported;
        bool     defectStatisticsSupported;
        bool     protocolSpecificStatisticsSupported;
        bool     powerConditionTransitionsSupported;
        uint16_t statisticsPopulated; // just a count of how many were populated...not any specific order
        // Write Error Counters
        statistic writeErrorsCorrectedWithoutSubstantialDelay;
        statistic writeErrorsCorrectedWithPossibleDelays;
        statistic writeTotalReWrites;
        statistic writeErrorsCorrected;
        statistic writeTotalTimeCorrectionAlgorithmProcessed;
        statistic writeTotalBytesProcessed;
        statistic writeTotalUncorrectedErrors;
        // Read Error Counters
        statistic readErrorsCorrectedWithoutSubstantialDelay;
        statistic readErrorsCorrectedWithPossibleDelays;
        statistic readTotalRereads;
        statistic readErrorsCorrected;
        statistic readTotalTimeCorrectionAlgorithmProcessed;
        statistic readTotalBytesProcessed;
        statistic readTotalUncorrectedErrors;
        // Read Reverse Error Counters - These might be for tape drives, not HDDs...may remove this-TJE
        statistic readReverseErrorsCorrectedWithoutSubstantialDelay;
        statistic readReverseErrorsCorrectedWithPossibleDelays;
        statistic readReverseTotalReReads;
        statistic readReverseErrorsCorrected;
        statistic readReverseTotalTimeCorrectionAlgorithmProcessed;
        statistic readReverseTotalBytesProcessed;
        statistic readReverseTotalUncorrectedErrors;
        // Verify Error Counters
        statistic verifyErrorsCorrectedWithoutSubstantialDelay;
        statistic verifyErrorsCorrectedWithPossibleDelays;
        statistic verifyTotalReVerifies;
        statistic verifyErrorsCorrected;
        statistic verifyTotalTimeCorrectionAlgorithmProcessed;
        statistic verifyTotalBytesProcessed;
        statistic verifyTotalUncorrectedErrors;
        // non-medium Error
        statistic nonMediumErrorCount;
        // Format Status
        statistic grownDefectsDuringCertification;
        statistic totalBlocksReassignedDuringFormat;
        statistic totalNewBlocksReassigned;
        statistic powerOnMinutesSinceFormat;
        // logical block provisioning
        statistic availableLBAMappingresourceCount;
        statistic usedLBAMappingResourceCount;
        statistic availableProvisioningResourcePercentage;
        statistic deduplicatedLBAResourceCount;
        statistic compressedLBAResourceCount;
        statistic totalEfficiencyLBAResourceCount;
        // Temperature
        statistic temperature;
        statistic referenceTemperature;
        // Environment (Temperature and humidity) (reporting)
        statistic currentTemperature;
        statistic lifetimeMaximumTemperature;
        statistic lifetimeMinimumTemperature;
        statistic maximumTemperatureSincePowerOn;
        statistic minimumTemperatureSincePowerOn;
        statistic maximumOtherTemperature;
        statistic minimumOtherTemperature;
        statistic currentRelativeHumidity;
        statistic lifetimeMaximumRelativeHumidity;
        statistic lifetimeMinumumRelativeHumidity;
        statistic maximumRelativeHumiditySincePoweron;
        statistic minimumRelativeHumiditySincePoweron;
        statistic maximumOtherRelativeHumidity;
        statistic minimumOtherRelativeHumidity;
        // Environment (Temperature and humidity) (limits)
        statistic highCriticalTemperatureLimitTrigger;
        statistic highCriticalTemperatureLimitReset;
        statistic lowCriticalTemperatureLimitReset;
        statistic lowCriticalTemperatureLimitTrigger;
        statistic highOperatingTemperatureLimitTrigger;
        statistic highOperatingTemperatureLimitReset;
        statistic lowOperatingTemperatureLimitReset;
        statistic lowOperatingTemperatureLimitTrigger;
        statistic highCriticalHumidityLimitTrigger;
        statistic highCriticalHumidityLimitReset;
        statistic lowCriticalHumidityLimitReset;
        statistic lowCriticalHumidityLimitTrigger;
        statistic highOperatingHumidityLimitTrigger;
        statistic highOperatingHumidityLimitReset;
        statistic lowOperatingHumidityLimitReset;
        statistic lowOperatingHumidityLimitTrigger;
        // start-stop cycle counter
        statistic dateOfManufacture;
        statistic accountingDate;
        statistic specifiedCycleCountOverDeviceLifetime;
        statistic accumulatedStartStopCycles;
        statistic specifiedLoadUnloadCountOverDeviceLifetime;
        statistic accumulatedLoadUnloadCycles;
        // utilization
        statistic workloadUtilization;
        statistic utilizationUsageRateBasedOnDateAndTime;
        // SSD
        statistic percentUsedEndurance;
        // Background scan results
        statistic accumulatedPowerOnMinutes;
        statistic numberOfBackgroundScansPerformed;
        // statistic backgroundScanProgress;//not sure if we need this - TJE
        statistic numberOfBackgroundMediaScansPerformed;
        // pending defects
        statistic pendingDefectCount;
        // LPS misalignment
        statistic lpsMisalignmentCount;
        // NVCache
        statistic remainingNonvolatileTime;
        statistic maximumNonvolatileTime;
        // General Statistics and performance
        statistic numberOfReadCommands;
        statistic numberOfWriteCommands;
        statistic numberOfLogicalBlocksReceived;
        statistic numberOfLogicalBlocksTransmitted;
        statistic readCommandProcessingIntervals;
        statistic writeCommandProcessingIntervals;
        statistic weightedNumberOfReadCommandsPlusWriteCommands;
        statistic weightedReadCommandProcessingPlusWriteCommandProcessing;
        statistic idleTimeIntervals;
        statistic timeIntervalDescriptor;
        statistic numberOfReadFUACommands;
        statistic numberOfWriteFUACommands;
        statistic numberOfReadFUANVCommands;
        statistic numberOfWriteFUANVCommands;
        statistic readFUACommandProcessingIntervals;
        statistic writeFUACommandProcessingIntervals;
        statistic readFUANVCommandProcessingIntervals;
        statistic writeFUANVCommandProcessingIntervals;
        // Cache Memory Statistics
        statistic readCacheMemoryHits;
        statistic readsToCacheMemory;
        statistic writeCacheMemoryHits;
        statistic writesFromCacheMemory;
        statistic timeFromLastHardReset;
        statistic cacheTimeInterval;
        // timestamp
        statistic dateAndTimeTimestamp;
        // ZBC Statistics (ZBC2)
        statistic maximumOpenZones;
        statistic maximumExplicitlyOpenZones;
        statistic maximumImplicitlyOpenZones;
        statistic minimumEmptyZones;
        statistic maximumNonSequentialZones;
        statistic zonesEmptied;
        statistic suboptimalWriteCommands;
        statistic commandsExceedingOptimalLimit;
        statistic failedExplicitOpens;
        statistic readRuleViolations;
        statistic writeRuleViolations;
        statistic maxImplicitlyOpenSeqOrBeforeReqZones;
        // Defect list counts (Grown and Primary)
        statistic grownDefects;
        statistic primaryDefects;
        // Protocol specific statistics
        eProtocolSpecificStatisticsType protocolStatisticsType;
        // TODO: How do we want to handle multiple port SAS? Currently limiting this output to 2 ports since that is the
        // most supported today-TJE
        union
        {
            sasProtocolStatistics sasProtStats;
            // Other data structures for other protocols that implement the protocol specific port log page
        };
        // Power condition transitions
        statistic transitionsToActive;
        statistic transitionsToIdleA;
        statistic transitionsToIdleB;
        statistic transitionsToIdleC;
        statistic transitionsToStandbyZ;
        statistic transitionsToStandbyY;
        //      Command duration limits statistics page
        //      Informational exceptions??? Not sure how we should track this data yet - TJE
        //
    } sasDeviceStatitics;

    // access the proper stats in the union based on device->drive_info.drive_type
    typedef struct s_deviceStatistics
    {
        union
        {
            sataDeviceStatistics sataStatistics;
            sasDeviceStatitics   sasStatistics;
        };
    } deviceStatistics, *ptrDeviceStatistics;

    M_NONNULL_PARAM_LIST(1, 2)
    M_PARAM_RO(1)
    M_PARAM_WO(2)
    OPENSEA_OPERATIONS_API eReturnValues get_DeviceStatistics(tDevice* device, ptrDeviceStatistics deviceStats);

    M_NONNULL_PARAM_LIST(1, 2)
    M_PARAM_RO(1)
    M_PARAM_RO(2)
    OPENSEA_OPERATIONS_API eReturnValues print_DeviceStatistics(tDevice* device, ptrDeviceStatistics deviceStats);

    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1)
    bool is_Timestamp_Supported(tDevice* device);

    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1)
    eReturnValues set_Date_And_Time_Timestamp(tDevice* device);

    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1)
    eReturnValues ata_Device_Statistics_Reinitialize(tDevice* device, eDeviceStatisticsLog reinitializeRequest);

#if defined(__cplusplus)
}
#endif
