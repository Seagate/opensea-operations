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
#include "operations.h" //for reset to defaults bit check

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
        uint8_t *identifyData = (uint8_t*)calloc_aligned(LEGACY_DRIVE_SEC_SIZE, sizeof(uint8_t), device->os_info.minimumAlignment);
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
        safe_Free_aligned(identifyData);

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
        uint8_t *senseData = (uint8_t*)calloc_aligned(SPC3_SENSE_LEN, sizeof(uint8_t), device->os_info.minimumAlignment);
        if (!senseData)
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
        safe_Free_aligned(senseData);
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
    int ret = NOT_SUPPORTED; 
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
        case PWR_CND_ACTIVE: //No such thing in ATA. Attempt by sending read-verify to a few sectors on the disk randomly
            seed_64(time(NULL));
            for (uint8_t counter = 0; counter < 5; ++counter)
            {
                uint64_t lba = 0;
                lba = random_Range_64(0, device->drive_info.deviceMaxLba);
                ata_Read_Verify(device, lba, 1);
            }
            //TODO: better way to judge if tried commands worked or not...
            //TODO: better handling for zoned devices...
            ret = SUCCESS;
            break;
        case PWR_CND_IDLE://send idle immediate
            ret = ata_Idle_Immediate(device, false);
            break;
        case PWR_CND_IDLE_UNLOAD://send idle immediate - unload
            if ((device->drive_info.IdentifyData.ata.Word084 != UINT16_MAX && device->drive_info.IdentifyData.ata.Word084 != 0 && device->drive_info.IdentifyData.ata.Word084 & BIT13) ||
                (device->drive_info.IdentifyData.ata.Word087 != UINT16_MAX && device->drive_info.IdentifyData.ata.Word087 != 0 && device->drive_info.IdentifyData.ata.Word087 & BIT13)
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
        };
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
        };
    }
    return ret;
}
#if !defined(DISABLE_NVME_PASSTHROUGH)
int transition_NVM_Power_State(tDevice *device, uint8_t newState)
{
    int ret = NOT_SUPPORTED;
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
#endif


int ata_Set_EPC_Power_Mode(tDevice *device, ePowerConditionID powerCondition, ptrPowerConditionSettings powerConditionSettings)
{
    int ret = SUCCESS;
    if (!powerConditionSettings || powerCondition == PWR_CND_ACTIVE)
    {
        return BAD_PARAMETER;
    }
    if (powerConditionSettings->powerConditionValid)
    {
        if (powerConditionSettings->restoreToDefault)
        {
            //this command is restoring the power conditions from the drive's default settings (bit6) and saving them upon completion (bit4)...the other option is to return to the saved settings, but we aren't going to support that with this option right now
            ret = ata_Set_Features(device, SF_EXTENDED_POWER_CONDITIONS, powerCondition, EPC_RESTORE_POWER_CONDITION_SETTINGS | BIT6 | BIT4, RESERVED, RESERVED);
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
                    uint64_t convertedMinutes = ((uint64_t)powerConditionSettings->timerInHundredMillisecondIncrements * UINT64_C(100)) / UINT64_C(60000);
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
            ret = ata_Set_Features(device, SF_EXTENDED_POWER_CONDITIONS, powerCondition, lbalo, lbaMid, lbaHi);
        }
    }
    return ret;
}

//enableDisable = true means enable, false means disable
int ata_Set_Device_Power_Mode(tDevice *device, bool restoreDefaults, bool enableDisable, \
    ePowerConditionID powerCondition, uint32_t powerModeTimer, bool powerModeTimerValid)
{
    int ret = UNKNOWN;
    //first verify the device supports the EPC feature
    uint8_t *ataDataBuffer = (uint8_t*)calloc_aligned(LEGACY_DRIVE_SEC_SIZE, sizeof(uint8_t), device->os_info.minimumAlignment);
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
            safe_Free_aligned(ataDataBuffer);
            return NOT_SUPPORTED;
        }
    }
    else
    {
        if (VERBOSITY_QUIET < device->deviceVerbosity)
        {
            printf("Failed to check if drive supports EPC feature!!\n");
        }
        safe_Free_aligned(ataDataBuffer);
        return FAILURE;
    }
    safe_Free_aligned(ataDataBuffer);
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
        powerSettings.timerValid = powerModeTimer;
        powerSettings.timerInHundredMillisecondIncrements = powerModeTimer;
    }
    ret = ata_Set_EPC_Power_Mode(device, powerCondition, &powerSettings);
    return ret;
}

//TODO: Only supports mode_Select_10. This should work 98% of the time. Add mode sense/select 6 commands??? May improve some devices, such as IEEE1394 or USB if this page is even supported by them (unlikely) - TJE
int scsi_Set_Power_Conditions(tDevice *device, bool restoreAllToDefaults, ptrPowerConditionTimers powerConditions)
{
    //TODO: This will need to work for both EPC and legacy modes, so no checking pages in advance or anything.
    //If restore all to defaults, then read the default mode page, then write it back.
    //TODO: Check the powerConditions structure to see if there are any specific timers to restore up front, then copy the default data into that timer structure so it can be written back.
    //Write all applicable changes back to the drive, checking each structure as it goes.
    int ret = SUCCESS;
    uint32_t powerConditionsPageLength = 0;
    uint8_t *powerConditionsPage = NULL;
    if (restoreAllToDefaults)
    {
        if (scsi_MP_Reset_To_Defaults_Supported(device))
        {
            ret = scsi_Mode_Select_10(device, 0, true, true, true, NULL, 0);//RTD bit is set and supported by the drive which will reset the page to defaults for us without a data transfer or multiple commands.
        }
        else
        {
            //read the default mode page, then send it to the drive with mode select.
            powerConditionsPageLength = MODE_PARAMETER_HEADER_10_LEN + MP_POWER_CONDITION_LEN;//*should* be maximum size we need assuming no block descriptor
            powerConditionsPage = (uint8_t*)calloc_aligned(powerConditionsPageLength, sizeof(uint8_t), device->os_info.minimumAlignment);
            if (!powerConditionsPage)
            {
                return MEMORY_FAILURE;
            }
            if (SUCCESS == (ret = scsi_Mode_Sense_10(device, MP_POWER_CONDTION, powerConditionsPageLength, 0, true, false, MPC_DEFAULT_VALUES, powerConditionsPage)))
            {
                //got the page, now send it to the drive with a mode select
                ret = scsi_Mode_Select_10(device, powerConditionsPageLength, true, true, false, powerConditionsPage, powerConditionsPageLength);
            }
            safe_Free_aligned(powerConditionsPage);
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
            powerConditionsPage = (uint8_t*)calloc_aligned(powerConditionsPageLength, sizeof(uint8_t), device->os_info.minimumAlignment);
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
                    powerConditions->idle_a.enable = (powerConditionsPage[mpStartOffset + 3] & BIT1) > 0 ? true : false;
                    powerConditions->idle_a.timerValid = true;
                    powerConditions->idle_a.timerInHundredMillisecondIncrements = M_BytesTo4ByteValue(powerConditionsPage[mpStartOffset + 4], powerConditionsPage[mpStartOffset + 5], powerConditionsPage[mpStartOffset + 6], powerConditionsPage[mpStartOffset + 7]);
                    powerConditions->idle_a.restoreToDefault = false;//turn this off now that we have the other settings stored.
                }
                if (powerConditions->standby_z.powerConditionValid && powerConditions->standby_z.restoreToDefault)
                {
                    powerConditions->standby_z.enableValid = true;
                    powerConditions->standby_z.enable = (powerConditionsPage[mpStartOffset + 3] & BIT0) > 0 ? true : false;
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
                        powerConditions->idle_b.enable = (powerConditionsPage[mpStartOffset + 3] & BIT2) > 0 ? true : false;
                        powerConditions->idle_b.timerValid = true;
                        powerConditions->idle_b.timerInHundredMillisecondIncrements = M_BytesTo4ByteValue(powerConditionsPage[mpStartOffset + 12], powerConditionsPage[mpStartOffset + 13], powerConditionsPage[mpStartOffset + 14], powerConditionsPage[mpStartOffset + 15]);
                        powerConditions->idle_b.restoreToDefault = false;//turn this off now that we have the other settings stored.
                    }
                    if (powerConditions->idle_c.powerConditionValid && powerConditions->idle_c.restoreToDefault)
                    {
                        powerConditions->idle_c.enableValid = true;
                        powerConditions->idle_c.enable = (powerConditionsPage[mpStartOffset + 3] & BIT3) > 0 ? true : false;
                        powerConditions->idle_c.timerValid = true;
                        powerConditions->idle_c.timerInHundredMillisecondIncrements = M_BytesTo4ByteValue(powerConditionsPage[mpStartOffset + 16], powerConditionsPage[mpStartOffset + 17], powerConditionsPage[mpStartOffset + 18], powerConditionsPage[mpStartOffset + 19]);
                        powerConditions->idle_c.restoreToDefault = false;//turn this off now that we have the other settings stored.
                    }
                    if (powerConditions->standby_y.powerConditionValid && powerConditions->standby_y.restoreToDefault)
                    {
                        powerConditions->standby_y.enableValid = true;
                        powerConditions->standby_y.enable = (powerConditionsPage[mpStartOffset + 2] & BIT0) > 0 ? true : false;
                        powerConditions->standby_y.timerValid = true;
                        powerConditions->standby_y.timerInHundredMillisecondIncrements = M_BytesTo4ByteValue(powerConditionsPage[mpStartOffset + 20], powerConditionsPage[mpStartOffset + 21], powerConditionsPage[mpStartOffset + 22], powerConditionsPage[mpStartOffset + 23]);
                        powerConditions->standby_y.restoreToDefault = false;//turn this off now that we have the other settings stored.
                    }
                    //TODO: Other future power modes fields here
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
                safe_Free_aligned(powerConditionsPage);
                return ret;
            }
            safe_Free_aligned(powerConditionsPage);
        }

        //Now, read the current settings mode page, make any necessary changes, then send it to the drive and we're done.
        powerConditionsPageLength = MODE_PARAMETER_HEADER_10_LEN + MP_POWER_CONDITION_LEN;//*should* be maximum size we need assuming no block descriptor
        powerConditionsPage = (uint8_t*)calloc_aligned(powerConditionsPageLength, sizeof(uint8_t), device->os_info.minimumAlignment);
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
                        M_SET_BIT(powerConditionsPage[mpStartOffset + 3], 1);
                    }
                    else
                    {
                        M_CLEAR_BIT(powerConditionsPage[mpStartOffset + 3], 1);
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
                        M_SET_BIT(powerConditionsPage[mpStartOffset + 3], 0);
                    }
                    else
                    {
                        M_CLEAR_BIT(powerConditionsPage[mpStartOffset + 3], 0);
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
                            M_SET_BIT(powerConditionsPage[mpStartOffset + 3], 2);
                        }
                        else
                        {
                            M_CLEAR_BIT(powerConditionsPage[mpStartOffset + 3], 2);
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
                            M_SET_BIT(powerConditionsPage[mpStartOffset + 3], 3);
                        }
                        else
                        {
                            M_CLEAR_BIT(powerConditionsPage[mpStartOffset + 3], 3);
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
                            M_SET_BIT(powerConditionsPage[mpStartOffset + 2], 0);
                        }
                        else
                        {
                            M_CLEAR_BIT(powerConditionsPage[mpStartOffset + 2], 0);
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
                //TODO: Other future power modes fields here
                //CCF fields
                if (powerConditions->checkConditionFlags.ccfIdleValid && !powerConditions->checkConditionFlags.ccfIdleResetDefault)
                {
                    powerConditionsPage[mpStartOffset + 39] |= powerConditions->checkConditionFlags.ccfIdleMode << 6;
                }
                if (powerConditions->checkConditionFlags.ccfStandbyValid && !powerConditions->checkConditionFlags.ccfStandbyResetDefault)
                {
                    powerConditionsPage[mpStartOffset + 39] |= powerConditions->checkConditionFlags.ccfStandbyMode << 4;
                }
                if (powerConditions->checkConditionFlags.ccfStopValid && !powerConditions->checkConditionFlags.ccfStopResetDefault)
                {
                    powerConditionsPage[mpStartOffset + 39] |= powerConditions->checkConditionFlags.ccfStopMode << 2;
                }
            }
            //send the modified data to the drive
            ret = scsi_Mode_Select_10(device, powerConditionsPageLength, true, true, false, powerConditionsPage, powerConditionsPageLength);
            safe_Free_aligned(powerConditionsPage);
        }
        else
        {
            safe_Free_aligned(powerConditionsPage);
            return ret;
        }
        safe_Free_aligned(powerConditionsPage);
    }
    safe_Free_aligned(powerConditionsPage);
    return ret;
}

int scsi_Set_EPC_Power_Conditions(tDevice *device, bool restoreAllToDefaults, ptrPowerConditionTimers powerConditions)
{
    //TODO: Check to make sure the changes being requested are supported by the device.
    return scsi_Set_Power_Conditions(device, restoreAllToDefaults, powerConditions);
}

//This function will go through and change each requested setting.
//The first failure that happens will cause the function to fail and not proceed to set any other timer values.
int ata_Set_EPC_Power_Conditions(tDevice *device, bool restoreAllToDefaults, ptrPowerConditionTimers powerConditions)
{
    int ret = NOT_SUPPORTED;
    if (device->drive_info.IdentifyData.ata.Word119 & BIT7)
    {
        //TODO: Should each of the settings be validated that it is supported before issuing to the drive???
        if (restoreAllToDefaults)
        {
            powerConditionSettings allSettings;
            memset(&allSettings, 0, sizeof(powerConditionSettings));
            allSettings.powerConditionValid = true;
            allSettings.restoreToDefault = true;
            ret = ata_Set_EPC_Power_Mode(device, EPC_POWER_CONDITION_ALL_POWER_CONDITIONS, &allSettings);
        }
        else
        {
            if (powerConditions)
            {
                //go through each and every power condition and for each valid one, pass it to the ATA function. If unsuccessful, return immediately
                //This does it in the same top-down order as the SCSI mode page to keep things working "the same" between the two
                if (powerConditions->idle_a.powerConditionValid)
                {
                    ret = ata_Set_EPC_Power_Mode(device, EPC_POWER_CONDITION_IDLE_A, &powerConditions->idle_a);
                    if (ret != SUCCESS)
                    {
                        return ret;
                    }
                }
                if (powerConditions->standby_z.powerConditionValid)
                {
                    ret = ata_Set_EPC_Power_Mode(device, EPC_POWER_CONDITION_STANDBY_Z, &powerConditions->idle_a);
                    if (ret != SUCCESS)
                    {
                        return ret;
                    }
                }
                if (powerConditions->idle_b.powerConditionValid)
                {
                    ret = ata_Set_EPC_Power_Mode(device, EPC_POWER_CONDITION_IDLE_B, &powerConditions->idle_a);
                    if (ret != SUCCESS)
                    {
                        return ret;
                    }
                }
                if (powerConditions->idle_c.powerConditionValid)
                {
                    ret = ata_Set_EPC_Power_Mode(device, EPC_POWER_CONDITION_IDLE_C, &powerConditions->idle_a);
                    if (ret != SUCCESS)
                    {
                        return ret;
                    }
                }
                if (powerConditions->standby_y.powerConditionValid)
                {
                    ret = ata_Set_EPC_Power_Mode(device, EPC_POWER_CONDITION_STANDBY_Y, &powerConditions->idle_a);
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
    return ret;
}

int set_EPC_Power_Conditions(tDevice *device, bool restoreAllToDefaults, ptrPowerConditionTimers powerConditions)
{
    int ret = NOT_SUPPORTED;
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
int scsi_Set_Device_Power_Mode(tDevice *device, bool restoreDefaults, bool enableDisable, \
    ePowerConditionID powerCondition, uint32_t powerModeTimer, bool powerModeTimerValid)
{
    int ret = NOT_SUPPORTED;
    //first we need to check that VPD page 8Ah (power condition) exists...and we can possibly use that information to return "not supported, etc"
    uint8_t *powerConditionVPD = (uint8_t*)calloc_aligned(VPD_POWER_CONDITION_LEN, sizeof(uint8_t), device->os_info.minimumAlignment);//size of 18 is defined in SPC4 for this VPD page
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
            safe_Free_aligned(powerConditionVPD);
            return FAILURE;
        }
    }
    else
    {
        if (VERBOSITY_QUIET < device->deviceVerbosity)
        {
            printf("Failed to check if drive supports modifying power conditions!\n");
        }
        safe_Free_aligned(powerConditionVPD);
        return FAILURE;
    }

    //Calling new scsi_Set_Power_Conditions() function since it's setup to handle EPC and legacy drives. Just need to setup structures in here then call that function properly.
    if (restoreDefaults)
    {
        if (powerCondition == PWR_CND_ALL)
        {
            ret = scsi_Set_Power_Conditions(device, true, NULL);
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
                safe_Free_aligned(powerConditionVPD);
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
                safe_Free_aligned(powerConditionVPD);
                return NOT_SUPPORTED;
            }
            powerConditions.idle_a.powerConditionValid = true;
            //check value for enable/disable bit
            powerConditions.idle_a.enableValid = true;
            powerConditions.idle_a.enable = enableDisable;
            //set the timer value
            powerConditions.idle_a.timerInHundredMillisecondIncrements = powerModeTimer;
            break;
        case PWR_CND_IDLE_B:
            if (!(powerConditionVPD[5] & BIT1))
            {
                safe_Free_aligned(powerConditionVPD);
                return NOT_SUPPORTED;
            }
            powerConditions.idle_b.powerConditionValid = true;
            //check value for enable/disable bit
            powerConditions.idle_b.enableValid = true;
            powerConditions.idle_b.enable = enableDisable;
            //set the timer value
            powerConditions.idle_b.timerInHundredMillisecondIncrements = powerModeTimer;
            break;
        case PWR_CND_IDLE_C:
            if (!(powerConditionVPD[5] & BIT2))
            {
                safe_Free_aligned(powerConditionVPD);
                return NOT_SUPPORTED;
            }
            powerConditions.idle_c.powerConditionValid = true;
            //check value for enable/disable bit
            powerConditions.idle_c.enableValid = true;
            powerConditions.idle_c.enable = enableDisable;
            //set the timer value
            powerConditions.idle_c.timerInHundredMillisecondIncrements = powerModeTimer;
            break;
        case PWR_CND_STANDBY_Y:
            if (!(powerConditionVPD[4] & BIT1))
            {
                safe_Free_aligned(powerConditionVPD);
                return NOT_SUPPORTED;
            }
            powerConditions.standby_y.powerConditionValid = true;
            //check value for enable/disable bit
            powerConditions.standby_y.enableValid = true;
            powerConditions.standby_y.enable = enableDisable;
            //set the timer value
            powerConditions.standby_y.timerInHundredMillisecondIncrements = powerModeTimer;
            break;
        case PWR_CND_STANDBY_Z:
            if (!(powerConditionVPD[4] & BIT0))
            {
                safe_Free_aligned(powerConditionVPD);
                return NOT_SUPPORTED;
            }
            powerConditions.standby_z.powerConditionValid = true;
            //check value for enable/disable bit
            powerConditions.standby_z.enableValid = true;
            powerConditions.standby_z.enable = enableDisable;
            //set the timer value
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
                powerConditions.standby_y.timerInHundredMillisecondIncrements = powerModeTimer;
            }
            else if (powerConditionVPD[4] & BIT0)//standby_z
            {
                powerConditions.standby_z.powerConditionValid = true;
                //check value for enable/disable bit
                powerConditions.standby_z.enableValid = true;
                powerConditions.standby_z.enable = enableDisable;
                //set the timer value
                powerConditions.standby_z.timerInHundredMillisecondIncrements = powerModeTimer;
            }
            else if (powerConditionVPD[5] & BIT2)//idle_c
            {
                powerConditions.idle_c.powerConditionValid = true;
                //check value for enable/disable bit
                powerConditions.idle_c.enableValid = true;
                powerConditions.idle_c.enable = enableDisable;
                //set the timer value
                powerConditions.idle_c.timerInHundredMillisecondIncrements = powerModeTimer;
            }
            else if (powerConditionVPD[5] & BIT1)//idle_b
            {
                powerConditions.idle_b.powerConditionValid = true;
                //check value for enable/disable bit
                powerConditions.idle_b.enableValid = true;
                powerConditions.idle_b.enable = enableDisable;
                //set the timer value
                powerConditions.idle_b.timerInHundredMillisecondIncrements = powerModeTimer;
            }
            else if (powerConditionVPD[5] & BIT0)//idle_a
            {
                powerConditions.idle_a.powerConditionValid = true;
                //check value for enable/disable bit
                powerConditions.idle_a.enableValid = true;
                powerConditions.idle_a.enable = enableDisable;
                //set the timer value
                powerConditions.idle_a.timerInHundredMillisecondIncrements = powerModeTimer;
            }
            break;
        default:
            safe_Free_aligned(powerConditionVPD);
            return BAD_PARAMETER;
        }
        ret = scsi_Set_Power_Conditions(device, false, &powerConditions);
    }
    safe_Free_aligned(powerConditionVPD);
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
            uint8_t *powerConsumptionPage = (uint8_t*)calloc_aligned(powerConsumptionLength, sizeof(uint8_t), device->os_info.minimumAlignment);
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
            safe_Free_aligned(powerConsumptionPage);
        }
        if (ret != FAILURE)
        {
            uint8_t *pcModePage = (uint8_t*)calloc_aligned(MODE_PARAMETER_HEADER_10_LEN + 16, sizeof(uint8_t), device->os_info.minimumAlignment);
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
            safe_Free_aligned(pcModePage);
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
        uint8_t *pcModePage = (uint8_t*)calloc_aligned(16 + MODE_PARAMETER_HEADER_10_LEN, sizeof(uint8_t), device->os_info.minimumAlignment);
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
        safe_Free_aligned(pcModePage);
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
    uint8_t *epcLog = (uint8_t*)calloc_aligned(epcLogSize * sizeof(uint8_t), sizeof(uint8_t), device->os_info.minimumAlignment);
    if (!epcLog)
    {
        return MEMORY_FAILURE;
    }
    if (SUCCESS == get_ATA_Log(device, ATA_LOG_POWER_CONDITIONS, NULL, NULL, true, false, true, epcLog, epcLogSize, NULL, epcLogSize,0))
    {
        ret = SUCCESS;
        for (uint32_t offset = 0; offset < (LEGACY_DRIVE_SEC_SIZE * 2); offset += 64)
        {
            ptrPowerConditionInfo currentPowerCondition = NULL;
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
    safe_Free_aligned(epcLog);
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

void print_Power_Condition(ptrPowerConditionInfo condition, char *conditionName)
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
}

//NOTE: This is intended for legacy drives that don't support extra timers for EPC. An EPC drive will still process this command though!!! - TJE
//More Notes: This function is similar to the EPC function, BUT this will not check for the VPD page as it doesn't exist on old drives.
//            These functions should probaby be combined at some point

int scsi_Set_Legacy_Power_Conditions(tDevice *device, bool restoreAllToDefaults, ptrPowerConditionSettings standbyTimer, ptrPowerConditionSettings idleTimer)
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
int ata_Set_Standby_Timer(tDevice *device, uint32_t hundredMillisecondIncrements)
{
    int ret = NOT_SUPPORTED;
    if (device->drive_info.IdentifyData.ata.Word049 & BIT13)//this is the only bit across all ATA standards that will most likely work. Prior to ATA3, there was no other support bit for the power management feature set.
    {
        uint8_t standbyTimer = 0;
        if (hundredMillisecondIncrements == 0)
        {
            //send standby immediate and return immediately
            return ata_Standby_Immediate(device);
        }
        else if (hundredMillisecondIncrements >= 1 && hundredMillisecondIncrements <= 12000)
        {
            standbyTimer = ((hundredMillisecondIncrements - 1) / 50) + 1;
        }
        else if (hundredMillisecondIncrements >= 12001 && hundredMillisecondIncrements <= 12600)
        {
            standbyTimer = 0xFC;
        }
        else if (hundredMillisecondIncrements >= 12601 && hundredMillisecondIncrements <= 12750)
        {
            standbyTimer = 0xFF;
        }
        else if (hundredMillisecondIncrements >= 12751 && hundredMillisecondIncrements <= 17999)
        {
            standbyTimer = 0xF1;
        }
        else if (hundredMillisecondIncrements >= 18000 && hundredMillisecondIncrements <= 198000)
        {
            standbyTimer = (hundredMillisecondIncrements / 18000) + 240;
        }
        else
        {
            standbyTimer = 0xFD;
        }
        //if we made it here, send a standby command with the count field set from above. Standby immediate case will already have returned.
        ret = ata_Standby(device, standbyTimer);
    }
    return ret;
}

int scsi_Set_Standby_Timer_State(tDevice *device, bool enable)
{
    powerConditionSettings standbyTimer;
    memset(&standbyTimer, 0, sizeof(powerConditionSettings));
    standbyTimer.powerConditionValid = true;
    standbyTimer.enableValid = true;
    standbyTimer.enable = enable;

    return scsi_Set_Legacy_Power_Conditions(device, false, &standbyTimer, NULL);
}

int set_Standby_Timer(tDevice *device, uint32_t hundredMillisecondIncrements, bool restoreToDefault)
{
    int ret = NOT_SUPPORTED;
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
        ret = scsi_Set_Legacy_Power_Conditions(device, false, &standbyTimer, NULL);
    }
    return ret;
}

int scsi_Set_Idle_Timer_State(tDevice *device, bool enable)
{
    powerConditionSettings idleTimer;
    memset(&idleTimer, 0, sizeof(powerConditionSettings));
    idleTimer.powerConditionValid = true;
    idleTimer.enableValid = true;
    idleTimer.enable = enable;

    return scsi_Set_Legacy_Power_Conditions(device, false, NULL, &idleTimer);
}

int set_Idle_Timer(tDevice *device, uint32_t hundredMillisecondIncrements, bool restoreToDefault)
{
    int ret = NOT_SUPPORTED;
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
        ret = scsi_Set_Legacy_Power_Conditions(device, false, NULL, &idleTimer);
    }
    return ret;
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

int scsi_Set_Partial_Slumber(tDevice *device, bool enablePartial, bool enableSlumber, bool partialValid, bool slumberValid, bool allPhys, uint8_t phyNumber)
{
    int ret = SUCCESS;
    if (!partialValid && !slumberValid)
    {
        return BAD_PARAMETER;
    }
    bool gotFullPageLength = false;
    uint16_t enhPhyControlLength = 0;
    uint8_t *enhSasPhyControl = (uint8_t*)calloc_aligned((MODE_PARAMETER_HEADER_10_LEN + enhPhyControlLength) * sizeof(uint8_t), sizeof(uint8_t), device->os_info.minimumAlignment);
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
        uint8_t *temp = realloc_aligned(enhSasPhyControl, 0, enhPhyControlLength, device->os_info.minimumAlignment);
        if (!temp)
        {
            return MEMORY_FAILURE;
        }
        enhSasPhyControl = temp;
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
                    for (uint16_t phyIter = 0; phyIter < (uint16_t)numberOfPhys; ++phyIter, phyDescriptorOffset += descriptorLength)
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
                                    M_SET_BIT(enhSasPhyControl[phyDescriptorOffset + 19], 1);
                                }
                                else
                                {
                                    M_CLEAR_BIT(enhSasPhyControl[phyDescriptorOffset + 19], 1);
                                }
                            }
                            if (slumberValid)
                            {
                                //byte 19, bit 2
                                if (enableSlumber)
                                {
                                    M_SET_BIT(enhSasPhyControl[phyDescriptorOffset + 19], 2);
                                }
                                else
                                {
                                    M_CLEAR_BIT(enhSasPhyControl[phyDescriptorOffset + 19], 2);
                                }
                            }
                        }
                    }
                    //we've finished making our changes to the mode page, so it's time to write it back!
                    if (SUCCESS != scsi_Mode_Select_10(device, (MODE_PARAMETER_HEADER_10_LEN + enhPhyControlLength), true, true, false, enhSasPhyControl, (MODE_PARAMETER_HEADER_10_LEN + enhPhyControlLength)))
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
    safe_Free_aligned(enhSasPhyControl);
    return ret;
}

int get_SAS_Enhanced_Phy_Control_Number_Of_Phys(tDevice *device, uint8_t *phyCount)
{
    int ret = SUCCESS;
    if (!phyCount)
    {
        return BAD_PARAMETER;
    }
    uint16_t enhPhyControlLength = 8;//only need 8 bytes to get the number of phys
    uint8_t *enhSasPhyControl = (uint8_t*)calloc_aligned((MODE_PARAMETER_HEADER_10_LEN + enhPhyControlLength) * sizeof(uint8_t), sizeof(uint8_t), device->os_info.minimumAlignment);
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
    safe_Free_aligned(enhSasPhyControl);
    return ret;
}

int get_SAS_Enhanced_Phy_Control_Partial_Slumber_Settings(tDevice *device, bool allPhys, uint8_t phyNumber, ptrSasEnhPhyControl enhPhyControlData, uint32_t enhPhyControlDataSize)
{
    int ret = SUCCESS;
    //make sure the structure that will be filled in makes sense at a quick check
    if (!enhPhyControlData || enhPhyControlDataSize == 0 || enhPhyControlDataSize % sizeof(sasEnhPhyControl))
    {
        return BAD_PARAMETER;
    }

    bool gotFullPageLength = false;
    uint16_t enhPhyControlLength = 0;
    uint8_t *enhSasPhyControl = (uint8_t*)calloc_aligned((MODE_PARAMETER_HEADER_10_LEN + enhPhyControlLength) * sizeof(uint8_t), sizeof(uint8_t), device->os_info.minimumAlignment);
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
        safe_Free_aligned(enhSasPhyControl);
        enhSasPhyControl = (uint8_t*)calloc_aligned((MODE_PARAMETER_HEADER_10_LEN + enhPhyControlLength) * sizeof(uint8_t), sizeof(uint8_t), device->os_info.minimumAlignment);
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
                    for (uint16_t phyIter = 0; phyIter < (uint16_t)numberOfPhys && (phyCounter * sizeof(sasEnhPhyControl)) < enhPhyControlDataSize; ++phyIter, phyDescriptorOffset += descriptorLength, ++phyCounter)
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
                        else if(phyNumber == phyIdentifier)
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
    safe_Free_aligned(enhSasPhyControl);

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