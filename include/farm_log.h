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
// \file farm_log.c
// \brief This file defines the functions related to FARM Log

#pragma once
#include "operations_Common.h"

#include <stddef.h> //offset of macro

#if defined(__cplusplus)
extern "C"
{
#endif
    typedef enum eSataFarmCopyTypeEnum
    {
        SATA_FARM_COPY_TYPE_UNKNOWN,
        SATA_FARM_COPY_TYPE_DISC,
        SATA_FARM_COPY_TYPE_FLASH,
    } eSataFarmCopyType;

    //-----------------------------------------------------------------------------
    //
    //  pull_FARM_Combined_Log(tDevice *device, const char * const filePath);
    //
    //! \brief   Description: This function pulls the Seagate Combined FARM log. This Log is a combination of all
    //!						  FARM Log Subpages.
    //
    //  Entry:
    //!   \param[in] device = poiner to a valid device structure with a device handle
    //!   \param[in] filePath = pointer to the path where this log should be generated. Use M_NULLPTR for current
    //!   working dir. \param[in] transferSizeBytes = OPTIONAL. If set to zero, this is ignored.
    //  Exit:
    //!   \return SUCCESS = everything worked, !SUCCESS means something went wrong
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1)
    M_NULL_TERM_STRING(2)
    M_PARAM_RO(2)
    OPENSEA_OPERATIONS_API eReturnValues pull_FARM_Combined_Log(tDevice*    device,
                                                                const char* filePath,
                                                                uint32_t    transferSizeBytes,
                                                                int         sataFarmCopyType);

    #define FARM_LOG_SIGNATURE_VAL_QWORD UINT64_C(0x00004641524D4552)
#define FARM_LOG_SIGNATURE_STR "FARMER\0\0"
#define FARM_FACTORY_STR "FACTORY\0"
#define FARM_LOG_SIGNATURE_LEN 8 // bytes
#define FARM_LOG_HEADER_RESERVED_QWORDS 2039

#define FARM_PAGE_LEN 16384

typedef enum eFarmPageEnum
{
    FARM_PAGE_HEADER = 0,
    FARM_PAGE_DRIVE_INFO = 1,
    FARM_PAGE_WORKLOAD = 2,
    FARM_PAGE_ERROR_STATS = 3,
    FARM_PAGE_ENVIRONMENT_STATS = 4,
    FARM_PAGE_RELIABILITY_STATS = 5,
}eFarmPage;

// Convert drive info page to this enum
typedef enum eFARMDriveInterfaceEnum
{
    FARM_DRIVE_INTERFACE_SATA,
    FARM_DRIVE_INTERFACE_SAS,
    // NVMe can be added here when it gets defined - TJE
}eFARMDriveInterface;

// Used when printing for single vs dual actuator fields
typedef enum eFARMActuatorEnum
{
    FARM_ACTUATOR_FULL_DRIVE,
    FARM_ACTUATOR_0,
    FARM_ACTUATOR_1,
}eFARMActuator;

#define FARM_LOG_MAX_FIELDS_PER_PAGE 2046

#define FARM_FIELD_SUPPORTED_BIT BIT7
#define FARM_FIELD_VALID_BIT     BIT6
static M_INLINE uint8_t get_Farm_Status_Byte(uint64_t field)
{
    return M_Byte7(field);
}

static M_INLINE uint64_t get_Farm_Qword_Data(uint64_t field)
{
    return field & UINT64_C(0x00FFFFFFFFFFFFFF);
}

typedef struct s_farmGenericPage
{
    uint64_t pageNumber;
    uint64_t copyNumber; //ASCII "FACTORY" for factory, otherwise a number
    uint64_t fields[FARM_LOG_MAX_FIELDS_PER_PAGE];
}farmGenericPage;

M_STATIC_ASSERT(sizeof(farmGenericPage) == FARM_PAGE_LEN, farm_generic_page_stuct_is_not_16kib);

typedef struct s_farmHeader
{
    union {
        uint64_t signature;
        char signaturestr[FARM_LOG_SIGNATURE_LEN];
    };
    uint64_t majorVersion;
    uint64_t minorVersion;
    uint64_t numberOfPagesSupported;
    uint64_t logSizeInBytes;
    uint64_t pageSizeInBytes;
    uint64_t maxDriveHeadsSupported;
    uint64_t reserved;
    uint64_t reasonForFrameCapture;
    uint64_t reservedQwords[FARM_LOG_HEADER_RESERVED_QWORDS];
}farmHeader;

M_STATIC_ASSERT(sizeof(farmHeader) == FARM_PAGE_LEN, farm_header_stuct_is_not_16kib);

#define FARM_MAX_HEADS 24
#define FARM_GET_PHYS_ELEM_STAT_BY_HEAD_MAX FARM_MAX_HEADS

#define FARM_DRIVE_RECORDING_CMR BIT1
#define FARM_DRIVE_RECORDING_SMR BIT0

#define FARM_DRIVE_INFO_SN_ASCII_LEN 2
#define FARM_DRIVE_INFO_WWN_ASCII_LEN 2
#define FARM_DRIVE_INFO_FWREV_ASCII_LEN 2
#define FARM_DRIVE_INFO_MN_ASCII_LEN 10

#define FARM_DRIVE_INFO_RESERVED_FIELDS 1968

typedef struct s_farmDriveInfo
{
    uint64_t pageNumber;
    uint64_t copyNumber;
    uint64_t sn[FARM_DRIVE_INFO_SN_ASCII_LEN];
    uint64_t wwn[FARM_DRIVE_INFO_WWN_ASCII_LEN];
    uint64_t driveInterface;// ASCII
    uint64_t driveCapacity;//LBAs
    uint64_t physicalSectorSize;
    uint64_t logicalSectorSize;
    uint64_t deviceBufferSize;
    uint64_t numberOfHeads;
    uint64_t deviceFormFactor;
    uint64_t rotationRate;
    uint64_t fwrev[FARM_DRIVE_INFO_FWREV_ASCII_LEN];
    uint64_t ataSecurityState;   //reserved on SAS
    uint64_t ataFeaturesSupported;// ATA ID word 78. reserved on SAS
    uint64_t ataFeaturesEnabled;  // ATA ID word 79. reserved on SAS
    uint64_t powerOnHours;
    uint64_t spindlePowerOnHours; //reserved on sas
    uint64_t headFlightHours; //reserved on sas
    uint64_t headLoadEvents; //parameter 50h on SAS
    uint64_t powerCycleCount;
    uint64_t hardwareResetCount;
    uint64_t spinUpTimeMilliseconds; //reserved on sas
    union { //might need to move this union in the future - TJE
        uint64_t satareserved[2]; 
        struct
        {
            uint64_t nvcStatusOnPoweron;
            uint64_t timeAvailableToSaveUDToNVMem;//Time Available to Save User Data to Non-volatile Memory Over Last Power Cycle (in 100us)
        };
    };
    uint64_t highestPOHForTimeRestrictedParameters;// milliseconds //sas last SMART summary frame POH
    uint64_t lowestPOHForTimeRestrictedParameters;// milliseconds //sas first SMART summary frame POH
    uint64_t timeToReadyOfLastPowerCycle; //milliseconds // ON SAS
    uint64_t timeDriveHeldInStaggeredSpinDuringLastPowerOnSequence; // milliseconds // ON SAS
    uint64_t modelNumber[FARM_DRIVE_INFO_MN_ASCII_LEN];// lower 32bit = partial model number
    uint64_t driveRecordingType;// ON SAS
    uint64_t isDriveDepopulated;// 1 = depopulated, 0 = not depopulated
    uint64_t maxAvailableSectorsForReassignment; // disc sectors //ON SAS
    uint64_t dateOfAssembly;// ASCII YYWW
    uint64_t depopulationHeadMask;// ON SAS
    uint64_t headFlightHoursActuator1;
    uint64_t headLoadEventsActuator1; //parameter 60h on SAS
    uint64_t hamrDataProtectStatus;// 1 = data protect, 0 = no data protect // ON SAS
    uint64_t regenHeadMask;//bit mask. Bad head = 1, good head = 0 //ON SAS
    uint64_t pohOfMostRecentTimeseriesFrame;// ON SAS
    uint64_t pohOfSecondMostRecentTimeseriesFrame;// ON SAS
    uint64_t sequentialOrBeforeWriteRequiredForActiveZoneConfiguration;
    uint64_t sequentialWriteRequiredForActiveZoneConfiguration;
    uint64_t numberOfLBAs;// HSMR SWR Capacity
    uint64_t getPhysicalElementStatusByHead[FARM_GET_PHYS_ELEM_STAT_BY_HEAD_MAX];
    uint64_t reservedFields[FARM_DRIVE_INFO_RESERVED_FIELDS];
}farmDriveInfo;

M_STATIC_ASSERT(sizeof(farmDriveInfo) == FARM_PAGE_LEN, farm_driveInfo_stuct_is_not_16kib);

#define FARM_WORKLOAD_RESERVED_STATS 350
#define FARM_WORKLOAD_RESERVED_STATS2 1650

typedef struct s_farmWorkload
{
    uint64_t pageNumber;
    uint64_t copyNumber;
    uint64_t ratedWorkloadPercentage;//now obsolete
    uint64_t totalReadCommands;
    uint64_t totalWriteCommands;
    uint64_t totalRandomReadCommands;
    uint64_t totalRandomWriteCommands;
    uint64_t totalOtherCommands;
    uint64_t logicalSectorsWritten;
    uint64_t logicalSectorsRead;
    uint64_t numberOfDitherEventsInCurrentPowerCycle;//not on SAS
    uint64_t numberDitherHeldOffDueToRandomWorkloadsInCurrentPowerCycle;//not on SAS
    uint64_t numberDitherHeldOffDueToSequentialWorkloadsInCurrentPowerCycle;//not on SAS
    uint64_t numReadsInLBA0To3125PercentRange;// 0% - 3.125%
    uint64_t numReadsInLBA3125To25PercentRange;// 3.125% - 25%
    uint64_t numReadsInLBA25To50PercentRange;//25%-50%
    uint64_t numReadsInLBA50To100PercentRange;//50%-100%
    uint64_t numWritesInLBA0To3125PercentRange;// 0% - 3.125%
    uint64_t numWritesInLBA3125To25PercentRange;// 3.125% - 25%
    uint64_t numWritesInLBA25To50PercentRange;//25%-50%
    uint64_t numWritesInLBA50To100PercentRange;//50%-100%
    uint64_t numReadsOfXferLenLT16KB;// transfer length <= 16KB
    uint64_t numReadsOfXferLen16KBTo512KB;// 16KB - 512KB
    uint64_t numReadsOfXferLen512KBTo2MB;//512KB - 2MB
    uint64_t numReadsOfXferLenGT2MB;//>2MB
    uint64_t numWritesOfXferLenLT16KB;// transfer length <= 16KB
    uint64_t numWritesOfXferLen16KBTo512KB;// 16KB - 512KB
    uint64_t numWritesOfXferLen512KBTo2MB;//512KB - 2MB
    uint64_t numWritesOfXferLenGT2MB;//>2MB
    uint64_t countQD1at30sInterval;
    uint64_t countQD2at30sInterval;
    uint64_t countQD3To4at30sInterval;
    uint64_t countQD5To8at30sInterval;
    uint64_t countQD9To16at30sInterval;
    uint64_t countQD17To32at30sInterval;
    uint64_t countQD33To64at30sInterval;
    uint64_t countGTQD64at30sInterval; // Queue depth > 64
    // Data below here not available on SAS
    uint64_t numberOfDitherEventsInCurrentPowerCycleActuator1;
    uint64_t numberDitherHeldOffDueToRandomWorkloadsInCurrentPowerCycleActuator1;
    uint64_t numberDitherHeldOffDueToSequentialWorkloadsInCurrentPowerCycleActuator1;
    uint64_t workloadReserved[FARM_WORKLOAD_RESERVED_STATS];
    uint64_t numReadsXferLenBin4Last3SMARTSummaryFrames;
    uint64_t numReadsXferLenBin5Last3SMARTSummaryFrames;
    uint64_t numReadsXferLenBin6Last3SMARTSummaryFrames;
    uint64_t numReadsXferLenBin7Last3SMARTSummaryFrames;
    uint64_t numWritesXferLenBin4Last3SMARTSummaryFrames;
    uint64_t numWritesXferLenBin5Last3SMARTSummaryFrames;
    uint64_t numWritesXferLenBin6Last3SMARTSummaryFrames;
    uint64_t numWritesXferLenBin7Last3SMARTSummaryFrames;
    uint64_t reserved[FARM_WORKLOAD_RESERVED_STATS2];
}farmWorkload;

// Multiple static asserts to make sure things are where we expect them to be - TJE
M_STATIC_ASSERT(offsetof(farmWorkload, countQD1at30sInterval) == 232, farm_QD1_Wrong_Offset);

M_STATIC_ASSERT(offsetof(farmWorkload, numberOfDitherEventsInCurrentPowerCycleActuator1) == 296, farm_Dither_Actuator1_Wrong_Offset);

M_STATIC_ASSERT(offsetof(farmWorkload, workloadReserved) == 320, farm_Workload_Reserved_Wrong_Offset);

M_STATIC_ASSERT(offsetof(farmWorkload, numReadsXferLenBin4Last3SMARTSummaryFrames) == 3120, farm_Xfer_Bin4_Wrong_Offset);

M_STATIC_ASSERT(sizeof(farmWorkload) == FARM_PAGE_LEN, farm_workload_stuct_is_not_16kib);

#define FARM_FLED_EVENTS 8
#define FARM_RW_RETRY_EVENTS 8
#define FARM_RESERVED2_CNT 17
#define FARM_RESERVED3_CNT 23
#define FARM_SATA_PFA_CNT 2
#define FARM_SATA_PFA1_ATTR_01H_TRIP_BIT BIT0
#define FARM_SATA_PFA1_ATTR_03H_TRIP_BIT BIT1
#define FARM_SATA_PFA1_ATTR_05H_TRIP_BIT BIT2
#define FARM_SATA_PFA1_ATTR_07H_TRIP_BIT BIT3
#define FARM_SATA_PFA1_ATTR_0AH_TRIP_BIT BIT4
#define FARM_SATA_PFA1_ATTR_12H_TRIP_BIT BIT5
#define FARM_SATA_PFA2_ATTR_C8H_TRIP_BIT BIT0
#define FARM_SAS_FRU_TRIP_CNT 2
#define FARM_SAS_SMART_TRIP1_FRU_32 BIT6
#define FARM_SAS_SMART_TRIP1_FRU_30 BIT5
#define FARM_SAS_SMART_TRIP1_FRU_16 BIT4
#define FARM_SAS_SMART_TRIP1_FRU_14 BIT3
#define FARM_SAS_SMART_TRIP1_FRU_12 BIT2
#define FARM_SAS_SMART_TRIP1_FRU_10 BIT1
#define FARM_SAS_SMART_TRIP1_FRU_05 BIT0
#define FARM_SAS_SMART_TRIP2_FRU_93 BIT4
#define FARM_SAS_SMART_TRIP2_FRU_92 BIT3
#define FARM_SAS_SMART_TRIP2_FRU_5B BIT2
#define FARM_SAS_SMART_TRIP2_FRU_43 BIT1
#define FARM_SAS_SMART_TRIP2_FRU_42 BIT0
#define FARM_RESERVED_ERROR_STATISTICS 1820
typedef struct s_farmErrorStatistics
{
    uint64_t pageNumber;
    uint64_t copyNumber;
    uint64_t numberOfUnrecoverableReadErrors;
    uint64_t numberOfUnrecoverableWriteErrors;
    uint64_t numberOfReallocatedSectors;//actuator 0. // on SAS in by actuator param 51h or 61h
    uint64_t numberOfReadRecoveryAttempts;
    uint64_t numberOfMechanicalStartRetries;
    uint64_t numberOfReallocationCandidateSectors;//actuator 0 // on SAS in by actuator param 51h or 61h
    union { //might need to move this union in the future - TJE
        struct sataErrStats {
            uint64_t numberOfASREvents;
            uint64_t numberOfInterfaceCRCErrors;
            uint64_t spinRetryCount;
            uint64_t spinRetryCountNormalized;
            uint64_t spinRetryCountWorstEver;
            uint64_t numberOfIOEDCErrors;
            uint64_t commandTimeoutTotal;
            uint64_t commandTimeoutOver5s;
            uint64_t commandTimeoutOver7pt5s;// >7.5s
        }sataErr;
        struct sasErrorStats
        {
            uint64_t fruCodeOfSMARTTripMostRecentFrame;//if any
            uint64_t portAinvDWordCount;
            uint64_t portBinvDWordCount;
            uint64_t portADisparityErrCount;
            uint64_t portBDisparityErrCount;
            uint64_t portAlossOfDWordSync;
            uint64_t portBlossOfDWordSync;
            uint64_t portAphyResetProblem;
            uint64_t portBphyResetProblem;
        }sasErr;
    };
    uint64_t totalFlashLEDEvents;//actuator 0 // on SAS in by actuator param 51h or 61h
    uint64_t lastFLEDIndex;//FLED array wraps so this points to most recent entry // on SAS in by actuator param 51h or 61h
    uint64_t uncorrectableErrors;//SMART attribute 187
    uint64_t reserved1;
    uint64_t last8FLEDEvents[FARM_FLED_EVENTS];//actuator 0 // on SAS in by actuator param 51h or 61h
    uint64_t last8ReadWriteRetryEvents[FARM_RW_RETRY_EVENTS];//actuator 0
    uint64_t reserved2[FARM_RESERVED2_CNT];
    uint64_t timestampOfLast8FLEDs[FARM_FLED_EVENTS];// actuator 0 // on SAS in by actuator param 51h or 61h
    uint64_t powerCycleOfLast8FLEDs[FARM_FLED_EVENTS];//actuator 0 // on SAS in by actuator param 51h or 61h
    uint64_t cumulativeLifetimeUnrecoverableReadErrorsDueToERC;//sct error recovery control
    uint64_t cumLTUnrecReadRepeatByHead[FARM_MAX_HEADS];
    uint64_t cumLTUnrecReadUniqueByHead[FARM_MAX_HEADS];   
    uint64_t numberOfReallocatedSectorsActuator1; // on SAS in by actuator param 51h or 61h
    uint64_t numberOfReallocationCandidateSectorsActuator1; // on SAS in by actuator param 51h or 61h
    uint64_t totalFlashLEDEventsActuator1;// on SAS in by actuator param 51h or 61h
    uint64_t lastFLEDIndexActuator1;//FLED array wraps so this points to most recent entry // on SAS in by actuator param 51h or 61h
    uint64_t last8FLEDEventsActuator1[FARM_FLED_EVENTS]; // on SAS in by actuator param 51h or 61h
    uint64_t reserved3[FARM_RESERVED3_CNT];
    uint64_t timestampOfLast8FLEDsActuator1[FARM_FLED_EVENTS]; // on SAS in by actuator param 51h or 61h
    uint64_t powerCycleOfLast8FLEDsActuator1[FARM_FLED_EVENTS]; // on SAS in by actuator param 51h or 61h
    union {
        uint64_t sataPFAAttributes[FARM_SATA_PFA_CNT];//pre-fail/advisory attributes. Bitfield.
        uint64_t sasFRUTrips[FARM_SAS_FRU_TRIP_CNT];//FRU codes reported for different SMART trips on SAS
    };
    uint64_t numberReallocatedSectorsSinceLastFARMTimeSeriesFrameSaved; // on SAS in by actuator param 51h or 61h
    uint64_t numberReallocatedSectorsBetweenFarmTimeSeriesFrameNandNminus1; // on SAS in by actuator param 51h or 61h
    uint64_t numberReallocationCandidateSectorsSinceLastFARMTimeSeriesFrameSaved; // on SAS in by actuator param 51h or 61h
    uint64_t numberReallocationCandidateSectorsBetweenFarmTimeSeriesFrameNandNminus1; // on SAS in by actuator param 51h or 61h
    uint64_t numberReallocatedSectorsSinceLastFARMTimeSeriesFrameSavedActuator1; // on SAS in by actuator param 51h or 61h
    uint64_t numberReallocatedSectorsBetweenFarmTimeSeriesFrameNandNminus1Actuator1; // on SAS in by actuator param 51h or 61h
    uint64_t numberReallocationCandidateSectorsSinceLastFARMTimeSeriesFrameSavedActuator1; // on SAS in by actuator param 51h or 61h
    uint64_t numberReallocationCandidateSectorsBetweenFarmTimeSeriesFrameNandNminus1Actuator1; // on SAS in by actuator param 51h or 61h
    uint64_t numberUniqueUnrecoverableSectorsSinceLastFARMTimeSeriesFrameSavedByHead[FARM_MAX_HEADS];// on SAS in by param 107h
    uint64_t numberUniqueUnrecoverableSectorsBetweenFarmTimeSeriesFrameNandNminus1ByHead[FARM_MAX_HEADS];// on SAS in by param 108h
    uint64_t satareserved4[FARM_RESERVED_ERROR_STATISTICS];
}farmErrorStatistics;

M_STATIC_ASSERT(offsetof(farmErrorStatistics, reserved1) == 160, farm_error_reserved1_wrong_offset);
M_STATIC_ASSERT(offsetof(farmErrorStatistics, reserved2) == 296, farm_error_reserved2_wrong_offset);
M_STATIC_ASSERT(offsetof(farmErrorStatistics, cumulativeLifetimeUnrecoverableReadErrorsDueToERC) == 560, farm_error_cumltUnrecReadERC_wrong_offset);
M_STATIC_ASSERT(offsetof(farmErrorStatistics, numberOfReallocatedSectorsActuator1) == 952, farm_error_realloc_sector_act1_wrong_offset);

M_STATIC_ASSERT(offsetof(farmErrorStatistics, reserved3) == 1048, farm_error_reserved3_wrong_offset);
M_STATIC_ASSERT(offsetof(farmErrorStatistics, satareserved4) == 1824, farm_error_reserved4_wrong_offset);

M_STATIC_ASSERT(sizeof(farmErrorStatistics) == FARM_PAGE_LEN, farm_error_stats_stuct_is_not_16kib);

#define FARM_ENV_STAT_RESERVED 2016
typedef struct s_farmEnvironmentStatistics
{
    uint64_t pageNumber;
    uint64_t copyNumber;
    uint64_t currentTemperature;//degrees C
    uint64_t highestTemperature;
    uint64_t lowestTemperature;
    uint64_t avgShortTermTemp;//reserved SAS
    uint64_t avgLongTermTemp;//reserved SAS
    uint64_t highestAvgShortTermTemp;//reserved SAS
    uint64_t lowestAvgShortTermTemp;//reserved SAS
    uint64_t highestAvgLongTermTemp;//reserved SAS
    uint64_t lowestAvgLongTermTemp;//reserved SAS
    uint64_t timeOverTemp;//Minutes //reserved SAS
    uint64_t timeUnderTemp;//Minutes //reserved SAS
    uint64_t specifiedMaxTemp;
    uint64_t specifiedMinTemp;
    uint64_t reserved1[2]; //reserved SAS
    uint64_t currentRelativeHumidity;// in .1% increments
    uint64_t reserved2; //reserved SAS
    uint64_t currentMotorPowerFromMostRecentSMARTSummaryFrame;
    uint64_t current12Vinput;//millivolts
    uint64_t min12Vinput;//millivolts
    uint64_t max12Vinput;//millivolts
    uint64_t current5Vinput;//millivolts
    uint64_t min5Vinput;//millivolts
    uint64_t max5Vinput;//millivolts
    uint64_t average12Vpwr;//milliwatts
    uint64_t min12VPwr;//milliwatts
    uint64_t max12VPwr;//milliwatts
    uint64_t average5Vpwr;////milliwatts
    uint64_t min5Vpwr;//milliwatts
    uint64_t max5Vpwr;//milliwatts
    uint64_t reserved[FARM_ENV_STAT_RESERVED];
}farmEnvironmentStatistics;

M_STATIC_ASSERT(sizeof(farmEnvironmentStatistics) == FARM_PAGE_LEN, farm_environment_stats_stuct_is_not_16kib);

#define FARM_RELI_RESERVED1 58
#define FARM_RELI_RESERVED2 26
#define FARM_RELI_RESERVED3 73
#define FARM_RELI_RESERVED4 24
#define FARM_RELI_RESERVED5 27
#define FARM_RELI_RESERVED6 264
#define FARM_RELI_RESERVED7 178
#define FARM_RELI_RESERVED8 4
#define FARM_RELI_RESERVED9 241
#define FARM_RELI_RESERVED10 485

#define MR_HEAD_RESISTANCE_PERCENT_DELTA_FACTORY_BIT BIT0
#define MR_HEAD_RESISTANCE_NEGATIVE_BIT BIT1
static M_INLINE uint8_t get_Farm_MR_Head_Resistance_Bits(uint64_t mrHeadData)
{
    return M_Byte6(mrHeadData);
}

typedef struct s_farmReliabilityStatistics
{
    uint64_t pageNumber;
    uint64_t copyNumber;
    uint64_t reserved1[FARM_RELI_RESERVED1];
    uint64_t numDOSScansPerformed;// on SAS in by actuator param 50h or 60h
    uint64_t numLBAsCorrectedByISP;// on SAS in by actuator param 50h or 60h
    uint64_t reserved2[FARM_RELI_RESERVED2];
    uint64_t dvgaSkipWriteDetectByHead[FARM_MAX_HEADS];
    uint64_t rvgaSkipWriteDetectByHead[FARM_MAX_HEADS];
    uint64_t fvgaSkipWriteDetectByHead[FARM_MAX_HEADS];
    uint64_t skipWriteDetectExceedsThresholdByHead[FARM_MAX_HEADS];
    union {
        uint64_t readErrorRate;//SMART attr 1 raw - SATA
        uint64_t numRAWOperations;//SAS 
    };
    uint64_t readErrorRateNormalized;
    uint64_t readErrorRateWorstEver;
    uint64_t seekErrorRate;//SMART attr 7 raw
    uint64_t seekErrorRateNormalized;
    uint64_t seekErrorRateWorstEver;
    uint64_t highPriorityUnloadEvents; // ON SAS
    uint64_t reserved3[FARM_RELI_RESERVED3];
    uint64_t mrHeadResistanceByHead[FARM_MAX_HEADS];// on SAS in by param 1Ah
    uint64_t reserved4[FARM_RELI_RESERVED4];
    uint64_t velocityObserverByHead[FARM_MAX_HEADS];
    uint64_t numberOfVelocityObserverByHead[FARM_MAX_HEADS];
    uint64_t currentH2SATtrimmedMeanBitsInErrorByHeadZone1[FARM_MAX_HEADS];// on SAS in by param 20h
    uint64_t currentH2SATtrimmedMeanBitsInErrorByHeadZone2[FARM_MAX_HEADS];// on SAS in by param 31h
    uint64_t currentH2SATtrimmedMeanBitsInErrorByHeadZone3[FARM_MAX_HEADS];// on SAS in by param 32h
    uint64_t currentH2SATiterationsToConvergeByHeadZone1[FARM_MAX_HEADS];// on SAS in by param 33h
    uint64_t currentH2SATiterationsToConvergeByHeadZone2[FARM_MAX_HEADS];// on SAS in by param 34h
    uint64_t currentH2SATiterationsToConvergeByHeadZone3[FARM_MAX_HEADS];// on SAS in by param 35h
    uint64_t currentH2SATpercentCodewordsPerIterByHeadTZAvg[FARM_MAX_HEADS];
    uint64_t currentH2SATamplitudeByHeadTZAvg[FARM_MAX_HEADS];// on SAS in by param 1Fh
    uint64_t currentH2SATasymmetryByHeadTZAvg[FARM_MAX_HEADS];// on SAS in by param 20h
    uint64_t appliedFlyHeightClearanceDeltaByHeadOuter[FARM_MAX_HEADS];
    uint64_t appliedFlyHeightClearanceDeltaByHeadInner[FARM_MAX_HEADS];
    uint64_t appliedFlyHeightClearanceDeltaByHeadMiddle[FARM_MAX_HEADS];
    uint64_t numDiscSlipRecalibrationsPerformed; // ON SAS
    uint64_t numReallocatedSectorsByHead[FARM_MAX_HEADS];// on SAS in by param 21h
    uint64_t numReallocationCandidateSectorsByHead[FARM_MAX_HEADS];// on SAS in by param 22h
    uint64_t heliumPressureThresholdTrip;// 0 - no trip, 1 = trip // ON SAS
    uint64_t dosOughtScanCountByHead[FARM_MAX_HEADS];
    uint64_t dosNeedToScanCountByHead[FARM_MAX_HEADS];
    uint64_t dosWriteFaultScansByHead[FARM_MAX_HEADS];
    uint64_t writeWorkloadPowerOnTimeByHead[FARM_MAX_HEADS];//seconds // on SAS in by param 26h
    uint64_t reserved5[FARM_RELI_RESERVED5];
    uint64_t secondHeadMRHeadResistanceByHead[FARM_MAX_HEADS];// on SAS in by param 43h
    uint64_t reserved6[FARM_RELI_RESERVED6];
    uint64_t numLBAsCorrectedByParitySector;//actuator 0 // on SAS in by actuator param 50h or 60h
    uint64_t superParityCoveragePercent;//actuator 0
    uint64_t reserved7[FARM_RELI_RESERVED7];
    uint64_t numDOSScansPerformedActuator1;// on SAS in by actuator param 50h or 60h
    uint64_t numLBAsCorrectedByISPActuator1;// on SAS in by actuator param 50h or 60h
    uint64_t reserved8[FARM_RELI_RESERVED8];
    uint64_t numLBAsCorrectedByParitySectorActuator1; // on SAS in by actuator param 50h or 60h
    uint64_t reserved9[FARM_RELI_RESERVED9];
    uint64_t primarySuperParityCoveragePercentageSMR_HSMR_SWR;//actuator0 // on SAS in by actuator param 50h or 60h
    uint64_t primarySuperParityCoveragePercentageSMR_HSMR_SWRActuator1;   // on SAS in by actuator param 50h or 60h
    uint64_t lifetimeTerabytesWrittenperHead[FARM_MAX_HEADS];// on SAS in by param 100h
    uint64_t reserved10[FARM_RELI_RESERVED10];
}farmReliabilityStatistics;

//check that fields after the various reserved blocks are in the right places
M_STATIC_ASSERT(offsetof(farmReliabilityStatistics, numDOSScansPerformed) == 480, farm_reli_dosscan_perf_wrong_offset);
M_STATIC_ASSERT(offsetof(farmReliabilityStatistics, dvgaSkipWriteDetectByHead) == 704, farm_reli_dvgaSkipWriteDetectByHead_wrong_offset);
M_STATIC_ASSERT(offsetof(farmReliabilityStatistics, mrHeadResistanceByHead) == 2112, farm_reli_mrHeadResistanceByHead_wrong_offset);
M_STATIC_ASSERT(offsetof(farmReliabilityStatistics, velocityObserverByHead) == 2496, farm_reli_velocityObserverByHead_wrong_offset);
M_STATIC_ASSERT(offsetof(farmReliabilityStatistics, currentH2SATtrimmedMeanBitsInErrorByHeadZone1) == 2880, farm_reli_h2sattrimMeanBitsHeadZ1_wrong_offset);
M_STATIC_ASSERT(offsetof(farmReliabilityStatistics, currentH2SATtrimmedMeanBitsInErrorByHeadZone2) == 3072, farm_reli_h2sattrimMeanBitsHeadZ2_wrong_offset);
M_STATIC_ASSERT(offsetof(farmReliabilityStatistics, currentH2SATtrimmedMeanBitsInErrorByHeadZone3) == 3264, farm_reli_h2sattrimMeanBitsHeadZ3_wrong_offset);
M_STATIC_ASSERT(offsetof(farmReliabilityStatistics, currentH2SATiterationsToConvergeByHeadZone1) == 3456, farm_reli_h2satiterConvHeadZ1_wrong_offset);
M_STATIC_ASSERT(offsetof(farmReliabilityStatistics, currentH2SATiterationsToConvergeByHeadZone2) == 3648, farm_reli_h2satiterConvHeadZ2_wrong_offset);
M_STATIC_ASSERT(offsetof(farmReliabilityStatistics, currentH2SATiterationsToConvergeByHeadZone3) == 3840, farm_reli_h2satiterConvHeadZ3_wrong_offset);
M_STATIC_ASSERT(offsetof(farmReliabilityStatistics, secondHeadMRHeadResistanceByHead) == 6568, farm_reli_secondHeadMRHeadResistanceByHead_wrong_offset);
M_STATIC_ASSERT(offsetof(farmReliabilityStatistics, numLBAsCorrectedByParitySector) == 8872, farm_reli_numLBAsCorrectedByParitySector_wrong_offset);
M_STATIC_ASSERT(offsetof(farmReliabilityStatistics, numDOSScansPerformedActuator1) == 10312, farm_reli_numDOSScansPerformedActuator1_wrong_offset);
M_STATIC_ASSERT(offsetof(farmReliabilityStatistics, reserved10) == 12504, farm_reli_dosscan_perf_wrong_offset);

M_STATIC_ASSERT(sizeof(farmReliabilityStatistics) == FARM_PAGE_LEN, farm_reliability_stats_stuct_is_not_16kib);

typedef struct s_farmLogData
{
    farmHeader header;
    farmDriveInfo driveinfo;
    farmWorkload workload;
    farmErrorStatistics error;
    farmEnvironmentStatistics environment;
    farmReliabilityStatistics reliability;
}farmLogData;

    // TODO: Option to select which FARM data between current, saved, factory
eReturnValues read_FARM_Data(tDevice* device, farmLogData* farmdata);

void print_FARM_Data(farmLogData *farmdata);

#if defined(__cplusplus)
}
#endif
