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

    static M_INLINE void safe_free_smart_log_data(smartLogData** smart)
    {
        safe_free_core(M_REINTERPRET_CAST(void**, smart));
    }

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
    M_NONNULL_PARAM_LIST(1, 2)
    M_PARAM_RO(1)
    M_PARAM_WO(2) OPENSEA_OPERATIONS_API eReturnValues get_SMART_Attributes(tDevice* device, smartLogData* smartAttrs);

    M_NONNULL_PARAM_LIST(1, 3)
    M_PARAM_RO(1)
    M_PARAM_WO(3)
    OPENSEA_OPERATIONS_API void get_Attribute_Name(tDevice* device, uint8_t attributeNumber, char** attributeName);

    typedef enum eSMARTAttrOutModeEnum
    {
        SMART_ATTR_OUTPUT_RAW,
        SMART_ATTR_OUTPUT_ANALYZED,
        SMART_ATTR_OUTPUT_HYBRID,
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
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1)
    OPENSEA_OPERATIONS_API eReturnValues print_SMART_Attributes(tDevice* device, eSMARTAttrOutMode outputMode);

#define MAX_SMART_STATUS_STRING_LENGTH          21 // This leaves room for a M_NULLPTR terminating character
#define MAX_RAW_FIELD_NAME_LENGTH               47 // This leaves room for a M_NULLPTR terminating character
#define MAX_RAW_FIELD_SHORT_NAME_LENGTH         41 // This leaves room for a M_NULLPTR terminating character
#define MAX_RAW_ANALYZED_FIELD_NAME_LENGTH      41 // This leaves room for a M_NULLPTR terminating character
#define MAX_RAW_ANALYZED_STRING_VALUE_LENGTH    86 // This leaves room for a M_NULLPTR terminating character
#define MAX_ATTRIBUTE_FAIL_STATUS_STRING_LENGTH 91 // This leaves room for a M_NULLPTR terminating character
#define MAX_HYBRID_RAW_STRING_LENGTH            24 // This leaves room for a M_NULLPTR terminating character
#define MAX_RAW_FEILD_UNIT_STRING_LENGTH        9  // This leaves room for a M_NULLPTR terminating character
#define MAX_RAW_FEILD_COUNT                     4  // Right now we have identified maximum 4 field for attributes

    M_DECLARE_ENUM(eATAAttributeRawFieldUnitType,
                   /*!< No Unit. */
                   RAW_FIELD_UNIT_NONE = 0,
                   /*!< Time in Milliseconds. */
                   RAW_FIELD_UNIT_TIME_IN_MILLISECONDS = 1,
                   /*!< Time in Seconds. */
                   RAW_FIELD_UNIT_TIME_IN_SECONDS = 2,
                   /*!< Time in Minutes. */
                   RAW_FIELD_UNIT_TIME_IN_MINUTE = 3,
                   /*!< Time in Hours. */
                   RAW_FIELD_UNIT_TIME_IN_HOURS = 4,
                   /*!< Temperature in Celsius. */
                   RAW_FIELD_UNIT_TEMPERATURE_IN_CELSIUS = 5,
                   /*!< Value in LBA. */
                   RAW_FIELD_UNIT_LBA = 6,
                   /*!< Value in GB. */
                   RAW_FIELD_UNIT_GB = 7,
                   /*!< Value in MB. */
                   RAW_FIELD_UNIT_MB = 8,
                   /*!< Value in GiB. */
                   RAW_FIELD_UNIT_GiB = 9,
                   /*!< Value in MiB. */
                   RAW_FIELD_UNIT_MiB = 10,
                   /*!< Value in Sectors. */
                   RAW_FIELD_UNIT_SECTORS = 11,
                   /*!< Value in Count. */
                   RAW_FIELD_UNIT_COUNT = 12,
                   /*!< Value in Percentage. */
                   RAW_FIELD_UNIT_PERCENTAGE = 13,
                   /*!< Unknown Unit. */
                   RAW_FIELD_UNIT_UNKNOWN = 14);

    M_DECLARE_ENUM(eATAAttributeThresholdType,
                   /*!< Unknown Threshold. */
                   THRESHOLD_UNKNOWN = 0,
                   /*!< Some valid value is set. */
                   THRESHOLD_SET = 1,
                   /*!< Threshold is set to always pass. */
                   THRESHOLD_ALWAYS_PASSING = 2,
                   /*!< Threshold is set to always fail. */
                   THRESHOLD_ALWAYS_FAILING = 3,
                   /*!< Threshold set to invalid value. */
                   THRESHOLD_INVALID = 4);

    M_DECLARE_ENUM(
        eATAAttributeFailStatus,
        /*!< Attribute Fail Status Not Set. */
        FAIL_STATUS_NOT_SET = 0,
        /*!< Attribute Failing now, nominal is less than threshold value(warranty attribute). */
        FAIL_STATUS_ATTRIBUTE_FAILING_NOW = 1,
        /*!< Attribute is issuing Warning now, nominal is less than threshold value(non-warranty attribute). */
        FAIL_STATUS_ATTRIBUTE_WARNING_NOW = 2,
        /*!< Attribute Failed in past, worst is less than threshold value(warranty attribute). */
        FAIL_STATUS_ATTRIBUTE_FAILED_IN_PAST = 3,
        /*!< Attribute has issued Warning in past, worst is less than threshold value(non-warranty attribute). */
        FAIL_STATUS_ATTRIBUTE_WARNED_IN_PAST = 4);

    // clang-format off
    M_PACK_ALIGN_STRUCT(ataAttributeRawFieldData, 1,
                        char                          fieldName[MAX_RAW_FIELD_NAME_LENGTH];
                        char                          fieldShortName[MAX_RAW_FIELD_SHORT_NAME_LENGTH];
                        int64_t                       fieldValue; // making it signed to handle negative values as well
                        eATAAttributeRawFieldUnitType fieldUnit;
    );

    M_PACK_ALIGN_STRUCT(ataAttributeRawData, 1,
                        uint8_t                  rawData[SMART_ATTRIBUTE_RAW_DATA_BYTE_COUNT];
                        char                     rawHybridString[MAX_HYBRID_RAW_STRING_LENGTH];
                        uint8_t                  userFieldCount; // maximum allowed MAX_RAW_FEILD_COUNT
                        ataAttributeRawFieldData rawField[MAX_RAW_FEILD_COUNT];

                        bool    int64TypeAnalyzedFieldValid; // will be true if raw data has any field representable in int64_t format
                        int64_t int64TypeAnalyzedFieldValue;
                        eATAAttributeRawFieldUnitType int64TypeAnalyzedFieldUnit;
                        char                          int64TypeAnalyzedFieldName[MAX_RAW_ANALYZED_FIELD_NAME_LENGTH];
                        char                          int64TypeAnalyzedFieldShortName[MAX_RAW_ANALYZED_FIELD_NAME_LENGTH];

                        bool   doubleTypeAnalyzedFieldValid; // will be true if raw data has any field representable in double format
                        double doubleTypeAnalyzedFieldValue;
                        eATAAttributeRawFieldUnitType doubleTypeAnalyzedFieldUnit;
                        char                          doubleTypeAnalyzedFieldName[MAX_RAW_ANALYZED_FIELD_NAME_LENGTH];
                        char                          doubleTypeAnalyzedFieldShortName[MAX_RAW_ANALYZED_FIELD_NAME_LENGTH];

                        bool stringTypeAnalyzedFieldValid; // will be true if raw data has any field representable in some string format
                        char stringTypeAnalyzedFieldName[MAX_RAW_ANALYZED_FIELD_NAME_LENGTH];
                        char stringTypeAnalyzedFieldShortName[MAX_RAW_ANALYZED_FIELD_NAME_LENGTH];
                        char stringTypeAnalyzedFieldValue[MAX_RAW_ANALYZED_STRING_VALUE_LENGTH];
    );

    M_PACK_ALIGN_STRUCT(ataAttributeTypeData, 1, 
                        bool preFailAttribute; 
                        bool onlineDataCollection;
                        bool performanceIndicator;
                        bool errorRateIndicator;
                        bool eventCounter;
                        bool selfPreserving;
    );

    M_PACK_ALIGN_STRUCT(ataAttributeThresholdInfo, 1,
                        uint8_t                    thresholdValue;
                        eATAAttributeThresholdType thresholdType; // Since we have added this enum, no need to add threshold valid boolean flag
                        eATAAttributeFailStatus    failStatus; // This is for the implementation similar to "WHEN_FAILED" info of smartmontool
                        char                       failStatusString[MAX_ATTRIBUTE_FAIL_STATUS_STRING_LENGTH];
    );

    M_PACK_ALIGN_STRUCT(ataSMARTAnalyzedAttribute, 1, 
                        bool                      isValid; 
                        uint8_t                   attributeNumber;
                        char                      attributeName[MAX_ATTRIBUTE_NAME_LENGTH];
                        uint16_t                  status;
                        ataAttributeTypeData      attributeType;
                        ataAttributeThresholdInfo thresholdInfo;
                        uint8_t                   nominal;
                        uint8_t                   worstEver;
                        bool                      seeAnalyzedFlag;
                        ataAttributeRawData       rawData;
    );

    M_PACK_ALIGN_STRUCT(ataSMARTAnalyzedData, 1,
                        ataSMARTAnalyzedAttribute attributes[ATA_SMART_LOG_MAX_ATTRIBUTES]; // attribute numbers 1 - 255 are valid (check
                                                                      // valid bit to make sure it's a used attribute)
    );
    // clang-format on

    static M_INLINE void safe_free_ata_smart_analyzed_data(ataSMARTAnalyzedData** smartData)
    {
        safe_free_core(M_REINTERPRET_CAST(void**, smartData));
    }

    M_NONNULL_PARAM_LIST(2)
    M_PARAM_WO(2)
    OPENSEA_OPERATIONS_API void get_Raw_Field_Unit_String(eATAAttributeRawFieldUnitType uintType,
                                                          char**                        unitString,
                                                          bool                          isShortName);
    //-----------------------------------------------------------------------------
    //
    // get_ATA_Analyzed_SMART_Attributes(tDevice * device, ataSMARTAnalyzedData * ataSMARTAnalyzedData )
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
    M_NONNULL_PARAM_LIST(1, 2)
    M_PARAM_RO(1)
    M_PARAM_WO(2)
    OPENSEA_OPERATIONS_API eReturnValues get_ATA_Analyzed_SMART_Attributes(tDevice*              device,
                                                                           ataSMARTAnalyzedData* smartAnylyzedData);

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
    M_NONNULL_PARAM_LIST(1) M_PARAM_RO(1) OPENSEA_OPERATIONS_API eReturnValues show_NVMe_Health(tDevice* device);

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
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1)
    M_PARAM_WO(2) OPENSEA_OPERATIONS_API eReturnValues run_SMART_Check(tDevice* device, ptrSmartTripInfo tripInfo);

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
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1)
    M_PARAM_WO(2) OPENSEA_OPERATIONS_API eReturnValues ata_SMART_Check(tDevice* device, ptrSmartTripInfo tripInfo);

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
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1)
    M_PARAM_WO(2) OPENSEA_OPERATIONS_API eReturnValues scsi_SMART_Check(tDevice* device, ptrSmartTripInfo tripInfo);

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
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1)
    M_PARAM_WO(2) OPENSEA_OPERATIONS_API eReturnValues nvme_SMART_Check(tDevice* device, ptrSmartTripInfo tripInfo);

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
    M_NONNULL_PARAM_LIST(1) M_PARAM_RO(1) OPENSEA_OPERATIONS_API bool is_SMART_Enabled(tDevice* device);

    M_NONNULL_PARAM_LIST(1) M_PARAM_RO(1) OPENSEA_OPERATIONS_API bool is_SMART_Check_Supported(tDevice* device);

    M_NONNULL_PARAM_LIST(1, 2)
    M_PARAM_RO(1)
    M_PARAM_WO(2) OPENSEA_OPERATIONS_API eReturnValues get_Pending_List_Count(tDevice* device, uint32_t* pendingCount);

    M_NONNULL_PARAM_LIST(1, 2)
    M_PARAM_RO(1)
    M_PARAM_WO(2) OPENSEA_OPERATIONS_API eReturnValues get_Grown_List_Count(tDevice* device, uint32_t* grownCount);

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
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1)
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
    M_NONNULL_PARAM_LIST(1, 3, 4, 6)
    M_PARAM_RO(1)
    M_PARAM_WO(3)
    M_PARAM_WO(4)
    M_PARAM_WO(5)
    M_PARAM_WO(6)
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
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1)
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
    M_NONNULL_PARAM_LIST(1, 3)
    M_PARAM_RO(1)
    M_PARAM_WO(3)
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
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1)
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
    M_NONNULL_PARAM_LIST(1, 2)
    M_PARAM_RO(1)
    M_PARAM_WO(2)
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
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1) OPENSEA_OPERATIONS_API eReturnValues enable_Disable_SMART_Feature(tDevice* device, bool enable);

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
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1)
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
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1) OPENSEA_OPERATIONS_API eReturnValues enable_Disable_SMART_Auto_Offline(tDevice* device, bool enable);

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
    M_NONNULL_PARAM_LIST(1, 2)
    M_PARAM_RO(1)
    M_PARAM_WO(2) OPENSEA_OPERATIONS_API eReturnValues get_SMART_Info(tDevice* device, ptrSmartFeatureInfo smartInfo);

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
    M_NONNULL_PARAM_LIST(1, 2)
    M_PARAM_RO(1)
    M_PARAM_RO(2) OPENSEA_OPERATIONS_API eReturnValues print_SMART_Info(tDevice* device, ptrSmartFeatureInfo smartInfo);

    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1) OPENSEA_OPERATIONS_API eReturnValues nvme_Print_Temp_Statistics(tDevice* device);

    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1) OPENSEA_OPERATIONS_API eReturnValues nvme_Print_PCI_Statistics(tDevice* device);

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
    M_NONNULL_PARAM_LIST(1, 3)
    M_PARAM_RO(1)
    M_PARAM_WO(3)
    M_PARAM_WO(4)
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
    M_NONNULL_PARAM_LIST(1, 3)
    M_PARAM_RO(1)
    M_PARAM_RO(3)
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
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1)
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
    M_NONNULL_PARAM_LIST(1, 2)
    M_PARAM_RO(1)
    M_PARAM_WO(2)
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
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1)
    OPENSEA_OPERATIONS_API
    void print_ATA_Summary_SMART_Error_Log(ptrSummarySMARTErrorLog errorLogData, bool genericOutput);

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
    M_NONNULL_PARAM_LIST(1, 2)
    M_PARAM_RO(1)
    M_PARAM_WO(2)
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
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1)
    OPENSEA_OPERATIONS_API
    void print_ATA_Comprehensive_SMART_Error_Log(ptrComprehensiveSMARTErrorLog errorLogData, bool genericOutput);

    M_NONNULL_PARAM_LIST(1) M_PARAM_RO(1) OPENSEA_OPERATIONS_API bool is_SMART_Error_Logging_Supported(tDevice* device);

    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1) OPENSEA_OPERATIONS_API bool is_SMART_Command_Transport_Supported(tDevice* device);

#if defined(__cplusplus)
}
#endif
