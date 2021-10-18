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
// \file reservations.c   SCSI reservations based functions. NOTE: THis is SCSI-3 persistent reservations only at this time - TJE

//NOTE: Old reservations from SCSI2 (and up to SPC2) are not currently supported. These are much more limited than what is available in the Persistent Reservations feature
//NOTE: Some additional enhancement for NVMe is probably possible. The current NVMe implementation is more-or-less what is part of NVMe to SCSI translation. - TJE

#include "reservations.h"
#include "scsi_helper.h"
#include "scsi_helper_func.h"

bool is_Persistent_Reservations_Supported(tDevice *device)
{
    bool supported = false;
    if (device->drive_info.drive_type == SCSI_DRIVE)
    {
        //try a persistent reserve in with no data transferred. This SHOULD not return an error if this feature is supported - TJE
        if (SUCCESS == scsi_Persistent_Reserve_In(device, SCSI_PERSISTENT_RESERVE_IN_READ_KEYS, 0, NULL))
        {
            supported = true;
        }
    }
#if !defined (DISABLE_NVME_PASSTHROUGH)
    else if (device->drive_info.drive_type == NVME_DRIVE)
    {
        //Controller identify says if the commands are supported
        //NS identify says which types of reservations are supported...should have both non-zero if supported - TJE
        if (device->drive_info.IdentifyData.nvme.ctrl.oncs & BIT5 && device->drive_info.IdentifyData.nvme.ns.rescap > 0)
        {
            supported = true;
        }
    }
#endif
    return supported;
}

typedef struct _reservationTypesSupportedV1
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
}reservationTypesSupportedV1;

typedef enum _eAllowedCommandDetailV1
{
    RES_CMD_ALLOWED_NO_INFO_V1,
    RES_CMD_ALLOWED_WE_AND_EA_V1,
    RES_CMD_NOT_ALLOWED_WE_V1,
    RES_CMD_ALLOWED_WE_V1,
    RES_CMD_PERSIST_ALLOWED_WE_AND_EA_V1,
    RES_CMD_PERSIST_ALLOWED_WE_V1,
}eAllowedCommandDetailV1;

typedef struct _allowedCommandsV1
{
    uint8_t allowedCommandsRawValue;//0 - 7 as reported by the drive in case the remaining info is not useful enough 
    eAllowedCommandDetailV1 testUnitReady;
    eAllowedCommandDetailV1 modeSense;
    eAllowedCommandDetailV1 readAttribute;
    eAllowedCommandDetailV1 readBuffer10;
    eAllowedCommandDetailV1 receiveDiagnosticResults;
    eAllowedCommandDetailV1 reportSupportedOperationCodes;
    eAllowedCommandDetailV1 reportSupportedTaskManagementFunctions;
    eAllowedCommandDetailV1 readDefectData;
}allowedCommandsV1;

typedef struct _persistentReservationCapabilitiesV1
{
    size_t size;
    uint32_t version;
    bool replaceLostReservationCapable;
    bool compatibleReservationHandling;//scsi2 reservation reelease/reserve exceptions
    bool specifyInitiatorPortCapable;
    bool allTargetPortsCapable;
    bool persistThroughPowerLossCapable;
    allowedCommandsV1 allowedCommandsInfo;
    bool persistThroughPowerLossActivated;
    bool reservationTypesSupportedValid;//If set to true, the device reported the type mask indicating which reservation types are supported (below)
    reservationTypesSupported reservationsCapabilities;
}persistentReservationCapabilitiesV1, *ptrPersistentReservationCapabilitiesV1;

#define PERSISTENT_RESERVATION_CAPABILITIES_VERSION_V1 1

int get_Persistent_Reservations_Capabilities(tDevice *device, ptrPersistentReservationCapabilities prCapabilities)
{
    //note: some older drives don't support report capabilities...need to figure out what to do about those - TJE
    int ret = NOT_SUPPORTED;
    if (!prCapabilities)
    {
        return BAD_PARAMETER;
    }
    if (!(prCapabilities->version >= PERSISTENT_RESERVATION_CAPABILITIES_VERSION_V1 && prCapabilities->size >= sizeof(persistentReservationCapabilitiesV1)))
    {
        return BAD_PARAMETER;
    }
    if (device->drive_info.drive_type == SCSI_DRIVE)
    {
        uint8_t capabilities[8] = { 0 };
        //todo: should this just be sent, or should it have a scsi version check???
        if (SUCCESS == (ret = scsi_Persistent_Reserve_In(device, SCSI_PERSISTENT_RESERVE_IN_REPORT_CAPABILITIES, 8, capabilities)))
        {
            uint16_t prCapabilitiesLength = M_BytesTo2ByteValue(capabilities[0], capabilities[1]);
            if (prCapabilitiesLength >= 8)
            {
                if (capabilities[2] & BIT7)
                {
                    prCapabilities->replaceLostReservationCapable = true;
                }
                else
                {
                    prCapabilities->replaceLostReservationCapable = false;
                }
                if (capabilities[2] & BIT4)
                {
                    prCapabilities->compatibleReservationHandling = true;
                }
                else
                {
                    prCapabilities->compatibleReservationHandling = false;
                }
                if (capabilities[2] & BIT3)
                {
                    prCapabilities->specifyInitiatorPortCapable = true;
                }
                else
                {
                    prCapabilities->specifyInitiatorPortCapable = false;
                }
                if (capabilities[2] & BIT2)
                {
                    prCapabilities->allTargetPortsCapable = true;
                }
                else
                {
                    prCapabilities->allTargetPortsCapable = false;
                }
                if (capabilities[2] & BIT0)
                {
                    prCapabilities->persistThroughPowerLossCapable = true;
                }
                else
                {
                    prCapabilities->persistThroughPowerLossCapable = false;
                }
                prCapabilities->allowedCommandsInfo.allowedCommandsRawValue = M_GETBITRANGE(capabilities[3], 6, 4);
                switch (prCapabilities->allowedCommandsInfo.allowedCommandsRawValue)
                {
                case 0:
                    //no info for any
                    prCapabilities->allowedCommandsInfo.testUnitReady = RES_CMD_ALLOWED_NO_INFO;
                    prCapabilities->allowedCommandsInfo.modeSense = RES_CMD_ALLOWED_NO_INFO;
                    prCapabilities->allowedCommandsInfo.readAttribute = RES_CMD_ALLOWED_NO_INFO;
                    prCapabilities->allowedCommandsInfo.readBuffer10 = RES_CMD_ALLOWED_NO_INFO;
                    prCapabilities->allowedCommandsInfo.receiveDiagnosticResults = RES_CMD_ALLOWED_NO_INFO;
                    prCapabilities->allowedCommandsInfo.reportSupportedOperationCodes = RES_CMD_ALLOWED_NO_INFO;
                    prCapabilities->allowedCommandsInfo.reportSupportedTaskManagementFunctions = RES_CMD_ALLOWED_NO_INFO;
                    prCapabilities->allowedCommandsInfo.readDefectData = RES_CMD_ALLOWED_NO_INFO;
                    break;
                case 1:
                    //test unit ready, no further info
                    prCapabilities->allowedCommandsInfo.testUnitReady = RES_CMD_ALLOWED_WE_AND_EA;
                    prCapabilities->allowedCommandsInfo.modeSense = RES_CMD_ALLOWED_NO_INFO;
                    prCapabilities->allowedCommandsInfo.readAttribute = RES_CMD_ALLOWED_NO_INFO;
                    prCapabilities->allowedCommandsInfo.readBuffer10 = RES_CMD_ALLOWED_NO_INFO;
                    prCapabilities->allowedCommandsInfo.receiveDiagnosticResults = RES_CMD_ALLOWED_NO_INFO;
                    prCapabilities->allowedCommandsInfo.reportSupportedOperationCodes = RES_CMD_ALLOWED_NO_INFO;
                    prCapabilities->allowedCommandsInfo.reportSupportedTaskManagementFunctions = RES_CMD_ALLOWED_NO_INFO;
                    prCapabilities->allowedCommandsInfo.readDefectData = RES_CMD_ALLOWED_NO_INFO;
                    break;
                case 2:
                    //test unit ready supported, all others not allowed.
                    prCapabilities->allowedCommandsInfo.testUnitReady = RES_CMD_ALLOWED_WE_AND_EA;
                    prCapabilities->allowedCommandsInfo.modeSense = RES_CMD_NOT_ALLOWED_WE;
                    prCapabilities->allowedCommandsInfo.readAttribute = RES_CMD_NOT_ALLOWED_WE;
                    prCapabilities->allowedCommandsInfo.readBuffer10 = RES_CMD_NOT_ALLOWED_WE;
                    prCapabilities->allowedCommandsInfo.receiveDiagnosticResults = RES_CMD_NOT_ALLOWED_WE;
                    prCapabilities->allowedCommandsInfo.reportSupportedOperationCodes = RES_CMD_NOT_ALLOWED_WE;
                    prCapabilities->allowedCommandsInfo.reportSupportedTaskManagementFunctions = RES_CMD_NOT_ALLOWED_WE;
                    prCapabilities->allowedCommandsInfo.readDefectData = RES_CMD_NOT_ALLOWED_WE;
                    break;
                case 3:
                    //test unit ready supported, all others allowed in WE.
                    prCapabilities->allowedCommandsInfo.testUnitReady = RES_CMD_ALLOWED_WE_AND_EA;
                    prCapabilities->allowedCommandsInfo.modeSense = RES_CMD_ALLOWED_WE;
                    prCapabilities->allowedCommandsInfo.readAttribute = RES_CMD_ALLOWED_WE;
                    prCapabilities->allowedCommandsInfo.readBuffer10 = RES_CMD_ALLOWED_WE;
                    prCapabilities->allowedCommandsInfo.receiveDiagnosticResults = RES_CMD_ALLOWED_WE;
                    prCapabilities->allowedCommandsInfo.reportSupportedOperationCodes = RES_CMD_ALLOWED_WE;
                    prCapabilities->allowedCommandsInfo.reportSupportedTaskManagementFunctions = RES_CMD_ALLOWED_WE;
                    prCapabilities->allowedCommandsInfo.readDefectData = RES_CMD_ALLOWED_WE;
                    break;
                case 4:
                    //same as 3, but persistent
                    prCapabilities->allowedCommandsInfo.testUnitReady = RES_CMD_PERSIST_ALLOWED_WE_AND_EA;
                    prCapabilities->allowedCommandsInfo.modeSense = RES_CMD_PERSIST_ALLOWED_WE;
                    prCapabilities->allowedCommandsInfo.readAttribute = RES_CMD_PERSIST_ALLOWED_WE;
                    prCapabilities->allowedCommandsInfo.readBuffer10 = RES_CMD_PERSIST_ALLOWED_WE;
                    prCapabilities->allowedCommandsInfo.receiveDiagnosticResults = RES_CMD_PERSIST_ALLOWED_WE;
                    prCapabilities->allowedCommandsInfo.reportSupportedOperationCodes = RES_CMD_PERSIST_ALLOWED_WE;
                    prCapabilities->allowedCommandsInfo.reportSupportedTaskManagementFunctions = RES_CMD_PERSIST_ALLOWED_WE;
                    prCapabilities->allowedCommandsInfo.readDefectData = RES_CMD_PERSIST_ALLOWED_WE;
                    break;
                case 5:
                    //a few more commands than 4 in WE and EA
                    prCapabilities->allowedCommandsInfo.testUnitReady = RES_CMD_PERSIST_ALLOWED_WE_AND_EA;
                    prCapabilities->allowedCommandsInfo.modeSense = RES_CMD_PERSIST_ALLOWED_WE;
                    prCapabilities->allowedCommandsInfo.readAttribute = RES_CMD_PERSIST_ALLOWED_WE;
                    prCapabilities->allowedCommandsInfo.readBuffer10 = RES_CMD_PERSIST_ALLOWED_WE;
                    prCapabilities->allowedCommandsInfo.receiveDiagnosticResults = RES_CMD_PERSIST_ALLOWED_WE;
                    prCapabilities->allowedCommandsInfo.reportSupportedOperationCodes = RES_CMD_PERSIST_ALLOWED_WE_AND_EA;
                    prCapabilities->allowedCommandsInfo.reportSupportedTaskManagementFunctions = RES_CMD_PERSIST_ALLOWED_WE_AND_EA;
                    prCapabilities->allowedCommandsInfo.readDefectData = RES_CMD_PERSIST_ALLOWED_WE;
                    break;
                default:
                    prCapabilities->allowedCommandsInfo.testUnitReady = RES_CMD_UNKNOWN;
                    prCapabilities->allowedCommandsInfo.modeSense = RES_CMD_UNKNOWN;
                    prCapabilities->allowedCommandsInfo.readAttribute = RES_CMD_UNKNOWN;
                    prCapabilities->allowedCommandsInfo.readBuffer10 = RES_CMD_UNKNOWN;
                    prCapabilities->allowedCommandsInfo.receiveDiagnosticResults = RES_CMD_UNKNOWN;
                    prCapabilities->allowedCommandsInfo.reportSupportedOperationCodes = RES_CMD_UNKNOWN;
                    prCapabilities->allowedCommandsInfo.reportSupportedTaskManagementFunctions = RES_CMD_UNKNOWN;
                    prCapabilities->allowedCommandsInfo.readDefectData = RES_CMD_UNKNOWN;
                    break;
                }

                if (capabilities[3] & BIT0)
                {
                    prCapabilities->persistThroughPowerLossActivated = true;
                }
                else
                {
                    prCapabilities->persistThroughPowerLossActivated = false;
                }
                if (capabilities[3] & BIT7)
                {
                    prCapabilities->reservationTypesSupportedValid = true;
                    if (capabilities[4] & BIT0)
                    {
                        prCapabilities->reservationsCapabilities.readShared = true;
                    }
                    if (capabilities[4] & BIT1)
                    {
                        prCapabilities->reservationsCapabilities.writeExclusive = true;
                    }
                    if (capabilities[4] & BIT2)
                    {
                        prCapabilities->reservationsCapabilities.readExclusive = true;
                    }
                    if (capabilities[4] & BIT3)
                    {
                        prCapabilities->reservationsCapabilities.exclusiveAccess = true;
                    }
                    if (capabilities[4] & BIT4)
                    {
                        prCapabilities->reservationsCapabilities.sharedAccess = true;
                    }
                    if (capabilities[4] & BIT5)
                    {
                        prCapabilities->reservationsCapabilities.writeExclusiveRegistrantsOnly = true;
                    }
                    if (capabilities[4] & BIT6)
                    {
                        prCapabilities->reservationsCapabilities.exclusiveAccessRegistrantsOnly = true;
                    }
                    if (capabilities[4] & BIT7)
                    {
                        prCapabilities->reservationsCapabilities.writeExclusiveAllRegistrants = true;
                    }
                    if (capabilities[5] & BIT0)
                    {
                        prCapabilities->reservationsCapabilities.exclusiveAccessAllRegistrants = true;
                    }
                    if (capabilities[5] & BIT1)
                    {
                        prCapabilities->reservationsCapabilities.reserved9h = true;
                    }
                    if (capabilities[5] & BIT2)
                    {
                        prCapabilities->reservationsCapabilities.reservedAh = true;
                    }
                    if (capabilities[5] & BIT3)
                    {
                        prCapabilities->reservationsCapabilities.reservedBh = true;
                    }
                    if (capabilities[5] & BIT4)
                    {
                        prCapabilities->reservationsCapabilities.reservedCh = true;
                    }
                    if (capabilities[5] & BIT5)
                    {
                        prCapabilities->reservationsCapabilities.reservedDh = true;
                    }
                    if (capabilities[5] & BIT6)
                    {
                        prCapabilities->reservationsCapabilities.reservedEh = true;
                    }
                    if (capabilities[5] & BIT7)
                    {
                        prCapabilities->reservationsCapabilities.reservedFh = true;
                    }
                }
                else
                {
                    prCapabilities->reservationTypesSupportedValid = false;
                    memset(&prCapabilities->reservationsCapabilities, 0, sizeof(reservationTypesSupported));
                }
            }
            else
            {
                ret = UNKNOWN;
            }
        }
    }
#if !defined (DISABLE_NVME_PASSTHROUGH)
    else if (device->drive_info.drive_type == NVME_DRIVE)
    {
        nvmeFeaturesCmdOpt getReservatinPersistence;
        memset(&getReservatinPersistence, 0, sizeof(nvmeFeaturesCmdOpt));
        getReservatinPersistence.fid = NVME_FEAT_RESERVATION_PERSISTANCE_;
        getReservatinPersistence.nsid = device->drive_info.namespaceID;
        prCapabilities->replaceLostReservationCapable = true;//not in NVMe translation, but seems to be part of NVMe spec
        prCapabilities->compatibleReservationHandling = false;
        prCapabilities->specifyInitiatorPortCapable = false;
        prCapabilities->allTargetPortsCapable = true;
        prCapabilities->persistThroughPowerLossCapable = M_ToBool(device->drive_info.IdentifyData.nvme.ns.rescap & BIT0);
        //need to do get features to figure out if persist through power loss activated is true or false
        prCapabilities->allowedCommandsInfo.allowedCommandsRawValue = 0;
        prCapabilities->allowedCommandsInfo.testUnitReady = RES_CMD_ALLOWED_NO_INFO;
        prCapabilities->allowedCommandsInfo.modeSense = RES_CMD_ALLOWED_NO_INFO;
        prCapabilities->allowedCommandsInfo.readAttribute = RES_CMD_ALLOWED_NO_INFO;
        prCapabilities->allowedCommandsInfo.readBuffer10 = RES_CMD_ALLOWED_NO_INFO;
        prCapabilities->allowedCommandsInfo.receiveDiagnosticResults = RES_CMD_ALLOWED_NO_INFO;
        prCapabilities->allowedCommandsInfo.reportSupportedOperationCodes = RES_CMD_ALLOWED_NO_INFO;
        prCapabilities->allowedCommandsInfo.reportSupportedTaskManagementFunctions = RES_CMD_ALLOWED_NO_INFO;
        prCapabilities->allowedCommandsInfo.readDefectData = RES_CMD_ALLOWED_NO_INFO;
        prCapabilities->reservationTypesSupportedValid = true;
        prCapabilities->reservationsCapabilities.readShared = false;
        prCapabilities->reservationsCapabilities.writeExclusive = M_ToBool(device->drive_info.IdentifyData.nvme.ns.rescap & BIT1);
        prCapabilities->reservationsCapabilities.readExclusive = false;
        prCapabilities->reservationsCapabilities.exclusiveAccess = M_ToBool(device->drive_info.IdentifyData.nvme.ns.rescap & BIT2);
        prCapabilities->reservationsCapabilities.sharedAccess = false;
        prCapabilities->reservationsCapabilities.writeExclusiveRegistrantsOnly = M_ToBool(device->drive_info.IdentifyData.nvme.ns.rescap & BIT3);
        prCapabilities->reservationsCapabilities.exclusiveAccessRegistrantsOnly = M_ToBool(device->drive_info.IdentifyData.nvme.ns.rescap & BIT4);
        prCapabilities->reservationsCapabilities.writeExclusiveAllRegistrants = M_ToBool(device->drive_info.IdentifyData.nvme.ns.rescap & BIT5);
        prCapabilities->reservationsCapabilities.exclusiveAccessAllRegistrants = M_ToBool(device->drive_info.IdentifyData.nvme.ns.rescap & BIT6);
        prCapabilities->reservationsCapabilities.reserved9h = false;
        prCapabilities->reservationsCapabilities.reservedAh = false;
        prCapabilities->reservationsCapabilities.reservedBh = false;
        prCapabilities->reservationsCapabilities.reservedCh = false;
        prCapabilities->reservationsCapabilities.reservedDh = false;
        prCapabilities->reservationsCapabilities.reservedEh = false;
        prCapabilities->reservationsCapabilities.reservedFh = false;
        if (SUCCESS == (ret = nvme_Get_Features(device, &getReservatinPersistence)))
        {
            //in dw0 completion
            prCapabilities->persistThroughPowerLossActivated = M_ToBool(device->drive_info.lastNVMeResult.lastNVMeCommandSpecific & BIT0);
        }
    }
#endif
    return ret;
}

static void show_Allowed_Commands_Value(eAllowedCommandDetail value)
{
    switch (value)
    {
    case RES_CMD_ALLOWED_NO_INFO:
        printf("No Information\n");
        break;
    case RES_CMD_ALLOWED_WE_AND_EA:
        printf("Allowed in write exclusive and exclusive access\n");
        break;
    case RES_CMD_NOT_ALLOWED_WE:
        printf("Not allowed in write exclusive\n");
        break;
    case RES_CMD_ALLOWED_WE:
        printf("Allowed in write exclusive\n");
        break;
    case RES_CMD_PERSIST_ALLOWED_WE_AND_EA:
        printf("Allowed in persistent write exclusive and persistent exclusive\n");
        break;
    case RES_CMD_PERSIST_ALLOWED_WE:
        printf("Allowed in persistent write exclusive\n");
        break;
    case RES_CMD_UNKNOWN:
    default:
        printf("Unknown\n");
        break;
    }
    return;
}

void show_Persistent_Reservations_Capabilities(ptrPersistentReservationCapabilities prCapabilities)
{
    if (prCapabilities)
    {
        if ((prCapabilities->version >= PERSISTENT_RESERVATION_CAPABILITIES_VERSION_V1 && prCapabilities->size >= sizeof(persistentReservationCapabilitiesV1)))
        {
            printf("\nPersistent Reservations Capabilities:\n");
            printf("=====================================\n");
            printf("\tReplace Lost Reservations Capable: ");
            if (prCapabilities->replaceLostReservationCapable)
            {
                printf("supported\n");
            }
            else
            {
                printf("not supported\n");
            }
            printf("\tCompatible Reservation Handling: ");
            if(prCapabilities->compatibleReservationHandling)
            {
                printf("supported\n");
            }
            else
            {
                printf("not supported\n");
            }
            printf("\tSpecify Initiator Port Capable: ");
            if(prCapabilities->specifyInitiatorPortCapable)
            {
                printf("supported\n");
            }
            else
            {
                printf("not supported\n");
            }
            printf("\tAll Target Ports Capable: ");
            if (prCapabilities->allTargetPortsCapable)
            {
                printf("supported\n");
            }
            else
            {
                printf("not supported\n");
            }
            printf("\tPersist Through Power Loss Capable: ");
            if(prCapabilities->persistThroughPowerLossCapable)
            {
                printf("supported\n");
            }
            else
            {
                printf("not supported\n");
            }
            printf("\tPersist Through Power Loss Activated: ");
            if (prCapabilities->persistThroughPowerLossActivated)
            {
                printf("Enabled\n");
            }
            else
            {
                printf("Disabled\n");
            }
            if (prCapabilities->reservationTypesSupportedValid)
            {
                printf("\n\tSupported Reservation Types:\n");
                printf("\t----------------------------\n");
                if (prCapabilities->reservationsCapabilities.readShared)
                {
                    printf("\t\tRead Shared\n");
                }
                if (prCapabilities->reservationsCapabilities.writeExclusive)
                {
                    printf("\t\tWrite Exclusive\n");
                }
                if (prCapabilities->reservationsCapabilities.readExclusive)
                {
                    printf("\t\tRead Exclusive\n");
                }
                if (prCapabilities->reservationsCapabilities.exclusiveAccess)
                {
                    printf("\t\tExclusive Access\n");
                }
                if (prCapabilities->reservationsCapabilities.sharedAccess)
                {
                    printf("\t\tShared Access\n");
                }
                if (prCapabilities->reservationsCapabilities.writeExclusiveRegistrantsOnly)
                {
                    printf("\t\tWrite Exclusive - Registrants Only\n");
                }
                if (prCapabilities->reservationsCapabilities.exclusiveAccessRegistrantsOnly)
                {
                    printf("\t\tExclusive Access - Registrants Only\n");
                }
                if (prCapabilities->reservationsCapabilities.writeExclusiveAllRegistrants)
                {
                    printf("\t\tWrite Exclusive - All Registrants\n");
                }
                if (prCapabilities->reservationsCapabilities.exclusiveAccessAllRegistrants)
                {
                    printf("\t\tExclusive Access - All Registrants\n");
                }
            }
            else
            {
                printf("\tDevice does not report supported reservation types.\n");
            }
            //TODO: Allowed commands...not sure how to output this nicely at this time.
            if (prCapabilities->allowedCommandsInfo.allowedCommandsRawValue < 6)//restricted like this since this is reserved at the time of writing this code - TJE
            {
                printf("\n\tAllowed Commands Info:\n");
                printf("\t----------------------\n");
                printf("\t\tTest Unit Ready: ");
                show_Allowed_Commands_Value(prCapabilities->allowedCommandsInfo.testUnitReady);
                printf("\t\tMode Sense: ");
                show_Allowed_Commands_Value(prCapabilities->allowedCommandsInfo.modeSense);
                printf("\t\tRead Attribute: ");
                show_Allowed_Commands_Value(prCapabilities->allowedCommandsInfo.readAttribute);
                printf("\t\tRead Buffer (10): ");
                show_Allowed_Commands_Value(prCapabilities->allowedCommandsInfo.readBuffer10);
                printf("\t\tReceive Diagnostic Results: ");
                show_Allowed_Commands_Value(prCapabilities->allowedCommandsInfo.receiveDiagnosticResults);
                printf("\t\tReport Supported Operation Codes: ");
                show_Allowed_Commands_Value(prCapabilities->allowedCommandsInfo.reportSupportedOperationCodes);
                printf("\t\tReport Supported Task Management Functions: ");
                show_Allowed_Commands_Value(prCapabilities->allowedCommandsInfo.reportSupportedTaskManagementFunctions);
                printf("\t\tRead Defect Data: ");
                show_Allowed_Commands_Value(prCapabilities->allowedCommandsInfo.readDefectData);
            }
            else
            {
                printf("\tAllowed Commands: Not Reportable. Reserved value reported: %" PRIu8 "\n", prCapabilities->allowedCommandsInfo.allowedCommandsRawValue);
            }
        }
        else
        {
            printf("Error: Incorrect reservations capabilities structure version or bad structure size.\n");
        }
    }
    return;
}

int get_Registration_Key_Count(tDevice *device, uint16_t *keyCount)
{
    int ret = NOT_SUPPORTED;
    if (!keyCount)
    {
        return BAD_PARAMETER;
    }
    if (device->drive_info.drive_type == SCSI_DRIVE)
    {
        uint8_t readKeyCount[8] = { 0 };
        if (SUCCESS == (ret = scsi_Persistent_Reserve_In(device, SCSI_PERSISTENT_RESERVE_IN_READ_KEYS, 8, readKeyCount)))
        {
            *keyCount = M_BytesTo4ByteValue(readKeyCount[4], readKeyCount[5], readKeyCount[6], readKeyCount[7]) / 8;//each registered key is 8 bytes in length
        }
    }
#if !defined (DISABLE_NVME_PASSTHROUGH)
    else if (device->drive_info.drive_type == NVME_DRIVE)
    {
        uint8_t readKeyCount[24] = { 0 };//may be able to get away with only 8 bytes, but this read up until the list begins - TJE
        if (SUCCESS == (ret = nvme_Reservation_Report(device, false, readKeyCount, 24)))
        {
            *keyCount = M_BytesTo2ByteValue(readKeyCount[6], readKeyCount[5]);
        }
    }
#endif
    return ret;
}

#define REGISTRATION_KEY_DATA_VERSION_V1 1

typedef struct _registrationKeysDataV1
{
    size_t size;
    uint32_t version;
    uint32_t generation;//counter that updates each time new registration is added or removed
    uint16_t numberOfKeys;//number of keys reported below
    uint64_t registrationKey[1];//This is variable sized depending on how many are requested to be read and how many are filled in when read. 
}registrationKeysDataV1, *ptrRegistrationKeysDataV1;

int get_Registration_Keys(tDevice *device, uint16_t numberOfKeys, ptrRegistrationKeysData keys)
{
    //get only registration keys
    int ret = NOT_SUPPORTED;
    if (!keys)
    {
        return BAD_PARAMETER;
    }
    if (!(keys->version >= REGISTRATION_KEY_DATA_VERSION_V1) && !(keys->size >= sizeof(registrationKeysDataV1)))
    {
        return BAD_PARAMETER;
    }
    if (device->drive_info.drive_type == SCSI_DRIVE)
    {
        uint16_t dataLength = (numberOfKeys * 8) + 8;
        uint8_t *registrationKeys = C_CAST(uint8_t*, calloc_aligned(dataLength, sizeof(uint8_t), device->os_info.minimumAlignment));
        if (!registrationKeys)
        {
            return MEMORY_FAILURE;
        }
        if (SUCCESS == (ret = scsi_Persistent_Reserve_In(device, SCSI_PERSISTENT_RESERVE_IN_READ_KEYS, dataLength, registrationKeys)))
        {
            uint16_t reportedKeys = M_BytesTo4ByteValue(registrationKeys[4], registrationKeys[5], registrationKeys[6], registrationKeys[7]) / 8;//each registered key is 8 bytes in length
            keys->generation = M_BytesTo4ByteValue(registrationKeys[0], registrationKeys[1], registrationKeys[2], registrationKeys[3]);
            //loop through and save each key
            keys->numberOfKeys = 0;
            for (uint32_t keyIter = 0, offset = 8; keyIter < reportedKeys && keyIter < numberOfKeys && offset < dataLength; ++keys->numberOfKeys, ++keyIter, offset += 8)
            {
                keys->registrationKey[keyIter] = M_BytesTo8ByteValue(registrationKeys[offset + 0], registrationKeys[offset + 1], registrationKeys[offset + 2], registrationKeys[offset + 3], registrationKeys[offset + 4], registrationKeys[offset + 5], registrationKeys[offset + 6], registrationKeys[offset + 7]);
            }
        }
        safe_Free_aligned(registrationKeys)
    }
#if !defined (DISABLE_NVME_PASSTHROUGH)
    else if (device->drive_info.drive_type == NVME_DRIVE)
    {
        uint32_t dataLength = (numberOfKeys * 24) + 24;//24 byte header, then 24 bytes per key....if extended, then it is even larger.
        uint8_t *registrationKeys = C_CAST(uint8_t*, calloc_aligned(dataLength, sizeof(uint8_t), device->os_info.minimumAlignment));
        if (!registrationKeys)
        {
            return MEMORY_FAILURE;
        }
        keys->numberOfKeys = 0;
        if (SUCCESS == (ret = nvme_Reservation_Report(device, false, registrationKeys, dataLength)))
        {
            uint16_t reportedKeys = M_BytesTo2ByteValue(registrationKeys[6], registrationKeys[5]);
            keys->generation = M_BytesTo4ByteValue(registrationKeys[3], registrationKeys[2], registrationKeys[1], registrationKeys[0]);
            //loop through and save each key
            for (uint32_t keyIter = 0, offset = 24; keyIter < reportedKeys && keyIter < numberOfKeys && offset < dataLength; ++keys->numberOfKeys, ++keyIter, offset += 24)
            {
                keys->registrationKey[keyIter] = M_BytesTo8ByteValue(registrationKeys[offset + 23], registrationKeys[offset + 22], registrationKeys[offset + 21], registrationKeys[offset + 20], registrationKeys[offset + 19], registrationKeys[offset + 18], registrationKeys[offset + 17], registrationKeys[offset + 16]);
            }
        }
        safe_Free_aligned(registrationKeys)
    }
#endif
    return ret;
}

void show_Registration_Keys(ptrRegistrationKeysData keys)
{
    if (keys && keys->version >= REGISTRATION_KEY_DATA_VERSION_V1 && keys->size >= sizeof(registrationKeysDataV1))
    {
        printf("\nRegistration Keys:\n");
        printf("==================\n");
        printf("Generation: %" PRIu32 "\n", keys->generation);
        printf("------------------------\n");
        for (uint32_t keyIter = 0; keyIter < UINT16_MAX && keyIter < keys->numberOfKeys; ++keyIter)
        {
            printf("%016" PRIX64 "\n", keys->registrationKey[keyIter]);
        }
        if (keys->numberOfKeys == 0)
        {
            printf("No registration keys to report.\n");
        }
    }
}

//If supporting "extents", multiple can be reported, but this capability is obsolete, so this will likely return 1 or 0
int get_Reservation_Count(tDevice *device, uint16_t *reservationKeyCount)
{
    //get only reservations
    int ret = NOT_SUPPORTED;
    if (!reservationKeyCount)
    {
        return BAD_PARAMETER;
    }
    if (device->drive_info.drive_type == SCSI_DRIVE)
    {
        uint8_t reservationKeys[8] = { 0 };
        if (SUCCESS == (ret = scsi_Persistent_Reserve_In(device, SCSI_PERSISTENT_RESERVE_IN_READ_RESERVATION, 8, reservationKeys)))
        {
            *reservationKeyCount = M_BytesTo4ByteValue(reservationKeys[4], reservationKeys[5], reservationKeys[6], reservationKeys[7]) / 16;
        }
    }
#if !defined (DISABLE_NVME_PASSTHROUGH)
    else if (device->drive_info.drive_type == NVME_DRIVE)
    {
        uint8_t readKeyCount[24] = { 0 };//may be able to get away with only 8 bytes, but this read up until the list begins - TJE
        if (SUCCESS == (ret = nvme_Reservation_Report(device, false, readKeyCount, 24)))
        {
            if (readKeyCount[4] > 0)
            {
                *reservationKeyCount = 1;
            }
            else
            {
                *reservationKeyCount = 0;
            }
        }
    }
#endif
    return ret;
}

typedef struct _reservationInfoV1
{
    uint64_t reservationKey;
    uint32_t scopeSpecificAddress;//obsolete
    eReservationScope scope;
    eReservationType type;
    uint16_t extentLength;//obsolete
}reservationInfoV1;

#define RESERVATION_DATA_VERSION_V1 1

typedef struct _reservationsDataV1
{
    size_t size;
    uint32_t version;
    uint32_t generation;
    uint16_t numberOfReservations;//will most likely be 0 or 1 since element and extent types are obsolete.
    reservationInfoV1 reservation[1];//variable length depending on how it was allocated. Should always be AT LEAST one of these
}reservationsDataV1, *ptrReservationsDataV1;

int get_Reservations(tDevice *device, uint16_t numberReservations, ptrReservationsData reservations)
{
    //get only reservations
    int ret = NOT_SUPPORTED;
    if (!reservations)
    {
        return BAD_PARAMETER;
    }
    if (!(reservations->version >= RESERVATION_DATA_VERSION_V1) && !(reservations->size >= sizeof(reservationsDataV1)))
    {
        return BAD_PARAMETER;
    }
    if (device->drive_info.drive_type == SCSI_DRIVE)
    {
        uint16_t reservationsLength = (numberReservations * 16) + 8;
        uint8_t *reservationKeys = C_CAST(uint8_t*, calloc_aligned(reservationsLength, sizeof(uint8_t), device->os_info.minimumAlignment));
        if (!reservationKeys)
        {
            return MEMORY_FAILURE;
        }
        reservations->numberOfReservations = 0;
        if (SUCCESS == (ret = scsi_Persistent_Reserve_In(device, SCSI_PERSISTENT_RESERVE_IN_READ_RESERVATION, reservationsLength, reservationKeys)))
        {
            uint16_t reservationKeyCount = M_BytesTo4ByteValue(reservationKeys[4], reservationKeys[5], reservationKeys[6], reservationKeys[7]) / 16;
            reservations->generation = M_BytesTo4ByteValue(reservationKeys[0], reservationKeys[1], reservationKeys[2], reservationKeys[3]);
            for (uint32_t reservationIter = 0, offset = 8; reservationIter < reservationKeyCount && reservationIter < numberReservations && offset < reservationsLength; ++reservations->numberOfReservations, ++reservationIter, offset += 16)
            {
                reservations->reservation[reservationIter].reservationKey = M_BytesTo8ByteValue(reservationKeys[offset + 0], reservationKeys[offset + 1], reservationKeys[offset + 2], reservationKeys[offset + 3], reservationKeys[offset + 4], reservationKeys[offset + 5], reservationKeys[offset + 6], reservationKeys[offset + 7]);
                reservations->reservation[reservationIter].scopeSpecificAddress = M_BytesTo4ByteValue(reservationKeys[offset + 8], reservationKeys[offset + 9], reservationKeys[offset + 10], reservationKeys[offset + 11]);//obsolete on newer drives
                reservations->reservation[reservationIter].extentLength = M_BytesTo2ByteValue(reservationKeys[offset + 14], reservationKeys[offset + 15]);//obsolete on newer drives
                switch (M_GETBITRANGE(reservationKeys[offset + 13], 7, 4))
                {
                case 0:
                    reservations->reservation[reservationIter].scope = RESERVATION_SCOPE_LOGICAL_UNIT;
                    break;
                case 1:
                    reservations->reservation[reservationIter].scope = RESERVATION_SCOPE_EXTENT;
                    break;
                case 2:
                    reservations->reservation[reservationIter].scope = RESERVATION_SCOPE_ELEMENT;
                    break;
                default:
                    reservations->reservation[reservationIter].scope = RESERVATION_SCOPE_UNKNOWN;
                    break;
                }
                switch (M_GETBITRANGE(reservationKeys[offset + 13], 3, 0))
                {
                case 0:
                    reservations->reservation[reservationIter].type = RES_TYPE_READ_SHARED;
                    break;
                case 1:
                    reservations->reservation[reservationIter].type = RES_TYPE_WRITE_EXCLUSIVE;
                    break;
                case 2:
                    reservations->reservation[reservationIter].type = RES_TYPE_READ_EXCLUSIVE;
                    break;
                case 3:
                    reservations->reservation[reservationIter].type = RES_TYPE_EXCLUSIVE_ACCESS;
                    break;
                case 4:
                    reservations->reservation[reservationIter].type = RES_TYPE_SHARED_ACCESS;
                    break;
                case 5:
                    reservations->reservation[reservationIter].type = RES_TYPE_WRITE_EXCLUSIVE_REGISTRANTS_ONLY;
                    break;
                case 6:
                    reservations->reservation[reservationIter].type = RES_TYPE_EXCLUSIVE_ACCESS_REGISTRANTS_ONLY;
                    break;
                case 7:
                    reservations->reservation[reservationIter].type = RES_TYPE_WRITE_EXCLUSIVE_ALL_REGISTRANTS;
                    break;
                case 8:
                    reservations->reservation[reservationIter].type = RES_TYPE_EXCLUSIVE_ACCESS_ALL_REGISTRANTS;
                    break;
                default:
                    reservations->reservation[reservationIter].type = RES_TYPE_UNKNOWN;
                    break;
                }
            }
        }
        safe_Free_aligned(reservationKeys)
    }
#if !defined (DISABLE_NVME_PASSTHROUGH)
    else if (device->drive_info.drive_type == NVME_DRIVE)
    {
        //due to how the API was written and NVMe works, we need to call this instead to read all the keys. The get reservations key count will only return 0 or 1 since
        //there is at MOST 1 active reservation. But we need to go through and find out how many keys are currently registered to loop through them later.
        uint16_t totalReservationKeys = 0;
        if (SUCCESS == get_Registration_Key_Count(device, &totalReservationKeys))
        {
            uint32_t reservationsLength = (totalReservationKeys * 24) + 24;
            uint8_t *reservationKeys = C_CAST(uint8_t*, calloc_aligned(reservationsLength, sizeof(uint8_t), device->os_info.minimumAlignment));
            if (!reservationKeys)
            {
                return MEMORY_FAILURE;
            }
            reservations->numberOfReservations = 0;
            if (SUCCESS == (ret = nvme_Reservation_Report(device, false, reservationKeys, reservationsLength)))
            {
                reservations->generation = M_BytesTo4ByteValue(reservationKeys[3], reservationKeys[2], reservationKeys[1], reservationKeys[0]);
                if (reservationKeys[4] > 0 && numberReservations > 0)
                {
                    uint16_t reservationCount = M_BytesTo2ByteValue(reservationKeys[6], reservationKeys[5]);
                    if (reservationKeys[4] > 0)//if this doesn't have an active registration right now, no need to loop through.
                    {
                        //loop through and find the reservatio nthat is currently active.
                        for (uint32_t reservationIter = 0, offset = 24; reservationIter < reservationCount && offset < reservationsLength; offset += 24, ++reservationIter)
                        {
                            if (reservationKeys[2] & BIT0)
                            {
                                reservations->reservation[0].reservationKey = M_BytesTo8ByteValue(reservationKeys[offset + 23], reservationKeys[offset + 22], reservationKeys[offset + 21], reservationKeys[offset + 20], reservationKeys[offset + 19], reservationKeys[offset + 18], reservationKeys[offset + 17], reservationKeys[offset + 16]);
                                reservations->numberOfReservations = 1;
                                reservations->reservation[0].scope = RESERVATION_SCOPE_LOGICAL_UNIT;//scope is always zero on NVMe (at least for now)
                                switch (reservationKeys[4])
                                {
                                case 0:
                                    reservations->reservation[0].type = RES_TYPE_NO_RESERVATION;//or unknown? THis is reserved in NVMe
                                    break;
                                case 1:
                                    reservations->reservation[0].type = RES_TYPE_WRITE_EXCLUSIVE;
                                    break;
                                case 2:
                                    reservations->reservation[0].type = RES_TYPE_EXCLUSIVE_ACCESS;
                                    break;
                                case 3:
                                    reservations->reservation[0].type = RES_TYPE_WRITE_EXCLUSIVE_REGISTRANTS_ONLY;
                                    break;
                                case 4:
                                    reservations->reservation[0].type = RES_TYPE_EXCLUSIVE_ACCESS_REGISTRANTS_ONLY;
                                    break;
                                case 5:
                                    reservations->reservation[0].type = RES_TYPE_WRITE_EXCLUSIVE_ALL_REGISTRANTS;
                                    break;
                                case 6:
                                    reservations->reservation[0].type = RES_TYPE_EXCLUSIVE_ACCESS_ALL_REGISTRANTS;
                                    break;
                                default:
                                    reservations->reservation[0].type = RES_TYPE_UNKNOWN;
                                    break;
                                }
                            }
                        }
                    }
                }
            }
            safe_Free_aligned(reservationKeys)
        }
    }
#endif
    return ret;
}

void show_Reservations(ptrReservationsData reservations)
{
    if (reservations->version >= RESERVATION_DATA_VERSION_V1 && reservations->size >= sizeof(reservationsDataV1))
    {
        printf("Reservations:\n");
        printf("=============\n");
        printf("Generation: %" PRIu32 "\n", reservations->generation);
        printf("      Key        | Scope |        Type        \n");
        for (uint32_t resIter = 0; resIter < UINT16_MAX && resIter < reservations->numberOfReservations; ++resIter)
        {
            char scopeBuf[9] = { 0 };
            char *scope = &scopeBuf[0];
            char typeBuf[23] = { 0 };
            char *type = &typeBuf[0];
            switch (reservations->reservation[resIter].scope)
            {
            case RESERVATION_SCOPE_LOGICAL_UNIT:
                snprintf(scope, 9, "LU");
                break;
            case RESERVATION_SCOPE_EXTENT:
                snprintf(scope, 9, "Extent");
                break;
            case RESERVATION_SCOPE_ELEMENT:
                snprintf(scope, 9, "Element");
                break;
            case RESERVATION_SCOPE_UNKNOWN:
            default:
                snprintf(scope, 9, "Unknown");
                break;
            }
            switch (reservations->reservation[resIter].type)
            {
            case RES_TYPE_NO_RESERVATION:
                snprintf(type, 23, "None");
                break;
            case RES_TYPE_READ_SHARED:
                snprintf(type, 23, "Read Shared");
                break;
            case RES_TYPE_WRITE_EXCLUSIVE:
                snprintf(type, 23, "Write Exclusive");
                break;
            case RES_TYPE_READ_EXCLUSIVE:
                snprintf(type, 23, "Read Exclusive");
                break;
            case RES_TYPE_EXCLUSIVE_ACCESS:
                snprintf(type, 23, "Exclusive Access");
                break;
            case RES_TYPE_SHARED_ACCESS:
                snprintf(type, 23, "Shared Access");
                break;
            case RES_TYPE_WRITE_EXCLUSIVE_REGISTRANTS_ONLY:
                snprintf(type, 23, "Write Exclusive - RO");
                break;
            case RES_TYPE_EXCLUSIVE_ACCESS_REGISTRANTS_ONLY:
                snprintf(type, 23, "Exclusive Access - RO");
                break;
            case RES_TYPE_WRITE_EXCLUSIVE_ALL_REGISTRANTS:
                snprintf(type, 23, "Write Exclusive - AR");
                break;
            case RES_TYPE_EXCLUSIVE_ACCESS_ALL_REGISTRANTS:
                snprintf(type, 23, "Exclusive Access - AR");
                break;
            case RES_TYPE_UNKNOWN:
            default:
                snprintf(type, 23, "Unknown");
                break;
            }
            printf("%16" PRIX64 "h  %7s  %20s", reservations->reservation[resIter].reservationKey, scope, type);
        }
        if (reservations->numberOfReservations == 0)
        {
            printf("No active reservations.\n");
        }
    }
}

int get_Full_Status_Key_Count(tDevice *device, uint16_t *keyCount)
{
    int ret = NOT_SUPPORTED;
    if (!keyCount)
    {
        return BAD_PARAMETER;
    }
    *keyCount = 0;
    if (device->drive_info.drive_type == SCSI_DRIVE)
    {
        uint32_t fullStatusDataLength = 32;//some drive FW have a bug where if this is read as 8 bytes, it returns that there are no keys, even when there are...-TJE
        uint8_t *fullStatusData = C_CAST(uint8_t*, calloc_aligned(fullStatusDataLength, sizeof(uint8_t), device->os_info.minimumAlignment));
        if (!fullStatusData)
        {
            return MEMORY_FAILURE;
        }
        //check SCSI version and fall back if "new enough" but command doesn't complete successfully???
        if (device->drive_info.scsiVersion >= SCSI_VERSION_SPC_3 && SUCCESS == (ret = scsi_Persistent_Reserve_In(device, SCSI_PERSISTENT_RESERVE_IN_READ_FULL_STATUS, C_CAST(uint16_t, M_Min(fullStatusDataLength, UINT16_MAX)), fullStatusData)))
        {
            //since the transport ID can vary in size, we cannot calculate this on length alone, so we need to re-read with the full length of the data just reported and count them.
            fullStatusDataLength = 8 + M_BytesTo4ByteValue(fullStatusData[4], fullStatusData[5], fullStatusData[6], fullStatusData[7]);
            //reallocate with enough memory
            safe_Free_aligned(fullStatusData)
            fullStatusData = C_CAST(uint8_t*, calloc_aligned(fullStatusDataLength, sizeof(uint8_t), device->os_info.minimumAlignment));
            if (!fullStatusData)
            {
                return MEMORY_FAILURE;
            }
            //reread the data
            if (SUCCESS == (ret = scsi_Persistent_Reserve_In(device, SCSI_PERSISTENT_RESERVE_IN_READ_FULL_STATUS, C_CAST(uint16_t, M_Min(fullStatusDataLength, UINT16_MAX)), fullStatusData)))
            {
                //loop through each descriptor to count them.
                //each will be at least 24 bytes before the transport ID
                uint32_t offsetAdditionalLength = 0;
                for (uint32_t offset = 8; offset < fullStatusDataLength; offset += 24 + offsetAdditionalLength)
                {
                    offsetAdditionalLength = M_BytesTo4ByteValue(fullStatusData[offset + 20], fullStatusData[offset + 21], fullStatusData[offset + 22], fullStatusData[offset + 23]);
                    ++(*keyCount);
                }
            }
        }
        else //full status not supported or an old device
        {
            //use the registration key count function instead
            ret = get_Registration_Key_Count(device, keyCount);
        }
        safe_Free_aligned(fullStatusData)
    }
#if !defined (DISABLE_NVME_PASSTHROUGH)
    else if (device->drive_info.drive_type == NVME_DRIVE)
    {
        uint8_t readKeyCount[24] = { 0 };//may be able to get away with only 8 bytes, but this read up until the list begins - TJE
        if (SUCCESS == (ret = nvme_Reservation_Report(device, false, readKeyCount, 24)))
        {
            *keyCount = M_BytesTo2ByteValue(readKeyCount[6], readKeyCount[5]);
        }
    }
#endif
    return ret;
}

typedef struct _fullReservationKeyInfoV1
{
    uint64_t key;
    bool allTargetPorts;
    bool reservationHolder;//NOTE: This must be set to true for scope and type to be valid
    uint16_t relativeTargetPortIdentifier;
    eReservationScope scope;
    eReservationType type;
    uint32_t transportIDLength;
    uint8_t transportID[24];//NOTE: This is 24 bytes as that is the common size. iSCSI is variable in size, so it will be truncated in this case -TJE
}fullReservationKeyInfoV1;

#define FULL_RESERVATION_INFO_VERSION_V1 1

typedef struct _fullReservationInfoV1
{
    size_t size;
    uint32_t version;
    uint32_t generation;
    uint16_t numberOfKeys;
    fullReservationKeyInfoV1 reservationKey[1];//Variable size depending on how many will be reported by the device at a given time.
}fullReservationInfoV1, *ptrFullReservationInfoV1;

int get_Full_Status(tDevice *device, uint16_t numberOfKeys, ptrFullReservationInfo fullReservation)
{
    //if newer SPC, use the read full status subcommand.
    //If older SPC, use the get_Registrations and get_Reservations functions to get all the data we need to collect. - TJE
    int ret = NOT_SUPPORTED;
    if (!fullReservation)
    {
        return BAD_PARAMETER;
    }
    if (!(fullReservation->version >= FULL_RESERVATION_INFO_VERSION_V1) && !(fullReservation->size >= sizeof(fullReservationInfoV1)))
    {
        return BAD_PARAMETER;
    }
    if (device->drive_info.drive_type == SCSI_DRIVE)
    {
        uint32_t fullStatusDataLength = 32;//some drive FW have a bug where if this is read as 8 bytes, it returns that there are no keys, even when there are...-TJE
        uint8_t *fullStatusData = C_CAST(uint8_t*, calloc_aligned(fullStatusDataLength, sizeof(uint8_t), device->os_info.minimumAlignment));
        if (!fullStatusData)
        {
            return MEMORY_FAILURE;
        }
        //check SCSI version and fall back if "new enough" but command doesn't complete successfully???
        if (device->drive_info.scsiVersion >= SCSI_VERSION_SPC_3 && SUCCESS == (ret = scsi_Persistent_Reserve_In(device, SCSI_PERSISTENT_RESERVE_IN_READ_FULL_STATUS, C_CAST(uint16_t, M_Min(fullStatusDataLength, UINT16_MAX)), fullStatusData)))
        {
            //since the transport ID can vary in size, we cannot calculate this on length alone, so we need to re-read with the full length of the data just reported and count them.
            fullStatusDataLength = 8 + M_BytesTo4ByteValue(fullStatusData[4], fullStatusData[5], fullStatusData[6], fullStatusData[7]);
            //reallocate with enough memory
            safe_Free_aligned(fullStatusData)
            fullStatusData = C_CAST(uint8_t*, calloc_aligned(fullStatusDataLength, sizeof(uint8_t), device->os_info.minimumAlignment));
            if (!fullStatusData)
            {
                return MEMORY_FAILURE;
            }
            //reread the data
            if (SUCCESS == (ret = scsi_Persistent_Reserve_In(device, SCSI_PERSISTENT_RESERVE_IN_READ_FULL_STATUS, C_CAST(uint16_t, M_Min(fullStatusDataLength, UINT16_MAX)), fullStatusData)))
            {
                fullReservation->generation = M_BytesTo4ByteValue(fullStatusData[0], fullStatusData[1], fullStatusData[2], fullStatusData[3]);

                //loop through each descriptor to count them.
                //each will be at least 24 bytes before the transport ID
                uint32_t offsetAdditionalLength = 0;
                for (uint32_t offset = 8, keyIter = 0; offset < fullStatusDataLength && keyIter < numberOfKeys; offset += 24 + offsetAdditionalLength, ++keyIter)
                {
                    offsetAdditionalLength = M_BytesTo4ByteValue(fullStatusData[offset + 20], fullStatusData[offset + 21], fullStatusData[offset + 22], fullStatusData[offset + 23]);
                    //set the data to return
                    ++fullReservation->numberOfKeys;
                    fullReservation->reservationKey[keyIter].key = M_BytesTo8ByteValue(fullStatusData[offset + 0], fullStatusData[offset + 1], fullStatusData[offset + 2], fullStatusData[offset + 3], fullStatusData[offset + 4], fullStatusData[offset + 5], fullStatusData[offset + 6], fullStatusData[offset + 7]);
                    fullReservation->reservationKey[keyIter].allTargetPorts = M_ToBool(fullStatusData[offset + 12] & BIT1);
                    fullReservation->reservationKey[keyIter].reservationHolder = M_ToBool(fullStatusData[offset + 12] & BIT0);
                    fullReservation->reservationKey[keyIter].relativeTargetPortIdentifier = M_BytesTo2ByteValue(fullStatusData[offset + 18], fullStatusData[offset + 19]);
                    switch (M_GETBITRANGE(fullStatusData[offset + 13], 7, 4))
                    {
                    case 0:
                        fullReservation->reservationKey[keyIter].scope = RESERVATION_SCOPE_LOGICAL_UNIT;
                        break;
                    case 1:
                        fullReservation->reservationKey[keyIter].scope = RESERVATION_SCOPE_EXTENT;
                        break;
                    case 2:
                        fullReservation->reservationKey[keyIter].scope = RESERVATION_SCOPE_ELEMENT;
                        break;
                    default:
                        fullReservation->reservationKey[keyIter].scope = RESERVATION_SCOPE_UNKNOWN;
                        break;
                    }
                    switch (M_GETBITRANGE(fullStatusData[offset + 13], 3, 0))
                    {
                    case 0:
                        if (fullReservation->reservationKey[keyIter].reservationHolder)
                        {
                            fullReservation->reservationKey[keyIter].type = RES_TYPE_READ_SHARED;
                        }
                        else
                        {
                            fullReservation->reservationKey[keyIter].type = RES_TYPE_NO_RESERVATION;
                        }
                        break;
                    case 1:
                        fullReservation->reservationKey[keyIter].type = RES_TYPE_WRITE_EXCLUSIVE;
                        break;
                    case 2:
                        fullReservation->reservationKey[keyIter].type = RES_TYPE_READ_EXCLUSIVE;
                        break;
                    case 3:
                        fullReservation->reservationKey[keyIter].type = RES_TYPE_EXCLUSIVE_ACCESS;
                        break;
                    case 4:
                        fullReservation->reservationKey[keyIter].type = RES_TYPE_SHARED_ACCESS;
                        break;
                    case 5:
                        fullReservation->reservationKey[keyIter].type = RES_TYPE_WRITE_EXCLUSIVE_REGISTRANTS_ONLY;
                        break;
                    case 6:
                        fullReservation->reservationKey[keyIter].type = RES_TYPE_EXCLUSIVE_ACCESS_REGISTRANTS_ONLY;
                        break;
                    case 7:
                        fullReservation->reservationKey[keyIter].type = RES_TYPE_WRITE_EXCLUSIVE_ALL_REGISTRANTS;
                        break;
                    case 8:
                        fullReservation->reservationKey[keyIter].type = RES_TYPE_EXCLUSIVE_ACCESS_ALL_REGISTRANTS;
                        break;
                    default:
                        fullReservation->reservationKey[keyIter].type = RES_TYPE_UNKNOWN;
                        break;
                    }
                    fullReservation->reservationKey[keyIter].transportIDLength = offsetAdditionalLength;
                    if (offsetAdditionalLength > 0)
                    {
                        //copy the transport ID, if any, up to 24 bytes
                        memcpy(fullReservation->reservationKey[keyIter].transportID, &fullStatusData[offset + 24], M_Min(24, offsetAdditionalLength));
                    }
                    else
                    {
                        memset(fullReservation->reservationKey[keyIter].transportID, 0, 24);
                    }
                }
            }
        }
        else
        {
            //Older drive, or just doesn't support the full status capability, so we can read registrations and reservations and then combine the two outputs into the expected full status results.
            //read registrations and reservations to match things up to the "equivalent" of the full status.
            uint16_t registrationCount = 0, reservationCount = 0;
            if (SUCCESS == get_Registration_Key_Count(device, &registrationCount) && SUCCESS == get_Reservation_Count(device, &reservationCount))
            {
                //start with reading registrations first, then if the reservation count is > 0 read and map that information too.
                ptrRegistrationKeysData registrations = C_CAST(ptrRegistrationKeysData, calloc(sizeof(registrationKeysData) + registrationCount * sizeof(uint64_t), sizeof(uint8_t)));
                ptrReservationsData reservations = C_CAST(ptrReservationsData, calloc(sizeof(reservationsData) + reservationCount * sizeof(reservationInfo), sizeof(uint8_t)));
                if (!registrations || !reservations)
                {
                    //not sure which of these failed, but these macros should be safe to use to make sure we don't leave any memory out there
                    safe_Free(registrations)
                    safe_Free(reservations)
                    return MEMORY_FAILURE;
                }
                registrations->size = sizeof(registrationKeysData);
                registrations->version = REGISTRATION_KEY_DATA_VERSION;
                reservations->size = sizeof(reservationsData);
                reservations->version = RESERVATION_DATA_VERSION;
                if (SUCCESS == (ret = get_Registration_Keys(device, registrationCount, registrations)) && SUCCESS == (ret = get_Reservations(device, reservationCount, reservations)))
                {
                    //got both things we need to map to the full data as best we can...some parts may be omitted, but that's OK...it's the best we can do right now.
                    //registrations will hold all the keys.
                    //reservations will have a list of the keys with an active reservation. So match this to the registration key to set the r_holder bit and set scope and type...everything else will be left at zero
                    //first fill in the keys from registrations
                    fullReservation->generation = registrations->generation;
                    for (uint32_t keyIter = 0; keyIter < registrationCount && keyIter < registrations->numberOfKeys && keyIter < numberOfKeys; ++keyIter)
                    {
                        //initialize the following to zeros since they don't map
                        fullReservation->reservationKey[keyIter].allTargetPorts = false;
                        fullReservation->reservationKey[keyIter].relativeTargetPortIdentifier = 0;
                        memset(fullReservation->reservationKey[keyIter].transportID, 0, 24);
                        fullReservation->reservationKey[keyIter].transportIDLength = 0;
                        //initialize the following fields before we check the reservations data
                        fullReservation->reservationKey[keyIter].reservationHolder = false;
                        fullReservation->reservationKey[keyIter].scope = RESERVATION_SCOPE_LOGICAL_UNIT;
                        fullReservation->reservationKey[keyIter].type = RES_TYPE_NO_RESERVATION;
                        //set the key
                        fullReservation->reservationKey[keyIter].key = registrations->registrationKey[keyIter];
                        //check if this key matches any reported in the reservations data and add more fields from that...
                        for (uint32_t resKeyIter = 0; resKeyIter < reservationCount && resKeyIter < reservations->numberOfReservations; ++resKeyIter)
                        {
                            if (registrations->registrationKey[keyIter] == reservations->reservation[resKeyIter].reservationKey)
                            {
                                //found a matching key, so update remaining fields.
                                fullReservation->reservationKey[keyIter].reservationHolder = true;
                                fullReservation->reservationKey[keyIter].scope = reservations->reservation[resKeyIter].scope;
                                fullReservation->reservationKey[keyIter].type = reservations->reservation[resKeyIter].type;
                                break;
                            }
                        }
                        ++(fullReservation->numberOfKeys);
                    }
                }
            }
            else
            {
                ret = FAILURE;
            }
        }
        safe_Free_aligned(fullStatusData)
    }
#if !defined (DISABLE_NVME_PASSTHROUGH)
    else if (device->drive_info.drive_type == NVME_DRIVE)
    {
        uint32_t nvmeFullDataLen = 24 + (24 * numberOfKeys);
        uint8_t *nvmeFullData = C_CAST(uint8_t*, calloc_aligned(nvmeFullDataLen, sizeof(uint8_t), device->os_info.minimumAlignment));
        if (!nvmeFullData)
        {
            return MEMORY_FAILURE;
        }
        if (SUCCESS == (ret = nvme_Reservation_Report(device, false, nvmeFullData, nvmeFullDataLen)))
        {
            uint16_t keyCount = M_BytesTo2ByteValue(nvmeFullData[6], nvmeFullData[5]);
            fullReservation->generation = M_BytesTo4ByteValue(nvmeFullData[3], nvmeFullData[2], nvmeFullData[1], nvmeFullData[0]);
            for (uint32_t keyIter = 0, offset = 24; keyIter < keyCount && keyIter < numberOfKeys; ++keyIter, offset += 24)
            {
                fullReservation->reservationKey[keyIter].relativeTargetPortIdentifier = M_BytesTo2ByteValue(nvmeFullData[offset + 1], nvmeFullData[offset + 2]);
                fullReservation->reservationKey[keyIter].scope = RESERVATION_SCOPE_LOGICAL_UNIT;//all nvme scops are like this
                fullReservation->reservationKey[keyIter].allTargetPorts = true;
                fullReservation->reservationKey[keyIter].reservationHolder = M_ToBool(nvmeFullData[offset + 2] & BIT2);
                if (fullReservation->reservationKey[keyIter].reservationHolder)
                {
                    //set the reservation scope and type...only 1 reservation allowed at a time per namespace, so check offset 4 for this information.
                    switch (nvmeFullData[4])
                    {
                    case 0:
                        fullReservation->reservationKey[keyIter].type = RES_TYPE_NO_RESERVATION;//or unknown? THis is reserved in NVMe
                        break;
                    case 1:
                        fullReservation->reservationKey[keyIter].type = RES_TYPE_WRITE_EXCLUSIVE;
                        break;
                    case 2:
                        fullReservation->reservationKey[keyIter].type = RES_TYPE_EXCLUSIVE_ACCESS;
                        break;
                    case 3:
                        fullReservation->reservationKey[keyIter].type = RES_TYPE_WRITE_EXCLUSIVE_REGISTRANTS_ONLY;
                        break;
                    case 4:
                        fullReservation->reservationKey[keyIter].type = RES_TYPE_EXCLUSIVE_ACCESS_REGISTRANTS_ONLY;
                        break;
                    case 5:
                        fullReservation->reservationKey[keyIter].type = RES_TYPE_WRITE_EXCLUSIVE_ALL_REGISTRANTS;
                        break;
                    case 6:
                        fullReservation->reservationKey[keyIter].type = RES_TYPE_EXCLUSIVE_ACCESS_ALL_REGISTRANTS;
                        break;
                    default:
                        fullReservation->reservationKey[keyIter].type = RES_TYPE_UNKNOWN;
                        break;
                    }
                }
                else
                {
                    //no reservation active
                    fullReservation->reservationKey[keyIter].type = RES_TYPE_NO_RESERVATION;
                }
                //host id/transport id
                memcpy(fullReservation->reservationKey[keyIter].transportID, &nvmeFullData[offset + 8], 8);
                fullReservation->reservationKey[keyIter].transportIDLength = 8;
                //finally, the key
                fullReservation->reservationKey[keyIter].key = M_BytesTo8ByteValue(nvmeFullData[offset + 23], nvmeFullData[offset + 22], nvmeFullData[offset + 21], nvmeFullData[offset + 20], nvmeFullData[offset + 19], nvmeFullData[offset + 18], nvmeFullData[offset + 17], nvmeFullData[offset + 16]);
            }
        }
        safe_Free_aligned(nvmeFullData)
    }
#endif
    return ret;
}

void show_Full_Status(ptrFullReservationInfo fullReservation)
{
    if (fullReservation && fullReservation->version >= FULL_RESERVATION_INFO_VERSION_V1 && fullReservation->size >= sizeof(fullReservationInfoV1))
    {
        printf("Full Reservation Status:\n");
        printf("\tGeneration: %" PRIX32 "h\n", fullReservation->generation);

        printf("      Key        | ATP | Res Holder | Scope |         Type         |  RTPID  | Transport ID \n");//TODO: relative target port ID, transport ID
        for (uint32_t keyIter = 0; keyIter < UINT16_MAX && keyIter < fullReservation->numberOfKeys; ++keyIter)
        {
            char atp = 'N';
            char resHolder = 'N';
            char scopeBuf[9] = { 0 };
            char *scope = &scopeBuf[0];
            char typeBuf[23] = { 0 };
            char *type = &typeBuf[0];
            if (fullReservation->reservationKey[keyIter].allTargetPorts)
            {
                atp = 'Y';
            }
            if (fullReservation->reservationKey[keyIter].reservationHolder)
            {
                resHolder = 'Y';
            }
            switch (fullReservation->reservationKey[keyIter].scope)
            {
            case RESERVATION_SCOPE_LOGICAL_UNIT:
                snprintf(scope, 9, "LU");
                break;
            case RESERVATION_SCOPE_EXTENT:
                snprintf(scope, 9, "Extent");
                break;
            case RESERVATION_SCOPE_ELEMENT:
                snprintf(scope, 9, "Element");
                break;
            case RESERVATION_SCOPE_UNKNOWN:
            default:
                snprintf(scope, 9, "Unknown");
                break;
            }
            switch (fullReservation->reservationKey[keyIter].type)
            {
            case RES_TYPE_NO_RESERVATION:
                snprintf(type, 23, "None");
                break;
            case RES_TYPE_READ_SHARED:
                snprintf(type, 23, "Read Shared");
                break;
            case RES_TYPE_WRITE_EXCLUSIVE:
                snprintf(type, 23, "Write Exclusive");
                break;
            case RES_TYPE_READ_EXCLUSIVE:
                snprintf(type, 23, "Read Exclusive");
                break;
            case RES_TYPE_EXCLUSIVE_ACCESS:
                snprintf(type, 23, "Exclusive Access");
                break;
            case RES_TYPE_SHARED_ACCESS:
                snprintf(type, 23, "Shared Access");
                break;
            case RES_TYPE_WRITE_EXCLUSIVE_REGISTRANTS_ONLY:
                snprintf(type, 23, "Write Exclusive - RO");
                break;
            case RES_TYPE_EXCLUSIVE_ACCESS_REGISTRANTS_ONLY:
                snprintf(type, 23, "Exclusive Access - RO");
                break;
            case RES_TYPE_WRITE_EXCLUSIVE_ALL_REGISTRANTS:
                snprintf(type, 23, "Write Exclusive - AR");
                break;
            case RES_TYPE_EXCLUSIVE_ACCESS_ALL_REGISTRANTS:
                snprintf(type, 23, "Exclusive Access - AR");
                break;
            case RES_TYPE_UNKNOWN:
            default:
                snprintf(type, 23, "Unknown");
                break;
            }
            printf("%16" PRIX64 "h  %c        %c      %7s  %23s  %08" PRIX16 "h ", fullReservation->reservationKey[keyIter].key, atp, resHolder, scope, type, fullReservation->reservationKey[keyIter].relativeTargetPortIdentifier);
            if (fullReservation->reservationKey[keyIter].transportIDLength > 0)
            {
                for (uint32_t transportIDoffset = 0; transportIDoffset <= 24 && transportIDoffset <= fullReservation->reservationKey[keyIter].transportIDLength; ++transportIDoffset)
                {
                printf("%02" PRIX8, fullReservation->reservationKey[keyIter].transportID[transportIDoffset]);
                }
                if (fullReservation->reservationKey[keyIter].transportIDLength > 24)
                {
                    printf("...");
                }
                printf("h");
            }
            else
            {
            printf("     N/A");
            }
            printf("\n");
        }
        if (fullReservation->numberOfKeys == 0)
        {
            printf("No reservations or registration keys to report.\n");
        }
    }
    else
    {
    printf("ERROR: Invalid full status structure version or size.\n");
    }
}

typedef struct _persistentReserveOutBasic
{
    uint64_t reservationKey;
    uint64_t serviceActionReservationKey;
    uint32_t scopeSpecificAddress;//OBSOLETE, for use with older devices
    bool activatePersistThroughPowerLoss;
    bool allTargetPorts;
    bool specifyInitiatorPorts;
    uint16_t extentLength;//OBSOLETE, for use with older extent reservations
    uint32_t transportIDLength;//length of the following buffer
    uint8_t transportID[1];//overallocate to add transport IDs. Length is not needed in this data and is set from the parameter above
}persistentReserveOutBasic, *ptrPersistentReserveOutBasic;

#define PR_OUT_BASIC_MIN_LENGTH 24

static void format_Basic_Info(uint8_t *ptrData, uint32_t dataLength, ptrPersistentReserveOutBasic basicInfo)
{
    if (ptrData && dataLength >= 24 && basicInfo)//24 is minimum length for this buffer
    {
        //reservation key
        ptrData[0] = M_Byte7(basicInfo->reservationKey);
        ptrData[1] = M_Byte6(basicInfo->reservationKey);
        ptrData[2] = M_Byte5(basicInfo->reservationKey);
        ptrData[3] = M_Byte4(basicInfo->reservationKey);
        ptrData[4] = M_Byte3(basicInfo->reservationKey);
        ptrData[5] = M_Byte2(basicInfo->reservationKey);
        ptrData[6] = M_Byte1(basicInfo->reservationKey);
        ptrData[7] = M_Byte0(basicInfo->reservationKey);
        //service action reservation key
        ptrData[8] = M_Byte7(basicInfo->serviceActionReservationKey);
        ptrData[9] = M_Byte6(basicInfo->serviceActionReservationKey);
        ptrData[10] = M_Byte5(basicInfo->serviceActionReservationKey);
        ptrData[11] = M_Byte4(basicInfo->serviceActionReservationKey);
        ptrData[12] = M_Byte3(basicInfo->serviceActionReservationKey);
        ptrData[13] = M_Byte2(basicInfo->serviceActionReservationKey);
        ptrData[14] = M_Byte1(basicInfo->serviceActionReservationKey);
        ptrData[15] = M_Byte0(basicInfo->serviceActionReservationKey);
        //(obsolete) scope specific address
        ptrData[16] = M_Byte3(basicInfo->scopeSpecificAddress);
        ptrData[17] = M_Byte2(basicInfo->scopeSpecificAddress);
        ptrData[18] = M_Byte1(basicInfo->scopeSpecificAddress);
        ptrData[19] = M_Byte0(basicInfo->scopeSpecificAddress);
        //clear our 20 & 21, then set the bits needed
        ptrData[20] = UINT8_C(0);
        ptrData[21] = UINT8_C(0);
        if (basicInfo->activatePersistThroughPowerLoss)
        {
            ptrData[20] |= BIT0;
        }
        if (basicInfo->allTargetPorts)
        {
            ptrData[20] |= BIT2;
        }
        if (basicInfo->specifyInitiatorPorts)
        {
            ptrData[20] |= BIT3;
        }
        //(obsolete) extent length
        ptrData[22] = M_Byte1(basicInfo->extentLength);
        ptrData[23] = M_Byte0(basicInfo->extentLength);
        //additional info (transport IDs)
        if (basicInfo->transportIDLength > 0 && (basicInfo->transportIDLength + 24 /*length of basic data buffer before transport IDs*/ + 4 /*for the length that is set before the transport ids start */) >= dataLength)
        {
            ptrData[24] = M_Byte3(basicInfo->transportIDLength);
            ptrData[25] = M_Byte2(basicInfo->transportIDLength);
            ptrData[26] = M_Byte1(basicInfo->transportIDLength);
            ptrData[27] = M_Byte0(basicInfo->transportIDLength);
            //now copy remaining data to the buffer...already checked the size above
            memcpy(&ptrData[28], basicInfo->transportID, basicInfo->transportIDLength);
        }
    }
}

int register_Key(tDevice * device, uint64_t registrationKey, bool allTargetPorts, bool persistThroughPowerLoss, bool ignoreExisting)
{
    int ret = NOT_SUPPORTED;
    if (device->drive_info.drive_type == SCSI_DRIVE)
    {
        uint8_t registerData[PR_OUT_BASIC_MIN_LENGTH] = { 0 };
        persistentReserveOutBasic prData;
        memset(&prData, 0, sizeof(persistentReserveOutBasic));
        prData.reservationKey = 0;//when registering, set this to zero to begin.
        prData.serviceActionReservationKey = registrationKey;
        prData.allTargetPorts = allTargetPorts;
        prData.activatePersistThroughPowerLoss = persistThroughPowerLoss;
        //NOTE: Not currently supporting the specifiy initiator ports field and transport IDs, but the data formatter does support creating that buffer.

        format_Basic_Info(registerData, PR_OUT_BASIC_MIN_LENGTH, &prData);
        ret = scsi_Persistent_Reserve_Out(device, ignoreExisting ? SCSI_PERSISTENT_RESERVE_OUT_REGISTER_AND_IGNORE_EXISTING_KEY : SCSI_PERSISTENT_RESERVE_OUT_REGISTER, 0, 0, PR_OUT_BASIC_MIN_LENGTH, registerData);
    }
#if !defined (DISABLE_NVME_PASSTHROUGH)
    else if (device->drive_info.drive_type == NVME_DRIVE)
    {
        uint8_t registerData[16] = { 0 };
        registerData[8] = M_Byte0(registrationKey);
        registerData[9] = M_Byte1(registrationKey);
        registerData[10] = M_Byte2(registrationKey);
        registerData[11] = M_Byte3(registrationKey);
        registerData[12] = M_Byte4(registrationKey);
        registerData[13] = M_Byte5(registrationKey);
        registerData[14] = M_Byte6(registrationKey);
        registerData[15] = M_Byte7(registrationKey);
        //NOTE: SCSI translation mentions sending set features first if persist through power loss is not already activated...not sure if this is needed or not in this case since translation is not being used...don't really see it in the spec- TJE
        ret = nvme_Reservation_Register(device, persistThroughPowerLoss ? 3 : 2, ignoreExisting, 0, registerData, 16);//TODO: Move values to enums/definitions
    }
#endif
    return ret;
}

int unregister_Key(tDevice *device, uint64_t currentRegistrationKey)
{
    int ret = NOT_SUPPORTED;
    if (device->drive_info.drive_type == SCSI_DRIVE)
    {
        uint8_t registerData[PR_OUT_BASIC_MIN_LENGTH] = { 0 };
        persistentReserveOutBasic prData;
        memset(&prData, 0, sizeof(persistentReserveOutBasic));
        prData.reservationKey = currentRegistrationKey;
        prData.serviceActionReservationKey = 0;
        format_Basic_Info(registerData, PR_OUT_BASIC_MIN_LENGTH, &prData);
        ret = scsi_Persistent_Reserve_Out(device, SCSI_PERSISTENT_RESERVE_OUT_REGISTER, 0, 0, PR_OUT_BASIC_MIN_LENGTH, registerData);

    }
#if !defined (DISABLE_NVME_PASSTHROUGH)
    else if (device->drive_info.drive_type == NVME_DRIVE)
    {
        uint8_t registerData[16] = { 0 };
        registerData[0] = M_Byte0(currentRegistrationKey);
        registerData[1] = M_Byte1(currentRegistrationKey);
        registerData[2] = M_Byte2(currentRegistrationKey);
        registerData[3] = M_Byte3(currentRegistrationKey);
        registerData[4] = M_Byte4(currentRegistrationKey);
        registerData[5] = M_Byte5(currentRegistrationKey);
        registerData[6] = M_Byte6(currentRegistrationKey);
        registerData[7] = M_Byte7(currentRegistrationKey);
        ret = nvme_Reservation_Register(device, 0, 0, 1, registerData, 16);//TODO: Move values to enums/definitions
    }
#endif
    return ret;
}

int acquire_Reservation(tDevice *device, uint64_t key, eReservationType resType)
{
    int ret = NOT_SUPPORTED;
    if (device->drive_info.drive_type == SCSI_DRIVE)
    {
        uint8_t acquireRes[PR_OUT_BASIC_MIN_LENGTH] = { 0 };
        uint8_t scsiReservationType = 0;
        persistentReserveOutBasic prData;
        memset(&prData, 0, sizeof(persistentReserveOutBasic));
        prData.reservationKey = key;
        format_Basic_Info(acquireRes, PR_OUT_BASIC_MIN_LENGTH, &prData);
        switch (resType)
        {
        case RES_TYPE_READ_SHARED:
            scsiReservationType = 0;
            break;
        case RES_TYPE_WRITE_EXCLUSIVE:
            scsiReservationType = 1;
            break;
        case RES_TYPE_READ_EXCLUSIVE://obsolete - old scsi only
            scsiReservationType = 2;
            break;
        case RES_TYPE_EXCLUSIVE_ACCESS:
            scsiReservationType = 3;
            break;
        case RES_TYPE_SHARED_ACCESS://obsolete - old SCSI only
            scsiReservationType = 4;
            break;
        case RES_TYPE_WRITE_EXCLUSIVE_REGISTRANTS_ONLY:
            scsiReservationType = 5;
            break;
        case RES_TYPE_EXCLUSIVE_ACCESS_REGISTRANTS_ONLY:
            scsiReservationType = 6;
            break;
        case RES_TYPE_WRITE_EXCLUSIVE_ALL_REGISTRANTS:
            scsiReservationType = 7;
            break;
        case RES_TYPE_EXCLUSIVE_ACCESS_ALL_REGISTRANTS:
            scsiReservationType = 8;
            break;
        case RES_TYPE_NO_RESERVATION:
        default:
            return BAD_PARAMETER;
        }
        ret = scsi_Persistent_Reserve_Out(device, SCSI_PERSISTENT_RESERVE_OUT_RESERVE, 0, scsiReservationType, PR_OUT_BASIC_MIN_LENGTH, acquireRes);
    }
#if !defined (DISABLE_NVME_PASSTHROUGH)
    else if (device->drive_info.drive_type == NVME_DRIVE)
    {
        uint8_t acquireRes[16] = { 0 };
        uint8_t nvmeReservationType = 0;
        acquireRes[0] = M_Byte0(key);
        acquireRes[1] = M_Byte1(key);
        acquireRes[2] = M_Byte2(key);
        acquireRes[3] = M_Byte3(key);
        acquireRes[4] = M_Byte4(key);
        acquireRes[5] = M_Byte5(key);
        acquireRes[6] = M_Byte6(key);
        acquireRes[7] = M_Byte7(key);
        switch (resType)
        {
        case RES_TYPE_WRITE_EXCLUSIVE:
            nvmeReservationType = 1;
            break;
        case RES_TYPE_EXCLUSIVE_ACCESS:
            nvmeReservationType = 2;
            break;
        case RES_TYPE_WRITE_EXCLUSIVE_REGISTRANTS_ONLY:
            nvmeReservationType = 3;
            break;
        case RES_TYPE_EXCLUSIVE_ACCESS_REGISTRANTS_ONLY:
            nvmeReservationType = 4;
            break;
        case RES_TYPE_WRITE_EXCLUSIVE_ALL_REGISTRANTS:
            nvmeReservationType = 5;
            break;
        case RES_TYPE_EXCLUSIVE_ACCESS_ALL_REGISTRANTS:
            nvmeReservationType = 6;
            break;
        case RES_TYPE_NO_RESERVATION:
        default:
            return BAD_PARAMETER;
        }
        ret = nvme_Reservation_Acquire(device, nvmeReservationType, false, 0 /*acquire*/, acquireRes, 16);
    }
#endif
    return ret;
}

int release_Reservation(tDevice *device, uint64_t key, eReservationType resType)
{
    int ret = NOT_SUPPORTED;
    if (device->drive_info.drive_type == SCSI_DRIVE)
    {
        uint8_t releaseRes[PR_OUT_BASIC_MIN_LENGTH] = { 0 };
        uint8_t scsiReservationType = 0;
        persistentReserveOutBasic prData;
        memset(&prData, 0, sizeof(persistentReserveOutBasic));
        prData.reservationKey = key;
        format_Basic_Info(releaseRes, PR_OUT_BASIC_MIN_LENGTH, &prData);
        switch (resType)
        {
        case RES_TYPE_READ_SHARED:
            scsiReservationType = 0;
            break;
        case RES_TYPE_WRITE_EXCLUSIVE:
            scsiReservationType = 1;
            break;
        case RES_TYPE_READ_EXCLUSIVE://obsolete - old scsi only
            scsiReservationType = 2;
            break;
        case RES_TYPE_EXCLUSIVE_ACCESS:
            scsiReservationType = 3;
            break;
        case RES_TYPE_SHARED_ACCESS://obsolete - old SCSI only
            scsiReservationType = 4;
            break;
        case RES_TYPE_WRITE_EXCLUSIVE_REGISTRANTS_ONLY:
            scsiReservationType = 5;
            break;
        case RES_TYPE_EXCLUSIVE_ACCESS_REGISTRANTS_ONLY:
            scsiReservationType = 6;
            break;
        case RES_TYPE_WRITE_EXCLUSIVE_ALL_REGISTRANTS:
            scsiReservationType = 7;
            break;
        case RES_TYPE_EXCLUSIVE_ACCESS_ALL_REGISTRANTS:
            scsiReservationType = 8;
            break;
        case RES_TYPE_NO_RESERVATION:
        default:
            return BAD_PARAMETER;
        }
        ret = scsi_Persistent_Reserve_Out(device, SCSI_PERSISTENT_RESERVE_OUT_RELEASE, 0, scsiReservationType, PR_OUT_BASIC_MIN_LENGTH, releaseRes);
    }
#if !defined (DISABLE_NVME_PASSTHROUGH)
    else if (device->drive_info.drive_type == NVME_DRIVE)
    {
        uint8_t releaseRes[8] = { 0 };
        uint8_t nvmeReservationType = 0;
        releaseRes[0] = M_Byte0(key);
        releaseRes[1] = M_Byte1(key);
        releaseRes[2] = M_Byte2(key);
        releaseRes[3] = M_Byte3(key);
        releaseRes[4] = M_Byte4(key);
        releaseRes[5] = M_Byte5(key);
        releaseRes[6] = M_Byte6(key);
        releaseRes[7] = M_Byte7(key);
        switch (resType)
        {
        case RES_TYPE_WRITE_EXCLUSIVE:
            nvmeReservationType = 1;
            break;
        case RES_TYPE_EXCLUSIVE_ACCESS:
            nvmeReservationType = 2;
            break;
        case RES_TYPE_WRITE_EXCLUSIVE_REGISTRANTS_ONLY:
            nvmeReservationType = 3;
            break;
        case RES_TYPE_EXCLUSIVE_ACCESS_REGISTRANTS_ONLY:
            nvmeReservationType = 4;
            break;
        case RES_TYPE_WRITE_EXCLUSIVE_ALL_REGISTRANTS:
            nvmeReservationType = 5;
            break;
        case RES_TYPE_EXCLUSIVE_ACCESS_ALL_REGISTRANTS:
            nvmeReservationType = 6;
            break;
        case RES_TYPE_NO_RESERVATION:
        default:
            return BAD_PARAMETER;
        }
        ret = nvme_Reservation_Release(device, nvmeReservationType, false, 0 /*release*/, releaseRes, 8);
    }
#endif
    return ret;
}

int clear_Reservations(tDevice *device, uint64_t key)
{
    int ret = NOT_SUPPORTED;
    if (device->drive_info.drive_type == SCSI_DRIVE)
    {
        uint8_t clearRes[PR_OUT_BASIC_MIN_LENGTH] = { 0 };
        persistentReserveOutBasic prData;
        memset(&prData, 0, sizeof(persistentReserveOutBasic));
        prData.reservationKey = key;
        format_Basic_Info(clearRes, PR_OUT_BASIC_MIN_LENGTH, &prData);
        ret = scsi_Persistent_Reserve_Out(device, SCSI_PERSISTENT_RESERVE_OUT_CLEAR, 0, 0, PR_OUT_BASIC_MIN_LENGTH, clearRes);
    }
#if !defined (DISABLE_NVME_PASSTHROUGH)
    else if (device->drive_info.drive_type == NVME_DRIVE)
    {
        uint8_t clearRes[8] = { 0 };
        clearRes[0] = M_Byte0(key);
        clearRes[1] = M_Byte1(key);
        clearRes[2] = M_Byte2(key);
        clearRes[3] = M_Byte3(key);
        clearRes[4] = M_Byte4(key);
        clearRes[5] = M_Byte5(key);
        clearRes[6] = M_Byte6(key);
        clearRes[7] = M_Byte7(key);
        ret = nvme_Reservation_Release(device, 0, false, 1 /*clear*/, clearRes, 8);
    }
#endif
    return ret;
}

int preempt_Reservation(tDevice *device, uint64_t key, uint64_t preemptKey, bool abort, eReservationType resType)
{
    int ret = NOT_SUPPORTED;
    if (device->drive_info.drive_type == SCSI_DRIVE)
    {
        uint8_t preemptRes[PR_OUT_BASIC_MIN_LENGTH] = { 0 };
        uint8_t scsiReservationType = 0;
        persistentReserveOutBasic prData;
        memset(&prData, 0, sizeof(persistentReserveOutBasic));
        prData.reservationKey = key;
        prData.serviceActionReservationKey = preemptKey;
        format_Basic_Info(preemptRes, PR_OUT_BASIC_MIN_LENGTH, &prData);
        switch (resType)
        {
        case RES_TYPE_READ_SHARED:
            scsiReservationType = 0;
            break;
        case RES_TYPE_WRITE_EXCLUSIVE:
            scsiReservationType = 1;
            break;
        case RES_TYPE_READ_EXCLUSIVE://obsolete - old scsi only
            scsiReservationType = 2;
            break;
        case RES_TYPE_EXCLUSIVE_ACCESS:
            scsiReservationType = 3;
            break;
        case RES_TYPE_SHARED_ACCESS://obsolete - old SCSI only
            scsiReservationType = 4;
            break;
        case RES_TYPE_WRITE_EXCLUSIVE_REGISTRANTS_ONLY:
            scsiReservationType = 5;
            break;
        case RES_TYPE_EXCLUSIVE_ACCESS_REGISTRANTS_ONLY:
            scsiReservationType = 6;
            break;
        case RES_TYPE_WRITE_EXCLUSIVE_ALL_REGISTRANTS:
            scsiReservationType = 7;
            break;
        case RES_TYPE_EXCLUSIVE_ACCESS_ALL_REGISTRANTS:
            scsiReservationType = 8;
            break;
        case RES_TYPE_NO_RESERVATION:
        default:
            return BAD_PARAMETER;
        }
        ret = scsi_Persistent_Reserve_Out(device, abort ? SCSI_PERSISTENT_RESERVE_OUT_PREEMPT_AND_ABORT : SCSI_PERSISTENT_RESERVE_OUT_PREEMPT, 0, scsiReservationType, PR_OUT_BASIC_MIN_LENGTH, preemptRes);
    }
#if !defined (DISABLE_NVME_PASSTHROUGH)
    else if (device->drive_info.drive_type == NVME_DRIVE)
    {
        uint8_t preemptRes[16] = { 0 };
        uint8_t nvmeReservationType = 0;
        preemptRes[0] = M_Byte0(key);
        preemptRes[1] = M_Byte1(key);
        preemptRes[2] = M_Byte2(key);
        preemptRes[3] = M_Byte3(key);
        preemptRes[4] = M_Byte4(key);
        preemptRes[5] = M_Byte5(key);
        preemptRes[6] = M_Byte6(key);
        preemptRes[7] = M_Byte7(key);
        preemptRes[8] = M_Byte0(preemptKey);
        preemptRes[9] = M_Byte1(preemptKey);
        preemptRes[10] = M_Byte2(preemptKey);
        preemptRes[11] = M_Byte3(preemptKey);
        preemptRes[12] = M_Byte4(preemptKey);
        preemptRes[13] = M_Byte5(preemptKey);
        preemptRes[14] = M_Byte6(preemptKey);
        preemptRes[15] = M_Byte7(preemptKey);
        switch (resType)
        {
        case RES_TYPE_WRITE_EXCLUSIVE:
            nvmeReservationType = 1;
            break;
        case RES_TYPE_EXCLUSIVE_ACCESS:
            nvmeReservationType = 2;
            break;
        case RES_TYPE_WRITE_EXCLUSIVE_REGISTRANTS_ONLY:
            nvmeReservationType = 3;
            break;
        case RES_TYPE_EXCLUSIVE_ACCESS_REGISTRANTS_ONLY:
            nvmeReservationType = 4;
            break;
        case RES_TYPE_WRITE_EXCLUSIVE_ALL_REGISTRANTS:
            nvmeReservationType = 5;
            break;
        case RES_TYPE_EXCLUSIVE_ACCESS_ALL_REGISTRANTS:
            nvmeReservationType = 6;
            break;
        case RES_TYPE_NO_RESERVATION:
        default:
            return BAD_PARAMETER;
        }
        ret = nvme_Reservation_Acquire(device, nvmeReservationType, false, abort ? 2 /*preempt & abort*/ : 1 /*preempt*/, preemptRes, 16);
    }
#endif
    return ret;
}

//TODO: replace lost reservation
//TODO: register and move: Requires transport ID which there is not a good way to deal with right now with how these tools work-TJE
