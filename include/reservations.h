//
// Do NOT modify or remove this copyright and license
//
// Copyright (c) 2021-2021 Seagate Technology LLC and/or its Affiliates, All Rights Reserved
//
// This software is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
//
// ******************************************************************************************
// 
#pragma once

#include "operations_Common.h"

#if defined (__cplusplus)
extern "C"
{
#endif

    //TODO: NVMe drives can support persistent reservations, but are not currently supported by this code.

    OPENSEA_OPERATIONS_API bool is_Persistent_Reservations_Supported(tDevice *device);

    //The enum below can be used to specify which type of reservation is used. NOTE: These are not defined to values for NVMe or SCSI since they are different so that
    //they can be translated as necessary for the device type.

    //Scopes defined for clarity in reporting, but not supported for actual use since scopes other than logical unit are obsolete.
    typedef enum _eReservationScope
    {
        RESERVATION_SCOPE_LOGICAL_UNIT,
        RESERVATION_SCOPE_EXTENT,//obsolete (SPC only)
        RESERVATION_SCOPE_ELEMENT,//obsolete (SPC2 and SPC only)
        RESERVATION_SCOPE_UNKNOWN = 0xFF
    }eReservationScope;

    typedef enum _eReservationType
    {
        RES_TYPE_NO_RESERVATION,
        RES_TYPE_READ_SHARED,//obsolete - old SCSI only
        RES_TYPE_WRITE_EXCLUSIVE,
        RES_TYPE_READ_EXCLUSIVE,//obsolete - old SCSI only
        RES_TYPE_EXCLUSIVE_ACCESS,
        RES_TYPE_SHARED_ACCESS,//obsolete - old SCSI only
        RES_TYPE_WRITE_EXCLUSIVE_REGISTRANTS_ONLY,
        RES_TYPE_EXCLUSIVE_ACCESS_REGISTRANTS_ONLY,
        RES_TYPE_WRITE_EXCLUSIVE_ALL_REGISTRANTS,
        RES_TYPE_EXCLUSIVE_ACCESS_ALL_REGISTRANTS,
        RES_TYPE_UNKNOWN = 0xFF
    }eReservationType;

    typedef struct _reservationTypesSupported
    {
        bool readShared;//marked as reserved in spec...but pretty sure it was reserved for this
        bool writeExclusive;
        bool readExclusive;//marked as reserved in spec...but pretty sure it was reserved for this
        bool exclusiveAccess;
        bool sharedAccess;//marked as reserved in spec...but pretty sure it was reserved for this
        bool writeExclusiveRegistrantsOnly;
        bool exclusiveAccessRegistrantsOnly;
        bool writeExclusiveAllRegistrants;
        bool exclusiveAccessAllRegistrants;
        //remaining are reserved, but making each a bool indicating SCSI mode number in case it expands in the future
        bool reserved9h;
        bool reservedAh;
        bool reservedBh;
        bool reservedCh;
        bool reservedDh;
        bool reservedEh;
        bool reservedFh;
    }reservationTypesSupported;

    #define PERSISTENT_RESERVATION_CAPABILITIES_VERSION 1

    //WE = write exclusive
    //EA = exclusive access
    typedef enum _eAllowedCommandDetail
    {
        RES_CMD_ALLOWED_NO_INFO,
        RES_CMD_ALLOWED_WE_AND_EA,
        RES_CMD_NOT_ALLOWED_WE,
        RES_CMD_ALLOWED_WE,
        RES_CMD_PERSIST_ALLOWED_WE_AND_EA,
        RES_CMD_PERSIST_ALLOWED_WE,
        RES_CMD_UNKNOWN = 0xFF,//for forward compatibility with values that are currently reserved at the time of writing this code.
    }eAllowedCommandDetail;

    typedef struct _allowedCommands
    {
        uint8_t allowedCommandsRawValue;//0 - 7 as reported by the drive in case the remaining info is not useful enough 
        eAllowedCommandDetail testUnitReady;
        eAllowedCommandDetail modeSense;
        eAllowedCommandDetail readAttribute;
        eAllowedCommandDetail readBuffer10;
        eAllowedCommandDetail receiveDiagnosticResults;
        eAllowedCommandDetail reportSupportedOperationCodes;
        eAllowedCommandDetail reportSupportedTaskManagementFunctions;
        eAllowedCommandDetail readDefectData;
    }allowedCommands;

    typedef struct _persistentReservationCapabilities
    {
        size_t size; //set to sizeof(persistentReservationCapabilities)
        uint32_t version;// set to PERSISTENT_RESERVATION_CAPABILITIES_VERSION
        bool replaceLostReservationCapable;
        bool compatibleReservationHandling;//scsi2 reservation reelease/reserve exceptions
        bool specifyInitiatorPortCapable;
        bool allTargetPortsCapable;
        bool persistThroughPowerLossCapable;
        allowedCommands allowedCommandsInfo;
        bool persistThroughPowerLossActivated;
        bool reservationTypesSupportedValid;//If set to true, the device reported the type mask indicating which reservation types are supported (below)
        reservationTypesSupported reservationsCapabilities;
    }persistentReservationCapabilities, *ptrPersistentReservationCapabilities;

    OPENSEA_OPERATIONS_API int get_Persistent_Reservations_Capabilities(tDevice *device, ptrPersistentReservationCapabilities prCapabilities);

    OPENSEA_OPERATIONS_API void show_Persistent_Reservations_Capabilities(ptrPersistentReservationCapabilities prCapabilities);

    OPENSEA_OPERATIONS_API int get_Registration_Key_Count(tDevice *device, uint16_t *keyCount);

    #define REGISTRATION_KEY_DATA_VERSION 1

    typedef struct _registrationKeysData
    {
        size_t size;
        uint32_t version;
        uint32_t generation;//counter that updates each time new registration is added or removed
        uint16_t numberOfKeys;//number of keys reported below
        uint64_t registrationKey[1];//This is variable sized depending on how many are requested to be read and how many are filled in when read. 
    }registrationKeysData, *ptrRegistrationKeysData;

    OPENSEA_OPERATIONS_API int get_Registration_Keys(tDevice *device, uint16_t numberOfKeys, ptrRegistrationKeysData keys);

    OPENSEA_OPERATIONS_API void show_Registration_Keys(ptrRegistrationKeysData keys);

    OPENSEA_OPERATIONS_API int get_Reservation_Count(tDevice *device, uint16_t *reservationKeyCount);

    typedef struct _reservationInfo
    {
        uint64_t reservationKey;
        uint32_t scopeSpecificAddress;//obsolete
        eReservationScope scope;
        eReservationType type;
        uint16_t extentLength;//obsolete
    }reservationInfo;

    #define RESERVATION_DATA_VERSION 1

    typedef struct _reservationsData
    {
        size_t size;
        uint32_t version;
        uint32_t generation;
        uint16_t numberOfReservations;//will most likely be 0 or 1 since element and extent types are obsolete.
        reservationInfo reservation[1];//variable length depending on how it was allocated. Should always be AT LEAST one of these
    }reservationsData, *ptrReservationsData;

    OPENSEA_OPERATIONS_API int get_Reservations(tDevice *device, uint16_t numberReservations, ptrReservationsData reservations);

    OPENSEA_OPERATIONS_API void show_Reservations(ptrReservationsData reservations);

    OPENSEA_OPERATIONS_API int get_Full_Status_Key_Count(tDevice *device, uint16_t *keyCount);

    typedef struct _fullReservationKeyInfo
    {
        uint64_t key;
        bool allTargetPorts;
        bool reservationHolder;//NOTE: This must be set to true for scope and type to be valid
        uint16_t relativeTargetPortIdentifier;
        eReservationScope scope;
        eReservationType type;
        uint32_t transportIDLength;
        uint8_t transportID[24];//NOTE: This is 24 bytes as that is the common size. iSCSI is variable in size, so it will be truncated in this case -TJE
    }fullReservationKeyInfo;

    #define FULL_RESERVATION_INFO_VERSION 1

    typedef struct _fullReservationInfo
    {
        size_t size;
        uint32_t version;
        uint32_t generation;
        uint16_t numberOfKeys;
        fullReservationKeyInfo reservationKey[1];//Variable size depending on how many will be reported by the device at a given time.
    }fullReservationInfo, *ptrFullReservationInfo;

    OPENSEA_OPERATIONS_API int get_Full_Status(tDevice *device, uint16_t numberOfKeys, ptrFullReservationInfo fullReservation);

    OPENSEA_OPERATIONS_API void show_Full_Status(ptrFullReservationInfo fullReservation);

    //note: ignore existing may not be supported on older devices.
    OPENSEA_OPERATIONS_API int register_Key(tDevice * device, uint64_t registrationKey, bool allTargetPorts, bool persistThroughPowerLoss, bool ignoreExisting);

    OPENSEA_OPERATIONS_API int unregister_Key(tDevice *device, uint64_t currentRegistrationKey);

    OPENSEA_OPERATIONS_API int acquire_Reservation(tDevice *device, uint64_t key, eReservationType resType);

    OPENSEA_OPERATIONS_API int release_Reservation(tDevice *device, uint64_t key, eReservationType resType);

    OPENSEA_OPERATIONS_API int clear_Reservations(tDevice *device, uint64_t key);

    OPENSEA_OPERATIONS_API int preempt_Reservation(tDevice *device, uint64_t key, uint64_t preemptKey, bool abort, eReservationType resType);

#if defined (__cplusplus)
}
#endif
