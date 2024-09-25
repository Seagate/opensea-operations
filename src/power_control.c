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
// \file power_control.c
// \brief This file defines the functions for power related changes to drives.

#include "common_types.h"
#include "precision_timer.h"
#include "memory_safety.h"
#include "type_conversion.h"
#include "string_utils.h"
#include "bit_manip.h"
#include "code_attributes.h"
#include "math_utils.h"
#include "error_translation.h"
#include "io_utils.h"
#include "prng.h"

#include "operations_Common.h"
#include "power_control.h"
#include "logs.h"
#include "cmds.h"
#include "operations.h" //for reset to defaults bit check

//There is no specific way to enable or disable this on SCSI, so this simulates the bahaviour according to what we see with ATA
static eReturnValues scsi_Enable_Disable_EPC_Feature(tDevice *device, eEPCFeatureSet lba_field)
{
    eReturnValues ret = UNKNOWN;

    if (lba_field == ENABLE_EPC_NOT_SET)
    {
        ret = BAD_PARAMETER;
    }
    else if (lba_field == ENABLE_EPC)
    {
        //read the default mode page and write it back to the drive to set the default values from the drive...should be the same as enabling
        ret = scsi_Set_Device_Power_Mode(device, true, true, PWR_CND_ALL, 0, false);
    }
    else if (lba_field == DISABLE_EPC)
    {
        //read the current settings, disable any timers that are enabled and set the timer values to zero...should be the same as disabling.
        ret = scsi_Set_Device_Power_Mode(device, false, false, PWR_CND_ALL, 0, true);
    }
    else
    {
        ret = BAD_PARAMETER;
    }

    return ret;
}

//-----------------------------------------------------------------------------
//
//  enable_Disable_EPC_Feature (tDevice *device, eEPCFeatureSet lba_field))
//
//! \brief   Enable the EPC Feature or Disable it [SATA Only)
//
//  Entry:
//!   \param[in]  device file descriptor
//!   \param[in]  lba_field what is the LBA Field should be set to. 
//  Exit:
//!   \return SUCCESS = good, !SUCCESS something went wrong see error codes
//
//-----------------------------------------------------------------------------
eReturnValues enable_Disable_EPC_Feature(tDevice *device, eEPCFeatureSet lba_field)
{
    eReturnValues ret = UNKNOWN;

    if (lba_field == ENABLE_EPC_NOT_SET)
    {
        ret = BAD_PARAMETER;
        return ret;
    }
    if (device->drive_info.drive_type == ATA_DRIVE)
    {
        ret = ata_Set_Features(device, SF_EXTENDED_POWER_CONDITIONS, 0, C_CAST(uint8_t, lba_field), 0, 0);
    }
    else if (device->drive_info.drive_type == SCSI_DRIVE)
    {
        ret = scsi_Enable_Disable_EPC_Feature(device, lba_field);
    }
    else
    {
        ret = NOT_SUPPORTED;
    }
    return ret;
}

eReturnValues print_Current_Power_Mode(tDevice *device)
{
    eReturnValues ret = UNKNOWN;
    if (device->drive_info.drive_type == ATA_DRIVE)
    {
        uint8_t powerMode = 0;
        //first check if EPC feature is supported and/or enabled
        uint8_t epcFeature = 0;//0 - disabled, 1 - supported, 2 - enabled.
        uint8_t *identifyData = C_CAST(uint8_t*, safe_calloc_aligned(LEGACY_DRIVE_SEC_SIZE, sizeof(uint8_t), device->os_info.minimumAlignment));
        if (identifyData == M_NULLPTR)
        {
            perror("Calloc Failure!\n");
            return MEMORY_FAILURE;
        }

        if (SUCCESS == ata_Identify(device, identifyData, LEGACY_DRIVE_SEC_SIZE))
        {
            //check word 119 bit 7 for EPC support
            uint16_t *identWordPTR = C_CAST(uint16_t*, identifyData);
            if ((identWordPTR[119] & BIT7) > 0)
            {
                epcFeature = 1;
            }
            //check word 120 bit 7 for EPC enabled
            if ((identWordPTR[120] & BIT7) > 0)
            {
                epcFeature = 2;
            }
        }
        else
        {
            printf("Unable to detect if EPC feature status! Cannot continue!\n");//this SHOULDN'T happen
            safe_free_aligned(&identifyData);
            return FAILURE;
        }
        safe_free_aligned(&identifyData);

        if (SUCCESS == ata_Check_Power_Mode(device, &powerMode))
        {
            printf("Device is in the ");
            switch (powerMode)
            {
            case 0x00:
                if (epcFeature != 2)
                {
                    printf("PM2: Standby state.\n");
                }
                else
                {
                    printf("PM2: Standby state and device is in the Standby_z power condition\n");
                }
                break;
            case 0x01://should only happen when EPC is enabled according to the spec...so not checking EPC feature
                printf("PM2: Standby state and the device is in the Standby_y power condition\n");
                break;
            case 0x40://NV cache
                printf("PM0: Active state. NV Cache power is enabled and spindle is spun/spinning down\n");
                break;
            case 0x41://NV cache
                printf("PM0: Active state. NV Cache power is enabled and spindle is spun/spinning up\n");
                break;
            case 0x80:
                if (epcFeature == 0)
                {
                    printf("PM1: Idle state\n");
                }
                else
                {
                    printf("PM1: Idle state. EPC feature disabled");
                }
                break;
            case 0x81://should only happen when EPC is enabled according to the spec...so not checking EPC feature
                printf("PM1: Idle state and the device is in the Idle_a power condition\n");
                break;
            case 0x82://should only happen when EPC is enabled according to the spec...so not checking EPC feature
                printf("PM1: Idle state and the device is in the Idle_b power condition\n");
                break;
            case 0x83://should only happen when EPC is enabled according to the spec...so not checking EPC feature
                printf("PM1: Idle state and the device is in the Idle_c power condition\n");
                break;
            case 0xFF:
                printf("PM0: Active state or PM1: Idle State\n");
                break;
            default:
                printf("Unknown/Reserved Power State\n");
                break;
            }
            ret = SUCCESS;
        }
        else
        {
            if (VERBOSITY_QUIET < device->deviceVerbosity)
            {
                printf("Unable to retrive current power mode!\n");
            }
            ret = FAILURE;
        }
    }
    else if (device->drive_info.drive_type == NVME_DRIVE)
    {
        uint32_t powerMode = 0;
        ret = get_Power_State(device, &powerMode, CURRENT_VALUE);
        if (ret == SUCCESS)
        {
            printf("Device is in Power State %" PRIu32 "\n", powerMode);
        }
        else
        {
            if (VERBOSITY_QUIET < device->deviceVerbosity)
            {
                printf("Unable to retrive current power state!\n");
            }
        }
    }
    else
    {
        /*
        NOTE: Removed the code which was checking to see if the power mode is supported
              mainly because it was changing the power state of the drive. -MA
        */
        uint8_t *senseData = C_CAST(uint8_t*, safe_calloc_aligned(SPC3_SENSE_LEN, sizeof(uint8_t), device->os_info.minimumAlignment));
        if (!senseData)
        {
            perror("Calloc Failure!\n");
            return MEMORY_FAILURE;
        }
        if (SUCCESS == scsi_Request_Sense_Cmd(device, false, senseData, SPC3_SENSE_LEN))
        {
            bool issuetur = false;
            //requested fixed format sensedata, so parse it. If we don't find what we are looking for post the "unable to retrieve current power mode" message
            if ((senseData[0] & 0x7F) == SCSI_SENSE_CUR_INFO_FIXED || (senseData[0] & 0x7F) == SCSI_SENSE_DEFER_ERR_FIXED)
            {
                uint8_t acs = senseData[12];
                uint8_t acsq = senseData[13];
                ret = SUCCESS;
                printf("Device is in the ");
                switch (acs)
                {
                case 0x5E:
                    switch (acsq)
                    {
                    case 0x00:
                        printf("Low Power state\n");
                        break;
                    case 0x01:
                        printf("Idle state activated by timer\n");
                        break;
                    case 0x03:
                        printf("Idle state activated by host command\n");
                        break;
                    case 0x02:
                        printf("Standby_Z state activated by timer\n");
                        break;
                    case 0x04:
                        printf("Standby_Z state activated by host command\n");
                        break;
                    case 0x05:
                        printf("Idle_B state activated by timer\n");
                        break;
                    case 0x06:
                        printf("Idle_B state activated by host command\n");
                        break;
                    case 0x07:
                        printf("Idle_C state activated by timer\n");
                        break;
                    case 0x08:
                        printf("Idle_C state activated by host command\n");
                        break;
                    case 0x09:
                        printf("Standby_Y state activated by timer\n");
                        break;
                    case 0x0A:
                        printf("Standby_Y state activated by host command\n");
                        break;
                    default:
                        issuetur = true;
                        break;
                    }
                    break;
                default:
                    issuetur = true;
                    break;
                }
                
            }
            else
            {
                issuetur = true;
            }
            if (issuetur == true)
            {
                scsiStatus returnedStatus;
                memset(&returnedStatus, 0, sizeof(scsiStatus));
                ret = scsi_Test_Unit_Ready(device, &returnedStatus);
                if ((ret == SUCCESS) && (returnedStatus.senseKey == SENSE_KEY_NO_ERROR))
                {
                    //assume active state
                    printf("Device is in active state or an unknown power state.\n");
                }
                else if (returnedStatus.senseKey == SENSE_KEY_NOT_READY)
                {
                    //check asc and ascq if spinup command is required
                    if (returnedStatus.asc == 0x04 && returnedStatus.ascq == 0x02)
                    {
                        printf("Standby state\n");//activated by host command???
                    }
                    else 
                    {
                        printf("Unknown power state. Unit reports: ");
                        show_Test_Unit_Ready_Status(device);
                    }
                }
                else
                {
                    printf("Unknown power state. Unit reports: ");
                    show_Test_Unit_Ready_Status(device);
                    ret = FAILURE;
                }
            }
        }
        safe_free_aligned(&senseData);
    }
    return ret;
}

eReturnValues transition_Power_State(tDevice *device, ePowerConditionID newState)
{
    eReturnValues ret = NOT_SUPPORTED;
    if (device->drive_info.drive_type == ATA_DRIVE)
    {
        switch (newState)
        {
        case PWR_CND_STANDBY_Z:
            ret = ata_Set_Features(device, SF_EXTENDED_POWER_CONDITIONS, PWR_CND_STANDBY_Z, \
                                    EPC_GO_TO_POWER_CONDITION, RESERVED, RESERVED);
            break;
        case PWR_CND_STANDBY_Y:
            ret = ata_Set_Features(device, SF_EXTENDED_POWER_CONDITIONS, PWR_CND_STANDBY_Y, \
                                    EPC_GO_TO_POWER_CONDITION, RESERVED, RESERVED);
            break;
        case PWR_CND_IDLE_A:
            ret = ata_Set_Features(device, SF_EXTENDED_POWER_CONDITIONS, PWR_CND_IDLE_A, \
                                    EPC_GO_TO_POWER_CONDITION, RESERVED, RESERVED);
            break;
        case PWR_CND_IDLE_B:
            ret = ata_Set_Features(device, SF_EXTENDED_POWER_CONDITIONS, PWR_CND_IDLE_B, \
                                    EPC_GO_TO_POWER_CONDITION, RESERVED, RESERVED);
            break;
        case PWR_CND_IDLE_C:
            ret = ata_Set_Features(device, SF_EXTENDED_POWER_CONDITIONS, PWR_CND_IDLE_C, \
                                    EPC_GO_TO_POWER_CONDITION, RESERVED, RESERVED);
            break;
        case PWR_CND_ACTIVE: 
            //No such thing in ATA. Attempt by sending read-verify to a few sectors on the disk randomly (Early SAT translation recommended this behavior)
            seed_64(C_CAST(uint64_t, time(M_NULLPTR)));
            for (uint8_t counter = 0; counter < 5; ++counter)
            {
                uint64_t lba = 0;
                lba = random_Range_64(0, device->drive_info.deviceMaxLba);
                ata_Read_Verify(device, lba, 1);
            }
            //TODO: zoned devices will require a different method. See SAT4 or later for details.
            //SAT-6 says:
            //EPC: set features for all supported power conditions: ID set to FFh, enable set to zero, save set to zero to disable all current timers
            //     Then issue an idle immediate. If no errors: In active condition, else return an error.
            //non-EPC: send idle with feature 0, count 0, lba 0. If no error: In active state.
            ret = SUCCESS;
            break;
        case PWR_CND_IDLE://send idle immediate
            ret = ata_Idle_Immediate(device, false);
            break;
        case PWR_CND_IDLE_UNLOAD://send idle immediate - unload
            if ((is_ATA_Identify_Word_Valid_With_Bits_14_And_15(device->drive_info.IdentifyData.ata.Word084) && device->drive_info.IdentifyData.ata.Word084 & BIT13) ||
                (is_ATA_Identify_Word_Valid_With_Bits_14_And_15(device->drive_info.IdentifyData.ata.Word087) && device->drive_info.IdentifyData.ata.Word087 & BIT13)
                )
            {
                ret = ata_Idle_Immediate(device, true);
            }
            else
            {
                ret = NOT_SUPPORTED;
            }
            break;
        case PWR_CND_STANDBY://send standby immediate
            ret = ata_Standby_Immediate(device);
            break;
        case PWR_CND_SLEEP://send sleep command
            ret = ata_Sleep(device);
            break;
        default:
            if (VERBOSITY_QUIET < device->deviceVerbosity)
            {
                printf("Power State Transition is not supported on this device type at this time\n");
            }
            ret = NOT_SUPPORTED;
            break;
        }
    }
    else //if (device->drive_info.drive_type == SCSI_DRIVE) /*removed the if SCSI here to handle NVMe or other translations*/
    {
        switch (newState)
        {
        case PWR_CND_ACTIVE:
            if (device->drive_info.scsiVersion > SCSI_VERSION_SCSI2)//checking for support after SCSI2. This isn't perfect, but should be ok for now.
            {
                ret = scsi_Start_Stop_Unit(device, false, 0, PC_ACTIVE, false, false, false);
            }
            else
            {
                //before you could specify a power condition, you used the "Start" bit as a way to move from standby to active
                ret = scsi_Start_Stop_Unit(device, false, 0, PC_START_VALID, false, false, true);
            }
            break;
        case PWR_CND_STANDBY_Z:
            ret = scsi_Start_Stop_Unit(device, false, 0, PC_STANDBY, false, false, false);
            break;
        case PWR_CND_STANDBY_Y:
            ret = scsi_Start_Stop_Unit(device, false, 1, PC_STANDBY, false, false, false);
            break;
        case PWR_CND_IDLE_A:
            ret = scsi_Start_Stop_Unit(device, false, 0, PC_IDLE, false, false, false);
            break;
        case PWR_CND_IDLE_B:
            ret = scsi_Start_Stop_Unit(device, false, 1, PC_IDLE, false, false, false);
            break;
        case PWR_CND_IDLE_C:
            ret = scsi_Start_Stop_Unit(device, false, 2, PC_IDLE, false, false, false);
            break;
        case PWR_CND_IDLE://send idle immediate
            ret = scsi_Start_Stop_Unit(device, false, 0, PC_IDLE, false, false, false);
            break;
        case PWR_CND_IDLE_UNLOAD://send idle immediate - unload
            if (device->drive_info.scsiVersion > SCSI_VERSION_SPC_2)
            {
                ret = scsi_Start_Stop_Unit(device, false, 1, PC_IDLE, false, false, false);
            }
            break;
        case PWR_CND_STANDBY://send standby immediate
            if (device->drive_info.scsiVersion > SCSI_VERSION_SCSI2)//checking for support after SCSI2. This isn't perfect, but should be ok for now.
            {
                ret = scsi_Start_Stop_Unit(device, false, 0, PC_STANDBY, false, false, false);
            }
            else
            {
                //before you could specify a power condition, you used the "Start" bit as a way to move from standby to active
                ret = scsi_Start_Stop_Unit(device, false, 0, 0, false, false, false);
            }
            break;
        case PWR_CND_SLEEP://send sleep command
            if (device->drive_info.scsiVersion > SCSI_VERSION_SCSI2)//checking for support after SCSI2. This isn't perfect, but should be ok for now.
            {
                ret = scsi_Start_Stop_Unit(device, false, 0, PC_SLEEP, false, false, false);//This is obsolete since SBC2...but we'll send it anyways
            }
            break;
        default:
            if (VERBOSITY_QUIET < device->deviceVerbosity)
            {
                printf("Power State Transition is not supported on this device type at this time\n");
            }
            ret = NOT_SUPPORTED;
            break;
        }
    }
    return ret;
}

eReturnValues get_NVMe_Power_States(tDevice* device, ptrNVMeSupportedPowerStates nvmps)
{
    eReturnValues ret = NOT_SUPPORTED;
    if (device && device->drive_info.drive_type == NVME_DRIVE && nvmps)
    {
        ret = SUCCESS;
        //use cached NVMe identify ctrl data since this won't change.
        uint16_t driveMaxPowerStates = device->drive_info.IdentifyData.nvme.ctrl.npss + 1;//plus 1 since this is zeroes based
        memset(nvmps, 0, sizeof(nvmeSupportedPowerStates));
        for (uint16_t powerIter = 0; powerIter < driveMaxPowerStates && powerIter < MAXIMUM_NVME_POWER_STATES; ++powerIter)
        {
            nvmps->powerState[powerIter].powerStateNumber = powerIter;
            //set max power if available
            if (device->drive_info.IdentifyData.nvme.ctrl.psd[powerIter].maxPower > 0)
            {
                nvmps->powerState[powerIter].maxPowerValid = true;
                nvmps->powerState[powerIter].maxPowerMilliWatts = device->drive_info.IdentifyData.nvme.ctrl.psd[powerIter].maxPower;
                if ((device->drive_info.IdentifyData.nvme.ctrl.psd[powerIter].flags & BIT0) == 0)
                {
                    //reported in centiwatts, so convert it
                    nvmps->powerState[powerIter].maxPowerMilliWatts *= 10;
                }
            }
            if (device->drive_info.IdentifyData.nvme.ctrl.psd[powerIter].flags & BIT1)
            {
                nvmps->powerState[powerIter].isNonOperationalPS = true;
            }
            //entry exit latency
            nvmps->powerState[powerIter].entryLatency = device->drive_info.IdentifyData.nvme.ctrl.psd[powerIter].entryLat;
            nvmps->powerState[powerIter].exitLatency = device->drive_info.IdentifyData.nvme.ctrl.psd[powerIter].exitLat;
            //r/w throughput and latency are all 5bit fields, so stripping off the top 3 bits when assigning in case future revisions make changes.
            nvmps->powerState[powerIter].relativeReadThroughput = device->drive_info.IdentifyData.nvme.ctrl.psd[powerIter].readTPut & 0x1F;
            nvmps->powerState[powerIter].relativeReadLatency = device->drive_info.IdentifyData.nvme.ctrl.psd[powerIter].readLat & 0x1F;
            nvmps->powerState[powerIter].relativeWriteThroughput = device->drive_info.IdentifyData.nvme.ctrl.psd[powerIter].writeLput & 0x1F;
            nvmps->powerState[powerIter].relativeWriteLatency = device->drive_info.IdentifyData.nvme.ctrl.psd[powerIter].writeLat & 0x1F;
            //set idle power if available
            if (device->drive_info.IdentifyData.nvme.ctrl.psd[powerIter].idlePower > 0)
            {
                nvmps->powerState[powerIter].idlePowerValid = true;
                nvmps->powerState[powerIter].idlePowerMilliWatts = device->drive_info.IdentifyData.nvme.ctrl.psd[powerIter].idlePower;
                uint8_t scale = M_GETBITRANGE(device->drive_info.IdentifyData.nvme.ctrl.psd[powerIter].idleScale, 7, 6);
                if (scale == 2)
                {
                    //reported in centiwatts, so convert it
                    nvmps->powerState[powerIter].idlePowerMilliWatts *= 10;
                }
                else if (scale == 3 || scale == 0)
                {
                    //cannot handle these cases, so disable reporting idle power since we cannot report it correctly
                    nvmps->powerState[powerIter].idlePowerValid = false;
                }
            }
            //set active power if available
            if (device->drive_info.IdentifyData.nvme.ctrl.psd[powerIter].activePower > 0)
            {
                nvmps->powerState[powerIter].activePowerValid = true;
                nvmps->powerState[powerIter].activePowerMilliWatts = device->drive_info.IdentifyData.nvme.ctrl.psd[powerIter].activePower;
                nvmps->powerState[powerIter].activePowerWorkload = M_GETBITRANGE(device->drive_info.IdentifyData.nvme.ctrl.psd[powerIter].activeWorkScale, 2, 0);
                uint8_t scale = M_GETBITRANGE(device->drive_info.IdentifyData.nvme.ctrl.psd[powerIter].activeWorkScale, 7, 6);
                if (scale == 2)
                {
                    //reported in centiwatts, so convert it
                    nvmps->powerState[powerIter].idlePowerMilliWatts *= 10;
                }
                else if (scale == 3 || scale == 0)
                {
                    //cannot handle these cases, so disable reporting idle power since we cannot report it correctly
                    nvmps->powerState[powerIter].activePowerValid = false;
                }
            }
            nvmps->numberOfPowerStates += 1;
        }
        //finish by reading which is the current power state that the device is operating in
        get_Power_State(device, &nvmps->activePowerState, CURRENT_VALUE);
    }
    return ret;
}

static uint8_t calculate_Relative_NVM_Latency_Or_Throughput(uint8_t value, uint16_t numberOfPowerStates)
{
    uint8_t relativeVal = C_CAST(uint8_t, 100.0 - (C_CAST(double, value) / C_CAST(double, numberOfPowerStates) * 100.0));

    return relativeVal;
}

//convert the entry/exit latencies into human readable strings
//This was basically copy-pasted from common_public.c in opensea-transport. Need to make this a function in opensea-common instead
#define NVM_POWER_ENT_EX_TIME_MAX_STR_LEN 10
static const char* convert_NVM_Latency_To_HR_Time_Str(uint64_t timeInNanoSeconds, char timeStr[NVM_POWER_ENT_EX_TIME_MAX_STR_LEN])
{
    const char* ctimestr = &timeStr[0];
    double printTime = C_CAST(double, timeInNanoSeconds);
    uint8_t unitCounter = 0;
    bool breakLoop = false;
    while (printTime > 1 && unitCounter <= 6)
    {
        switch (unitCounter)
        {
        case 6://shouldn't get this far...
            break;
        case 5://h to d
            if ((printTime / 24.0) < 1)
            {
                breakLoop = true;
            }
            break;
        case 4://m to h
        case 3://s to m
            if ((printTime / 60.0) < 1)
            {
                breakLoop = true;
            }
            break;
        case 0://ns to us
        case 1://us to ms
        case 2://ms to s
        default:
            if ((printTime / 1000.0) < 1)
            {
                breakLoop = true;
            }
            break;
        }
        if (breakLoop)
        {
            break;
        }
        switch (unitCounter)
        {
        case 6://shouldn't get this far...
            break;
        case 5://h to d
            printTime /= 24.0;
            break;
        case 4://m to h
        case 3://s to m
            printTime /= 60.0;
            break;
        case 0://ns to us
        case 1://us to ms
        case 2://ms to s
        default:
            printTime /= 1000.0;
            break;
        }
        if (unitCounter == 6)
        {
            break;
        }
        ++unitCounter;
    }
#define NVM_LAT_UNIT_STR_LEN 3
    DECLARE_ZERO_INIT_ARRAY(char, units, NVM_LAT_UNIT_STR_LEN);
    switch (unitCounter)
    {
    case 6://we shouldn't get to a days value, but room for future large drives I guess...-TJE
        snprintf(units, NVM_LAT_UNIT_STR_LEN, "d");
        break;
    case 5:
        snprintf(units, NVM_LAT_UNIT_STR_LEN, "h");
        break;
    case 4:
        snprintf(units, NVM_LAT_UNIT_STR_LEN, "m");
        break;
    case 3:
        snprintf(units, NVM_LAT_UNIT_STR_LEN, "s");
        break;
    case 2:
        snprintf(units, NVM_LAT_UNIT_STR_LEN, "ms");
        break;
    case 1:
        snprintf(units, NVM_LAT_UNIT_STR_LEN, "us");
        break;
    case 0:
        snprintf(units, NVM_LAT_UNIT_STR_LEN, "ns");
        break;
    default://couldn't get a good conversion or something weird happened so show original nanoseconds.
        snprintf(units, NVM_LAT_UNIT_STR_LEN, "ns");
        printTime = C_CAST(double, timeInNanoSeconds);
        break;
    }
    snprintf(timeStr, NVM_POWER_ENT_EX_TIME_MAX_STR_LEN, "%0.02f %s", printTime, units);
    return ctimestr;
}

#define NVM_POWER_WATTS_MAX_STR_LEN 10
void print_NVM_Power_States(ptrNVMeSupportedPowerStates nvmps)
{
    if (nvmps)
    {
        printf("\nSupported NVMe Power States\n");
        //flags = non operational, current power state
        printf("\t* = current power state\n");
        printf("\t! = non-operational power state\n");
        printf("\tNR = this value was not reported by the device\n");
        printf("\n\tRead/write through put and latency meanings:\n");
        printf("\t\tRRT = Relative Read Throughput\n");
        printf("\t\tRRL = Relative Read Latency\n");
        printf("\t\tRWT = Relative Write Throughput\n");
        printf("\t\tRWL = Relative Write Latency\n");
        printf("\t\tRead/Write throughput and latency values are scaled from 0 - 100%%.\n");
        printf("\t100%% = max performance, 0%% = minimum relative performance.\n");
        //flags | # | max power | idle power | active power | latencies and throughputs (can be N/A when not reported)
        printf("\n   #  Max Power: Idle Power: Active Power: RRT: RRL: RWT: RWL: Entry Time: Exit Time:\n");
        printf("-------------------------------------------------------------------------------------\n");
        //all values should be in watts. 1.00 watts, 0.25 watts, etc
        //should relative values be scaled to percentages? 100% = max performance. This may be easier for some to understand than a random number
        for (uint16_t psIter = 0; psIter < nvmps->numberOfPowerStates && psIter < MAXIMUM_NVME_POWER_STATES; ++psIter)
        {
            char flags[3] = { ' ', ' ', '\0' };
            DECLARE_ZERO_INIT_ARRAY(char, maxPowerWatts, NVM_POWER_WATTS_MAX_STR_LEN);
            DECLARE_ZERO_INIT_ARRAY(char, idlePowerWatts, NVM_POWER_WATTS_MAX_STR_LEN);
            DECLARE_ZERO_INIT_ARRAY(char, activePowerWatts, NVM_POWER_WATTS_MAX_STR_LEN);
            DECLARE_ZERO_INIT_ARRAY(char, entryTime, NVM_POWER_ENT_EX_TIME_MAX_STR_LEN);
            DECLARE_ZERO_INIT_ARRAY(char, exitTime, NVM_POWER_ENT_EX_TIME_MAX_STR_LEN);
            if (nvmps->activePowerState == nvmps->powerState[psIter].powerStateNumber)
            {
                //mark as active with a *
                flags[0] = '*';
            }
            if (nvmps->powerState[psIter].isNonOperationalPS)
            {
                //mark as non operational with a !
                flags[1] = '!';
            }
            if (nvmps->powerState[psIter].maxPowerValid)
            {
                snprintf(maxPowerWatts, NVM_POWER_WATTS_MAX_STR_LEN, "%.4f W", (nvmps->powerState[psIter].maxPowerMilliWatts / 1000.0));
            }
            else
            {
                snprintf(maxPowerWatts, NVM_POWER_WATTS_MAX_STR_LEN, "NR");
            }
            if (nvmps->powerState[psIter].idlePowerValid)
            {
                snprintf(idlePowerWatts, NVM_POWER_WATTS_MAX_STR_LEN, "%.4f W", (nvmps->powerState[psIter].idlePowerMilliWatts / 1000.0));
            }
            else
            {
                snprintf(idlePowerWatts, NVM_POWER_WATTS_MAX_STR_LEN, "NR");
            }
            if (nvmps->powerState[psIter].activePowerValid)
            {
                snprintf(activePowerWatts, NVM_POWER_WATTS_MAX_STR_LEN, "%.4f W", (nvmps->powerState[psIter].activePowerMilliWatts / 1000.0));
            }
            else
            {
                snprintf(activePowerWatts, NVM_POWER_WATTS_MAX_STR_LEN, "NR");
            }
            if (nvmps->powerState[psIter].entryLatency > 0)
            {
                convert_NVM_Latency_To_HR_Time_Str(nvmps->powerState[psIter].entryLatency * UINT64_C(1000), entryTime);
            }
            else
            {
                snprintf(entryTime, NVM_POWER_ENT_EX_TIME_MAX_STR_LEN, "NR");
            }
            if (nvmps->powerState[psIter].exitLatency > 0)
            {
                convert_NVM_Latency_To_HR_Time_Str(nvmps->powerState[psIter].exitLatency * UINT64_C(1000), exitTime);
            }
            else
            {
                snprintf(exitTime, NVM_POWER_ENT_EX_TIME_MAX_STR_LEN, "NR");
            }
            printf("%s%2" PRIu16 " %10s  %10s    %10s %4" PRIu8 " %4" PRIu8 " %4" PRIu8 " %4" PRIu8 "  %10s %10s\n", flags, nvmps->powerState[psIter].powerStateNumber, maxPowerWatts, idlePowerWatts, activePowerWatts,
                calculate_Relative_NVM_Latency_Or_Throughput(nvmps->powerState[psIter].relativeReadThroughput, nvmps->numberOfPowerStates - 1),
                calculate_Relative_NVM_Latency_Or_Throughput(nvmps->powerState[psIter].relativeReadLatency, nvmps->numberOfPowerStates - 1),
                calculate_Relative_NVM_Latency_Or_Throughput(nvmps->powerState[psIter].relativeWriteThroughput, nvmps->numberOfPowerStates - 1),
                calculate_Relative_NVM_Latency_Or_Throughput(nvmps->powerState[psIter].relativeWriteLatency, nvmps->numberOfPowerStates - 1),
                entryTime, exitTime
                );
        }
    }
    return;
}

eReturnValues transition_NVM_Power_State(tDevice *device, uint8_t newState)
{
    eReturnValues ret = NOT_SUPPORTED;
    if (device->drive_info.drive_type == NVME_DRIVE)
    {
        nvmeFeaturesCmdOpt cmdOpts;
        memset(&cmdOpts, 0, sizeof(cmdOpts));
        cmdOpts.featSetGetValue = newState;
        cmdOpts.fid = NVME_FEAT_POWER_MGMT_;
        cmdOpts.sel = NVME_CURRENT_FEAT_SEL;
        ret = nvme_Set_Features(device, &cmdOpts);
        if (ret != SUCCESS)
        {
            if (VERBOSITY_QUIET < device->deviceVerbosity)
            {
                printf("Power State Transition Failed in NVMe Set Features command\n");
            }
        }
    }
    return ret;
}

static eReturnValues ata_Set_EPC_Power_Mode(tDevice *device, ePowerConditionID powerCondition, ptrPowerConditionSettings powerConditionSettings)
{
    eReturnValues ret = SUCCESS;
    if (!powerConditionSettings || powerCondition == PWR_CND_ACTIVE)
    {
        return BAD_PARAMETER;
    }
    if (powerConditionSettings->powerConditionValid)
    {
        if (powerConditionSettings->restoreToDefault)
        {
            //this command is restoring the power conditions from the drive's default settings (bit6) and saving them upon completion (bit4)...the other option is to return to the saved settings, but we aren't going to support that with this option right now
            ret = ata_Set_Features(device, SF_EXTENDED_POWER_CONDITIONS, C_CAST(uint8_t, powerCondition), EPC_RESTORE_POWER_CONDITION_SETTINGS | BIT6 | BIT4, RESERVED, RESERVED);
        }
        else//we aren't restoring settings, so we need to set things up to save settings
        {
            uint8_t lbalo = 0;
            uint8_t lbaMid = 0;
            uint16_t lbaHi = 0;
            if (powerConditionSettings->timerValid)
            {
                lbalo = EPC_SET_POWER_CONDITION_TIMER;
                if (powerConditionSettings->timerInHundredMillisecondIncrements <= UINT16_MAX)
                {
                    lbaMid = M_Byte0(powerConditionSettings->timerInHundredMillisecondIncrements);
                    lbaHi = M_Byte1(powerConditionSettings->timerInHundredMillisecondIncrements);
                }
                else
                {
                    //need to convert to a number of minutes to send to the drive instead!
                    lbalo |= BIT7;//meaning unit in minutes instead of 100ms
                    uint64_t convertedMinutes = (C_CAST(uint64_t, powerConditionSettings->timerInHundredMillisecondIncrements) * UINT64_C(100)) / UINT64_C(60000);
                    //now, this value should be able to be sent...
                    lbaMid = M_Byte0(convertedMinutes);
                    lbaHi = M_Byte1(convertedMinutes);
                }
            }
            else //they didn't enter a timer value so this command will do the EXACT same thing...just decided to use a different feature to EPC
            {
                lbalo = EPC_SET_POWER_CONDITION_STATE;
            }
            if (powerConditionSettings->enableValid && powerConditionSettings->enable)
            {
                lbalo |= BIT5;
            }
            //set the save bit
            lbalo |= BIT4;
            //issue the command
            ret = ata_Set_Features(device, SF_EXTENDED_POWER_CONDITIONS, C_CAST(uint8_t, powerCondition), lbalo, lbaMid, lbaHi);
        }
    }
    return ret;
}

//enableDisable = true means enable, false means disable
eReturnValues ata_Set_Device_Power_Mode(tDevice *device, bool restoreDefaults, bool enableDisable, \
    ePowerConditionID powerCondition, uint32_t powerModeTimer, bool powerModeTimerValid)
{
    eReturnValues ret = UNKNOWN;
    //first verify the device supports the EPC feature
    uint8_t *ataDataBuffer = C_CAST(uint8_t*, safe_calloc_aligned(LEGACY_DRIVE_SEC_SIZE, sizeof(uint8_t), device->os_info.minimumAlignment));
    if (!ataDataBuffer)
    {
        perror("calloc failure!\n");
        return MEMORY_FAILURE;
    }
    if (SUCCESS == ata_Identify(device, ataDataBuffer, LEGACY_DRIVE_SEC_SIZE))
    {
        uint16_t *wordPtr = C_CAST(uint16_t*, ataDataBuffer);
        if ((wordPtr[119] & BIT7) == 0)
        {
            //this means EPC is not supported by the drive.
            if (VERBOSITY_QUIET < device->deviceVerbosity)
            {
                printf("Device does not support the Extended Power Control Feature!\n");
            }
            safe_free_aligned(&ataDataBuffer);
            return NOT_SUPPORTED;
        }
    }
    else
    {
        if (VERBOSITY_QUIET < device->deviceVerbosity)
        {
            printf("Failed to check if drive supports EPC feature!!\n");
        }
        safe_free_aligned(&ataDataBuffer);
        return FAILURE;
    }
    safe_free_aligned(&ataDataBuffer);
    //if we go this far, then we know that we support the required EPC feature
    powerConditionSettings powerSettings;
    memset(&powerSettings, 0, sizeof(powerConditionSettings));
    powerSettings.powerConditionValid = true;
    if (restoreDefaults)
    {
        powerSettings.restoreToDefault = restoreDefaults;
    }
    else
    {
        powerSettings.enableValid = true;
        powerSettings.enable = enableDisable;
        powerSettings.timerValid = powerModeTimerValid;
        powerSettings.timerInHundredMillisecondIncrements = powerModeTimer;
    }
    ret = ata_Set_EPC_Power_Mode(device, powerCondition, &powerSettings);
    return ret;
}

eReturnValues scsi_Set_Power_Conditions(tDevice *device, bool restoreAllToDefaults, ptrPowerConditionTimers powerConditions)
{
    //If restore all to defaults, then read the default mode page, then write it back.
    //Write all applicable changes back to the drive, checking each structure as it goes.
    eReturnValues ret = SUCCESS;
    uint16_t powerConditionsPageLength = 0;
    uint8_t *powerConditionsPage = M_NULLPTR;
    if (restoreAllToDefaults)
    {
        if (scsi_MP_Reset_To_Defaults_Supported(device))
        {
            ret = scsi_Mode_Select_10(device, 0, true, true, true, M_NULLPTR, 0);//RTD bit is set and supported by the drive which will reset the page to defaults for us without a data transfer or multiple commands.
        }
        else
        {
            //read the default mode page, then send it to the drive with mode select.
            powerConditionsPageLength = MODE_PARAMETER_HEADER_10_LEN + MP_POWER_CONDITION_LEN;//*should* be maximum size we need assuming no block descriptor
            powerConditionsPage = C_CAST(uint8_t*, safe_calloc_aligned(powerConditionsPageLength, sizeof(uint8_t), device->os_info.minimumAlignment));
            if (!powerConditionsPage)
            {
                return MEMORY_FAILURE;
            }
            if (SUCCESS == (ret = scsi_Mode_Sense_10(device, MP_POWER_CONDTION, powerConditionsPageLength, 0, true, false, MPC_DEFAULT_VALUES, powerConditionsPage)))
            {
                //got the page, now send it to the drive with a mode select
                ret = scsi_Mode_Select_10(device, powerConditionsPageLength, true, true, false, powerConditionsPage, powerConditionsPageLength);
            }
            safe_free_aligned(&powerConditionsPage);
        }
    }
    else
    {
        if (!powerConditions)
        {
            return BAD_PARAMETER;
        }
        //Check if anything in the incoming list is requesting default values so we can allocate and read the defaults for those conditions before sending to the drive.
        if ((powerConditions->idle_a.powerConditionValid && powerConditions->idle_a.restoreToDefault) ||
            (powerConditions->idle_b.powerConditionValid && powerConditions->idle_b.restoreToDefault) ||
            (powerConditions->idle_c.powerConditionValid && powerConditions->idle_c.restoreToDefault) ||
            (powerConditions->standby_y.powerConditionValid && powerConditions->standby_y.restoreToDefault) ||
            (powerConditions->standby_z.powerConditionValid && powerConditions->standby_z.restoreToDefault) ||
            (powerConditions->powerModeBackgroundValid && powerConditions->powerModeBackgroundResetDefault) ||
            (powerConditions->checkConditionFlags.ccfIdleValid && powerConditions->checkConditionFlags.ccfIdleResetDefault) ||
            (powerConditions->checkConditionFlags.ccfStandbyValid && powerConditions->checkConditionFlags.ccfStandbyResetDefault) ||
            (powerConditions->checkConditionFlags.ccfStopValid && powerConditions->checkConditionFlags.ccfStopResetDefault)
            )
        {
            //Read the default mode page, then save whichever things need defaults into the proper structure
            powerConditionsPageLength = MODE_PARAMETER_HEADER_10_LEN + MP_POWER_CONDITION_LEN;//*should* be maximum size we need assuming no block descriptor
            powerConditionsPage = C_CAST(uint8_t*, safe_calloc_aligned(powerConditionsPageLength, sizeof(uint8_t), device->os_info.minimumAlignment));
            if (!powerConditionsPage)
            {
                return MEMORY_FAILURE;
            }
            if (SUCCESS == (ret = scsi_Mode_Sense_10(device, MP_POWER_CONDTION, powerConditionsPageLength, 0, true, false, MPC_DEFAULT_VALUES, powerConditionsPage)))
            {
                //uint16_t modeDataLength = M_BytesTo2ByteValue(powerConditionsPage[0], powerConditionsPage[1]);
                uint16_t blockDescriptorLength = M_BytesTo2ByteValue(powerConditionsPage[6], powerConditionsPage[7]);
                uint32_t mpStartOffset = MODE_PARAMETER_HEADER_10_LEN + blockDescriptorLength;
                //read the power conditions values that were requested
                if (powerConditions->idle_a.powerConditionValid && powerConditions->idle_a.restoreToDefault)
                {
                    powerConditions->idle_a.enableValid = true;
                    powerConditions->idle_a.enable = M_ToBool(powerConditionsPage[mpStartOffset + 3] & BIT1);
                    powerConditions->idle_a.timerValid = true;
                    powerConditions->idle_a.timerInHundredMillisecondIncrements = M_BytesTo4ByteValue(powerConditionsPage[mpStartOffset + 4], powerConditionsPage[mpStartOffset + 5], powerConditionsPage[mpStartOffset + 6], powerConditionsPage[mpStartOffset + 7]);
                    powerConditions->idle_a.restoreToDefault = false;//turn this off now that we have the other settings stored.
                }
                if (powerConditions->standby_z.powerConditionValid && powerConditions->standby_z.restoreToDefault)
                {
                    powerConditions->standby_z.enableValid = true;
                    powerConditions->standby_z.enable = M_ToBool(powerConditionsPage[mpStartOffset + 3] & BIT0);
                    powerConditions->standby_z.timerValid = true;
                    powerConditions->standby_z.timerInHundredMillisecondIncrements = M_BytesTo4ByteValue(powerConditionsPage[mpStartOffset + 8], powerConditionsPage[mpStartOffset + 9], powerConditionsPage[mpStartOffset + 10], powerConditionsPage[mpStartOffset + 11]);
                    powerConditions->standby_z.restoreToDefault = false;//turn this off now that we have the other settings stored.
                }
                if (powerConditions->powerModeBackgroundValid && powerConditions->powerModeBackgroundResetDefault)
                {
                    powerConditions->powerModeBackGroundRelationShip = M_GETBITRANGE(powerConditionsPage[mpStartOffset + 2], 7, 6);
                    powerConditions->powerModeBackgroundResetDefault = false;
                }
                if (powerConditionsPage[mpStartOffset + 1] > 0x0A)//newer EPC drives support these, but older devices do not.
                {
                    if (powerConditions->idle_b.powerConditionValid && powerConditions->idle_b.restoreToDefault)
                    {
                        powerConditions->idle_b.enableValid = true;
                        powerConditions->idle_b.enable = M_ToBool(powerConditionsPage[mpStartOffset + 3] & BIT2);
                        powerConditions->idle_b.timerValid = true;
                        powerConditions->idle_b.timerInHundredMillisecondIncrements = M_BytesTo4ByteValue(powerConditionsPage[mpStartOffset + 12], powerConditionsPage[mpStartOffset + 13], powerConditionsPage[mpStartOffset + 14], powerConditionsPage[mpStartOffset + 15]);
                        powerConditions->idle_b.restoreToDefault = false;//turn this off now that we have the other settings stored.
                    }
                    if (powerConditions->idle_c.powerConditionValid && powerConditions->idle_c.restoreToDefault)
                    {
                        powerConditions->idle_c.enableValid = true;
                        powerConditions->idle_c.enable = M_ToBool(powerConditionsPage[mpStartOffset + 3] & BIT3);
                        powerConditions->idle_c.timerValid = true;
                        powerConditions->idle_c.timerInHundredMillisecondIncrements = M_BytesTo4ByteValue(powerConditionsPage[mpStartOffset + 16], powerConditionsPage[mpStartOffset + 17], powerConditionsPage[mpStartOffset + 18], powerConditionsPage[mpStartOffset + 19]);
                        powerConditions->idle_c.restoreToDefault = false;//turn this off now that we have the other settings stored.
                    }
                    if (powerConditions->standby_y.powerConditionValid && powerConditions->standby_y.restoreToDefault)
                    {
                        powerConditions->standby_y.enableValid = true;
                        powerConditions->standby_y.enable = M_ToBool(powerConditionsPage[mpStartOffset + 2] & BIT0);
                        powerConditions->standby_y.timerValid = true;
                        powerConditions->standby_y.timerInHundredMillisecondIncrements = M_BytesTo4ByteValue(powerConditionsPage[mpStartOffset + 20], powerConditionsPage[mpStartOffset + 21], powerConditionsPage[mpStartOffset + 22], powerConditionsPage[mpStartOffset + 23]);
                        powerConditions->standby_y.restoreToDefault = false;//turn this off now that we have the other settings stored.
                    }
                    //CCF fields
                    if (powerConditions->checkConditionFlags.ccfIdleValid && powerConditions->checkConditionFlags.ccfIdleResetDefault)
                    {
                        powerConditions->checkConditionFlags.ccfIdleMode = M_GETBITRANGE(powerConditionsPage[mpStartOffset + 39], 7, 6);
                        powerConditions->checkConditionFlags.ccfIdleResetDefault = false;
                    }
                    if (powerConditions->checkConditionFlags.ccfStandbyValid && powerConditions->checkConditionFlags.ccfStandbyResetDefault)
                    {
                        powerConditions->checkConditionFlags.ccfStandbyMode = M_GETBITRANGE(powerConditionsPage[mpStartOffset + 39], 5, 4);
                        powerConditions->checkConditionFlags.ccfStandbyResetDefault = false;
                    }
                    if (powerConditions->checkConditionFlags.ccfStopValid && powerConditions->checkConditionFlags.ccfStopResetDefault)
                    {
                        powerConditions->checkConditionFlags.ccfStopMode = M_GETBITRANGE(powerConditionsPage[mpStartOffset + 39], 3, 2);
                        powerConditions->checkConditionFlags.ccfStopResetDefault = false;
                    }
                }
            }
            else
            {
                safe_free_aligned(&powerConditionsPage);
                return ret;
            }
            safe_free_aligned(&powerConditionsPage);
        }

        //Now, read the current settings mode page, make any necessary changes, then send it to the drive and we're done.
        powerConditionsPageLength = MODE_PARAMETER_HEADER_10_LEN + MP_POWER_CONDITION_LEN;//*should* be maximum size we need assuming no block descriptor
        powerConditionsPage = C_CAST(uint8_t*, safe_calloc_aligned(powerConditionsPageLength, sizeof(uint8_t), device->os_info.minimumAlignment));
        if (!powerConditionsPage)
        {
            return MEMORY_FAILURE;
        }
        if (SUCCESS == (ret = scsi_Mode_Sense_10(device, MP_POWER_CONDTION, powerConditionsPageLength, 0, true, false, MPC_CURRENT_VALUES, powerConditionsPage)))
        {
            //uint16_t modeDataLength = M_BytesTo2ByteValue(powerConditionsPage[0], powerConditionsPage[1]);
            uint16_t blockDescriptorLength = M_BytesTo2ByteValue(powerConditionsPage[6], powerConditionsPage[7]);
            uint32_t mpStartOffset = MODE_PARAMETER_HEADER_10_LEN + blockDescriptorLength;
            //read the power conditions values that were requested
            if (powerConditions->idle_a.powerConditionValid && !powerConditions->idle_a.restoreToDefault)
            {
                if (powerConditions->idle_a.enableValid)
                {
                    if (powerConditions->idle_a.enable)
                    {
                        M_SET_BIT8(powerConditionsPage[mpStartOffset + 3], 1);
                    }
                    else
                    {
                        M_CLEAR_BIT8(powerConditionsPage[mpStartOffset + 3], 1);
                    }
                }
                if (powerConditions->idle_a.timerValid)
                {
                    powerConditionsPage[mpStartOffset + 4] = M_Byte3(powerConditions->idle_a.timerInHundredMillisecondIncrements);
                    powerConditionsPage[mpStartOffset + 5] = M_Byte2(powerConditions->idle_a.timerInHundredMillisecondIncrements);
                    powerConditionsPage[mpStartOffset + 6] = M_Byte1(powerConditions->idle_a.timerInHundredMillisecondIncrements);
                    powerConditionsPage[mpStartOffset + 7] = M_Byte0(powerConditions->idle_a.timerInHundredMillisecondIncrements);
                }
            }
            if (powerConditions->standby_z.powerConditionValid && !powerConditions->standby_z.restoreToDefault)
            {
                if (powerConditions->standby_z.enableValid)
                {
                    if (powerConditions->standby_z.enable)
                    {
                        M_SET_BIT8(powerConditionsPage[mpStartOffset + 3], 0);
                    }
                    else
                    {
                        M_CLEAR_BIT8(powerConditionsPage[mpStartOffset + 3], 0);
                    }
                }
                if (powerConditions->standby_z.timerValid)
                {
                    powerConditionsPage[mpStartOffset + 8] = M_Byte3(powerConditions->standby_z.timerInHundredMillisecondIncrements);
                    powerConditionsPage[mpStartOffset + 9] = M_Byte2(powerConditions->standby_z.timerInHundredMillisecondIncrements);
                    powerConditionsPage[mpStartOffset + 10] = M_Byte1(powerConditions->standby_z.timerInHundredMillisecondIncrements);
                    powerConditionsPage[mpStartOffset + 11] = M_Byte0(powerConditions->standby_z.timerInHundredMillisecondIncrements);
                }
            }
            if (powerConditions->powerModeBackgroundValid && !powerConditions->powerModeBackgroundResetDefault)
            {
                powerConditionsPage[mpStartOffset + 2] |= powerConditions->powerModeBackGroundRelationShip << 6;
            }
            if (powerConditionsPage[mpStartOffset + 1] > 0x0A)//newer EPC drives support these, but older devices do not.
            {
                if (powerConditions->idle_b.powerConditionValid && !powerConditions->idle_b.restoreToDefault)
                {
                    if (powerConditions->idle_b.enableValid)
                    {
                        if (powerConditions->idle_b.enable)
                        {
                            M_SET_BIT8(powerConditionsPage[mpStartOffset + 3], 2);
                        }
                        else
                        {
                            M_CLEAR_BIT8(powerConditionsPage[mpStartOffset + 3], 2);
                        }
                    }
                    if (powerConditions->idle_b.timerValid)
                    {
                        powerConditionsPage[mpStartOffset + 12] = M_Byte3(powerConditions->idle_b.timerInHundredMillisecondIncrements);
                        powerConditionsPage[mpStartOffset + 13] = M_Byte2(powerConditions->idle_b.timerInHundredMillisecondIncrements);
                        powerConditionsPage[mpStartOffset + 14] = M_Byte1(powerConditions->idle_b.timerInHundredMillisecondIncrements);
                        powerConditionsPage[mpStartOffset + 15] = M_Byte0(powerConditions->idle_b.timerInHundredMillisecondIncrements);
                    }
                }
                if (powerConditions->idle_c.powerConditionValid && !powerConditions->idle_c.restoreToDefault)
                {
                    if (powerConditions->idle_c.enableValid)
                    {
                        if (powerConditions->idle_c.enable)
                        {
                            M_SET_BIT8(powerConditionsPage[mpStartOffset + 3], 3);
                        }
                        else
                        {
                            M_CLEAR_BIT8(powerConditionsPage[mpStartOffset + 3], 3);
                        }
                    }
                    if (powerConditions->idle_c.timerValid)
                    {
                        powerConditionsPage[mpStartOffset + 16] = M_Byte3(powerConditions->idle_c.timerInHundredMillisecondIncrements);
                        powerConditionsPage[mpStartOffset + 17] = M_Byte2(powerConditions->idle_c.timerInHundredMillisecondIncrements);
                        powerConditionsPage[mpStartOffset + 18] = M_Byte1(powerConditions->idle_c.timerInHundredMillisecondIncrements);
                        powerConditionsPage[mpStartOffset + 19] = M_Byte0(powerConditions->idle_c.timerInHundredMillisecondIncrements);
                    }
                }
                if (powerConditions->standby_y.powerConditionValid && !powerConditions->standby_y.restoreToDefault)
                {
                    if (powerConditions->standby_y.enableValid)
                    {
                        if (powerConditions->standby_y.enable)
                        {
                            M_SET_BIT8(powerConditionsPage[mpStartOffset + 2], 0);
                        }
                        else
                        {
                            M_CLEAR_BIT8(powerConditionsPage[mpStartOffset + 2], 0);
                        }
                    }
                    if (powerConditions->standby_y.timerValid)
                    {
                        powerConditionsPage[mpStartOffset + 20] = M_Byte3(powerConditions->standby_y.timerInHundredMillisecondIncrements);
                        powerConditionsPage[mpStartOffset + 21] = M_Byte2(powerConditions->standby_y.timerInHundredMillisecondIncrements);
                        powerConditionsPage[mpStartOffset + 22] = M_Byte1(powerConditions->standby_y.timerInHundredMillisecondIncrements);
                        powerConditionsPage[mpStartOffset + 23] = M_Byte0(powerConditions->standby_y.timerInHundredMillisecondIncrements);
                    }
                }
                //CCF fields
                if (powerConditions->checkConditionFlags.ccfIdleValid && !powerConditions->checkConditionFlags.ccfIdleResetDefault)
                {
                    powerConditionsPage[mpStartOffset + 39] |= get_bit_range_uint8(powerConditions->checkConditionFlags.ccfIdleMode, 1, 0) << 6;
                }
                if (powerConditions->checkConditionFlags.ccfStandbyValid && !powerConditions->checkConditionFlags.ccfStandbyResetDefault)
                {
                    powerConditionsPage[mpStartOffset + 39] |= get_bit_range_uint8(powerConditions->checkConditionFlags.ccfStandbyMode, 1, 0) << 4;
                }
                if (powerConditions->checkConditionFlags.ccfStopValid && !powerConditions->checkConditionFlags.ccfStopResetDefault)
                {
                    powerConditionsPage[mpStartOffset + 39] |= get_bit_range_uint8(powerConditions->checkConditionFlags.ccfStopMode, 1, 0) << 2;
                }
            }
            //send the modified data to the drive
            ret = scsi_Mode_Select_10(device, powerConditionsPageLength, true, true, false, powerConditionsPage, powerConditionsPageLength);
            safe_free_aligned(&powerConditionsPage);
        }
        else
        {
            safe_free_aligned(&powerConditionsPage);
            return ret;
        }
        safe_free_aligned(&powerConditionsPage);
    }
    safe_free_aligned(&powerConditionsPage);
    return ret;
}

static eReturnValues scsi_Set_EPC_Power_Conditions(tDevice *device, bool restoreAllToDefaults, ptrPowerConditionTimers powerConditions)
{
    return scsi_Set_Power_Conditions(device, restoreAllToDefaults, powerConditions);
}

//This function will go through and change each requested setting.
//The first failure that happens will cause the function to fail and not proceed to set any other timer values.
static eReturnValues ata_Set_EPC_Power_Conditions(tDevice *device, bool restoreAllToDefaults, ptrPowerConditionTimers powerConditions)
{
    eReturnValues ret = NOT_SUPPORTED;
    if (is_ATA_Identify_Word_Valid(device->drive_info.IdentifyData.ata.Word086) && device->drive_info.IdentifyData.ata.Word086 & BIT15)//words 119, 120 valid
    {
        if (is_ATA_Identify_Word_Valid_With_Bits_14_And_15(device->drive_info.IdentifyData.ata.Word119) && device->drive_info.IdentifyData.ata.Word119 & BIT7)
        {
            if (restoreAllToDefaults)
            {
                powerConditionSettings allSettings;
                memset(&allSettings, 0, sizeof(powerConditionSettings));
                allSettings.powerConditionValid = true;
                allSettings.restoreToDefault = true;
                ret = ata_Set_EPC_Power_Mode(device, PWR_CND_ALL, &allSettings);
            }
            else
            {
                if (powerConditions)
                {
                    //go through each and every power condition and for each valid one, pass it to the ATA function. If unsuccessful, return immediately
                    //This does it in the same top-down order as the SCSI mode page to keep things working "the same" between the two
                    if (powerConditions->idle_a.powerConditionValid)
                    {
                        ret = ata_Set_EPC_Power_Mode(device, PWR_CND_IDLE_A, &powerConditions->idle_a);
                        if (ret != SUCCESS)
                        {
                            return ret;
                        }
                    }
                    if (powerConditions->standby_z.powerConditionValid)
                    {
                        ret = ata_Set_EPC_Power_Mode(device, PWR_CND_STANDBY_Z, &powerConditions->standby_z);
                        if (ret != SUCCESS)
                        {
                            return ret;
                        }
                    }
                    if (powerConditions->idle_b.powerConditionValid)
                    {
                        ret = ata_Set_EPC_Power_Mode(device, PWR_CND_IDLE_B, &powerConditions->idle_b);
                        if (ret != SUCCESS)
                        {
                            return ret;
                        }
                    }
                    if (powerConditions->idle_c.powerConditionValid)
                    {
                        ret = ata_Set_EPC_Power_Mode(device, PWR_CND_IDLE_C, &powerConditions->idle_c);
                        if (ret != SUCCESS)
                        {
                            return ret;
                        }
                    }
                    if (powerConditions->standby_y.powerConditionValid)
                    {
                        ret = ata_Set_EPC_Power_Mode(device, PWR_CND_STANDBY_Y, &powerConditions->standby_y);
                        if (ret != SUCCESS)
                        {
                            return ret;
                        }
                    }
                }
                else
                {
                    ret = BAD_PARAMETER;
                }
            }
        }
    }
    return ret;
}

eReturnValues set_EPC_Power_Conditions(tDevice *device, bool restoreAllToDefaults, ptrPowerConditionTimers powerConditions)
{
    eReturnValues ret = NOT_SUPPORTED;
    if (device->drive_info.drive_type == ATA_DRIVE)
    {
        ret = ata_Set_EPC_Power_Conditions(device, restoreAllToDefaults, powerConditions);
    }
    else
    {
        //assume and try SCSI method
        ret = scsi_Set_EPC_Power_Conditions(device, restoreAllToDefaults, powerConditions);
    }
    return ret;
}

//enableDisable = true means enable, false means disable
eReturnValues scsi_Set_Device_Power_Mode(tDevice *device, bool restoreDefaults, bool enableDisable, \
    ePowerConditionID powerCondition, uint32_t powerModeTimer, bool powerModeTimerValid)
{
    eReturnValues ret = NOT_SUPPORTED;
    //first we need to check that VPD page 8Ah (power condition) exists...and we can possibly use that information to return "not supported, etc"
    uint8_t *powerConditionVPD = C_CAST(uint8_t*, safe_calloc_aligned(VPD_POWER_CONDITION_LEN, sizeof(uint8_t), device->os_info.minimumAlignment));//size of 18 is defined in SPC4 for this VPD page
    if (!powerConditionVPD)
    {
        perror("calloc failure!");
        return MEMORY_FAILURE;
    }
    if (SUCCESS == scsi_Inquiry(device, powerConditionVPD, VPD_POWER_CONDITION_LEN, POWER_CONDITION, true, false))//Not technically necessary, but will keep this only functional on EPC drives.
    {
        if (powerConditionVPD[1] != POWER_CONDITION)//make sure we got the correct page...if we did, we will proceed to do what we can with the other passed in options
        {
            if (VERBOSITY_QUIET < device->deviceVerbosity)
            {
                printf("Failed to check if drive supports modifying power conditions!\n");
            }
            safe_free_aligned(&powerConditionVPD);
            return FAILURE;
        }
    }
    else
    {
        if (VERBOSITY_QUIET < device->deviceVerbosity)
        {
            printf("Failed to check if drive supports modifying power conditions!\n");
        }
        safe_free_aligned(&powerConditionVPD);
        return FAILURE;
    }

    //Calling new scsi_Set_Power_Conditions() function since it's setup to handle EPC and legacy drives. Just need to setup structures in here then call that function properly.
    if (restoreDefaults)
    {
        if (powerCondition == PWR_CND_ALL)
        {
            ret = scsi_Set_Power_Conditions(device, true, M_NULLPTR);
        }
        else
        {
            powerConditionTimers powerConditions;
            memset(&powerConditions, 0, sizeof(powerConditionTimers));
            switch (powerCondition)
            {
            case PWR_CND_IDLE_A:
                powerConditions.idle_a.powerConditionValid = true;
                powerConditions.idle_a.restoreToDefault = true;
                break;
            case PWR_CND_IDLE_B:
                powerConditions.idle_b.powerConditionValid = true;
                powerConditions.idle_b.restoreToDefault = true;
                break;
            case PWR_CND_IDLE_C:
                powerConditions.idle_c.powerConditionValid = true;
                powerConditions.idle_c.restoreToDefault = true;
                break;
            case PWR_CND_STANDBY_Y:
                powerConditions.standby_y.powerConditionValid = true;
                powerConditions.standby_y.restoreToDefault = true;
                break;
            case PWR_CND_STANDBY_Z:
                powerConditions.standby_z.powerConditionValid = true;
                powerConditions.standby_z.restoreToDefault = true;
                break;
            default:
                safe_free_aligned(&powerConditionVPD);
                return BAD_PARAMETER;
            }
            ret = scsi_Set_Power_Conditions(device, false, &powerConditions);
        }
    }
    else
    {
        //not restoring, so figure out what timer is being changed.
        powerConditionTimers powerConditions;
        memset(&powerConditions, 0, sizeof(powerConditionTimers));
        //All checks for NOT_SUPPORTED are based off of VPD page support bits. Current assumption is if that timer is supported by the device, then so is changing that timer or changing it from enabled to disabled.
        //It would probably be a good idea to also check the changable mode page, but that is not done right now - TJE
        switch (powerCondition)
        {
        case PWR_CND_IDLE_A:
            if (!(powerConditionVPD[5] & BIT0))
            {
                safe_free_aligned(&powerConditionVPD);
                return NOT_SUPPORTED;
            }
            powerConditions.idle_a.powerConditionValid = true;
            //check value for enable/disable bit
            powerConditions.idle_a.enableValid = true;
            powerConditions.idle_a.enable = enableDisable;
            //set the timer value
            powerConditions.idle_a.timerValid = powerModeTimerValid;
            powerConditions.idle_a.timerInHundredMillisecondIncrements = powerModeTimer;
            break;
        case PWR_CND_IDLE_B:
            if (!(powerConditionVPD[5] & BIT1))
            {
                safe_free_aligned(&powerConditionVPD);
                return NOT_SUPPORTED;
            }
            powerConditions.idle_b.powerConditionValid = true;
            //check value for enable/disable bit
            powerConditions.idle_b.enableValid = true;
            powerConditions.idle_b.enable = enableDisable;
            //set the timer value
            powerConditions.idle_b.timerValid = powerModeTimerValid;
            powerConditions.idle_b.timerInHundredMillisecondIncrements = powerModeTimer;
            break;
        case PWR_CND_IDLE_C:
            if (!(powerConditionVPD[5] & BIT2))
            {
                safe_free_aligned(&powerConditionVPD);
                return NOT_SUPPORTED;
            }
            powerConditions.idle_c.powerConditionValid = true;
            //check value for enable/disable bit
            powerConditions.idle_c.enableValid = true;
            powerConditions.idle_c.enable = enableDisable;
            //set the timer value
            powerConditions.idle_c.timerValid = powerModeTimerValid;
            powerConditions.idle_c.timerInHundredMillisecondIncrements = powerModeTimer;
            break;
        case PWR_CND_STANDBY_Y:
            if (!(powerConditionVPD[4] & BIT1))
            {
                safe_free_aligned(&powerConditionVPD);
                return NOT_SUPPORTED;
            }
            powerConditions.standby_y.powerConditionValid = true;
            //check value for enable/disable bit
            powerConditions.standby_y.enableValid = true;
            powerConditions.standby_y.enable = enableDisable;
            //set the timer value
            powerConditions.standby_y.timerValid = powerModeTimerValid;
            powerConditions.standby_y.timerInHundredMillisecondIncrements = powerModeTimer;
            break;
        case PWR_CND_STANDBY_Z:
            if (!(powerConditionVPD[4] & BIT0))
            {
                safe_free_aligned(&powerConditionVPD);
                return NOT_SUPPORTED;
            }
            powerConditions.standby_z.powerConditionValid = true;
            //check value for enable/disable bit
            powerConditions.standby_z.enableValid = true;
            powerConditions.standby_z.enable = enableDisable;
            //set the timer value
            powerConditions.standby_z.timerValid = powerModeTimerValid;
            powerConditions.standby_z.timerInHundredMillisecondIncrements = powerModeTimer;
            break;
        case PWR_CND_ALL:
            //setup all enable/disable bits and timer values.
            if (powerConditionVPD[4] & BIT1)//standby_y
            {
                powerConditions.standby_y.powerConditionValid = true;
                //check value for enable/disable bit
                powerConditions.standby_y.enableValid = true;
                powerConditions.standby_y.enable = enableDisable;
                //set the timer value
                powerConditions.standby_y.timerValid = powerModeTimerValid;
                powerConditions.standby_y.timerInHundredMillisecondIncrements = powerModeTimer;
            }
            else if (powerConditionVPD[4] & BIT0)//standby_z
            {
                powerConditions.standby_z.powerConditionValid = true;
                //check value for enable/disable bit
                powerConditions.standby_z.enableValid = true;
                powerConditions.standby_z.enable = enableDisable;
                //set the timer value
                powerConditions.standby_z.timerValid = powerModeTimerValid;
                powerConditions.standby_z.timerInHundredMillisecondIncrements = powerModeTimer;
            }
            else if (powerConditionVPD[5] & BIT2)//idle_c
            {
                powerConditions.idle_c.powerConditionValid = true;
                //check value for enable/disable bit
                powerConditions.idle_c.enableValid = true;
                powerConditions.idle_c.enable = enableDisable;
                //set the timer value
                powerConditions.idle_c.timerValid = powerModeTimerValid;
                powerConditions.idle_c.timerInHundredMillisecondIncrements = powerModeTimer;
            }
            else if (powerConditionVPD[5] & BIT1)//idle_b
            {
                powerConditions.idle_b.powerConditionValid = true;
                //check value for enable/disable bit
                powerConditions.idle_b.enableValid = true;
                powerConditions.idle_b.enable = enableDisable;
                //set the timer value
                powerConditions.idle_b.timerValid = powerModeTimerValid;
                powerConditions.idle_b.timerInHundredMillisecondIncrements = powerModeTimer;
            }
            else if (powerConditionVPD[5] & BIT0)//idle_a
            {
                powerConditions.idle_a.powerConditionValid = true;
                //check value for enable/disable bit
                powerConditions.idle_a.enableValid = true;
                powerConditions.idle_a.enable = enableDisable;
                //set the timer value
                powerConditions.idle_a.timerValid = powerModeTimerValid;
                powerConditions.idle_a.timerInHundredMillisecondIncrements = powerModeTimer;
            }
            break;
        default:
            safe_free_aligned(&powerConditionVPD);
            return BAD_PARAMETER;
        }
        ret = scsi_Set_Power_Conditions(device, false, &powerConditions);
    }
    safe_free_aligned(&powerConditionVPD);
    return ret;
}

//enableDisable = true means enable, false means disable
eReturnValues set_Device_Power_Mode(tDevice *device, bool restoreDefaults, bool enableDisable, \
    ePowerConditionID powerCondition, uint32_t powerModeTimer, bool powerModeTimerValid)
{
    eReturnValues ret = UNKNOWN;

    if (device->drive_info.drive_type == ATA_DRIVE)
    {
        ret = ata_Set_Device_Power_Mode(device, restoreDefaults, enableDisable, powerCondition, powerModeTimer, powerModeTimerValid);
    }
    else if (device->drive_info.drive_type == SCSI_DRIVE)
    {
        ret = scsi_Set_Device_Power_Mode(device, restoreDefaults, enableDisable, powerCondition, powerModeTimer, powerModeTimerValid);
    }
    else
    {
        if (VERBOSITY_QUIET < device->deviceVerbosity)
        {
            printf("Power choice configuration not supported on this device type at this time.\n");
        }
        ret = NOT_SUPPORTED;
    }
    return ret;
}

eReturnValues get_Power_State(tDevice *device, uint32_t * powerState, eFeatureModeSelect selectValue)
{
    eReturnValues ret = UNKNOWN;
    if (device->drive_info.drive_type == NVME_DRIVE)
    {
        nvmeFeaturesCmdOpt cmdOpts;
        memset(&cmdOpts, 0, sizeof(nvmeFeaturesCmdOpt));
        switch (selectValue)
        {
        case CURRENT_VALUE:
            cmdOpts.fid = NVME_FEAT_POWER_MGMT_;
            cmdOpts.sel = NVME_CURRENT_FEAT_SEL;
            ret = nvme_Get_Features(device, &cmdOpts);
            if (ret == SUCCESS)
            {
                *powerState = cmdOpts.featSetGetValue;
            }
            break;
        case DEFAULT_VALUE:
        case SAVED_VALUE:
        case CAPABILITIES:
        case CHANGEABLE_VALUE:
        default:
            if (VERBOSITY_QUIET < device->deviceVerbosity)
            {
                printf("Power State=0x%x is currently not supported on this device.\n", selectValue);
            }
            ret = NOT_SUPPORTED;
            break;

        }
    }
    else
    {
        if (VERBOSITY_QUIET < device->deviceVerbosity)
        {
            printf("Get Power State is currently not supported on this device type at this time.\n");
        }
        ret = NOT_SUPPORTED;
    }

    return ret;
}

eReturnValues get_Power_Consumption_Identifiers(tDevice *device, ptrPowerConsumptionIdentifiers identifiers)
{
    eReturnValues ret = NOT_SUPPORTED;
    if (device->drive_info.drive_type == SCSI_DRIVE)//this is only available on SCSI drives.
    {
        uint32_t powerConsumptionLength = 0;
        if (SUCCESS == get_SCSI_VPD_Page_Size(device, POWER_CONSUMPTION, &powerConsumptionLength))
        {
            uint8_t *powerConsumptionPage = C_CAST(uint8_t*, safe_calloc_aligned(powerConsumptionLength, sizeof(uint8_t), device->os_info.minimumAlignment));
            if (!powerConsumptionPage)
            {
                return MEMORY_FAILURE;
            }
            if (SUCCESS == scsi_Inquiry(device, powerConsumptionPage, powerConsumptionLength, POWER_CONSUMPTION, true, false))
            {
                ret = SUCCESS;
                //now get all the power consumption descriptors into the struct
                identifiers->numberOfPCIdentifiers = C_CAST(uint8_t, (powerConsumptionLength - 4) / 4);
                uint32_t pcIter = 4;
                uint32_t counter = 0;
                //ctc changed the "<" conditions to "<=" so all the identifiers get parsed (was an "off-by-1" problem")
                for (; pcIter <= powerConsumptionLength && pcIter <= C_CAST(uint32_t, identifiers->numberOfPCIdentifiers * 4); pcIter += 4, counter++)
                {
                    identifiers->identifiers[counter].identifierValue = powerConsumptionPage[pcIter];
                    identifiers->identifiers[counter].units = powerConsumptionPage[pcIter + 1] & 0x07;
                    identifiers->identifiers[counter].value = M_BytesTo2ByteValue(powerConsumptionPage[pcIter + 2], powerConsumptionPage[pcIter + 3]);
                }
            }
            else
            {
                ret = FAILURE;
            }
            safe_free_aligned(&powerConsumptionPage);
        }
        if (ret != FAILURE)
        {
            uint8_t *pcModePage = C_CAST(uint8_t*, safe_calloc_aligned(MODE_PARAMETER_HEADER_10_LEN + 16, sizeof(uint8_t), device->os_info.minimumAlignment));
            if (!pcModePage)
            {
                return MEMORY_FAILURE;
            }
            //read changable value to see if active field can be modified
            if (SUCCESS == scsi_Mode_Sense_10(device, MP_POWER_CONSUMPTION, MODE_PARAMETER_HEADER_10_LEN + 16, 0x01, true, false, MPC_CHANGABLE_VALUES, pcModePage))
            {
                if (M_GETBITRANGE(pcModePage[MODE_PARAMETER_HEADER_10_LEN + 6], 2, 0) > 0)
                {
                    identifiers->activeLevelChangable = true;
                }
                else
                {
                    identifiers->activeLevelChangable = false;
                }
            }
            //read the mode page to get the current identifier.
            if (SUCCESS == scsi_Mode_Sense_10(device, MP_POWER_CONSUMPTION, MODE_PARAMETER_HEADER_10_LEN + 16, 0x01, true, false, MPC_CURRENT_VALUES, pcModePage))
            {
                ret = SUCCESS;
                //check the active level to make sure it is zero
                identifiers->activeLevel = pcModePage[MODE_PARAMETER_HEADER_10_LEN + 6] & 0x07;
                if (identifiers->activeLevel == 0)
                {
                    identifiers->currentIdentifierValid = true;
                    //ctc 10 lines of code after the comments are necessary because the pcIdentifier and the identfiers->identifiers[] are NOT necessarily in order
                    //ctc need to step through the indentifiers to find the correct one
                    uint8_t pcIdentifier = pcModePage[MODE_PARAMETER_HEADER_10_LEN + 7];
                    uint8_t counter = 0;
                    for (; counter < C_CAST(uint32_t, identifiers->numberOfPCIdentifiers); counter++)
                    {
                        if (identifiers->identifiers[counter].identifierValue == pcIdentifier)
                        {
                            identifiers->currentIdentifier = counter;
                        }
                    }
                }
            }
            else
            {
                ret = NOT_SUPPORTED;
            }
            safe_free_aligned(&pcModePage);
        }
    }
    return ret;
}

void print_Power_Consumption_Identifiers(ptrPowerConsumptionIdentifiers identifiers)
{
    if (identifiers)
    {
        if (identifiers->numberOfPCIdentifiers > 0)
        {
            //show the current value
            if (identifiers->currentIdentifierValid)
            {
                double currentConsumption = identifiers->identifiers[identifiers->currentIdentifier].value;
                uint8_t currentUnit = identifiers->identifiers[identifiers->currentIdentifier].units;
#define POWER_CONSUMPTION_UNIT_BUFFER_LENGTH 25
                DECLARE_ZERO_INIT_ARRAY(char, unitBuff, POWER_CONSUMPTION_UNIT_BUFFER_LENGTH);
                char* currentUnits = &unitBuff[0];
                //convert this to a smaller value that can be reprsented with minimal floating point (13500mw->13.5w)
                while ((currentConsumption / 1000.0) > 1 && currentUnit > 0)
                {
                    currentConsumption /= 1000.0;
                    --currentUnit;//change the unit
                }

                //now print the units
                switch (currentUnit)
                {
                case 0://gigawatts
                    snprintf(currentUnits, POWER_CONSUMPTION_UNIT_BUFFER_LENGTH, "Gigawatts");
                    break;
                case 1://megawatts
                    snprintf(currentUnits, POWER_CONSUMPTION_UNIT_BUFFER_LENGTH, "Megawatts");
                    break;
                case 2://kilowatts
                    snprintf(currentUnits, POWER_CONSUMPTION_UNIT_BUFFER_LENGTH, "Kilowatts");
                    break;
                case 3://watts
                    snprintf(currentUnits, POWER_CONSUMPTION_UNIT_BUFFER_LENGTH, "Watts");
                    break;
                case 4://milliwatts
                    snprintf(currentUnits, POWER_CONSUMPTION_UNIT_BUFFER_LENGTH, "Milliwatts");
                    break;
                case 5://microwatts
                    snprintf(currentUnits, POWER_CONSUMPTION_UNIT_BUFFER_LENGTH, "Microwatts");
                    break;
                default:
                    snprintf(currentUnits, POWER_CONSUMPTION_UNIT_BUFFER_LENGTH, "unknown unit of measure");
                    break;
                }
                printf("Current Power Consumption Value: %g %s\n", currentConsumption, currentUnits);
            }
            else
            {
                //high medium low value
                printf("Drive is currently configured with ");
                switch (identifiers->activeLevel)
                {
                case 1:
                    printf("highest relative active power consumption\n");
                    break;
                case 2:
                    printf("intermediate relative active power consumption\n");
                    break;
                case 3:
                    printf("lowest relative active power consumption\n");
                    break;
                default:
                    printf("unknown active level!\n");
                    break;
                }
            }
            //show a list of the values supported (in watts). If the value is less than 1 watt, exclude it
            printf("Supported Max Power Consumption Set Points (Watts): \n\t[");
            uint8_t pcIter = 0;
            for (; pcIter < identifiers->numberOfPCIdentifiers; pcIter++)
            {
                double watts = identifiers->identifiers[pcIter].value;
                switch (identifiers->identifiers[pcIter].units)
                {
                case 0://gigawatts
                    watts *= 1000000000;
                    break;
                case 1://megawatts
                    watts *= 1000000;
                    break;
                case 2://kilowatts
                    watts *= 1000;
                    break;
                case 3://watts
                    break;
                case 4://milliwatts
//ctc properly round milliwatts values
                    watts /= 1000;
                    break;
                case 5://microwatts
//ctc properly round milliwatts values
                    watts /= 1000000;
                    break;
                default:
                    continue;//continue the for loop
                }
                printf(" %g |", watts);//use %g to use shortest possible notation for the output. This keeps 12w 13.5w without extra zeros all over the place
            }
            if (identifiers->activeLevelChangable)
            {
                //now print default, highest, lowest, and intermediate
                printf(" highest | intermediate | lowest |");
            }
            printf(" default ]\n");//always allow default so that we can restore back to original settings
        }
        else
        {
            //high medium low value
            printf("Drive is currently configured with ");
            switch (identifiers->activeLevel)
            {
            case 0:
                printf("Power consumption identifier set to %" PRIu8 "\n", identifiers->currentIdentifier);
                break;
            case 1:
                printf("highest relative active power consumption\n");
                break;
            case 2:
                printf("intermediate relative active power consumption\n");
                break;
            case 3:
                printf("lowest relative active power consumption\n");
                break;
            default:
                printf("unknown active level!\n");
                break;
            }
            printf("Supported Max Power Consumption Set Points : \n\t[ ");
            if (identifiers->activeLevelChangable)
            {
                //now print default, highest, lowest, and intermediate
                printf(" highest | intermediate | lowest |");
            }
            printf(" default ]\n");//always allow default so that we can restore back to original settings
        }
    }
}

eReturnValues set_Power_Consumption(tDevice *device, ePCActiveLevel activeLevelField, uint8_t powerConsumptionIdentifier, bool resetToDefault)
{
    eReturnValues ret = NOT_SUPPORTED;
    if (device->drive_info.drive_type == SCSI_DRIVE)
    {
        uint8_t *pcModePage = C_CAST(uint8_t*, safe_calloc_aligned(16 + MODE_PARAMETER_HEADER_10_LEN, sizeof(uint8_t), device->os_info.minimumAlignment));
        eScsiModePageControl mpControl = MPC_CURRENT_VALUES;
        if (!pcModePage)
        {
            return MEMORY_FAILURE;
        }
        if (resetToDefault)
        {
            mpControl = MPC_DEFAULT_VALUES;
        }
        if (SUCCESS == scsi_Mode_Sense_10(device, MP_POWER_CONSUMPTION, 16 + MODE_PARAMETER_HEADER_10_LEN, 0x01, true, false, mpControl, pcModePage))
        {
            if (!resetToDefault)
            {
                //modify the value we want to set
                switch (activeLevelField)
                {
                case PC_ACTIVE_LEVEL_IDENTIFIER:
                    //set active level to 0
                    pcModePage[MODE_PARAMETER_HEADER_10_LEN + 6] &= 0xFC;//clear lower 2 bits to 0
                    //set the power consumption identifier we were given
                    pcModePage[MODE_PARAMETER_HEADER_10_LEN + 7] = powerConsumptionIdentifier;
                    break;
                case PC_ACTIVE_LEVEL_HIGHEST:
                case PC_ACTIVE_LEVEL_INTERMEDIATE:
                case PC_ACTIVE_LEVEL_LOWEST:
                    //set the active level to what was requested (power consumption identifier is ignored here)
                    pcModePage[MODE_PARAMETER_HEADER_10_LEN + 6] &= 0xFC;//clear lower 2 bits to 0
                    //now set it now that the bits are cleared out
                    pcModePage[MODE_PARAMETER_HEADER_10_LEN + 6] |= C_CAST(uint8_t, activeLevelField);
                    break;
                default:
                    ret = FAILURE;
                    break;
                }
            }
            if (ret != FAILURE)
            {
                //now do mode select with the data for the mode to set
                ret = scsi_Mode_Select_10(device, 16 + MODE_PARAMETER_HEADER_10_LEN, true, true, false, pcModePage, 16 + MODE_PARAMETER_HEADER_10_LEN);
            }
        }
        safe_free_aligned(&pcModePage);
    }
    return ret;
}

eReturnValues map_Watt_Value_To_Power_Consumption_Identifier(tDevice *device, double watts, uint8_t *pcIdentifier)
{
    eReturnValues ret = NOT_SUPPORTED;
    powerConsumptionIdentifiers identifiers;
    memset(&identifiers, 0, sizeof(powerConsumptionIdentifiers));
    *pcIdentifier = 0xFF;//invalid
//ctc one line code change follows
    uint64_t roundedWatts = C_CAST(uint64_t, watts + 0.5);
    //*/
    ret = get_Power_Consumption_Identifiers(device, &identifiers);
    /*/
    //This is a dummied up test to make sure this code REALLY REALLY works by putting these in a random order (since order is not specified in the SPC specification)
    ret = SUCCESS;
    identifiers.currentIdentifier = 0;
    identifiers.numberOfPCIdentifiers = 4;
    identifiers.identifiers[0].identifierValue = 3;
    identifiers.identifiers[0].units = 3;//watts
    identifiers.identifiers[0].value = 4;
    identifiers.identifiers[1].identifierValue = 0;
    identifiers.identifiers[1].units = 3;//watts
    identifiers.identifiers[1].value = 8;
    identifiers.identifiers[2].identifierValue = 1;
    identifiers.identifiers[2].units = 3;//watts
    identifiers.identifiers[2].value = 6;
    identifiers.identifiers[3].identifierValue = 4;
    identifiers.identifiers[3].units = 3;//watts
    identifiers.identifiers[3].value = 2;
    //*/
    if (ret == SUCCESS)
    {
        bool exactMatchFound = false;
        //now map the watt value to a power consumption identifier
//ctc had to change variable initialization of iter1 and inter2 to match now-nested for loops
        uint8_t iter1 = 0;
        uint8_t iter2 = 0;
        uint8_t pcId1 = 0xFF;
        uint8_t pcId2 = 0xFF;
        uint64_t watts1 = 0;
        uint64_t watts2 = 0;

        ret = NOT_SUPPORTED;
        //ctc changed to nested for loops here... not sure it's needed, but it's clearer
        //        for (; iter1 < identifiers.numberOfPCIdentifiers /* && iter2 >= 0*/; iter1++, iter2--)
        for (; iter1 < identifiers.numberOfPCIdentifiers; iter1++)
        {
            //ctc needed to reset iter2=0 to go through the for loop the next times... not sure why the code doesn't follow convention
            //ctc and use for(initializer, condition, increment), but whatever.  Nonstandard and goofy coding sytle, I guess
            iter2 = 0;
            for (; iter2 < identifiers.numberOfPCIdentifiers; iter2++)
            {
                uint64_t pcWatts1 = identifiers.identifiers[iter1].value;
                uint64_t pcWatts2 = identifiers.identifiers[iter2].value;
                //convert based on the units!
                switch (identifiers.identifiers[iter1].units)
                {
                case 0://gigawatts
                    pcWatts1 *= 1000000000;
                    break;
                case 1://megawatts
                    pcWatts1 *= 1000000;
                    break;
                case 2://kilowatts
                    pcWatts1 *= 1000;
                    break;
                case 3://watts
                    break;
                case 4://milliwatts
//ctc properly round milliwatts values
                    pcWatts1 = (pcWatts1 + 500) / 1000;
                    break;
                case 5://microwatts
//ctc properly round microwatts values
                    pcWatts1 = (pcWatts1 + 500000) / 1000000;
                    break;
                default:
                    ret = NOT_SUPPORTED;
                    break;
                }
                //ctc change code line below to switch on [iter2] instead of [iter1]
                switch (identifiers.identifiers[iter2].units)
                {
                case 0://gigawatts
                    pcWatts2 *= 1000000000;
                    break;
                case 1://megawatts
                    pcWatts2 *= 1000000;
                    break;
                case 2://kilowatts
                    pcWatts2 *= 1000;
                    break;
                case 3://watts
                    break;
                case 4://milliwatts
//ctc properly round milliwatts values
                    pcWatts2 = (pcWatts2 + 500) / 1000;
                    break;
                case 5://microwatts
//ctc properly round microwatts values
                    pcWatts2 = (pcWatts2 + 500000) / 1000000;
                    break;
                default:
                    ret = NOT_SUPPORTED;
                    break;
                }
                if (pcWatts1 <= roundedWatts)
                {
                    if (watts - C_CAST(double, watts1) > watts - C_CAST(double, pcWatts1))
                    {
                        pcId1 = identifiers.identifiers[iter1].identifierValue;
                        watts1 = pcWatts1;
                        if (pcWatts1 == roundedWatts)
                        {
                            ret = SUCCESS;
                            exactMatchFound = true;
                            *pcIdentifier = identifiers.identifiers[iter1].identifierValue;
                            break;
                        }
                    }
                }
                if (pcWatts2 <= roundedWatts)
                {
                    if (watts - C_CAST(double, watts2) > watts - C_CAST(double, pcWatts2))
                    {
                        pcId2 = identifiers.identifiers[iter2].identifierValue;
                        watts2 = pcWatts2;
                        if (pcWatts2 == roundedWatts)
                        {
                            ret = SUCCESS;
                            exactMatchFound = true;
                            *pcIdentifier = identifiers.identifiers[iter2].identifierValue;
                            break;
                        }
                    }
                }
            }
        }
        if (!exactMatchFound)
        {
            //now compare the best results between the two iterators to see which is closer to the best match, or is the best match
            //need to check which one is closer and select it

            if (watts - C_CAST(double, watts1) >= watts - C_CAST(double, watts2))
            {
                ret = SUCCESS;
                *pcIdentifier = pcId2;
            }
            else if (watts - C_CAST(double, watts1) <= watts - C_CAST(double, watts2))
            {
                ret = SUCCESS;
                *pcIdentifier = pcId1;
            }
        }
    }
    return ret;
}

eReturnValues enable_Disable_APM_Feature(tDevice *device, bool enable)
{
    eReturnValues ret = NOT_SUPPORTED;
    if (device->drive_info.drive_type == ATA_DRIVE)
    {
        //check the identify bits to make sure APM is supported.
        if (is_ATA_Identify_Word_Valid(device->drive_info.IdentifyData.ata.Word083) && device->drive_info.IdentifyData.ata.Word083 & BIT3)
        {
            if (enable)
            {
                //subcommand 05..set value to 0x7F when requesting an enable operation so that it's a good mix of performance and power savings.
                ret = ata_Set_Features(device, SF_ENABLE_APM_FEATURE, 0x7F, 0, 0, 0);
            }
            else
            {
                //subcommand 85
                ret = ata_Set_Features(device, SF_DISABLE_APM_FEATURE, 0, 0, 0, 0);
                if (ret != SUCCESS)
                {
                    //the disable APM feature is not available on all devices according to ATA spec.
                    ret = NOT_SUPPORTED;
                }
            }
        }
    }
    return ret;
}
//APM Levels:
// 0 - reserved
// 1 = minimum power consumption with standby mode
// 2-7Fh = Intermedia power management levels with standby mode
// 80h = minimum power consumption without standby mode
// 81h - FDh = intermediate power management levels without standby mode
// FEh = maximum performance.
eReturnValues set_APM_Level(tDevice *device, uint8_t apmLevel)
{
    eReturnValues ret = NOT_SUPPORTED;
    if (device->drive_info.drive_type == ATA_DRIVE)
    {
        //check the identify bits to make sure APM is supported.
        if (is_ATA_Identify_Word_Valid(device->drive_info.IdentifyData.ata.Word083) && device->drive_info.IdentifyData.ata.Word083 & BIT3)
        {
            //subcommand 05 with the apmLevel in the count field
            ret = ata_Set_Features(device, SF_ENABLE_APM_FEATURE, apmLevel, 0, 0, 0);
        }
    }
    return ret;
}

eReturnValues get_APM_Level(tDevice *device, uint8_t *apmLevel)
{
    eReturnValues ret = NOT_SUPPORTED;
    if (device->drive_info.drive_type == ATA_DRIVE)
    {
        //check the identify bits to make sure APM is supported.
        if (is_ATA_Identify_Word_Valid(device->drive_info.IdentifyData.ata.Word083) && device->drive_info.IdentifyData.ata.Word083 & BIT3)
        {
            //get it from identify device word 91
            ret = SUCCESS;
            if (is_ATA_Identify_Word_Valid(device->drive_info.IdentifyData.ata.Word091))
            {
                *apmLevel = M_Byte0(device->drive_info.IdentifyData.ata.Word091);
            }
            else
            {
                *apmLevel = UINT8_MAX;//invalid value
            }
        }
    }
    return ret;
}

static eReturnValues ata_Get_EPC_Settings(tDevice *device, ptrEpcSettings epcSettings)
{
    eReturnValues ret = NOT_SUPPORTED;
    if (!epcSettings)
    {
        return BAD_PARAMETER;
    }
    uint32_t epcLogSize = LEGACY_DRIVE_SEC_SIZE * 2;//from ATA Spec
    //get_ATA_Log_Size(device, ATA_LOG_POWER_CONDITIONS, &epcLogSize, true, false) //uncomment this line to ask the drive for the EPC log size rather than use the hard coded value above.
    uint8_t *epcLog = C_CAST(uint8_t*, safe_calloc_aligned(epcLogSize * sizeof(uint8_t), sizeof(uint8_t), device->os_info.minimumAlignment));
    if (!epcLog)
    {
        return MEMORY_FAILURE;
    }
    if (SUCCESS == get_ATA_Log(device, ATA_LOG_POWER_CONDITIONS, M_NULLPTR, M_NULLPTR, true, false, true, epcLog, epcLogSize, M_NULLPTR, epcLogSize, 0))
    {
        ret = SUCCESS;
        for (uint32_t offset = 0; offset < (LEGACY_DRIVE_SEC_SIZE * 2); offset += 64)
        {
            ptrPowerConditionInfo currentPowerCondition = M_NULLPTR;
            switch (offset)
            {
            case 0://idle a
                currentPowerCondition = &epcSettings->idle_a;
                break;
            case 64://idle b
                currentPowerCondition = &epcSettings->idle_b;
                break;
            case 128://idle c
                currentPowerCondition = &epcSettings->idle_c;
                break;
            case LEGACY_DRIVE_SEC_SIZE + 384://standby y
                currentPowerCondition = &epcSettings->standby_y;
                break;
            case LEGACY_DRIVE_SEC_SIZE + 448://standby z
                currentPowerCondition = &epcSettings->standby_z;
                break;
            default:
                continue;
            }
            if (!currentPowerCondition)
            {
                continue;
            }
            if (epcLog[offset + 1] & BIT7)
            {
                //set the bits
                currentPowerCondition->powerConditionSupported = true;
                if (epcLog[offset + 1] & BIT6)
                {
                    currentPowerCondition->powerConditionSaveable = true;
                }
                if (epcLog[offset + 1] & BIT5)
                {
                    currentPowerCondition->powerConditionChangeable = true;
                }
                if (epcLog[offset + 1] & BIT4)
                {
                    currentPowerCondition->defaultTimerEnabled = true;
                }
                if (epcLog[offset + 1] & BIT3)
                {
                    currentPowerCondition->savedTimerEnabled = true;
                }
                if (epcLog[offset + 1] & BIT2)
                {
                    currentPowerCondition->currentTimerEnabled = true;
                }
                if (epcLog[offset + 1] & BIT1)
                {
                    currentPowerCondition->holdPowerConditionNotSupported = true;
                }
                //set the values
                currentPowerCondition->defaultTimerSetting = M_BytesTo4ByteValue(epcLog[offset + 7], epcLog[offset + 6], epcLog[offset + 5], epcLog[offset + 4]);
                currentPowerCondition->savedTimerSetting = M_BytesTo4ByteValue(epcLog[offset + 11], epcLog[offset + 10], epcLog[offset + 9], epcLog[offset + 8]);
                currentPowerCondition->currentTimerSetting = M_BytesTo4ByteValue(epcLog[offset + 15], epcLog[offset + 14], epcLog[offset + 13], epcLog[offset + 12]);
                currentPowerCondition->nominalRecoveryTimeToActiveState = M_BytesTo4ByteValue(epcLog[offset + 19], epcLog[offset + 18], epcLog[offset + 17], epcLog[offset + 16]);
                currentPowerCondition->minimumTimerSetting = M_BytesTo4ByteValue(epcLog[offset + 23], epcLog[offset + 22], epcLog[offset + 21], epcLog[offset + 20]);
                currentPowerCondition->maximumTimerSetting = M_BytesTo4ByteValue(epcLog[offset + 27], epcLog[offset + 26], epcLog[offset + 25], epcLog[offset + 24]);
            }
        }
    }
    safe_free_aligned(&epcLog);
    return ret;
}

static eReturnValues scsi_Get_EPC_Settings(tDevice *device, ptrEpcSettings epcSettings)
{
    eReturnValues ret = NOT_SUPPORTED;
    if (!epcSettings)
    {
        return BAD_PARAMETER;
    }
    bool powerConditionVPDsupported = true;
    DECLARE_ZERO_INIT_ARRAY(uint8_t, epcVPDPage, VPD_POWER_CONDITION_LEN);
    if (SUCCESS == get_SCSI_VPD(device, POWER_CONDITION, M_NULLPTR, M_NULLPTR, true, epcVPDPage, VPD_POWER_CONDITION_LEN, M_NULLPTR))
    {
        //NOTE: Recovery times are in milliseconds, not 100 milliseconds like other timers, so need to convert to 100 millisecond units for now-TJE
        //idle a
        if (epcVPDPage[5] & BIT0)
        {
            epcSettings->idle_a.powerConditionSupported = true;
            epcSettings->idle_a.nominalRecoveryTimeToActiveState = M_BytesTo2ByteValue(epcVPDPage[12], epcVPDPage[13]);
            if (epcSettings->idle_a.nominalRecoveryTimeToActiveState > 0)
            {
                epcSettings->idle_a.nominalRecoveryTimeToActiveState = M_Max(1, epcSettings->idle_a.nominalRecoveryTimeToActiveState / 100);
            }
        }
        //idle b
        if (epcVPDPage[5] & BIT1)
        {
            epcSettings->idle_b.powerConditionSupported = true;
            epcSettings->idle_b.nominalRecoveryTimeToActiveState = M_BytesTo2ByteValue(epcVPDPage[14], epcVPDPage[15]);
            if (epcSettings->idle_b.nominalRecoveryTimeToActiveState > 0)
            {
                epcSettings->idle_b.nominalRecoveryTimeToActiveState = M_Max(1, epcSettings->idle_b.nominalRecoveryTimeToActiveState / 100);
            }
        }
        //idle c
        if (epcVPDPage[5] & BIT2)
        {
            epcSettings->idle_c.powerConditionSupported = true;
            epcSettings->idle_c.nominalRecoveryTimeToActiveState = M_BytesTo2ByteValue(epcVPDPage[16], epcVPDPage[17]);
            if (epcSettings->idle_c.nominalRecoveryTimeToActiveState > 0)
            {
                epcSettings->idle_c.nominalRecoveryTimeToActiveState = M_Max(1, epcSettings->idle_c.nominalRecoveryTimeToActiveState / 100);
            }
        }
        //standby z
        if (epcVPDPage[4] & BIT0)
        {
            epcSettings->standby_z.powerConditionSupported = true;
            epcSettings->standby_z.nominalRecoveryTimeToActiveState = M_BytesTo2ByteValue(epcVPDPage[8], epcVPDPage[9]);
            if (epcSettings->standby_z.nominalRecoveryTimeToActiveState > 0)
            {
                epcSettings->standby_z.nominalRecoveryTimeToActiveState = M_Max(1, epcSettings->standby_z.nominalRecoveryTimeToActiveState / 100);
            }
        }
        //standby y
        if (epcVPDPage[4] & BIT1)
        {
            epcSettings->standby_y.powerConditionSupported = true;
            epcSettings->standby_y.nominalRecoveryTimeToActiveState = M_BytesTo2ByteValue(epcVPDPage[10], epcVPDPage[11]);
            if (epcSettings->standby_y.nominalRecoveryTimeToActiveState > 0)
            {
                epcSettings->standby_y.nominalRecoveryTimeToActiveState = M_Max(1, epcSettings->standby_y.nominalRecoveryTimeToActiveState / 100);
            }
        }
    }
    else 
    {
        powerConditionVPDsupported = false;
    }
    epcSettings->settingsAffectMultipleLogicalUnits = scsi_Mode_Pages_Shared_By_Multiple_Logical_Units(device, MP_POWER_CONDTION, 0);
    //now time to read the mode pages for the other information (start with current, then saved, then default)
    DECLARE_ZERO_INIT_ARRAY(uint8_t, epcModePage, MP_POWER_CONDITION_LEN + MODE_PARAMETER_HEADER_10_LEN);
    for (eScsiModePageControl modePageControl = MPC_CURRENT_VALUES; modePageControl <= MPC_SAVED_VALUES; ++modePageControl)
    {
        safe_memset(epcModePage, MP_POWER_CONDITION_LEN + MODE_PARAMETER_HEADER_10_LEN, 0, MP_POWER_CONDITION_LEN + MODE_PARAMETER_HEADER_10_LEN);
        bool gotData = false;
        uint8_t headerLength = MODE_PARAMETER_HEADER_10_LEN;
        if (SUCCESS == scsi_Mode_Sense_10(device, MP_POWER_CONDTION, MP_POWER_CONDITION_LEN + MODE_PARAMETER_HEADER_10_LEN, 0, true, false, modePageControl, epcModePage))
        {
            gotData = true;
        }
        else if (SUCCESS == scsi_Mode_Sense_6(device, MP_POWER_CONDTION, MP_POWER_CONDITION_LEN + MODE_PARAMETER_HEADER_6_LEN, 0, true, modePageControl, epcModePage))
        {
            gotData = true;
            headerLength = MODE_PARAMETER_HEADER_6_LEN;
        }
        if (gotData)
        {
            bool *idleAenabledBit = M_NULLPTR;
            uint32_t *idleAtimerSetting = M_NULLPTR;
            bool *idleBenabledBit = M_NULLPTR;
            uint32_t *idleBtimerSetting = M_NULLPTR;
            bool *idleCenabledBit = M_NULLPTR;
            uint32_t *idleCtimerSetting = M_NULLPTR;
            bool *standbyYenabledBit = M_NULLPTR;
            uint32_t *standbyYtimerSetting = M_NULLPTR;
            bool *standbyZenabledBit = M_NULLPTR;
            uint32_t *standbyZtimerSetting = M_NULLPTR;
            switch (modePageControl)
            {
            case MPC_CURRENT_VALUES:
                idleAenabledBit = &epcSettings->idle_a.currentTimerEnabled;
                idleAtimerSetting = &epcSettings->idle_a.currentTimerSetting;
                idleBenabledBit = &epcSettings->idle_b.currentTimerEnabled;
                idleBtimerSetting = &epcSettings->idle_b.currentTimerSetting;
                idleCenabledBit = &epcSettings->idle_c.currentTimerEnabled;
                idleCtimerSetting = &epcSettings->idle_c.currentTimerSetting;
                standbyYenabledBit = &epcSettings->standby_y.currentTimerEnabled;
                standbyYtimerSetting = &epcSettings->standby_y.currentTimerSetting;
                standbyZenabledBit = &epcSettings->standby_z.currentTimerEnabled;
                standbyZtimerSetting = &epcSettings->standby_z.currentTimerSetting;
                //special case. If reading the current values, check the page for the PS bit (parameters savable) to note this for all power conditions.
                if (epcModePage[headerLength + 0] & BIT7)
                {
                    epcSettings->idle_a.powerConditionSaveable = true;
                    epcSettings->idle_b.powerConditionSaveable = true;
                    epcSettings->idle_c.powerConditionSaveable = true;
                    epcSettings->standby_y.powerConditionSaveable = true;
                    epcSettings->standby_z.powerConditionSaveable = true;
                }
                break;
            case MPC_CHANGABLE_VALUES:
                idleAenabledBit = &epcSettings->idle_a.powerConditionChangeable;
                idleAtimerSetting = &epcSettings->idle_a.maximumTimerSetting;
                idleBenabledBit = &epcSettings->idle_b.powerConditionChangeable;
                idleBtimerSetting = &epcSettings->idle_b.maximumTimerSetting;
                idleCenabledBit = &epcSettings->idle_c.powerConditionChangeable;
                idleCtimerSetting = &epcSettings->idle_c.maximumTimerSetting;
                standbyYenabledBit = &epcSettings->standby_y.powerConditionChangeable;
                standbyYtimerSetting = &epcSettings->standby_y.maximumTimerSetting;
                standbyZenabledBit = &epcSettings->standby_z.powerConditionChangeable;
                standbyZtimerSetting = &epcSettings->standby_z.maximumTimerSetting;
                break;
            case MPC_DEFAULT_VALUES:
                idleAenabledBit = &epcSettings->idle_a.defaultTimerEnabled;
                idleAtimerSetting = &epcSettings->idle_a.defaultTimerSetting;
                idleBenabledBit = &epcSettings->idle_b.defaultTimerEnabled;
                idleBtimerSetting = &epcSettings->idle_b.defaultTimerSetting;
                idleCenabledBit = &epcSettings->idle_c.defaultTimerEnabled;
                idleCtimerSetting = &epcSettings->idle_c.defaultTimerSetting;
                standbyYenabledBit = &epcSettings->standby_y.defaultTimerEnabled;
                standbyYtimerSetting = &epcSettings->standby_y.defaultTimerSetting;
                standbyZenabledBit = &epcSettings->standby_z.defaultTimerEnabled;
                standbyZtimerSetting = &epcSettings->standby_z.defaultTimerSetting;
                break;
            case MPC_SAVED_VALUES:
                idleAenabledBit = &epcSettings->idle_a.savedTimerEnabled;
                idleAtimerSetting = &epcSettings->idle_a.savedTimerSetting;
                idleBenabledBit = &epcSettings->idle_b.savedTimerEnabled;
                idleBtimerSetting = &epcSettings->idle_b.savedTimerSetting;
                idleCenabledBit = &epcSettings->idle_c.savedTimerEnabled;
                idleCtimerSetting = &epcSettings->idle_c.savedTimerSetting;
                standbyYenabledBit = &epcSettings->standby_y.savedTimerEnabled;
                standbyYtimerSetting = &epcSettings->standby_y.savedTimerSetting;
                standbyZenabledBit = &epcSettings->standby_z.savedTimerEnabled;
                standbyZtimerSetting = &epcSettings->standby_z.savedTimerSetting;
                break;
            default:
                continue;
            }
            //idle a
            if (epcModePage[headerLength + 3] & BIT1)
            {
                *idleAenabledBit = true;
                if (powerConditionVPDsupported == false)
                {
                    if (modePageControl == MPC_CHANGABLE_VALUES || modePageControl == MPC_CURRENT_VALUES)
                    {
                        //special case, mostly for older drives before EPC.
                        //SCSI has supported this mode page for a while, so if it's the "changeable" page set "supported" bits for different power conditions.
                        epcSettings->idle_a.powerConditionSupported = true;
                    }
                }
            }
            *idleAtimerSetting = M_BytesTo4ByteValue(epcModePage[headerLength + 4], epcModePage[headerLength + 5], epcModePage[headerLength + 6], epcModePage[headerLength + 7]);
            //standby z
            if (epcModePage[headerLength + 3] & BIT0)
            {
                *standbyZenabledBit = true;
                if (modePageControl == MPC_CHANGABLE_VALUES || modePageControl == MPC_CURRENT_VALUES)
                    {
                        //special case, mostly for older drives before EPC.
                        //SCSI has supported this mode page for a while, so if it's the "changeable" page set "supported" bits for different power conditions.
                        epcSettings->standby_z.powerConditionSupported = true;
                    }
            }
            *standbyZtimerSetting = M_BytesTo4ByteValue(epcModePage[headerLength + 8], epcModePage[headerLength + 9], epcModePage[headerLength + 10], epcModePage[headerLength + 11]);
            if (epcModePage[headerLength + 1] > 0x0A)//before EPC this page was shorter, so do not try to access the rest of it as the data is invalid
            {
                //idle b
                if (epcModePage[headerLength + 3] & BIT2)
                {
                    *idleBenabledBit = true;
                }
                *idleBtimerSetting = M_BytesTo4ByteValue(epcModePage[headerLength + 12], epcModePage[headerLength + 13], epcModePage[headerLength + 14], epcModePage[headerLength + 15]);
                //idle c
                if (epcModePage[headerLength + 3] & BIT3)
                {
                    *idleCenabledBit = true;
                }
                *idleCtimerSetting = M_BytesTo4ByteValue(epcModePage[headerLength + 16], epcModePage[headerLength + 17], epcModePage[headerLength + 18], epcModePage[headerLength + 19]);
                //standby y
                if (epcModePage[headerLength + 2] & BIT0)
                {
                    *standbyYenabledBit = true;
                }
                *standbyYtimerSetting = M_BytesTo4ByteValue(epcModePage[headerLength + 20], epcModePage[headerLength + 21], epcModePage[headerLength + 22], epcModePage[headerLength + 23]);
            }
            ret = SUCCESS;
        }
    }
    return ret;
}

eReturnValues get_EPC_Settings(tDevice *device, ptrEpcSettings epcSettings)
{
    if (device->drive_info.drive_type == ATA_DRIVE)
    {
        return ata_Get_EPC_Settings(device, epcSettings);
    }
    else if (device->drive_info.drive_type == SCSI_DRIVE)
    {
        return scsi_Get_EPC_Settings(device, epcSettings);
    }
    else
    {
        return NOT_SUPPORTED;
    }
}

static void print_Power_Condition(ptrPowerConditionInfo condition, const char *conditionName)
{
    printf("%-10s ", conditionName);
    if (condition->currentTimerEnabled)
    {
        printf("*");
    }
    else
    {
        printf(" ");
    }
    printf("%-12" PRIu32 " ", condition->currentTimerSetting);
    if (condition->defaultTimerEnabled)
    {
        printf("*");
    }
    else
    {
        printf(" ");
    }
    printf("%-12" PRIu32 " ", condition->defaultTimerSetting);
    if (condition->savedTimerEnabled)
    {
        printf("*");
    }
    else
    {
        printf(" ");
    }
    printf("%-12" PRIu32 " ", condition->savedTimerSetting);
    printf("%-12" PRIu32 " ", condition->nominalRecoveryTimeToActiveState);
    if (condition->powerConditionChangeable)
    {
        printf(" Y");
    }
    else
    {
        printf(" N");
    }
    if (condition->powerConditionSaveable)
    {
        printf(" Y");
    }
    else
    {
        printf(" N");
    }
    printf("\n");
}

void print_EPC_Settings(tDevice *device, ptrEpcSettings epcSettings)
{
    if (!epcSettings)
    {
        return;
    }
    M_USE_UNUSED(device);
    printf("\n===EPC Settings===\n");
    printf("\t* = timer is enabled\n");
    printf("\tC column = Changeable\n");
    printf("\tS column = Savable\n");
    printf("\tAll times are in 100 milliseconds\n\n");
    printf("%-10s %-13s %-13s %-13s %-12s C S\n", "Name", "Current Timer", "Default Timer", "Saved Timer", "Recovery Time");
    if (epcSettings->idle_a.powerConditionSupported)
    {
        print_Power_Condition(&epcSettings->idle_a, "Idle A");
    }
    if (epcSettings->idle_b.powerConditionSupported)
    {
        print_Power_Condition(&epcSettings->idle_b, "Idle B");
    }
    if (epcSettings->idle_c.powerConditionSupported)
    {
        print_Power_Condition(&epcSettings->idle_c, "Idle C");
    }
    if (epcSettings->standby_y.powerConditionSupported)
    {
        print_Power_Condition(&epcSettings->standby_y, "Standby Y");
    }
    if (epcSettings->standby_z.powerConditionSupported)
    {
        print_Power_Condition(&epcSettings->standby_z, "Standby Z");
    }
    /*if (epcSettings->settingsAffectMultipleLogicalUnits)
    {
        printf("\nNote: All settings affect multiple logical units.\n");
    }*/
}

//NOTE: This is intended for legacy drives that don't support extra timers for EPC. An EPC drive will still process this command though!!! - TJE
//More Notes: This function is similar to the EPC function, BUT this will not check for the VPD page as it doesn't exist on old drives.
//            These functions should probaby be combined at some point

eReturnValues scsi_Set_Legacy_Power_Conditions(tDevice *device, bool restoreAllToDefaults, ptrPowerConditionSettings standbyTimer, ptrPowerConditionSettings idleTimer)
{
    //Check the changable page for support of idle and standby timers before beginning???
    if (!restoreAllToDefaults || !standbyTimer || !idleTimer)
    {
        return BAD_PARAMETER;
    }
    powerConditionTimers pwrConditions;
    memset(&pwrConditions, 0, sizeof(powerConditionTimers));
    if (standbyTimer)
    {
        memcpy(&pwrConditions.standby, standbyTimer, sizeof(powerConditionSettings));
    }
    if (idleTimer)
    {
        memcpy(&pwrConditions.idle, idleTimer, sizeof(powerConditionSettings));
    }
    return scsi_Set_Power_Conditions(device, restoreAllToDefaults, &pwrConditions);
}

//using 100 millisecond increments since that is what SCSI uses and the methodology in here will match SAT spec. This seemed simpler - TJE
static eReturnValues ata_Set_Standby_Timer(tDevice *device, uint32_t hundredMillisecondIncrements)
{
    eReturnValues ret = NOT_SUPPORTED;
    if (is_ATA_Identify_Word_Valid(device->drive_info.IdentifyData.ata.Word049) && device->drive_info.IdentifyData.ata.Word049 & BIT13)//this is the only bit across all ATA standards that will most likely work. Prior to ATA3, there was no other support bit for the power management feature set.
    {
        uint8_t standbyTimer = 0;
        uint8_t currentPowerMode = 0;
        if (hundredMillisecondIncrements == 0)
        {
            //send standby immediate and return immediately
            return ata_Standby_Immediate(device);
        }
        else if (hundredMillisecondIncrements >= UINT32_C(1) && hundredMillisecondIncrements <= UINT32_C(12000))
        {
            standbyTimer = C_CAST(uint8_t, ((hundredMillisecondIncrements - UINT32_C(1)) / UINT32_C(50)) + UINT32_C(1));
        }
        else if (hundredMillisecondIncrements >= UINT32_C(12001) && hundredMillisecondIncrements <= UINT32_C(12600))
        {
            standbyTimer = 0xFC;
        }
        else if (hundredMillisecondIncrements >= UINT32_C(12601) && hundredMillisecondIncrements <= UINT32_C(12750))
        {
            standbyTimer = 0xFF;
        }
        else if (hundredMillisecondIncrements >= UINT32_C(12751) && hundredMillisecondIncrements <= UINT32_C(17999))
        {
            standbyTimer = 0xF1;
        }
        else if (hundredMillisecondIncrements >= UINT32_C(18000) && hundredMillisecondIncrements <= UINT32_C(198000))
        {
            standbyTimer = C_CAST(uint8_t, (hundredMillisecondIncrements / UINT32_C(18000)) + UINT32_C(240));
        }
        else
        {
            standbyTimer = 0xFD;
        }
        //if we made it here, set the timer.
        //Check the current power mode. If the drive is in standby already, use standby, otherwise use idle to set the timer.
        //NOTE: Previously only the standby command was used like in SAT-5 for non-EPC. Changed to checking states, but only non-EPC behavior
        ata_Check_Power_Mode(device, &currentPowerMode);
        if (currentPowerMode == 0)
        {
            ret = ata_Standby(device, standbyTimer);
        }
        else
        {
            //not in standby mode, so use idle. NOTE: This does not take into account EPC, but this is meant for legacy drives anyways, so this does not need to be complicated.
            //This may affect standby_y, but users should be using EPC instead if they want better, more granular timers anyways. -TJE
            ret = ata_Idle(device, standbyTimer);
        }
    }
    return ret;
}

eReturnValues scsi_Set_Standby_Timer_State(tDevice *device, bool enable)
{
    powerConditionSettings standbyTimer;
    memset(&standbyTimer, 0, sizeof(powerConditionSettings));
    standbyTimer.powerConditionValid = true;
    standbyTimer.enableValid = true;
    standbyTimer.enable = enable;

    return scsi_Set_Legacy_Power_Conditions(device, false, &standbyTimer, M_NULLPTR);
}

eReturnValues set_Standby_Timer(tDevice *device, uint32_t hundredMillisecondIncrements, bool restoreToDefault)
{
    eReturnValues ret = NOT_SUPPORTED;
    if (device->drive_info.drive_type == ATA_DRIVE)
    {
        if (restoreToDefault)
        {
            //Cannot restore ATA to default values. This is done with a power cycle or some SATL that has a capability to remember the original timer (in which case, the scsi function should be used instead)
            return NOT_SUPPORTED;
        }
        ret = ata_Set_Standby_Timer(device, hundredMillisecondIncrements);
    }
    else if (device->drive_info.drive_type == SCSI_DRIVE)
    {
        powerConditionSettings standbyTimer;
        memset(&standbyTimer, 0, sizeof(powerConditionSettings));
        standbyTimer.powerConditionValid = true;
        if (restoreToDefault)
        {
            standbyTimer.restoreToDefault = restoreToDefault;
        }
        else
        {
            standbyTimer.enableValid = true;
            standbyTimer.enable = true;
            standbyTimer.timerValid = true;
            standbyTimer.timerInHundredMillisecondIncrements = hundredMillisecondIncrements;
        }
        ret = scsi_Set_Legacy_Power_Conditions(device, false, &standbyTimer, M_NULLPTR);
    }
    return ret;
}

eReturnValues scsi_Set_Idle_Timer_State(tDevice *device, bool enable)
{
    powerConditionSettings idleTimer;
    memset(&idleTimer, 0, sizeof(powerConditionSettings));
    idleTimer.powerConditionValid = true;
    idleTimer.enableValid = true;
    idleTimer.enable = enable;

    return scsi_Set_Legacy_Power_Conditions(device, false, M_NULLPTR, &idleTimer);
}

eReturnValues set_Idle_Timer(tDevice *device, uint32_t hundredMillisecondIncrements, bool restoreToDefault)
{
    eReturnValues ret = NOT_SUPPORTED;
    if (device->drive_info.drive_type == SCSI_DRIVE)
    {
        powerConditionSettings idleTimer;
        memset(&idleTimer, 0, sizeof(powerConditionSettings));
        idleTimer.powerConditionValid = true;
        if (restoreToDefault)
        {
            idleTimer.restoreToDefault = restoreToDefault;
        }
        else
        {
            idleTimer.enableValid = true;
            idleTimer.enable = true;
            idleTimer.timerValid = true;
            idleTimer.timerInHundredMillisecondIncrements = hundredMillisecondIncrements;
        }
        ret = scsi_Set_Legacy_Power_Conditions(device, false, M_NULLPTR, &idleTimer);
    }
    return ret;
}

eReturnValues sata_Get_Device_Initiated_Interface_Power_State_Transitions(tDevice *device, bool *supported, bool *enabled)
{
    eReturnValues ret = NOT_SUPPORTED;
    if ((device->drive_info.drive_type == ATA_DRIVE || device->drive_info.drive_type == ATAPI_DRIVE) && is_SATA(device))
    {
        ret = SUCCESS;
        if (supported)
        {
            if (is_ATA_Identify_Word_Valid_SATA(device->drive_info.IdentifyData.ata.Word078) && device->drive_info.IdentifyData.ata.Word078 & BIT3)
            {
                *supported = true;
            }
            else
            {
                *supported = false;
            }
        }
        if (enabled)
        {
            if (is_ATA_Identify_Word_Valid_SATA(device->drive_info.IdentifyData.ata.Word079) && device->drive_info.IdentifyData.ata.Word079 & BIT3)
            {
                *enabled = true;
            }
            else
            {
                *enabled = false;
            }
        }

    }
    return ret;
}

eReturnValues sata_Set_Device_Initiated_Interface_Power_State_Transitions(tDevice *device, bool enable)
{
    eReturnValues ret = NOT_SUPPORTED;
    if ((device->drive_info.drive_type == ATA_DRIVE || device->drive_info.drive_type == ATAPI_DRIVE) && is_SATA(device))
    {
        bool supported = false;
        if (SUCCESS == sata_Get_Device_Initiated_Interface_Power_State_Transitions(device, &supported, M_NULLPTR))
        {
            if (enable)
            {
                ret = ata_Set_Features(device, SF_ENABLE_SATA_FEATURE, 0x03, 0, 0, 0);
            }
            else
            {
                ret = ata_Set_Features(device, SF_DISABLE_SATA_FEATURE, 0x03, 0, 0, 0);
            }
            //Issue an identify to update the identify data...
            if (device->drive_info.drive_type == ATA_DRIVE)
            {
                ata_Identify(device, C_CAST(uint8_t*, &device->drive_info.IdentifyData.ata.Word000), LEGACY_DRIVE_SEC_SIZE);
            }
            else if (device->drive_info.drive_type == ATAPI_DRIVE)
            {
                ata_Identify_Packet_Device(device, C_CAST(uint8_t*, &device->drive_info.IdentifyData.ata.Word000), LEGACY_DRIVE_SEC_SIZE);
            }
        }
    }
    return ret;
}

eReturnValues sata_Get_Device_Automatic_Partioan_To_Slumber_Transtisions(tDevice *device, bool *supported, bool *enabled)
{
    eReturnValues ret = NOT_SUPPORTED;
    if ((device->drive_info.drive_type == ATA_DRIVE || device->drive_info.drive_type == ATAPI_DRIVE) && is_SATA(device))
    {
        ret = SUCCESS;
        if (supported)
        {
            if (is_ATA_Identify_Word_Valid_SATA(device->drive_info.IdentifyData.ata.Word076) && device->drive_info.IdentifyData.ata.Word076 & BIT14)
            {
                *supported = true;
            }
            else
            {
                *supported = false;
            }
        }
        if (enabled)
        {
            if (is_ATA_Identify_Word_Valid_SATA(device->drive_info.IdentifyData.ata.Word079) && device->drive_info.IdentifyData.ata.Word079 & BIT7)
            {
                *enabled = true;
            }
            else
            {
                *enabled = false;
            }
        }
    }
    return ret;
}

eReturnValues sata_Set_Device_Automatic_Partioan_To_Slumber_Transtisions(tDevice *device, bool enable)
{
    eReturnValues ret = NOT_SUPPORTED;
    if ((device->drive_info.drive_type == ATA_DRIVE || device->drive_info.drive_type == ATAPI_DRIVE) && is_SATA(device))
    {
        bool dipmEnabled = false;
        eReturnValues getDIPM = sata_Get_Device_Initiated_Interface_Power_State_Transitions(device, M_NULLPTR, &dipmEnabled);//DIPM must be ENABLED before we can change this feature!!
        if (getDIPM == SUCCESS && dipmEnabled)
        {
            bool supported = false;
            if (SUCCESS == sata_Get_Device_Automatic_Partioan_To_Slumber_Transtisions(device, &supported, M_NULLPTR))
            {
                if (enable)
                {
                    ret = ata_Set_Features(device, SF_ENABLE_SATA_FEATURE, 0x07, 0, 0, 0);
                }
                else
                {
                    ret = ata_Set_Features(device, SF_DISABLE_SATA_FEATURE, 0x07, 0, 0, 0);
                }
                //Issue an identify to update the identify data...
                if (device->drive_info.drive_type == ATA_DRIVE)
                {
                    ata_Identify(device, C_CAST(uint8_t*, &device->drive_info.IdentifyData.ata.Word000), LEGACY_DRIVE_SEC_SIZE);
                }
                else if (device->drive_info.drive_type == ATAPI_DRIVE)
                {
                    ata_Identify_Packet_Device(device, C_CAST(uint8_t*, &device->drive_info.IdentifyData.ata.Word000), LEGACY_DRIVE_SEC_SIZE);
                }
            }
        }
    }
    return ret;
}

eReturnValues transition_To_Active(tDevice *device)
{
    eReturnValues ret = NOT_SUPPORTED;
    if (device->drive_info.drive_type == ATA_DRIVE && device->drive_info.interface_type == IDE_INTERFACE)
    {
        //no ATA command to do this, so we need to issue something to perform a medium access.
        uint64_t randomLBA = 0;
        seed_64(C_CAST(uint64_t, time(M_NULLPTR)));
        randomLBA = random_Range_64(0, device->drive_info.deviceMaxLba);
        ret = ata_Read_Verify(device, randomLBA, 1);
    }
    else //treat as SCSI
    {
        if (device->drive_info.scsiVersion > SCSI_VERSION_SCSI2)//checking for support after SCSI2. This isn't perfect, but should be ok for now.
        {
            ret = scsi_Start_Stop_Unit(device, false, 0, PC_ACTIVE, false, false, false);
        }
        else
        {
            //before you could specify a power condition, you used the "Start" bit as a way to move from standby to active
            ret = scsi_Start_Stop_Unit(device, false, 0, PC_START_VALID, false, false, true);
        }
    }
    return ret;
}

eReturnValues transition_To_Standby(tDevice *device)
{
    eReturnValues ret = NOT_SUPPORTED;
    if (device->drive_info.drive_type == ATA_DRIVE)
    {
        ret = ata_Standby_Immediate(device);
    }
    else //treat as SCSI
    {
        if (device->drive_info.scsiVersion > SCSI_VERSION_SCSI2)//checking for support after SCSI2. This isn't perfect, but should be ok for now.
        {
            ret = scsi_Start_Stop_Unit(device, false, 0, PC_STANDBY, false, false, false);
        }
        else
        {
            //before you could specify a power condition, you used the "Start" bit as a way to move from standby to active
            ret = scsi_Start_Stop_Unit(device, false, 0, 0, false, false, false);
        }
    }
    return ret;
}

eReturnValues transition_To_Idle(tDevice *device, bool unload)
{
    eReturnValues ret = NOT_SUPPORTED;
    if (device->drive_info.drive_type == ATA_DRIVE)
    {
        if (unload)
        {
            if ((is_ATA_Identify_Word_Valid_With_Bits_14_And_15(device->drive_info.IdentifyData.ata.Word084) && device->drive_info.IdentifyData.ata.Word084 & BIT13)
                || (is_ATA_Identify_Word_Valid_With_Bits_14_And_15(device->drive_info.IdentifyData.ata.Word087) && device->drive_info.IdentifyData.ata.Word087 & BIT13))
            {
                //send the command since it supports the unload feature...otherwise we return NOT_SUPPORTED
                ret = ata_Idle_Immediate(device, true);
            }
        }
        else
        {
            ret = ata_Idle_Immediate(device, false);
        }
    }
    else
    {
        if (device->drive_info.scsiVersion > SCSI_VERSION_SCSI2)//checking for support after SCSI2. This isn't perfect, but should be ok for now.
        {
            if (unload)
            {
                //unload can happen if power condition modifier set to 1. Needs SBC3/SPC3.
                if (device->drive_info.scsiVersion > SCSI_VERSION_SPC_2)
                {
                    ret = scsi_Start_Stop_Unit(device, false, 1, PC_IDLE, false, false, false);
                }
            }
            else
            {
                ret = scsi_Start_Stop_Unit(device, false, 0, PC_IDLE, false, false, false);
            }
        }
    }
    return ret;
}

eReturnValues transition_To_Sleep(tDevice *device)
{
    eReturnValues ret = NOT_SUPPORTED;
    if (device->drive_info.drive_type == ATA_DRIVE)
    {
        ret = ata_Sleep(device);
    }
    else //treat as SCSI
    {
        if (device->drive_info.scsiVersion > SCSI_VERSION_SCSI2)//checking for support after SCSI2. This isn't perfect, but should be ok for now.
        {
            ret = scsi_Start_Stop_Unit(device, false, 0, PC_SLEEP, false, false, false);//This is obsolete since SBC2...but we'll send it anyways
        }
    }
    return ret;
}

eReturnValues scsi_Set_Partial_Slumber(tDevice *device, bool enablePartial, bool enableSlumber, bool partialValid, bool slumberValid, bool allPhys, uint8_t phyNumber)
{
    eReturnValues ret = SUCCESS;
    if (!partialValid && !slumberValid)
    {
        return BAD_PARAMETER;
    }
    bool gotFullPageLength = false;
    bool alreadyHaveAllData = false;
    uint16_t enhPhyControlLength = MODE_PARAMETER_HEADER_10_LEN + 8 + 40;//first 8 bytes are a "header" followed by 20 bytes per phy and setting this for 2 phys since that is most common right now. -TJE
    uint8_t *enhSasPhyControl = C_CAST(uint8_t*, safe_calloc_aligned(enhPhyControlLength * sizeof(uint8_t), sizeof(uint8_t), device->os_info.minimumAlignment));
    if (!enhSasPhyControl)
    {
        return MEMORY_FAILURE;
    }
    //read first 4 bytes to get total mode page length, then re-read the part with all the data
    if (SUCCESS == (ret = scsi_Mode_Sense_10(device, MP_PROTOCOL_SPECIFIC_PORT, enhPhyControlLength, 0x03, true, false, MPC_CURRENT_VALUES, enhSasPhyControl)))
    {
        if (enhPhyControlLength < M_BytesTo2ByteValue(enhSasPhyControl[0], enhSasPhyControl[1]) + MODE_PARAMETER_HEADER_10_LEN + M_BytesTo2ByteValue(enhSasPhyControl[6], enhSasPhyControl[7]))
        {
            //parse the header to figure out full page length
            enhPhyControlLength = C_CAST(uint16_t, M_BytesTo2ByteValue(enhSasPhyControl[0], enhSasPhyControl[1]) + MODE_PARAMETER_HEADER_10_LEN + M_BytesTo2ByteValue(enhSasPhyControl[6], enhSasPhyControl[7]));
            gotFullPageLength = true;
            uint8_t *temp = safe_reallocf_aligned(C_CAST(void**, &enhSasPhyControl), 0, enhPhyControlLength, device->os_info.minimumAlignment);
            if (!temp)
            {
                return MEMORY_FAILURE;
            }
            enhSasPhyControl = temp;
        }
        else
        {
            gotFullPageLength = true;
            alreadyHaveAllData = true;
        }
    }
    if (gotFullPageLength)
    {
        if (alreadyHaveAllData || SUCCESS == scsi_Mode_Sense_10(device, MP_PROTOCOL_SPECIFIC_PORT, enhPhyControlLength, 0x03, true, false, MPC_CURRENT_VALUES, enhSasPhyControl))
        {
            //make sure we got the header as we expect it, then validate we got all the data we needed.
            //uint16_t modeDataLength = M_BytesTo2ByteValue(enhSasPhyControl[0], enhSasPhyControl[1]);
            uint16_t blockDescriptorLength = M_BytesTo2ByteValue(enhSasPhyControl[6], enhSasPhyControl[7]);
            //validate we got the right page
            if ((enhSasPhyControl[MODE_PARAMETER_HEADER_10_LEN + blockDescriptorLength + 0] & 0x3F) == 0x19 && (enhSasPhyControl[MODE_PARAMETER_HEADER_10_LEN + blockDescriptorLength + 1]) == 0x03 && (enhSasPhyControl[MODE_PARAMETER_HEADER_10_LEN + blockDescriptorLength + 0] & BIT6) > 0)
            {
                if ((enhSasPhyControl[MODE_PARAMETER_HEADER_10_LEN + blockDescriptorLength + 5] & 0x0F) == 6)//make sure it's the SAS protocol page
                {
                    uint8_t numberOfPhys = enhSasPhyControl[MODE_PARAMETER_HEADER_10_LEN + blockDescriptorLength + 7];
                    uint32_t phyDescriptorOffset = MODE_PARAMETER_HEADER_10_LEN + blockDescriptorLength + 8;//this will be set to the beginnging of the phy descriptors so that when looping through them, it is easier code to read.
                    uint16_t descriptorLength = 19;
                    for (uint16_t phyIter = 0; phyIter < C_CAST(uint16_t, numberOfPhys) && phyDescriptorOffset < enhPhyControlLength; ++phyIter, phyDescriptorOffset += descriptorLength)
                    {
                        uint8_t phyIdentifier = enhSasPhyControl[phyDescriptorOffset + 1];
                        descriptorLength = M_BytesTo2ByteValue(enhSasPhyControl[phyDescriptorOffset + 2], enhSasPhyControl[phyDescriptorOffset + 3]);
                        //check if the caller requested changing all phys or a specific phy and only modify it's descriptor if either of those are true.
                        if (allPhys || phyNumber == phyIdentifier)
                        {
                            if (partialValid)
                            {
                                //byte 19, bit 1
                                if (enablePartial)
                                {
                                    M_SET_BIT8(enhSasPhyControl[phyDescriptorOffset + 19], 1);
                                }
                                else
                                {
                                    M_CLEAR_BIT8(enhSasPhyControl[phyDescriptorOffset + 19], 1);
                                }
                            }
                            if (slumberValid)
                            {
                                //byte 19, bit 2
                                if (enableSlumber)
                                {
                                    M_SET_BIT8(enhSasPhyControl[phyDescriptorOffset + 19], 2);
                                }
                                else
                                {
                                    M_CLEAR_BIT8(enhSasPhyControl[phyDescriptorOffset + 19], 2);
                                }
                            }
                        }
                    }
                    //we've finished making our changes to the mode page, so it's time to write it back!
                    if (SUCCESS != scsi_Mode_Select_10(device, enhPhyControlLength, true, true, false, enhSasPhyControl, enhPhyControlLength))
                    {
                        ret = FAILURE;
                    }
                }
                else
                {
                    ret = NOT_SUPPORTED;
                }
            }
            else
            {
                ret = FAILURE;
            }
        }
        else
        {
            ret = FAILURE;
        }
    }
    safe_free_aligned(&enhSasPhyControl);
    return ret;
}

eReturnValues get_SAS_Enhanced_Phy_Control_Number_Of_Phys(tDevice *device, uint8_t *phyCount)
{
    eReturnValues ret = SUCCESS;
    if (!phyCount)
    {
        return BAD_PARAMETER;
    }
    uint16_t enhPhyControlLength = 8;//only need 8 bytes to get the number of phys
    uint8_t *enhSasPhyControl = C_CAST(uint8_t*, safe_calloc_aligned((MODE_PARAMETER_HEADER_10_LEN + enhPhyControlLength) * sizeof(uint8_t), sizeof(uint8_t), device->os_info.minimumAlignment));
    if (!enhSasPhyControl)
    {
        return MEMORY_FAILURE;
    }
    //read first 4 bytes to get total mode page length, then re-read the part with all the data
    if (SUCCESS == (ret = scsi_Mode_Sense_10(device, MP_PROTOCOL_SPECIFIC_PORT, (MODE_PARAMETER_HEADER_10_LEN + enhPhyControlLength), 0x03, true, false, MPC_CURRENT_VALUES, enhSasPhyControl)))
    {
        //uint16_t modeDataLength = M_BytesTo2ByteValue(enhSasPhyControl[0], enhSasPhyControl[1]);
        uint16_t blockDescriptorLength = M_BytesTo2ByteValue(enhSasPhyControl[6], enhSasPhyControl[7]);
        //validate we got the right page
        if ((enhSasPhyControl[MODE_PARAMETER_HEADER_10_LEN + blockDescriptorLength + 0] & 0x3F) == 0x19 && (enhSasPhyControl[MODE_PARAMETER_HEADER_10_LEN + blockDescriptorLength + 1]) == 0x03 && (enhSasPhyControl[MODE_PARAMETER_HEADER_10_LEN + blockDescriptorLength + 0] & BIT6) > 0)
        {
            if ((enhSasPhyControl[MODE_PARAMETER_HEADER_10_LEN + blockDescriptorLength + 5] & 0x0F) == 6)//make sure it's the SAS protocol page
            {
                *phyCount = enhSasPhyControl[MODE_PARAMETER_HEADER_10_LEN + blockDescriptorLength + 7];
            }
        }
    }
    safe_free_aligned(&enhSasPhyControl);
    return ret;
}

eReturnValues get_SAS_Enhanced_Phy_Control_Partial_Slumber_Settings(tDevice *device, bool allPhys, uint8_t phyNumber, ptrSasEnhPhyControl enhPhyControlData, uint32_t enhPhyControlDataSize)
{
    eReturnValues ret = SUCCESS;
    //make sure the structure that will be filled in makes sense at a quick check
    if (!enhPhyControlData || enhPhyControlDataSize == 0 || enhPhyControlDataSize % sizeof(sasEnhPhyControl))
    {
        return BAD_PARAMETER;
    }

    bool gotFullPageLength = false;
    uint16_t enhPhyControlLength = 0;
    uint8_t *enhSasPhyControl = C_CAST(uint8_t*, safe_calloc_aligned((MODE_PARAMETER_HEADER_10_LEN + enhPhyControlLength) * sizeof(uint8_t), sizeof(uint8_t), device->os_info.minimumAlignment));
    if (!enhSasPhyControl)
    {
        return MEMORY_FAILURE;
    }
    //read first 4 bytes to get total mode page length, then re-read the part with all the data
    if (SUCCESS == (ret = scsi_Mode_Sense_10(device, MP_PROTOCOL_SPECIFIC_PORT, (MODE_PARAMETER_HEADER_10_LEN + enhPhyControlLength), 0x03, true, false, MPC_CURRENT_VALUES, enhSasPhyControl)))
    {
        //parse the header to figure out full page length
        enhPhyControlLength = M_BytesTo2ByteValue(enhSasPhyControl[0], enhSasPhyControl[1]);
        gotFullPageLength = true;
        safe_free_aligned(&enhSasPhyControl);
        enhSasPhyControl = C_CAST(uint8_t*, safe_calloc_aligned((MODE_PARAMETER_HEADER_10_LEN + enhPhyControlLength) * sizeof(uint8_t), sizeof(uint8_t), device->os_info.minimumAlignment));
        if (!enhSasPhyControl)
        {
            return MEMORY_FAILURE;
        }
    }
    if (gotFullPageLength)
    {
        if (SUCCESS == scsi_Mode_Sense_10(device, MP_PROTOCOL_SPECIFIC_PORT, (MODE_PARAMETER_HEADER_10_LEN + enhPhyControlLength), 0x03, true, false, MPC_CURRENT_VALUES, enhSasPhyControl))
        {
            //make sure we got the header as we expect it, then validate we got all the data we needed.
            //uint16_t modeDataLength = M_BytesTo2ByteValue(enhSasPhyControl[0], enhSasPhyControl[1]);
            uint16_t blockDescriptorLength = M_BytesTo2ByteValue(enhSasPhyControl[6], enhSasPhyControl[7]);
            //validate we got the right page
            if ((enhSasPhyControl[MODE_PARAMETER_HEADER_10_LEN + blockDescriptorLength + 0] & 0x3F) == 0x19 && (enhSasPhyControl[MODE_PARAMETER_HEADER_10_LEN + blockDescriptorLength + 1]) == 0x03 && (enhSasPhyControl[MODE_PARAMETER_HEADER_10_LEN + blockDescriptorLength + 0] & BIT6) > 0)
            {
                if ((enhSasPhyControl[MODE_PARAMETER_HEADER_10_LEN + blockDescriptorLength + 5] & 0x0F) == 6)//make sure it's the SAS protocol page
                {
                    uint8_t numberOfPhys = enhSasPhyControl[MODE_PARAMETER_HEADER_10_LEN + blockDescriptorLength + 7];
                    uint32_t phyDescriptorOffset = MODE_PARAMETER_HEADER_10_LEN + blockDescriptorLength + 8;//this will be set to the beginnging of the phy descriptors so that when looping through them, it is easier code to read.
                    uint16_t descriptorLength = 19;
                    uint8_t phyCounter = 0;
                    for (uint16_t phyIter = 0; phyIter < C_CAST(uint16_t, numberOfPhys) && (phyCounter * sizeof(sasEnhPhyControl)) < enhPhyControlDataSize; ++phyIter, phyDescriptorOffset += descriptorLength, ++phyCounter)
                    {
                        uint8_t phyIdentifier = enhSasPhyControl[phyDescriptorOffset + 1];
                        descriptorLength = M_BytesTo2ByteValue(enhSasPhyControl[phyDescriptorOffset + 2], enhSasPhyControl[phyDescriptorOffset + 3]) + 4;
                        //check if the caller requested changing all phys or a specific phy and only modify it's descriptor if either of those are true.
                        if (allPhys)
                        {
                            enhPhyControlData[phyIdentifier].phyIdentifier = phyIdentifier;
                            enhPhyControlData[phyIdentifier].enablePartial = enhSasPhyControl[phyDescriptorOffset + 19] & BIT1;
                            enhPhyControlData[phyIdentifier].enableSlumber = enhSasPhyControl[phyDescriptorOffset + 19] & BIT2;
                        }
                        else if (phyNumber == phyIdentifier)
                        {
                            enhPhyControlData->phyIdentifier = phyIdentifier;
                            enhPhyControlData->enablePartial = enhSasPhyControl[phyDescriptorOffset + 19] & BIT1;
                            enhPhyControlData->enableSlumber = enhSasPhyControl[phyDescriptorOffset + 19] & BIT2;
                        }
                    }
                }
                else
                {
                    ret = NOT_SUPPORTED;
                }
            }
            else
            {
                ret = FAILURE;
            }
        }
        else
        {
            ret = FAILURE;
        }
    }
    safe_free_aligned(&enhSasPhyControl);

    return ret;
}

void show_SAS_Enh_Phy_Control_Partial_Slumber(ptrSasEnhPhyControl enhPhyControlData, uint32_t enhPhyControlDataSize, bool showPartial, bool showSlumber)
{
    if (!showPartial && !showSlumber)
    {
        return;//nothing that matters was requested to be shown
    }
    if (!enhPhyControlData || enhPhyControlDataSize == 0 || enhPhyControlDataSize % sizeof(sasEnhPhyControl))
    {
        return;//bad parameter that could cause breakage
    }
    uint32_t totalPhys = enhPhyControlDataSize / sizeof(sasEnhPhyControl);
    //Print a format header
    printf("Phy#");
    if (showPartial)
    {
        printf("\tPartial ");
    }
    if (showSlumber)
    {
        printf("\tSlumber");
    }
    printf("\n");
    for (uint32_t phyIter = 0; phyIter < totalPhys; ++phyIter)
    {
        printf(" %2" PRIu8 " ", enhPhyControlData[phyIter].phyIdentifier);
        if (showPartial)
        {
            if (enhPhyControlData[phyIter].enablePartial)
            {
                printf("\tEnabled ");
            }
            else
            {
                printf("\tDisabled");
            }
        }
        if (showSlumber)
        {
            if (enhPhyControlData[phyIter].enableSlumber)
            {
                printf("\tEnabled ");
            }
            else
            {
                printf("\tDisabled");
            }
        }
        printf("\n");
    }
    printf("\n");
    return;
}

eReturnValues get_PUIS_Info(tDevice* device, ptrPuisInfo info)
{
    eReturnValues ret = NOT_SUPPORTED;
    if (!info)
    {
        return BAD_PARAMETER;
    }
    if (device->drive_info.drive_type == ATA_DRIVE)
    {
        ret = SUCCESS;
        //set everything to false then verify the identify bits
        info->puisSupported = false;
        info->puisEnabled = false;
        info->spinupCommandRequired = false;
        if (is_ATA_Identify_Word_Valid_With_Bits_14_And_15(device->drive_info.IdentifyData.ata.Word083) && device->drive_info.IdentifyData.ata.Word083 & BIT5)
        {
            info->puisSupported = true;
        }
        if (is_ATA_Identify_Word_Valid(device->drive_info.IdentifyData.ata.Word086) && device->drive_info.IdentifyData.ata.Word086 & BIT5)
        {
            info->puisEnabled = true;
        }
        if ((is_ATA_Identify_Word_Valid_With_Bits_14_And_15(device->drive_info.IdentifyData.ata.Word083) && device->drive_info.IdentifyData.ata.Word083 & BIT6)
            || (is_ATA_Identify_Word_Valid(device->drive_info.IdentifyData.ata.Word086) && device->drive_info.IdentifyData.ata.Word086 & BIT6))
        {
            info->spinupCommandRequired = true;
        }
    }
    return ret;
}

eReturnValues enable_Disable_PUIS_Feature(tDevice* device, bool enable)
{
    eReturnValues ret = NOT_SUPPORTED;
    if (device->drive_info.drive_type == ATA_DRIVE)
    {
        //check the identify bits to make sure PUIS is supported.
        if (is_ATA_Identify_Word_Valid_With_Bits_14_And_15(device->drive_info.IdentifyData.ata.Word083) && device->drive_info.IdentifyData.ata.Word083 & BIT5)
        {
            if (enable)
            {
                ret = ata_Set_Features(device, SF_ENABLE_PUIS_FEATURE, 0, 0, 0, 0);
            }
            else
            {
                ret = ata_Set_Features(device, SF_DISABLE_PUIS_FEATURE, 0, 0, 0, 0);
            }
        }
    }
    return ret;
}

eReturnValues puis_Spinup(tDevice* device)
{
    eReturnValues ret = NOT_SUPPORTED;
    if (device->drive_info.drive_type == ATA_DRIVE)
    {
        //check the identify bits to make sure PUIS is supported.
        if (is_ATA_Identify_Word_Valid_With_Bits_14_And_15(device->drive_info.IdentifyData.ata.Word083) && device->drive_info.IdentifyData.ata.Word083 & BIT5 && device->drive_info.IdentifyData.ata.Word083 & BIT6)
        {
            ret = ata_Set_Features(device, SF_PUIS_DEVICE_SPIN_UP, 0, 0, 0, 0);
        }
        else
        {
            //this command is not required to spinup the drive. Any media access will spin it up.
        }
    }
    return ret;
}
