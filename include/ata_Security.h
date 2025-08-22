// SPDX-License-Identifier: MPL-2.0

//! \file ata_Security.h
//! \brief Defines functions, enums, types, etc. for performing ATA Security operations
//! \copyright
//! Do NOT modify or remove this copyright and license
//!
//! Copyright (c) 2012-2025 Seagate Technology LLC and/or its Affiliates, All Rights Reserved
//!
//! This software is subject to the terms of the Mozilla Public License, v. 2.0.
//! If a copy of the MPL was not distributed with this file, You can obtain one at http://mozilla.org/MPL/2.0/.

#pragma once

#include "operations_Common.h"

#if defined(__cplusplus)
extern "C"
{
#endif

//! \def WINDOWS_PE_ATA_SECURITY_PASSWORD
//! \brief Definition of the ATA security password required for use in Windows PE mode
//! \see https://docs.microsoft.com/en-us/windows-hardware/drivers/storage/security-group-commands
//! \note Can only be used as a user password during ATA Security Erase in Windows PE. Master
//! password is not allowed.
#define WINDOWS_PE_ATA_SECURITY_PASSWORD "AutoATAWindowsString12345678901"

    //! \enum eATASecurityPasswordType
    //! \brief User vs Master password
    typedef enum eATASecurityPasswordTypeEnum
    {
        /*!< Password is the User's password */
        ATA_PASSWORD_USER,
        /*!< Password is the Master/Admin password */
        ATA_PASSWORD_MASTER
    } eATASecurityPasswordType;

    //! \enum eATASecurityMasterPasswordCapability
    //! \brief When setting a password, this selects between high and maximum security modes
    //! \note In high security, the master password can unlock data access.
    //! In maximum security, the master password can only be used to erase and repurpose the device.
    typedef enum eATASecurityMasterPasswordCapabilityEnum
    {
        /*!< High security user password: Master/admin can also unlock the data */
        ATA_MASTER_PASSWORD_HIGH,
        /*!< Maximum security user password: Only user can unlock the data. Master/Admin can only erase */
        ATA_MASTER_PASSWORD_MAXIMUM
    } eATASecurityMasterPasswordCapability;

    //! \enum eATASecurityZacOptions
    //! \brief Also called ZNR - zone no reset. Can control state of drive at completion of erase
    typedef enum eATASecurityZacOptionsEnum
    {
        /*!< Default: ZAC devices set zones to empty at completion of erase */
        ATA_ZAC_ERASE_EMPTY_ZONES = 0,
        /*!< ZAC devices leave zones full at completion. This allows reading for verification of */
        /*   data removal. */
        ATA_ZAC_ERASE_FULL_ZONES = 1
    } eATASecurityZacOptions;

    //! \enum eATASecurityEraseType
    //! \brief Select between standard ATA security erase and Enhanced Security erase mode
    //! \note Not all devices support enhanced erase. Check for support before sending enhanced erase.
    typedef enum eATASecurityEraseTypeEnum
    {
        /*!< Default: Standard erase. Writes 00h or FFh to all bytes from LBA 0 to current maxLBA */
        ATA_SECURITY_ERASE_STANDARD_ERASE,
        /*< Enhanced: Writes vendor unique pattern to all LBAs on device including reallocated, spare, */
        /*  currently inaccessible (HPA or AMAC or DCO) LBAs. Any place user data has been or could have */
        /*  been written during the device's life of use. */
        ATA_SECURITY_ERASE_ENHANCED_ERASE
    } eATASecurityEraseType;

    //! \enum eATASecurityMasterPWID
    //! \brief Holds valid minimum and maximum values that can be used for the master password
    //! identifier field. This is set when the master password is set.
    typedef enum eATASecurityMasterPWIDEnum
    {
        /*!< Minimum value that can be set for master password identifier */
        ATA_SEC_MASTER_PW_ID_MIN = 0x0001,
        /*!< Maximum value that can be set for master password identifier */
        ATA_SEC_MASTER_PW_ID_MAX = 0xFFFE,
        /*!< If set to this value, the master password may still be set to the */
        /* Device vendor's default master password. This may be a security risk */
        /* as sometimes a vendors default leaks online or could still be used */
        /* by the vendor to unlock/erase the drive */
        ATA_SEC_MASTER_PW_ID_VENDOR_DEFAULT = 0xFFFE
    } eATASecurityMasterPWID;

    //! \fn uint16_t increment_Master_Password_Identifier(uint16_t masterPWID)
    //! \brief Increments the master password identifier to the next possible value.
    //! Will roll over to 1 as needed.
    //! \returns Will never return FFFEh value to avoid confusion around being set to the
    //! manufacturer's default value or not.
    OPENSEA_OPERATIONS_API uint16_t increment_Master_Password_Identifier(uint16_t masterPWID);

    //! \struct ataSecurityPassword
    //! \brief This structure holds all the information necessary for how to use a given password with
    //! ATA security. Some information will only be used when setting the password such as the master password
    //! identifier others may only be used during erase (ZAC options).
    typedef struct s_ataSecurityPassword
    {
        //! \var passwordType
        //! \see \a eATASecurityPasswordType
        eATASecurityPasswordType passwordType;
        //! \var masterCapability
        //! \see \a eATASecurityMasterPasswordCapability
        eATASecurityMasterPasswordCapability masterCapability;
        //! \var zacSecurityOption
        //! \see \a eATASecurityZacOptions
        eATASecurityZacOptions zacSecurityOption;
        //! \var masterPWIdentifier
        //! \brief A value between 1 and FFFEh to use as a lookup for the administrator to find the password.
        //! \note FFFEh is the default value when this field is supported and means it is set to the manufacturer's
        //! default master password value. Changing this is recommended for additional security.
        uint16_t masterPWIdentifier;
        //! \var password
        //! \brief 32 byte field to hold the password. May be set to any 32byte value (all zeroes, all F's, hash, ASCII)
        //! \note The ATA specification does not set any requirements on how this field is used. The drive simply
        //! compares the value to whatever it has saved from when the password was set. 
        //! \note Some BIOS's will hash the password in a proprietary way that this software does not know.
        //! Do not expect that just because you typed the same thing as you set in the BIOS that this will unlock
        //! exactly the same way. Whatever method the BIOS uses before filling in this field must also be used when
        //!  filling in this field in order for the drive to properly match the password.
        uint8_t password[ATA_SECURITY_MAX_PW_LENGTH];
        //! \var passwordLength
        //! \brief length of the password provided in \a password.
        //! Between this value and \a ATA_SECURITY_MAX_PW_LENGTH will be copied to the drive buffer and zero padded.
        uint8_t passwordLength;
    } ataSecurityPassword, *ptrATASecurityPassword;

    //! \fn bool sat_ATA_Security_Protocol_Supported(tDevice* device)
    //! \brief Checks if the SAT specification's security protocol Eh is supported or not.
    //! \param[in] device pointer to the device structure representing the drive to check
    //! \return true means supported, false means not supported.
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1) OPENSEA_OPERATIONS_API bool sat_ATA_Security_Protocol_Supported(tDevice* device);

    //! \struct ataSecurityStatus
    //! \brief This structure holds all ATA security information that can be read from the device.
    //! \note SAT security protocol does not have a way to report \a encryptAll or
    //! \a restrictedSanitizeOverridesSecurity. Devices that only support this method will set these both to false.
    typedef struct s_ataSecurityStatus
    {
        //! \var masterPasswordIdentifier
        //! \brief A value between 1 and FFFEh to use as a lookup for the administrator to find the password.
        //! \note FFFEh is the default value when this field is supported and means it is set to the manufacturer's
        //! default master password value. Changing this is recommended for additional security.
        uint16_t masterPasswordIdentifier;
        //! \var masterPasswordCapability
        //! \brief Used when setting a password to put the drive into "high" or "maximum" security mode
        //! false = high security, true = maximum security
        //! \note High security = master password can erase and unlock the drive.
        //! Maximum security = master password can only erase the drive.
        bool masterPasswordCapability;
        //! \var enhancedEraseSupported
        //! \brief If set to true, enhanced security erase mode is supported
        bool enhancedEraseSupported;
        //! \var securityCountExpired
        //! \brief If set to true, the maximum password attempts has been reached (5 attempts). Drive must be power
        //! cycled to reset this counter.
        bool securityCountExpired;
        //! \var securityFrozen
        //! \brief Set to true if ATA security has been frozen. If frozen, no password changes or erasure are allowed.
        bool securityFrozen;
        //! \var securityLocked
        //! \brief If true, ATA security is enabled, but the password has not yet been used to unlock the drive.
        bool securityLocked;
        //! \var securityEnabled
        //! \brief If true, ATA security is enabled.
        bool securityEnabled;
        //! \var securitySupported
        //! \brief If true, ATA security is supported by the device.
        bool securitySupported;
        //! \var extendedTimeFormat
        //! \brief The erase time is reported in an extended format to allow for larger values.
        bool extendedTimeFormat;
        //! \var securityEraseUnitTimeMinutes
        //! \brief The number of minutes ATA security erase is expected to take to complete. This estimate is not exact.
        //! Real erase time may be longer.
        uint16_t securityEraseUnitTimeMinutes;
        //! \var enhancedSecurityEraseUnitTimeMinutes
        //! \brief The number of minutes ATA enhanced security erase is expected to take to complete. This estimate is
        //! not exact. Real erase time may be longer.
        uint16_t enhancedSecurityEraseUnitTimeMinutes;
        //! \var securityState
        //! \brief The ATA security state from the spec. Set by checking the boolean values above into a single
        //! convenient variable.
        eATASecurityState securityState;
        //! \var restrictedSanitizeOverridesSecurity
        //! \brief If true, running a sanitize command in restricted mode overrides ATA security and can be used to wipe
        //! the data and remove the user password once it has completed.
        bool restrictedSanitizeOverridesSecurity;
        //! \var encryptAll
        //! \brief If true, the device encrypts all user data on the storage medium.
        //! \note If this is true, sometimes the enhanced security erase time may report as 2 minutes (lowest possible
        //! value) to indicate that it performs a crypto graphic erasure of the data.
        bool encryptAll;
    } ataSecurityStatus, *ptrATASecurityStatus;

    //! \fn void get_ATA_Security_Info(tDevice* device, ptrATASecurityStatus securityStatus, bool useSAT);
    //! \brief Reads the ATA Security info from the device
    //! \param[in] device pointer to the device structure of the device to query for information
    //! \param[out] securityStatus pointer to the \a ataSecurityStatus structure to fill with information
    //! \param[in] useSAT use the SAT security protocol to retrieve information
    //! \return void
    M_NONNULL_PARAM_LIST(1, 2)
    M_PARAM_RO(1)
    M_PARAM_WO(2)
    OPENSEA_OPERATIONS_API
    void get_ATA_Security_Info(tDevice* device, ptrATASecurityStatus securityStatus, bool useSAT);

    //! \fn void print_ATA_Security_Info(ptrATASecurityStatus securityStatus, bool satSecurityProtocolSupported);
    //! \brief Prints the ATA Security info to stdout
    //! \param[in] securityStatus pointer to the \a ataSecurityStatus structure with valid information.
    //! \param[in] satSecurityProtocolSupported Specifies if SAT security protocol is supported so this function
    //! can adjust output or note SAT security protocol support as needed.
    //! \return void
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1)
    OPENSEA_OPERATIONS_API
    void print_ATA_Security_Info(ptrATASecurityStatus securityStatus, bool satSecurityProtocolSupported);

    //! \fn void set_ATA_Security_Password_In_Buffer(uint8_t*               ptrData,
    //!                                                                     ptrATASecurityPassword ataPassword,
    //!                                                                     bool                   setPassword,
    //!                                                                     bool                   eraseUnit,
    //!                                                                     bool                   useSAT)
    //! \brief Takes the ATA security password structure and writes it and any associated flags into the
    //! provided 512B buffer. There are some variations between set password and erase unit commands
    //! so those flags are necessary for this function to interpret the fields correctly.
    //! \param[out] ptrData pointer to data buffer that is 512B in size to setup the fields in
    //! \param[in] ataPassword pointer to ATA security password details and flags needed for the buffer
    //! \param[in] setPassword set to true if setting up the buffer for the ATA security set password command
    //! \param[in] eraseUnit set to true if setting up the buffer for the ATA security erase unit command
    //! \param[in] useSAT set to true if this buffer is for use the SAT security protocol since that may
    //! put some flags in different locations
    //! \return void
    M_NONNULL_PARAM_LIST(1, 2)
    M_PARAM_WO(1)
    M_PARAM_RO(2)
    OPENSEA_OPERATIONS_API void set_ATA_Security_Password_In_Buffer(uint8_t*               ptrData,
                                                                    ptrATASecurityPassword ataPassword,
                                                                    bool                   setPassword,
                                                                    bool                   eraseUnit,
                                                                    bool                   useSAT);

    //! \fn void set_ATA_Security_Erase_Type_In_Buffer(uint8_t* ptrData, eATASecurityEraseType eraseType, bool useSAT)
    //! \brief Sets the requested ATA security erase type into the provided buffer
    //! \param[out] ptrData pointer to data buffer that is 512B in size to set the erase type in
    //! \param[in] eraseType see \a eATASecurityEraseType for values
    //! \param[in] useSAT set to true if this buffer is for use the SAT security protocol since that may
    //! put some flags in different locations
    //! \return void
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RW(1)
    OPENSEA_OPERATIONS_API
    void set_ATA_Security_Erase_Type_In_Buffer(uint8_t* ptrData, eATASecurityEraseType eraseType, bool useSAT);

    //! \fn eReturnValues disable_ATA_Security_Password(tDevice*            device,
    //!                                                                     ataSecurityPassword ataPassword,
    //!                                                                     bool                useSAT)
    //! \brief Uses the provided password information to run the disable ATA Security password command
    //! \param[in] device pointer to the device structure of the device to disable the password on
    //! \param[in] ataPassword structure with the ATA security password information to use
    //! \param[in] useSAT set to true if this buffer is for use the SAT security protocol since that may
    //! put some flags in different locations
    //! \return SUCCESS if disabling the password worked successfully. FROZEN if ATA security is frozen.
    //! other values may be returned if some other failure occurs.
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1)
    OPENSEA_OPERATIONS_API eReturnValues disable_ATA_Security_Password(tDevice*            device,
                                                                       ataSecurityPassword ataPassword,
                                                                       bool                useSAT);

    //! \fn eReturnValues set_ATA_Security_Password(tDevice*            device,
    //!                                                                 ataSecurityPassword ataPassword,
    //!                                                                 bool                useSAT)
    //! \brief Uses the provided information to set the ATA security password on the device
    //! \param[in] device pointer to the device structure of the device to disable the password on
    //! \param[in] ataPassword structure with the ATA security password information to use
    //! \param[in] useSAT set to true if this buffer is for use the SAT security protocol since that may
    //! put some flags in different locations
    //! \return SUCCESS if the password is set successfully. FROZEN if ATA security is frozen.
    //! other values may be returned if some other failure occurs.
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1)
    OPENSEA_OPERATIONS_API eReturnValues set_ATA_Security_Password(tDevice*            device,
                                                                   ataSecurityPassword ataPassword,
                                                                   bool                useSAT);

    //! \fn eReturnValues unlock_ATA_Security(tDevice*            device,
    //!                                                           ataSecurityPassword ataPassword,
    //!                                                           bool                useSAT)
    //! \brief Uses the provided password information to unlock ATA security on a device.
    //! \param[in] device pointer to the device structure of the device to unlock
    //! \param[in] ataPassword structure with the ATA security password information to use
    //! \param[in] useSAT set to true if this buffer is for use the SAT security protocol since that may
    //! put some flags in different locations
    //! \return SUCCESS if the drive is unlocked successfully. FROZEN if ATA security is frozen.
    //! other values may be returned if some other failure occurs.
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1)
    OPENSEA_OPERATIONS_API eReturnValues unlock_ATA_Security(tDevice*            device,
                                                             ataSecurityPassword ataPassword,
                                                             bool                useSAT);

    //! \fn eReturnValues start_ATA_Security_Erase(tDevice*              device,
    //!                                                                  ataSecurityPassword   ataPassword,
    //!                                                                  eATASecurityEraseType eraseType,
    //!                                                                  uint32_t              timeout,
    //!                                                                  bool                  useSAT)
    //! \brief Uses the provided password information to start an ATA security erase (sending prepare and erase
    //! commands)
    //! \note This function will not return until the drive has completed the erase or it has been interrupted.
    //! It is not possible to poll for progress during an ATA security erase. It holds the bus busy until it completes.
    //! \param[in] device pointer to the device structure of the device to erase
    //! \param[in] ataPassword structure with the ATA security password information to use
    //! \param[in] eraseType see \a eATASecurityEraseType for values
    //! \param[in] timeout The timeout value to provide to the operating system when issuing the erase command.
    //! This should be set to at least the erase time estimate from the drive. Recommended to set 20% more than the
    //! estimated time or longer. If a drive does not provide an estimate, it is recommended to use 2 hours per
    //! terabyte, then add 20% more time.
    //! \param[in] useSAT set to true if this buffer is for use the SAT security protocol since that may
    //! put some flags in different locations
    //! \return SUCCESS if the drive is unlocked successfully. FROZEN if ATA security is frozen.
    //! other values may be returned if some other failure occurs.
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1)
    OPENSEA_OPERATIONS_API eReturnValues start_ATA_Security_Erase(tDevice*              device,
                                                                  ataSecurityPassword   ataPassword,
                                                                  eATASecurityEraseType eraseType,
                                                                  uint32_t              timeout,
                                                                  bool                  useSAT);

    //! \fn eReturnValues run_ATA_Security_Erase(tDevice*              device,
    //!                                                                eATASecurityEraseType eraseType,
    //!                                                                ataSecurityPassword   ataPassword,
    //!                                                                bool                  forceSATvalid,
    //!                                                                bool                  forceSAT)
    //! \brief This function handles all necessary steps to perform an ATA security erase on a device.
    //! It will check current state, set passwords, start and run the erase, and check the results and
    //! remove a password if it fails to complete successfully.
    //! \param[in] device pointer to the device structure of the device to erase
    //! \param[in] eraseType see \a eATASecurityEraseType for values
    //! \param[in] ataPassword structure with the ATA security password information to use
    //! \param[in] forceSATValid set to true to say the next variable is set to a valid value by the caller,
    //! otherwise it is ignored.
    //! \param[in] forceSAT set to true to force using the SAT security protocol instead of passthrough ATA
    //! security commands.
    //! \return SUCCESS if the drive is unlocked successfully. FROZEN if ATA security is frozen.
    //! other values may be returned if some other failure occurs.
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1)
    OPENSEA_OPERATIONS_API eReturnValues run_ATA_Security_Erase(tDevice*              device,
                                                                eATASecurityEraseType eraseType,
                                                                ataSecurityPassword   ataPassword,
                                                                bool                  forceSATvalid,
                                                                bool                  forceSAT);

    //! \fn eReturnValues eReturnValues run_Disable_ATA_Security_Password(tDevice*   device,
    //!                                                                              ataSecurityPassword ataPassword,
    //!                                                                              bool                forceSATvalid,
    //!                                                                              bool                forceSAT)
    //! \brief This function handles all necessary steps to perform an ATA security disable password on a device.
    //! It will check current state, run the unlock, and disable password commands and check the results of each.
    //! \param[in] device pointer to the device structure of the device to disable the password on
    //! \param[in] ataPassword structure with the ATA security password information to use
    //! \param[in] forceSATValid set to true to say the next variable is set to a valid value by the caller,
    //! otherwise it is ignored.
    //! \param[in] forceSAT set to true to force using the SAT security protocol instead of passthrough ATA
    //! security commands.
    //! \return SUCCESS if the drive password is disabled successfully. FROZEN if ATA security is frozen.
    //! other values may be returned if some other failure occurs.
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1)
    OPENSEA_OPERATIONS_API eReturnValues run_Disable_ATA_Security_Password(tDevice*            device,
                                                                           ataSecurityPassword ataPassword,
                                                                           bool                forceSATvalid,
                                                                           bool                forceSAT);

    //! \fn eReturnValues eReturnValues run_Set_ATA_Security_Password(tDevice*   device,
    //!                                                                          ataSecurityPassword ataPassword,
    //!                                                                          bool                forceSATvalid,
    //!                                                                          bool                forceSAT)
    //! \brief This function handles all necessary steps to perform an ATA security set password on a device.
    //! It will check current state, and run any necessary steps to set the password
    //! \param[in] device pointer to the device structure of the device to set the password on
    //! \param[in] ataPassword structure with the ATA security password information to use
    //! \param[in] forceSATValid set to true to say the next variable is set to a valid value by the caller,
    //! otherwise it is ignored.
    //! \param[in] forceSAT set to true to force using the SAT security protocol instead of passthrough ATA
    //! security commands.
    //! \return SUCCESS if the drive password is set successfully. FROZEN if ATA security is frozen.
    //! other values may be returned if some other failure occurs.
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1)
    OPENSEA_OPERATIONS_API eReturnValues run_Set_ATA_Security_Password(tDevice*            device,
                                                                       ataSecurityPassword ataPassword,
                                                                       bool                forceSATvalid,
                                                                       bool                forceSAT);

    //! \fn eReturnValues eReturnValues run_Unlock_ATA_Security(tDevice*   device,
    //!                                                                    ataSecurityPassword ataPassword,
    //!                                                                    bool                forceSATvalid,
    //!                                                                    bool                forceSAT)
    //! \brief This function handles all necessary steps to perform an ATA security unlock on a device.
    //! It will check current state, and run any necessary steps to unlock the security
    //! \param[in] device pointer to the device structure of the device to unlock
    //! \param[in] ataPassword structure with the ATA security password information to use
    //! \param[in] forceSATValid set to true to say the next variable is set to a valid value by the caller,
    //! otherwise it is ignored.
    //! \param[in] forceSAT set to true to force using the SAT security protocol instead of passthrough ATA
    //! security commands.
    //! \return SUCCESS if the drive unlock is successfully. FROZEN if ATA security is frozen.
    //! other values may be returned if some other failure occurs.
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1)
    OPENSEA_OPERATIONS_API eReturnValues run_Unlock_ATA_Security(tDevice*            device,
                                                                 ataSecurityPassword ataPassword,
                                                                 bool                forceSATvalid,
                                                                 bool                forceSAT);

    //! \fn eReturnValues run_Freeze_ATA_Security(tDevice* device, bool forceSATvalid, bool forceSAT)
    //! \brief This function handles all necessary steps to perform an ATA security freeze lock on a device.
    //! It will check current state, and run any necessary steps to freeze ATA security. Once frozen, other
    //! ATA security operations cannot be run until the drive has been power cycled.
    //! \param[in] device pointer to the device structure of the device to unlock
    //! \param[in] forceSATValid set to true to say the next variable is set to a valid value by the caller,
    //! otherwise it is ignored.
    //! \param[in] forceSAT set to true to force using the SAT security protocol instead of passthrough ATA
    //! security commands.
    //! \return SUCCESS if the drive freeze lock is successfully.
    //! other values may be returned if some other failure occurs.
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1)
    OPENSEA_OPERATIONS_API eReturnValues run_Freeze_ATA_Security(tDevice* device, bool forceSATvalid, bool forceSAT);

#if defined(__cplusplus)
}
#endif
