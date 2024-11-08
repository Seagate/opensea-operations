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
// \file smart.h
// \brief This file defines the functions related to SMART features on a drive (attributes, Status check)

#pragma once

#include "ata_helper.h"
#include "operations_Common.h"

#if defined(__cplusplus)
extern "C"
{
#endif

    typedef struct s_smartLogData
    {
        union
        {
            ataSMARTLog  ataSMARTAttr;
            nvmeSmartLog nvmeSMARTAttr;
        } attributes;
    } smartLogData;

#define MAX_ATTRIBUTE_NAME_LENGTH 43 // This leaves room for a M_NULLPTR terminating character

// SMART attributes are NOT standardized. Use these definitions with caution as they may have different meanings between
// vendors and firmwares. - TJE
#define ATTRB_NUM_RETIRED_SECTOR (5)
#define ATTRB_NUM_SEEK_ERRORS    (7)
#define ATTRB_NUM_POH            (9) // Power On Hours.
#define ATTRB_NUM_SHOCK_COUNT    (191)
#define ATTRB_NUM_PENDING_SPARES (197)
#define ATTRB_NUM_CRC_ERROR      (199)

    //-----------------------------------------------------------------------------
    //
    // get_SMART_Attributes( tDevice * device )
    //
    //! \brief   Gets the SMART attributes
    //!
    //  Entry:
    //!   \param[in]  device file descriptor
    //!   \param[out] smartAttrs structure that hold attributes.
    //!
    //  Exit:
    //!   \return SUCCESS = good, !SUCCESS something went wrong see error codes
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API eReturnValues get_SMART_Attributes(tDevice* device, smartLogData* smartAttrs);

    OPENSEA_OPERATIONS_API void get_Attribute_Name(tDevice* device, uint8_t attributeNumber, char** attributeName);

    typedef enum eSMARTAttrOutModeEnum
    {
        SMART_ATTR_OUTPUT_RAW,
        SMART_ATTR_OUTPUT_ANALYZED,
        SMART_ATTR_OUTPUT_HYBRID
    } eSMARTAttrOutMode;

    //-----------------------------------------------------------------------------
    //
    // print_SMART_Attributes( tDevice * device )
    //
    //! \brief   Pulls the SMART attributes and parses them for display to the user (SATA only)
    //
    //  Entry:
    //!   \param[in]  device file descriptor
    //!   \param[in] outputMode -  mode to use for displaying the attributes
    //
    //  Exit:
    //!   \return SUCCESS = good, !SUCCESS something went wrong see error codes
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API eReturnValues print_SMART_Attributes(tDevice* device, eSMARTAttrOutMode outputMode);

    //-----------------------------------------------------------------------------
    //
    // show_NVMe_Health( tDevice * device )
    //
    //! \brief   Pulls the NVMe health data and displays it to stdout
    //
    //  Entry:
    //!   \param[in]  device file descriptor
    //!   \param[in] outputMode -  mode to use for displaying the attributes
    //
    //  Exit:
    //!   \return SUCCESS = good, !SUCCESS something went wrong see error codes
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API eReturnValues show_NVMe_Health(tDevice* device);

    typedef enum eSMARTTripInfoTypeEnum
    {
        SMART_TRIP_INFO_TYPE_UNKNOWN,
        SMART_TRIP_INFO_TYPE_SCSI,
        SMART_TRIP_INFO_TYPE_ATA,
        SMART_TRIP_INFO_TYPE_NVME
    } eSMARTTripInfoType;

    typedef struct s_smartTripInfo
    {
        bool informationIsValid;
        char reasonString[UINT8_MAX]; // This is a string with a translatable reason for the trip. Only valid when the
                                      // above bool is set. The fields below will also give the reason if you would like
                                      // to use those instead.
        uint8_t            reasonStringLength; // length of the string. If it is zero, then it was not used.
        eSMARTTripInfoType additionalInformationType;
        union
        {
            struct
            {
                uint8_t asc;
                uint8_t ascq;
            } scsiSenseCode;
            struct
            {
                uint8_t attributeNumber; // NOTE: This may not be available since the threshold sector has been obsolete
                                         // for a long time. If this is zero, it is an invalid attribute number
                uint8_t thresholdValue;
                uint8_t nominalValue;
                uint8_t worstValue;
            } ataAttribute;
            struct
            {
                bool spareSpaceBelowThreshold;
                bool temperatureExceedsThreshold; // Above or below a threshold
                bool nvmSubsystemDegraded;
                bool mediaReadOnly;
                bool volatileMemoryBackupFailed;
                bool persistentMemoryRegionReadOnlyOrUnreliable;
                bool reservedBit6; // reserved as of nvme 1.4c
                bool reservedBit7; // reserved as of nvme 1.4c
            } nvmeCriticalWarning;
        };
    } smartTripInfo, *ptrSmartTripInfo;

    //-----------------------------------------------------------------------------
    //
    //  run_SMART_Check()
    //
    //! \brief   Description:  Function to run a SMART check on and ATA or SCSI device
    //
    //  Entry:
    //!   \param[in] device = file descriptor
    //!   \param[in] tripInfo = OPTIONAL pointer to a struct to get why a drive has been tripped (if available).
    //!
    //  Exit:
    //!   \return SUCCESS = pass, FAILURE = SMART tripped, IN_PROGRESS = warning condition detected, all others -
    //!   unknown status or error occured
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API eReturnValues run_SMART_Check(tDevice* device, ptrSmartTripInfo tripInfo);

    //-----------------------------------------------------------------------------
    //
    //  print_SMART_Tripped_Message()
    //
    //! \brief   Description:  prints the SFF-8055 SMART trip approved warning message. Output modified to handle modern
    //! SSDs if passed in
    //
    //  Entry:
    //!   \param[in] ssd = true means output showing the SSD is tripping SMART, otherwise output indicating it is an HDD
    //!   tripping SMART
    //!
    //  Exit:
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API void print_SMART_Tripped_Message(bool ssd);

    //-----------------------------------------------------------------------------
    //
    //  ata_SMART_Check()
    //
    //! \brief   Description:  Function to perform an ATA SMART check on a device (sends SMART return status and checks
    //! that the rtfrs come back as expected)
    //
    //  Entry:
    //!   \param[in] device = file descriptor
    //!   \param[in] tripInfo = OPTIONAL pointer to a struct to get why a drive has been tripped (if available).
    //!
    //  Exit:
    //!   \return SUCCESS = SMART healthy,
    //!           FAILURE = SMART tripped,
    //!           COMMAND_FAILURE = unknown error/smart not enabled undefined status,
    //!           UNKNOWN - didn't get back rtfrs, so unable to verify SMART status
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API eReturnValues ata_SMART_Check(tDevice* device, ptrSmartTripInfo tripInfo);

    //-----------------------------------------------------------------------------
    //
    //  scsi_SMART_Check()
    //
    //! \brief   Description:  Function to Perform a SMART check on a SCSI device
    //
    //  Entry:
    //!   \param device - pointer to the device structure
    //!   \param[in] tripInfo = OPTIONAL pointer to a struct to get why a drive has been tripped (if available).
    //!
    //  Exit:
    //!   \return SUCCESS = pass, FAILURE = SMART tripped, IN_PROGRESS = warning condition detected, COMMAND_FAILURE =
    //!   unknown error/smart not enabled undefined status, UNKNOWN - didn't get back rtfrs, so unable to verify SMART
    //!   status
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API eReturnValues scsi_SMART_Check(tDevice* device, ptrSmartTripInfo tripInfo);

    //-----------------------------------------------------------------------------
    //
    //  nvme_SMART_Check(tDevice *device, ptrSmartTripInfo tripInfo)
    //
    //! \brief   Description:  Function to Perform a SMART check on a NVMe device
    //
    //  Entry:
    //!   \param device - pointer to the device structure
    //!   \param[in] tripInfo = OPTIONAL pointer to a struct to get why a drive has been tripped (if available).
    //!
    //  Exit:
    //!   \return SUCCESS = pass, FAILURE = SMART tripped, IN_PROGRESS = warning condition detected, COMMAND_FAILURE =
    //!   unknown error/smart not enabled undefined status, UNKNOWN - didn't get back rtfrs, so unable to verify SMART
    //!   status
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API eReturnValues nvme_SMART_Check(tDevice* device, ptrSmartTripInfo tripInfo);

    //-----------------------------------------------------------------------------
    //
    //  is_SMART_Enabled(tDevice *device)
    //
    //! \brief   Description:  Function to check if SMART is enabled on a device
    //
    //  Entry:
    //!   \param device - pointer to the device structure
    //!
    //  Exit:
    //!   \return true = enabled, false = not enabled (may not be supported or just not enabled)
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API bool is_SMART_Enabled(tDevice* device);

    OPENSEA_OPERATIONS_API bool is_SMART_Check_Supported(tDevice* device);

    OPENSEA_OPERATIONS_API eReturnValues get_Pending_List_Count(tDevice* device, uint32_t* pendingCount);

    OPENSEA_OPERATIONS_API eReturnValues get_Grown_List_Count(tDevice* device, uint32_t* grownCount);

    //-----------------------------------------------------------------------------
    //
    //  sct_Set_Feature_Control(tDevice *device, eSCTFeature sctFeature, bool enableDisable, bool defaultValue, bool
    //  isVolatile, uint16_t hdaTemperatureIntervalOrState)
    //
    //! \brief   Description:  set a SCT feature to a specific value using SCT (SMART command transport)
    //
    //  Entry:
    //!   \param device - pointer to the device structure
    //!   \param sctFeature - the code of the SCT feature to change
    //!   \param enableDisable - true = enable, false = disable
    //!   \param defaultValue - restore to drive's default value
    //!   \param isVolatile - true = volatile change (cleared on reset/power cycle), false = non-volatile change
    //!   \param hdaTemperatureIntervalOrState - used for the HDA Temperature interval feature code only to change the
    //!   state or frequency of the logging.
    //  Exit:
    //!   \return SUCCESS = pass, FAILURE = failed to change the feature, NOT_SUPPORTED = sct not supported or feature
    //!   not supported
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API eReturnValues sct_Set_Feature_Control(tDevice*    device,
                                                                 eSCTFeature sctFeature,
                                                                 bool        enableDisable,
                                                                 bool        defaultValue,
                                                                 bool        isVolatile,
                                                                 uint16_t    hdaTemperatureIntervalOrState);

    //-----------------------------------------------------------------------------
    //
    //  sct_Get_Feature_Control(tDevice *device, eSCTFeature sctFeature, bool enableDisable, bool defaultValue, bool
    //  isVolatile, uint16_t hdaTemperatureIntervalOrState)
    //
    //! \brief   Description:  get a SCT feature's information using SCT (SMART command transport)
    //
    //  Entry:
    //!   \param device - pointer to the device structure
    //!   \param sctFeature - the code of the SCT feature to change
    //!   \param enableDisable - true = enable, false = disable
    //!   \param defaultValue - restore to drive's default value
    //!   \param hdaTemperatureIntervalOrState - used for the HDA Temperature interval feature code only to change the
    //!   state or frequency of the logging. \param featureOptionFlags - option flags specific to the feature
    //  Exit:
    //!   \return SUCCESS = pass, FAILURE = failed to change the feature, NOT_SUPPORTED = sct not supported or feature
    //!   not supported
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API eReturnValues sct_Get_Feature_Control(tDevice*    device,
                                                                 eSCTFeature sctFeature,
                                                                 bool*       enableDisable,
                                                                 bool*       defaultValue,
                                                                 uint16_t*   hdaTemperatureIntervalOrState,
                                                                 uint16_t*   featureOptionFlags);

    typedef enum eSCTErrorRecoveryCommandEnum
    {
        SCT_ERC_READ_COMMAND,
        SCT_ERC_WRITE_COMMAND
    } eSCTErrorRecoveryCommand;

    //-----------------------------------------------------------------------------
    //
    //  sct_Set_Command_Timer(tDevice *device, eSCTErrorRecoveryCommand ercCommand, uint32_t timerValueMilliseconds)
    //
    //! \brief   Description:  Set the SCT Error recovery command timeout value
    //
    //  Entry:
    //!   \param device - pointer to the device structure
    //!   \param ercCommand - specifies if the timer is for read or write commands
    //!   \param timerValueMilliseconds - how long to set the timer to in milliseconds
    //!   \param isVolatile - true = volatile change (cleared on reset/power cycle), false = non-volatile change
    //  Exit:
    //!   \return SUCCESS = pass, FAILURE = failed to change the feature, NOT_SUPPORTED = sct not supported or feature
    //!   not supported
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API eReturnValues sct_Set_Command_Timer(tDevice*                 device,
                                                               eSCTErrorRecoveryCommand ercCommand,
                                                               uint32_t                 timerValueMilliseconds,
                                                               bool                     isVolatile);

    //-----------------------------------------------------------------------------
    //
    //  sct_Get_Command_Timer(tDevice *device, eSCTErrorRecoveryCommand ercCommand, uint32_t timerValueMilliseconds)
    //
    //! \brief   Description:  Get the SCT Error recovery command timeout value
    //
    //  Entry:
    //!   \param device - pointer to the device structure
    //!   \param ercCommand - specifies if the timer is for read or write commands
    //!   \param timerValueMilliseconds - how long to set the timer to in milliseconds
    //!   \param isVolatile - true = volatile change (cleared on reset/power cycle), false = non-volatile change
    //  Exit:
    //!   \return SUCCESS = pass, FAILURE = failed to change the feature, NOT_SUPPORTED = sct not supported or feature
    //!   not supported
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API eReturnValues sct_Get_Command_Timer(tDevice*                 device,
                                                               eSCTErrorRecoveryCommand ercCommand,
                                                               uint32_t*                timerValueMilliseconds,
                                                               bool                     isVolatile);

    //-----------------------------------------------------------------------------
    //
    //  sct_Restore_Command_Timer(tDevice *device, eSCTErrorRecoveryCommand ercCommand)
    //
    //! \brief   Description:  Restore the SCT Error recovery command timeout value to default
    //
    //  Entry:
    //!   \param device - pointer to the device structure
    //!   \param ercCommand - specifies if the timer is for read or write commands
    //  Exit:
    //!   \return SUCCESS = pass, FAILURE = failed to change the feature, NOT_SUPPORTED = sct not supported or feature
    //!   not supported
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API eReturnValues sct_Restore_Command_Timer(tDevice*                 device,
                                                                   eSCTErrorRecoveryCommand ercCommand);

    //-----------------------------------------------------------------------------
    //
    //  sct_Get_Min_Recovery_Time_Limit(tDevice *device, uint32_t *minRcvTimeLmtMilliseconds)
    //
    //! \brief   Description:  Get the Minimum supported value for SCT Error recovery command timeout
    //
    //  Entry:
    //!   \param device - pointer to the device structure
    //!   \param minRcvTimeLmtMilliseconds - Minimum supported value for the RECOVERY TIME LIMIT field in milliseconds
    //  Exit:
    //!   \return SUCCESS = pass, FAILURE = failed to change the feature, NOT_SUPPORTED = sct not supported or feature
    //!   not supported
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API eReturnValues sct_Get_Min_Recovery_Time_Limit(tDevice*  device,
                                                                         uint32_t* minRcvTimeLmtMilliseconds);

    //-----------------------------------------------------------------------------
    //
    //  enable_Disable_SMART_Feature(tDevice *device, bool enable)
    //
    //! \brief   Description:  Enable or disable the SMART feature on a device
    //
    //  Entry:
    //!   \param device - pointer to the device structure
    //!   \param enable - true = set to enabled, false = set to disabled
    //  Exit:
    //!   \return SUCCESS = pass, FAILURE = failed to change the feature, NOT_SUPPORTED = feature not supported
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API eReturnValues enable_Disable_SMART_Feature(tDevice* device, bool enable);

    //-----------------------------------------------------------------------------
    //
    //  enable_Disable_SMART_Attribute_Autosave(tDevice *device, bool enable)
    //
    //! \brief   Description:  Enable or disable the SMART Attribute Autosave feature on a device
    //
    //  Entry:
    //!   \param device - pointer to the device structure
    //!   \param enable - true = set to enabled, false = set to disabled
    //  Exit:
    //!   \return SUCCESS = pass, FAILURE = failed to change the feature, NOT_SUPPORTED = feature not supported
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API eReturnValues enable_Disable_SMART_Attribute_Autosave(tDevice* device, bool enable);

    //-----------------------------------------------------------------------------
    //
    //  enable_Disable_SMART_Auto_Offline(tDevice *device, bool enable)
    //
    //! \brief   Description:  Enable or disable the SMART Auto Offline feature on a device
    //
    //  Entry:
    //!   \param device - pointer to the device structure
    //!   \param enable - true = set to enabled, false = set to disabled
    //  Exit:
    //!   \return SUCCESS = pass, FAILURE = failed to change the feature, NOT_SUPPORTED = feature not supported
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API eReturnValues enable_Disable_SMART_Auto_Offline(tDevice* device, bool enable);

    typedef struct s_smartFeatureInfo
    {
        uint16_t smartVersion;
        // smart attributes? Currently in a separate API
        uint8_t  offlineDataCollectionStatus;
        uint8_t  selfTestExecutionStatus;
        uint16_t timeToCompleteOfflineDataCollection; // vendor specific in new specs
        uint8_t  reserved;                            // also called vendor specific...
        uint8_t  offlineDataCollectionCapability;
        uint16_t smartCapability;
        uint8_t  errorLoggingCapability;
        uint8_t  vendorSpecific; // or reserved
        uint8_t  shortSelfTestPollingTime;
        uint8_t  extendedSelfTestPollingTime;
        uint8_t  conveyenceSelfTestPollingTime;
        uint16_t longExtendedSelfTestPollingTime;
        // a bunch more reserved bytes and vendor specific bytes
        // checksum
    } smartFeatureInfo, *ptrSmartFeatureInfo;

    //-----------------------------------------------------------------------------
    //
    //  get_SMART_Info(tDevice *device, ptrSmartFeatureInfo smartInfo)
    //
    //! \brief   Description:  Get SMART information from an ATA device (excludes vendor unique data and attributes)
    //
    //  Entry:
    //!   \param device - pointer to the device structure
    //!   \param smartInfo - pointer to structure to save SMART info to
    //  Exit:
    //!   \return SUCCESS = pass, FAILURE = failed to change the feature, NOT_SUPPORTED = feature not supported on this
    //!   device
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API eReturnValues get_SMART_Info(tDevice* device, ptrSmartFeatureInfo smartInfo);

    //-----------------------------------------------------------------------------
    //
    //  print_SMART_Info(tDevice *device, ptrSmartFeatureInfo smartInfo)
    //
    //! \brief   Description:  Print SMART information from an ATA device (excludes vendor unique data and attributes)
    //
    //  Entry:
    //!   \param device - pointer to the device structure
    //!   \param smartInfo - pointer to structure to save SMART info to
    //  Exit:
    //!   \return SUCCESS = pass, FAILURE = failed to change the feature, NOT_SUPPORTED = feature not supported on this
    //!   device
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API eReturnValues print_SMART_Info(tDevice* device, ptrSmartFeatureInfo smartInfo);

    OPENSEA_OPERATIONS_API eReturnValues nvme_Print_Temp_Statistics(tDevice* device);

    OPENSEA_OPERATIONS_API eReturnValues nvme_Print_PCI_Statistics(tDevice* device);

    typedef struct s_informationalExceptionsControl
    {
        bool    isValid;                 // the page was able to be read
        bool    sixByteCommandUsed;      // don't change this. for use when calling a set function after a get function.
        bool    ps;                      // don't change this. for use when calling a set function after a get function.
        uint8_t deviceSpecificParameter; // only stored here in case we are performing a mode select command after
                                         // reading the data. Recommended that this is not changed!
        bool     perf;
        bool     ebf;      // enable device specific background functions (not related to bms)
        bool     ewasc;    // enable warning additional sense code
        bool     dexcpt;   // disable exception control
        bool     test;     // set test mode, device behaves as if there is an error
        bool     ebackerr; // enable background error
        bool     logerr; // log error. 1 = will log the error to informational exceptions log, 0 means it may or maynot.
        uint8_t  mrie;
        uint32_t intervalTimer;
        uint32_t reportCount;
    } informationalExceptionsControl, *ptrInformationalExceptionsControl;

    typedef struct s_informationalExceptionsLog
    {
        bool    isValid;
        uint8_t additionalSenseCode;
        uint8_t additionalSenseCodeQualifier;
        uint8_t mostRecentTemperatureReading;
        // all other bytes are vendor specific
    } informationalExceptionsLog, *ptrInformationalExceptionsLog;

    //-----------------------------------------------------------------------------
    //
    //  get_SCSI_Informational_Exceptions_Info(tDevice *device, eScsiModePageControl mpc,
    //  ptrInformationalExceptionsControl controlData, ptrInformationalExceptionsLog logData)
    //
    //! \brief   Description:  Get SCSI Informational Exceptions information (SMART)
    //
    //  Entry:
    //!   \param device - pointer to the device structure
    //!   \param mpc - Default vs Saved vs Current information
    //!   \param controlData - pointer to structure to save mode page data to
    //!   \param logData - pointer to structure to save log page data to
    //  Exit:
    //!   \return SUCCESS = pass, FAILURE = failed to change the feature, NOT_SUPPORTED = feature not supported on this
    //!   device
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API eReturnValues
    get_SCSI_Informational_Exceptions_Info(tDevice*                          device,
                                           eScsiModePageControl              mpc,
                                           ptrInformationalExceptionsControl controlData,
                                           ptrInformationalExceptionsLog     logData);

    //-----------------------------------------------------------------------------
    //
    //  get_SCSI_Informational_Exceptions_Info(tDevice *device, eScsiModePageControl mpc,
    //  ptrInformationalExceptionsControl controlData, ptrInformationalExceptionsLog logData)
    //
    //! \brief   Description:  Set SCSI Informational Exceptions information (SMART). This should be called AFTER the
    //! get_SCSI_Informational_Exceptions_Info function since a mode sense is required before a mode select...
    //
    //  Entry:
    //!   \param device - pointer to the device structure
    //!   \param save - Set to true to make this change save across a power cycle. False to make this change without
    //!   saving it. \param controlData - pointer to structure to save mode page data to
    //  Exit:
    //!   \return SUCCESS = pass, FAILURE = failed to change the feature, NOT_SUPPORTED = feature not supported on this
    //!   device
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API eReturnValues
    set_SCSI_Informational_Exceptions_Info(tDevice* device, bool save, ptrInformationalExceptionsControl controlData);

    //-----------------------------------------------------------------------------
    //
    //  set_MRIE_Mode(tDevice *device, uint8_t mrieMode, bool driveDefault)
    //
    //! \brief   Description:  Set SCSI Informational Exceptions MRIE (Method of reporting informational exceptions)
    //! (SMART Check) (Changes when the condition is reported and the sense code used)
    //
    //  Entry:
    //!   \param device - pointer to the device structure
    //!   \param mrieMode - A value of 0 - 6. 0 = off, 6 = on request. See spec for mode details or use enum from
    //!   scsi_helper.h \param driveDefault - restore to the drive's default value
    //  Exit:
    //!   \return SUCCESS = pass, FAILURE = failed to change the feature, NOT_SUPPORTED = feature not supported on this
    //!   device
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API eReturnValues set_MRIE_Mode(tDevice* device, uint8_t mrieMode, bool driveDefault);

#define SMART_ERROR_STATE_MASK                                                                                         \
    0x0F // highnibble is vendor unique. use this to look at the low nibble and match a state to the enum below
    typedef enum eSMARTErrorStateEnum // Low nibble only!!! high nibble is vendor unique!
    {
        SMART_ERROR_STATE_UNKNOWN                = 0x0,
        SMART_ERROR_STATE_SLEEP                  = 0x1,
        SMART_ERROR_STATE_STANDBY                = 0x2,
        SMART_ERROR_STATE_ACTIVE_IDLE            = 0x3,
        SMART_ERROR_STATE_EXECUTING_OFFLINE_TEST = 0x4,
        SMART_ERROR_STATE_RESERVED1              = 0x5,
        SMART_ERROR_STATE_RESERVED2              = 0x6,
        SMART_ERROR_STATE_RESERVED3              = 0x7,
        SMART_ERROR_STATE_RESERVED4              = 0x8,
        SMART_ERROR_STATE_RESERVED5              = 0x9,
        SMART_ERROR_STATE_RESERVED6              = 0xA,
        SMART_ERROR_STATE_VENDOR_SPECIFIC1       = 0xB,
        SMART_ERROR_STATE_VENDOR_SPECIFIC2       = 0xC,
        SMART_ERROR_STATE_VENDOR_SPECIFIC3       = 0xD,
        SMART_ERROR_STATE_VENDOR_SPECIFIC4       = 0xE,
        SMART_ERROR_STATE_VENDOR_SPECIFIC5       = 0xF
    } eSMARTErrorState;

    typedef struct s_SMARTCommandDataStructure
    {
        uint8_t  transportSpecific; // when command was initiated. If FFh, then this is a hardware reset
        uint8_t  feature;
        uint8_t  count;
        uint8_t  lbaLow;
        uint8_t  lbaMid;
        uint8_t  lbaHi;
        uint8_t  device;
        uint8_t  contentWritten;        // command register?
        uint32_t timestampMilliseconds; // since power on. can wrap
    } SMARTCommandDataStructure;

    typedef struct s_ExtSMARTCommandDataStructure
    {
        uint8_t  deviceControl;
        uint8_t  feature;
        uint8_t  featureExt;
        uint8_t  count;
        uint8_t  countExt;
        uint8_t  lbaLow;
        uint8_t  lbaLowExt;
        uint8_t  lbaMid;
        uint8_t  lbaMidExt;
        uint8_t  lbaHi;
        uint8_t  lbaHiExt;
        uint8_t  device;
        uint8_t  contentWritten; // command register?
        uint8_t  reserved;
        uint32_t timestampMilliseconds; // since power on. can wrap
    } ExtSMARTCommandDataStructure;

#define VENDOR_EXTENDED_SMART_CMD_ERR_DATA_LEN (19)

    typedef struct s_SMARTCommandErrorDataStructure
    {
        uint8_t  reserved;
        uint8_t  error;
        uint8_t  count;
        uint8_t  lbaLow;
        uint8_t  lbaMid;
        uint8_t  lbaHi;
        uint8_t  device;
        uint8_t  status;
        uint8_t  extendedErrorInformation[VENDOR_EXTENDED_SMART_CMD_ERR_DATA_LEN]; // vendor specific
        uint8_t  state;
        uint16_t lifeTimestamp; // POH when error occured
    } SMARTCommandErrorDataStructure;

    typedef struct s_ExtSMARTCommandErrorDataStructure
    {
        uint8_t  transportSpecific;
        uint8_t  error;
        uint8_t  count;
        uint8_t  countExt;
        uint8_t  lbaLow;
        uint8_t  lbaLowExt;
        uint8_t  lbaMid;
        uint8_t  lbaMidExt;
        uint8_t  lbaHi;
        uint8_t  lbaHiExt;
        uint8_t  device;
        uint8_t  status;
        uint8_t  extendedErrorInformation[VENDOR_EXTENDED_SMART_CMD_ERR_DATA_LEN]; // vendor specific
        uint8_t  state;
        uint16_t lifeTimestamp; // POH when error occured
    } ExtSMARTCommandErrorDataStructure;

    typedef struct s_SMARTErrorDataStructure
    {
        uint8_t version;
        uint8_t numberOfCommands;  // number of commands logged before the error. Between 0 and 5
        bool    extDataStructures; // when true, the data structures are for ext commands (ext comprehensive vs
                                   // comprehensive/summary logs)
        union
        {
            SMARTCommandDataStructure    command[5];
            ExtSMARTCommandDataStructure extCommand[5];
        };
        union
        {
            SMARTCommandErrorDataStructure    error;
            ExtSMARTCommandErrorDataStructure extError;
        };
    } SMARTErrorDataStructure;

#define SMART_SUMMARY_ERRORS_MAX UINT8_C(5) // defined as this in ATA spec
    typedef struct s_summarySMARTErrorLog
    {
        uint8_t                 version;
        uint8_t                 numberOfEntries;                      // max of 5
        SMARTErrorDataStructure smartError[SMART_SUMMARY_ERRORS_MAX]; // sorted in order from most recent to oldest
        uint16_t                deviceErrorCount;
        bool                    checksumsValid;
    } summarySMARTErrorLog, *ptrSummarySMARTErrorLog;

#define SMART_COMPREHENSIVE_ERRORS_MAX                                                                                 \
    UINT8_C(25) // 255 is the maximum allowed by the spec. We are doing less than this since we don't have products
                // supporting more than this.
#define SMART_EXT_COMPREHENSIVE_ERRORS_MAX                                                                             \
    UINT8_C(100) // 65532 is the maximum allowed by the spec. We are doing less than this since we don't have products
                 // supporting more than this. Other vendors might though...

    typedef struct s_comprehensiveSMARTErrorLog
    {
        uint8_t version;
        uint8_t numberOfEntries;
        bool    extLog;
        // NOTE: This union isn't really needed...Can remove this sometime
        union
        {
            SMARTErrorDataStructure
                smartError[SMART_COMPREHENSIVE_ERRORS_MAX]; // sorted in order from most recent to oldest
            SMARTErrorDataStructure
                extSmartError[SMART_EXT_COMPREHENSIVE_ERRORS_MAX]; // sorted in order from most recent to oldest
        };
        uint16_t deviceErrorCount;
        bool     checksumsValid;
    } comprehensiveSMARTErrorLog, *ptrComprehensiveSMARTErrorLog;

    //-----------------------------------------------------------------------------
    //
    //  get_ATA_Summary_SMART_Error_Log(tDevice * device, ptrSummarySMARTErrorLog smartErrorLog)
    //
    //! \brief   Description:  Get the ATA Summary SMART Error Log (will be ordered from most recent to oldest according
    //! to ATA spec) (only holds 28bit commands accurately)
    //
    //  Entry:
    //!   \param device - pointer to the device structure
    //!   \param smartErrorLog - pointer to the summary SMART error log structure to fill in
    //  Exit:
    //!   \return SUCCESS = pass, FAILURE = failed to change the feature, NOT_SUPPORTED = feature not supported on this
    //!   device
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API eReturnValues get_ATA_Summary_SMART_Error_Log(tDevice*                device,
                                                                         ptrSummarySMARTErrorLog smartErrorLog);

    //-----------------------------------------------------------------------------
    //
    //  print_ATA_Summary_SMART_Error_Log(ptrSummarySMARTErrorLog errorLogData, bool genericOutput)
    //
    //! \brief   Description:  Print the ATA Summary SMART Error Log (will be ordered from most recent to oldest
    //! according to ATA spec) (only prints 28bit commands accurately due to 48bit truncation)
    //
    //  Entry:
    //!   \param errorLogData - pointer to the summary SMART error log structure to print out
    //!   \param genericOutput - true = generic output showing registers in hex. false = detailed output that is
    //!   translated from the reported regiters according to ATA spec
    //  Exit:
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API void print_ATA_Summary_SMART_Error_Log(ptrSummarySMARTErrorLog errorLogData,
                                                                  bool                    genericOutput);

    //-----------------------------------------------------------------------------
    //
    //  get_ATA_Comprehensive_SMART_Error_Log(tDevice * device, ptrComprehensiveSMARTErrorLog smartErrorLog, bool
    //  forceSMARTLog)
    //
    //! \brief   Description:  Get the ATA (ext) Comprehensive SMART Error Log (will be ordered from most recent to
    //! oldest according to ATA spec). Automatically pulls Ext log when GPL is supported (48bit drive) for most accurate
    //! information
    //
    //  Entry:
    //!   \param device - pointer to the device structure
    //!   \param smartErrorLog - pointer to the comprehensive SMART error log structure to fill in
    //!   \param forceSMARTLog - set this to true to force a 48bit drive with GPL to read the SMART log instead. NOTE:
    //!   not recommended as the SMART log can only hold 28bit commands. 48Bit may show up, but will be truncated to
    //!   fit.
    //  Exit:
    //!   \return SUCCESS = pass, FAILURE = failed to change the feature, NOT_SUPPORTED = feature not supported on this
    //!   device
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API eReturnValues
    get_ATA_Comprehensive_SMART_Error_Log(tDevice*                      device,
                                          ptrComprehensiveSMARTErrorLog smartErrorLog,
                                          bool                          forceSMARTLog);

    //-----------------------------------------------------------------------------
    //
    //  print_ATA_Comprehensive_SMART_Error_Log(ptrComprehensiveSMARTErrorLog errorLogData, bool genericOutput)
    //
    //! \brief   Description:  Print the ATA (ext) comprehensive SMART Error Log (will be ordered from most recent to
    //! oldest according to ATA spec)
    //
    //  Entry:
    //!   \param errorLogData - pointer to the summary SMART error log structure to print out
    //!   \param genericOutput - true = generic output showing registers in hex. false = detailed output that is
    //!   translated from the reported regiters according to ATA spec
    //  Exit:
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API void print_ATA_Comprehensive_SMART_Error_Log(ptrComprehensiveSMARTErrorLog errorLogData,
                                                                        bool                          genericOutput);

    OPENSEA_OPERATIONS_API bool is_SMART_Error_Logging_Supported(tDevice* device);

    OPENSEA_OPERATIONS_API bool is_SMART_Command_Transport_Supported(tDevice* device);

#if defined(__cplusplus)
}
#endif
