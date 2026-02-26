// SPDX-License-Identifier: MPL-2.0
//
// Do NOT modify or remove this copyright and license
//
// Copyright (c) 2021-2025 Seagate Technology LLC and/or its Affiliates, All Rights Reserved
//
// This software is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
//
// ******************************************************************************************
//
#pragma once

#include "common_types.h"
#include "memory_safety.h"
#include "operations_Common.h"
#include "type_conversion.h"

#if defined(__cplusplus)
extern "C"
{
#endif

    M_PARAM_RO(1) OPENSEA_OPERATIONS_API bool is_Persistent_Reservations_Supported(const tDevice* M_NONNULL device);

    // The enum below can be used to specify which type of reservation is used. NOTE: These are not defined to values
    // for NVMe or SCSI since they are different so that they can be translated as necessary for the device type.

    // Scopes defined for clarity in reporting, but not supported for actual use since scopes other than logical unit
    // are obsolete.
    typedef enum eReservationScopeEnum
    {
        RESERVATION_SCOPE_LOGICAL_UNIT,
        RESERVATION_SCOPE_EXTENT,  // obsolete (SPC only)
        RESERVATION_SCOPE_ELEMENT, // obsolete (SPC2 and SPC only)
        RESERVATION_SCOPE_UNKNOWN = 0xFF
    } eReservationScope;

    typedef enum eReservationTypeEnum
    {
        RES_TYPE_NO_RESERVATION,
        RES_TYPE_READ_SHARED, // obsolete - old SCSI only
        RES_TYPE_WRITE_EXCLUSIVE,
        RES_TYPE_READ_EXCLUSIVE, // obsolete - old SCSI only
        RES_TYPE_EXCLUSIVE_ACCESS,
        RES_TYPE_SHARED_ACCESS, // obsolete - old SCSI only
        RES_TYPE_WRITE_EXCLUSIVE_REGISTRANTS_ONLY,
        RES_TYPE_EXCLUSIVE_ACCESS_REGISTRANTS_ONLY,
        RES_TYPE_WRITE_EXCLUSIVE_ALL_REGISTRANTS,
        RES_TYPE_EXCLUSIVE_ACCESS_ALL_REGISTRANTS,
        RES_TYPE_UNKNOWN = 0xFF
    } eReservationType;

    typedef struct s_reservationTypesSupported
    {
        bool readShared; // marked as reserved in spec...but pretty sure it was reserved for this
        bool writeExclusive;
        bool readExclusive; // marked as reserved in spec...but pretty sure it was reserved for this
        bool exclusiveAccess;
        bool sharedAccess; // marked as reserved in spec...but pretty sure it was reserved for this
        bool writeExclusiveRegistrantsOnly;
        bool exclusiveAccessRegistrantsOnly;
        bool writeExclusiveAllRegistrants;
        bool exclusiveAccessAllRegistrants;
        // remaining are reserved, but making each a bool indicating SCSI mode number in case it expands in the future
        bool reserved9h;
        bool reservedAh;
        bool reservedBh;
        bool reservedCh;
        bool reservedDh;
        bool reservedEh;
        bool reservedFh;
    } reservationTypesSupported;

#define PERSISTENT_RESERVATION_CAPABILITIES_VERSION 1

    // WE = write exclusive
    // EA = exclusive access
    typedef enum eAllowedCommandDetailEnum
    {
        RES_CMD_ALLOWED_NO_INFO,
        RES_CMD_ALLOWED_WE_AND_EA,
        RES_CMD_NOT_ALLOWED_WE,
        RES_CMD_ALLOWED_WE,
        RES_CMD_PERSIST_ALLOWED_WE_AND_EA,
        RES_CMD_PERSIST_ALLOWED_WE,
        RES_CMD_UNKNOWN =
            0xFF, // for forward compatibility with values that are currently reserved at the time of writing this code.
    } eAllowedCommandDetail;

    typedef struct s_allowedCommands
    {
        uint8_t
            allowedCommandsRawValue; // 0 - 7 as reported by the drive in case the remaining info is not useful enough
        eAllowedCommandDetail testUnitReady;
        eAllowedCommandDetail modeSense;
        eAllowedCommandDetail readAttribute;
        eAllowedCommandDetail readBuffer10;
        eAllowedCommandDetail receiveDiagnosticResults;
        eAllowedCommandDetail reportSupportedOperationCodes;
        eAllowedCommandDetail reportSupportedTaskManagementFunctions;
        eAllowedCommandDetail readDefectData;
    } allowedCommands;

    typedef struct s_persistentReservationCapabilities
    {
        size_t          size;    // set to sizeof(persistentReservationCapabilities)
        uint32_t        version; // set to PERSISTENT_RESERVATION_CAPABILITIES_VERSION
        bool            replaceLostReservationCapable;
        bool            compatibleReservationHandling; // scsi2 reservation reelease/reserve exceptions
        bool            specifyInitiatorPortCapable;
        bool            allTargetPortsCapable;
        bool            persistThroughPowerLossCapable;
        allowedCommands allowedCommandsInfo;
        bool            persistThroughPowerLossActivated;
        bool reservationTypesSupportedValid; // If set to true, the device reported the type mask indicating which
                                             // reservation types are supported (below)
        reservationTypesSupported reservationsCapabilities;
    } persistentReservationCapabilities, *ptrPersistentReservationCapabilities;

    M_PARAM_RO(1)
    M_PARAM_RW(2)
    OPENSEA_OPERATIONS_API eReturnValues
    get_Persistent_Reservations_Capabilities(const tDevice* M_NONNULL                       device,
                                             ptrPersistentReservationCapabilities M_NONNULL prCapabilities);

    M_PARAM_RO(1)
    OPENSEA_OPERATIONS_API
    void show_Persistent_Reservations_Capabilities(ptrPersistentReservationCapabilities M_NONNULL prCapabilities);

    M_PARAM_RO(1)
    M_PARAM_WO(2)
    OPENSEA_OPERATIONS_API eReturnValues get_Registration_Key_Count(const tDevice* M_NONNULL device,
                                                                    uint16_t* M_NONNULL      keyCount);

#define REGISTRATION_KEY_DATA_VERSION 1

    typedef struct s_registrationKeysData
    {
        size_t                   size;
        uint32_t                 version;
        uint32_t                 generation;   // counter that updates each time new registration is added or removed
        uint16_t                 numberOfKeys; // number of keys reported below
        M_STRICT_FLEX_ARRAY_AUTO M_COUNTED_BY(numberOfKeys) uint64_t
            registrationKey[FLEX_ARRAY]; // This is variable sized depending on how many are requested to be read and
                                         // how many are filled in when read.
    } registrationKeysData, *ptrRegistrationKeysData;

    static M_INLINE void safe_free_registration_key_data(registrationKeysData * M_NULLABLE * M_NULLABLE regKeyData)
    {
        safe_free_core(M_REINTERPRET_CAST(void**, regKeyData));
    }

    M_PARAM_RO(1)
    M_PARAM_RW(3)
    OPENSEA_OPERATIONS_API eReturnValues get_Registration_Keys(const tDevice* M_NONNULL          device,
                                                               uint16_t                          numberOfKeys,
                                                               ptrRegistrationKeysData M_NONNULL keys);

    M_PARAM_RO(1) OPENSEA_OPERATIONS_API void show_Registration_Keys(ptrRegistrationKeysData M_NONNULL keys);

    M_PARAM_RO(1)
    M_PARAM_WO(2)
    OPENSEA_OPERATIONS_API eReturnValues get_Reservation_Count(const tDevice* M_NONNULL device,
                                                               uint16_t* M_NONNULL      reservationKeyCount);

    typedef struct s_reservationInfo
    {
        uint64_t          reservationKey;
        uint32_t          scopeSpecificAddress; // obsolete
        eReservationScope scope;
        eReservationType  type;
        uint16_t          extentLength; // obsolete
    } reservationInfo;

#define RESERVATION_DATA_VERSION 1

    typedef struct s_reservationsData
    {
        size_t   size;
        uint32_t version;
        uint32_t generation;
        uint16_t numberOfReservations; // will most likely be 0 or 1 since element and extent types are obsolete.
        M_STRICT_FLEX_ARRAY_AUTO M_COUNTED_BY(numberOfReservations)
            reservationInfo reservation[FLEX_ARRAY]; // variable length depending on how it was allocated. Should always
                                                     // be AT LEAST one of these
    } reservationsData, *ptrReservationsData;

    static M_INLINE void safe_free_reservation_data(reservationsData * M_NULLABLE * M_NULLABLE resData)
    {
        safe_free_core(M_REINTERPRET_CAST(void**, resData));
    }

    M_PARAM_RO(1)
    M_PARAM_RW(3)
    OPENSEA_OPERATIONS_API eReturnValues get_Reservations(const tDevice* M_NONNULL      device,
                                                          uint16_t                      numberReservations,
                                                          ptrReservationsData M_NONNULL reservations);

    M_PARAM_RO(1) OPENSEA_OPERATIONS_API void show_Reservations(ptrReservationsData M_NONNULL reservations);

    M_PARAM_RO(1)
    M_PARAM_WO(2)
    OPENSEA_OPERATIONS_API eReturnValues get_Full_Status_Key_Count(const tDevice* M_NONNULL device,
                                                                   uint16_t* M_NONNULL      keyCount);

// NOTE: This is 24 bytes as that is the common size. iSCSI is variable in size, so it
// will be truncated in this case -TJE
#define RESERVATION_KEY_TRANSPORT_ID_MAX_LEN 24

    typedef struct s_fullReservationKeyInfo
    {
        uint64_t          key;
        bool              allTargetPorts;
        bool              reservationHolder; // NOTE: This must be set to true for scope and type to be valid
        uint16_t          relativeTargetPortIdentifier;
        eReservationScope scope;
        eReservationType  type;
        uint32_t          transportIDLength;
        uint8_t           transportID[RESERVATION_KEY_TRANSPORT_ID_MAX_LEN];
    } fullReservationKeyInfo;

#define FULL_RESERVATION_INFO_VERSION 1

    typedef struct s_fullReservationInfo
    {
        size_t                   size;
        uint32_t                 version;
        uint32_t                 generation;
        uint16_t                 numberOfKeys;
        M_STRICT_FLEX_ARRAY_AUTO M_COUNTED_BY(numberOfKeys)
            fullReservationKeyInfo reservationKey[FLEX_ARRAY]; // Variable size depending on how many will be reported
                                                               // by the device at a given time.
    } fullReservationInfo, *ptrFullReservationInfo;

    static M_INLINE void safe_free_full_reservation_info(fullReservationInfo * M_NULLABLE * M_NULLABLE resInfo)
    {
        safe_free_core(M_REINTERPRET_CAST(void**, resInfo));
    }

    M_PARAM_RO(1)
    M_PARAM_RW(3)
    OPENSEA_OPERATIONS_API eReturnValues get_Full_Status(const tDevice* M_NONNULL         device,
                                                         uint16_t                         numberOfKeys,
                                                         ptrFullReservationInfo M_NONNULL fullReservation);

    M_PARAM_RO(1) OPENSEA_OPERATIONS_API void show_Full_Status(ptrFullReservationInfo M_NONNULL fullReservation);

    // note: ignore existing may not be supported on older devices.
    M_PARAM_RO(1)
    OPENSEA_OPERATIONS_API eReturnValues register_Key(const tDevice* M_NONNULL device,
                                                      uint64_t                 registrationKey,
                                                      bool                     allTargetPorts,
                                                      bool                     persistThroughPowerLoss,
                                                      bool                     ignoreExisting);

    M_PARAM_RO(1)
    OPENSEA_OPERATIONS_API eReturnValues unregister_Key(const tDevice* M_NONNULL device,
                                                        uint64_t                 currentRegistrationKey);

    M_PARAM_RO(1)
    OPENSEA_OPERATIONS_API eReturnValues acquire_Reservation(const tDevice* M_NONNULL device,
                                                             uint64_t                 key,
                                                             eReservationType         resType);

    M_PARAM_RO(1)
    OPENSEA_OPERATIONS_API eReturnValues release_Reservation(const tDevice* M_NONNULL device,
                                                             uint64_t                 key,
                                                             eReservationType         resType);

    M_PARAM_RO(1)
    OPENSEA_OPERATIONS_API eReturnValues clear_Reservations(const tDevice* M_NONNULL device, uint64_t key);

    M_PARAM_RO(1)
    OPENSEA_OPERATIONS_API eReturnValues preempt_Reservation(const tDevice* M_NONNULL device,
                                                             uint64_t                 key,
                                                             uint64_t                 preemptKey,
                                                             bool                     abort,
                                                             eReservationType         resType);

#if defined(__cplusplus)
}
#endif
