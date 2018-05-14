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
// \file smart.h
// \brief This file defines the functions related to SMART features on a drive (attributes, Status check)

#pragma once

#include "operations_Common.h"

#if defined (__cplusplus)
extern "C"
{
#endif

    typedef struct _smartLogData { 
        union {
        ataSMARTLog ataSMARTAttr;
#if !defined(DISABLE_NVME_PASSTHROUGH)
        nvmeSmartLog nvmeSMARTAttr;
#endif
        } attributes;
    } smartLogData; 

    #define MAX_ATTRIBUTE_NAME_LENGTH 43 //This leaves room for a NULL terminating character

    #define ATTRB_NUM_RETIRED_SECTOR    (5)
    #define ATTRB_NUM_SEEK_ERRORS       (7)
    #define ATTRB_NUM_POH               (9) //Power On Hours. 
    #define ATTRB_NUM_SHOCK_COUNT       (191)
    #define ATTRB_NUM_PENDING_SPARES    (197)
    #define ATTRB_NUM_CRC_ERROR         (199)

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
    OPENSEA_OPERATIONS_API int get_SMART_Attributes(tDevice *device, smartLogData * smartAttrs);

    typedef enum _eSMARTAttrOutMode
    {
        SMART_ATTR_OUTPUT_RAW,
        SMART_ATTR_OUTPUT_ANALYZED
    }eSMARTAttrOutMode;

    //-----------------------------------------------------------------------------
    //
    // print_SMART_Attributes( tDevice * device )
    //
    //! \brief   Pulls the SMART attributes and parses them for display to the user
    //
    //  Entry:
    //!   \param[in]  device file descriptor
    //!   \param[in] outputMode -  mode to use for displaying the attributes
    //
    //  Exit:
    //!   \return SUCCESS = good, !SUCCESS something went wrong see error codes
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API int print_SMART_Attributes(tDevice *device, eSMARTAttrOutMode outputMode);

    typedef enum _eSMARTTripInfoType
    {
        SMART_TRIP_INFO_TYPE_UNKNOWN,
        SMART_TRIP_INFO_TYPE_SCSI,
        SMART_TRIP_INFO_TYPE_ATA,
        SMART_TRIP_INFO_TYPE_NVME
    }eSMARTTripInfoType;

    typedef struct _smartTripInfo
    {
        bool informationIsValid;
        char reasonString[UINT8_MAX];//This is a string with a translatable reason for the trip. Only valid when the above bool is set. The fields below will also give the reason if you would like to use those instead.
        uint8_t reasonStringLength;//length of the string. If it is zero, then it was not used.
        eSMARTTripInfoType additionalInformationType;
        union
        {
            struct
            {
                uint8_t asc;
                uint8_t ascq;
            }scsiSenseCode;
            struct
            {
                uint8_t attributeNumber;//NOTE: This may not be available since the threshold sector has been obsolete for a long time. If this is zero, it is an invalid attribute number
                uint8_t thresholdValue;
                uint8_t nominalValue;
            }ataAttribute;
            struct
            {
                bool spareSpaceBelowThreshold;
                bool temperatureExceedsThreshold;//Above or below a threshold
                bool nvmSubsystemDegraded;
                bool mediaReadOnly;
                bool volatileMemoryBackupFailed;
                bool reservedBit5;//reserved as of nvme 1.3
                bool reservedBit6;//reserved as of nvme 1.3
                bool reservedBit7;//reserved as of nvme 1.3
            }nvmeCriticalWarning;
        };
    }smartTripInfo, *ptrSmartTripInfo;

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
    //!   \return SUCCESS = pass, FAILURE = SMART tripped, IN_PROGRESS = warning condition detected (from SCSI SMART check), all others - unknown status or error occured
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API int run_SMART_Check(tDevice *device, ptrSmartTripInfo tripInfo);

    //-----------------------------------------------------------------------------
    //
    //  ata_SMART_Check()
    //
    //! \brief   Description:  Function to perform an ATA SMART check on a device (sends SMART return status and checks that the rtfrs come back as expected)
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
    OPENSEA_OPERATIONS_API int ata_SMART_Check(tDevice *device, ptrSmartTripInfo tripInfo);

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
    //!   \return SUCCESS = pass, FAILURE = SMART tripped, IN_PROGRESS = warning condition detected, COMMAND_FAILURE = unknown error/smart not enabled undefined status, UNKNOWN - didn't get back rtfrs, so unable to verify SMART status
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API int scsi_SMART_Check(tDevice *device, ptrSmartTripInfo tripInfo);

    OPENSEA_OPERATIONS_API int nvme_SMART_Check(tDevice *device, ptrSmartTripInfo tripInfo);

    OPENSEA_OPERATIONS_API bool is_SMART_Enabled(tDevice *device);

    OPENSEA_OPERATIONS_API int get_Pending_List_Count(tDevice *device, uint32_t *pendingCount);

    OPENSEA_OPERATIONS_API int get_Grown_List_Count(tDevice *device, uint32_t *grownCount);

    typedef enum _eSCTFeature
    {
        SCT_FEATURE_CONTROL_WRITE_CACHE_STATE = 1,
        SCT_FEATURE_CONTROL_WRITE_CACHE_REORDERING = 2,
        SCT_FEATURE_CONTROL_SET_HDA_TEMPERATURE_INTERVAL = 3,
        SCT_FEATURE_CONTROL_RESERVED
    }eSCTFeature;

    OPENSEA_OPERATIONS_API int sct_Set_Feature_Control(tDevice *device, eSCTFeature sctFeature, bool enableDisable, bool defaultValue, bool isVolatile, uint16_t hdaTemperatureIntervalOrState);

    OPENSEA_OPERATIONS_API int sct_Get_Feature_Control(tDevice *device, eSCTFeature sctFeature, bool *enableDisable, bool *defaultValue, uint16_t *hdaTemperatureIntervalOrState, uint16_t *featureOptionFlags);

    typedef enum _eSCTErrorRecoveryCommand
    {
        SCT_ERC_READ_COMMAND,
        SCT_ERC_WRITE_COMMAND
    }eSCTErrorRecoveryCommand;

    OPENSEA_OPERATIONS_API int sct_Set_Command_Timer(tDevice *device, eSCTErrorRecoveryCommand ercCommand, uint32_t timerValueMilliseconds);

    OPENSEA_OPERATIONS_API int sct_Get_Command_Timer(tDevice *device, eSCTErrorRecoveryCommand ercCommand, uint32_t *timerValueMilliseconds);
    
    OPENSEA_OPERATIONS_API int enable_Disable_SMART_Feature(tDevice *device, bool enable);
    
    OPENSEA_OPERATIONS_API int enable_Disable_SMART_Attribute_Autosave(tDevice *device, bool enable);

    OPENSEA_OPERATIONS_API int enable_Disable_SMART_Auto_Offline(tDevice *device, bool enable);

    typedef struct _smartFeatureInfo
    {
        uint16_t smartVersion;
        //TODO: smart attributes?
        uint8_t offlineDataCollectionStatus;
        uint8_t selfTestExecutionStatus;
        uint16_t timeToCompleteOfflineDataCollection;//vendor specific in new specs
        uint8_t reserved;//also called vendor specific...
        uint8_t offlineDataCollectionCapability;
        uint16_t smartCapability;
        uint8_t errorLoggingCapability;
        uint8_t vendorSpecific;//or reserved
        uint8_t shortSelfTestPollingTime;
        uint8_t extendedSelfTestPollingTime;
        uint8_t conveyenceSelfTestPollingTime;
        uint16_t longExtendedSelfTestPollingTime;
        //a bunch more reserved bytes and vendor specific bytes
        //checksum
    }smartFeatureInfo, *ptrSmartFeatureInfo;

    OPENSEA_OPERATIONS_API int get_SMART_Info(tDevice *device, ptrSmartFeatureInfo smartInfo);

    OPENSEA_OPERATIONS_API int print_SMART_Info(tDevice *device, ptrSmartFeatureInfo smartInfo);

    typedef struct _informationalExceptionsControl
    {
        bool isValid;//the page was able to be read
        bool sixByteCommandUsed;//don't change this. for use when calling a set function after a get function.
        bool ps;//don't change this. for use when calling a set function after a get function.
        uint8_t deviceSpecificParameter;//only stored here in case we are performing a mode select command after reading the data. Recommended that this is not changed!
        bool perf;
        bool ebf;//enable device specific background functions (not related to bms)
        bool ewasc;//enable warning additional sense code
        bool dexcpt;//disable exception control
        bool test;//set test mode, device behaves as if there is an error
        bool ebackerr;//enable background error
        bool logerr;//log error. 1 = will log the error to informational exceptions log, 0 means it may or maynot.
        uint8_t mrie;
        uint32_t intervalTimer;
        uint32_t reportCount;
    }informationalExceptionsControl, *ptrInformationalExceptionsControl;

    typedef struct _informationalExceptionsLog
    {
        bool isValid;
        uint8_t additionalSenseCode;
        uint8_t additionalSenseCodeQualifier;
        uint8_t mostRecentTemperatureReading;
        //all other bytes are vendor specific
    }informationalExceptionsLog, *ptrInformationalExceptionsLog;

    OPENSEA_OPERATIONS_API int get_SCSI_Informational_Exceptions_Info(tDevice *device, eScsiModePageControl mpc, ptrInformationalExceptionsControl controlData, ptrInformationalExceptionsLog logData);

    //NOTE: This should be called AFTER the get_SCSI_Informational_Exceptions_Info function since a mode sense is required before a mode select...
    OPENSEA_OPERATIONS_API int set_SCSI_Informational_Exceptions_Info(tDevice *device, bool save, ptrInformationalExceptionsControl controlData);

    OPENSEA_OPERATIONS_API int set_MRIE_Mode(tDevice *device, uint8_t mrieMode, bool driveDefault);

#if defined (__cplusplus)
}
#endif
