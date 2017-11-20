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
// \file ata_Security.h
// \brief This file defines the function calls for performing some ATA Security operations

#pragma once

#include "operations_Common.h"

#if defined (__cplusplus)
extern "C"
{
#endif

    //-----------------------------------------------------------------------------
    //
    //  sat_ATA_Security_Protocol_Supported(tDevice *device)
    //
    //! \brief   Description:  Checks if the SAT ATA Security protocol is supportd or not.
    //
    //  Entry:
    //!   \param[in] device = file descriptor
    //!
    //  Exit:
    //!   \return true = SATL supports ATA Security protocol, false = not supported.
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API bool sat_ATA_Security_Protocol_Supported(tDevice *device);

    typedef struct _ataSecurityStatus
    {
        uint16_t masterPasswordIdentifier;
        bool masterPasswordCapability; //false = high, true = maximum
        bool enhancedEraseSupported;
        bool securityCountExpired;
        bool securityFrozen;
        bool securityLocked;
        bool securityEnabled;
        bool securitySupported;
        bool extendedTimeFormat; //this bool lets a caller know if the time was reported by the drive in extended format or normal format.
        uint16_t securityEraseUnitTimeMinutes;
        uint16_t enhancedSecurityEraseUnitTimeMinutes;
    }ataSecurityStatus, *ptrATASecurityStatus;

    //-----------------------------------------------------------------------------
    //
    //  get_ATA_Security_Info()
    //
    //! \brief   Description:  Fills in the struct with information about what is supported by the drive for ATA security. This uses the device->drive_info.IdentifyData.ata.ata to fill this in. Performing an identify command before calling this will update the data
    //
    //  Entry:
    //!   \param[in] device = file descriptor
    //!   \param[out] securityStatus = pointer to the structure to fill in with security information
    //!   \param[in] useSAT = set to true to attempt commands using the SAT spec security protocol for ATA security. This is recommended for non-ata interfaces if the SATL supports it.
    //!
    //  Exit:
    //!   \return VOID
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API void get_ATA_Security_Info(tDevice *device, ptrATASecurityStatus securityStatus, bool useSAT);

    typedef enum _eATASecurityPasswordType
    {
        ATA_PASSWORD_USER,
        ATA_PASSWORD_MASTER
    }eATASecurityPasswordType;

    typedef enum _eATASecurityMasterPasswordCapability
    {
        ATA_MASTER_PASSWORD_HIGH,
        ATA_MASTER_PASSWORD_MAXIMUM
    }eATASecurityMasterPasswordCapability;

    //-----------------------------------------------------------------------------
    //
    //  set_ATA_Security_Password_In_Buffer()
    //
    //! \brief   Description:  Sets the password for ATA security in the buffer
    //
    //  Entry:
    //!   \param[out] ptrData = pointer to the buffer to set the password in
    //!   \param[in] ATAPassword = null terminated string to represent the password
    //!   \param[in] passwordType = enum type to tell this function when to set certain bits
    //!   \param[in] masterPasswordCapability = master password capability. Only used when password type is ATA_PASSWORD_MASTER
    //!   \param[in] masterPasswordIdentifier = master password identifier. Only used when password type is ATA_PASSWORD_MASTER
    //!
    //  Exit:
    //!   \return VOID
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API void set_ATA_Security_Password_In_Buffer(uint8_t *ptrData, const char *ATAPassword, eATASecurityPasswordType passwordType, eATASecurityMasterPasswordCapability masterPasswordCapability, uint16_t masterPasswordIdentifier);

    typedef enum _eATASecurityEraseType
    {
        ATA_SECURITY_ERASE_STANDARD_ERASE,
        ATA_SECURITY_ERASE_ENHANCED_ERASE
    }eATASecurityEraseType;

    //-----------------------------------------------------------------------------
    //
    //  set_ATA_Security_Erase_Type_In_Buffer()
    //
    //! \brief   Description:  sets the bits telling whether this is a enhanced erase or not
    //
    //  Entry:
    //!   \param[out] ptrData = pointer to the buffer to set the password in
    //!   \param[in] eraseType = enhanced or normal ATA Security erase.
    //!
    //  Exit:
    //!   \return VOID
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API void set_ATA_Security_Erase_Type_In_Buffer(uint8_t *ptrData, eATASecurityEraseType eraseType);

    //-----------------------------------------------------------------------------
    //
    //  disable_ATA_Security_Password()
    //
    //! \brief   Description:  takes a password and sends the disable ATA Security password command
    //
    //  Entry:
    //!   \param[in] device = file descriptor
    //!   \param[in] ATAPassword = null terminated string to represent the password
    //!   \param[in] master = set to true to say this is a master password
    //!   \param[in] useSAT = set to true to attempt commands using the SAT spec security protocol for ATA security. This is recommended for non-ata interfaces if the SATL supports it.
    //!
    //  Exit:
    //!   \return SUCCESS = pass, FAILURE = fail
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API int disable_ATA_Security_Password(tDevice *device, const char *ATAPassword, bool master, bool useSAT);

    //-----------------------------------------------------------------------------
    //
    //  set_ATA_Security_Password()
    //
    //! \brief   Description:  takes a password and sends the set ATA Security password command
    //
    //  Entry:
    //!   \param[in] device = file descriptor
    //!   \param[in] ATAPassword = null terminated string to represent the password
    //!   \param[in] master = set to true to say this is a master password
    //!   \param[in] masterPasswordCapabilityMaximum = set to true to set the bit for master password capability to maximum. only valid when master is set to true
    //!   \param[in] masterPasswordIdentifier = the master password identifier to use when setting the master password. only valid when master is set to true
    //!   \param[in] useSAT = set to true to attempt commands using the SAT spec security protocol for ATA security. This is recommended for non-ata interfaces if the SATL supports it.
    //!
    //  Exit:
    //!   \return SUCCESS = pass, FAILURE = fail
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API int set_ATA_Security_Password(tDevice *device, const char *ATAPassword, bool master, bool masterPasswordCapabilityMaximum, uint16_t masterPasswordIdentifier, bool useSAT);

    //-----------------------------------------------------------------------------
    //
    //  unlock_ATA_Security()
    //
    //! \brief   Description:  takes a password and sends the unlock ATA Security password command
    //
    //  Entry:
    //!   \param[in] device = file descriptor
    //!   \param[in] ATAPassword = null terminated string to represent the password
    //!   \param[in] master = set to true to say this is a master password
    //!   \param[in] useSAT = set to true to attempt commands using the SAT spec security protocol for ATA security. This is recommended for non-ata interfaces if the SATL supports it.
    //!
    //  Exit:
    //!   \return SUCCESS = pass, FAILURE = fail
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API int unlock_ATA_Security(tDevice *device, const char *ATAPassword, bool master, bool useSAT);

    //-----------------------------------------------------------------------------
    //
    //  start_ATA_Security_Erase()
    //
    //! \brief   Description:  takes a password and sends the erase prepare and erase unit commands. WARNING! This function may appear to hang on drives that have a long timeout for this function! Strongly suggest using multithreaded code with this function!
    //
    //  Entry:
    //!   \param[in] device = file descriptor
    //!   \param[in] ATAPassword = null terminated string to represent the password
    //!   \param[in] master = set to true to say this is a master password
    //!   \param[in] enhanced = set to true to set the bit for enhanced security erase
    //!   \param[in] timeout = timeout value to use for the ata security erase unit command. This should be the value returned from the ATA identify data or greater (in seconds).
    //!   \param[in] useSAT = set to true to attempt commands using the SAT spec security protocol for ATA security. This is recommended for non-ata interfaces if the SATL supports it.
    //!
    //  Exit:
    //!   \return SUCCESS = pass, FAILURE = fail
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API int start_ATA_Security_Erase(tDevice *device, const char *ATAPassword, bool master, bool enhanced, uint32_t timeout, bool useSAT);

    typedef enum _eATASecurityFailureReason
    {
        ATA_SECURITY_NO_FAILURE,
        ATA_SECURITY_IS_FROZEN,
        ATA_SECURITY_IS_LOCKED,
        ATA_SECURITY_IS_ALREADY_ENABLED,
        ATA_SECURITY_IS_NOT_ENABLED,
        ATA_SECURITY_IS_NOT_SUPPORTED,
        ATA_SECURITY_ENHANCED_ERASE_IS_NOT_SUPPORTED,
        ATA_SECURITY_IS_NOT_AN_ATA_DRIVE,
        ATA_SECURITY_UNKNOWN_ERROR
    }eATASecurityFailureReason;

    //-----------------------------------------------------------------------------
    //
    //  run_ATA_Security_Erase()
    //
    //! \brief   Description:  Function to send a ATA Spec Secure Erase chosen device
    //
    //  Entry:
    //!   \param[in] device = file descriptor
    //!   \param[in] enhanced = 0 - standard secure erase, 1 - enhanced secure erase
    //!   \param[in] master = 0 - user password, 1 - master password
    //!   \param[in] password - password to use for ata security erase
    //!   \param[in] pollForProgress = 0 - start the erase and return, 1 - start the erase and run a countdown timer until complete
    //!
    //  Exit:
    //!   \return SUCCESS = pass, FAILURE = fail
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API int run_ATA_Security_Erase(tDevice *device, bool enhanced, bool master, const char *password, bool pollForProgress);

    //-----------------------------------------------------------------------------
    //
    //  run_Disable_ATA_Security_Password( tDevice * device )
    //
    //! \brief   Disable the ATA security password. This is useful if the ATA security erase was interrupted and a password is still set on the drive. 
    //! Note that this takes the ASCII password sent in and uses it, but a BIOS may do a hash or something else when setting a password so this may 
    //! not work for passwords other than those set by this code base
    //
    //  Entry:
    //!   \param device - file descriptor
    //!   \param ATAPassword - password to use for disabling ATA Security password.
    //!   \param userMaster - boolean flag to select user or master password
    //!
    //  Exit:
    //!   \return SUCCESS = good, !SUCCESS something went wrong see error codes
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API int run_Disable_ATA_Security_Password(tDevice *device, const char *ATAPassword, bool userMaster);

#if defined (__cplusplus)
}
#endif