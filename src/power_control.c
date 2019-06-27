//
// Do NOT modify or remove this copyright and license
//
// Copyright (c) 2012 - 2018 Seagate Technology LLC and/or its Affiliates, All Rights Reserved
//
// This software is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
//
// ******************************************************************************************
// 
// \file power_control.c
// \brief This file defines the functions for power related changes to drives.

#include "operations_Common.h"
#include "power_control.h"
#include "logs.h"
#include "cmds.h"

//There is no specific way to enable or disable this on SCSI, so this simulates the bahaviour according to what we see with ATA
int scsi_Enable_Disable_EPC_Feature(tDevice *device, eEPCFeatureSet lba_field)
{
    int ret = UNKNOWN;

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
int enable_Disable_EPC_Feature(tDevice *device, eEPCFeatureSet lba_field)
{
    int ret = UNKNOWN;

    if (lba_field == ENABLE_EPC_NOT_SET)
    {
        ret = BAD_PARAMETER;
        return ret;
    }
    if (device->drive_info.drive_type == ATA_DRIVE)
    {
        ret = ata_Set_Features(device, SF_EXTENDED_POWER_CONDITIONS, 0, lba_field, 0,0);
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

int print_Current_Power_Mode(tDevice *device)
{
    int ret = UNKNOWN;
    if (device->drive_info.drive_type == ATA_DRIVE)
    {
        uint8_t powerMode = 0;
        //first check if EPC feature is supported and/or enabled
        uint8_t epcFeature = 0;//0 - disabled, 1 - supported, 2 - enabled.
        uint8_t *identifyData = (uint8_t*)calloc(LEGACY_DRIVE_SEC_SIZE, sizeof(uint8_t));
        if (identifyData == NULL)
        {
            perror("Calloc Failure!\n");
            return MEMORY_FAILURE;
        }

        if (SUCCESS == ata_Identify(device, identifyData, LEGACY_DRIVE_SEC_SIZE))
        {
            //check word 119 bit 7 for EPC support
            uint16_t *identWordPTR = (uint16_t*)identifyData;
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
            free(identifyData);
            return FAILURE;
        }
        free(identifyData);

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
    else if (device->drive_info.drive_type == SCSI_DRIVE)
    {
        /*
        NOTE: Removed the code which was checking to see if the power mode is supported 
              mainly because it was changing the power state of the drive. -MA 
        */
        uint8_t *senseData = (uint8_t*)calloc(SPC3_SENSE_LEN, sizeof(uint8_t));
        if (senseData == NULL)
        {
            perror("Calloc Failure!\n");
            return MEMORY_FAILURE;
        }
        if (SUCCESS == scsi_Request_Sense_Cmd(device, false, senseData, SPC3_SENSE_LEN))
        {
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
                        printf("Active State or in an Unknown state.\n");
                        break;
                    }
                    break;
                default:
                    printf("Active State or in an Unknown state.\n");
                    break;
                }
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
        free(senseData);
    }
    #if !defined(DISABLE_NVME_PASSTHROUGH)
    else if (device->drive_info.drive_type == NVME_DRIVE) 
    {
        uint32_t powerMode = 0;
        ret = get_Power_State(device, &powerMode, CURRENT_VALUE );
        if (ret==SUCCESS)
        {
            printf("Device is in Power State %d\n",powerMode);
        }
        else
        {
            if (VERBOSITY_QUIET < device->deviceVerbosity)
            {
                printf("Unable to retrive current power state!\n");
            }
        }
    } 
    #endif
    else
    {
        if (VERBOSITY_QUIET < device->deviceVerbosity)
        {
            printf("Showing the current power mode is not supported on this drive type at this time\n");
        }
        ret = NOT_SUPPORTED;
    }
    return ret;
}

int transition_Power_State(tDevice *device, ePowerConditionID newState)
{
    int ret = UNKNOWN; 
    if (device->drive_info.drive_type == ATA_DRIVE)
    {
        switch (newState)
        {
        case PWR_CND_STANDBY_Z:
            ret = ata_Set_Features(device, SF_EXTENDED_POWER_CONDITIONS, PWR_CND_STANDBY_Z,\
                                   EPC_GO_TO_POWER_CONDITION, RESERVED, RESERVED);
            break;
        case PWR_CND_STANDBY_Y:
            ret = ata_Set_Features(device, SF_EXTENDED_POWER_CONDITIONS, PWR_CND_STANDBY_Y,\
                                   EPC_GO_TO_POWER_CONDITION, RESERVED, RESERVED);
            break;
        case PWR_CND_IDLE_A:
            ret = ata_Set_Features(device, SF_EXTENDED_POWER_CONDITIONS, PWR_CND_IDLE_A,\
                                   EPC_GO_TO_POWER_CONDITION, RESERVED, RESERVED);
            break;
        case PWR_CND_IDLE_B:
            ret = ata_Set_Features(device, SF_EXTENDED_POWER_CONDITIONS, PWR_CND_IDLE_B,\
                                   EPC_GO_TO_POWER_CONDITION, RESERVED, RESERVED);
            break;
        case PWR_CND_IDLE_C:
            ret = ata_Set_Features(device, SF_EXTENDED_POWER_CONDITIONS, PWR_CND_IDLE_C,\
                                   EPC_GO_TO_POWER_CONDITION, RESERVED, RESERVED);
            break;
        case PWR_CND_ACTIVE: //No such thing in ATA (...yet)
        default:
            if (VERBOSITY_QUIET < device->deviceVerbosity)
            {
                printf("Power State Transition is not supported on this device type at this time\n");
            }
            ret = NOT_SUPPORTED;
            break;
        };
    }
    else if (device->drive_info.drive_type == SCSI_DRIVE)
    {
        switch (newState)
        {
        case PWR_CND_ACTIVE:
            ret = scsi_Start_Stop_Unit(device, true, 0, PC_ACTIVE, false, false, false); 
            break;
        case PWR_CND_STANDBY_Z:
            ret = scsi_Start_Stop_Unit(device, true, 0, PC_STANDBY, false, false, false); 
            break;
        case PWR_CND_STANDBY_Y:
            ret = scsi_Start_Stop_Unit(device, true, 1, PC_STANDBY, false, false, false); 
            break;
        case PWR_CND_IDLE_A:
            ret = scsi_Start_Stop_Unit(device, true, 0, PC_IDLE, false, false, false); 
            break;
        case PWR_CND_IDLE_B:
            ret = scsi_Start_Stop_Unit(device, true, 1, PC_IDLE, false, false, false); 
            break;
        case PWR_CND_IDLE_C:
            ret = scsi_Start_Stop_Unit(device, true, 2, PC_IDLE, false, false, false); 
            break;
        default:
            if (VERBOSITY_QUIET < device->deviceVerbosity)
            {
                printf("Power State Transition is not supported on this device type at this time\n");
            }
            ret = NOT_SUPPORTED;
            break;
        };
    }
    #if !defined(DISABLE_NVME_PASSTHROUGH)
    else if (device->drive_info.drive_type == NVME_DRIVE)
    {
        nvmeFeaturesCmdOpt cmdOpts;
        memset(&cmdOpts,0,sizeof(cmdOpts));
        cmdOpts.featSetGetValue = newState;
        cmdOpts.fid = NVME_FEAT_POWER_MGMT_;
        cmdOpts.sel = NVME_CURRENT_FEAT_SEL;
        ret = nvme_Set_Features(device,&cmdOpts);
        if (ret != SUCCESS) 
        {
            if (VERBOSITY_QUIET < device->deviceVerbosity)
            {
                printf("Power State Transition Failed in NVMe Set Features command\n");
            }
        }
    }
    #endif
    else
    {
        if (VERBOSITY_QUIET < device->deviceVerbosity)
        {
            printf("Power State Transition is not supported on this device type at this time\n");
        }
        ret = NOT_SUPPORTED;
    }
    return ret;
}

//enableDisable = true means enable, false means disable
int ata_Set_Device_Power_Mode(tDevice *device, bool restoreDefaults, bool enableDisable, \
    ePowerConditionID powerCondition, uint32_t powerModeTimer, bool powerModeTimerValid)
{
    int ret = UNKNOWN;
    //first verify the device supports the EPC feature
    uint8_t *ataDataBuffer = (uint8_t*)calloc(LEGACY_DRIVE_SEC_SIZE, sizeof(uint8_t));
    if (!ataDataBuffer)
    {
        perror("calloc failure!\n");
        return MEMORY_FAILURE;
    }
    if (SUCCESS == ata_Identify(device, ataDataBuffer, LEGACY_DRIVE_SEC_SIZE))
    {
        uint16_t *wordPtr = (uint16_t*)ataDataBuffer;
        if ((wordPtr[119] & BIT7) == 0)
        {
            //this means EPC is not supported by the drive.
            if (VERBOSITY_QUIET < device->deviceVerbosity)
            {
                printf("Device does not support the Extended Power Control Feature!\n");
            }
            free(ataDataBuffer);
            return NOT_SUPPORTED;
        }
    }
    else
    {
        if (VERBOSITY_QUIET < device->deviceVerbosity)
        {
            printf("Failed to check if drive supports EPC feature!!\n");
        }
        free(ataDataBuffer);
        return FAILURE;
    }
    free(ataDataBuffer);
    //if we go this far, then we know that we support the required EPC feature
    if (restoreDefaults)
    {
        //this command is restoring the power conditions from the drive's default settings (bit6) and saving them upon completion (bit4)...the other option is to return to the saved settings, but we aren't going to support that with this option right now
        ret = ata_Set_Features(device, SF_EXTENDED_POWER_CONDITIONS, powerCondition, EPC_RESTORE_POWER_CONDITION_SETTINGS | BIT6 | BIT4, RESERVED, RESERVED);
    }
    else//we aren't restoring settings, so we need to set things up to save settings
    {
        uint8_t lbalo = 0;
        uint8_t lbaMid = 0;
        uint16_t lbaHi = 0;
        if (powerModeTimerValid)
        {
            lbalo = EPC_SET_POWER_CONDITION_TIMER;
            lbaMid = M_Byte0(powerModeTimer);
            lbaHi = M_Byte1(powerModeTimer);
        }
        else//they didn't enter a timer value so this command will do the EXACT same thing...just decided to use a different feature to EPC
        {
            lbalo = EPC_SET_POWER_CONDITION_STATE;
        }
        if (enableDisable)
        {
            lbalo |= BIT5;
        }
        //set the save bit
        lbalo |= BIT4;
        //issue the command
        ret = ata_Set_Features(device, SF_EXTENDED_POWER_CONDITIONS, powerCondition, lbalo, lbaMid, lbaHi);
    }
    return ret;
}

//enableDisable = true means enable, false means disable
int scsi_Set_Device_Power_Mode(tDevice *device, bool restoreDefaults, bool enableDisable, \
    ePowerConditionID powerCondition, uint32_t powerModeTimer, bool powerModeTimerValid)
{
    int ret = UNKNOWN;
    uint8_t *temp = NULL;
    //first we need to check that VPD page 8Ah (power condition) exists...and we can possibly use that information to return "not supported, etc"
    uint8_t *scsiDataBuffer = (uint8_t*)calloc(VPD_POWER_CONDITION_LEN, sizeof(uint8_t));//size of 18 is defined in SPC4 for this VPD page
    if (scsiDataBuffer == NULL)
    {
        perror("calloc failure!");
        return MEMORY_FAILURE;
    }
    if (SUCCESS == scsi_Inquiry(device, scsiDataBuffer, VPD_POWER_CONDITION_LEN, POWER_CONDITION, true, false))
    {
        if (scsiDataBuffer[1] != POWER_CONDITION)//make sure we got the correct page...if we did, we will proceed to do what we can with the other passed in options
        {
            if (VERBOSITY_QUIET < device->deviceVerbosity)
            {
                printf("Failed to check if drive supports modifying power conditions!\n");
            }
            free(scsiDataBuffer);
            return FAILURE;
        }
    }
    else
    {
        if (VERBOSITY_QUIET < device->deviceVerbosity)
        {
            printf("Failed to check if drive supports modifying power conditions!\n");
        }
        free(scsiDataBuffer);
        return FAILURE;
    }
    temp = realloc(scsiDataBuffer, (MP_POWER_CONDITION_LEN + MODE_PARAMETER_HEADER_10_LEN) * sizeof(uint8_t));
    if (!temp)
    {
        perror("realloc failure!");
        return MEMORY_FAILURE;
    }
    scsiDataBuffer = temp;
    memset(scsiDataBuffer, 0, MP_POWER_CONDITION_LEN + MODE_PARAMETER_HEADER_10_LEN);
    if (restoreDefaults)
    {
        //read the default values
        if (SUCCESS == scsi_Mode_Sense_10(device, MP_POWER_CONDTION, MP_POWER_CONDITION_LEN + MODE_PARAMETER_HEADER_10_LEN, 0, true, true, MPC_DEFAULT_VALUES, scsiDataBuffer))
        {
            if (powerCondition == PWR_CND_ALL)
            {
                //this will reset all conditions back to default
                ret = scsi_Mode_Select_10(device, MP_POWER_CONDITION_LEN + MODE_PARAMETER_HEADER_10_LEN, true, true, false, scsiDataBuffer, MP_POWER_CONDITION_LEN + MODE_PARAMETER_HEADER_10_LEN);
            }
            else
            {
                uint8_t *currentTimers = (uint8_t*)calloc((MP_POWER_CONDITION_LEN + MODE_PARAMETER_HEADER_10_LEN) * sizeof(uint8_t), sizeof(uint8_t));
                //read the current values
                if (SUCCESS == scsi_Mode_Sense_10(device, MP_POWER_CONDTION, MP_POWER_CONDITION_LEN + MODE_PARAMETER_HEADER_10_LEN, 0, true, true, MPC_CURRENT_VALUES, currentTimers))
                {
                    switch (powerCondition)
                    {
                    case PWR_CND_STANDBY_Z:
                        if (scsiDataBuffer[MODE_PARAMETER_HEADER_10_LEN + 3] & BIT0)
                        {
                            currentTimers[MODE_PARAMETER_HEADER_10_LEN + 3] |= BIT0;
                        }
                        else
                        {
                            if (currentTimers[MODE_PARAMETER_HEADER_10_LEN + 3] & BIT0)
                            {
                                currentTimers[MODE_PARAMETER_HEADER_10_LEN + 3] ^= BIT0;
                            }
                        }
                        currentTimers[MODE_PARAMETER_HEADER_10_LEN + 8] = scsiDataBuffer[MODE_PARAMETER_HEADER_10_LEN + 8];
                        currentTimers[MODE_PARAMETER_HEADER_10_LEN + 9] = scsiDataBuffer[MODE_PARAMETER_HEADER_10_LEN + 9];
                        currentTimers[MODE_PARAMETER_HEADER_10_LEN + 10] = scsiDataBuffer[MODE_PARAMETER_HEADER_10_LEN + 10];
                        currentTimers[MODE_PARAMETER_HEADER_10_LEN + 11] = scsiDataBuffer[MODE_PARAMETER_HEADER_10_LEN + 11];
                        break;
                    case PWR_CND_STANDBY_Y:
                        if (scsiDataBuffer[MODE_PARAMETER_HEADER_10_LEN + 2] & BIT0)
                        {
                            currentTimers[MODE_PARAMETER_HEADER_10_LEN + 2] |= BIT0;
                        }
                        else
                        {
                            if (currentTimers[MODE_PARAMETER_HEADER_10_LEN + 2] & BIT0)
                            {
                                currentTimers[MODE_PARAMETER_HEADER_10_LEN + 2] ^= BIT0;
                            }
                        }
                        currentTimers[MODE_PARAMETER_HEADER_10_LEN + 20] = scsiDataBuffer[MODE_PARAMETER_HEADER_10_LEN + 20];
                        currentTimers[MODE_PARAMETER_HEADER_10_LEN + 21] = scsiDataBuffer[MODE_PARAMETER_HEADER_10_LEN + 21];
                        currentTimers[MODE_PARAMETER_HEADER_10_LEN + 22] = scsiDataBuffer[MODE_PARAMETER_HEADER_10_LEN + 22];
                        currentTimers[MODE_PARAMETER_HEADER_10_LEN + 23] = scsiDataBuffer[MODE_PARAMETER_HEADER_10_LEN + 23];
                        break;
                    case PWR_CND_IDLE_A:
                        if (scsiDataBuffer[MODE_PARAMETER_HEADER_10_LEN + 3] & BIT1)
                        {
                            currentTimers[MODE_PARAMETER_HEADER_10_LEN + 3] |= BIT1;
                        }
                        else
                        {
                            if (currentTimers[MODE_PARAMETER_HEADER_10_LEN + 3] & BIT1)
                            {
                                currentTimers[MODE_PARAMETER_HEADER_10_LEN + 3] ^= BIT1;
                            }
                        }
                        currentTimers[MODE_PARAMETER_HEADER_10_LEN + 4] = scsiDataBuffer[MODE_PARAMETER_HEADER_10_LEN + 4];
                        currentTimers[MODE_PARAMETER_HEADER_10_LEN + 5] = scsiDataBuffer[MODE_PARAMETER_HEADER_10_LEN + 5];
                        currentTimers[MODE_PARAMETER_HEADER_10_LEN + 6] = scsiDataBuffer[MODE_PARAMETER_HEADER_10_LEN + 6];
                        currentTimers[MODE_PARAMETER_HEADER_10_LEN + 7] = scsiDataBuffer[MODE_PARAMETER_HEADER_10_LEN + 7];
                        break;
                    case PWR_CND_IDLE_B:
                        if (scsiDataBuffer[MODE_PARAMETER_HEADER_10_LEN + 3] & BIT2)
                        {
                            currentTimers[MODE_PARAMETER_HEADER_10_LEN + 3] |= BIT2;
                        }
                        else
                        {
                            if (currentTimers[MODE_PARAMETER_HEADER_10_LEN + 3] & BIT2)
                            {
                                currentTimers[MODE_PARAMETER_HEADER_10_LEN + 3] ^= BIT2;
                            }
                        }
                        currentTimers[MODE_PARAMETER_HEADER_10_LEN + 12] = scsiDataBuffer[MODE_PARAMETER_HEADER_10_LEN + 12];
                        currentTimers[MODE_PARAMETER_HEADER_10_LEN + 13] = scsiDataBuffer[MODE_PARAMETER_HEADER_10_LEN + 13];
                        currentTimers[MODE_PARAMETER_HEADER_10_LEN + 14] = scsiDataBuffer[MODE_PARAMETER_HEADER_10_LEN + 14];
                        currentTimers[MODE_PARAMETER_HEADER_10_LEN + 15] = scsiDataBuffer[MODE_PARAMETER_HEADER_10_LEN + 15];
                        break;
                    case PWR_CND_IDLE_C:
                        if (scsiDataBuffer[MODE_PARAMETER_HEADER_10_LEN + 3] & BIT3)
                        {
                            currentTimers[MODE_PARAMETER_HEADER_10_LEN + 3] |= BIT3;
                        }
                        else
                        {
                            if (currentTimers[MODE_PARAMETER_HEADER_10_LEN + 3] & BIT3)
                            {
                                currentTimers[MODE_PARAMETER_HEADER_10_LEN + 3] ^= BIT3;
                            }
                        }
                        currentTimers[MODE_PARAMETER_HEADER_10_LEN + 16] = scsiDataBuffer[MODE_PARAMETER_HEADER_10_LEN + 16];
                        currentTimers[MODE_PARAMETER_HEADER_10_LEN + 17] = scsiDataBuffer[MODE_PARAMETER_HEADER_10_LEN + 17];
                        currentTimers[MODE_PARAMETER_HEADER_10_LEN + 18] = scsiDataBuffer[MODE_PARAMETER_HEADER_10_LEN + 18];
                        currentTimers[MODE_PARAMETER_HEADER_10_LEN + 19] = scsiDataBuffer[MODE_PARAMETER_HEADER_10_LEN + 19];
                        break;
                    default://don't do anything...or return a error? (this case fixes a warning)
                        break;
                    }
                    //send the timers back with the default retored for the specified timer
                    ret = scsi_Mode_Select_10(device, MP_POWER_CONDITION_LEN + MODE_PARAMETER_HEADER_10_LEN, true, true, false, currentTimers, MP_POWER_CONDITION_LEN + MODE_PARAMETER_HEADER_10_LEN);
                }
                else
                {
                    ret = FAILURE;
                }
                safe_Free(currentTimers);
            }
        }
        else
        {
            //failure
            if (VERBOSITY_QUIET < device->deviceVerbosity)
            {
                printf("Failed to read default power condition values!\n");
            }
            ret = FAILURE;
        }
    }
    else
    {
        //first read the mode page so that we only make the specified change and not disable or break anything else
        if (SUCCESS == scsi_Mode_Sense_10(device, MP_POWER_CONDTION, MP_POWER_CONDITION_LEN + MODE_PARAMETER_HEADER_10_LEN, 0, true, true, MPC_CURRENT_VALUES, scsiDataBuffer))
        {
            uint8_t *modeSelectBuffer = (uint8_t*)calloc(MP_POWER_CONDITION_LEN + MODE_PARAMETER_HEADER_10_LEN, sizeof(uint8_t));
            if (modeSelectBuffer == NULL)
            {
                perror("calloc failure!");
                return MEMORY_FAILURE;
            }
            switch (powerCondition)
            {
            case PWR_CND_STANDBY_Z:
                if (enableDisable)
                {
                    scsiDataBuffer[MODE_PARAMETER_HEADER_10_LEN + 3] |= BIT0;
                    if (powerModeTimerValid)
                    {
                        scsiDataBuffer[MODE_PARAMETER_HEADER_10_LEN + 8] = (uint8_t)(powerModeTimer >> 24);
                        scsiDataBuffer[MODE_PARAMETER_HEADER_10_LEN + 9] = (uint8_t)(powerModeTimer >> 16);
                        scsiDataBuffer[MODE_PARAMETER_HEADER_10_LEN + 10] = (uint8_t)(powerModeTimer >> 8);
                        scsiDataBuffer[MODE_PARAMETER_HEADER_10_LEN + 11] = (uint8_t)powerModeTimer;
                    }
                }
                else
                {
                    if (scsiDataBuffer[MODE_PARAMETER_HEADER_10_LEN + 3] & BIT0)
                    {
                        scsiDataBuffer[MODE_PARAMETER_HEADER_10_LEN + 3] ^= BIT0;
                    }
                }
                break;
            case PWR_CND_STANDBY_Y:
                if (enableDisable)
                {
                    scsiDataBuffer[MODE_PARAMETER_HEADER_10_LEN + 2] |= BIT0;
                    if (powerModeTimerValid)
                    {
                        scsiDataBuffer[MODE_PARAMETER_HEADER_10_LEN + 20] = (uint8_t)(powerModeTimer >> 24);
                        scsiDataBuffer[MODE_PARAMETER_HEADER_10_LEN + 21] = (uint8_t)(powerModeTimer >> 16);
                        scsiDataBuffer[MODE_PARAMETER_HEADER_10_LEN + 22] = (uint8_t)(powerModeTimer >> 8);
                        scsiDataBuffer[MODE_PARAMETER_HEADER_10_LEN + 23] = (uint8_t)powerModeTimer;
                    }
                }
                else
                {
                    if (scsiDataBuffer[MODE_PARAMETER_HEADER_10_LEN + 2] & BIT0)
                    {
                        scsiDataBuffer[MODE_PARAMETER_HEADER_10_LEN + 2] ^= BIT0;
                    }
                }
                break;
            case PWR_CND_IDLE_A:
                if (enableDisable)
                {
                    scsiDataBuffer[MODE_PARAMETER_HEADER_10_LEN + 3] |= BIT1;
                    if (powerModeTimerValid)
                    {
                        scsiDataBuffer[MODE_PARAMETER_HEADER_10_LEN + 4] = (uint8_t)(powerModeTimer >> 24);
                        scsiDataBuffer[MODE_PARAMETER_HEADER_10_LEN + 5] = (uint8_t)(powerModeTimer >> 16);
                        scsiDataBuffer[MODE_PARAMETER_HEADER_10_LEN + 6] = (uint8_t)(powerModeTimer >> 8);
                        scsiDataBuffer[MODE_PARAMETER_HEADER_10_LEN + 7] = (uint8_t)powerModeTimer;
                    }
                }
                else
                {
                    if (scsiDataBuffer[MODE_PARAMETER_HEADER_10_LEN + 3] & BIT1)
                    {
                        scsiDataBuffer[MODE_PARAMETER_HEADER_10_LEN + 3] ^= BIT1;
                    }
                }
                break;
            case PWR_CND_IDLE_B:
                if (enableDisable)
                {
                    scsiDataBuffer[MODE_PARAMETER_HEADER_10_LEN + 3] |= BIT2;
                    if (powerModeTimerValid)
                    {
                        scsiDataBuffer[MODE_PARAMETER_HEADER_10_LEN + 12] = (uint8_t)(powerModeTimer >> 24);
                        scsiDataBuffer[MODE_PARAMETER_HEADER_10_LEN + 13] = (uint8_t)(powerModeTimer >> 16);
                        scsiDataBuffer[MODE_PARAMETER_HEADER_10_LEN + 14] = (uint8_t)(powerModeTimer >> 8);
                        scsiDataBuffer[MODE_PARAMETER_HEADER_10_LEN + 15] = (uint8_t)powerModeTimer;
                    }
                }
                else
                {
                    if (scsiDataBuffer[MODE_PARAMETER_HEADER_10_LEN + 3] & BIT2)
                    {
                        scsiDataBuffer[MODE_PARAMETER_HEADER_10_LEN + 3] ^= BIT2;
                    }
                }
                break;
            case PWR_CND_IDLE_C:
                if (enableDisable)
                {
                    scsiDataBuffer[MODE_PARAMETER_HEADER_10_LEN + 3] |= BIT3;
                    if (powerModeTimerValid)
                    {
                        scsiDataBuffer[MODE_PARAMETER_HEADER_10_LEN + 16] = (uint8_t)(powerModeTimer >> 24);
                        scsiDataBuffer[MODE_PARAMETER_HEADER_10_LEN + 17] = (uint8_t)(powerModeTimer >> 16);
                        scsiDataBuffer[MODE_PARAMETER_HEADER_10_LEN + 18] = (uint8_t)(powerModeTimer >> 8);
                        scsiDataBuffer[MODE_PARAMETER_HEADER_10_LEN + 19] = (uint8_t)powerModeTimer;
                    }
                }
                else
                {
                    if (scsiDataBuffer[MODE_PARAMETER_HEADER_10_LEN + 3] & BIT3)
                    {
                        scsiDataBuffer[MODE_PARAMETER_HEADER_10_LEN + 3] ^= BIT3;
                    }
                }
                break;
            case PWR_CND_ALL:
            default:
                if (enableDisable)
                {
                    scsiDataBuffer[MODE_PARAMETER_HEADER_10_LEN + 2] |= BIT0;
                    scsiDataBuffer[MODE_PARAMETER_HEADER_10_LEN + 3] |= BIT3 | BIT2 | BIT1;
                }
                else
                {
                    if ((scsiDataBuffer[MODE_PARAMETER_HEADER_10_LEN + 2] & BIT0) > 0)
                    {
                        scsiDataBuffer[MODE_PARAMETER_HEADER_10_LEN + 2] ^= BIT0;
                    }
                    if (scsiDataBuffer[MODE_PARAMETER_HEADER_10_LEN + 3] & BIT3)
                    {
                        scsiDataBuffer[MODE_PARAMETER_HEADER_10_LEN + 3] ^= BIT3;
                    }
                    if (scsiDataBuffer[MODE_PARAMETER_HEADER_10_LEN + 3] & BIT2)
                    {
                        scsiDataBuffer[MODE_PARAMETER_HEADER_10_LEN + 3] ^= BIT2;
                    }
                    if (scsiDataBuffer[MODE_PARAMETER_HEADER_10_LEN + 3] & BIT1)
                    {
                        scsiDataBuffer[MODE_PARAMETER_HEADER_10_LEN + 3] ^= BIT1;
                    }
                }
                //set the timers to the same thing if one was provided
                if (powerModeTimerValid)
                {
                    //idle a
                    scsiDataBuffer[MODE_PARAMETER_HEADER_10_LEN + 4] = (uint8_t)(powerModeTimer >> 24);
                    scsiDataBuffer[MODE_PARAMETER_HEADER_10_LEN + 5] = (uint8_t)(powerModeTimer >> 16);
                    scsiDataBuffer[MODE_PARAMETER_HEADER_10_LEN + 6] = (uint8_t)(powerModeTimer >> 8);
                    scsiDataBuffer[MODE_PARAMETER_HEADER_10_LEN + 7] = (uint8_t)powerModeTimer;
                    //standby z
                    scsiDataBuffer[MODE_PARAMETER_HEADER_10_LEN + 8] = (uint8_t)(powerModeTimer >> 24);
                    scsiDataBuffer[MODE_PARAMETER_HEADER_10_LEN + 9] = (uint8_t)(powerModeTimer >> 16);
                    scsiDataBuffer[MODE_PARAMETER_HEADER_10_LEN + 10] = (uint8_t)(powerModeTimer >> 8);
                    scsiDataBuffer[MODE_PARAMETER_HEADER_10_LEN + 11] = (uint8_t)powerModeTimer;
                    //idle b
                    scsiDataBuffer[MODE_PARAMETER_HEADER_10_LEN + 12] = (uint8_t)(powerModeTimer >> 24);
                    scsiDataBuffer[MODE_PARAMETER_HEADER_10_LEN + 13] = (uint8_t)(powerModeTimer >> 16);
                    scsiDataBuffer[MODE_PARAMETER_HEADER_10_LEN + 14] = (uint8_t)(powerModeTimer >> 8);
                    scsiDataBuffer[MODE_PARAMETER_HEADER_10_LEN + 15] = (uint8_t)powerModeTimer;
                    //idle c
                    scsiDataBuffer[MODE_PARAMETER_HEADER_10_LEN + 16] = (uint8_t)(powerModeTimer >> 24);
                    scsiDataBuffer[MODE_PARAMETER_HEADER_10_LEN + 17] = (uint8_t)(powerModeTimer >> 16);
                    scsiDataBuffer[MODE_PARAMETER_HEADER_10_LEN + 18] = (uint8_t)(powerModeTimer >> 8);
                    scsiDataBuffer[MODE_PARAMETER_HEADER_10_LEN + 19] = (uint8_t)powerModeTimer;
                    //standby y
                    scsiDataBuffer[MODE_PARAMETER_HEADER_10_LEN + 20] = (uint8_t)(powerModeTimer >> 24);
                    scsiDataBuffer[MODE_PARAMETER_HEADER_10_LEN + 21] = (uint8_t)(powerModeTimer >> 16);
                    scsiDataBuffer[MODE_PARAMETER_HEADER_10_LEN + 22] = (uint8_t)(powerModeTimer >> 8);
                    scsiDataBuffer[MODE_PARAMETER_HEADER_10_LEN + 23] = (uint8_t)powerModeTimer;
                }
            }
            //set up the mode select header
            modeSelectBuffer[0] = 0;//len MSB
            modeSelectBuffer[1] = 47;//len LSB
            modeSelectBuffer[2] = 0;//medium type
            modeSelectBuffer[3] = 0;//device specific parameter
            modeSelectBuffer[4] = RESERVED;
            modeSelectBuffer[5] = RESERVED;
            modeSelectBuffer[6] = 0;//block descriptor length MSB
            modeSelectBuffer[7] = 0;//block descriptor length LSB
            //copy the data we were modifying to the buffer with the header
            memcpy(&modeSelectBuffer[8], &scsiDataBuffer[MODE_PARAMETER_HEADER_10_LEN], MP_POWER_CONDITION_LEN);
            ret = scsi_Mode_Select_10(device, MP_POWER_CONDITION_LEN + MODE_PARAMETER_HEADER_10_LEN, true, true, false, modeSelectBuffer, MP_POWER_CONDITION_LEN + MODE_PARAMETER_HEADER_10_LEN);
            safe_Free(modeSelectBuffer);
        }
    }
    safe_Free(scsiDataBuffer);
    return ret;
}

//enableDisable = true means enable, false means disable
int set_Device_Power_Mode(tDevice *device, bool restoreDefaults, bool enableDisable,\
    ePowerConditionID powerCondition, uint32_t powerModeTimer, bool powerModeTimerValid)
{
    int ret = UNKNOWN;

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

int get_Power_State(tDevice *device, uint32_t * powerState, eFeatureModeSelect selectValue )
{
    #if !defined(DISABLE_NVME_PASSTHROUGH)
    int ret = UNKNOWN;
    if (device->drive_info.drive_type == NVME_DRIVE)
    {
        nvmeFeaturesCmdOpt cmdOpts;

        switch (selectValue) 
        {
        case CURRENT_VALUE:
            cmdOpts.fid = NVME_FEAT_POWER_MGMT_;
            cmdOpts.sel = NVME_CURRENT_FEAT_SEL;
            ret = nvme_Get_Features(device,&cmdOpts);
            if (ret == SUCCESS) 
            {
                * powerState = cmdOpts.featSetGetValue;
            }
            break;
        case DEFAULT_VALUE:
        case SAVED_VALUE:
        case CAPABILITIES:
        case CHANGEABLE_VALUE:
        default:
            if (VERBOSITY_QUIET < device->deviceVerbosity)
            {
                printf("Power State=0x%x is currently not supported on this device.\n",selectValue);
            }
            ret = NOT_SUPPORTED;
            break;

        }
    }
    else
    #endif
    {
        if (VERBOSITY_QUIET < device->deviceVerbosity)
        {
            printf("Get Power State is currently not supported on this device type at this time.\n");
        }
        return NOT_SUPPORTED;
    }

    return SUCCESS;
}

int get_Power_Consumption_Identifiers(tDevice *device, ptrPowerConsumptionIdentifiers identifiers)
{
    int ret = NOT_SUPPORTED;
    if (device->drive_info.drive_type == SCSI_DRIVE)//this is only available on SCSI drives.
    {
        uint32_t powerConsumptionLength = 0;
        if (SUCCESS == get_SCSI_VPD_Page_Size(device, POWER_CONSUMPTION, &powerConsumptionLength))
        {
            uint8_t *powerConsumptionPage = (uint8_t*)calloc(powerConsumptionLength, sizeof(uint8_t));
            if (!powerConsumptionPage)
            {
                return MEMORY_FAILURE;
            }
            if (SUCCESS == scsi_Inquiry(device, powerConsumptionPage, powerConsumptionLength, POWER_CONSUMPTION, true, false))
            {
                ret = SUCCESS;
                //now get all the power consumption descriptors into the struct
                identifiers->numberOfPCIdentifiers = (powerConsumptionLength - 4) / 4;
                uint16_t pcIter = 4, counter = 0;
                for (; pcIter < powerConsumptionLength; pcIter += 4, counter++)
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
            safe_Free(powerConsumptionPage);
        }
        if (ret != FAILURE)
        {
            uint8_t *pcModePage = (uint8_t*)calloc(MODE_PARAMETER_HEADER_10_LEN + 16, sizeof(uint8_t));
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
                    identifiers->currentIdentifier = pcModePage[MODE_PARAMETER_HEADER_10_LEN + 7];
                }
            }
            else
            {
                ret = NOT_SUPPORTED;
            }
            safe_Free(pcModePage);
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
                printf("Current Power Consumption Value: %"PRIu16" ", identifiers->identifiers[identifiers->currentIdentifier].value);
                //now print the units
                switch (identifiers->identifiers[identifiers->currentIdentifier].units)
                {
                case 0://gigawatts
                    printf("Gigawatts");
                    break;
                case 1://megawatts
                    printf("Megawatts");
                    break;
                case 2://kilowatts
                    printf("Kilowatts");
                    break;
                case 3://watts
                    printf("Watts");
                    break;
                case 4://milliwatts
                    printf("Milliwatts");
                    break;
                case 5://microwatts
                    printf("Microwatts");
                default:
                    printf("unknown unit of measure");
                    break;
                }
                printf("\n");
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
                uint64_t watts = identifiers->identifiers[pcIter].value;
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
                case 5://microwatts
                default:
                    continue;//continue the for loop
                    break;
                }
                printf(" %"PRIu64" |", watts);
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

int set_Power_Consumption(tDevice *device, ePCActiveLevel activeLevelField, uint8_t powerConsumptionIdentifier, bool resetToDefault)
{
    int ret = NOT_SUPPORTED;
    if (device->drive_info.drive_type == SCSI_DRIVE)
    {
        uint8_t *pcModePage = (uint8_t*)calloc(16 + MODE_PARAMETER_HEADER_10_LEN, sizeof(uint8_t));
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
                    pcModePage[MODE_PARAMETER_HEADER_10_LEN + 6] |= activeLevelField;
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
        safe_Free(pcModePage);
    }
    return ret;
}

int map_Watt_Value_To_Power_Consumption_Identifier(tDevice *device, double watts, uint8_t *pcIdentifier)
{
    int ret = NOT_SUPPORTED;
    powerConsumptionIdentifiers identifiers;
    memset(&identifiers, 0, sizeof(powerConsumptionIdentifiers));
    *pcIdentifier = 0xFF;//invalid
    uint64_t roundedWatts = (uint64_t)watts;
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
        uint8_t iter1 = 0, iter2 = identifiers.numberOfPCIdentifiers - 1;//subtract 1 since this is a 1 indexed counter.
        uint8_t pcId1 = 0xFF, pcId2 = 0xFF;
        uint64_t watts1 = 0, watts2 = 0;
        ret = NOT_SUPPORTED;
        for (; iter1 < identifiers.numberOfPCIdentifiers && iter2 >= 0; iter1++, iter2--)
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
                pcWatts1 /= 1000;
                break;
            case 5://microwatts
                pcWatts1 /= 1000000;
            default:
                ret = NOT_SUPPORTED;
                break;
            }
            switch (identifiers.identifiers[iter1].units)
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
                pcWatts2 /= 1000;
                break;
            case 5://microwatts
                pcWatts2 /= 1000000;
            default:
                ret = NOT_SUPPORTED;
                break;
            }
            if (pcWatts1 <= roundedWatts)
            {
                if (watts - watts1 > watts - pcWatts1)
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
                if (watts - watts2 > watts - pcWatts2)
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
        if (!exactMatchFound)
        {
            //now compare the best results between the two iterators to see which is closer to the best match, or is the best match
            //need to check which one is closer and select it
            if (watts - watts1 >= watts - watts2)
            {
                ret = SUCCESS;
                *pcIdentifier = pcId2;
            }
            else if (watts - watts1 <= watts - watts2)
            {
                ret = SUCCESS;
                *pcIdentifier = pcId1;
            }
        }
    }
    return ret;
}

int enable_Disable_APM_Feature(tDevice *device, bool enable)
{
    int ret = NOT_SUPPORTED;
    if(device->drive_info.drive_type == ATA_DRIVE)
    {
        //check the identify bits to make sure APM is supported.
        if(device->drive_info.IdentifyData.ata.Word083 & BIT3)
        {
            if(enable)
            {
                //subcommand 05..set value to 0x7F when requesting an enable operation so that it's a good mix of performance and power savings.
                ret = ata_Set_Features(device, SF_ENABLE_APM_FEATURE, 0x7F, 0, 0, 0);
            }
            else
            {
                //subcommand 85
                ret = ata_Set_Features(device, SF_DISABLE_APM_FEATURE, 0, 0, 0, 0);
                if(ret != SUCCESS)
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
int set_APM_Level(tDevice *device, uint8_t apmLevel)
{
    int ret = NOT_SUPPORTED;
    if(device->drive_info.drive_type == ATA_DRIVE)
    {
        //check the identify bits to make sure APM is supported.
        if(device->drive_info.IdentifyData.ata.Word083 & BIT3)
        {
            //subcommand 05 with the apmLevel in the count field
            ret = ata_Set_Features(device, SF_ENABLE_APM_FEATURE, apmLevel, 0, 0, 0);
        }
    }
    return ret;
}

int get_APM_Level(tDevice *device, uint8_t *apmLevel)
{
    int ret = NOT_SUPPORTED;
    if(device->drive_info.drive_type == ATA_DRIVE)
    {
        //check the identify bits to make sure APM is supported.
        if(device->drive_info.IdentifyData.ata.Word083 & BIT3)
        {
            //get it from identify device word 91
            ret = SUCCESS;
            *apmLevel = M_Byte0(device->drive_info.IdentifyData.ata.Word091);
        }
    }
    return ret;
}

int ata_Get_EPC_Settings(tDevice *device, ptrEpcSettings epcSettings)
{
    int ret = NOT_SUPPORTED;
    if (!epcSettings)
    {
        return BAD_PARAMETER;
    }
    uint32_t epcLogSize = LEGACY_DRIVE_SEC_SIZE * 2;//from ATA Spec
    //get_ATA_Log_Size(device, ATA_LOG_POWER_CONDITIONS, &epcLogSize, true, false) //uncomment this line to ask the drive for the EPC log size rather than use the hard coded value above.
    uint8_t *epcLog = (uint8_t*)calloc(epcLogSize * sizeof(uint8_t), sizeof(uint8_t));
    if (!epcLog)
    {
        return MEMORY_FAILURE;
    }
    if (SUCCESS == get_ATA_Log(device, ATA_LOG_POWER_CONDITIONS, NULL, NULL, true, false, true, epcLog, epcLogSize, NULL, epcLogSize,0))
    {
        ret = SUCCESS;
        for (uint32_t offset = 0; offset < (LEGACY_DRIVE_SEC_SIZE * 2); offset += 64)
        {
            ptrPowerCondition currentPowerCondition = NULL;
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
    safe_Free(epcLog);
    return ret;
}

int scsi_Get_EPC_Settings(tDevice *device, ptrEpcSettings epcSettings)
{
    int ret = NOT_SUPPORTED;
    if (!epcSettings)
    {
        return BAD_PARAMETER;
    }
    uint8_t epcVPDPage[VPD_POWER_CONDITION_LEN] = { 0 };
    if (SUCCESS == get_SCSI_VPD(device, POWER_CONDITION, NULL, NULL, true, epcVPDPage, VPD_POWER_CONDITION_LEN, NULL))
    {
        //idle a
        if (epcVPDPage[5] & BIT0)
        {
            epcSettings->idle_a.powerConditionSupported = true;
            epcSettings->idle_a.nominalRecoveryTimeToActiveState = M_BytesTo2ByteValue(epcVPDPage[12], epcVPDPage[13]);
        }
        //idle b
        if (epcVPDPage[5] & BIT1)
        {
            epcSettings->idle_b.powerConditionSupported = true;
            epcSettings->idle_b.nominalRecoveryTimeToActiveState = M_BytesTo2ByteValue(epcVPDPage[14], epcVPDPage[15]);
        }
        //idle c
        if (epcVPDPage[5] & BIT2)
        {
            epcSettings->idle_c.powerConditionSupported = true;
            epcSettings->idle_c.nominalRecoveryTimeToActiveState = M_BytesTo2ByteValue(epcVPDPage[16], epcVPDPage[17]);
        }
        //standby y
        if (epcVPDPage[4] & BIT0)
        {
            epcSettings->standby_y.powerConditionSupported = true;
            epcSettings->standby_y.nominalRecoveryTimeToActiveState = M_BytesTo2ByteValue(epcVPDPage[10], epcVPDPage[11]);
        }
        //standby z
        if (epcVPDPage[4] & BIT1)
        {
            epcSettings->standby_z.powerConditionSupported = true;
            epcSettings->standby_z.nominalRecoveryTimeToActiveState = M_BytesTo2ByteValue(epcVPDPage[8], epcVPDPage[9]);
        }
        //now time to read the mode pages for the other information (start with current, then saved, then default)
        uint8_t epcModePage[MP_POWER_CONDITION_LEN + MODE_PARAMETER_HEADER_10_LEN] = { 0 };
        for (eScsiModePageControl modePageControl = MPC_CURRENT_VALUES; modePageControl <= MPC_SAVED_VALUES; ++modePageControl)
        {
            memset(epcModePage, 0, MP_POWER_CONDITION_LEN + MODE_PARAMETER_HEADER_10_LEN);
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
                bool *idleAenabledBit = NULL;
                uint32_t *idleAtimerSetting = NULL;
                bool *idleBenabledBit = NULL;
                uint32_t *idleBtimerSetting = NULL;
                bool *idleCenabledBit = NULL;
                uint32_t *idleCtimerSetting = NULL;
                bool *standbyYenabledBit = NULL;
                uint32_t *standbyYtimerSetting = NULL;
                bool *standbyZenabledBit = NULL;
                uint32_t *standbyZtimerSetting = NULL;
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
                }
                *idleAtimerSetting = M_BytesTo4ByteValue(epcModePage[headerLength + 4], epcModePage[headerLength + 5], epcModePage[headerLength + 6], epcModePage[headerLength + 7]);
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
                if (epcModePage[2] & BIT0)
                {
                    *standbyYenabledBit = true;
                }
                *standbyYtimerSetting = M_BytesTo4ByteValue(epcModePage[headerLength + 20], epcModePage[headerLength + 21], epcModePage[headerLength + 22], epcModePage[headerLength + 23]);
                //standby z
                if (epcModePage[3] & BIT0)
                {
                    *standbyZenabledBit = true;
                }
                *standbyZtimerSetting = M_BytesTo4ByteValue(epcModePage[headerLength + 8], epcModePage[headerLength + 9], epcModePage[headerLength + 10], epcModePage[headerLength + 11]);
                ret = SUCCESS;
            }
        }
    }
    return ret;
}

int get_EPC_Settings(tDevice *device, ptrEpcSettings epcSettings)
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

void print_Power_Condition(ptrPowerCondition condition, char *conditionName)
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
    printf("%-12"PRIu32" ", condition->currentTimerSetting);
    if (condition->defaultTimerEnabled)
    {
        printf("*");
    }
    else
    {
        printf(" ");
    }
    printf("%-12"PRIu32" ", condition->defaultTimerSetting);
    if (condition->savedTimerEnabled)
    {
        printf("*");
    }
    else
    {
        printf(" ");
    }
    printf("%-12"PRIu32" ", condition->savedTimerSetting);
    printf("%-12"PRIu32" ", condition->nominalRecoveryTimeToActiveState);
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
    printf("\n===EPC Settings===\n");
    printf("\t* = timer is enabled\n");
    printf("\tC column = Changeable\n");
    printf("\tS column = Saveable\n");
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
}

int sata_Get_Device_Initiated_Interface_Power_State_Transitions(tDevice *device, bool *supported, bool *enabled)
{
    int ret = NOT_SUPPORTED;
    if ((device->drive_info.drive_type == ATA_DRIVE || device->drive_info.drive_type == ATAPI_DRIVE) && is_SATA(device))
    {
        ret = SUCCESS;
        if (supported)
        {
            if (!(device->drive_info.IdentifyData.ata.Word078 & BIT0) && device->drive_info.IdentifyData.ata.Word078 & BIT3)
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
            if (!(device->drive_info.IdentifyData.ata.Word079 & BIT0) && device->drive_info.IdentifyData.ata.Word079 & BIT3)
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

int sata_Set_Device_Initiated_Interface_Power_State_Transitions(tDevice *device, bool enable)
{
    int ret = NOT_SUPPORTED;
    if ((device->drive_info.drive_type == ATA_DRIVE || device->drive_info.drive_type == ATAPI_DRIVE) && is_SATA(device))
    {
        bool supported = false;
        if (SUCCESS == sata_Get_Device_Initiated_Interface_Power_State_Transitions(device, &supported, NULL))
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
                ata_Identify(device, (uint8_t*)&device->drive_info.IdentifyData.ata.Word000, LEGACY_DRIVE_SEC_SIZE);
            }
            else if (device->drive_info.drive_type == ATAPI_DRIVE)
            {
                ata_Identify_Packet_Device(device, (uint8_t*)&device->drive_info.IdentifyData.ata.Word000, LEGACY_DRIVE_SEC_SIZE);
            }
        }
    }
    return ret;
}

//TODO: Do we return a separate error if DIPM isn't enabled first? - TJE
int sata_Get_Device_Automatic_Partioan_To_Slumber_Transtisions(tDevice *device, bool *supported, bool *enabled)
{
    int ret = NOT_SUPPORTED;
    if ((device->drive_info.drive_type == ATA_DRIVE || device->drive_info.drive_type == ATAPI_DRIVE) && is_SATA(device))
    {
        ret = SUCCESS;
        if (supported)
        {
            if (!(device->drive_info.IdentifyData.ata.Word076 & BIT0) && device->drive_info.IdentifyData.ata.Word076 & BIT14)
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
            if (!(device->drive_info.IdentifyData.ata.Word079 & BIT0) && device->drive_info.IdentifyData.ata.Word079 & BIT7)
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

int sata_Set_Device_Automatic_Partioan_To_Slumber_Transtisions(tDevice *device, bool enable)
{
    int ret = NOT_SUPPORTED;
    if ((device->drive_info.drive_type == ATA_DRIVE || device->drive_info.drive_type == ATAPI_DRIVE) && is_SATA(device))
    {
        bool dipmEnabled = false;
        int getDIPM = sata_Get_Device_Initiated_Interface_Power_State_Transitions(device, NULL, &dipmEnabled);//DIPM must be ENABLED before we can change this feature!!
        if (getDIPM == SUCCESS && dipmEnabled)
        {
            bool supported = false;
            if (SUCCESS == sata_Get_Device_Automatic_Partioan_To_Slumber_Transtisions(device, &supported, NULL))
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
                    ata_Identify(device, (uint8_t*)&device->drive_info.IdentifyData.ata.Word000, LEGACY_DRIVE_SEC_SIZE);
                }
                else if (device->drive_info.drive_type == ATAPI_DRIVE)
                {
                    ata_Identify_Packet_Device(device, (uint8_t*)&device->drive_info.IdentifyData.ata.Word000, LEGACY_DRIVE_SEC_SIZE);
                }
            }
        }
        else
        {
            //TODO: do we return failure, not supported, or some other exit code? For not, returning NOT_SUPPORTED - TJE
        }
    }
    return ret;
}

int transition_To_Active(tDevice *device)
{
    int ret = NOT_SUPPORTED;
    if (device->drive_info.drive_type == ATA_DRIVE && device->drive_info.interface_type == IDE_INTERFACE)
    {
        //no ATA command to do this, so we need to issue something to perform a medium access.
        uint64_t randomLBA = 0;
        seed_64(time(NULL));
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

int transition_To_Standby(tDevice *device)
{
    int ret = NOT_SUPPORTED;
    if (device->drive_info.drive_type == ATA_DRIVE)
    {
        ret = ata_Standby_Immediate(device);
    }
    else //treat as SCSI
    {
        if (device->drive_info.scsiVersion > SCSI_VERSION_SCSI2)//checking for support after SCSI2. This isn't perfect, but should be ok for now.
        {
            ret = scsi_Start_Stop_Unit(device, false, 0, PC_FORCE_STANDBY_0, false, false, false);
        }
        else
        {
            //before you could specify a power condition, you used the "Start" bit as a way to move from standby to active
            ret = scsi_Start_Stop_Unit(device, false, 0, 0, false, false, false);
        }
    }
    return ret;
}

int transition_To_Idle(tDevice *device, bool unload)
{
    int ret = NOT_SUPPORTED;
    if (device->drive_info.drive_type == ATA_DRIVE)
    {
        if (unload)
        {
            if (device->drive_info.IdentifyData.ata.Word084 & BIT13 || device->drive_info.IdentifyData.ata.Word087 & BIT13)
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
                    ret = scsi_Start_Stop_Unit(device, false, 1, PC_FORCE_IDLE_0, false, false, false);
                }
            }
            else
            {
                ret = scsi_Start_Stop_Unit(device, false, 0, PC_FORCE_IDLE_0, false, false, false);
            }
        }
    }
    return ret;
}

int transition_To_Sleep(tDevice *device)
{
    int ret = NOT_SUPPORTED;
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
