//
// Do NOT modify or remove this copyright and license
//
// Copyright (c) 2012 - 2017 Seagate Technology LLC and/or its Affiliates, All Rights Reserved
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
    //  change_Pin11( tDevice * device )
    //
    //! \brief   Change Pin 11 behavior on SAS drives. SAS is configurable with a command, SATA is not so SAS is the only thing supported in this call.
    //
    //  Entry:
    //!   \param device - file descriptor
    //!   \param pin11Default - set to true to restore the drive's default pin11 behavior
    //!   \param pin11OnOff - when set to true, set the ready LED bit to a 1, other wise set to a 0. See the SAS protocol spec for details on this mode page.
    //!
    //  Exit:
    //!   \return SUCCESS = good, !SUCCESS something went wrong see error codes
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API int change_Pin11(tDevice *device, bool pin11Default, bool pin11OnOff);

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
    OPENSEA_OPERATIONS_API int set_Read_Look_Ahead(tDevice *device, bool readLookAheadEnableDisable);

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
    OPENSEA_OPERATIONS_API int scsi_Set_Read_Look_Ahead(tDevice *device, bool readLookAheadEnableDisable);

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
    OPENSEA_OPERATIONS_API int ata_Set_Read_Look_Ahead(tDevice *device, bool readLookAheadEnableDisable);

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
    OPENSEA_OPERATIONS_API int set_Write_Cache(tDevice *device, bool writeCacheEnableDisable);

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
    OPENSEA_OPERATIONS_API int scsi_Set_Write_Cache(tDevice *device, bool writeCacheEnableDisable);

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
    OPENSEA_OPERATIONS_API int ata_Set_Write_Cache(tDevice *device, bool writeCacheEnableDisable);

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
        ERASE_TRIM_UNMAP,
        ERASE_TCG_REVERT_SP, //will be use in tcg operations lib, not operations lib
        ERASE_TCG_REVERT, //will be use in tcg operations lib, not operations lib
        ERASE_FORMAT_UNIT,
        ERASE_MAX_VALUE = -1
    }eEraseMethod;

    #define MAX_SUPPORTED_ERASE_METHODS 13
    #define MAX_ERASE_NAME_LENGTH 30
    #define MAX_ERASE_WARNING_LENGTH 70

    typedef struct _eraseMethod
    {
        eEraseMethod eraseIdentifier;
        char eraseName[MAX_ERASE_NAME_LENGTH];
        bool warningValid;
        char eraseWarning[MAX_ERASE_WARNING_LENGTH];//may be an empty string. May contain something like "requires password" or "cannot be stopped"
        uint8_t eraseWeight;//used to store how fast/slow it is...used for sorting from fastest to slowest
    }eraseMethod;

    //-----------------------------------------------------------------------------
    //
    //  get_Supported_Erase_Methods(tDevice *device, eraseMethod const eraseMethodList[MAX_SUPPORTED_ERASE_METHODS])
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
    OPENSEA_OPERATIONS_API int get_Supported_Erase_Methods(tDevice *device, eraseMethod const eraseMethodList[MAX_SUPPORTED_ERASE_METHODS], uint32_t *overwriteEraseTimeEstimateMinutes);

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
    //!   \return SUCCESS = successfully determined erase support, anything else = some error occured while determining support.
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API void print_Supported_Erase_Methods(tDevice *device, eraseMethod const eraseMethodList[MAX_SUPPORTED_ERASE_METHODS], uint32_t *overwriteEraseTimeEstimateMinutes);
    
    //-----------------------------------------------------------------------------
    //
    //  enable_Disable_PUIS_Feature(tDevice *device, bool enable)
    //
    //! \brief   Enables or disables the SATA PUIS feature
    //
    //  Entry:
    //!   \param device - file descriptor
    //!   \param enable - set to true to enable the PUIS feature. Set to False to disable the PUIS feature.
    //!
    //  Exit:
    //!   \return SUCCESS = successfully determined erase support, anything else = some error occured while determining support.
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API int enable_Disable_PUIS_Feature(tDevice *device, bool enable);

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
    OPENSEA_OPERATIONS_API int set_Sense_Data_Format(tDevice *device, bool defaultSetting, bool descriptorFormat, bool saveParameters);

    OPENSEA_OPERATIONS_API int get_Current_Free_Fall_Control_Sensitivity(tDevice * device, uint16_t *sensitivity);//if sensitivity is set to UINT16_MAX, then the feature is supported, but not enabled, so the value wouldn't otherwise make sense

    OPENSEA_OPERATIONS_API int set_Free_Fall_Control_Sensitivity(tDevice *device, uint8_t sensitivity);//enables the feature. Value of zero sets a vendor's recommended setting

    OPENSEA_OPERATIONS_API int disable_Free_Fall_Control_Feature(tDevice *device);//disables the free fall control feature

    #if defined (__cplusplus)
}
    #endif
