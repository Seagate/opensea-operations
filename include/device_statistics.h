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
        uint64_t       statisticValue; // may need additional parsing depending on the statistic this represents
        bool           isThresholdValid;
        bool           thresholdNotificationEnabled;
        eThresholdType threshType;
        bool           nonValidityTrigger;
        bool           validityTrigger;
        uint64_t       threshold; // may not be populated depending on drive support/code support
    } statistic;

    typedef struct s_sataDeviceStatistics
    {
        uint16_t statisticsPopulated; // just a count of how many were populated...not any specific order
        // general statistics
        bool      generalStatisticsSupported;
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
        bool      freeFallStatisticsSupported;
        statistic numberOfFreeFallEventsDetected;
        statistic overlimitShockEvents;
        // rotating media statistics
        bool      rotatingMediaStatisticsSupported;
        statistic spindleMotorPoweronHours;
        statistic headFlyingHours;
        statistic headLoadEvents;
        statistic numberOfReallocatedLogicalSectors;
        statistic readRecoveryAttempts;
        statistic numberOfMechanicalStartFailures;
        statistic numberOfReallocationCandidateLogicalSectors;
        statistic numberOfHighPriorityUnloadEvents;
        // general errors statistics
        bool      generalErrorsStatisticsSupported;
        statistic numberOfReportedUncorrectableErrors;
        statistic numberOfResetsBetweenCommandAcceptanceAndCommandCompletion;
        statistic physicalElementStatusChanged;
        // temperature statistics
        bool      temperatureStatisticsSupported;
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
        bool      transportStatisticsSupported;
        statistic numberOfHardwareResets;
        statistic numberOfASREvents;
        statistic numberOfInterfaceCRCErrors;
        // solid state device statistics
        bool      ssdStatisticsSupported;
        statistic percentageUsedIndicator;
        // Zoned device statistics (ZAC2)
        bool      zonedDeviceStatisticsSupported;
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
        // vendor specific
        bool      vendorSpecificStatisticsSupported;
        uint8_t   vendorSpecificStatisticsPopulated;
        statistic vendorSpecificStatistics[64];
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
        uint16_t                 portID;
        uint8_t                  phyCount;
        sasProtocolStatisticsPhy perPhy[SAS_STATISTICS_MAX_PHYS];
    } sasProtocolPortStatistics;

    typedef struct s_sasProtocolStatistics
    {
        uint16_t                  portCount;
        sasProtocolPortStatistics sasStatsPerPort[SAS_STATISTICS_MAX_PORTS];
    } sasProtocolStatistics;

    typedef struct s_sasDeviceStatitics
    {
        uint16_t statisticsPopulated; // just a count of how many were populated...not any specific order
        // Write Error Counters
        bool      writeErrorCountersSupported;
        statistic writeErrorsCorrectedWithoutSubstantialDelay;
        statistic writeErrorsCorrectedWithPossibleDelays;
        statistic writeTotalReWrites;
        statistic writeErrorsCorrected;
        statistic writeTotalTimeCorrectionAlgorithmProcessed;
        statistic writeTotalBytesProcessed;
        statistic writeTotalUncorrectedErrors;
        // Read Error Counters
        bool      readErrorCountersSupported;
        statistic readErrorsCorrectedWithoutSubstantialDelay;
        statistic readErrorsCorrectedWithPossibleDelays;
        statistic readTotalRereads;
        statistic readErrorsCorrected;
        statistic readTotalTimeCorrectionAlgorithmProcessed;
        statistic readTotalBytesProcessed;
        statistic readTotalUncorrectedErrors;
        // Read Reverse Error Counters - These might be for tape drives, not HDDs...may remove this-TJE
        bool      readReverseErrorCountersSupported;
        statistic readReverseErrorsCorrectedWithoutSubstantialDelay;
        statistic readReverseErrorsCorrectedWithPossibleDelays;
        statistic readReverseTotalReReads;
        statistic readReverseErrorsCorrected;
        statistic readReverseTotalTimeCorrectionAlgorithmProcessed;
        statistic readReverseTotalBytesProcessed;
        statistic readReverseTotalUncorrectedErrors;
        // Verify Error Counters
        bool      verifyErrorCountersSupported;
        statistic verifyErrorsCorrectedWithoutSubstantialDelay;
        statistic verifyErrorsCorrectedWithPossibleDelays;
        statistic verifyTotalReVerifies;
        statistic verifyErrorsCorrected;
        statistic verifyTotalTimeCorrectionAlgorithmProcessed;
        statistic verifyTotalBytesProcessed;
        statistic verifyTotalUncorrectedErrors;
        // non-medium Error
        bool      nonMediumErrorSupported;
        statistic nonMediumErrorCount;
        // Format Status
        bool      formatStatusSupported;
        statistic grownDefectsDuringCertification;
        statistic totalBlocksReassignedDuringFormat;
        statistic totalNewBlocksReassigned;
        statistic powerOnMinutesSinceFormat;
        // logical block provisioning
        bool      logicalBlockProvisioningSupported;
        statistic availableLBAMappingresourceCount;
        statistic usedLBAMappingResourceCount;
        statistic availableProvisioningResourcePercentage;
        statistic deduplicatedLBAResourceCount;
        statistic compressedLBAResourceCount;
        statistic totalEfficiencyLBAResourceCount;
        // Temperature
        bool      temperatureSupported;
        statistic temperature;
        statistic referenceTemperature;
        // Environment (Temperature and humidity) (reporting)
        bool      environmentReportingSupported;
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
        bool      environmentLimitsSupported;
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
        bool      startStopCycleCounterSupported;
        statistic dateOfManufacture;
        statistic accountingDate;
        statistic specifiedCycleCountOverDeviceLifetime;
        statistic accumulatedStartStopCycles;
        statistic specifiedLoadUnloadCountOverDeviceLifetime;
        statistic accumulatedLoadUnloadCycles;
        // utilization
        bool      utilizationSupported;
        statistic workloadUtilization;
        statistic utilizationUsageRateBasedOnDateAndTime;
        // SSD
        bool      solidStateMediaSupported;
        statistic percentUsedEndurance;
        // Background scan results
        bool      backgroundScanResultsSupported;
        statistic accumulatedPowerOnMinutes;
        statistic numberOfBackgroundScansPerformed;
        // statistic backgroundScanProgress;//not sure if we need this - TJE
        statistic numberOfBackgroundMediaScansPerformed;
        // pending defects
        bool      pendingDefectsSupported;
        statistic pendingDefectCount;
        // LPS misalignment
        bool      lpsMisalignmentSupported;
        statistic lpsMisalignmentCount;
        // NVCache
        bool      nvCacheSupported;
        statistic remainingNonvolatileTime;
        statistic maximumNonvolatileTime;
        // General Statistics and performance
        bool      generalStatisticsAndPerformanceSupported;
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
        bool      cacheMemoryStatisticsSupported;
        statistic readCacheMemoryHits;
        statistic readsToCacheMemory;
        statistic writeCacheMemoryHits;
        statistic writesFromCacheMemory;
        statistic timeFromLastHardReset;
        statistic cacheTimeInterval;
        // timestamp
        bool      timeStampSupported;
        statistic dateAndTimeTimestamp;
        // ZBC Statistics (ZBC2)
        bool      zonedDeviceStatisticsSupported;
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
        bool      defectStatisticsSupported;
        statistic grownDefects;
        statistic primaryDefects;
        // Protocol specific statistics
        bool                            protocolSpecificStatisticsSupported;
        eProtocolSpecificStatisticsType protocolStatisticsType;
        // TODO: How do we want to handle multiple port SAS? Currently limiting this output to 2 ports since that is the
        // most supported today-TJE
        union
        {
            sasProtocolStatistics sasProtStats;
            // Other data structures for other protocols that implement the protocol specific port log page
        };
        // Power condition transitions
        bool      powerConditionTransitionsSupported;
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

    OPENSEA_OPERATIONS_API eReturnValues get_DeviceStatistics(tDevice* device, ptrDeviceStatistics deviceStats);

    OPENSEA_OPERATIONS_API eReturnValues print_DeviceStatistics(tDevice* device, ptrDeviceStatistics deviceStats);

#if defined(__cplusplus)
}
#endif
