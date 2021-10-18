//
// Do NOT modify or remove this copyright and license
//
// Copyright (c) 2012-2021 Seagate Technology LLC and/or its Affiliates, All Rights Reserved
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

    //This is a password defined in this MSDN article for performing secure erase in Windows PE.
    //https://docs.microsoft.com/en-us/windows-hardware/drivers/storage/security-group-commands
    //This can only be used for the user password during an erase in WinPE.
    #define WINDOWS_PE_ATA_SECURITY_PASSWORD "AutoATAWindowsString12345678901"

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

    typedef enum _eATASecurityZacOptions
    {
        ATA_ZAC_ERASE_EMPTY_ZONES = 0,
        ATA_ZAC_ERASE_FULL_ZONES = 1
    }eATASecurityZacOptions;

    typedef enum _eATASecurityEraseType
    {
        ATA_SECURITY_ERASE_STANDARD_ERASE,
        ATA_SECURITY_ERASE_ENHANCED_ERASE
    }eATASecurityEraseType;

    typedef struct _ataSecurityPassword
    {
        //set this to true when setting/using the master password.
        eATASecurityPasswordType passwordType;
        //When set to true, this means setting the master password capability bit to maximum. zero is high. (Only for setting the password!!!)
        //If this is set to high, user and master password can unlock the drive.
        //If this is set to maximum, then the master can only be used to erase the drive
        eATASecurityMasterPasswordCapability masterCapability;
        //ZAC drives only and only on Secure Erase command. This is used to specify if zone conditions are set to empty (0-false) or full (1-true)
        eATASecurityZacOptions zacSecurityOption;
        //must be between 1 and FFFEh. zero and FFFFh will be ignored by the drive. This is only a reference used for when the master password is changed, someone could look it up.
        uint16_t masterPWIdentifier;
        //32 bytes. These are not required to be ASCII in the spec.
        uint8_t password[ATA_SECURITY_MAX_PW_LENGTH];
        //This is here in case of using an empty password so that we know when placing it in the buffer what we are setting. Max length is 32bytes. Anything larger in this value is ignored.
        uint8_t passwordLength;
    }ataSecurityPassword, *ptrATASecurityPassword;

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
        eATASecurityState securityState;
        bool restrictedSanitizeOverridesSecurity;//If this is true, then a sanitize command can be run and clear the user password. (See ACS4 for more details)
        bool encryptAll;//Set to true means the device encrypts all user data on the drive.
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
    //!   \param[in] useSAT = set to true to attempt commands using the SAT spec security protocol for ATA security. This is recommended for non-ata interfaces if the SATL supports it since it allows the SATL to control the erase and incomming commands.
    //!
    //  Exit:
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API void get_ATA_Security_Info(tDevice *device, ptrATASecurityStatus securityStatus, bool useSAT);

    OPENSEA_OPERATIONS_API void print_ATA_Security_Info(ptrATASecurityStatus securityStatus, bool satSecurityProtocolSupported);

    //-----------------------------------------------------------------------------
    //
    //  set_ATA_Security_Password_In_Buffer()
    //
    //! \brief   Description:  Sets the password for ATA security in the buffer
    //
    //  Entry:
    //!   \param[out] ptrData = pointer to the buffer to set the password in
    //!   \param[in] ataPassword = pointer to the structure that holds all the relavent information to put the password in the buffer. Allows for empty passwords and non-ascii characters
    //!   \param[in] setPassword = Set to true when using the buffer to send the security set password command so that the master password identifier and capability fields will be set.
    //!   \param[in] eraseUnit = set to true when using the buffer to send the security erase command so that the zac options bit can be set.
    //!
    //  Exit:
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API void set_ATA_Security_Password_In_Buffer(uint8_t *ptrData, ptrATASecurityPassword ataPassword, bool setPassword, bool eraseUnit);

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
    //!   \param[in] ataPassword = structure holding password information.
    //!   \param[in] useSAT = set to true to attempt commands using the SAT spec security protocol for ATA security. This is recommended for non-ata interfaces if the SATL supports it.
    //!
    //  Exit:
    //!   \return SUCCESS = pass, FAILURE = fail
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API int disable_ATA_Security_Password(tDevice *device, ataSecurityPassword ataPassword, bool useSAT);

    //-----------------------------------------------------------------------------
    //
    //  set_ATA_Security_Password()
    //
    //! \brief   Description:  takes a password and sends the set ATA Security password command
    //
    //  Entry:
    //!   \param[in] device = file descriptor
    //!   \param[in] ataPassword = structure holding password information.
    //!   \param[in] useSAT = set to true to attempt commands using the SAT spec security protocol for ATA security. This is recommended for non-ata interfaces if the SATL supports it.
    //!
    //  Exit:
    //!   \return SUCCESS = pass, FAILURE = fail
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API int set_ATA_Security_Password(tDevice *device, ataSecurityPassword ataPassword, bool useSAT);

    //-----------------------------------------------------------------------------
    //
    //  unlock_ATA_Security()
    //
    //! \brief   Description:  takes a password and sends the unlock ATA Security password command
    //
    //  Entry:
    //!   \param[in] device = file descriptor
    //!   \param[in] ataPassword = structure holding password information.
    //!   \param[in] useSAT = set to true to attempt commands using the SAT spec security protocol for ATA security. This is recommended for non-ata interfaces if the SATL supports it.
    //!
    //  Exit:
    //!   \return SUCCESS = pass, FAILURE = fail
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API int unlock_ATA_Security(tDevice *device, ataSecurityPassword ataPassword, bool useSAT);

    //-----------------------------------------------------------------------------
    //
    //  start_ATA_Security_Erase()
    //
    //! \brief   Description:  takes a password and sends the erase prepare and erase unit commands. WARNING! This function may appear to hang on drives that have a long timeout for this function! Strongly suggest using multithreaded code with this function!
    //
    //  Entry:
    //!   \param[in] device = file descriptor
    //!   \param[in] ataPassword = structure holding password information.
    //!   \param[in] eraseType = set to the enum value specifying which erase to perform on the drive.
    //!   \param[in] timeout = timeout value to use for the ata security erase unit command. This should be the value returned from the ATA identify data or greater (in seconds).
    //!   \param[in] useSAT = set to true to attempt commands using the SAT spec security protocol for ATA security. This is recommended for non-ata interfaces if the SATL supports it.
    //!
    //  Exit:
    //!   \return SUCCESS = pass, FAILURE = fail
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API int start_ATA_Security_Erase(tDevice *device, ataSecurityPassword ataPassword, eATASecurityEraseType eraseType, uint32_t timeout, bool useSAT);

    //-----------------------------------------------------------------------------
    //
    //  run_ATA_Security_Erase()
    //
    //! \brief   Description:  Function to send a ATA Spec Secure Erase chosen device
    //
    //  Entry:
    //!   \param[in] device = file descriptor
    //!   \param[in] eraseType = set to enum value for type of erase to perform
    //!   \param[in] ataPassword - structure holding password information.
    //!   \param[in] forceSATValid = when true, the force SAT variable is checked and used to force the type of command used to do the erase.
    //!   \param[in] forceSAT = checked when above bool is true. When this is true, use SAT security protocol commands. When false, use ata security protocol commands (may be wrapped in SAT ATA Pass-through command which is different from sending security protocol and letting the SATL translate)
    //!
    //  Exit:
    //!   \return SUCCESS = pass, FAILURE = fail
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API int run_ATA_Security_Erase(tDevice *device, eATASecurityEraseType eraseType,  ataSecurityPassword ataPassword, bool forceSATvalid, bool forceSAT);

    //-----------------------------------------------------------------------------
    //
    //  run_Disable_ATA_Security_Password( tDevice * device )
    //
    //! \brief   Disable the ATA security password. This is useful if the ATA security erase was interrupted and a password is still set on the drive. 
    //! Note that this takes the ASCII password sent in and uses it, but a BIOS may do a hash or something else when setting a password so this may 
    //! not work for passwords other than those set by this code base
    //
    //  Entry:
    //!   \param[in] device = file descriptor
    //!   \param[in] ataPassword - structure holding password information.
    //!   \param[in] forceSATValid = when true, the force SAT variable is checked and used to force the type of command used to do the erase.
    //!   \param[in] forceSAT = checked when above bool is true. When this is true, use SAT security protocol commands. When false, use ata security protocol commands (may be wrapped in SAT ATA Pass-through command which is different from sending security protocol and letting the SATL translate)
    //!
    //  Exit:
    //!   \return SUCCESS = good, !SUCCESS something went wrong see error codes
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API int run_Disable_ATA_Security_Password(tDevice *device, ataSecurityPassword ataPassword, bool forceSATvalid, bool forceSAT);

    //-----------------------------------------------------------------------------
    //
    //  run_Set_ATA_Security_Password(tDevice *device, ataSecurityPassword ataPassword, bool forceSATvalid, bool forceSAT)
    //
    //! \brief   Sets an ATA security password. NOTE: This is not recommended from software since some systems may not even boot with a locked drive,
    //!          or may not encode the password the same way as this software. 
    //!          NOTE2: Some SATLs don't seem to properly handle locked ATA security drives, so you may not be able to unlock them or remove the password without retrying multiple times from software.
    //!          It is strongly recommended that passwords only be set from the BIOS or host controller option rom.
    //
    //  Entry:
    //!   \param[in] device = file descriptor
    //!   \param[in] ataPassword - structure holding password information.
    //!   \param[in] forceSATValid = when true, the force SAT variable is checked and used to force the type of command used to do the erase.
    //!   \param[in] forceSAT = checked when above bool is true. When this is true, use SAT security protocol commands. When false, use ata security protocol commands (may be wrapped in SAT ATA Pass-through command which is different from sending security protocol and letting the SATL translate)
    //!
    //  Exit:
    //!   \return SUCCESS = good, !SUCCESS something went wrong see error codes
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API int run_Set_ATA_Security_Password(tDevice *device, ataSecurityPassword ataPassword, bool forceSATvalid, bool forceSAT);

    //-----------------------------------------------------------------------------
    //
    //  run_Unlock_ATA_Security(tDevice *device, ataSecurityPassword ataPassword, bool forceSATvalid, bool forceSAT)
    //
    //! \brief   Unlocks ATA security with the provided password. This is useful if the ATA security erase was interrupted and a password is still set on the drive. 
    //! Note that this takes the ASCII password sent in and uses it, but a BIOS may do a hash or something else when setting a password so this may 
    //! not work for passwords other than those set by this code base
    //
    //  Entry:
    //!   \param[in] device = file descriptor
    //!   \param[in] ataPassword - structure holding password information.
    //!   \param[in] forceSATValid = when true, the force SAT variable is checked and used to force the type of command used to do the erase.
    //!   \param[in] forceSAT = checked when above bool is true. When this is true, use SAT security protocol commands. When false, use ata security protocol commands (may be wrapped in SAT ATA Pass-through command which is different from sending security protocol and letting the SATL translate)
    //!
    //  Exit:
    //!   \return SUCCESS = good, !SUCCESS something went wrong see error codes
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API int run_Unlock_ATA_Security(tDevice *device, ataSecurityPassword ataPassword, bool forceSATvalid, bool forceSAT);

    //-----------------------------------------------------------------------------
    //
    //  run_Freeze_ATA_Security(tDevice *device, bool forceSATvalid, bool forceSAT)
    //
    //! \brief   Freezes ATA security with the freezelock command. This is used to prevent other ATA security commands from being processed by the drive.
    //
    //  Entry:
    //!   \param[in] device = file descriptor
    //!   \param[in] forceSATValid = when true, the force SAT variable is checked and used to force the type of command used to do the erase.
    //!   \param[in] forceSAT = checked when above bool is true. When this is true, use SAT security protocol commands. When false, use ata security protocol commands (may be wrapped in SAT ATA Pass-through command which is different from sending security protocol and letting the SATL translate)
    //!
    //  Exit:
    //!   \return SUCCESS = good, !SUCCESS something went wrong see error codes
    //
    //-----------------------------------------------------------------------------
    OPENSEA_OPERATIONS_API int run_Freeze_ATA_Security(tDevice *device, bool forceSATvalid, bool forceSAT);

#if defined (__cplusplus)
}
#endif
