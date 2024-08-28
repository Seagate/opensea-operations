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
// \file seagate_operations.h
// \brief This file defines the functions for Seagate drive specific operations

#pragma once

#include "operations_Common.h"
#include "vendor/seagate/seagate_ata_types.h"

#if defined (__cplusplus)
extern "C"
{
#endif

    //-----------------------------------------------------------------------------
    //
    //  seagate_ata_SCT_SATA_phy_speed()
    //
    //! \brief   Description:  This issues a Seagate Specific SCT command to change the SATA PHY speed. Only available on Seagate HDD's
    //
    //  Entry:
    //!   \param device - pointer to the device structure.
    //!   \param speedGen - Which SATA generation speed to set. 1 = 1.5Gb/s, 2 = 3.0Gb/s, 3 = 6.0Gb/s. All other inputs return BAD_PARAMETER
    //!
    //  Exit:
    //!   \return SUCCESS = successfully set Phy Speed, !SUCCESS = check return code
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API eReturnValues seagate_ata_SCT_SATA_phy_speed(tDevice *device, uint8_t speedGen);

    //-----------------------------------------------------------------------------
    //
    //  scsi_Set_Phy_Speed(tDevice *device, uint8_t phySpeedGen, bool allPhys, uint8_t phyNumber)
    //
    //! \brief   Description:  This issues a mode sense and mode select to the SAS phy page to change the programmed maximum link rate of 1 or all phys.
    //
    //  Entry:
    //!   \param device - pointer to the device structure.
    //!   \param phySpeedGen - Which SAS generation speed to set. 1 = 1.5Gb/s, 2 = 3.0Gb/s, 3 = 6.0Gb/s, 4 = 12.0Gb/s, 5 = 22.5Gb/s All other inputs return BAD_PARAMETER
    //!   \param allPhys - on SAS, set this to true to set all Phys to the same speed.
    //!   \param phyNumber - if allPhys is false, this is used to specify which phy to change speed on.
    //!
    //  Exit:
    //!   \return SUCCESS = successfully set Phy Speed, !SUCCESS = check return code
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API eReturnValues scsi_Set_Phy_Speed(tDevice *device, uint8_t phySpeedGen, bool allPhys, uint8_t phyNumber);

    #define SET_PHY_SPEED_MAX_GENERATION 5
    #define SET_PHY_SPEED_SATA_MAX_GENERATION 3 //SATA only has 3 generations, so it's a lower number than the overall limit above which covers SAS as well.
    //-----------------------------------------------------------------------------
    //
    //  set_phy_speed()
    //
    //! \brief   Description:  This is the friendly call to use that does all the input checking for you before calling the proper functions to set Phy Speed for SATA or SAS.
    //
    //  Entry:
    //!   \param device - pointer to the device structure.
    //!   \param phySpeedGen - Which generation speed to set. 1 = 1.5Gb/s, 2 = 3.0Gb/s, 3 = 6.0Gb/s, 4 = 12.0Gb/s (SAS), 5 = 22.5Gb/s (SAS). All other inputs return BAD_PARAMETER
    //!   \param allPhys - on SAS, set this to true to set all Phys to the same speed. This is ignored on SATA
    //!   \param phyIdentifier - if allPhys is false, this is used to specify which phy to change speed on. This is ignored on SATA
    //!
    //  Exit:
    //!   \return SUCCESS = successfully set Phy Speed, !SUCCESS = check return code
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API eReturnValues set_phy_speed(tDevice *device, uint8_t phySpeedGen, bool allPhys, uint8_t phyIdentifier);

    //-----------------------------------------------------------------------------
    //
    //  is_SCT_Low_Current_Spinup_Supported(tDevice *device)
    //
    //! \brief   Description:  This function checks if the SCT command for low current spinup is supported or not.
    //
    //  Entry:
    //!   \param device - pointer to the device structure.
    //!
    //  Exit:
    //!   \return true = supported, false = not supported
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API bool is_SCT_Low_Current_Spinup_Supported(tDevice *device);

    //-----------------------------------------------------------------------------
    //
    //  is_Low_Current_Spin_Up_Enabled(tDevice *device)
    //
    //! \brief   Description:  This function will check if low current spin up is enabled on Seagate ATA drives. Not all drives support this feature.
    //
    //  Entry:
    //!   \param device - pointer to the device structure.
    //!   \param sctCommandSupported - set to true means the SCT command is supported and to be used to determine the enabled value, otherwise false means check an identify bit on 2.5" products. Should be set to the return value of is_SCT_Low_Current_Spinup_Supported(device)
    //!
    //  Exit:
    //!   \return sctCommandSupported = true: 0 - invalid state or could not be detected due to SAT translation failure, 1 = low, 2 = default, 3 = ultralow
    //!                               = false: 0 - not enabled or not supported. 1 = low. The set features method does not have the same granularity as the SCT command.
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API int is_Low_Current_Spin_Up_Enabled(tDevice *device, bool sctCommandSupported);

    //-----------------------------------------------------------------------------
    //
    //  seagate_SCT_Low_Current_Spinup(tDevice *device, eSeagateLCSpinLevel spinupLevel)
    //
    //! \brief   Description:  This function will send the SCT command to set the state of the low-current spinup feature on a Seagate drive that supports this SCT command. NOTE: Not all Seagate products support this command.
    //
    //  Entry:
    //!   \param device - pointer to the device structure.
    //!   \param spinupLevel - the spinup mode to set
    //!
    //  Exit:
    //!   \return SUCCESS = successfully enabled low current spin up, NOT_SUPPORTED = not Seagate or drive doesn't support this feature.
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API eReturnValues seagate_SCT_Low_Current_Spinup(tDevice *device, eSeagateLCSpinLevel spinupLevel);

    //-----------------------------------------------------------------------------
    //
    //  set_Low_Current_Spin_Up(tDevice *device, bool useSCTCommand, uint8_t state)
    //
    //! \brief   Description:  Sets the state of the low-current spinup feature.
    //
    //  Entry:
    //!   \param device - pointer to the device structure.
    //!   \param useSCTCommand - set to true to use the SCT command to set spinup current level. Should be set to true when is_SCT_Low_Current_Spinup_Supported() == true
    //!   \param state - state to set low current spinup to. Tt should be set to one of eSeagateLCSpinLevel regardless of the value of useSCTCommand. This will properly translate the SCT value to a value for the Set Features command version as necessary.
    //!
    //  Exit:
    //!   \return SUCCESS = successfully enabled low current spin up, NOT_SUPPORTED = not Seagate or drive doesn't support this feature.
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API eReturnValues set_Low_Current_Spin_Up(tDevice *device, bool useSCTCommand, eSeagateLCSpinLevel state);

    //-----------------------------------------------------------------------------
    //
    //  set_SSC_Feature_SATA(tDevice *device, eSSCFeatureState mode)
    //
    //! \brief   Description:  This function will send the command to set the SSC (Spread Spectrum Clocking) state of a Seagate SATA drive. A power cycle is required to make changes take affect
    //
    //  Entry:
    //!   \param device - pointer to the device structure.
    //!   \param mode - set to enum value saying whether to enable, disable, or set to defaults
    //!
    //  Exit:
    //!   \return SUCCESS = successfully set SSC state, FAILURE = failed to set SSC state, NOT_SUPPORTED = not Seagate or drive doesn't support this feature.
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API eReturnValues set_SSC_Feature_SATA(tDevice *device, eSSCFeatureState mode);

    //-----------------------------------------------------------------------------
    //
    //  get_SSC_Feature_SATA(tDevice *device, eSSCFeatureState *mode)
    //
    //! \brief   Description:  This function will send the command to get the SSC (Spread Spectrum Clocking) state of a Seagate SATA drive.
    //
    //  Entry:
    //!   \param device - pointer to the device structure.
    //!   \param mode - pointer to enum variable that will hold the state upon successful completion
    //!
    //  Exit:
    //!   \return SUCCESS = successfully got SSC state, FAILURE = failed to get SSC state, NOT_SUPPORTED = not Seagate or drive doesn't support this feature.
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API eReturnValues get_SSC_Feature_SATA(tDevice *device, eSSCFeatureState *mode);

    typedef struct _seagateJITModes
    {
        bool valid;//this must be set to true for the remaining fileds to have meaning.
        bool vJIT;//variable...drive will use the fastest method
        bool jit0;//fastest
        bool jit1;//second fastest
        bool jit2;//second slowest
        bool jit3;//slowest
    }seagateJITModes, *ptrSeagateJITModes;

    OPENSEA_OPERATIONS_API eReturnValues seagate_Set_JIT_Modes(tDevice *device, bool disableVjit, uint8_t jitMode, bool revertToDefaults, bool nonvolatile);

    OPENSEA_OPERATIONS_API eReturnValues seagate_Get_JIT_Modes(tDevice *device, ptrSeagateJITModes jitModes);

    OPENSEA_OPERATIONS_API eReturnValues seagate_Get_Power_Balance(tDevice *device, bool *supported, bool *enabled);//SATA only. SAS should use the set power consumption options in power_control.h

    //this enum is used to know the power mode of a device
    typedef enum _ePowerBalanceMode
    {
        POWER_BAL_ENABLE = 1,
        POWER_BAL_DISABLE = 2,
        POWER_BAL_LIMITED = 3
    } ePowerBalanceMode;

    OPENSEA_OPERATIONS_API eReturnValues seagate_Set_Power_Balance(tDevice *device, ePowerBalanceMode powerMode);//SATA only. SAS should use the set power consumption options in power_control.h

    typedef enum _eIDDTests
    {
        SEAGATE_IDD_SHORT,
        SEAGATE_IDD_LONG,
    }eIDDTests;

    typedef struct _iddSupportedFeatures
    {
        bool iddShort;//reset and recalibrate
        bool iddLong;//testPendingAndReallocationLists
    }iddSupportedFeatures, *ptrIDDSupportedFeatures;

    //-----------------------------------------------------------------------------
    //
    //  get_IDD_Support()
    //
    //! \brief   Description:  Gets which IDD features/operations are supported by the device
    //
    //  Entry:
    //!   \param[in] device = file descriptor
    //!   \param[in] iddSupport = pointer to a iddSupportedFeatures structure that will hold which features are supported
    //!
    //  Exit:
    //!   \return SUCCESS on successful completion, FAILURE = fail, NOT_SUPPORTED = IDD not supported
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API eReturnValues get_IDD_Support(tDevice *device, ptrIDDSupportedFeatures iddSupport);

    //-----------------------------------------------------------------------------
    //
    //  get_Approximate_IDD_Time()
    //
    //! \brief   Description:  Gets an approximate time for how long a specific IDD operation may take
    //
    //  Entry:
    //!   \param[in] device = file descriptor
    //!   \param[in] iddTest = enum value describing the IDD test to get the time for
    //!   \param[in] timeInSeconds = pointer to a uint64_t that will hold the amount of time in seconds that IDD is estimated to take
    //!
    //  Exit:
    //!   \return SUCCESS on successful completion, FAILURE = fail, NOT_SUPPORTED = IDD not supported
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API eReturnValues get_Approximate_IDD_Time(tDevice *device, eIDDTests iddTest, uint64_t *timeInSeconds);

    //-----------------------------------------------------------------------------
    //
    //  run_IDD()
    //
    //! \brief   Description:  Function to send a Seagate IDD test to a device
    //
    //  Entry:
    //!   \param[in] device = file descriptor
    //!   \param[in] IDDtest = enum value describing the IDD test to run (CAPTIVE not supported right now)
    //!   \param[in] pollForProgress = 0 = don't poll, just start the test. 1 = poll for progress and display the progress on the screen.
    //!   \param[in] captive = set to true to force running the test in captive mode. Long test only!
    //!
    //  Exit:
    //!   \return SUCCESS on successful completion, FAILURE = fail
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API eReturnValues run_IDD(tDevice *device, eIDDTests IDDtest, bool pollForProgress, bool captive);

    //-----------------------------------------------------------------------------
    //
    //  get_IDD_Status(tDevice *device, uint8_t *status)
    //
    //! \brief   Description:  Gets the status of an ongoing IDD operation
    //
    //  Entry:
    //!   \param[in] device = file descriptor
    //!   \param[out] status = returns a status value for an ongoing IDD operation. This is similar to a DST status code value.
    //!
    //  Exit:
    //!   \return SUCCESS on successful completion, FAILURE = fail
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API eReturnValues get_IDD_Status(tDevice *device, uint8_t *status);

    //-----------------------------------------------------------------------------
    //
    //  gis_Seagate_Power_Telemetry_Feature_Supported(tDevice *device)
    //
    //! \brief   Description:  Checks if the Seagate power telemetry feature is supported or not
    //
    //  Entry:
    //!   \param[in] device = file descriptor
    //!
    //  Exit:
    //!   \return true = supported, false = not supported
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API bool is_Seagate_Power_Telemetry_Feature_Supported(tDevice *device);

    //NOTE: While these 2 structures are common, keep them in this layer since they are meant to be read into for an operation.
    //Putting these in opensea-transport may confuse users into thinking a memcpy can be done to use them, but these are modified to make things easier for operations
    typedef struct _seagatePwrTelemetryMeasurement
    {
        uint16_t fiveVoltMilliWatts;
        uint16_t twelveVoltMilliWatts;
        uint16_t reserved;
    }seagatePwrTelemetryMeasurement;

    //NOTE: This structure should not be used for memcpy or pointing over a data buffer!
    //      This will be filled in by parsing the return data from the drive to make it easily usable in a utility.
    typedef struct _seagatePwrTelemetry
    {
        bool multipleLogicalUnits;
        char serialNumber[9]; //including M_NULLPTR terminator.
        uint16_t powerCycleCount;
        uint64_t driveTimeStampForHostRequestedMeasurement;//in microseconds //should this be a double???
        uint64_t driveTimeStampWhenTheLogWasRetrieved;//in microseconds //should this be a double???
        uint8_t majorRevision;
        uint8_t minorRevision;
        char signature[9]; //including null terminator
        uint16_t totalMeasurementTimeRequested;//seconds
        uint16_t numberOfMeasurements;//default = 1024
        uint8_t measurementFormat;
        uint8_t temperatureCelcius;
        uint16_t measurementWindowTimeMilliseconds;
        seagatePwrTelemetryMeasurement measurement[POWER_TELEMETRY_MAXIMUM_MEASUREMENTS];
    }seagatePwrTelemetry, *ptrSeagatePwrTelemetry;

    //-----------------------------------------------------------------------------
    //
    //  get_Power_Telemetry_Data(tDevice *device, ptrSeagatePwrTelemetry pwrTelData)
    //
    //! \brief   Description:  Gets the power telemetry data into a structure that can be used to display the data
    //
    //  Entry:
    //!   \param[in] device = file descriptor
    //!   \param[out] pwrTelData = must be allocated before calling. Will be filled with power telemetry data upon success
    //!
    //  Exit:
    //!   \return SUCCESS on successful completion, FAILURE = fail
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API eReturnValues get_Power_Telemetry_Data(tDevice *device, ptrSeagatePwrTelemetry pwrTelData);

    //-----------------------------------------------------------------------------
    //
    //  show_Power_Telemetry_Data(ptrSeagatePwrTelemetry pwrTelData)
    //
    //! \brief   Description:  Shows the power telemetry data on the screen
    //
    //  Entry:
    //!   \param[in] pwrTelData = pointer to power telemetry data already retrieved from the drive.
    //!
    //  Exit:
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API void show_Power_Telemetry_Data(ptrSeagatePwrTelemetry pwrTelData);

    //-----------------------------------------------------------------------------
    //
    //  request_Power_Measurement(tDevice *device, uint16_t timeMeasurementSeconds, ePowerTelemetryMeasurementOptions measurementOption)
    //
    //! \brief   Description: Sends a power measurement request to the drive.
    //
    //  Entry:
    //!   \param[in] device = file descriptor
    //!   \param[in] timeMeasurementSeconds = amount of time to measure for
    //!   \param[in] measurementOption = set to measure 5v, 12v, or both
    //!
    //  Exit:
    //!   \return SUCCESS on successful completion, FAILURE = fail
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API eReturnValues request_Power_Measurement(tDevice *device, uint16_t timeMeasurementSeconds, ePowerTelemetryMeasurementOptions measurementOption);

    //-----------------------------------------------------------------------------
    //
    //  pull_Power_Telemetry_Log(tDevice *device, const char * const filePath, uint32_t transferSizeBytes)
    //
    //! \brief   Description:  Pulls the power telemetry data to a binary file
    //
    //  Entry:
    //!   \param[in] device = file descriptor
    //!   \param[in] filePath = pointer to the path where this log should be generated. Use M_NULLPTR for current working directory.
    //!   \param[in] transferSizeBytes = OPTIONAL. If set to zero, this is ignored. Should be rounded to 512B for ATA devices
    //!
    //  Exit:
    //!   \return VOID
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API eReturnValues pull_Power_Telemetry_Log(tDevice *device, const char * const filePath, uint32_t transferSizeBytes);

    //-----------------------------------------------------------------------------
    //
    //  translate_IDD_Status_To_String(uint8_t status, char *translatedString, bool justRanDST)
    //
    //! \brief   Description: Translates the status code from IDD to a string. NOTE: This is basically the same as the DST translation. This may be able to be improved in the future.
    //
    //  Entry:
    //!   \param[in] status = status code from last run IDD (from log or elsewhere)
    //!   \param[out] translatedString = pointer to memory to set the information string about IDD that needs to be printed
    //!   \param[in] justRanDST = set to true if IDD just finished running. Set to false if not known
    //!
    //  Exit:
    //!   \return VOID
    //
    //-----------------------------------------------------------------------------
    #define MAX_IDD_STATUS_STRING_LENGTH 160
    OPENSEA_OPERATIONS_API void translate_IDD_Status_To_String(uint8_t status, char *translatedString, bool justRanDST);

    //-----------------------------------------------------------------------------
    //
    //  is_Seagate_Quick_Format_Supported(tDevice *device)
    //
    //! \brief   Description:  This function checks if the Seagate SATA quick format command is supported
    //
    //  Entry:
    //!   \param device - pointer to the device structure.
    //!
    //  Exit:
    //!   \return true = supported, false = not supported
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API bool is_Seagate_Quick_Format_Supported(tDevice *device);

    //-----------------------------------------------------------------------------
    //
    //  seagate_Quick_Format(tDevice *device)
    //
    //! \brief   Description:  This function issues the Seagate SATA quick format command. This is a captive operation, so you must wait for it to complete, no matter how long it takes, but should be a couple minutes at most
    //
    //  Entry:
    //!   \param device - pointer to the device structure.
    //!
    //  Exit:
    //!   \return SUCCESS on successful completion, FAILURE = fail, NOT_SUPPORTED not supported on this device. This only happens on non-sata right now. - TJE
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API eReturnValues seagate_Quick_Format(tDevice *device);

    OPENSEA_OPERATIONS_API eReturnValues clr_Pcie_Correctable_Errs(tDevice *device);
    //-----------------------------------------------------------------------------
    //
    //  nvme_Get_Ext_Smrt_Log_Page
    //
    //! \brief   Description:  Function to send Get Extended SMART Information Log Page NVMe command to a device
    //
    //  Entry:
    //!   \param[in] device = pointer to tDevice structure
    //!   \param[in] nsid = Namespace ID for the namespace of 0xFFFFFFFF for entire controller. 
    //!   \param[out] pData = Data buffer (suppose to be 512 bytes)
    //!   \param[in] dataLen = Data buffer Length
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API eReturnValues get_Ext_Smrt_Log(tDevice *device);

    OPENSEA_OPERATIONS_API void print_smart_log(uint16_t  verNo, SmartVendorSpecific attr, int lastAttr);
    OPENSEA_OPERATIONS_API uint64_t smart_attribute_vs(uint16_t  verNo, SmartVendorSpecific attr);
    OPENSEA_OPERATIONS_API const char* print_ext_smart_id(uint8_t  attrId);

    OPENSEA_OPERATIONS_API void print_smart_log_CF(fb_log_page_CF *pLogPageCF);

    typedef struct _seagateStatistic
    {
        uint32_t statisticsDataValue;
        bool isTimeStampsInMinutes;
        uint8_t failureInfo;
        bool isSupported;
        bool isValueValid;
        bool isNormalized;
    }seagateStatistic;

    typedef struct _seagateSataDeviceStatistics
    {
        uint8_t version;
        seagateStatistic sanitizeCryptoErasePassCount;
        seagateStatistic sanitizeCryptoErasePassTimeStamp;
        seagateStatistic sanitizeOverwriteErasePassCount;
        seagateStatistic sanitizeOverwriteErasePassTimeStamp;
        seagateStatistic sanitizeBlockErasePassCount;
        seagateStatistic sanitizeBlockErasePassTimeStamp;
        seagateStatistic ataSecurityEraseUnitPassCount;
        seagateStatistic ataSecurityEraseUnitPassTimeStamp;
        seagateStatistic eraseSecurityFileFailureCount;
        seagateStatistic eraseSecurityFileFailureTimeStamp;
        seagateStatistic ataSecurityEraseUnitEnhancedPassCount;
        seagateStatistic ataSecurityEraseUnitEnhancedPassTimeStamp;
        seagateStatistic sanitizeCryptoEraseFailCount;
        seagateStatistic sanitizeCryptoEraseFailTimeStamp;
        seagateStatistic sanitizeOverwriteEraseFailCount;
        seagateStatistic sanitizeOverwriteEraseFailTimeStamp;
        seagateStatistic sanitizeBlockEraseFailCount;
        seagateStatistic sanitizeBlockEraseFailTimeStamp;
        seagateStatistic ataSecurityEraseUnitFailCount;
        seagateStatistic ataSecurityEraseUnitFailTimeStamp;
        seagateStatistic ataSecurityEraseUnitEnhancedFailCount;
        seagateStatistic ataSecurityEraseUnitEnhancedFailTimeStamp;
    }seagateSataDeviceStatistics;

    typedef struct _seagateSasDeviceStatistics
    {
        seagateStatistic sanitizeCryptoEraseCount;
        seagateStatistic sanitizeCryptoEraseTimeStamp;
        seagateStatistic sanitizeOverwriteEraseCount;
        seagateStatistic sanitizeOverwriteEraseTimeStamp;
        seagateStatistic sanitizeBlockEraseCount;
        seagateStatistic sanitizeBlockEraseTimeStamp;
        seagateStatistic eraseSecurityFileFailureCount;
        seagateStatistic eraseSecurityFileFailureTimeStamp;
    }seagateSasDeviceStatistics;

    //access the proper stats in the union based on device->drive_info.drive_type
    typedef struct _seagateDeviceStatistics
    {
        union
        {
            seagateSataDeviceStatistics sataStatistics;
            seagateSasDeviceStatistics sasStatistics;
        };
    }seagateDeviceStatistics, *ptrSeagateDeviceStatistics;

    OPENSEA_OPERATIONS_API bool is_Seagate_DeviceStatistics_Supported(tDevice *device);

    OPENSEA_OPERATIONS_API eReturnValues get_Seagate_DeviceStatistics(tDevice *device, ptrSeagateDeviceStatistics seagateDeviceStats);

    OPENSEA_OPERATIONS_API void print_Seagate_DeviceStatistics(tDevice *device, ptrSeagateDeviceStatistics seagateDeviceStats);

    #define FIRMWARE_RELEASE_NUM_LEN 8
    #define SERVO_FIRMWARE_RELEASE_NUM_LEN 8
    #define SAP_BP_NUM_LEN 8
    #define SERVO_FW_RELEASE_DATE_LEN 4
    #define SERVO_ROM_RELEASE_DATE_LEN 4
    #define SAP_FW_RELEASE_NUM_LEN 8
    #define SAP_FW_RELEASE_DATE_LEN 4
    #define SAP_FW_RELEASE_YEAR_LEN 4
    #define SAP_MANUFACTURING_KEY_LEN 4
    #define SERVO_PRODUCT_FAMILY_LEN 4

    typedef struct _seagateSCSIFWNumbers
    {
        char scsiFirmwareReleaseNumber[FIRMWARE_RELEASE_NUM_LEN + 1];
        char servoFirmwareReleaseNumber[SERVO_FIRMWARE_RELEASE_NUM_LEN + 1];
        char sapBlockPointNumbers[SAP_BP_NUM_LEN + 1];
        char servoFirmmwareReleaseDate[SERVO_FW_RELEASE_DATE_LEN + 1];
        char servoRomReleaseDate[SERVO_ROM_RELEASE_DATE_LEN + 1];
        char sapFirmwareReleaseNumber[SAP_FW_RELEASE_NUM_LEN + 1];
        char sapFirmwareReleaseDate[SAP_FW_RELEASE_DATE_LEN + 1];
        char sapFirmwareReleaseYear[SAP_FW_RELEASE_YEAR_LEN + 1];
        char sapManufacturingKey[SAP_MANUFACTURING_KEY_LEN + 1];
        char servoFirmwareProductFamilyAndProductFamilyMemberIDs[SERVO_PRODUCT_FAMILY_LEN + 1];
    }seagateSCSIFWNumbers, *ptrSeagateSCSIFWNumbers;

    //This is defined in the Seagate SCSI commands reference manual available on the web
    OPENSEA_OPERATIONS_API eReturnValues get_Seagate_SCSI_Firmware_Numbers(tDevice* device, ptrSeagateSCSIFWNumbers fwNumbers);

#if defined (__cplusplus)
}
#endif
