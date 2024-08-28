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
#pragma once

#include "operations_Common.h"
#include "ata_helper.h"

#if defined (__cplusplus)
extern "C"
{
#endif


    #include <time.h>

    //-----------------------------------------------------------------------------
    //
    //  get_Ready_LED_State(tDevice *device, bool *readyLEDOnOff)
    //
    //! \brief   Get the current Ready LED behavior on SAS drives.
    //
    //  Entry:
    //!   \param device - file descriptor
    //!   \param readyLEDOnOff - when set to true, the ready LED bit is set to a 1, other wise set to a 0. See the SAS protocol spec for details on this mode page.
    //!
    //  Exit:
    //!   \return SUCCESS = good, !SUCCESS something went wrong see error codes
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API eReturnValues get_Ready_LED_State(tDevice *device, bool *readyLEDOnOff);

    //-----------------------------------------------------------------------------
    //
    //  change_Ready_LED( tDevice * device )
    //
    //! \brief   Change Ready LED behavior on SAS drives. SAS is configurable with a command, SATA is not so SAS is the only thing supported in this call.
    //
    //  Entry:
    //!   \param device - file descriptor
    //!   \param readyLEDDefault - set to true to restore the drive's default ready LED behavior
    //!   \param readyLEDOnOff - when set to true, set the ready LED bit to a 1, other wise set to a 0. See the SAS protocol spec for details on this mode page.
    //!
    //  Exit:
    //!   \return SUCCESS = good, !SUCCESS something went wrong see error codes
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API eReturnValues change_Ready_LED(tDevice *device, bool readyLEDDefault, bool readyLEDOnOff);


    //-----------------------------------------------------------------------------
    //
    //  scsi_is_NV_DIS_Bit_Set( tDevice * device )
    //
    //! \brief   get whether NV_DIS bit in the SCSI Caching mode page is set or not
    //
    //  Entry:
    //!   \param device - file descriptor
    //!
    //  Exit:
    //!   \return true = enabled, false = disabled
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API bool scsi_is_NV_DIS_Bit_Set(tDevice *device);

    OPENSEA_OPERATIONS_API bool scsi_Is_NV_Cache_Supported(tDevice *device);

    OPENSEA_OPERATIONS_API bool is_NV_Cache_Supported(tDevice *device);

    OPENSEA_OPERATIONS_API bool is_NV_Cache_Enabled(tDevice *device);

    //-----------------------------------------------------------------------------
    //
    //  scsi_Set_NV_DIS( tDevice * device, bool nv_disEnableDisable)
    //
    //! \brief   Set the SCSI NV_DIS bit using scsi commands (Caching Mode Page, SBC). setting enableDisable to true turns the NV cache ON (NV_DIS = 0), false turns the cache off (NV_DIS = 1)
    //
    //  Entry:
    //!   \param device - file descriptor
    //!   \param nv_disEnableDisable - set to true to enable the NV Cache. False to disable the NV cache
    //!
    //  Exit:
    //!   \return SUCCESS = good, !SUCCESS something went wrong see error codes
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API eReturnValues scsi_Set_NV_DIS(tDevice *device, bool nv_disEnableDisable);

    //-----------------------------------------------------------------------------
    //
    //  set_Read_Look_Ahead( tDevice * device )
    //
    //! \brief   set read look-ahead to enabled or disabled.
    //
    //  Entry:
    //!   \param device - file descriptor
    //!   \param readLookAheadEnableDisable - set to true to enable read look-ahead. Set to false to disable
    //!
    //  Exit:
    //!   \return SUCCESS = good, !SUCCESS something went wrong see error codes
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API eReturnValues set_Read_Look_Ahead(tDevice *device, bool readLookAheadEnableDisable);

    //-----------------------------------------------------------------------------
    //
    //  scsi_Set_Read_Look_Ahead( tDevice * device )
    //
    //! \brief   set read look-ahead to enabled or disabled using scsi commands
    //
    //  Entry:
    //!   \param device - file descriptor
    //!   \param readLookAheadEnableDisable - set to true to enable read look-ahead. Set to false to disable
    //!
    //  Exit:
    //!   \return SUCCESS = good, !SUCCESS something went wrong see error codes
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API eReturnValues scsi_Set_Read_Look_Ahead(tDevice *device, bool readLookAheadEnableDisable);

    //-----------------------------------------------------------------------------
    //
    //  ata_Set_Read_Look_Ahead( tDevice * device )
    //
    //! \brief   set read look-ahead to enabled or disabled using ata commands
    //
    //  Entry:
    //!   \param device - file descriptor
    //!   \param readLookAheadEnableDisable - set to true to enable read look-ahead. Set to false to disable
    //!
    //  Exit:
    //!   \return SUCCESS = good, !SUCCESS something went wrong see error codes
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API eReturnValues ata_Set_Read_Look_Ahead(tDevice *device, bool readLookAheadEnableDisable);

    //-----------------------------------------------------------------------------
    //
    //  set_Write_Cache( tDevice * device )
    //
    //! \brief   set write cache to enabled or disabled.
    //
    //  Entry:
    //!   \param device - file descriptor
    //!   \param writeCacheEnableDisable - set to true to enable write cache. Set to false to disable
    //!
    //  Exit:
    //!   \return SUCCESS = good, !SUCCESS something went wrong see error codes
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API eReturnValues set_Write_Cache(tDevice *device, bool writeCacheEnableDisable);

    //-----------------------------------------------------------------------------
    //
    //  scsi_Set_Write_Cache( tDevice * device )
    //
    //! \brief   set write cache to enabled or disabled using scsi commands
    //
    //  Entry:
    //!   \param device - file descriptor
    //!   \param writeCacheEnableDisable - set to true to enable write cache. Set to false to disable
    //!
    //  Exit:
    //!   \return SUCCESS = good, !SUCCESS something went wrong see error codes
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API eReturnValues scsi_Set_Write_Cache(tDevice *device, bool writeCacheEnableDisable);

    //-----------------------------------------------------------------------------
    //
    //  ata_Set_Write_Cache( tDevice * device )
    //
    //! \brief   set write cache to enabled or disabled using ata commands
    //
    //  Entry:
    //!   \param device - file descriptor
    //!   \param writeCacheEnableDisable - set to true to enable write cache. Set to false to disable
    //!
    //  Exit:
    //!   \return SUCCESS = good, !SUCCESS something went wrong see error codes
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API eReturnValues ata_Set_Write_Cache(tDevice *device, bool writeCacheEnableDisable);

    OPENSEA_OPERATIONS_API eReturnValues nvme_Set_Write_Cache(tDevice *device, bool writeCacheEnableDisable);

    //-----------------------------------------------------------------------------
    //
    //  is_Read_Look_Ahead_Enabled( tDevice * device )
    //
    //! \brief   get whether read look ahead is currently enabled or not.
    //
    //  Entry:
    //!   \param device - file descriptor
    //!
    //  Exit:
    //!   \return true = enabled, false = disabled
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API bool is_Read_Look_Ahead_Enabled(tDevice *device);

    OPENSEA_OPERATIONS_API bool is_Read_Look_Ahead_Supported(tDevice *device);

    //-----------------------------------------------------------------------------
    //
    //  scsi_Is_Read_Look_Ahead_Enabled( tDevice * device )
    //
    //! \brief   get whether read look ahead is currently enabled or not from scsi caching mode page
    //
    //  Entry:
    //!   \param device - file descriptor
    //!
    //  Exit:
    //!   \return true = enabled, false = disabled
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API bool scsi_Is_Read_Look_Ahead_Enabled(tDevice *device);

    OPENSEA_OPERATIONS_API bool scsi_Is_Read_Look_Ahead_Supported(tDevice *device);

    //-----------------------------------------------------------------------------
    //
    //  ata_Is_Read_Look_Ahead_Enabled( tDevice * device )
    //
    //! \brief   get whether read look ahead is currently enabled or not from ata identify information
    //
    //  Entry:
    //!   \param device - file descriptor
    //!
    //  Exit:
    //!   \return true = enabled, false = disabled
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API bool ata_Is_Read_Look_Ahead_Enabled(tDevice *device);

    OPENSEA_OPERATIONS_API bool ata_Is_Read_Look_Ahead_Supported(tDevice *device);

    OPENSEA_OPERATIONS_API bool nvme_Is_Write_Cache_Enabled(tDevice *device);

    OPENSEA_OPERATIONS_API bool nvme_Is_Write_Cache_Supported(tDevice *device);

    //-----------------------------------------------------------------------------
    //
    //  is_Write_Cache_Enabled( tDevice * device )
    //
    //! \brief   get whether write caching is currently enabled or not.
    //
    //  Entry:
    //!   \param device - file descriptor
    //!
    //  Exit:
    //!   \return true = enabled, false = disabled
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API bool is_Write_Cache_Enabled(tDevice *device);

    OPENSEA_OPERATIONS_API bool is_Write_Cache_Supported(tDevice *device);

    //-----------------------------------------------------------------------------
    //
    //  scsi_Is_Read_Look_Ahead_Enabled( tDevice * device )
    //
    //! \brief   get whether read look ahead is currently enabled or not from scsi caching mode page
    //
    //  Entry:
    //!   \param device - file descriptor
    //!
    //  Exit:
    //!   \return true = enabled, false = disabled
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API bool scsi_Is_Write_Cache_Enabled(tDevice *device);

    OPENSEA_OPERATIONS_API bool scsi_Is_Write_Cache_Supported(tDevice *device);

    //-----------------------------------------------------------------------------
    //
    //  ata_Is_Write_Cache_Enabled( tDevice * device )
    //
    //! \brief   get whether read look ahead is currently enabled or not from ata identify information
    //
    //  Entry:
    //!   \param device - file descriptor
    //!
    //  Exit:
    //!   \return true = enabled, false = disabled
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API bool ata_Is_Write_Cache_Enabled(tDevice *device);

    OPENSEA_OPERATIONS_API bool ata_Is_Write_Cache_Supported(tDevice *device);

    typedef enum _eEraseMethod
    {
        ERASE_NOT_SUPPORTED,
        ERASE_OVERWRITE,
        ERASE_WRITE_SAME,
        ERASE_ATA_SECURITY_NORMAL,
        ERASE_ATA_SECURITY_ENHANCED,
        ERASE_SANITIZE_CRYPTO,
        ERASE_SANITIZE_BLOCK,
        ERASE_SANITIZE_OVERWRITE,
        ERASE_OBSOLETE, //This was previously for Trim/unmap, but has been removed since these don't guarantee data erasure. They are more of "hints" which may or may not cause erasure.
        ERASE_TCG_REVERT_SP, //will be use in tcg operations lib, not operations lib
        ERASE_TCG_REVERT, //will be use in tcg operations lib, not operations lib
        ERASE_FORMAT_UNIT,
        ERASE_NVM_FORMAT_USER_SECURE_ERASE,
        ERASE_NVM_FORMAT_CRYPTO_SECURE_ERASE,
        ERASE_MAX_VALUE = -1
    }eEraseMethod;

    #define MAX_SUPPORTED_ERASE_METHODS 13
    #define MAX_ERASE_NAME_LENGTH 30
    #define MAX_ERASE_WARNING_LENGTH 80

    //This is based on IEEE 2883 and assumes the device firmware is compliant according to the specifications
    typedef enum _eraseSanitizationLevel
    {
        ERASE_SANITIZATION_UNKNOWN,
        ERASE_SANITIZATION_CLEAR,//any erase that can go fro 0 - max user addressable sector
        ERASE_SANITIZATION_POSSIBLE_PURGE,//special case for NVMe format since it is labelled as vendor unique if it qualifies as a purge command.
        ERASE_SANITIZATION_PURGE //an erase that can erase 0-max user addressable sector and any reallocated/reserved sectors and erase any sectors that can be made addressable that are not currently addressable.
    }eraseSanitizationLevel;

    typedef struct _eraseMethod
    {
        eEraseMethod eraseIdentifier;
        char eraseName[MAX_ERASE_NAME_LENGTH];
        bool warningValid;
        char eraseWarning[MAX_ERASE_WARNING_LENGTH];//may be an empty string. May contain something like "requires password" or "cannot be stopped"
        uint8_t eraseWeight;//used to store how fast/slow it is...used for sorting from fastest to slowest
        eraseSanitizationLevel sanitizationLevel;//What does the given erase type comply with as far as IEEE 2883 specification mentions.
    }eraseMethod;

    //-----------------------------------------------------------------------------
    //
    //  get_Supported_Erase_Methods(tDevice *device, eraseMethod eraseMethodList[MAX_SUPPORTED_ERASE_METHODS])
    //
    //! \brief   Gets a list of the supported erase functions on a drive. list must be at least MAX_SUPPORTED_ERASE_METHODS in size. The list will be in order from fastest to slowest.
    //!          There is also a TCG version of this function in tcg_base_operations.h that will fill in the list including support for revert and revertSP if the drive supports these methods
    //
    //  Entry:
    //!   \param device - file descriptor
    //!   \param eraseMethodList - list of erase methods to be filled in. Must be at least MAX_SUPPORTED_ERASE_METHODS in size.
    //!   \param overwriteEraseTimeEstimateMinutes - a time estimate in minutes for an overwrite erase to complete on a drive (whole drive). (optional)
    //!
    //  Exit:
    //!   \return SUCCESS = successfully determined erase support, anything else = some error occured while determining support.
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API eReturnValues get_Supported_Erase_Methods(tDevice *device, eraseMethod eraseMethodList[MAX_SUPPORTED_ERASE_METHODS], uint32_t *overwriteEraseTimeEstimateMinutes);

    //-----------------------------------------------------------------------------
    //
    //  print_Supported_Erase_Methods(tDevice *device, eraseMethod const eraseMethodList[MAX_SUPPORTED_ERASE_METHODS])
    //
    //! \brief   Prints out the list of supported erase methods to the screen from fastest to slowest
    //
    //  Entry:
    //!   \param device - file descriptor
    //!   \param eraseMethodList - list of erase methods to be filled in. Must be at least MAX_SUPPORTED_ERASE_METHODS in size.
    //!   \param overwriteEraseTimeEstimateMinutes - a time estimate in minutes for an overwrite erase to complete on a drive (whole drive). (optional)
    //!
    //  Exit:
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API void print_Supported_Erase_Methods(tDevice *device, eraseMethod const eraseMethodList[MAX_SUPPORTED_ERASE_METHODS], uint32_t *overwriteEraseTimeEstimateMinutes);

    //-----------------------------------------------------------------------------
    //
    //  set_Sense_Data_Format(tDevice *device, bool defaultSetting, bool descriptorFormat, bool saveParameters)
    //
    //! \brief   Set the default sense data format.
    //
    //  Entry:
    //!   \param device - file descriptor
    //!   \param defaultSetting - this reads the default mode page and set's the device's default mode
    //!   \param descriptorFormat - set to true to set descriptor format sense data. set to false to set fixed format sense data
    //!   \param saveParameters - set to true to send the mode select command requesting to save the parameters. This may not be available if SAT
    //!
    //  Exit:
    //!   \return SUCCESS = successfully determined erase support, anything else = some error occured while determining support.
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API eReturnValues set_Sense_Data_Format(tDevice *device, bool defaultSetting, bool descriptorFormat, bool saveParameters);

    OPENSEA_OPERATIONS_API eReturnValues get_Current_Free_Fall_Control_Sensitivity(tDevice * device, uint16_t *sensitivity);//if sensitivity is set to UINT16_MAX, then the feature is supported, but not enabled, so the value wouldn't otherwise make sense

    OPENSEA_OPERATIONS_API eReturnValues set_Free_Fall_Control_Sensitivity(tDevice *device, uint8_t sensitivity);//enables the feature. Value of zero sets a vendor's recommended setting

    OPENSEA_OPERATIONS_API eReturnValues disable_Free_Fall_Control_Feature(tDevice *device);//disables the free fall control feature

    OPENSEA_OPERATIONS_API void show_Test_Unit_Ready_Status(tDevice *device);

    OPENSEA_OPERATIONS_API eReturnValues enable_Disable_AAM_Feature(tDevice *device, bool enable);

    OPENSEA_OPERATIONS_API eReturnValues set_AAM_Level(tDevice *device, uint8_t apmLevel);

    OPENSEA_OPERATIONS_API eReturnValues get_AAM_Level(tDevice *device, uint8_t *apmLevel);

    OPENSEA_OPERATIONS_API bool scsi_MP_Reset_To_Defaults_Supported(tDevice *device);//This is the reset to defaults bit in mode select command. Not anything else. If this is false, the old read the defaults and write it back should still work - TJE

    typedef enum _eSCSI_MP_UPDATE_MODE
    {
        UPDATE_SCSI_MP_RESET_TO_DEFAULT,
        UPDATE_SCSI_MP_RESTORE_TO_SAVED,
        UPDATE_SCSI_MP_SAVE_CURRENT
    }eSCSI_MP_UPDATE_MODE;

    OPENSEA_OPERATIONS_API eReturnValues scsi_Update_Mode_Page(tDevice *device, uint8_t modePage, uint8_t subpage, eSCSI_MP_UPDATE_MODE updateMode);

    OPENSEA_OPERATIONS_API void show_SCSI_Mode_Page(tDevice * device, uint8_t modePage, uint8_t subpage, eScsiModePageControl mpc, bool bufferFormatOutput);

    OPENSEA_OPERATIONS_API void show_SCSI_Mode_Page_All(tDevice * device, uint8_t modePage, uint8_t subpage, bool bufferFormatOutput);

    //Should this go into a different file???
    //NOTE: This rely's on NOT having the mode page header in the passed in buffer, just the raw mode page itself!
    OPENSEA_OPERATIONS_API eReturnValues scsi_Set_Mode_Page(tDevice *device, uint8_t* modePageData, uint16_t modeDataLength, bool saveChanges);//takes a byte array and sends it to the drive.

    //NOTE: SPC4 and higher is required to reset only a specific page. Prior to that, all pages will be reset (logpage and logSubPage both set to zero)
    //This function will return BAD_PARAMETER if the device does not support resetting a specific page (logpage or subpage not equal to zero)
    OPENSEA_OPERATIONS_API eReturnValues reset_SCSI_Log_Page(tDevice *device, eScsiLogPageControl pageControl, uint8_t logPage, uint8_t logSubPage, bool saveChanges);

    //The following functions are for help with devices that contain multiple logical units (actuators, for example).
    //These commands are intended to help inform users when certain things may affect multiple LUs.
    //Some commands that may affect more than one logical unit are:
    // -write buffer (download firmware), read buffer may also affect multiple depending on mode
    // -start-stop unit
    // -format unit
    // -remove element and truncate
    // -sanitize
    // -send diagnostic/receive diagnostic
    //Some mode pages that may affect more than one logical unit are:
    // -caching
    // -powerConditions
    //NOTE: some log pages may also share data for multiple logical units, like power transitions or cache memory statistics

    OPENSEA_OPERATIONS_API uint8_t get_LUN_Count(tDevice *device);

    typedef enum _eMLU
    {
        MLU_NOT_REPORTED = 0,
        MLU_AFFECTS_ONLY_THIS_UNIT = 1,
        MLU_AFFECTS_MULTIPLE_LU = 2,
        MLU_AFFECTS_ALL_LU = 3
    }eMLU;

    OPENSEA_OPERATIONS_API eMLU get_MLU_Value_For_SCSI_Operation(tDevice *device, uint8_t operationCode, uint16_t serviceAction);

    //If true, then the specified mode page affects multiple logical units, otherwise it is not reported whether multiple are affected or not.
    OPENSEA_OPERATIONS_API bool scsi_Mode_Pages_Shared_By_Multiple_Logical_Units(tDevice *device, uint8_t modePage, uint8_t subPage);

    #define CONCURRENT_RANGES_VERSION 1

    typedef struct _concurrentRangeDescription
    {
        uint8_t rangeNumber;
        uint8_t numberOfStorageElements;//if zero, then this is not reported by the device
        uint64_t lowestLBA;
        uint64_t numberOfLBAs;
    }concurrentRangeDescription;

    typedef struct _concurrentRanges
    {
        size_t size;
        uint32_t version;
        uint8_t numberOfRanges;
        concurrentRangeDescription range[15];//maximum of 15 concurrent ranges per ACS5
    }concurrentRanges, *ptrConcurrentRanges;

    //-----------------------------------------------------------------------------
    //
    //  get_Concurrent_Positioning_Ranges(tDevice *device, ptrConcurrentRanges ranges)
    //
    //! \brief   Use this to read the concurrent positioing ranges (actuator info) from a SAS or SATA drive. 
    //
    //  Entry:
    //!   \param device - file descriptor
    //!   \param ranges - pointer to a structure to hold the concurrent positioning information. This should have the size and version set before this function is called.
    //!
    //  Exit:
    //!   \return SUCCESS = successfully read concurrent positioning data, BAD_PARAMETER = invalid structure size or version or other input error, anything else = some error occured while determining support.
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API eReturnValues get_Concurrent_Positioning_Ranges(tDevice *device, ptrConcurrentRanges ranges);

    //-----------------------------------------------------------------------------
    //
    //  print_Concurrent_Positioning_Ranges(ptrConcurrentRanges ranges)
    //
    //! \brief   Use this to print the concurrent positioing ranges (actuator info) from a SAS or SATA drive to the screen (stdout)
    //
    //  Entry:
    //!   \param ranges - pointer to a structure filled in with the concurrent positioning information from a device.
    //!
    //  Exit:
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API void print_Concurrent_Positioning_Ranges(ptrConcurrentRanges ranges);

    typedef struct _wrvInfo
    {
        bool supported;//if the write-read-verify feature even supported. This must be true for any further data in this structure to be valid
        bool enabled;
        uint8_t currentWRVMode;//only valid if the feature is enabled.
        uint64_t bytesBeingVerified;//if set to UINT64_MAX, then all bytes are being verified. Otherwise it will match the mode * current logical sector size.-TJE
        uint32_t wrv2sectorCount;//vendor specific. Technically only valid if enabled to mode 2
        uint32_t wrv3sectorCount;//user defined. Technocally only valid if enabled to mode 3
        //Output number of bytes that are verified in addition to sectors???
    }wrvInfo, *ptrWRVInfo;

    //-----------------------------------------------------------------------------
    //
    //  get_Write_Read_Verify_Info(tDevice* device, ptrWRVInfo info)
    //
    //! \brief   This reads the current settings associated with an ATA drive's write-read-verify feature
    //
    //  Entry:
    //!   \param device - pointer to the tdevice structure for the drive to retrieve information from
    //!   \param ranges - pointer to a structure filled in with the write-read-verify info
    //!
    //  Exit:
    //!   \return SUCCESS = successfully read write-read-verify data, NOTE_SUPPORTED = feature not supported by the device, BAD_PARAMETER = invalid structure size or version or other input error, anything else = some error occured while determining support.
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API eReturnValues get_Write_Read_Verify_Info(tDevice* device, ptrWRVInfo info);

    //-----------------------------------------------------------------------------
    //
    //  print_Write_Read_Verify_Info(ptrWRVInfo info);
    //
    //! \brief   Use this to print the write-read-verify info from a SATA drive in human readable format to the screen
    //
    //  Entry:
    //!   \param ranges - pointer to a structure filled with the write-read-verify information from a device.
    //!
    //  Exit:
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API void print_Write_Read_Verify_Info(ptrWRVInfo info);

    //-----------------------------------------------------------------------------
    //
    //  disable_Write_Read_Verify(tDevice* device)
    //
    //! \brief   Disable the write-read-verify feature on an ATA device
    //
    //  Entry:
    //!   \param device - pointer to the tdevice structure for the drive to retrieve information from
    //!
    //  Exit:
    //!   \return SUCCESS = successfully disabled write-read-verify, NOTE_SUPPORTED = feature not supported by the device, BAD_PARAMETER = invalid structure size or version or other input error, anything else = some error occured while determining support.
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API eReturnValues disable_Write_Read_Verify(tDevice* device);

    //-----------------------------------------------------------------------------
    //
    //  set_Write_Read_Verify(tDevice* device, bool all, bool vendorSpecific, uint32_t wrvSectorCount)
    //
    //! \brief   Enable the write-read-verify feature on an ATA device to a specific mode
    //
    //  Entry:
    //!   \param device - pointer to the tdevice structure for the drive to retrieve information from
    //!   \param all - set write-read-verify to verify writes to all LBAs (cannot be used with vendor or wrvSectorCount)
    //!   \param vendor - set the vendor specific write-read-verify mode (cannot be used with all or wrvSectorCount)
    //!   \param wrvSectorCount - if all and vendor are false, this specifies the number of sectors to wrv
    //!
    //  Exit:
    //!   \return SUCCESS = successfully enabled write-read-verify with provided parameters, NOTE_SUPPORTED = feature not supported by the device, BAD_PARAMETER = invalid structure size or version or other input error, anything else = some error occured while determining support.
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API eReturnValues set_Write_Read_Verify(tDevice* device, bool all, bool vendorSpecific, uint32_t wrvSectorCount);

    typedef enum _eWriteAfterErasereq
    {
        WAEREQ_NOT_SPECIFIED = 0,
        WAEREQ_READ_COMPLETES_GOOD_STATUS = 1,
        WAEREQ_MEDIUM_ERROR_OTHER_ASC = 2,
        WAEREQ_MEDIUM_ERROR_WRITE_AFTER_SANITIZE_REQUIRED = 3,
        //The values above are in the SBC standard. The values below this comment are added to handle other cases not fully described in the standard.-TJE
        WAEREQ_PI_FORMATTED_MAY_REQUIRE_OVERWRITE = 4
    }eWriteAfterEraseReq;

    typedef struct _writeAfterErase
    {
        eWriteAfterEraseReq cryptoErase;
        eWriteAfterEraseReq blockErase;
    }writeAfterErase, *ptrWriteAfterErase;

    //-----------------------------------------------------------------------------
    //
    //  is_Write_After_Crypto_Erase_Required(tDevice* device, ptrWriteAfterErase writeReq)
    //
    //! \brief   This reads the SCSI block device characteristics VPD page to determine if a write is required after crypto or block erase before a read completes successfully.
    //
    //  Entry:
    //!   \param device - pointer to the tdevice structure for the drive to retrieve information from
    //!   \param writeReq - pointer to a structure filled in with the write after erase info
    //!
    //  Exit:
    //!   \return SUCCESS = successfully read write after erase data, NOTE_SUPPORTED = feature not supported by the device, BAD_PARAMETER = invalid structure size or version or other input error, anything else = some error occured while determining support.
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API eReturnValues is_Write_After_Erase_Required(tDevice* device, ptrWriteAfterErase writeReq);

#if defined (__cplusplus)
}
#endif
